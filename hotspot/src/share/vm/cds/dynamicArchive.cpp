/*
 * Copyright (c) 2019, 2021, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2022, Huawei Technologies Co., Ltd. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "cds/archiveBuilder.hpp"
#include "cds/archiveUtils.hpp"
#include "cds/dynamicArchive.hpp"
#include "classfile/systemDictionaryShared.hpp"
#include "runtime/vm_operations.hpp"
#include "runtime/arguments.hpp"
#include "runtime/vmThread.hpp"
#include "memory/metaspaceShared.hpp"
#include "memory/filemap.hpp"
#include "memory/metaspaceClosure.hpp"
#include "utilities/exceptions.hpp"
#include "utilities/align.hpp"
#include "utilities/bitMap.hpp"
#include "utilities/exceptions.hpp"

class DynamicArchiveBuilder : public ArchiveBuilder {
public:
  static int dynamic_dump_method_comparator(Method* a, Method* b) {
    Symbol* a_name = a->name();
    Symbol* b_name = b->name();

    if (a_name == b_name) {
      return 0;
    }

    u4 a_offset = ArchiveBuilder::current()->any_to_offset_u4(a_name);
    u4 b_offset = ArchiveBuilder::current()->any_to_offset_u4(b_name);

    if (a_offset < b_offset) {
      return -1;
    } else {
      assert(a_offset > b_offset, "must be");
      return 1;
    }
  }

public:
  FileMapInfo::DynamicArchiveHeader* _header;

  void init_header();
  void release_header();
  void sort_methods();
  void sort_methods(InstanceKlass* ik) const;
  void remark_pointers_for_instance_klass(InstanceKlass* k, bool should_mark) const;
  void write_archive(char* serialized_data);
  virtual void iterate_roots(MetaspaceClosure* it, bool is_relocating_pointers) {
    SystemDictionaryShared::dumptime_classes_do(it);
  }

  // Do this before and after the archive dump to see if any corruption
  // is caused by dynamic dumping.
  void verify_universe(const char* info) {
    if (VerifyBeforeExit) {
      if (InfoDynamicCDS) {
        dynamic_cds_log->print_cr("Verify %s", info);
      }
      // Among other things, this ensures that Eden top is correct.
      Universe::heap()->prepare_for_verify();
      Universe::verify(info);
    }
  }

  void doit() {
    SystemDictionaryShared::start_dumping();

    verify_universe("Before CDS dynamic dump");
    DEBUG_ONLY(SystemDictionaryShared::NoClassLoadingMark nclm);

    // No need DumpTimeTable_lock, since jdk8 doesn't support jcmd dump.
    // Just remains this lock.
    MutexLockerEx ml(DumpTimeTable_lock, Mutex::_no_safepoint_check_flag);
    SystemDictionaryShared::check_excluded_classes();
    SystemDictionaryShared::replace_klass_in_constantPool();

    init_header();
    gather_source_objs();
    if (klasses()->length() == 0) {
      if (InfoDynamicCDS) {
        dynamic_cds_log->print_cr("No classes gathered, so do not generate Dynamic CDS jsa");
      }
      return;
    }
    reserve_buffer();

    if (InfoDynamicCDS) {
      dynamic_cds_log->print_cr("Copying %d klasses and %d symbols",
                    klasses()->length(), symbols()->length());
    }
    dump_rw_metadata();
    dump_ro_metadata();
    relocate_metaspaceobj_embedded_pointers();
    relocate_roots();

    verify_estimate_size(_estimated_metaspaceobj_bytes, "MetaspaceObjs");

    char* serialized_data;
    {
      // Write the symbol table and system dictionaries to the RO space.
      // Note that these tables still point to the *original* objects, so
      // they would need to get the correct addresses.
      assert(current_dump_space() == ro_region(), "Must be RO space");
      SymbolTable::write_to_archive(symbols());

      ArchiveBuilder::OtherROAllocMark mark;
      SystemDictionaryShared::write_to_archive();

      serialized_data = ro_region()->top();
      DynamicWriteClosure wc(ro_region());
      SymbolTable::serialize_shared_table_header(&wc);
      SystemDictionaryShared::serialize_dictionary_headers(&wc);
    }

    verify_estimate_size(_estimated_hashtable_bytes, "Hashtables");

    sort_methods();

    if (InfoDynamicCDS) {
      dynamic_cds_log->print_cr("Make classes shareable");
    }
    make_klasses_shareable();

    patch_shared_obj_vtable();

    relocate_to_requested();

    dump_md_metadata();
    write_archive(serialized_data);
    release_header();

    assert(_num_dump_regions_used == _total_dump_regions, "must be");
    verify_universe("After CDS dynamic dump");
  }
};

void DynamicArchiveBuilder::init_header() {
  FileMapInfo* mapinfo = new FileMapInfo(false);
  assert(FileMapInfo::dynamic_info() == mapinfo, "must be");
  _header = mapinfo->dynamic_header();

  FileMapInfo* base_info = FileMapInfo::current_info();
  _header->set_base_header_crc(base_info->header()->crc());
  for (int i = 0; i < MetaspaceShared::n_regions; i++) {
    _header->set_base_region_crc(i, base_info->header()->space_crc(i));
  }

  _header->populate(base_info, base_info->alignment());
}

void DynamicArchiveBuilder::release_header() {
  // We temporarily allocated a dynamic FileMapInfo for dumping, which makes it appear we
  // have mapped a dynamic archive, but we actually have not. We are in a safepoint now.
  // Let's free it so that if class loading happens after we leave the safepoint, nothing
  // bad will happen.
  assert(SafepointSynchronize::is_at_safepoint(), "must be");
  FileMapInfo *mapinfo = FileMapInfo::dynamic_info();
  assert(mapinfo != NULL && _header == mapinfo->dynamic_header(), "must be");
  delete mapinfo;
  assert(!DynamicArchive::is_mapped(), "must be");
  _header = NULL;
}

void DynamicArchiveBuilder::sort_methods() {
  // Because high version support jcmd dynamic cds dump, jvm need go on after dump.
  // Jdk8 no need as so, just exit after dump.
  InstanceKlass::disable_method_binary_search();
  for (int i = 0; i < klasses()->length(); i++) {
    Klass* k = klasses()->at(i);
    if (k->oop_is_instance()) {
      sort_methods(InstanceKlass::cast(k));
    }
  }
}

// The address order of the copied Symbols may be different than when the original
// klasses were created. Re-sort all the tables. See Method::sort_methods().
void DynamicArchiveBuilder::sort_methods(InstanceKlass* ik) const {
  assert(ik != NULL, "DynamicArchiveBuilder currently doesn't support dumping the base archive");
  if (MetaspaceShared::is_in_shared_space(ik)) {
    // We have reached a supertype that's already in the base archive
    return;
  }

  if (ik->java_mirror() == NULL) {
    // NULL mirror means this class has already been visited and methods are already sorted
    return;
  }
  ik->remove_java_mirror();

  if (DebugDynamicCDS) {
    ResourceMark rm;
    dynamic_cds_log->print_cr("sorting methods for " PTR_FORMAT " (" PTR_FORMAT ") %s",
                  p2i(ik), p2i(to_requested(ik)), ik->external_name());
  }
  // Method sorting may re-layout the [iv]tables, which would change the offset(s)
  // of the locations in an InstanceKlass that would contain pointers. Let's clear
  // all the existing pointer marking bits, and re-mark the pointers after sorting.
  remark_pointers_for_instance_klass(ik, false);

  // Make sure all supertypes have been sorted
  sort_methods(ik->java_super());
  Array<Klass*>* interfaces = ik->local_interfaces();
  int len = interfaces->length();
  for (int i = 0; i < len; i++) {
    sort_methods(InstanceKlass::cast(interfaces->at(i)));
  }

#ifdef ASSERT
  if (ik->methods() != NULL) {
    for (int m = 0; m < ik->methods()->length(); m++) {
      Symbol* name = ik->methods()->at(m)->name();
      assert(MetaspaceShared::is_in_shared_space(name) || is_in_buffer_space(name), "must be");
    }
  }
  if (ik->default_methods() != NULL) {
    for (int m = 0; m < ik->default_methods()->length(); m++) {
      Symbol* name = ik->default_methods()->at(m)->name();
      assert(MetaspaceShared::is_in_shared_space(name) || is_in_buffer_space(name), "must be");
    }
  }
#endif

  Method::sort_methods(ik->methods(), /*idempotent=*/false, /*set_idnums=*/true, dynamic_dump_method_comparator);
  if (ik->default_methods() != NULL) {
    Method::sort_methods(ik->default_methods(), /*idempotent=*/false, /*set_idnums=*/false, dynamic_dump_method_comparator);
  }

  EXCEPTION_MARK;

  ik->vtable()->initialize_vtable(false, CATCH);  // No need checkconstraints
  CLEAR_PENDING_EXCEPTION;
  ik->itable()->initialize_itable(false, CATCH);
  CLEAR_PENDING_EXCEPTION;

  // Set all the pointer marking bits after sorting.
  remark_pointers_for_instance_klass(ik, true);
}

