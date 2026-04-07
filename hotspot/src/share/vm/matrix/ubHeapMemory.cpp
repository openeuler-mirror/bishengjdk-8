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

#include <sys/mman.h>
#include "precompiled.hpp"
#include "matrix/ubHeapMemory.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "utilities/debug.hpp"
#include "utilities/macros.hpp"

#define LOG_INFO(fmt, ...) tty->print_cr("[UB HEAP][INFO] " fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) tty->print_cr("[UB HEAP][WARN] " fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) tty->print_cr("[UB HEAP][ERROR] " fmt, ##__VA_ARGS__)

UBFixedPoolState UBHeapMemory::_g_pool = UBFixedPoolState();

size_t    UBHeapFixedMemPool::_size = 0;
uintptr_t UBHeapFixedMemPool::_base = 0;
uintptr_t UBHeapFixedMemPool::_end = 0;
bool      UBHeapFixedMemPool::_initialized = false;
// UB memory alignment depends on the low-level API and hardware requirements.
const size_t UBHeapMemory::_ub_heap_alignment = 4L * M;
// Off-heap memory is borrowed only if the Unsafe allocation size is greater than this threshold.
const size_t UBHeapMemory::_ub_unsafe_filter_threshold = 128L * M;

UBMemoryChunk* UBHeapMemory::create_chunk(void* base_addr, size_t size, int* ret_code) {
  UBMemoryChunk* chunk = NEW_C_HEAP_OBJ(UBMemoryChunk, mtInternal);
  if (chunk == NULL) {
    set_ret_code(ret_code, MEM_ENOMEM);
    return NULL;
  }

  void* mapped = NULL;
  int error_code = 0;
  if (USE_LOCAL_MEM) {
    mapped = os::Linux::ub_mem_borrow(size, &error_code, base_addr);
  } else {
    LOG_ERROR("Not implemented yet");
    set_ret_code(ret_code, MEM_EIMPLEMENTED);
    return NULL;
  }

  if (mapped == NULL || mapped != base_addr || error_code != 0) {
    FreeHeap(chunk);
    LOG_ERROR("create chunk error, code: %d", error_code);
    set_ret_code(ret_code, MEM_ENOMEM);
    return NULL;
  }

  size_t map_size = (size + _g_pool._page_size - 1) / _g_pool._page_size;
  uint8_t* usage_map = NEW_C_HEAP_ARRAY(uint8_t, map_size, mtInternal);
  if (usage_map == NULL) {
    os::Linux::ub_mem_return(base_addr, size);
    FreeHeap(chunk);
    set_ret_code(ret_code, MEM_ENOMEM);
    return NULL;
  }
  memset(usage_map, 0, map_size);

  chunk->_lock = new Mutex(Mutex::leaf, "UBChunk lock", true);
  if (chunk->_lock == NULL) {
    os::Linux::ub_mem_return(base_addr, size);
    FreeHeap(usage_map);
    FreeHeap(chunk);
    set_ret_code(ret_code, MEM_ENOMEM);
    return NULL;
  }

  chunk->_base_addr = base_addr;
  chunk->_size = size;
  chunk->_usage_map = usage_map;
  chunk->_map_size = map_size;

  LOG_INFO("Created chunk at %p (size: %zuMB)", base_addr, size / M);
  set_ret_code(ret_code, MEM_SUCCESS);
  return chunk;
}

int UBHeapMemory::destroy_chunk(UBMemoryChunk* chunk) {
  if (chunk == NULL) {
    return MEM_SUCCESS;
  }

  UBMemResult ret_code = MEM_SUCCESS;
  LOG_INFO("Destroyed chunk at %p", chunk->_base_addr);
  if (chunk->_base_addr != NULL) {
    int ret = 0;
    if (USE_LOCAL_MEM) {
      ret = os::Linux::ub_mem_return(chunk->_base_addr, chunk->_size);
    } else {
      LOG_ERROR("Not implemented yet");
      return MEM_EFAULT;
    }
    if (ret != 0) {
      LOG_ERROR("Free chunk memory failed, addr: %p error_code: %d", chunk->_base_addr, ret);
      ret_code = MEM_ENOMEM;
    }
  }

  if (chunk->_usage_map != NULL) {
    FreeHeap(chunk->_usage_map);
  }

  if (chunk->_lock != NULL) {
    delete chunk->_lock;
  }

  FreeHeap(chunk);
  return ret_code;
}

