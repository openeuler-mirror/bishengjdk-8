/*
 * Copyright (c) 2025, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2019 Alibaba Group Holding Limited. All rights reserved.
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
 */

#ifndef SHARED_VM_JPROFILECACHE_JITPROFILECACHEHOLDERS_HPP
#define SHARED_VM_JPROFILECACHE_JITPROFILECACHEHOLDERS_HPP

#include "memory/allocation.hpp"
#include "utilities/hashtable.hpp"
#include "oops/klass.hpp"
#include "oops/method.hpp"
#include "oops/methodData.hpp"
#include "oops/methodCounters.hpp"

class BoolObjectClosure;

class BytecodeProfileRecord : public CHeapObj<mtInternal> {
public:
  BytecodeProfileRecord(int size) : _size_in_bytes(size) {
    _data = NEW_C_HEAP_ARRAY(char, size, mtInternal);
  }
  ~BytecodeProfileRecord() { if (_size_in_bytes > 0) {FREE_C_HEAP_ARRAY(char, _data, mtInternal);}}

  DataLayout* data_in() const { return (DataLayout*)_data; }
  char* data()                { return _data;}
  int size_in_bytes()   const { return _size_in_bytes; }
  u2 bci()              const { return data_in()->bci(); }

  bool is_BranchData()      const { return data_in()->data_in()->is_BranchData(); }
  bool is_MultiBranchData() const { return data_in()->data_in()->is_MultiBranchData(); }
  bool is_ArgInfoData()     const { return data_in()->data_in()->is_ArgInfoData(); }

private:
  char* _data;
  int _size_in_bytes;
};

class ProfileCacheMethodHold : public CHeapObj<mtInternal> {
  friend class ProfileCacheClassHolder;
public:
  ProfileCacheMethodHold(Symbol* name, Symbol* signature);
  ProfileCacheMethodHold(ProfileCacheMethodHold& rhs);
  virtual ~ProfileCacheMethodHold();

  Symbol* method_name()      const { return _method_name; }
  Symbol* method_signature() const { return _method_signature; }

  unsigned int invocation_count() const { return _invocation_count;}
  int compile_level()             const { return _compile_level; }

  void set_interpreter_invocation_count(unsigned int value) { _interpreter_invocation_count = value; }
  void set_interpreter_exception_count(unsigned int value)   { _interpreter_exception_count = value; }
  void set_invocation_count(unsigned int value)      { _invocation_count = value; }
  void set_backage_count(unsigned int value)         { _backage_count = value; }
  void set_compile_level(int value)                  { _compile_level = value; }

  void set_method_hash(unsigned int value)           { _method_hash = value; }
  void set_method_size(unsigned int value)           { _method_size = value; }
  void set_mounted_offset(int value)          { _mounted_offset = value; }

  bool is_method_match(Method* method);

  ProfileCacheMethodHold* next() const                     { return _next; }
  void set_next(ProfileCacheMethodHold* h)                 { _next = h; }

  Method* resolved_method() const                       { return _resolved_method; }
  void set_resolved_method(Method* m)                   { _resolved_method = m; }

  GrowableArray<BytecodeProfileRecord*>* profile_list()         const { return _profile_list; }
  void set_profile_list(GrowableArray<BytecodeProfileRecord*>* value) { _profile_list = value; }

  ProfileCacheMethodHold* clone_and_add();

  bool is_alive() const;
  bool is_alive(BoolObjectClosure* is_alive_closure) const;

private:
  Symbol*      _method_name;
  Symbol*      _method_signature;

  unsigned int _method_size;
  unsigned int _method_hash;

  unsigned int _interpreter_invocation_count;
  unsigned int _interpreter_exception_count;
  unsigned int _invocation_count;
  unsigned int _backage_count;
  int          _compile_level;

  int          _mounted_offset;

  bool         _owns_profile_list;

  // A single linked list stores entries with the same initialization order
  ProfileCacheMethodHold*           _next;
  // The resolved method within the holder's list
  Method*                        _resolved_method;
  // An array of profile information, shared among entries with the same
  GrowableArray<BytecodeProfileRecord*>*  _profile_list;
};

class ProfileCacheClassHolder : public CHeapObj<mtInternal> {
public:
  ProfileCacheClassHolder(Symbol* name, Symbol* loader_name,
                     Symbol* path, unsigned int size,
                     unsigned int hash, unsigned int crc32);
  virtual ~ProfileCacheClassHolder();

  void add_method(ProfileCacheMethodHold* mh) {
      assert(_class_method_list != NULL, "not initialize");
      _class_method_list->append(mh);
  }

  unsigned int             size() const              { return _class_size; }
  unsigned int             hash() const              { return _class_hash; }
  unsigned int             crc32() const             { return _class_crc32; }
  unsigned int             methods_count() const     { return _class_method_list->length(); }
  Symbol*                  class_name() const        { return _class_name; }
  Symbol*                  class_loader_name() const { return _class_loader_name; }
  Symbol*                  path() const              { return _class_path; }
  ProfileCacheClassHolder*      next() const              { return _next; }
  bool                     resolved() const          { return _class_resolved; }

  void                     set_resolved()            { _class_resolved = true; }
  void                     set_next(ProfileCacheClassHolder* h) { _next = h; }

  GrowableArray<ProfileCacheMethodHold*>* method_list() const { return _class_method_list; }

private:
  Symbol*                                  _class_name;
  Symbol*                                  _class_loader_name;
  Symbol*                                  _class_path;

  unsigned int                             _class_size;
  unsigned int                             _class_hash;
  unsigned int                             _class_crc32;
  unsigned int                             _class_init_chain_index;

  bool                                     _class_resolved;

  GrowableArray<ProfileCacheMethodHold*>*  _class_method_list;

  ProfileCacheClassHolder*                  _next;
};

#endif // SHARED_VM_JPROFILECACHE_JITPROFILECACHEHOLDERS_HPP
