/*
 * Copyright (c) 2014, 2021, Oracle and/or its affiliates. All rights reserved.
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
#include "cds/dynamicArchive.hpp"
#include "classfile/systemDictionaryShared.hpp"
#include "classfile/classLoaderData.inline.hpp"
#include "runtime/arguments.hpp"
#include "runtime/mutexLocker.hpp"
#include "memory/metaspaceShared.hpp"
#include "memory/metaspaceClosure.hpp"
#include "utilities/resourceHash.hpp"
#include "runtime/mutexLocker.hpp"
#include "utilities/ostream.hpp"
#if INCLUDE_AGGRESSIVE_CDS
#include "memory/oopFactory.hpp"
#include "runtime/javaCalls.hpp"
#endif // INCLUDE_AGGRESSIVE_CDS

DEBUG_ONLY(bool SystemDictionaryShared::_no_class_loading_should_happen = false;)
bool SystemDictionaryShared::_dump_in_progress = false;

class DumpTimeSharedClassInfo: public CHeapObj<mtClass> {
  bool                         _excluded;
  bool                         _has_checked_exclusion;
public:
  struct DTLoaderConstraint {
    Symbol* _name;
    char _loader_type1;
    char _loader_type2;
    DTLoaderConstraint(Symbol* name, char l1, char l2) : _name(name), _loader_type1(l1), _loader_type2(l2) {
      _name->increment_refcount();
    }
    DTLoaderConstraint() : _name(NULL), _loader_type1('0'), _loader_type2('0') {}
    bool equals(const DTLoaderConstraint& t) {
      return t._name == _name &&
             ((t._loader_type1 == _loader_type1 && t._loader_type2 == _loader_type2) ||
              (t._loader_type2 == _loader_type1 && t._loader_type1 == _loader_type2));
    }
  };

  struct DTVerifierConstraint {
    Symbol* _name;
    Symbol* _from_name;
    DTVerifierConstraint() : _name(NULL), _from_name(NULL) {}
    DTVerifierConstraint(Symbol* n, Symbol* fn) : _name(n), _from_name(fn) {
      _name->increment_refcount();
      _from_name->increment_refcount();
    }
  };

#if INCLUDE_AGGRESSIVE_CDS
  struct DTSharedClassFile {
    int length;
    u1 data[1];
    int obj_size() { return ((int)sizeof(length)) + length; }
  };
#endif // INCLUDE_AGGRESSIVE_CDS

  InstanceKlass*               _klass;
  InstanceKlass*               _nest_host;
  bool                         _failed_verification;
  bool                         _is_archived_lambda_proxy;
  int                          _id;
  int                          _clsfile_size;
  int                          _clsfile_crc32;
  GrowableArray<DTVerifierConstraint>* _verifier_constraints;
  GrowableArray<char>*                 _verifier_constraint_flags;
  GrowableArray<DTLoaderConstraint>* _loader_constraints;
#if INCLUDE_AGGRESSIVE_CDS
  DTSharedClassFile*           _url_string;
  int64_t                     _classfile_timestamp;
#endif // INCLUDE_AGGRESSIVE_CDS

  DumpTimeSharedClassInfo() {
    _klass = NULL;
    _nest_host = NULL;
    _failed_verification = false;
    _is_archived_lambda_proxy = false;
    _has_checked_exclusion = false;
    _id = -1;
    _clsfile_size = -1;
    _clsfile_crc32 = -1;
    _excluded = false;
    _verifier_constraints = NULL;
    _verifier_constraint_flags = NULL;
    _loader_constraints = NULL;
#if INCLUDE_AGGRESSIVE_CDS
    _url_string = NULL;
    _classfile_timestamp = 0;
#endif // INCLUDE_AGGRESSIVE_CDS
  }

  void add_verification_constraint(InstanceKlass* k, Symbol* name,
         Symbol* from_name, bool from_field_is_protected, bool from_is_array, bool from_is_object);
  void record_linking_constraint(Symbol* name, Handle loader1, Handle loader2);

  bool is_builtin() {
    return SystemDictionaryShared::is_builtin(_klass);
  }

  int num_verifier_constraints() {
    if (_verifier_constraint_flags != NULL) {
      return _verifier_constraint_flags->length();
    } else {
      return 0;
    }
  }

  int num_loader_constraints() {
    if (_loader_constraints != NULL) {
      return _loader_constraints->length();
    } else {
      return 0;
    }
  }

  void metaspace_pointers_do(MetaspaceClosure* it) {
    it->push(&_klass);
    it->push(&_nest_host);
    if (_verifier_constraints != NULL) {
      for (int i = 0; i < _verifier_constraints->length(); i++) {
        DTVerifierConstraint* cons = _verifier_constraints->adr_at(i);
        it->push(&cons->_name);
        it->push(&cons->_from_name);
      }
    }
    if (_loader_constraints != NULL) {
      for (int i = 0; i < _loader_constraints->length(); i++) {
        DTLoaderConstraint* lc = _loader_constraints->adr_at(i);
        it->push(&lc->_name);
      }
    }
  }

  bool is_excluded() {
    // _klass may become NULL due to DynamicArchiveBuilder::set_to_null
    return _excluded || _failed_verification || _klass == NULL;
  }

  // simple accessors
  void set_excluded()                               { _excluded = true; }
  bool has_checked_exclusion() const                { return _has_checked_exclusion; }
  void set_has_checked_exclusion()                  { _has_checked_exclusion = true; }
  bool failed_verification() const                  { return _failed_verification; }
  void set_failed_verification()                    { _failed_verification = true; }
  InstanceKlass* nest_host() const                  { return _nest_host; }
  void set_nest_host(InstanceKlass* nest_host)      { _nest_host = nest_host; }

#if INCLUDE_AGGRESSIVE_CDS
  DTSharedClassFile* url_string() {
    return _url_string;
  }

  size_t url_string_size() {
    if (_url_string != NULL) {
      return (size_t)(_url_string->obj_size());
    }
    return (size_t)0;
  }

  void copy_url_string(char* string_value) {
    assert(strlen(string_value) != 0, "sanity");
    size_t string_len = strlen(string_value) + (size_t)1;
    size_t size = offset_of(DTSharedClassFile, data) + string_len;
    _url_string = (DTSharedClassFile*) NEW_C_HEAP_ARRAY(u1, size, mtInternal);
    _url_string->length = (int)string_len;
    memcpy(_url_string->data, string_value, string_len);
    assert((int)size == _url_string->obj_size(), "sanity");
  }

  void free_url_string() {
    if (_url_string != NULL) {
      FREE_C_HEAP_ARRAY(u1, _url_string, mtInternal);
      _url_string = NULL;
    }
  }

  int64_t classfile_timestamp() {
    return _classfile_timestamp;
  }

  void set_classfile_timestamp(int64_t classfile_timestamp) {
    _classfile_timestamp = classfile_timestamp;
  }
#endif // INCLUDE_AGGRESSIVE_CDS
};

inline unsigned DumpTimeSharedClassTable_hash(InstanceKlass* const& k) {
  // Deterministic archive is not possible because classes can be loaded
  // in multiple threads.
  return primitive_hash<InstanceKlass*>(k);
}

class DumpTimeSharedClassTable: public ResourceHashtable<
  InstanceKlass*,
  DumpTimeSharedClassInfo,
  &DumpTimeSharedClassTable_hash,
  primitive_equals<InstanceKlass*>,
  15889, // prime number
  ResourceObj::C_HEAP>
{
  int _builtin_count;
  int _unregistered_count;
public:
  DumpTimeSharedClassInfo* find_or_allocate_info_for(InstanceKlass* k, bool dump_in_progress) {
    bool created = false;
    DumpTimeSharedClassInfo* p;
    if (!dump_in_progress) {
      p = put_if_absent(k, &created);
    } else {
      p = get(k);
    }
    if (created) {
      assert(!SystemDictionaryShared::no_class_loading_should_happen(),
             "no new classes can be loaded while dumping archive");
      p->_klass = k;
    } else {
      if (!dump_in_progress) {
        assert(p->_klass == k, "Sanity");
      }
    }
    return p;
  }

  class CountClassByCategory : StackObj {
    DumpTimeSharedClassTable* _table;
  public:
    CountClassByCategory(DumpTimeSharedClassTable* table) : _table(table) {}
    bool do_entry(InstanceKlass* k, DumpTimeSharedClassInfo& info) {
      if (!info.is_excluded()) {
        if (info.is_builtin()) {
          ++ _table->_builtin_count;
        } else {
          ++ _table->_unregistered_count;
        }
      }
      return true; // keep on iterating
    }
  };

  void update_counts() {
    _builtin_count = 0;
    _unregistered_count = 0;
    CountClassByCategory counter(this);
    iterate(&counter);
  }

  int count_of(bool is_builtin) const {
    if (is_builtin) {
      return _builtin_count;
    } else {
      return _unregistered_count;
    }
  }
};

class RunTimeSharedClassInfo {
public:
  struct CrcInfo {
    int _clsfile_size;
    int _clsfile_crc32;
  };

  // This is different than  DumpTimeSharedClassInfo::DTVerifierConstraint. We use
  // u4 instead of Symbol* to save space on 64-bit CPU.
  struct RTVerifierConstraint {
    u4 _name;
    u4 _from_name;
    Symbol* name() { return (Symbol*)(SharedBaseAddress + _name);}
    Symbol* from_name() { return (Symbol*)(SharedBaseAddress + _from_name); }
  };

  struct RTLoaderConstraint {
    u4   _name;
    char _loader_type1;
    char _loader_type2;
    Symbol* constraint_name() {
      return (Symbol*)(SharedBaseAddress + _name);
    }
  };

#if INCLUDE_AGGRESSIVE_CDS
  struct RTSharedClassFile {
    int length;
    u1 data[1];
  };
#endif // INCLUDE_AGGRESSIVE_CDS

  InstanceKlass* _klass;
#if INCLUDE_AGGRESSIVE_CDS
  RTSharedClassFile* _url_string;
  int64_t _classfile_timestamp;
#endif // INCLUDE_AGGRESSIVE_CDS
  int _num_verifier_constraints;
  int _num_loader_constraints;

  // optional CrcInfo              _crc;  (only for UNREGISTERED classes)
  // optional InstanceKlass*       _nest_host
  // optional RTLoaderConstraint   _loader_constraint_types[_num_loader_constraints]
  // optional RTVerifierConstraint _verifier_constraints[_num_verifier_constraints]
  // optional char                 _verifier_constraint_flags[_num_verifier_constraints]

private:
  static size_t header_size_size() {
    return sizeof(RunTimeSharedClassInfo);
  }
  static size_t crc_size(InstanceKlass* klass) {
    if (!SystemDictionaryShared::is_builtin(klass)) {
      return sizeof(CrcInfo);
    } else {
      return 0;
    }
  }
  static size_t verifier_constraints_size(int num_verifier_constraints) {
    return sizeof(RTVerifierConstraint) * num_verifier_constraints;
  }
  static size_t verifier_constraint_flags_size(int num_verifier_constraints) {
    return sizeof(char) * num_verifier_constraints;
  }
  static size_t loader_constraints_size(int num_loader_constraints) {
    return sizeof(RTLoaderConstraint) * num_loader_constraints;
  }
  static size_t nest_host_size(InstanceKlass* klass) {
    assert(!klass->is_anonymous(), "klass should not be hidden right now.");
    if (klass->is_anonymous()) {
      return sizeof(InstanceKlass*);
    } else {
      return 0;
    }
  }

public:
  static size_t byte_size(InstanceKlass* klass, int num_verifier_constraints, int num_loader_constraints) {
    return header_size_size() +
           crc_size(klass) +
           nest_host_size(klass) +
           loader_constraints_size(num_loader_constraints) +
           verifier_constraints_size(num_verifier_constraints) +
           verifier_constraint_flags_size(num_verifier_constraints);
  }

#if INCLUDE_AGGRESSIVE_CDS
  static size_t byte_size(DumpTimeSharedClassInfo& info) {
    size_t previous_size = byte_size(info._klass, info.num_verifier_constraints(), info.num_loader_constraints());
    if (UseAggressiveCDS) {
      size_t cf_size = info.url_string_size();
      if (cf_size != 0) return align_up(previous_size, sizeof(int)) + cf_size;
    }
    return previous_size;
  }
#endif // INCLUDE_AGGRESSIVE_CDS

private:
  size_t crc_offset() const {
    return header_size_size();
  }

  size_t nest_host_offset() const {
      return crc_offset() + crc_size(_klass);
  }

  size_t loader_constraints_offset() const  {
    return nest_host_offset() + nest_host_size(_klass);
  }
  size_t verifier_constraints_offset() const {
    return loader_constraints_offset() + loader_constraints_size(_num_loader_constraints);
  }
  size_t verifier_constraint_flags_offset() const {
    return verifier_constraints_offset() + verifier_constraints_size(_num_verifier_constraints);
  }

  void check_verifier_constraint_offset(int i) const {
    assert(0 <= i && i < _num_verifier_constraints, "sanity");
  }

  void check_loader_constraint_offset(int i) const {
    assert(0 <= i && i < _num_loader_constraints, "sanity");
  }

#if INCLUDE_AGGRESSIVE_CDS
  size_t url_string_offset() const {
    return align_up(verifier_constraint_flags_offset() + verifier_constraint_flags_size(_num_verifier_constraints),
                    sizeof(int));
  }
#endif // INCLUDE_AGGRESSIVE_CDS

public:
  CrcInfo* crc() const {
    assert(crc_size(_klass) > 0, "must be");
    return (CrcInfo*)(address(this) + crc_offset());
  }
  RTVerifierConstraint* verifier_constraints() {
    assert(_num_verifier_constraints > 0, "sanity");
    return (RTVerifierConstraint*)(address(this) + verifier_constraints_offset());
  }
  RTVerifierConstraint* verifier_constraint_at(int i) {
    check_verifier_constraint_offset(i);
    return verifier_constraints() + i;
  }

  char* verifier_constraint_flags() {
    assert(_num_verifier_constraints > 0, "sanity");
    return (char*)(address(this) + verifier_constraint_flags_offset());
  }

  RTLoaderConstraint* loader_constraints() {
    assert(_num_loader_constraints > 0, "sanity");
    return (RTLoaderConstraint*)(address(this) + loader_constraints_offset());
  }

  RTLoaderConstraint* loader_constraint_at(int i) {
    check_loader_constraint_offset(i);
    return loader_constraints() + i;
  }

#if INCLUDE_AGGRESSIVE_CDS
  RTSharedClassFile* url_string() {
    return (RTSharedClassFile*)(address(this) + url_string_offset());
  }

  int64_t classfile_timestamp() {
    return _classfile_timestamp;
  }

  void set_classfile_timestamp(int64_t classfile_timestamp) {
    _classfile_timestamp = classfile_timestamp;
  }
#endif // INCLUDE_AGGRESSIVE_CDS

  void init(DumpTimeSharedClassInfo& info) {
    ArchiveBuilder* builder = ArchiveBuilder::current();
    assert(builder->is_in_buffer_space(info._klass), "must be");
    _klass = info._klass;
    if (!SystemDictionaryShared::is_builtin(_klass)) {
      CrcInfo* c = crc();
      c->_clsfile_size = info._clsfile_size;
      c->_clsfile_crc32 = info._clsfile_crc32;
    }
    _num_verifier_constraints = info.num_verifier_constraints();
    _num_loader_constraints   = info.num_loader_constraints();
    int i;
    if (_num_verifier_constraints > 0) {
      RTVerifierConstraint* vf_constraints = verifier_constraints();
      char* flags = verifier_constraint_flags();
      for (i = 0; i < _num_verifier_constraints; i++) {
        vf_constraints[i]._name      = builder->any_to_offset_u4(info._verifier_constraints->at(i)._name);
        vf_constraints[i]._from_name = builder->any_to_offset_u4(info._verifier_constraints->at(i)._from_name);
      }
      for (i = 0; i < _num_verifier_constraints; i++) {
        flags[i] = info._verifier_constraint_flags->at(i);
      }
    }

    if (_num_loader_constraints > 0) {
      RTLoaderConstraint* ld_constraints = loader_constraints();
      for (i = 0; i < _num_loader_constraints; i++) {
        ld_constraints[i]._name = builder->any_to_offset_u4(info._loader_constraints->at(i)._name);
        ld_constraints[i]._loader_type1 = info._loader_constraints->at(i)._loader_type1;
        ld_constraints[i]._loader_type2 = info._loader_constraints->at(i)._loader_type2;
      }
    }

#if INCLUDE_AGGRESSIVE_CDS
    if (info.url_string_size() != 0) {
      _url_string = url_string();
      memcpy(_url_string, info.url_string(), info.url_string_size());
      ArchivePtrMarker::mark_pointer(&_url_string);
      info.free_url_string();
    } else {
      _url_string = NULL;
    }
    set_classfile_timestamp(info.classfile_timestamp());
#endif // INCLUDE_AGGRESSIVE_CDS

    ArchivePtrMarker::mark_pointer(&_klass);
  }

  bool matches(int clsfile_size, int clsfile_crc32) const {
    return crc()->_clsfile_size  == clsfile_size &&
           crc()->_clsfile_crc32 == clsfile_crc32;
  }

  char verifier_constraint_flag(int i) {
    check_verifier_constraint_offset(i);
    return verifier_constraint_flags()[i];
  }

private:
  // ArchiveBuilder::make_shallow_copy() has reserved a pointer immediately
  // before archived InstanceKlasses. We can use this slot to do a quick
  // lookup of InstanceKlass* -> RunTimeSharedClassInfo* without
  // building a new hashtable.
  //
  //  info_pointer_addr(klass) --> 0x0100   RunTimeSharedClassInfo*
  //  InstanceKlass* klass     --> 0x0108   <C++ vtbl>
  //                               0x0110   fields from Klass ...
  static RunTimeSharedClassInfo** info_pointer_addr(InstanceKlass* klass) {
    return &((RunTimeSharedClassInfo**)klass)[-1];
  }

public:
  static RunTimeSharedClassInfo* get_for(InstanceKlass* klass) {
    assert(klass->is_shared(), "don't call for non-shared class");
    return *info_pointer_addr(klass);
  }
  static void set_for(InstanceKlass* klass, RunTimeSharedClassInfo* record) {
    assert(ArchiveBuilder::current()->is_in_buffer_space(klass), "must be");
    assert(ArchiveBuilder::current()->is_in_buffer_space(record), "must be");
    *info_pointer_addr(klass) = record;
    ArchivePtrMarker::mark_pointer(info_pointer_addr(klass));
  }

  // Used by RunTimeSharedDictionary to implement OffsetCompactHashtable::EQUALS
  static inline bool EQUALS(
       const RunTimeSharedClassInfo* value, Symbol* key, int len_unused) {
    return (value->_klass->name() == key);
  }

#if INCLUDE_AGGRESSIVE_CDS
  // check timestamp in the load time when UseAggressiveCDS.
  //   regular_file(*.class): need to check timestamp.
  //   jar_file(*.jar): no need to check timestamp here,already check programe_crc
  //                    somewhere else, see DynamicArchive::validate.
  //   other_file: not supported when UseAggressiveCDS.
  bool check_classfile_timestamp(char* url_string, TRAPS) {
    if (SystemDictionaryShared::is_regular_file(url_string)) {
      ResourceMark rm(THREAD);
      char* dir = SystemDictionaryShared::get_filedir(url_string);
      if (dir == NULL) {
        return false;
      }
      int64_t timestamp = SystemDictionaryShared::get_timestamp(dir, _klass->name());
      if (timestamp != _classfile_timestamp) {
        dynamic_cds_log->print_cr("%s, timestamp mismatch: " INT64_FORMAT " -> " INT64_FORMAT, _klass->name()->as_C_string(), _classfile_timestamp, timestamp);
        return false;
      }
    } else if (!SystemDictionaryShared::is_jar_file(url_string)) {
      dynamic_cds_log->print_cr("Unsupported URL:%s", url_string);
      return false;
    }
    return true;
  }

  Handle get_protection_domain(Handle class_loader, TRAPS) {
    if (_url_string == NULL) {
      return Handle();
    }
    char* data_ptr = (char*)(_url_string->data);

    if (CheckClassFileTimeStamp) {
      if (!check_classfile_timestamp(data_ptr, THREAD)) {
        return Handle();
      }
    }

    Handle url_string = java_lang_String::create_from_str(data_ptr, THREAD);
    JavaValue result(T_OBJECT);
    JavaCalls::call_virtual(&result,
                            class_loader,
                            class_loader->klass(),
                            vmSymbols::getProtectionDomainByURLString_name(),
                            vmSymbols::getProtectionDomainInternal_signature(),
                            url_string, THREAD);
    if (!HAS_PENDING_EXCEPTION) {
      oop pd = (oop) result.get_jobject();
      return Handle(THREAD, pd);
    } else {
      ResourceMark rm(THREAD);
      char* ex_msg = const_cast<char*>("");
      oop message = java_lang_Throwable::message(PENDING_EXCEPTION);
      if (message != NULL) {
        ex_msg = java_lang_String::as_utf8_string(message);
      }
      dynamic_cds_log->print_cr("get protection_domain has encountered exception: %s %s",
                                 PENDING_EXCEPTION->klass()->external_name(), ex_msg);
      CLEAR_PENDING_EXCEPTION;
    }
    return Handle();
  }
#endif // INCLUDE_AGGRESSIVE_CDS
};

class RunTimeSharedDictionary : public OffsetCompactHashtable<
  Symbol*,
  const RunTimeSharedClassInfo*,
  RunTimeSharedClassInfo::EQUALS> {};

static DumpTimeSharedClassTable* _dumptime_table = NULL;
// SystemDictionaries in the top layer dynamic archive
static RunTimeSharedDictionary _dynamic_builtin_dictionary;
static RunTimeSharedDictionary _dynamic_unregistered_dictionary;

void SystemDictionaryShared::set_class_has_failed_verification(InstanceKlass* ik) {
  Arguments::assert_is_dumping_archive();
  DumpTimeSharedClassInfo* p = find_or_allocate_info_for(ik);
  if (p != NULL) {
    p->set_failed_verification();
  }
}

void SystemDictionaryShared::start_dumping() {
  MutexLockerEx ml(DumpTimeTable_lock, Mutex::_no_safepoint_check_flag);
  _dump_in_progress = true;
}

void SystemDictionaryShared::init_dumptime_info(InstanceKlass* k) {
  (void)find_or_allocate_info_for(k);
}

void SystemDictionaryShared::remove_dumptime_info(InstanceKlass* k) {
  MutexLockerEx ml(DumpTimeTable_lock, Mutex::_no_safepoint_check_flag);
  DumpTimeSharedClassInfo* p = _dumptime_table->get(k);
  if (p == NULL) {
    return;
  }
  _dumptime_table->remove(k);
}

DumpTimeSharedClassInfo* SystemDictionaryShared::find_or_allocate_info_for(InstanceKlass* k) {
  MutexLockerEx ml(DumpTimeTable_lock, Mutex::_no_safepoint_check_flag);
  return find_or_allocate_info_for_locked(k);
}

DumpTimeSharedClassInfo* SystemDictionaryShared::find_or_allocate_info_for_locked(InstanceKlass* k) {
  assert_lock_strong(DumpTimeTable_lock);
  if (_dumptime_table == NULL) {
    _dumptime_table = new (ResourceObj::C_HEAP, mtClass)DumpTimeSharedClassTable();
  }
  return _dumptime_table->find_or_allocate_info_for(k, _dump_in_progress);
}

bool SystemDictionaryShared::empty_dumptime_table() {
  if (_dumptime_table == NULL) {
    return true;
  }
  _dumptime_table->update_counts();
  if (_dumptime_table->count_of(true) == 0 && _dumptime_table->count_of(false) == 0) {
    return true;
  }
  return false;
}

class ExcludeDumpTimeSharedClasses : StackObj {
public:
  bool do_entry(InstanceKlass* k, DumpTimeSharedClassInfo& info) {
    SystemDictionaryShared::check_for_exclusion(k, &info);
    return true; // keep on iterating
  }
};

class IterateDumpTimeSharedClassTable : StackObj {
  MetaspaceClosure *_it;
public:
  IterateDumpTimeSharedClassTable(MetaspaceClosure* it) : _it(it) {}

  bool do_entry(InstanceKlass* k, DumpTimeSharedClassInfo& info) {
    assert_lock_strong(DumpTimeTable_lock);
    if (!info.is_excluded()) {
      info.metaspace_pointers_do(_it);
    }
    return true; // keep on iterating
  }
};

class IterateDumpTimeTableReplaceKlass : StackObj {
public:
  IterateDumpTimeTableReplaceKlass() { }

  bool do_entry(InstanceKlass* k, DumpTimeSharedClassInfo& info) {
    if (k->oop_is_instance() && !info.is_excluded()) {
      k->constants()->symbol_replace_excluded_klass();
    }
    return true;
  }
};

void SystemDictionaryShared::check_excluded_classes() {
  assert(no_class_loading_should_happen(), "sanity");
  assert_lock_strong(DumpTimeTable_lock);
  ExcludeDumpTimeSharedClasses excl;
  _dumptime_table->iterate(&excl);
  _dumptime_table->update_counts();
}

bool SystemDictionaryShared::check_for_exclusion(InstanceKlass* k, DumpTimeSharedClassInfo* info) {
  if (MetaspaceShared::is_in_shared_space(k)) {
    // We have reached a super type that's already in the base archive. Treat it
    // as "not excluded".
    assert(DynamicDumpSharedSpaces, "must be");
    return false;
  }

  if (info == NULL) {
    info = _dumptime_table->get(k);
    assert(info != NULL, "supertypes of any classes in _dumptime_table must either be shared, or must also be in _dumptime_table");
  }

  if (!info->has_checked_exclusion()) {
    if (check_for_exclusion_impl(k)) {
      info->set_excluded();
    }
    info->set_has_checked_exclusion();
  }

  return info->is_excluded();
}

// Check if a class or any of its supertypes has been redefined.
bool SystemDictionaryShared::has_been_redefined(InstanceKlass* k) {
  if (k->has_been_redefined()) {
    return true;
  }
  if (k->java_super() != NULL && has_been_redefined(k->java_super())) {
    return true;
  }
  Array<Klass*>* interfaces = k->local_interfaces();
  int len = interfaces->length();
  for (int i = 0; i < len; i++) {
    if (has_been_redefined((InstanceKlass*)interfaces->at(i))) {
      return true;
    }
  }
  return false;
}

bool SystemDictionaryShared::check_for_exclusion_impl(InstanceKlass* k) {
  if (k->is_in_error_state()) {
    return warn_excluded(k, "In error state");
  }
  if (k->init_state() < InstanceKlass::loaded) {
    return warn_excluded(k, "not loaded klass");
  }
  if (has_been_redefined(k)) {
    return warn_excluded(k, "Has been redefined");
  }
  if (k->signers() != NULL) {
    // We cannot include signed classes in the archive because the certificates
    // used during dump time may be different than those used during
    // runtime (due to expiration, etc).
    return warn_excluded(k, "Signed JAR");
  }
  if (is_jfr_event_class(k)) {
    // We cannot include JFR event classes because they need runtime-specific
    // instrumentation in order to work with -XX:FlightRecorderOptions:retransform=false.
    // There are only a small number of these classes, so it's not worthwhile to
    // support them and make CDS more complicated.
    return warn_excluded(k, "JFR event class");
  }
  if (k->init_state() < InstanceKlass::linked) {
    // In CDS dumping, we will attempt to link all classes. Those that fail to link will
    // be recorded in DumpTimeSharedClassInfo.
    Arguments::assert_is_dumping_archive();

    // TODO -- rethink how this can be handled.
    // We should try to link ik, however, we can't do it here because
    // 1. We are at VM exit
    // 2. linking a class may cause other classes to be loaded, which means
    //    a custom ClassLoader.loadClass() may be called, at a point where the
    //    class loader doesn't expect it.
    if (has_class_failed_verification(k)) {
      return warn_excluded(k, "Failed verification");
    } else {
      if (k->can_be_verified_at_dumptime()) {
        return warn_excluded(k, "Not linked");
      }
    }
  }
  if (DynamicDumpSharedSpaces && k->major_version() < 50 /*JAVA_6_VERSION*/) {
    // In order to support old classes during dynamic dump, class rewriting needs to
    // be reverted. This would result in more complex code and testing but not much gain.
    ResourceMark rm;
    dynamic_cds_log->print_cr("Pre JDK 6 class not supported by CDS: %u.%u %s",
                     k->major_version(),  k->minor_version(), k->name()->as_C_string());
    return true;
  }

  if (!k->can_be_verified_at_dumptime() && k->is_linked()) {
    return warn_excluded(k, "Old class has been linked");
  }

  if (k->is_anonymous() /* && !is_registered_lambda_proxy_class(k) */) {
    return warn_excluded(k, "Hidden class");
  }

  InstanceKlass* super = k->java_super();
  if (super != NULL && check_for_exclusion(super, NULL)) {
    ResourceMark rm;
    dynamic_cds_log->print_cr("Skipping %s: super class %s is excluded", k->name()->as_C_string(), super->name()->as_C_string());
    return true;
  }

  Array<Klass*>* interfaces = k->local_interfaces();
  int len = interfaces->length();
  for (int i = 0; i < len; i++) {
    InstanceKlass* intf = (InstanceKlass*)interfaces->at(i);
    if (check_for_exclusion(intf, NULL)) {
      dynamic_cds_log->print_cr("Skipping %s: interface %s is excluded", k->name()->as_C_string(), intf->name()->as_C_string());
      return true;
    }
  }

  return false; // false == k should NOT be excluded
}

