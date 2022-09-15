/*
 * Copyright (c) 2020, 2021, Oracle and/or its affiliates. All rights reserved.
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
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionaryShared.hpp"
#include "interpreter/abstractInterpreter.hpp"
#include "memory/filemap.hpp"
#include "memory/memRegion.hpp"
#include "memory/metaspaceShared.hpp"
#include "memory/resourceArea.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/objArrayKlass.hpp"
#include "runtime/arguments.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/thread.hpp"
#include "utilities/align.hpp"
#include "utilities/bitMap.inline.hpp"
#include "utilities/hashtable.inline.hpp"

ArchiveBuilder* ArchiveBuilder::_current = NULL;

ArchiveBuilder::OtherROAllocMark::~OtherROAllocMark() {
  char* newtop = ArchiveBuilder::current()->_ro_region.top();
  ArchiveBuilder::alloc_stats()->record_other_type(int(newtop - _oldtop), true);
}

ArchiveBuilder::SourceObjList::SourceObjList() : _ptrmap(16 * K, false) {
  _total_bytes = 0;
  _objs = new (ResourceObj::C_HEAP, mtClassShared) GrowableArray<SourceObjInfo*>(128 * K, mtClassShared);
}

ArchiveBuilder::SourceObjList::~SourceObjList() {
  delete _objs;
}

static void caculate_fingerprint(Klass * klass) {
  if (klass->oop_is_instance()) {
    InstanceKlass* ik = InstanceKlass::cast(klass);
    for (int i = 0; i < ik->methods()->length(); i++) {
      Method* m = ik->methods()->at(i);
      Fingerprinter fp(m);
      // The side effect of this call sets method's fingerprint field.
      fp.fingerprint();
    }
  }
}

void ArchiveBuilder::SourceObjList::append(MetaspaceClosure::Ref* enclosing_ref, SourceObjInfo* src_info) {
  // Save this source object for copying
  _objs->append(src_info);

  // Prepare for marking the pointers in this source object
  assert(is_aligned(_total_bytes, sizeof(address)), "must be");
  src_info->set_ptrmap_start(_total_bytes / sizeof(address));
  _total_bytes = align_up(_total_bytes + (uintx)src_info->size_in_bytes(), sizeof(address));
  src_info->set_ptrmap_end(_total_bytes / sizeof(address));

  BitMap::idx_t bitmap_size_needed = BitMap::idx_t(src_info->ptrmap_end());
  if (_ptrmap.size() <= bitmap_size_needed) {
    _ptrmap.resize((bitmap_size_needed + 1) * 2, false);
  }
}

class PrintBitMap : public BitMapClosure {
  public:
  bool do_bit(BitMap::idx_t bit_offset) {
    tty->print_cr("PrintBitMap : " SIZE_FORMAT, bit_offset);
    return true;
  }
};

void ArchiveBuilder::SourceObjList::remember_embedded_pointer(SourceObjInfo* src_info, MetaspaceClosure::Ref* ref) {
  // src_obj contains a pointer. Remember the location of this pointer in _ptrmap,
  // so that we can copy/relocate it later. E.g., if we have
  //    class Foo { intx scala; Bar* ptr; }
  //    Foo *f = 0x100;
  // To mark the f->ptr pointer on 64-bit platform, this function is called with
  //    src_info()->obj() == 0x100
  //    ref->addr() == 0x108
  address src_obj = src_info->obj();
  address* field_addr = ref->addr();
  assert(src_info->ptrmap_start() < _total_bytes, "sanity");
  assert(src_info->ptrmap_end() <= _total_bytes, "sanity");
  assert(*field_addr != NULL, "should have checked");

  intx field_offset_in_bytes = ((address)field_addr) - src_obj;
  DEBUG_ONLY(int src_obj_size = src_info->size_in_bytes();)
  assert(field_offset_in_bytes >= 0, "must be");
  assert(field_offset_in_bytes + intx(sizeof(intptr_t)) <= intx(src_obj_size), "must be");
  assert(is_aligned(field_offset_in_bytes, sizeof(address)), "must be");

  BitMap::idx_t idx = BitMap::idx_t(src_info->ptrmap_start() + (uintx)(field_offset_in_bytes / sizeof(address)));
  if (TraceDynamicCDS) {
    dynamic_cds_log->print_cr("remember_embedded_pointer: _ptrmap_start: " SIZE_FORMAT
                                               "_ptrmap_end: " SIZE_FORMAT
                                               " field: " PTR_FORMAT" ->  " PTR_FORMAT
                                               " bit_index: " SIZE_FORMAT " ",
                                               src_info->ptrmap_start(), src_info->ptrmap_end(), p2i(src_obj), p2i(field_addr), idx);
  }
  _ptrmap.set_bit(BitMap::idx_t(idx));
}

class RelocateEmbeddedPointers : public BitMapClosure {
  ArchiveBuilder* _builder;
  address _dumped_obj;
  BitMap::idx_t _start_idx;
public:
  RelocateEmbeddedPointers(ArchiveBuilder* builder, address dumped_obj, BitMap::idx_t start_idx) :
    _builder(builder), _dumped_obj(dumped_obj), _start_idx(start_idx) {}

  bool do_bit(BitMap::idx_t bit_offset) {
    uintx FLAG_MASK = 0x03; // See comments around MetaspaceClosure::FLAG_MASK
    size_t field_offset = size_t(bit_offset - _start_idx) * sizeof(address);
    address* ptr_loc = (address*)(_dumped_obj + field_offset);
    uintx old_p_and_bits = (uintx)(*ptr_loc);
    uintx flag_bits = (old_p_and_bits & FLAG_MASK);
    address old_p = (address)(old_p_and_bits & (~FLAG_MASK));
    address new_p = _builder->get_dumped_addr(old_p);
    uintx new_p_and_bits = ((uintx)new_p) | flag_bits;

    if (TraceDynamicCDS) {
      dynamic_cds_log->print_cr("Ref: [" PTR_FORMAT "] -> " PTR_FORMAT " => " PTR_FORMAT,
                    p2i(ptr_loc), p2i(old_p), p2i(new_p));
    }
    ArchivePtrMarker::set_and_mark_pointer(ptr_loc, (address)(new_p_and_bits));
    return true; // keep iterating the bitmap
  }
};

void ArchiveBuilder::SourceObjList::relocate(int i, ArchiveBuilder* builder) {
  SourceObjInfo* src_info = objs()->at(i);
  assert(src_info->should_copy(), "must be");
  BitMap::idx_t start = BitMap::idx_t(src_info->ptrmap_start()); // inclusive
  BitMap::idx_t end = BitMap::idx_t(src_info->ptrmap_end());     // exclusive

  RelocateEmbeddedPointers relocator(builder, src_info->dumped_addr(), start);
  _ptrmap.iterate(&relocator, start, end);
}

ArchiveBuilder::ArchiveBuilder() :
  _current_dump_space(NULL),
  _buffer_bottom(NULL),
  _last_verified_top(NULL),
  _num_dump_regions_used(0),
  _other_region_used_bytes(0),
  _requested_static_archive_bottom(NULL),
  _requested_static_archive_top(NULL),
  _requested_dynamic_archive_bottom(NULL),
  _requested_dynamic_archive_top(NULL),
  _mapped_static_archive_bottom(NULL),
  _mapped_static_archive_top(NULL),
  _buffer_to_requested_delta(0),
  _rw_region("rw", MAX_SHARED_DELTA),
  _ro_region("ro", MAX_SHARED_DELTA),
  _md_region("md", MAX_SHARED_DELTA),
  _rw_src_objs(),
  _ro_src_objs(),
  _src_obj_table(INITIAL_TABLE_SIZE),
  _num_instance_klasses(0),
  _num_obj_array_klasses(0),
  _num_type_array_klasses(0),
  _estimated_metaspaceobj_bytes(0),
  _estimated_hashtable_bytes(0) {
  _klasses = new (ResourceObj::C_HEAP, mtClassShared) GrowableArray<Klass*>(4 * K, mtClassShared);
  _symbols = new (ResourceObj::C_HEAP, mtClassShared) GrowableArray<Symbol*>(256 * K, mtClassShared);

  assert(_current == NULL, "must be");
  _current = this;
}

ArchiveBuilder::~ArchiveBuilder() {
  assert(_current == this, "must be");
  _current = NULL;

  clean_up_src_obj_table();

  for (int i = 0; i < _symbols->length(); i++) {
    _symbols->at(i)->decrement_refcount();
  }

  delete _klasses;
  delete _symbols;
  if (_shared_rs.is_reserved()) {
    _shared_rs.release();
  }
}

bool ArchiveBuilder::gather_one_source_obj(MetaspaceClosure::Ref* enclosing_ref,
                                           MetaspaceClosure::Ref* ref, bool read_only) {
  address src_obj = ref->obj();
  if (src_obj == NULL) {
    return false;
  }
  ref->set_keep_after_pushing();
  remember_embedded_pointer_in_copied_obj(enclosing_ref, ref);

  FollowMode follow_mode = get_follow_mode(ref);
  SourceObjInfo src_info(ref, read_only, follow_mode);
  bool created;
  SourceObjInfo* p = _src_obj_table.add_if_absent(src_obj, src_info, &created);
  if (created) {
    if (_src_obj_table.maybe_grow(MAX_TABLE_SIZE)) {
      if (InfoDynamicCDS) {
        dynamic_cds_log->print_cr("Expanded _src_obj_table table to %d", _src_obj_table.table_size());
      }
    }
  }

  assert(p->read_only() == src_info.read_only(), "must be");

  if (created && src_info.should_copy()) {
    ref->set_user_data((void*)p);
    if (read_only) {
      _ro_src_objs.append(enclosing_ref, p);
    } else {
      _rw_src_objs.append(enclosing_ref, p);
    }
    return true; // Need to recurse into this ref only if we are copying it
  } else {
    return false;
  }
}

void ArchiveBuilder::iterate_sorted_roots(MetaspaceClosure* it, bool is_relocating_pointers) {
  int i;

  if (!is_relocating_pointers) {
    // Don't relocate _symbol, so we can safely call decrement_refcount on the
    // original symbols.
    int num_symbols = _symbols->length();
    for (i = 0; i < num_symbols; i++) {
      it->push(_symbols->adr_at(i));
    }
  }

  int num_klasses = _klasses->length();
  for (i = 0; i < num_klasses; i++) {
    it->push(_klasses->adr_at(i));
  }

  iterate_roots(it, is_relocating_pointers);
}

class GatherSortedSourceObjs : public MetaspaceClosure {
  ArchiveBuilder* _builder;

public:
  GatherSortedSourceObjs(ArchiveBuilder* builder) : _builder(builder) {}

  virtual bool do_ref(Ref* ref, bool read_only) {
    return _builder->gather_one_source_obj(enclosing_ref(), ref, read_only);
  }

  virtual void do_pending_ref(Ref* ref) {
    if (ref->obj() != NULL) {
      _builder->remember_embedded_pointer_in_copied_obj(enclosing_ref(), ref);
    }
  }
};

void ArchiveBuilder::gather_source_objs() {
  ResourceMark rm;
  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("Gathering all archivable objects ... ");
  }
  gather_klasses_and_symbols();
  GatherSortedSourceObjs doit(this);
  iterate_sorted_roots(&doit, /*is_relocating_pointers=*/false);
  doit.finish();
}

