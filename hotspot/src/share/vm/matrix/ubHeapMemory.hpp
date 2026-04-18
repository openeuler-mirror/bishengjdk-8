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

#ifndef UB_HEAP_MEMORY_HPP
#define UB_HEAP_MEMORY_HPP

#include "memory/allocation.hpp"
#include "runtime/os.hpp"
#include "runtime/mutex.hpp"
#include "runtime/atomic.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/resourceHash.hpp"

#define EXPANSION_UNIT ((size_t)4 * G)
#define USE_LOCAL_MEM true
#define USAGE_CHECK_INTERVAL 100

enum UBMemResult {
  MEM_SUCCESS = 0,
  MEM_EINVAL  = 1,
  MEM_ENOMEM  = 2,
  MEM_ERANGE  = 3,
  MEM_EALREADY = 4,
  MEM_ENOTINIT = 5,
  MEM_EFAULT = 6,
  MEM_EIMPLEMENTED = 7
};

typedef enum {
  SIZE_128KB = 0, SIZE_512KB, SIZE_4MB, SIZE_64MB, SIZE_128MB,
  SIZE_COUNT
} BlockSizeClass;

class UBHeapFixedMemPool : public AllStatic {
public:
  static uintptr_t _base;
  static uintptr_t _end;
  static size_t _size;
  static bool _initialized;
};

class UBMemoryChunk : public CHeapObj<mtInternal> {
public:
  void*    _base_addr;
  size_t   _size;
  uint8_t* _usage_map;
  size_t   _map_size;
  Mutex*   _lock;

  UBMemoryChunk() : _base_addr(NULL), _size(0), _usage_map(NULL), _map_size(0), _lock(NULL) {}
};

class UBFixedPoolState : public CHeapObj<mtInternal> {
public:
  void*           _base_addr;
  size_t          _total_size;
  volatile size_t _used_size;
  size_t          _allocated_size;
  size_t          _page_size;
  size_t          _chunk_size;
  size_t          _max_chunks;
  size_t          _num_chunks;
  UBMemoryChunk** _chunks;
  Mutex*          _global_lock;
  int             _initialized;
};

class UBSuperBlock : public CHeapObj<mtInternal> {
public:
  void*           _base_addr;
  void*           _chunk_base;
  BlockSizeClass  _size_class;
  size_t          _chunk_size;
  uint32_t        _chunk_count;
  uint64_t*       _bitmap;
  uint32_t        _free_chunks;
  UBSuperBlock*   _next;
  Mutex*          _lock;

  UBSuperBlock()
    : _base_addr(NULL), _chunk_base(NULL), _size_class(SIZE_COUNT),
      _chunk_size(0), _chunk_count(0), _bitmap(NULL),
      _free_chunks(0), _next(NULL), _lock(NULL) {}
};

class UBChunkHeader : public StackObj {
public:
  UBSuperBlock* _superblock;
  uint32_t      _chunk_index;
  size_t        _requested_size;
};

class UBSizeClassStats : public CHeapObj<mtInternal> {
public:
  volatile size_t _alloc_count;
  volatile size_t _release_count;
  volatile size_t _fail_count;
  volatile size_t _total_chunks;
  volatile size_t _free_chunks;
};

inline unsigned int ptr_hash(void* const& p) {
  uint64_t h = static_cast<uint64_t>(p2i(p));
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccd;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53;
  h ^= h >> 33;
  return static_cast<unsigned int>(h);
}

typedef ResourceHashtable<void*, size_t,
                          ptr_hash,
                          primitive_equals<void*>,
                          1024,
                          ResourceObj::C_HEAP,
                          mtInternal> UBBigMap;

class BigMemCleanupClosure : public StackObj {
public:
  bool do_entry(void*& addr, size_t& size) {
    os::Linux::ub_munmap(addr, size);
    return true;
  }
};

class UBMemoryPoolInfo : public CHeapObj<mtInternal> {
public:
  UBBigMap*         _big_req_map;
  Mutex*            _big_map_mutex;
  volatile size_t   _big_used_memory;
  volatile size_t   _big_requested_memory;
  volatile size_t   _big_block_count;

  UBSuperBlock*     _superblocks[SIZE_COUNT];
  Mutex*            _superblock_locks[SIZE_COUNT];
  UBSizeClassStats  _stats[SIZE_COUNT];

  volatile size_t   _total_memory;
  volatile size_t   _used_memory;
  volatile size_t   _total_requested;
  float             _busy_ratio;
  size_t            _max_size;
  volatile uint32_t _alloc_counter;
  bool              _initialized;
};

class UBHeapMemory : public AllStatic {
public:
  // Fixed address memory management
  static int            fixed_mem_init(void* base_addr, size_t total_size);
  static int            fixed_mem_cleanup(void);
  static int            fixed_mem_expand_to(size_t expect_size_bytes);
  static int            fixed_mem_acquire(void* req_addr, size_t size_bytes, void** result_ptr);
  static int            fixed_mem_release(void* addr, size_t size_bytes);
  // Dynamic address memory management
  static bool           check_runtime_flags();
  static int            dynamic_mem_init(float busy_ratio, size_t size_bytes);
  static int            dynamic_mem_cleanup(void);
  static int            dynamic_mem_acquire(size_t size_bytes, void** result_ptr);
  static int            dynamic_mem_release(void* requested_addr);

  static size_t         ub_heap_alignment() { return _ub_heap_alignment; }
  static size_t         ub_unsafe_filter_threshold() { return _ub_unsafe_filter_threshold; }

  static bool ub_dynamic_mem_enabled() {
    return (_memory_pool._initialized && MemTracker::tracking_level() != NMT_off);
  }

  static void set_ret_code(int* ret_code, UBMemResult result) {
    if (ret_code != NULL) {
      *ret_code = result;
    }
  }
private:
  static UBFixedPoolState _g_pool;
  static UBMemoryPoolInfo _memory_pool;
  static const size_t _size_class_config[SIZE_COUNT];
  static const size_t _ub_heap_alignment;
  static const size_t _ub_unsafe_filter_threshold;

  static UBMemoryChunk* create_chunk(void* base_addr, size_t size, int* ret_code);
  static int            destroy_chunk(UBMemoryChunk* chunk);
  static int            expand_to_size(size_t size_in_bytes);
  static int            chunk_acquire(UBMemoryChunk* chunk, uintptr_t addr, size_t size, void** result_ptr);
  static int            chunk_release(UBMemoryChunk* chunk, uintptr_t addr, size_t size);
  static UBSuperBlock*  create_superblock(BlockSizeClass size_class, int* ret_code);
  static BlockSizeClass select_best_size_class(size_t size_bytes);
  static BlockSizeClass find_most_needed_size_class(void);
  static void           check_and_expand_pool(void);
  static void*          setup_chunk_and_get_user_addr(void* chunk_addr, UBSuperBlock* sb,
                                                      size_t req_size, BlockSizeClass sc);
  static void*          try_alloc_in_superblock(UBSuperBlock* sb, size_t req_size, BlockSizeClass sc);
  static int            try_alloc(BlockSizeClass size_class, size_t requested_size, void** result_ptr);
  static int            acquire_big_memory(size_t size_bytes, void** result_ptr);
  static int            release_big_memory(void* requested_addr, bool* is_big);
  static void           print_memory_pool_stats(bool detailed);
  static bool           destroy_superblock_list(UBSuperBlock* sb);
};

#endif // UB_HEAP_MEMORY_HPP