// Returns true so the caller can do:    return warn_excluded(".....");
bool SystemDictionaryShared::warn_excluded(InstanceKlass* k, const char* reason) {
  ResourceMark rm;
  dynamic_cds_log->print_cr("Skipping %s: %s", k->name()->as_C_string(), reason);
  return true;
}

bool SystemDictionaryShared::is_jfr_event_class(InstanceKlass *k) {
  while (k) {
    if (k->name()->equals("jdk/jfr/Event") || k->name()->starts_with("jdk/jfr/event")) {
      return true;
    }
    k = k->java_super();
  }
  return false;
}

bool SystemDictionaryShared::has_class_failed_verification(InstanceKlass* ik) {
  if (_dumptime_table == NULL) {
    assert(DynamicDumpSharedSpaces, "sanity");
    assert(ik->is_shared(), "must be a shared class in the static archive");
    return false;
  }
  DumpTimeSharedClassInfo* p = _dumptime_table->get(ik);
  return (p == NULL) ? false : p->failed_verification();
}

void SystemDictionaryShared::dumptime_classes_do(class MetaspaceClosure* it) {
  assert_lock_strong(DumpTimeTable_lock);
  IterateDumpTimeSharedClassTable iter(it);
  _dumptime_table->iterate(&iter);
}

void SystemDictionaryShared::replace_klass_in_constantPool() {
  IterateDumpTimeTableReplaceKlass iter;
  _dumptime_table->iterate(&iter);
}