int UBHeapMemory::expand_to_size(size_t size_in_bytes) {
  LOG_INFO("expand_to_size %zuMB", size_in_bytes / M);

  MutexLockerEx ml(_g_pool._global_lock, Mutex::_no_safepoint_check_flag);

  if (_g_pool._allocated_size >= size_in_bytes) {
    LOG_INFO("Sufficient free memory available");
    return MEM_SUCCESS;
  }

  size_t needed = size_in_bytes - _g_pool._allocated_size;

  size_t blocks_to_alloc = (needed + _g_pool._chunk_size - 1) / _g_pool._chunk_size;
  size_t fixed_blocks_to_alloc = blocks_to_alloc;
  if (blocks_to_alloc * _g_pool._chunk_size + _g_pool._allocated_size > _g_pool._total_size) {
    fixed_blocks_to_alloc = blocks_to_alloc - 1;
  }

  int ret_code = MEM_SUCCESS;

  for (size_t i = 0; i < fixed_blocks_to_alloc; i++) {
    if (_g_pool._num_chunks >= _g_pool._max_chunks) {
      LOG_ERROR("Reached max chunks limit");
      return MEM_ERANGE;
    }

    void* chunk_addr = (void*)((uintptr_t)_g_pool._base_addr + _g_pool._num_chunks * _g_pool._chunk_size);
    UBMemoryChunk* chunk = create_chunk(chunk_addr, _g_pool._chunk_size, &ret_code);
    if (chunk == NULL) {
      LOG_ERROR("Failed to create chunk");
      return ret_code;
    }

    _g_pool._chunks[_g_pool._num_chunks++] = chunk;
    _g_pool._allocated_size += _g_pool._chunk_size;
  }

  if (blocks_to_alloc > fixed_blocks_to_alloc) {
    size_t remaining_size = _g_pool._total_size - _g_pool._allocated_size;

    if (remaining_size % _ub_heap_alignment != 0) {
      LOG_ERROR("Remaining size %zuMB is not aligned to %zuMB", remaining_size,
                _ub_heap_alignment / M);
      return MEM_EFAULT;
    }

    void* chunk_addr = (void*)((uintptr_t)_g_pool._base_addr + _g_pool._num_chunks * _g_pool._chunk_size);
    UBMemoryChunk* chunk = create_chunk(chunk_addr, remaining_size, &ret_code);
    if (chunk == NULL || ret_code != MEM_SUCCESS) {
      LOG_ERROR("Failed to create chunk");
      return ret_code;
    }

    _g_pool._chunks[_g_pool._num_chunks++] = chunk;
    _g_pool._allocated_size += remaining_size;
  }

  LOG_INFO("after expand_to_size pool allocated size is %zuMB", _g_pool._allocated_size / M);
  return MEM_SUCCESS;
}

int UBHeapMemory::chunk_acquire(UBMemoryChunk* chunk, uintptr_t addr, size_t size, void** result_ptr) {
  uintptr_t chunk_base = (uintptr_t)chunk->_base_addr;
  size_t offset = addr - chunk_base;
  size_t start_page = offset / _g_pool._page_size;
  size_t end_page = (offset + size + _g_pool._page_size - 1) / _g_pool._page_size;

  if (end_page > chunk->_map_size) {
    LOG_ERROR("Page index out of range: end_page=%zu, map_size=%zu", end_page, chunk->_map_size);
    return MEM_ERANGE;
  }

  MutexLockerEx ml(chunk->_lock, Mutex::_no_safepoint_check_flag);

  int overlap_page = 0;
  for (size_t i = start_page; i < end_page; i++) {
    if (chunk->_usage_map[i]) {
      overlap_page++;
    }
    chunk->_usage_map[i] = 1;
  }

  size_t page_count = end_page - start_page;
  size_t add_used = (page_count - overlap_page) * _g_pool._page_size;

  size_t new_used = (size_t)Atomic::add((jlong)add_used, (volatile jlong*)&_g_pool._used_size);
  size_t old_used = new_used - add_used;

  LOG_INFO("Used size: %.2fMB -> %.2fMB (added %.2fMB)",
           (double)old_used / M, (double)new_used / M, (double)add_used / M);

  *result_ptr = (void*)addr;

  if (overlap_page > 0) {
    LOG_WARN("%d pages overlapped in chunk", overlap_page);
  }

  return MEM_SUCCESS;
}

int UBHeapMemory::chunk_release(UBMemoryChunk* chunk, uintptr_t addr, size_t size) {
  uintptr_t chunk_base = (uintptr_t)chunk->_base_addr;
  size_t offset = addr - chunk_base;
  size_t start_page = offset / _g_pool._page_size;
  size_t end_page = (offset + size + _g_pool._page_size - 1) / _g_pool._page_size;

  if (end_page > chunk->_map_size) {
    LOG_ERROR("Page index out of range: end_page=%zu, map_size=%zu", end_page, chunk->_map_size);
    return MEM_ERANGE;
  }

  MutexLockerEx ml(chunk->_lock, Mutex::_no_safepoint_check_flag);

  int empty_page = 0;
  size_t actual_released = 0;

  for (size_t i = start_page; i < end_page; i++) {
    if (chunk->_usage_map[i]) {
      actual_released += _g_pool._page_size;
    } else {
      empty_page++;
    }
    chunk->_usage_map[i] = 0;
  }

  size_t new_used = (size_t)Atomic::add(-(jlong)actual_released, (volatile jlong*)&_g_pool._used_size);
  size_t old_used = new_used + actual_released;

  LOG_INFO("Used size: %.2fMB -> %.2fMB (released %.2fMB)",
           (double)old_used / M, (double)new_used / M, (double)actual_released / M);

  if (empty_page > 0) {
    LOG_WARN("%d pages were already empty", empty_page);
  }

  return MEM_SUCCESS;
}