bool ArchiveBuilder::is_excluded(Klass* klass) {
  if (klass->oop_is_instance()) {
    InstanceKlass* ik = InstanceKlass::cast(klass);
    return SystemDictionaryShared::is_excluded_class(ik);
  } else if (klass->oop_is_objArray()) {
    if (DynamicDumpSharedSpaces) {
      // Don't support archiving of array klasses for now (WHY???)
      return true;
    }
    Klass* bottom = ObjArrayKlass::cast(klass)->bottom_klass();
    if (bottom->oop_is_instance()) {
      return SystemDictionaryShared::is_excluded_class(InstanceKlass::cast(bottom));
    }
  }

  return false;
}

ArchiveBuilder::FollowMode ArchiveBuilder::get_follow_mode(MetaspaceClosure::Ref *ref) {
  address obj = ref->obj();
  if (MetaspaceShared::is_in_shared_space(obj)) {
    // Don't dump existing shared metadata again.
    return point_to_it;
  } else if (ref->msotype() == MetaspaceObj::MethodDataType) {
    return set_to_null;
  } else {
    if (ref->msotype() == MetaspaceObj::ClassType) {
      Klass* klass = (Klass*)ref->obj();
      assert(klass->is_klass(), "must be");
      if (is_excluded(klass)) {
        if (TraceDynamicCDS) {
          ResourceMark rm;
          dynamic_cds_log->print_cr("Skipping class (excluded): %s", klass->external_name());
        }
        return set_to_null;
      }
    }

    return make_a_copy;
  }
}