bool SystemDictionaryShared::is_excluded_class(InstanceKlass* k) {
  assert(_no_class_loading_should_happen, "sanity");
  assert_lock_strong(DumpTimeTable_lock);
  Arguments::assert_is_dumping_archive();
  DumpTimeSharedClassInfo* p = find_or_allocate_info_for_locked(k);
  return (p == NULL) ? true : p->is_excluded();
}

class EstimateSizeForArchive : StackObj {
  size_t _shared_class_info_size;
  int _num_builtin_klasses;
  int _num_unregistered_klasses;

public:
  EstimateSizeForArchive() {
    _shared_class_info_size = 0;
    _num_builtin_klasses = 0;
    _num_unregistered_klasses = 0;
  }

  bool do_entry(InstanceKlass* k, DumpTimeSharedClassInfo& info) {
    if (!info.is_excluded()) {
#if INCLUDE_AGGRESSIVE_CDS
      size_t byte_size = RunTimeSharedClassInfo::byte_size(info);
#else
      size_t byte_size = RunTimeSharedClassInfo::byte_size(info._klass, info.num_verifier_constraints(), info.num_loader_constraints());
#endif // INCLUDE_AGGRESSIVE_CDS
      _shared_class_info_size += align_up(byte_size, KlassAlignmentInBytes);
    }
    return true; // keep on iterating
  }