int UBHeapMemory::fixed_mem_init(void* base_addr, size_t total_size) {
  LOG_INFO("Initializing pool (base: %p, size: %zuMB)", base_addr, total_size / M);

  if (!base_addr || total_size == 0) {
    return MEM_EINVAL;
  }
  if (_g_pool._initialized) {
    return MEM_EALREADY;
  }

  if (total_size % _ub_heap_alignment != 0) {
    LOG_ERROR("Total size %zu bytes is not aligned to %zuMB", total_size, _ub_heap_alignment / M);
    return MEM_EINVAL;
  }

  _g_pool._global_lock = new Mutex(Mutex::leaf + 1, "matrix heap mem_pool Global_lock", true);
  if (_g_pool._global_lock == NULL) {
    return MEM_EFAULT;
  }

  _g_pool._page_size = (size_t)os::vm_page_size();
  if (_g_pool._page_size <= 0) {
    delete _g_pool._global_lock;
    return MEM_EFAULT;
  }

  _g_pool._chunk_size = EXPANSION_UNIT;
  _g_pool._max_chunks = (total_size + _g_pool._chunk_size - 1) / _g_pool._chunk_size;
  _g_pool._chunks = NEW_C_HEAP_ARRAY(UBMemoryChunk*, _g_pool._max_chunks, mtInternal);
  if (!_g_pool._chunks) {
    delete _g_pool._global_lock;
    return MEM_EFAULT;
  }
  memset(_g_pool._chunks, 0, sizeof(UBMemoryChunk*) * _g_pool._max_chunks);

  _g_pool._base_addr = base_addr;
  _g_pool._total_size = total_size;
  _g_pool._used_size = 0;
  _g_pool._allocated_size = 0;
  _g_pool._num_chunks = 0;
  _g_pool._initialized = 1;

  LOG_INFO("Pool initialized successfully, total size is %zu", _g_pool._total_size);
  return MEM_SUCCESS;
}

int UBHeapMemory::fixed_mem_cleanup(void) {
  LOG_INFO("Cleaning up memory pool");

  if (!_g_pool._initialized) {
    return MEM_ENOTINIT;
  }

  // Cleanup is invoked after thread deletion, so no lock is required.
  bool all_success = true;
  for (size_t i = 0; i < _g_pool._num_chunks; i++) {
    if (_g_pool._chunks[i]) {
      int ret_code = destroy_chunk(_g_pool._chunks[i]);
      if (ret_code != MEM_SUCCESS) {
        all_success = false;
      }
    }
  }

  FreeHeap(_g_pool._chunks);
  delete _g_pool._global_lock;
  memset(&_g_pool, 0, sizeof(_g_pool));

  if (!all_success) {
    LOG_ERROR("Pool cleanup partially failed");
    return MEM_EFAULT;
  }

  LOG_INFO("Pool cleanup completed");
  return MEM_SUCCESS;
}

int UBHeapMemory::fixed_mem_expand_to(size_t size_in_bytes) {
  LOG_INFO("Expanding pool to %zuMB", size_in_bytes / M);

  if (!_g_pool._initialized) {
    return MEM_ENOTINIT;
  }
  if (size_in_bytes > _g_pool._total_size) {
    return MEM_ERANGE;
  }

  int ret = expand_to_size(size_in_bytes);
  if (ret != MEM_SUCCESS) {
    LOG_ERROR("Expansion failed: %d", ret);
    return ret;
  }

  LOG_INFO("Expanding pool to %zuMB successfully", size_in_bytes / M);
  return MEM_SUCCESS;
}

int UBHeapMemory::fixed_mem_acquire(void* requested_addr, size_t size_bytes, void** result_ptr) {
  LOG_INFO("Acquiring %zuMB at %p", size_bytes / M, requested_addr);

  if (!requested_addr || !size_bytes || !result_ptr) {
    return MEM_EINVAL;
  }
  if (!_g_pool._initialized) {
    return MEM_ENOTINIT;
  }

  uintptr_t base = (uintptr_t)_g_pool._base_addr;
  uintptr_t req_addr = (uintptr_t)requested_addr;
  uintptr_t req_end = req_addr + size_bytes;
  uintptr_t pool_end = base + _g_pool._total_size;

  if (req_addr < base || req_end > pool_end) {
    LOG_ERROR("Address out of pool range");
    return MEM_ERANGE;
  }

  size_t expect_size = req_end - base;
  int ret = expand_to_size(expect_size);
  if (ret != MEM_SUCCESS) {
    LOG_ERROR("Expansion failed: %d", ret);
    return ret;
  }

  {
    MutexLockerEx ml(_g_pool._global_lock, Mutex::_no_safepoint_check_flag);

    size_t start_chunk = (req_addr - base) / _g_pool._chunk_size;
    size_t end_chunk = (req_end - 1 - base) / _g_pool._chunk_size;
    uintptr_t current_addr = req_addr;
    size_t remaining = size_bytes;

    for (size_t chunk_idx = start_chunk; chunk_idx <= end_chunk; chunk_idx++) {
      if (chunk_idx >= _g_pool._num_chunks) {
        LOG_ERROR("Chunk index %zu out of range", chunk_idx);
        return MEM_EFAULT;
      }

      UBMemoryChunk* chunk = _g_pool._chunks[chunk_idx];
      uintptr_t chunk_base = (uintptr_t)chunk->_base_addr;
      uintptr_t chunk_end = chunk_base + chunk->_size;
      uintptr_t part_start = current_addr;
      uintptr_t part_end = (req_end < chunk_end) ? req_end : chunk_end;
      size_t part_size = part_end - part_start;
      void* part_result;
      ret = chunk_acquire(chunk, part_start, part_size, &part_result);
      if (ret != MEM_SUCCESS) {
        LOG_ERROR("Chunk acquire failed: %d", ret);
        return ret;
      }
      current_addr = part_end;
      remaining -= part_size;
      if (remaining <= 0) {
        break;
      }
    }
  }

  *result_ptr = requested_addr;
  LOG_INFO("Memory acquired successfully");
  return MEM_SUCCESS;
}

