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

#ifndef SHARED_VM_JPROFILECACHE_JITPROFILECACHECLASS_HPP
#define SHARED_VM_JPROFILECACHE_JITPROFILECACHECLASS_HPP

#include "jprofilecache/jitProfileCacheHolders.hpp"
#include "memory/allocation.hpp"
#include "oops/klass.hpp"
#include "oops/method.hpp"
#include "oops/methodData.hpp"
#include "oops/methodCounters.hpp"


class ProfileCacheClassEntry : public HashtableEntry<Symbol*, mtInternal> {
  friend class JitProfileCacheInfo;
public:
  ProfileCacheClassEntry(ProfileCacheClassHolder* holder)
      : _head_holder(holder),
        _chain_offset(-1),
        _class_loader_name(NULL),
        _class_path(NULL) {

  }

  ProfileCacheClassEntry()
      : _head_holder(NULL),
        _chain_offset(-1),
        _class_loader_name(NULL),
        _class_path(NULL) {
  }

  virtual ~ProfileCacheClassEntry() {  }

  void init() {
      _head_holder = NULL;
      _chain_offset = -1;
      _class_loader_name = NULL;
      _class_path = NULL;
  }

  ProfileCacheClassHolder* head_holder()                          { return _head_holder; }
  void                set_head_holder(ProfileCacheClassHolder* h) { _head_holder = h; }

  int                 chain_offset()               { return _chain_offset; }
  void                set_chain_offset(int offset) { _chain_offset = offset; }

  Symbol*             class_loader_name()                { return _class_loader_name; }
  void                set_class_loader_name(Symbol* s)   { _class_loader_name = s; }
  Symbol*             class_path()                       { return _class_path; }
  void                set_class_path(Symbol* s)          { _class_path = s; }


  ProfileCacheClassEntry*  next() {
      return (ProfileCacheClassEntry*)HashtableEntry<Symbol*, mtInternal>::next();
  }

  void add_class_holder(ProfileCacheClassHolder* h) {
      h->set_next(_head_holder);
      _head_holder = h;
  }

  ProfileCacheClassHolder* find_class_holder(unsigned int size, unsigned int crc32);

private:
  ProfileCacheClassHolder* _head_holder;
  int                 _chain_offset;
  Symbol*             _class_loader_name;
  Symbol*             _class_path;

};

class JProfileCacheClassDictionary : public Hashtable<Symbol*, mtInternal> {
public:
  JProfileCacheClassDictionary(int size);
  virtual ~JProfileCacheClassDictionary();

  ProfileCacheClassEntry* find_entry(unsigned int hash_value, Symbol* name,
                                Symbol* loader_name, Symbol* path);

  ProfileCacheClassEntry* find_head_entry(unsigned int hash_value, Symbol* name);

  ProfileCacheClassEntry* find_entry(InstanceKlass* k);

  ProfileCacheClassEntry* bucket(int i) {
      return (ProfileCacheClassEntry*)Hashtable<Symbol*, mtInternal>::bucket(i);
  }

  ProfileCacheClassEntry* find_or_create_class_entry(unsigned int hash_value, Symbol* symbol,
                                              Symbol* loader_name, Symbol* path,
                                              int order);

private:

  ProfileCacheClassEntry* new_entry(Symbol* symbol);
};


#endif // SHARE_VM_JPROFILECACHE_JITPROFILECACHECLASS_HPP
