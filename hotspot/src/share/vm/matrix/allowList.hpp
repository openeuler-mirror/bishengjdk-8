/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
 */

#ifndef SHARE_VM_MATRIX_ALLOWLIST_HPP
#define SHARE_VM_MATRIX_ALLOWLIST_HPP

#include "utilities/hashtable.hpp"
#include "classfile/symbolTable.hpp"

class AllowListEntry : public HashtableEntry<Symbol*, mtClass> {
public:
  Symbol* class_name() const  { return literal(); } // 从基类继承 literal() 存储类名
  Symbol* method_name() const { return _method_name; }

  void set_method_name(Symbol* method_name) { _method_name = method_name; }

  AllowListEntry* next() {
    return (AllowListEntry*)HashtableEntry<Symbol*, mtClass>::next();
  }
private:
  Symbol* _method_name;
};

class AllowListTable : public Hashtable<Symbol*, mtClass> {
public:
  explicit AllowListTable(int table_size = 211) : Hashtable<Symbol*, mtClass>(table_size, sizeof(AllowListEntry)) {}

  void add(Symbol* class_name, Symbol* method_name) {
    unsigned int hash = class_name->identity_hash();
    int index = hash_to_index(hash);
    AllowListEntry* entry = (AllowListEntry*)Hashtable<Symbol*, mtClass>::new_entry(hash, class_name);
    entry->set_method_name(method_name);
    add_entry(index, entry);
  }

  bool contains(Symbol* class_name, Symbol* method_name) {
    unsigned int hash = class_name->identity_hash();
    int index = hash_to_index(hash);
    for (AllowListEntry* entry = bucket(index); entry != NULL; entry = entry->next()) {
      if (entry->class_name()->fast_compare(class_name) == 0 && 
          entry->method_name()->fast_compare(method_name) == 0) {
        return true;
      }
    }
    return false;
  }
private:
  AllowListEntry* bucket(int i) {
      return (AllowListEntry*)Hashtable<Symbol*, mtClass>::bucket(i);
  }
};

#endif // SHARE_VM_MATRIX_ALLOWLIST_HPP