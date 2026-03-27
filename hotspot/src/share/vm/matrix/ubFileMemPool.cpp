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

#include "matrix/ubFileMemPool.hpp"

#include <sys/mman.h>

#include "matrix/matrixManager.hpp"
#include "precompiled.hpp"
#include "runtime/atomic.inline.hpp"
#include "runtime/orderAccess.inline.hpp"
#include "runtime/os.hpp"
#include "utilities/debug.hpp"
#include "utilities/ostream.hpp"

void* UBFileMemPool::_occupied_addr = (void*)(uintptr_t)-1;
bool UBFileMemPool::_is_initialized = false;

GrowableArray<GrowableArray<void*>*>* UBFileMemPool::_block_list = NULL;
size_t UBFileMemPool::_block_list_num[MAX_KIND_LIMIT] = {12288, 12288, 7680, 7680, 3840};
volatile jint UBFileMemPool::_free_idx_list[MAX_KIND_LIMIT] = {0, 0, 0, 0, 0};

void* UBFileMemPool::start_by_data_addr(void* data_addr) {
  return (char*)data_addr - sizeof(BlockHeader);
}

size_t UBFileMemPool::get_blk_size(int kind) {
  return M * (1ULL << (kind + BLOCK_SIZE_SHIFT_OFFSET));
}

bool UBFileMemPool::allocate_memory(int kind, size_t blk_num) {
  size_t blk_size = get_blk_size(kind);
  size_t total_size = blk_size * blk_num;
  int alloc_count = 0;
  int block_count = 0;
  GrowableArray<void*>* blk_record_list = new (ResourceObj::C_HEAP, mtInternal)
      GrowableArray<void*>((int)blk_num, true);
  while (ALLOC_SIZE * alloc_count++ < total_size) {
    int ret = 0;
    char* addr = (char*)os::Linux::ub_mem_borrow(ALLOC_SIZE, &ret);
    if (addr == NULL) {
      UB_LOG("ERROR", "ub_mem_borrow failed, code %d\n", ret);
      _block_list->append(blk_record_list);  // easy to release later
      release_memory(kind);
      return false;
    }
    for (int count = 0;
         blk_size * count < ALLOC_SIZE && block_count < (int)blk_num; count++) {
      void* block_addr = addr + blk_size * count;
      BlockHeader* blk_header = (BlockHeader*)block_addr;
      blk_header->set_next(NULL);
      blk_header->_used = 0;
      blk_header->_kind = kind;

      blk_record_list->append(block_addr);
      block_count++;
    }
  }

  _block_list->at_put(kind, blk_record_list);
  OrderAccess::release_store(&_free_idx_list[kind], (jint)0);
  _is_initialized = true;
  return true;
}

bool UBFileMemPool::init() {
  _block_list = new (ResourceObj::C_HEAP, mtInternal)
      GrowableArray<GrowableArray<void*>*>(MAX_KIND_LIMIT, true);
  for (int i = 0; i < MAX_KIND_LIMIT; i++) {
    _block_list->append(NULL);
  }

  for (int kind = 0; kind < MAX_KIND_LIMIT; kind++) {
    bool is_success = allocate_memory(kind, _block_list_num[kind]);
    if (!is_success) {
      for (int i = 0; i < kind; i++) {
        release_memory(i);
      }
      return false;
    }
  }
  UB_LOG("DEBUG", "UBFileMemPool init success.\n");
  return true;
}

bool UBFileMemPool::release_memory(int kind) {
  GrowableArray<void*>* blk_list = _block_list->at(kind);
  if (blk_list == NULL || blk_list->length() == 0) {
    return true;
  }
  size_t blk_size = get_blk_size(kind);
  guarantee(blk_size > 0, "block size must be positive");
  size_t blocks_per_alloc = ALLOC_SIZE / blk_size;
  size_t total_released = 0;
  int released_count = 0;
  bool is_success = true;

  for (int i = 0; i < blk_list->length(); i += blocks_per_alloc) {
    char* block_addr = (char*)blk_list->at(i);
    int error_code = os::Linux::ub_mem_return(block_addr, ALLOC_SIZE);
    if (error_code == 0) {
      total_released += ALLOC_SIZE;
      released_count++;
    } else {
      is_success = false;
      UB_LOG("ERROR", "ub_mem_return %p size %ld failed, code %d\n", block_addr,
             ALLOC_SIZE, error_code);
    }
  }
  delete blk_list;
  _block_list->at_put(kind, NULL);
  OrderAccess::release_store(&_free_idx_list[kind], (jint)0);
  UB_LOG("DEBUG", "UB File Mem Released kind=%d, blocks=%d, size=%zu\n", kind,
         released_count, total_released);
  return is_success;
}

bool UBFileMemPool::release() {
  if (!_is_initialized) {
    return true;
  }
  int count = 0;
  for (int kind = 0; kind < MAX_KIND_LIMIT; kind++) {
    bool is_success = release_memory(kind);
    if (is_success) count++;
  }
  return count == MAX_KIND_LIMIT;
}