int UBHeapMemory::fixed_mem_release(void* addr, size_t size_bytes) {
  LOG_INFO("Releasing %zuMB at %p", size_bytes / M, addr);

  if (!addr || !size_bytes) {
    return MEM_EINVAL;
  }
  if (!_g_pool._initialized) {
    return MEM_ENOTINIT;
  }

  MutexLockerEx ml(_g_pool._global_lock, Mutex::_no_safepoint_check_flag);

  uintptr_t base = (uintptr_t)_g_pool._base_addr;
  uintptr_t rel_addr = (uintptr_t)addr;
  uintptr_t rel_end = rel_addr + size_bytes;
  uintptr_t pool_end = base + _g_pool._total_size;

  if (rel_addr < base || rel_end > pool_end) {
    LOG_ERROR("Address out of pool range");
    return MEM_ERANGE;
  }

  size_t start_chunk = (rel_addr - base) / _g_pool._chunk_size;
  size_t end_chunk = (rel_end - 1 - base) / _g_pool._chunk_size;
  uintptr_t current_addr = rel_addr;
  size_t remaining = size_bytes;

  for (size_t chunk_idx = start_chunk; chunk_idx <= end_chunk; chunk_idx++) {
    if (chunk_idx >= _g_pool._num_chunks) {
      LOG_ERROR("Chunk index %zu out of range", chunk_idx);
      return MEM_EFAULT;
    }

    UBMemoryChunk* chunk = _g_pool._chunks[chunk_idx];
    uintptr_t chunk_base = (uintptr_t)chunk->_base_addr;
    uintptr_t chunk_end = chunk_base + chunk->_size;

    uintptr_t part_start = current_addr;
    uintptr_t part_end = (rel_end < chunk_end) ? rel_end : chunk_end;
    size_t part_size = part_end - part_start;

    int ret = chunk_release(chunk, part_start, part_size);
    if (ret != MEM_SUCCESS) {
      LOG_ERROR("Chunk release failed: %d", ret);
      return ret;
    }

    current_addr = part_end;
    remaining -= part_size;
    if (remaining <= 0) {
      break;
    }
  }

  LOG_INFO("Memory released successfully");
  return MEM_SUCCESS;
}

UBMemoryPoolInfo UBHeapMemory::_memory_pool = UBMemoryPoolInfo();
const size_t UBHeapMemory::_size_class_config[SIZE_COUNT] = { 128 * K, 512 * K, 4 * M, 64 * M, 128 * M };

UBSuperBlock* UBHeapMemory::create_superblock(BlockSizeClass size_class, int* ret_code) {
  LOG_INFO("Creating new SuperBlock for size class: %d", size_class);
  size_t total_mem = (size_t)Atomic::load((volatile jlong*)&_memory_pool._total_memory) +
                     (size_t)Atomic::load((volatile jlong*)&_memory_pool._big_used_memory);
  if (total_mem + EXPANSION_UNIT > _memory_pool._max_size) {
    LOG_ERROR("Cannot allocate %zu bytes for SuperBlock (size class %d): would exceed max size limit of %zu",
              EXPANSION_UNIT, size_class, _memory_pool._max_size);
    set_ret_code(ret_code, MEM_ENOMEM);
    return NULL;
  }

  void* addr = NULL;
  int ret = 0;
  if (USE_LOCAL_MEM) {
    addr = os::Linux::ub_mem_borrow(EXPANSION_UNIT, &ret);
  } else {
    LOG_ERROR("Not implemented yet");
    set_ret_code(ret_code, MEM_EIMPLEMENTED);
    return NULL;
  }
  if (!addr || ret != 0) {
    LOG_ERROR("Failed to create SuperBlock (size class: %d), addr=%p, ret_code=%d",
              size_class, addr, ret);
    set_ret_code(ret_code, MEM_ENOMEM);
    return NULL;
  }

  uint32_t max_chunk_count = (uint32_t)(EXPANSION_UNIT / _size_class_config[size_class]);
  size_t bitmap_size = (max_chunk_count + 63) / 64 * sizeof(uint64_t);
  size_t data_size = EXPANSION_UNIT - sizeof(UBSuperBlock) - bitmap_size;

  // store chunk header
  UBSuperBlock* sb = (UBSuperBlock*)addr;
  sb->_base_addr = addr;
  sb->_size_class = size_class;
  sb->_chunk_size = _size_class_config[size_class] + sizeof(UBChunkHeader);
  sb->_chunk_count = (uint32_t)(data_size / sb->_chunk_size);
  sb->_chunk_base = (void*)((char*)addr + EXPANSION_UNIT - (sb->_chunk_count * sb->_chunk_size));
  sb->_free_chunks = sb->_chunk_count;

  sb->_lock = new Mutex(Mutex::leaf, "UB super block lock", true);

  sb->_bitmap = (uint64_t*)((char*)addr + sizeof(UBSuperBlock));
  memset(sb->_bitmap, 0, bitmap_size);
  sb->_next = _memory_pool._superblocks[size_class];
  _memory_pool._superblocks[size_class] = sb;

  Atomic::add((jint)sb->_chunk_count, (volatile jint*)&_memory_pool._stats[size_class]._total_chunks);
  Atomic::add((jint)sb->_chunk_count, (volatile jint*)&_memory_pool._stats[size_class]._free_chunks);
  Atomic::add((jlong)EXPANSION_UNIT, (volatile jlong*)&_memory_pool._total_memory);

  void* end_addr = (void*)((char*)addr + EXPANSION_UNIT);

  LOG_INFO("SuperBlock created: addr=%p, end_addr(not included)=%p, chunk_size=%zu, chunk_count=%u", addr, end_addr,
           sb->_chunk_size, sb->_chunk_count);
  set_ret_code(ret_code, MEM_SUCCESS);
  return sb;
}

