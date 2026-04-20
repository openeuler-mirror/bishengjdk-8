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

#ifndef SHARE_VM_MATRIX_MATRIXALLOWLIST_HPP
#define SHARE_VM_MATRIX_MATRIXALLOWLIST_HPP

#include "classfile/symbolTable.hpp"
#include "matrix/matrixManager.hpp"
#include "utilities/hashtable.hpp"

class AllowListEntry : public HashtableEntry<Symbol*, mtClass> {
 public:
  Symbol* class_name() const { return literal(); }
  Symbol* method_name() const { return _method_name; }
  void set_method_name(Symbol* method_name) { _method_name = method_name; }
  AllowListEntry* next() { return (AllowListEntry*)HashtableEntry<Symbol*, mtClass>::next(); }

 private:
  Symbol* _method_name;
};

class AllowListTable : public Hashtable<Symbol*, mtClass> {
 public:
  explicit AllowListTable(UBFeature feature, int table_size = 211)
      : Hashtable<Symbol*, mtClass>(table_size, sizeof(AllowListEntry)),
        _feature(feature) {}

  // Load one "pkg/Class.method" entry per non-comment line. Invalid lines are
  // ignored with a warning so one bad entry does not disable the whole file.
  int load_from_file(const char* conf_path);
  void add(Symbol* class_name, Symbol* method_name);
  bool contains(Symbol* class_name, Symbol* method_name);

  bool check_stack();

 private:
  UBFeature _feature;
  AllowListEntry* bucket_at(int index);
};

#endif  // SHARE_VM_MATRIX_MATRIXALLOWLIST_HPP
