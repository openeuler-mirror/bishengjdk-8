/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef SHARE_VM_MATRIX_UBSOCKETMEMMAPPING_HPP
#define SHARE_VM_MATRIX_UBSOCKETMEMMAPPING_HPP

#include <stddef.h>

#include "memory/allocation.hpp"

class Symbol;

class UBSocketMemMapping : public CHeapObj<mtInternal> {
 public:
  static void init();
  static UBSocketMemMapping* acquire(const char* remote_name, size_t remote_size);

  Symbol* name() const { return _name; }
  void*   addr() const { return _addr; }
  size_t  size() const { return _size; }
  int     ref_count() const { return _ref_count; }
  UBSocketMemMapping* next() const { return _next; }
  void set_next(UBSocketMemMapping* next) { _next = next; }

  static bool unbind(int fd);
  static void release_mapping(UBSocketMemMapping* mapping);

 private:
  Symbol* _name;
  size_t _size;
  void* _addr;
  int _ref_count;
  UBSocketMemMapping* _next;

  // Registry for tracking shared memory mappings across connections
  static Monitor* _registry_lock;
  static UBSocketMemMapping* _registry_head;

  UBSocketMemMapping(Symbol* name, size_t size, void* addr);
  int increment_ref() { return ++_ref_count; }
  int decrement_ref() { return --_ref_count; }
  bool release();  // returns true if last reference and mapping needs to be deleted

  static UBSocketMemMapping* find_locked(Symbol* remote_name);
  static void remove_locked(UBSocketMemMapping* mapping);
};

#endif  // SHARE_VM_MATRIX_UBSOCKETMEMMAPPING_HPP