BlockSizeClass UBHeapMemory::select_best_size_class(size_t size_bytes) {
  for (int i = 0; i < SIZE_COUNT; i++) {
    if (size_bytes <= _size_class_config[i]) {
      return (BlockSizeClass)i;
    }
  }
  return SIZE_128MB;
}

BlockSizeClass UBHeapMemory::find_most_needed_size_class() {
  float max_pressure = 0.0f;
  BlockSizeClass best_class = SIZE_4MB;

  for (int i = 0; i < SIZE_COUNT; i++) {
    size_t allocs = Atomic::load((volatile jlong*)&_memory_pool._stats[i]._alloc_count);
    size_t frees  = Atomic::load((volatile jlong*)&_memory_pool._stats[i]._release_count);
    size_t total  = Atomic::load((volatile jlong*)&_memory_pool._stats[i]._total_chunks);
    size_t fails  = Atomic::load((volatile jlong*)&_memory_pool._stats[i]._fail_count);

    float usage_rate = (total > 0) ? (float)(allocs - frees) / total : 0;
    float fail_rate  = (float)fails / (allocs + 1);
    float pressure   = (usage_rate * 0.7f) + (fail_rate * 0.3f);

    if (pressure > max_pressure) {
      max_pressure = pressure;
      best_class = (BlockSizeClass)i;
    }
  }
  return best_class;
}

void UBHeapMemory::check_and_expand_pool() {
  uint32_t alloc_count = (uint32_t)Atomic::add(1u, (volatile jint*)&_memory_pool._alloc_counter);
  if ((alloc_count - 1) % USAGE_CHECK_INTERVAL != 0) {
    return;
  }
  size_t total_mem = (size_t)Atomic::load((volatile jlong*)&_memory_pool._total_memory);
  size_t used_mem  = (size_t)Atomic::load((volatile jlong*)&_memory_pool._used_memory);
  if (total_mem == 0 || (float)used_mem / total_mem < _memory_pool._busy_ratio) {
    return;
  }

  BlockSizeClass new_class = find_most_needed_size_class();
  MutexLockerEx ml(_memory_pool._superblock_locks[new_class], Mutex::_no_safepoint_check_flag);

  // Double Check
  total_mem = (size_t)Atomic::load((volatile jlong*)&_memory_pool._total_memory);
  used_mem  = (size_t)Atomic::load((volatile jlong*)&_memory_pool._used_memory);
  if ((float)used_mem / total_mem >= _memory_pool._busy_ratio) {
    int ret_code = 0;
    UBSuperBlock* new_sb = create_superblock(new_class, &ret_code);
    if (new_sb != NULL && ret_code == MEM_SUCCESS) {
      LOG_INFO("Created new superblock for size class %d", new_class);
    } else {
      LOG_WARN("Created new superblock for size class %d failed, ret_code: %d",
               new_class, ret_code);
    }
  }
}

void* UBHeapMemory::setup_chunk_and_get_user_addr(void* chunk_addr, UBSuperBlock* sb, size_t requested_size,
                                                  BlockSizeClass size_class) {
  UBChunkHeader* header = (UBChunkHeader*)chunk_addr;
  header->_superblock = sb;

  size_t chunk_offset = (char*)chunk_addr - (char*)sb->_chunk_base;
  header->_chunk_index = (uint32_t)(chunk_offset / sb->_chunk_size);
  header->_requested_size = requested_size;

  return (char*)chunk_addr + sizeof(UBChunkHeader);
}

void* UBHeapMemory::try_alloc_in_superblock(UBSuperBlock* sb, size_t requested_size, BlockSizeClass size_class) {
  void* result_ptr = NULL;
  MutexLockerEx ml(sb->_lock, Mutex::_no_safepoint_check_flag);

  if (sb->_free_chunks == 0) {
    LOG_WARN("Superblock has no free chunks");
    return NULL;
  }

  const uint32_t chunk_count = sb->_chunk_count;
  const uint32_t words = (chunk_count + 63) / 64;

  for (uint32_t word_idx = 0; word_idx < words; word_idx++) {
    uint64_t word = sb->_bitmap[word_idx];
    if (word == UINT64_MAX)
      continue;

    int bit_idx = exact_log2(~word & -(~word));
    uint32_t chunk_idx = word_idx * 64 + bit_idx;

    if (chunk_idx >= chunk_count)
      break;

    sb->_bitmap[word_idx] |= (1ULL << bit_idx);
    sb->_free_chunks--;

    void* chunk_addr = (char*)sb->_chunk_base + (chunk_idx * sb->_chunk_size);
    result_ptr = setup_chunk_and_get_user_addr(chunk_addr, sb, requested_size, size_class);
    Atomic::dec(&_memory_pool._stats[size_class]._free_chunks);
    Atomic::inc(&_memory_pool._stats[size_class]._alloc_count);
    Atomic::add((jlong)sb->_chunk_size, (volatile jlong*)&_memory_pool._used_memory);
    Atomic::add((jlong)requested_size, (volatile jlong*)&_memory_pool._total_requested);
    return result_ptr;
  }
  LOG_WARN("Superblock has no available chunks");
  return NULL;
}

