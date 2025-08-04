/*
 * Copyright (c) 2025, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2019 Alibaba Group Holding Limited. All Rights Reserved.
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

#ifndef SHARED_VM_JPROFILECACHE_JITPROFILERECORD_HPP
#define SHARED_VM_JPROFILECACHE_JITPROFILERECORD_HPP

#include "jprofilecache/jitProfileCache.hpp"

class JitProfileCache;

class JitProfileRecorderEntry : public HashtableEntry<Method*, mtInternal> {
public:
  JitProfileRecorderEntry() {  }
  void free_allocate() {
    if (_method_name != NULL) {
      free((void*)_method_name);
      _method_name = NULL;
    }
    if (_method_sig != NULL) {
      free((void*)_method_sig);
      _method_sig = NULL;
    }
    if (_class_name != NULL) {
      free((void*)_class_name);
      _class_name = NULL;
    }
    if (_class_loader_name != NULL) {
      free((void*)_class_loader_name);
      _class_loader_name = NULL;
    }
    if (_class_path != NULL) {
      free((void*)_class_path);
      _class_path = NULL;
    }
  }

  void init() {
    _bci = InvocationEntryBci;
    _magic_number = 0;
    _compilation_type = 0;
    _method_name = NULL;
    _method_sig = NULL;
    _first_invoke_init_order = 0;
    _method_code_size = 0;
    _method_hash = 0;
    _method_bci = 0;
    _class_name = NULL;
    _class_loader_name = NULL;
    _class_path = NULL;
    _class_bytes_size = 0;
    _class_crc32 = 0;
    _class_number = 0;
    _interpreter_invocation_count = 0;
    _interpreter_throwout_count = 0;
    _invocation_counter = 0;
    _backedge_counter = 0;
  }

  void set_bci(int bci) { _bci = bci; }
  int  bci()            { return _bci; }

  void set_order(int order) { _order = order; }
  int  order()              { return _order; }

  JitProfileRecorderEntry* next() {
    return (JitProfileRecorderEntry*)HashtableEntry<Method*, mtInternal>::next();
  }

  u4 get_magic_number(){ return _magic_number;}
  u1 get_compilation_type(){ return _compilation_type;}
  const char* get_method_name(){ return _method_name;}
  const char* get_method_sig(){ return _method_sig;}
  u4 get_first_invoke_init_order(){ return _first_invoke_init_order;}
  u4 get_method_code_size(){ return _method_code_size;}
  u4 get_method_hash(){ return _method_hash;}
  u4 get_method_bci(){ return _method_bci;}
  u4 get_class_bytes_size(){ return _class_bytes_size;}
  u4 get_class_crc32(){ return _class_crc32;}
  u4 get_class_number(){ return _class_number;}
  u4 get_interpreter_invocation_count(){ return _interpreter_invocation_count;}
  u4 get_interpreter_throwout_count(){ return _interpreter_throwout_count;}
  u4 get_invocation_counter(){ return _invocation_counter;}
  u4 get_backedge_counter(){ return _backedge_counter;}
  const char* get_class_name(){ return _class_name;}
  const char* get_class_loader_name(){ return _class_loader_name;}
  const char* get_class_path(){ return _class_path;}

  void set_magic_number(u4 magic_number) { _magic_number = magic_number; }
  void set_compilation_type(u1 compilation_type) { _compilation_type = compilation_type; }
  void set_method_name(const char* method_name) { _method_name = method_name; }
  void set_method_sig(const char* method_sig) { _method_sig = method_sig; }
  void set_first_invoke_init_order(u4 first_invoke_init_order) { _first_invoke_init_order = first_invoke_init_order; }
  void set_method_code_size(u4 method_code_size) { _method_code_size = method_code_size; }
  void set_method_hash(u4 method_hash) { _method_hash = method_hash; }
  void set_method_bci(u4 method_bci) { _method_bci = method_bci; }
  void set_class_bytes_size(u4 class_bytes_size) { _class_bytes_size = class_bytes_size; }
  void set_class_crc32(u4 class_crc32) { _class_crc32 = class_crc32; }
  void set_class_number(u4 class_number) { _class_number = class_number; }
  void set_interpreter_invocation_count(u4 interpreter_invocation_count) { _interpreter_invocation_count = interpreter_invocation_count; }
  void set_interpreter_throwout_count(u4 interpreter_throwout_count) { _interpreter_throwout_count = interpreter_throwout_count; }
  void set_invocation_counter(u4 invocation_counter) { _invocation_counter = invocation_counter; }
  void set_backedge_counter(u4 backedge_counter) { _backedge_counter = backedge_counter; }
  void set_class_name(const char* class_name) { _class_name = class_name; }
  void set_class_loader_name(const char* class_loader_name) { _class_loader_name = class_loader_name; }
  void set_class_path(const char* class_path) { _class_path = class_path; }

private:
  int    _bci;                           
  int    _order;
  u4 _magic_number;                
  u1 _compilation_type;            
  const char* _method_name;              
  const char* _method_sig;               
  u4 _first_invoke_init_order;     
  u4 _method_code_size;            
  u4 _method_hash;                 
  u4 _method_bci;                  
  const char* _class_name;               
  const char* _class_loader_name;        
  const char* _class_path;               
  u4 _class_bytes_size;             
  u4 _class_crc32;                 
  u4 _class_number;                
  u4 _interpreter_invocation_count;
  u4 _interpreter_throwout_count;  
  u4 _invocation_counter;          
  u4 _backedge_counter;                  
};

class JitProfileRecordDictionary : public Hashtable<Method*, mtInternal> {
  friend class VMStructs;
  friend class JitProfileCache;
public:
  JitProfileRecordDictionary(unsigned int size);
  virtual ~JitProfileRecordDictionary();

  JitProfileRecorderEntry* add_method(unsigned int method_hash, Method* method, int bci);

  JitProfileRecorderEntry* find_entry(unsigned int hash, Method* method);

  void free_entry(JitProfileRecorderEntry* entry);

  unsigned int count() { return _count; }

  void print();

  JitProfileRecorderEntry* bucket(int i) {
    return (JitProfileRecorderEntry*)Hashtable<Method*, mtInternal>::bucket(i);
  }

private:
  unsigned int _count;
  JitProfileRecorderEntry* new_entry(unsigned int hash, Method* method);
};

class ClassSymbolEntry {
public:
  ClassSymbolEntry(Symbol* class_name, Symbol* class_loader_name, Symbol* path)
    : _class_name(class_name),
      _class_loader_name(class_loader_name),
      _class_path(path) {
    if (_class_name != NULL) _class_name->increment_refcount();
    if (_class_loader_name != NULL) _class_loader_name->increment_refcount();
    if (_class_path != NULL) _class_path->increment_refcount();
  }

  ClassSymbolEntry()
    : _class_name(NULL),
      _class_loader_name(NULL),
      _class_path(NULL) {
  }

  ~ClassSymbolEntry() {
    if (_class_name != NULL) _class_name->decrement_refcount();
    if (_class_loader_name != NULL) _class_loader_name->decrement_refcount();
    if (_class_path != NULL) _class_path->decrement_refcount();
  }

  Symbol* class_name() const { return _class_name; }
  Symbol* class_loader_name() const { return _class_loader_name; }
  Symbol* path() const { return _class_path; }

  bool equals(const ClassSymbolEntry& rhs) const {
    return _class_name == rhs._class_name;
  }

private:
  Symbol* _class_name;
  Symbol* _class_loader_name;
  Symbol* _class_path;
};

#define KNUTH_HASH_MULTIPLIER  2654435761UL
#define ADDR_CHANGE_NUMBER 3

class JitProfileRecorder : public CHeapObj<mtInternal> {
public:
  enum RecorderState {
    IS_OK = 0,
    IS_ERR = 1,
    NOT_INIT = 2
  };
public:
  JitProfileRecorder();
  virtual ~JitProfileRecorder();

  void init();

  int class_init_count()                   { return _class_init_order_num + 1; }

  address current_init_order_addr() { return (address)&_class_init_order_num;}

  unsigned int is_flushed()                { return _flushed; }
  void         set_flushed(bool value)  { _flushed = value; }

  const char*  logfile_name()                      { return _record_file_name; }

  JitProfileRecorder* recorder() { return _jit_profile_cache_recorder; }

  JitProfileCache*   holder()                 { return _holder; }
  void         set_holder(JitProfileCache* h) { _holder = h; }

  unsigned int recorded_count()   { return _profile_record_dict->count(); }
  JitProfileRecordDictionary* dict() { return _profile_record_dict; }

  void         set_logfile_name(const char* name);

  bool is_valid() { return _recorder_state == IS_OK;}

  LinkedListImpl<ClassSymbolEntry>*
      class_init_list()                    { return _class_init_list; }

  void add_method(Method* method, int method_bci);

  void flush_record();

  bool param_check();

  int assign_class_init_order(InstanceKlass* klass);

  unsigned int compute_hash(Method* method) {
    uint64_t m_addr = (uint64_t)method;
    return (m_addr >> ADDR_CHANGE_NUMBER) * KNUTH_HASH_MULTIPLIER; // Knuth multiply hash
  }

  static int compute_crc32(randomAccessFileStream* fileStream);

private:
  int                                          _max_symbol_length;
  unsigned int                                 _pos;
  volatile int                                 _class_init_order_num;
  volatile bool                                _flushed;
  const char*                                  _record_file_name;

  JitProfileRecorder*                          _jit_profile_cache_recorder;
  JitProfileCache*                             _holder;
  randomAccessFileStream*                      _profilelog;
  RecorderState                                _recorder_state;
  LinkedListImpl<ClassSymbolEntry>*            _class_init_list;
  LinkedListNode<ClassSymbolEntry>*            _init_list_tail_node;
  JitProfileRecordDictionary*                     _profile_record_dict;

private:
  void write_u1(u1 value);
  void write_u4(u4 value);

  void write_profilecache_header();
  void write_inited_class();
  void write_profilecache_record(JitProfileRecorderEntry* entry, int bci, int order);
  void record_class_info(JitProfileRecorderEntry* entry);
  void record_method_info(JitProfileRecorderEntry* entry, int bci);
  void write_profilecache_footer();

  void write_string(const char* src, size_t len);
  void overwrite_u4(u4 value, unsigned int offset);

  void update_max_symbol_length(int len);
};

#endif