template<bool should_mark>
class PointerRemarker: public MetaspaceClosure {
public:
  virtual bool do_ref(Ref* ref, bool read_only) {
    if (should_mark) {
      ArchivePtrMarker::mark_pointer(ref->addr());
    } else {
      ArchivePtrMarker::clear_pointer(ref->addr());
    }
    return false; // don't recurse
  }
};

void DynamicArchiveBuilder::remark_pointers_for_instance_klass(InstanceKlass* k, bool should_mark) const {
  if (should_mark) {
    PointerRemarker<true> marker;
    k->metaspace_pointers_do(&marker);
    marker.finish();
  } else {
    PointerRemarker<false> marker;
    k->metaspace_pointers_do(&marker);
    marker.finish();
  }
}

void DynamicArchiveBuilder::write_archive(char* serialized_data) {
  _header->set_serialized_data(serialized_data);

  FileMapInfo* dynamic_info = FileMapInfo::dynamic_info();
  assert(dynamic_info != NULL, "Sanity");

  // Update file offset
  ArchiveBuilder::write_archive(dynamic_info);

  // Write into file
  dynamic_info->open_for_write();
  dynamic_info->set_requested_base((char*)MetaspaceShared::requested_base_address());
  dynamic_info->set_header_base_archive_name_size(strlen(Arguments::GetSharedArchivePath()) + 1);
  dynamic_info->set_header_crc(dynamic_info->compute_header_crc());
  ArchiveBuilder::write_archive(dynamic_info);

  address base = _requested_dynamic_archive_bottom;
  address top  = _requested_dynamic_archive_top;
  size_t file_size = pointer_delta(top, base, sizeof(char));

  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("Written dynamic archive " PTR_FORMAT " - " PTR_FORMAT
                           " , " SIZE_FORMAT " bytes total]",
                           p2i(base), p2i(top), file_size);

    dynamic_cds_log->print_cr("%d klasses; %d symbols", klasses()->length(), symbols()->length());
  }
}