int UBHeapMemory::try_alloc(BlockSizeClass size_class, size_t requested_size, void** result_ptr) {
  UBSuperBlock* sb = _memory_pool._superblocks[size_class];
  while (sb != NULL) {
    *result_ptr = try_alloc_in_superblock(sb, requested_size, size_class);
    if (*result_ptr != NULL) {
      return MEM_SUCCESS;
    }
    sb = sb->_next;
  }
  LOG_WARN("No available chunks in SuperBlocks for size class: %d", size_class);
  MutexLockerEx ml(_memory_pool._superblock_locks[size_class], Mutex::_no_safepoint_check_flag);
  sb = _memory_pool._superblocks[size_class];
  while (sb != NULL) {
    *result_ptr = try_alloc_in_superblock(sb, requested_size, size_class);
    if (*result_ptr != NULL) {
      return MEM_SUCCESS;
    }
    sb = sb->_next;
  }
  int ret_code = 0;
  UBSuperBlock* new_sb = create_superblock(size_class, &ret_code);
  if (new_sb != NULL) {
    *result_ptr = try_alloc_in_superblock(new_sb, requested_size, size_class);
    if (*result_ptr != NULL) {
      return MEM_SUCCESS;
    }
  }
  LOG_ERROR("Allocation failed for size class: %d, ret_code=%d", size_class, ret_code);
  return ret_code;
}

int UBHeapMemory::acquire_big_memory(size_t size_bytes, void** result_ptr) {
  size_t aligned_size = ((size_bytes + _ub_heap_alignment - 1) / _ub_heap_alignment) * _ub_heap_alignment;

  size_t total_mem = (size_t)Atomic::load((volatile jlong*)&_memory_pool._total_memory) +
                     (size_t)Atomic::load((volatile jlong*)&_memory_pool._big_used_memory);
  if (total_mem + aligned_size > _memory_pool._max_size) {
    LOG_ERROR("Cannot allocate %zu bytes (request %zu bytes): would exceed max size limit of %zu", aligned_size,
              size_bytes, _memory_pool._max_size);
    return MEM_ENOMEM;
  }

  void* mapped = NULL;
  int ret_code;
  if (USE_LOCAL_MEM) {
    mapped = os::Linux::ub_mem_borrow(aligned_size, &ret_code);
  } else {
    LOG_ERROR("Not implemented yet");
    return MEM_EIMPLEMENTED;
  }
  if (mapped == NULL || ret_code != 0) {
    LOG_ERROR("Failed to create big memory block for size %zu, ret_code=%d", size_bytes, ret_code);
    return MEM_ENOMEM;
  }

  {
    MutexLockerEx ml(_memory_pool._big_map_mutex, Mutex::_no_safepoint_check_flag);
    _memory_pool._big_req_map->put((void*)mapped, size_bytes);
  }
  *result_ptr = (void*)mapped;
  Atomic::add((jlong)aligned_size, (volatile jlong*)&_memory_pool._big_used_memory);
  Atomic::add((jlong)size_bytes, (volatile jlong*)&_memory_pool._big_requested_memory);
  Atomic::inc(&_memory_pool._big_block_count);
  void* end_addr = (char*)mapped + aligned_size;
  void* request_end_addr = (char*)mapped + size_bytes;
  LOG_INFO("Big memory block allocated: addr=%p, end_addr(not included)=%p, req_end_addr(not included)=%p, size=%zu, "
           "aligned_size=%zu",
           mapped, end_addr, request_end_addr, size_bytes, aligned_size);

  return MEM_SUCCESS;
}

int UBHeapMemory::release_big_memory(void* requested_addr, bool* is_big) {
  if (is_big != NULL) {
    *is_big = false;
  }

  size_t req_size = 0;
  {
    MutexLockerEx ml(_memory_pool._big_map_mutex, Mutex::_no_safepoint_check_flag);

    size_t* size_ptr = _memory_pool._big_req_map->get(requested_addr);
    if (size_ptr == NULL) {
      return 0;
    }
    if (is_big != NULL) {
      *is_big = true;
    }
    req_size = *size_ptr;
    _memory_pool._big_req_map->remove(requested_addr);
  }

  size_t aligned_size = ((req_size + _ub_heap_alignment - 1) / _ub_heap_alignment) * _ub_heap_alignment;
  int ret = 0;
  if (USE_LOCAL_MEM) {
    ret = os::Linux::ub_mem_return(requested_addr, aligned_size);
  } else {
    LOG_ERROR("Not implemented yet");
    return MEM_EFAULT;
  }
  if (ret != 0) {
    return MEM_EFAULT;
  }

  Atomic::add(-(jlong)aligned_size, (volatile jlong*)&_memory_pool._big_used_memory);
  Atomic::add(-(jlong)req_size, (volatile jlong*)&_memory_pool._big_requested_memory);
  Atomic::dec(&_memory_pool._big_block_count);
  LOG_INFO("Big memory block released: addr=%p, size=%zu", requested_addr, req_size);
  return MEM_SUCCESS;
}