int ArchiveBuilder::compare_symbols_by_address(Symbol** a, Symbol** b) {
  if (a[0] < b[0]) {
    return -1;
  } else {
    assert(a[0] > b[0], "Duplicated symbol unexpected");
    return 1;
  }
}

int ArchiveBuilder::compare_klass_by_name(Klass** a, Klass** b) {
  return a[0]->name()->fast_compare(b[0]->name());
}

void ArchiveBuilder::sort_klasses() {
  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("Sorting classes ... ");
  }
  _klasses->sort(compare_klass_by_name);
}

class GatherKlassesAndSymbols : public UniqueMetaspaceClosure {
  ArchiveBuilder* _builder;

public:
  GatherKlassesAndSymbols(ArchiveBuilder* builder) : _builder(builder) { }

  virtual bool do_unique_ref(Ref* ref, bool read_only) {
    return _builder->gather_klass_and_symbol(ref, read_only);
  }
};

void ArchiveBuilder::gather_klasses_and_symbols() {
  ResourceMark rm;
  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("Gathering classes and symbols ... ");
  }
  GatherKlassesAndSymbols doit(this);
  iterate_roots(&doit, false);
  doit.finish();

  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("Number of classes %d", _num_instance_klasses + _num_obj_array_klasses + _num_type_array_klasses);
    dynamic_cds_log->print_cr("    instance classes   = %5d", _num_instance_klasses);
    dynamic_cds_log->print_cr("    obj array classes  = %5d", _num_obj_array_klasses);
    dynamic_cds_log->print_cr("    type array classes = %5d", _num_type_array_klasses);
    dynamic_cds_log->print_cr("               symbols = %5d", _symbols->length());
  }
}

