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

#ifndef SHARE_VM_MATRIX_UBFILEMEMPOOL_HPP
#define SHARE_VM_MATRIX_UBFILEMEMPOOL_HPP

#include "memory/allocation.hpp"
#include "runtime/atomic.hpp"
#include "runtime/orderAccess.hpp"
#include "utilities/growableArray.hpp"

class BlockHeader VALUE_OBJ_CLASS_SPEC {
 public:
  volatile void* _next;
  size_t _used;
  int _kind;

  void set_next(void* n) { OrderAccess::release_store_ptr(&_next, n); }
  void* next() const { return (void*)OrderAccess::load_ptr_acquire(&_next); }
};

class UBFileMemPool : public AllStatic {
 public:
  static bool init();
  static bool release();
  static void* mmap_remote_memory(const char* name, size_t size = 0);
  static int flush_shared_memory(void* start, size_t size = 0);
  static int munmap_shared_memory(void* start, size_t size = 0);
  static void* seek_shared_memory(void* start, size_t set_off, size_t* size,
                                  size_t* offset);
  static int get_used_size(void* data_addr, size_t* size);
  static int total_memory_info(size_t* used, size_t* alloc, size_t* total);
  static int malloc_remote_memory(const char* name, size_t size = 0);
  static int free_remote_memory(const char* name);
  static int rename_remote_memory(const char* from, const char* to);
  static int remote_name_exist(const char* name, bool* exist);
  static int shared_addr_exist(void* addr, bool* exist);

 private:
  static const int MAX_KIND_LIMIT = 5;
  static const int MAX_SCAN = 300;
  static const int BLOCK_SIZE_SHIFT_OFFSET = 2;
  static const size_t M = 1024 * 1024;
  static const size_t ALLOC_SIZE = 2UL * 1024 * 1024 * 1024;

  static void* _occupied_addr;
  static bool _is_initialized;

  static GrowableArray<GrowableArray<void*>*>* _block_list;
  static size_t _block_list_num[MAX_KIND_LIMIT];
  static volatile jint _free_idx_list[MAX_KIND_LIMIT];

  static size_t get_blk_size(int kind);
  static void* alloc_new_block(int kind);
  static bool allocate_memory(int kind, size_t blk_num);
  static bool release_memory(int kind);
  static void* start_by_data_addr(void* data_addr);
};

#endif  // SHARE_VM_MATRIX_UBFILEMEMPOOL_HPP