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

#include "memory_file.h"
#include <sys/mman.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>
#include <atomic>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif

#define M (1024L*1024)
#define ALLOC_SIZE (2L*1024*1024*1024)
#define MAX_KIND_LIMIT 5
#define MAX_SCAN 300

struct BlockHeader {
    std::atomic<void*> next;
    size_t used;
    int kind;
};

void* OCCUPIED_ADDR = reinterpret_cast<void*>(std::uintptr_t(-1));
bool is_initialized = false;

std::vector<std::vector<void*>> block_list(MAX_KIND_LIMIT);
std::vector<size_t> block_list_num{4096L, 4096L, 2560L, 2560L, 1280L};
std::vector<std::atomic<size_t>> free_idx_list(MAX_KIND_LIMIT);

void* start_by_data_addr(void* data_addr) {
    void* start_addr = (char*)data_addr - sizeof(BlockHeader);
    return start_addr;
}

size_t get_blk_size(int kind) {
    return M * (1ULL << (kind + 2));
}

void prepare_memory(int kind, size_t blk_num) {
    int alloc_count = 0;
    int block_count = 0;
    std::vector<void*> blk_list(blk_num);
    size_t blk_size = get_blk_size(kind);
    size_t total_size = blk_size * blk_num;
    while (ALLOC_SIZE * alloc_count++ < total_size) {
        void* addr = mmap(NULL, ALLOC_SIZE, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        for(int count = 0; blk_size * count < ALLOC_SIZE; count++) {
            void* block_addr = (char*)addr + blk_size * count;
            BlockHeader* blk_header = (BlockHeader*)block_addr;
            blk_header->next.store(nullptr, std::memory_order_acquire);
            blk_header->used = 0;
            blk_header->kind = (int)kind;
            blk_list[block_count++] = block_addr;
        }
    }
    block_list[kind] = std::move(blk_list);
    free_idx_list[kind].store(0, std::memory_order_relaxed);
    is_initialized = true;
}

int prepare_environments() {
    std::cout << "Use MemoryPool..." << std::endl;

    for (int kind = 0; kind < MAX_KIND_LIMIT; kind++) {
        prepare_memory(kind, block_list_num[kind]);
    }

    return 0;
}

int malloc_remote_memory(const char* name, size_t size) {
    return 0;
}

void* alloc_new_block(int kind) {
    size_t blk_num = block_list_num[kind];
    if (blk_num == 0) return nullptr;
    size_t start_idx = free_idx_list[kind].load(std::memory_order_acquire);
    size_t scanned = 0;
    size_t idx = start_idx;

    while (scanned < MAX_SCAN) {
        void* block_addr = block_list[kind][idx];
        BlockHeader* header = (BlockHeader*)block_addr;
        void* current = header->next.load(std::memory_order_acquire);
        if (current == nullptr) {
            if (header->next.compare_exchange_strong(current, OCCUPIED_ADDR,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
                size_t next_idx = (idx + 1) % blk_num;
                size_t expected = start_idx;
                // no need to force set next idx
                free_idx_list[kind].compare_exchange_weak(expected, next_idx,
                    std::memory_order_release, std::memory_order_acquire);
                return block_addr;
            }
        }
        idx = (idx + 1) % blk_num;
        scanned++;
    }

    // try to scan from list head
    idx = 0;
    while (scanned < MAX_SCAN) {
        void* block_addr = block_list[kind][idx];
        BlockHeader* header = (BlockHeader*)block_addr;
        void* current = header->next.load(std::memory_order_acquire);
        if (current == nullptr) {
            if (header->next.compare_exchange_strong(current, OCCUPIED_ADDR,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
                // Need update idx?
                return block_addr;
            }
        }
        idx = (idx + 1) % blk_num;
        scanned++;
    }
    std::cerr << "Memory pool exhausted for kind " << kind << std::endl;
    return nullptr;
}

void* mmap_remote_memory(const char* name, size_t size) {
    void* block_addr = alloc_new_block(0);
    void* data_addr = (char*)block_addr + sizeof(BlockHeader);
    return data_addr;
}

int flush_shared_memory(void* start, size_t size) {
    if (size == 0) {
        get_used_size(start, &size);
    }
    void* addr = start_by_data_addr(start);
    size_t recorded_size = 0;
    while (addr != nullptr) {
        BlockHeader* blk_header = (BlockHeader*)addr;
        void* next = blk_header->next.load(std::memory_order_acquire);
        if (next == OCCUPIED_ADDR) {
            blk_header->used = size - recorded_size;
            break;
        } else {
            recorded_size += get_blk_size(blk_header->kind) - sizeof(BlockHeader);
        }
        addr = next;
    }
    return 0;
}

int munmap_shared_memory(void* start, size_t size) {
    void* addr = start_by_data_addr(start);
    while (addr != nullptr) {
        BlockHeader* header = (BlockHeader*)addr;
        void* next = header->next.load(std::memory_order_relaxed);
        header->next.store(nullptr, std::memory_order_release);
        header->used = 0;
        if (next == OCCUPIED_ADDR) break;
        addr = next;
    }
    return 0;
}

int free_remote_memory(const char* name) {
    return 0;
}

int get_used_size(void* data_addr, size_t* size) {
    void* addr = start_by_data_addr(data_addr);
    size_t total_size = 0;
    while (addr != nullptr) {
        BlockHeader* blk_header = (BlockHeader*)addr;
        total_size += blk_header->used;
        void* next = blk_header->next.load(std::memory_order_acq_rel);
        if (next == OCCUPIED_ADDR) break;
        addr = next;
    }
    *size = total_size;
    return 0;
}

void* seek_shared_memory(void* start, size_t set_off, size_t* size, size_t* offset) {
    void* head_addr = start_by_data_addr(start);
    BlockHeader* old_header = (BlockHeader*)head_addr;
    size_t cap = get_blk_size(old_header->kind) - sizeof(BlockHeader);
    if (set_off < cap) {
        *size = cap;
        *offset = set_off;
        return start;
    }
    set_off -= cap;
    // has next blk
    void* old_next = old_header->next.load(std::memory_order_acquire);
    if (old_next != nullptr && old_next != OCCUPIED_ADDR) {
        void* data_addr = (char*)(old_next) + sizeof(BlockHeader);
        return seek_shared_memory(data_addr, set_off, size, offset);
    }
    // old blk is full if need alloc new blk
    old_header->used = cap;
    // no next blk, need alloc
    int next_kind = (old_header->kind + 1) % MAX_KIND_LIMIT;
    void* new_blk_addr = alloc_new_block(next_kind);
    if (new_blk_addr == nullptr) { // no enough memory
        return nullptr;
    }
    old_header->next.store(new_blk_addr, std::memory_order_release);
    BlockHeader* new_header = (BlockHeader*)new_blk_addr;
    void* data_addr = (char*)new_blk_addr + sizeof(BlockHeader);
    return seek_shared_memory(data_addr, set_off, size, offset);
}

int rename_remote_memory(const char* from, const char* to) {
    return 0;
}

int remote_name_exist(const char* name, bool* exist) {
    *exist = false;
    return 0;
}

int shared_addr_exist(void* addr, bool* exist) {
    *exist = false;
    return 0;
}

int total_memory_info(size_t* used, size_t* alloc, size_t* total) {
    size_t sum_used = 0;
    size_t sum_alloc = 0;
    size_t sum_total = 0;
    for (int kind = 0; kind < MAX_KIND_LIMIT; kind++) {
        size_t blk_size = get_blk_size(kind);
        int used_blk_count = 0;
        int full_blk_count = 0;
        for (int idx = 0; idx < block_list[kind].size(); idx++) {
            void* addr = block_list[kind][idx];
            BlockHeader* blk_header = (BlockHeader*)addr;
            void* next = blk_header->next.load();
            if (next != nullptr) {
                sum_used += blk_header->used;
                sum_alloc += blk_size;
                used_blk_count++;
                if (next != OCCUPIED_ADDR)
                    full_blk_count++;
            }
            sum_total += blk_size;
        }
        if (is_initialized)
            printf("Mem info: kind %d : %d / %d / %d\n",
                    kind, full_blk_count, used_blk_count, block_list[kind].size());
    }
    *used = sum_used;
    *alloc = sum_alloc;
    *total = sum_total;
    return 0;
}


#ifdef __cplusplus
};
#endif