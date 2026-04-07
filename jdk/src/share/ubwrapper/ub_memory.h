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

#ifndef UB_MEMORY_WRAPPER_H
#define UB_MEMORY_WRAPPER_H

#include <stddef.h>
#include <stdbool.h>
#include <sys/mman.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int   prepare_environments(int log_level, const char* log_path);

extern int   finalize_environments(void);

// Memory share
extern int   malloc_remote_memory(const char* name, size_t size);

extern void* mmap_remote_memory(const char* name, size_t length, int* ret_code, void *addr, int prot);

extern int   flush_shared_memory(void* start, size_t size);

extern int   munmap_shared_memory(void* start, size_t size);

extern int   free_remote_memory(const char* name);

extern int   rename_remote_memory(const char* from, const char* to);

extern int   remote_name_exist(const char* name, bool* exist);

extern int   remote_addr_exist(void* addr, bool* exist);

// Memory borrow
extern void* borrow_memory(size_t size, int* ret_code, void* start);

extern int   return_memory(void* addr, size_t size);

// Dynamic Max Heap Size
extern bool  dynamic_max_heap_g1_can_shrink(double used_after_gc_d, size_t _new_max_heap,
                                            double maximum_used_percentage, size_t max_heap_size);

extern uint  dynamic_max_heap_g1_get_region_limit(size_t _new_max_heap, size_t region_size);

#ifdef __cplusplus
};
#endif

#endif // UB_MEMORY_WRAPPER_H