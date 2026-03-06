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

#include "precompiled.hpp"
#include "classfile/symbolTable.hpp"
#include "jprofilecache/jitProfileCacheClass.hpp"
#include "jprofilecache/jitProfileCacheUtils.hpp"
#include "jprofilecache/jitProfileRecord.hpp"
#include "oops/typeArrayKlass.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"

ProfileCacheClassHolder* ProfileCacheClassEntry::find_class_holder(unsigned int size,
                                                            unsigned int crc32) {
  for (ProfileCacheClassHolder* p = this->head_holder(); p != NULL; p = p->next()) {
    if (p->crc32() == crc32 && p->size() == size) {
      return p;
    }
  }
  return NULL;
}

#define HEADER_SIZE                             36

#define JVM_DEFINE_CLASS_PATH    "_JVM_DefineClass_"

JProfileCacheClassDictionary::JProfileCacheClassDictionary(int size)
  : Hashtable<Symbol*, mtInternal>(size, sizeof(ProfileCacheClassEntry)) {
}

JProfileCacheClassDictionary::~JProfileCacheClassDictionary() { }

ProfileCacheClassEntry* JProfileCacheClassDictionary::new_entry(Symbol* symbol) {
  unsigned int hash = symbol->identity_hash();
  ProfileCacheClassEntry* entry = (ProfileCacheClassEntry*)Hashtable<Symbol*, mtInternal>::
                             new_entry(hash, symbol);
  entry->init();
  return entry;
}

ProfileCacheClassEntry* JProfileCacheClassDictionary::find_entry(InstanceKlass* k) {
  Symbol* name = k->name();
  Symbol* path = k->source_file_path();
  if (path == NULL) {
    Thread* THREAD = Thread::current();
    path = SymbolTable::new_symbol(JVM_DEFINE_CLASS_PATH, THREAD);
  }
  Symbol* loader_name = JitProfileCacheUtils::get_class_loader_name(k->class_loader_data());
  int hash = name->identity_hash();
  return find_entry(hash, name, loader_name, path);
}

ProfileCacheClassEntry* JProfileCacheClassDictionary::find_entry(unsigned int hash_value,
                                                      Symbol* name,
                                                      Symbol* loader_name,
                                                      Symbol* path) {
  int index = hash_to_index(hash_value);
  for (ProfileCacheClassEntry* p = bucket(index); p != NULL; p = p->next()) {
    if (p->literal()->fast_compare(name) == 0 &&
        p->class_loader_name()->fast_compare(loader_name) == 0 &&
        p->class_path()->fast_compare(path) == 0) {
      return p;
    }
  }
  return NULL;
}

ProfileCacheClassEntry* JProfileCacheClassDictionary::find_head_entry(unsigned int hash_value,
                                                           Symbol* name) {
  int index = hash_to_index(hash_value);
  for (ProfileCacheClassEntry* p = bucket(index); p != NULL; p = p->next()) {
    if (p->literal()->fast_compare(name) == 0) {
      return p;
    }
  }
  return NULL;
}

ProfileCacheClassEntry* JProfileCacheClassDictionary::find_or_create_class_entry(unsigned int hash_value,
                                                                    Symbol* name,
                                                                    Symbol* loader_name,
                                                                    Symbol* path,
                                                                    int index) {
  ProfileCacheClassEntry* p = find_entry(hash_value, name, loader_name, path);
  if (p == NULL) {
    p = new_entry(name);
    p->set_chain_offset(index);
    p->set_class_loader_name(loader_name);
    p->set_class_path(path);
    add_entry(hash_to_index(hash_value), p);
  }
  return p;
}