int UBHeapMemory::dynamic_mem_init(float busy_ratio, size_t size_bytes) {
  LOG_INFO("Initializing memory pool with busy ratio: %.2f, max size : %zu", busy_ratio, size_bytes);

  _memory_pool._busy_ratio = busy_ratio;
  _memory_pool._max_size = size_bytes;
  Atomic::store(0L, (volatile jlong*)&_memory_pool._total_memory);
  Atomic::store(0L, (volatile jlong*)&_memory_pool._used_memory);
  Atomic::store(0L, (volatile jlong*)&_memory_pool._total_requested);
  Atomic::store(0,  (volatile jint*)&_memory_pool._alloc_counter);

  for (int i = 0; i < SIZE_COUNT; i++) {
    _memory_pool._superblocks[i] = NULL;
    _memory_pool._superblock_locks[i] = new Mutex(Mutex::leaf + 1, "UBMemoryPool_SBLock", true);
  }

  _memory_pool._big_map_mutex = new Mutex(Mutex::leaf + 1, "UBMemoryPool_BigMapLock", true);

  _memory_pool._big_req_map = new (ResourceObj::C_HEAP, mtInternal) UBBigMap();

  for (int i = 0; i < SIZE_COUNT; i++) {
    UBSizeClassStats* stats = &_memory_pool._stats[i];
    Atomic::store(0, (volatile jint*)&stats->_alloc_count);
    Atomic::store(0, (volatile jint*)&stats->_release_count);
    Atomic::store(0, (volatile jint*)&stats->_total_chunks);
    Atomic::store(0, (volatile jint*)&stats->_free_chunks);
    Atomic::store(0, (volatile jint*)&stats->_fail_count);
  }

  _memory_pool._initialized = true;

  LOG_INFO("Memory pool initialized successfully");

  return MEM_SUCCESS;
}

int UBHeapMemory::dynamic_mem_acquire(size_t size_bytes, void** result_ptr) {
  if (result_ptr == NULL || size_bytes == 0) {
    return MEM_EINVAL;
  }
  int ret = MEM_SUCCESS;
  if (size_bytes > _size_class_config[SIZE_COUNT - 1]) {
    ret = acquire_big_memory(size_bytes, result_ptr);
    if (ret != MEM_SUCCESS) {
      LOG_ERROR("Dynamic memory acquire big failed, size in bytes: %lu", size_bytes);
      return ret;
    }
  } else {
    BlockSizeClass size_class = select_best_size_class(size_bytes);
    check_and_expand_pool();

    ret = try_alloc(size_class, size_bytes, result_ptr);
    if (ret != 0) {
      UBSizeClassStats* stats = &_memory_pool._stats[size_class];
      Atomic::inc((volatile jint*)&stats->_fail_count);
      LOG_ERROR("Memory acquire failed at try_alloc: size_class=%d", size_class);
      return ret;
    }
  }

  LOG_INFO("Dynamic memory acquire success, size in bytes: %lu", size_bytes);
  return MEM_SUCCESS;
}

int UBHeapMemory::dynamic_mem_release(void* requested_addr) {
  if (requested_addr == NULL) {
    LOG_WARN("Attempt to release nullptr pointer");
    return MEM_EINVAL;
  }

  bool is_big_block = false;
  int ret_code = release_big_memory(requested_addr, &is_big_block);
  if (is_big_block) {
    return ret_code;
  }

  UBChunkHeader* header = (UBChunkHeader*)((char*)requested_addr - sizeof(UBChunkHeader));
  UBSuperBlock* sb = header->_superblock;
  int size_class = sb->_size_class;

  uint32_t word_idx = header->_chunk_index / 64;
  uint32_t bit_idx  = header->_chunk_index % 64;
  uint64_t bit_mask = (1ULL << bit_idx);
  size_t requested_size = header->_requested_size;

  {
    MutexLockerEx ml(sb->_lock, Mutex::_no_safepoint_check_flag);

    uint64_t word = sb->_bitmap[word_idx];
    if ((word & bit_mask) == 0) {
      LOG_WARN("Double free detected: request_addr=%p, size_class=%d, chunk_index=%u, word_idx=%u, bit_idx=%u, "
               "word=%" PRIx64 ",mask=%" PRIx64 "",
               requested_addr, size_class, header->_chunk_index, word_idx, bit_idx, word, ~bit_mask);
      return MEM_SUCCESS;
    }
    sb->_bitmap[word_idx] = word & (~bit_mask);
    sb->_free_chunks++;
  }

  UBSizeClassStats* stats = &_memory_pool._stats[size_class];

  Atomic::inc((volatile jint*)&stats->_release_count);
  Atomic::inc((volatile jint*)&stats->_free_chunks);
  Atomic::add(-(jlong)sb->_chunk_size, (volatile jlong*)&_memory_pool._used_memory);
  Atomic::add(-(jlong)requested_size,  (volatile jlong*)&_memory_pool._total_requested);

  return MEM_SUCCESS;
}

