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

// Represents one remote shared-memory mapping in the current JVM. The mapping
// instance owns the mmap lifetime and reference count; fd binding remains a
// separate concern handled by SocketDataInfoTable.
class UBSocketMemMapping : public CHeapObj<mtInternal> {
 public:
  static UBSocketMemMapping* acquire(Symbol* remote_name, size_t remote_size);

  Symbol* name() const { return _name; }
  void* addr() const { return _addr; }
  size_t size() const { return _size; }
  int ref_count() const { return _ref_count; }
  UBSocketMemMapping* next() const { return _next; }
  void set_next(UBSocketMemMapping* next) { _next = next; }

  bool release(int* ref_count_ptr);

 private:
  Symbol* _name;
  size_t _size;
  void* _addr;
  int _ref_count;
  UBSocketMemMapping* _next;

  UBSocketMemMapping(Symbol* name, size_t size, void* addr);
  void increment_ref() { ++_ref_count; }
  int decrement_ref();
};

bool ub_socket_unbind_remote_mapping(int socket_fd, char* remote_mem_name,
                                     size_t remote_mem_name_len, int* ref_count_ptr);

#endif  // SHARE_VM_MATRIX_UBSOCKETMEMMAPPING_HPP