  size_t total() {
    return _shared_class_info_size;
  }
};

size_t SystemDictionaryShared::estimate_size_for_archive() {
  EstimateSizeForArchive est;
  _dumptime_table->iterate(&est);
  size_t total_size = est.total() +
    CompactHashtableWriter::estimate_size(_dumptime_table->count_of(true)) +
    CompactHashtableWriter::estimate_size(_dumptime_table->count_of(false));
  total_size += CompactHashtableWriter::estimate_size(0);
  return total_size;
}

unsigned int SystemDictionaryShared::hash_for_shared_dictionary(address ptr) {
  if (ArchiveBuilder::is_active()) {
    uintx offset = ArchiveBuilder::current()->any_to_offset(ptr);
    unsigned int hash = primitive_hash<uintx>(offset);
    DEBUG_ONLY({
        if (((const MetaspaceObj*)ptr)->is_shared()) {
          assert(hash == SystemDictionaryShared::hash_for_shared_dictionary_quick(ptr), "must be");
        }
      });
    return hash;
  } else {
    return SystemDictionaryShared::hash_for_shared_dictionary_quick(ptr);
  }
}

#if INCLUDE_AGGRESSIVE_CDS
class ExcludeDuplicateKlass : StackObj {
public:
  static const int INITIAL_TABLE_SIZE = 15889;