bool ArchiveBuilder::gather_klass_and_symbol(MetaspaceClosure::Ref* ref, bool read_only) {
  if (ref->obj() == NULL) {
    return false;
  }
  if (get_follow_mode(ref) != make_a_copy) {
    return false;
  }
  if (ref->msotype() == MetaspaceObj::ClassType) {
    Klass* klass = (Klass*)ref->obj();
    assert(klass->is_klass(), "must be");
    if (!is_excluded(klass)) {
      caculate_fingerprint(klass);
      _klasses->append(klass);
      if (klass->oop_is_instance()) {
        _num_instance_klasses ++;
      } else if (klass->oop_is_objArray()) {
        _num_obj_array_klasses ++;
      } else {
        assert(klass->oop_is_typeArray(), "sanity");
        _num_type_array_klasses ++;
      }
    }
    // See RunTimeSharedClassInfo::get_for()
    _estimated_metaspaceobj_bytes += align_up(BytesPerWord, KlassAlignmentInBytes);
  } else if (ref->msotype() == MetaspaceObj::SymbolType) {
    // Make sure the symbol won't be GC'ed while we are dumping the archive.
    Symbol* sym = (Symbol*)ref->obj();
    sym->increment_refcount();
    _symbols->append(sym);
  }

  int bytes = ref->size() * BytesPerWord;
  _estimated_metaspaceobj_bytes += align_up(bytes, KlassAlignmentInBytes);
  return true; // recurse
}