void* UBFileMemPool::alloc_new_block(int kind) {
  int blk_num = (int)_block_list_num[kind];
  if (blk_num == 0) return NULL;

  jint start_idx = OrderAccess::load_acquire(&_free_idx_list[kind]);
  int scanned = 0;
  int idx = (int)start_idx;

  GrowableArray<void*>* blk_list = _block_list->at(kind);

  while (scanned < MAX_SCAN) {
    void* block_addr = blk_list->at(idx);
    BlockHeader* header = (BlockHeader*)block_addr;
    void* current = header->next();

    if (current == NULL) {
      if (Atomic::cmpxchg_ptr(_occupied_addr, (volatile void**)&header->_next,
                              NULL) == NULL) {
        jint next_idx = (idx + 1) % blk_num;
        Atomic::cmpxchg(next_idx, &_free_idx_list[kind], start_idx);
        return block_addr;
      }
    }
    idx = (idx + 1) % blk_num;
    scanned++;
  }

  idx = 0;
  while (scanned < MAX_SCAN) {
    void* block_addr = blk_list->at(idx);
    BlockHeader* header = (BlockHeader*)block_addr;
    void* current = header->next();

    if (current == NULL) {
      if (Atomic::cmpxchg_ptr(_occupied_addr, (volatile void**)&header->_next,
                              NULL) == NULL) {
        return block_addr;
      }
    }
    idx = (idx + 1) % blk_num;
    scanned++;
  }

  UB_LOG("INFO", "UBFileMemPool Error: Memory pool exhausted for kind %d\n",
         kind);
  return NULL;
}

void* UBFileMemPool::mmap_remote_memory(const char* name, size_t size) {
  void* block_addr = alloc_new_block(0);
  if (block_addr == NULL) return NULL;
  return (char*)block_addr + sizeof(BlockHeader);
}

int UBFileMemPool::flush_shared_memory(void* start, size_t size) {
  if (size == 0) {
    get_used_size(start, &size);
  }
  void* addr = start_by_data_addr(start);
  size_t recorded_size = 0;
  while (addr != NULL) {
    BlockHeader* blk_header = (BlockHeader*)addr;
    void* next = blk_header->next();
    if (next == _occupied_addr) {
      blk_header->_used = size - recorded_size;
      break;
    } else {
      recorded_size += get_blk_size(blk_header->_kind) - sizeof(BlockHeader);
    }
    addr = next;
  }
  return 0;
}

int UBFileMemPool::munmap_shared_memory(void* start, size_t size) {
  void* addr = start_by_data_addr(start);
  while (addr != NULL) {
    BlockHeader* header = (BlockHeader*)addr;
    void* next = (void*)header->_next;
    header->set_next(NULL);
    header->_used = 0;
    if (next == _occupied_addr) break;
    addr = next;
  }
  return 0;
}

int UBFileMemPool::get_used_size(void* data_addr, size_t* size) {
  void* addr = start_by_data_addr(data_addr);
  size_t total_size = 0;
  while (addr != NULL) {
    BlockHeader* blk_header = (BlockHeader*)addr;
    total_size += blk_header->_used;
    void* next = blk_header->next();
    if (next == _occupied_addr) break;
    addr = next;
  }
  *size = total_size;
  return 0;
}

void* UBFileMemPool::seek_shared_memory(void* start, size_t set_off,
                                        size_t* size, size_t* offset) {
  void* head_addr = start_by_data_addr(start);
  BlockHeader* old_header = (BlockHeader*)head_addr;
  size_t cap = get_blk_size(old_header->_kind) - sizeof(BlockHeader);
  if (set_off < cap) {
    *size = cap;
    *offset = set_off;
    return start;
  }

  set_off -= cap;
  // has next blk
  void* old_next = old_header->next();
  if (old_next != NULL && old_next != _occupied_addr) {
    void* data_addr = (char*)old_next + sizeof(BlockHeader);
    return seek_shared_memory(data_addr, set_off, size, offset);
  }
  // old blk is full if need alloc new blk
  old_header->_used = cap;
  // no next blk, need alloc
  int next_kind = (old_header->_kind + 1) % MAX_KIND_LIMIT;
  void* new_blk_addr = alloc_new_block(next_kind);
  if (new_blk_addr == NULL) {
    return NULL;
  }
  old_header->set_next(new_blk_addr);
  void* data_addr = (char*)new_blk_addr + sizeof(BlockHeader);
  return seek_shared_memory(data_addr, set_off, size, offset);
}

int UBFileMemPool::malloc_remote_memory(const char* name, size_t size) {
  return 0;
}

int UBFileMemPool::free_remote_memory(const char* name) { return 0; }

int UBFileMemPool::rename_remote_memory(const char* from, const char* to) {
  return 0;
}

int UBFileMemPool::remote_name_exist(const char* name, bool* exist) {
  *exist = false;
  return 0;
}

int UBFileMemPool::shared_addr_exist(void* addr, bool* exist) {
  *exist = false;
  return 0;
}

int UBFileMemPool::total_memory_info(size_t* used, size_t* alloc,
                                     size_t* total) {
  size_t sum_used = 0;
  size_t sum_alloc = 0;
  size_t sum_total = 0;

  for (int kind = 0; kind < MAX_KIND_LIMIT; kind++) {
    size_t blk_size = get_blk_size(kind);
    int used_blk_count = 0;
    int full_blk_count = 0;

    if (_block_list == NULL || _block_list->at(kind) == NULL) {
      break;
    }
    GrowableArray<void*>* list = _block_list->at(kind);
    for (int idx = 0; idx < list->length(); idx++) {
      void* addr = list->at(idx);
      BlockHeader* blk_header = (BlockHeader*)addr;
      void* next = (void*)blk_header->_next;
      if (next != NULL) {
        sum_used += blk_header->_used;
        sum_alloc += blk_size;
        used_blk_count++;
        full_blk_count =
            (next != _occupied_addr) ? full_blk_count++ : full_blk_count;
      }
      sum_total += blk_size;
    }
    if (_is_initialized) {
      UB_LOG("INFO", "UBFileMemPool Mem info: kind %d : %d / %d / %d\n", kind,
             full_blk_count, used_blk_count, list->length());
    }
  }
  *used = sum_used;
  *alloc = sum_alloc;
  *total = sum_total;
  return 0;
}