  ExcludeDuplicateKlass(int size) : _has_been_visited(size) {}

  bool do_entry(InstanceKlass* k, DumpTimeSharedClassInfo& info) {
    if (!info.is_excluded()) {
      bool created;
      Symbol* name = info._klass->name();
      address* info_ptr = _has_been_visited.add_if_absent((address)name, (address)&info, &created);
      if (!created) {
        info.set_excluded();
        DumpTimeSharedClassInfo* first_info = (DumpTimeSharedClassInfo*)(*info_ptr);
        if (!first_info->is_excluded()) {
          first_info->set_excluded();
        }
        if (TraceDynamicCDS) {
          ResourceMark rm;
          dynamic_cds_log->print_cr("[AggressiveCDS] Skipping duplicate class (excluded): %s", name->as_C_string());
        }
      }
    }
    return true;
  }

private:
  KVHashtable<address, address, mtClassShared> _has_been_visited;
};
#endif // INCLUDE_AGGRESSIVE_CDS

class CopySharedClassInfoToArchive : StackObj {
  CompactHashtableWriter* _writer;
  bool _is_builtin;
  ArchiveBuilder *_builder;
public:
  CopySharedClassInfoToArchive(CompactHashtableWriter* writer,
                               bool is_builtin)
    : _writer(writer), _is_builtin(is_builtin), _builder(ArchiveBuilder::current()) {}