size_t ArchiveBuilder::estimate_archive_size() {
  // size of the symbol table and two dictionaries, plus the RunTimeSharedClassInfo's
  size_t symbol_table_est = SymbolTable::estimate_size_for_archive();
  size_t dictionary_est = SystemDictionaryShared::estimate_size_for_archive();
  _estimated_hashtable_bytes = symbol_table_est + dictionary_est;

  size_t total = 0;

  total += _estimated_metaspaceobj_bytes;
  total += _estimated_hashtable_bytes;

  // allow fragmentation at the end of each dump region
  total += _total_dump_regions * ((size_t)os::vm_allocation_granularity());

  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("_estimated_hashtable_bytes = " SIZE_FORMAT " + " SIZE_FORMAT " = " SIZE_FORMAT,
                  symbol_table_est, dictionary_est, _estimated_hashtable_bytes);
    dynamic_cds_log->print_cr("_estimated_metaspaceobj_bytes = " SIZE_FORMAT, _estimated_metaspaceobj_bytes);
    dynamic_cds_log->print_cr("total estimate bytes = " SIZE_FORMAT, total);
  }

  return align_up(total, (size_t)os::vm_allocation_granularity());
}

address ArchiveBuilder::reserve_buffer() {
  size_t buffer_size = estimate_archive_size();
  size_t package_hash_table_est = align_up(ClassLoader::estimate_size_for_archive(), (size_t)os::vm_allocation_granularity());
  ReservedSpace rs(buffer_size + package_hash_table_est, os::vm_allocation_granularity(), false);
  if (!rs.is_reserved()) {
    tty->print_cr("Failed to reserve " SIZE_FORMAT " bytes of output buffer.", buffer_size);
    vm_direct_exit(0);
  }

  // buffer_bottom is the lowest address of the 2 core regions (rw, ro) when
  // we are copying the class metadata into the buffer.
  address buffer_bottom = (address)rs.base();
  _shared_rs = rs.first_part(buffer_size);
  _md_rs = rs.last_part(buffer_size);

  _buffer_bottom = buffer_bottom;
  _last_verified_top = buffer_bottom;
  _current_dump_space = &_rw_region;
  _num_dump_regions_used = 1;
  _other_region_used_bytes = 0;
  _current_dump_space->init(&_shared_rs, &_shared_vs);

  ArchivePtrMarker::initialize(&_ptrmap, &_shared_vs);

  // The bottom of the static archive should be mapped at this address by default.
  _requested_static_archive_bottom = (address)MetaspaceShared::requested_base_address();

  size_t static_archive_size = FileMapInfo::shared_spaces_size();
  _requested_static_archive_top = _requested_static_archive_bottom + static_archive_size;

  _mapped_static_archive_bottom = (address)MetaspaceShared::shared_metaspace_static_bottom();
  _mapped_static_archive_top = _mapped_static_archive_bottom + static_archive_size;

  _requested_dynamic_archive_bottom = align_up(_requested_static_archive_top, (size_t)os::vm_allocation_granularity());

  _buffer_to_requested_delta = _requested_dynamic_archive_bottom - _buffer_bottom;

  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("Reserved output buffer space at " PTR_FORMAT " [" SIZE_FORMAT " bytes]",
                  p2i(buffer_bottom), buffer_size);
    dynamic_cds_log->print_cr("Dynamic archive mapped space at " PTR_FORMAT, p2i(_requested_dynamic_archive_bottom));
  }

  return buffer_bottom;
}

void ArchiveBuilder::verify_estimate_size(size_t estimate, const char* which) {
  address bottom = _last_verified_top;
  address top = (address)(current_dump_space()->top());
  size_t used = size_t(top - bottom) + _other_region_used_bytes;
  int diff = int(estimate) - int(used);

  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("%s estimate = " SIZE_FORMAT " used = " SIZE_FORMAT "; diff = %d bytes", which, estimate, used, diff);
  }
  assert(diff >= 0, "Estimate is too small");

  _last_verified_top = top;
  _other_region_used_bytes = 0;
}

void ArchiveBuilder::dump_rw_metadata() {
  ResourceMark rm;
  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("Allocating RW objects ... ");
  }
  make_shallow_copies(&_rw_region, &_rw_src_objs);
}

void ArchiveBuilder::dump_ro_metadata() {
  ResourceMark rm;
  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("Allocating RO objects ... ");
  }
  start_dump_space(&_ro_region);
  make_shallow_copies(&_ro_region, &_ro_src_objs);
}