class VM_GC_Sync_Operation : public VM_Operation {
public:

  VM_GC_Sync_Operation() : VM_Operation() { }

  // Acquires the Heap_lock.
  virtual bool doit_prologue() {
    Heap_lock->lock();
    return true;
  }
  // Releases the Heap_lock.
  virtual void doit_epilogue() {
    Heap_lock->unlock();
  }
};

class VM_PopulateDynamicDumpSharedSpace : public VM_GC_Sync_Operation {
  DynamicArchiveBuilder builder;
public:
  VM_PopulateDynamicDumpSharedSpace() : VM_GC_Sync_Operation() {}
  VMOp_Type type() const { return VMOp_PopulateDumpSharedSpace; }
  void doit() {
    if (DynamicDumpSharedSpaces == false) {
      return;
    }
    ResourceMark rm;

    if (SystemDictionaryShared::empty_dumptime_table()) {
      tty->print_cr("There is no class to be included in the dynamic archive.");
      return;
    }

    builder.doit();

    DynamicDumpSharedSpaces = false;
    exit(0);
  }
};

bool DynamicArchive::_has_been_dumped_once = false;

void DynamicArchive::prepare_for_dynamic_dumping_at_exit() {
  {
    MutexLockerEx ml(DumpTimeTable_lock, Mutex::_no_safepoint_check_flag);
    if (DynamicArchive::has_been_dumped_once()) {
      return;
    } else {
      DynamicArchive::set_has_been_dumped_once();
    }
  }
  EXCEPTION_MARK;
  ResourceMark rm(THREAD);
  MetaspaceShared::link_and_cleanup_shared_classes(THREAD);

    if (HAS_PENDING_EXCEPTION) {
      tty->print_cr("ArchiveClassesAtExit has failed");
      tty->print_cr("%s: %s", PENDING_EXCEPTION->klass()->external_name(),
                    java_lang_String::as_utf8_string(java_lang_Throwable::message(PENDING_EXCEPTION)));
      // We cannot continue to dump the archive anymore.
      DynamicDumpSharedSpaces = false;
      CLEAR_PENDING_EXCEPTION;
    }
}

void DynamicArchive::dump() {
  if (Arguments::GetSharedDynamicArchivePath() == NULL) {
    tty->print_cr("SharedDynamicArchivePath is not specified");
    return;
  }

  VM_PopulateDynamicDumpSharedSpace op;
  VMThread::execute(&op);
}

bool DynamicArchive::validate(FileMapInfo* dynamic_info) {
  assert(!dynamic_info->is_static(), "must be");
  // Check if the recorded base archive matches with the current one
  FileMapInfo* base_info = FileMapInfo::current_info();
  FileMapInfo::DynamicArchiveHeader* dynamic_header = dynamic_info->dynamic_header();

  // Check the header crc
  if (dynamic_header->base_header_crc() != base_info->crc()) {
    FileMapInfo::fail_continue("Dynamic archive cannot be used: static archive header checksum verification failed.");
    return false;
  }

  // Check each space's crc
  for (int i = 0; i < MetaspaceShared::n_regions; i++) {
    if (dynamic_header->base_region_crc(i) != base_info->space_crc(i)) {
      FileMapInfo::fail_continue("Dynamic archive cannot be used: static archive region #%d checksum verification failed.", i);
      return false;
    }
  }

  return true;
}