  bool do_entry(InstanceKlass* k, DumpTimeSharedClassInfo& info) {
    if (!info.is_excluded() && info.is_builtin() == _is_builtin) {
#if INCLUDE_AGGRESSIVE_CDS
      size_t byte_size = RunTimeSharedClassInfo::byte_size(info);
#else
      size_t byte_size = RunTimeSharedClassInfo::byte_size(info._klass, info.num_verifier_constraints(), info.num_loader_constraints());
#endif // INCLUDE_AGGRESSIVE_CDS
      RunTimeSharedClassInfo* record;
      record = (RunTimeSharedClassInfo*)ArchiveBuilder::ro_region_alloc(byte_size);
      record->init(info);

      unsigned int hash;
      Symbol* name = info._klass->name();
      hash = SystemDictionaryShared::hash_for_shared_dictionary((address)name);
      u4 delta = _builder->buffer_to_offset_u4((address)record);
      if (_is_builtin && info._klass->is_anonymous()) {
        // skip
      } else {
        _writer->add(hash, delta);
      }
      if (TraceDynamicCDS) {
        ResourceMark rm;
        dynamic_cds_log->print_cr("%s dictionary: %s", (_is_builtin ? "builtin" : "unregistered"), info._klass->external_name());
      }

      // Save this for quick runtime lookup of InstanceKlass* -> RunTimeSharedClassInfo*
      RunTimeSharedClassInfo::set_for(info._klass, record);
    }
    return true; // keep on iterating
  }
};