void ArchiveBuilder::dump_md_metadata() {
  ResourceMark rm;
  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("Allocating MD objects ... ");
  }
  _current_dump_space = &_md_region;
  _md_region.init(&_md_rs, &_md_vs);
  char* md_top = _md_vs.low();
  char* md_end = _md_vs.high_boundary();
  _md_region.allocate(md_end - md_top);
  ClassLoader::serialize_package_hash_table(&md_top, md_end);
}

void ArchiveBuilder::start_dump_space(DumpRegion* next) {
  address bottom = _last_verified_top;
  address top = (address)(_current_dump_space->top());
  _other_region_used_bytes += size_t(top - bottom);
  _current_dump_space->pack(next);
  _current_dump_space = next;
  _num_dump_regions_used ++;
  _last_verified_top = (address)(_current_dump_space->top());
}

void ArchiveBuilder::patch_shared_obj_vtable() {
  SourceObjList* objs = &_rw_src_objs;

  for (int i = 0; i < objs->objs()->length(); i++) {
    SourceObjInfo* src_info = objs->objs()->at(i);
    address dest = src_info->dumped_addr();
    MetaspaceClosure::Ref* ref = src_info->ref();
    intptr_t* archived_vtable = MetaspaceShared::get_archived_vtable(ref->msotype(), dest);
    if (archived_vtable != NULL) {
      // When we copy archived vtable from base archive into dynamic archive's objs, we can't call
      // virtual function before restore dynamic archive.
      *(intptr_t**)dest = archived_vtable;
      ArchivePtrMarker::mark_pointer((address*)dest);
    }
  }
  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("patch vtable done (%d objects)", objs->objs()->length());
  }
}

void ArchiveBuilder::remember_embedded_pointer_in_copied_obj(MetaspaceClosure::Ref* enclosing_ref,
                                                             MetaspaceClosure::Ref* ref) {
  assert(ref->obj() != NULL, "should have checked");

  if (enclosing_ref != NULL) {
    SourceObjInfo* src_info = (SourceObjInfo*)enclosing_ref->user_data();
    if (src_info == NULL) {
      // source objects of point_to_it/set_to_null types are not copied
      // so we don't need to remember their pointers.
    } else {
      if (src_info->read_only()) {
        _ro_src_objs.remember_embedded_pointer(src_info, ref);
      } else {
        _rw_src_objs.remember_embedded_pointer(src_info, ref);
      }
    }
  }
}

void ArchiveBuilder::make_shallow_copies(DumpRegion *dump_region,
                                         const ArchiveBuilder::SourceObjList* src_objs) {
  for (int i = 0; i < src_objs->objs()->length(); i++) {
    make_shallow_copy(dump_region, src_objs->objs()->at(i));
  }
  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("done (%d objects)", src_objs->objs()->length());
  }
}

void ArchiveBuilder::make_shallow_copy(DumpRegion *dump_region, SourceObjInfo* src_info) {
  MetaspaceClosure::Ref* ref = src_info->ref();
  address src = ref->obj();
  int bytes = src_info->size_in_bytes();
  char* dest;
  char* oldtop;
  char* newtop;

  oldtop = dump_region->top();
  if (ref->msotype() == MetaspaceObj::ClassType) {
    // Save a pointer immediate in front of an InstanceKlass, so
    // we can do a quick lookup from InstanceKlass* -> RunTimeSharedClassInfo*
    // without building another hashtable. See RunTimeSharedClassInfo::get_for()
    // in systemDictionaryShared.cpp.
    Klass* klass = (Klass*)src;
    if (klass->oop_is_instance()) {
      dump_region->allocate(sizeof(address));
    }
  }
  dest = dump_region->allocate(bytes);
  newtop = dump_region->top();

  memcpy(dest, src, bytes);

  if (TraceDynamicCDS) {
    dynamic_cds_log->print_cr("Copy: " PTR_FORMAT " ==> " PTR_FORMAT " %d", p2i(src), p2i(dest), bytes);
  }
  src_info->set_dumped_addr((address)dest);

  _alloc_stats.record(ref->msotype(), int(newtop - oldtop), src_info->read_only());
}

address ArchiveBuilder::get_dumped_addr(address src_obj) {
  SourceObjInfo* p = _src_obj_table.lookup(src_obj);
  assert(p != NULL, "must be");

  return p->dumped_addr();
}