void UBHeapMemory::print_memory_pool_stats(bool detailed) {
  outputStream* st = tty;

  jlong total_slab = Atomic::load((volatile jlong*)&_memory_pool._total_memory);
  jlong used_slab  = Atomic::load((volatile jlong*)&_memory_pool._used_memory);
  jlong req_slab   = Atomic::load((volatile jlong*)&_memory_pool._total_requested);

  jlong used_big   = Atomic::load((volatile jlong*)&_memory_pool._big_used_memory);
  jlong req_big    = Atomic::load((volatile jlong*)&_memory_pool._big_requested_memory);
  jlong big_count   = Atomic::load((volatile jlong*)&_memory_pool._big_block_count);

  size_t total_mem = (size_t)(total_slab + used_big);
  size_t used_mem  = (size_t)(used_slab + used_big);
  size_t total_req = (size_t)(req_slab + req_big);

  float utilization = total_mem > 0 ? (float)used_mem / total_mem : 0.0f;
  float fragmentation = used_mem > 0 ? (float)(used_mem - total_req) / used_mem : 0.0f;

  size_t total_superblocks = 0;
  for (int i = 0; i < SIZE_COUNT; i++) {
    MutexLockerEx ml(_memory_pool._superblock_locks[i], Mutex::_no_safepoint_check_flag);
    UBSuperBlock* sb = _memory_pool._superblocks[i];
    while (sb != NULL) {
      total_superblocks++;
      sb = sb->_next;
    }
  }

  st->print_cr("\n===== Memory Pool Statistics =====");
  st->print_cr("SuperBlocks: " SIZE_FORMAT ", Big Blocks: " SIZE_FORMAT, total_superblocks, big_count);

  st->print_cr("Total Memory: " SIZE_FORMAT " bytes (" SIZE_FORMAT " KB, " SIZE_FORMAT " MB)",
          total_mem, total_mem / K, total_mem / M);
  st->print_cr("Used Memory:  " SIZE_FORMAT " bytes (" SIZE_FORMAT " KB, " SIZE_FORMAT " MB)",
          used_mem, used_mem / K, used_mem / M);
  st->print_cr("Req. Memory:  " SIZE_FORMAT " bytes (" SIZE_FORMAT " KB, " SIZE_FORMAT " MB)",
          total_req, total_req / K, total_req / M);
  st->print_cr("Utilization: %.2f%%", utilization * 100.0f);
  st->print_cr("Fragmentation Rate: %.2f%%", fragmentation * 100.0f);

  if (!detailed) {
    st->print_cr("=================================");
    return;
  }

  st->print_cr("\nSize Class Details:");
  st->print_cr("Class | ChunkSize | Allocs    | Releases  | Fails     | TotalChunks | FreeChunks | Util");
  st->print_cr("------|-----------|-----------|-----------|-----------|-------------|------------|------");

  for (int i = 0; i < SIZE_COUNT; i++) {
    UBSizeClassStats* stats = &_memory_pool._stats[i];

    size_t allocs   = (int)Atomic::load((volatile jlong*)&stats->_alloc_count);
    size_t releases = (int)Atomic::load((volatile jlong*)&stats->_release_count);
    size_t fails    = (int)Atomic::load((volatile jlong*)&stats->_fail_count);
    size_t total    = (int)Atomic::load((volatile jlong*)&stats->_total_chunks);
    size_t free     = (int)Atomic::load((volatile jlong*)&stats->_free_chunks);

    size_t chunk_size = _size_class_config[i];
    float class_util  = total > 0 ? (float)(total - free) / total : 0.0f;

    st->print_cr("%-5d | " SIZE_FORMAT_W(9) " | " SIZE_FORMAT_W(9) " | " SIZE_FORMAT_W(9) " | " SIZE_FORMAT_W(9)
                 " | " SIZE_FORMAT_W(9) " | " SIZE_FORMAT_W(9) " | %.4f%%",
                 i, chunk_size, allocs, releases, fails, total, free, class_util * 100.0f);
  }
  st->print_cr("=================================");
}

bool UBHeapMemory::destroy_superblock_list(UBSuperBlock* sb) {
  if (sb == NULL) {
    return true;
  }
  int all_success = true;
  while (sb != NULL) {
    UBSuperBlock* next;
    Mutex* sb_lock = sb->_lock;
    if (sb_lock == NULL) {
      sb = next;
      all_success = false;
      continue;
    }
    {
      MutexLockerEx sb_ml(sb_lock, Mutex::_no_safepoint_check_flag);
      int ret_code = 0;
      next = sb->_next;
      if (USE_LOCAL_MEM) {
        ret_code = os::Linux::ub_mem_return(sb->_base_addr, sb->_chunk_size);
      } else {
        LOG_ERROR("Not implemented yet");
        all_success = false;
      }
      all_success = (ret_code == 0) ? all_success : false;
    }
    delete sb_lock;
    sb = next;
  }
  return all_success;
}

int UBHeapMemory::dynamic_mem_cleanup(void) {
  LOG_INFO("Cleaning up memory pool");

  print_memory_pool_stats(true);

  bool all_success = true;
  if (_memory_pool._big_req_map != NULL) {
    {
      MutexLockerEx ml(_memory_pool._big_map_mutex, Mutex::_no_safepoint_check_flag);
      BigMemCleanupClosure closure;
      _memory_pool._big_req_map->iterate(&closure);
    }

    delete _memory_pool._big_req_map;
    _memory_pool._big_req_map = NULL;

    delete _memory_pool._big_map_mutex;
    _memory_pool._big_map_mutex = NULL;
  }

  for (int i = 0; i < SIZE_COUNT; i++) {
    if (_memory_pool._superblock_locks[i] == NULL) {
      continue;
    }
    MutexLockerEx ml(_memory_pool._superblock_locks[i], Mutex::_no_safepoint_check_flag);
    UBSuperBlock* sb = _memory_pool._superblocks[i];
    int ret = destroy_superblock_list(sb);
    all_success = (ret == true) ? all_success : false;
    _memory_pool._superblocks[i] = NULL;
  }

  if (!all_success) {
    LOG_ERROR("Memory pool cleanup failed");
    return MEM_EFAULT;
  }

  _memory_pool._initialized = false;
  LOG_INFO("Memory pool cleanup completed");
  return MEM_SUCCESS;
}