void SystemDictionaryShared::write_dictionary(RunTimeSharedDictionary* dictionary,
                                              bool is_builtin) {
  CompactHashtableStats stats;
  dictionary->reset();
#if INCLUDE_AGGRESSIVE_CDS
  if (UseAggressiveCDS && !is_builtin) {
    ExcludeDuplicateKlass dup(ExcludeDuplicateKlass::INITIAL_TABLE_SIZE);
    _dumptime_table->iterate(&dup);
  }
#endif // INCLUDE_AGGRESSIVE_CDS
  CompactHashtableWriter writer(_dumptime_table->count_of(is_builtin), &stats);
  CopySharedClassInfoToArchive copy(&writer, is_builtin);
  assert_lock_strong(DumpTimeTable_lock);
  _dumptime_table->iterate(&copy);
  writer.dump(dictionary, is_builtin ? "builtin dictionary" : "unregistered dictionary");
}

void SystemDictionaryShared::write_to_archive() {
  write_dictionary(&_dynamic_builtin_dictionary, true);
  write_dictionary(&_dynamic_unregistered_dictionary, false);
}

void SystemDictionaryShared::serialize_dictionary_headers(SerializeClosure* soc) {
  _dynamic_builtin_dictionary.serialize_header(soc);
  _dynamic_unregistered_dictionary.serialize_header(soc);
}

void SystemDictionaryShared::set_shared_class_misc_info(InstanceKlass* k, ClassFileStream* cfs) {
  Arguments::assert_is_dumping_archive();
  assert(!is_builtin(k), "must be unregistered class");
  DumpTimeSharedClassInfo* info = find_or_allocate_info_for(k);
  if (info != NULL) {
    info->_clsfile_size  = cfs->length();
    info->_clsfile_crc32 = ClassLoader::crc32(0, (const char*)cfs->buffer(), cfs->length());
  }
}

// This function is called for loading only UNREGISTERED classes
InstanceKlass* SystemDictionaryShared::lookup_from_stream(Symbol* class_name,
                                                          Handle class_loader,
                                                          Handle protection_domain,
                                                          const ClassFileStream* cfs,
                                                          TRAPS) {
  if (!UseSharedSpaces) {
    return NULL;
  }
  if (class_name == NULL) {  // don't do this for hidden classes
    return NULL;
  }
  if (SystemDictionary::is_builtin_loader(class_loader)) {
    // Do nothing for the BUILTIN loaders.
    return NULL;
  }

  const RunTimeSharedClassInfo* record = find_record(&_dynamic_unregistered_dictionary, class_name);
  if (record == NULL) {
    return NULL;
  }

  int clsfile_size  = cfs->length();
  int clsfile_crc32 = ClassLoader::crc32(0, (const char*)cfs->buffer(), cfs->length());

  if (!record->matches(clsfile_size, clsfile_crc32)) {
    return NULL;
  }

  return acquire_class_for_current_thread(record->_klass, class_loader,
                                          protection_domain, cfs,
                                          THREAD);
}

const RunTimeSharedClassInfo*
SystemDictionaryShared::find_record(RunTimeSharedDictionary* dynamic_dict, Symbol* name) {
  if (!UseSharedSpaces || !name->is_shared()) {
    // The names of all shared classes must also be a shared Symbol.
    return NULL;
  }

  unsigned int hash = SystemDictionaryShared::hash_for_shared_dictionary_quick(name);
  const RunTimeSharedClassInfo* record = NULL;
  // AppCDS only support builtin classloader, customer class loader is just in dynamic archive.
  if (DynamicArchive::is_mapped()) {
    record = dynamic_dict->lookup(name, hash, 0);
  }

  return record;
}

InstanceKlass* SystemDictionaryShared::acquire_class_for_current_thread(
                   InstanceKlass *ik,
                   Handle class_loader,
                   Handle protection_domain,
                   const ClassFileStream *cfs,
                   TRAPS) {
  ClassLoaderData* loader_data = ClassLoaderData::class_loader_data(class_loader());

  {
    MutexLocker mu(SharedDictionary_lock, THREAD);
    if (ik->class_loader_data() != NULL) {
      //    ik is already loaded (by this loader or by a different loader)
      // or ik is being loaded by a different thread (by this loader or by a different loader)
      return NULL;
    }

    // No other thread has acquired this yet, so give it to *this thread*
    ik->set_class_loader_data(loader_data);
  }

  // No longer holding SharedDictionary_lock
  // No need to lock, as <ik> can be held only by a single thread.
  loader_data->add_class(ik);

  // Load and check super/interfaces, restore unsharable info
  instanceKlassHandle shared_klass = SystemDictionary::load_shared_class(ik, class_loader, protection_domain, THREAD);
  if (shared_klass() == NULL || HAS_PENDING_EXCEPTION) {
    // TODO: clean up <ik> so it can be used again
    return NULL;
  }

  return shared_klass();
}

InstanceKlass* SystemDictionaryShared::find_dynamic_builtin_class(Symbol* name) {
  const RunTimeSharedClassInfo* record = find_record(&_dynamic_builtin_dictionary, name);
  if (record != NULL) {
    assert(!record->_klass->is_anonymous(), "hidden class cannot be looked up by name");
    assert(check_klass_alignment(record->_klass), "Address not aligned");
    return record->_klass;
  } else {
    return NULL;
  }
}