void ArchiveBuilder::relocate_embedded_pointers(ArchiveBuilder::SourceObjList* src_objs) {
  for (int i = 0; i < src_objs->objs()->length(); i++) {
    src_objs->relocate(i, this);
  }
}

void ArchiveBuilder::print_stats() {
  _alloc_stats.print_stats(int(_ro_region.used()), int(_rw_region.used()));
}

void ArchiveBuilder::make_klasses_shareable() {
  for (int i = 0; i < klasses()->length(); i++) {
    Klass* k = klasses()->at(i);
    k->remove_java_mirror();
    if (k->oop_is_objArray()) {
      // InstanceKlass and TypeArrayKlass will in turn call remove_unshareable_info
      // on their array classes.
    } else if (k->oop_is_typeArray()) {
      k->remove_unshareable_info();
    } else {
      assert(k->oop_is_instance(), " must be");
      InstanceKlass* ik = InstanceKlass::cast(k);
      // High version introduce fast bytecode, jdk8 no need do it.
      // MetaspaceShared::rewrite_nofast_bytecodes_and_calculate_fingerprints(Thread::current(), ik);
      ik->remove_unshareable_info(); // assign_class_loader_type is in Klass::remove_unshareable_info

      if (DebugDynamicCDS) {
        ResourceMark rm;
        dynamic_cds_log->print_cr("klasses[%4d] = " PTR_FORMAT " => " PTR_FORMAT " %s", i, p2i(ik), p2i(to_requested(ik)), ik->external_name());
      }
    }
  }
}

uintx ArchiveBuilder::buffer_to_offset(address p) const {
  address requested_p = to_requested(p);
  assert(requested_p >= _requested_static_archive_bottom, "must be");
  return requested_p - _requested_static_archive_bottom;
}

uintx ArchiveBuilder::any_to_offset(address p) const {
  if (is_in_mapped_static_archive(p)) {
    assert(DynamicDumpSharedSpaces, "must be");
    return p - _mapped_static_archive_bottom;
  }
  return buffer_to_offset(p);
}

// RelocateBufferToRequested --- Relocate all the pointers in rw/ro,
// so that the archive can be mapped to the "requested" location without runtime relocation.
//
// - See ArchiveBuilder header for the definition of "buffer", "mapped" and "requested"
// - ArchivePtrMarker::ptrmap() marks all the pointers in the rw/ro regions
// - Every pointer must have one of the following values:
//   [a] NULL:
//       No relocation is needed. Remove this pointer from ptrmap so we don't need to
//       consider it at runtime.
//   [b] Points into an object X which is inside the buffer:
//       Adjust this pointer by _buffer_to_requested_delta, so it points to X
//       when the archive is mapped at the requested location.
//   [c] Points into an object Y which is inside mapped static archive:
//       - This happens only during dynamic dump
//       - Adjust this pointer by _mapped_to_requested_static_archive_delta,
//         so it points to Y when the static archive is mapped at the requested location.
class RelocateBufferToRequested : public BitMapClosure {
  ArchiveBuilder* _builder;
  address _buffer_bottom;
  intx _buffer_to_requested_delta;
  intx _mapped_to_requested_static_archive_delta;
  size_t _max_non_null_offset;

 public:
  RelocateBufferToRequested(ArchiveBuilder* builder) {
    _builder = builder;
    _buffer_bottom = _builder->buffer_bottom();
    _buffer_to_requested_delta = builder->buffer_to_requested_delta();
    _mapped_to_requested_static_archive_delta = builder->requested_static_archive_bottom() - builder->mapped_static_archive_bottom();
    _max_non_null_offset = 0;

    address bottom = _builder->buffer_bottom();
    address top = _builder->buffer_top();
    address new_bottom = bottom + _buffer_to_requested_delta;
    address new_top = top + _buffer_to_requested_delta;
    if (TraceDynamicCDS) {
      dynamic_cds_log->print_cr("Relocating archive from [" INTPTR_FORMAT " - " INTPTR_FORMAT "] to "
                     "[" INTPTR_FORMAT " - " INTPTR_FORMAT "]",
                     p2i(bottom), p2i(top),
                     p2i(new_bottom), p2i(new_top));
    }
  }