#if INCLUDE_AGGRESSIVE_CDS
static const char* JAR_FILE_PREFIX = "jar://file:";
static const char* FILE_SEPARATOR = "file://";
static const char* CLASSFILE_SUFFIX = ".class";

static bool start_with(char* str, const char* prefix) {
  if (str == NULL || prefix == NULL || strlen(str) < strlen(prefix)) {
    return false;
  }
  if (strncmp(str, prefix, strlen(prefix)) == 0) {
    return true;
  }
  return false;
}

bool SystemDictionaryShared::is_jar_file(char* url_string) {
  if (start_with(url_string, JAR_FILE_PREFIX)) {
    return true;
  }
  return false;
}

bool SystemDictionaryShared::is_regular_file(char* url_string) {
  if (start_with(url_string, FILE_SEPARATOR)) {
    return true;
  }
  return false;
}

char* SystemDictionaryShared::get_filedir(char* url_string) {
  if (!is_regular_file(url_string)) {
    return NULL;
  }
  char* dir = url_string + strlen(FILE_SEPARATOR);
  struct stat st;
  if (os::stat(dir, &st) == 0) {
    if ((st.st_mode & S_IFDIR) == S_IFDIR) {
      return dir;
    }
  }
  return NULL;
}

int64_t SystemDictionaryShared::get_timestamp(char* dir, Symbol* class_name) {
  char* name = class_name->as_C_string();
  size_t name_len = strlen(name);
  size_t dir_len = strlen(dir);
  size_t classfile_suffix_len = strlen(CLASSFILE_SUFFIX);
  char* file_path = NEW_RESOURCE_ARRAY(char, dir_len + name_len + classfile_suffix_len + 1);
  memcpy(file_path, dir, dir_len);
  memcpy(file_path + dir_len, name, name_len);
  memcpy(file_path + dir_len + name_len, CLASSFILE_SUFFIX, classfile_suffix_len + 1);
  assert(strlen(file_path) == dir_len + name_len + classfile_suffix_len, "sanity");
  struct stat st;
  if (os::stat(file_path, &st) == 0) {
    return st.st_mtime;
  }
  dynamic_cds_log->print_cr("get timestamp failed:%s", file_path);
  return 0;
}

Handle SystemDictionaryShared::get_protection_domain(InstanceKlass* k, Handle class_loader, TRAPS) {
  assert(UseAggressiveCDS, "sanity");
  RunTimeSharedClassInfo* info = RunTimeSharedClassInfo::get_for(k);
  assert(!is_builtin_loader(class_loader), "sanity");
  return info->get_protection_domain(class_loader, THREAD);
}

void SystemDictionaryShared::set_url_string(InstanceKlass* k, char* string_value) {
  assert(UseAggressiveCDS, "sanity");
  Arguments::assert_is_dumping_archive();
  assert(!is_builtin(k), "must be unregistered class");
  DumpTimeSharedClassInfo* info = find_or_allocate_info_for(k);
  if (info != NULL && info->_url_string == NULL) {
    info->copy_url_string(string_value);
  }
}

void SystemDictionaryShared::save_timestamp(InstanceKlass* k, char* string_value) {
  if (SystemDictionaryShared::is_regular_file(string_value)) {
    char* dir = SystemDictionaryShared::get_filedir(string_value);
    if (dir != NULL) {
      int64_t timestamp = SystemDictionaryShared::get_timestamp(dir, k->name());
      SystemDictionaryShared::set_classfile_timestamp(k, timestamp);
    }
  } else if (!SystemDictionaryShared::is_jar_file(string_value)) {
    dynamic_cds_log->print_cr("Unsupported URL:%s", string_value);
  }
}

void SystemDictionaryShared::set_classfile_timestamp(InstanceKlass* k, int64_t classfile_timestamp) {
  assert(UseAggressiveCDS, "sanity");
  Arguments::assert_is_dumping_archive();
  assert(!is_builtin(k), "must be unregistered class");
  DumpTimeSharedClassInfo* info = find_or_allocate_info_for(k);
  if (info != NULL) {
    info->set_classfile_timestamp(classfile_timestamp);
  }
}

InstanceKlass* SystemDictionaryShared::lookup_trusted_share_class(Symbol* class_name,
                                                                  Handle class_loader,
                                                                  TRAPS) {
  assert(UseAggressiveCDS, "sanity");
  if (!UseSharedSpaces) {
    return NULL;
  }
  if (class_name == NULL) {  // don't do this for hidden classes
    return NULL;
  }
  if (SystemDictionary::is_builtin_loader(class_loader)) {
    // Do nothing for the BUILTIN loaders.
    return NULL;
  }

  // Classloaders that support parallelism, e.g. bootstrap classloader,
  // or all classloaders with UnsyncloadClass do not acquire lock here
  bool DoObjectLock = true;
  if (is_parallelCapable(class_loader)) {
    DoObjectLock = false;
  }

  register_loader(class_loader, CHECK_NULL);

  // Make sure we are synchronized on the class loader before we proceed
  Handle lockObject = compute_loader_lock_object(class_loader, THREAD);
  check_loader_lock_contention(lockObject, THREAD);
  ObjectLocker ol(lockObject, THREAD, DoObjectLock);

  const RunTimeSharedClassInfo* record = find_record(&_dynamic_unregistered_dictionary, class_name);
  if (record == NULL) {
    return NULL;
  }

  Handle protection_domain = SystemDictionaryShared::get_protection_domain(record->_klass, class_loader, CHECK_NULL);

  if (protection_domain.is_null()) {
    // URL not matched, which means should not be loaded by this classloader
    return NULL;
  }

  instanceKlassHandle k = acquire_class_for_current_thread(record->_klass, class_loader, protection_domain, NULL, THREAD);
  if (!HAS_PENDING_EXCEPTION && k() != NULL) {
    if (is_parallelCapable(class_loader)) {
      k = find_or_define_instance_class(class_name, class_loader, k, THREAD);
    } else {
      define_instance_class(k, THREAD);
    }
  }
  return k();
}
#endif // INCLUDE_AGGRESSIVE_CDS