  bool do_bit(size_t offset) {
    address* p = (address*)_buffer_bottom + offset;
    assert(_builder->is_in_buffer_space(p), "pointer must live in buffer space");

    if (*p == NULL) {
      // todo -- clear bit, etc
      ArchivePtrMarker::ptrmap()->clear_bit(offset);
    } else {
      if (_builder->is_in_buffer_space(*p)) {
        *p += _buffer_to_requested_delta;
        // assert is in requested dynamic archive
      } else {
        assert(_builder->is_in_mapped_static_archive(*p), "old pointer must point inside buffer space or mapped static archive");
        *p += _mapped_to_requested_static_archive_delta;
        assert(_builder->is_in_requested_static_archive(*p), "new pointer must point inside requested archive");
      }

      _max_non_null_offset = offset;
    }

    return true; // keep iterating
  }

  void doit() {
    ArchivePtrMarker::ptrmap()->iterate(this);
    ArchivePtrMarker::compact(_max_non_null_offset);
  }
};

void ArchiveBuilder::relocate_to_requested() {
  ro_region()->pack();

  size_t my_archive_size = buffer_top() - buffer_bottom();

  assert(DynamicDumpSharedSpaces, "must be");
  _requested_dynamic_archive_top = _requested_dynamic_archive_bottom + my_archive_size;
  RelocateBufferToRequested patcher(this);
  patcher.doit();
}

void ArchiveBuilder::clean_up_src_obj_table() {
  SrcObjTableCleaner cleaner;
  _src_obj_table.iterate(&cleaner);
}

void ArchiveBuilder::write_archive(FileMapInfo* mapinfo) {
  assert(mapinfo->header()->magic() == CDS_DYNAMIC_ARCHIVE_MAGIC, "Dynamic CDS calls only");

  mapinfo->write_dynamic_header();

  write_region(mapinfo, MetaspaceShared::d_rw, &_rw_region, /*read_only=*/false,/*allow_exec=*/false);
  write_region(mapinfo, MetaspaceShared::d_ro, &_ro_region, /*read_only=*/true, /*allow_exec=*/false);
  write_region(mapinfo, MetaspaceShared::d_md, &_md_region, /*read_only=*/true, /*allow_exec=*/false);

  char* bitmap = mapinfo->write_bitmap_region(ArchivePtrMarker::ptrmap());

  if (InfoDynamicCDS && mapinfo->is_open()) {
    print_stats();
  }

  mapinfo->close();
  FREE_C_HEAP_ARRAY(char, bitmap, mtClassShared);
}

void ArchiveBuilder::write_region(FileMapInfo* mapinfo, int region_idx, DumpRegion* dump_region, bool read_only,  bool allow_exec) {
  mapinfo->write_region(region_idx, dump_region->base(), dump_region->used(), dump_region->used(), read_only, allow_exec);
}

class RefRelocator: public MetaspaceClosure {
  ArchiveBuilder* _builder;

public:
  RefRelocator(ArchiveBuilder* builder) : _builder(builder) {}

  virtual bool do_ref(Ref* ref, bool read_only) {
    if (ref->not_null()) {
      ref->update(_builder->get_dumped_addr(ref->obj()));
      ArchivePtrMarker::mark_pointer(ref->addr());
    }
    return false; // Do not recurse.
  }
};

void ArchiveBuilder::relocate_roots() {
  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("Relocating external roots ... ");
  }
  ResourceMark rm;
  RefRelocator doit(this);
  iterate_sorted_roots(&doit, /*is_relocating_pointers=*/true);
  doit.finish();
  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("done");
  }
}

void ArchiveBuilder::relocate_metaspaceobj_embedded_pointers() {
  if (InfoDynamicCDS) {
    dynamic_cds_log->print_cr("Relocating embedded pointers in core regions ... ");
  }
  relocate_embedded_pointers(&_rw_src_objs);
  relocate_embedded_pointers(&_ro_src_objs);
}

#ifndef PRODUCT
void ArchiveBuilder::assert_is_vm_thread() {
  assert(Thread::current()->is_VM_thread(), "ArchiveBuilder should be used only inside the VMThread");
}
#endif
