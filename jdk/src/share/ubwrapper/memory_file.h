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

#ifndef UB_WRAPPER_MEMORY_FILE_H
#define UB_WRAPPER_MEMORY_FILE_H

#include <cstdlib>

#ifdef __cplusplus
extern "C" {
#endif

extern int prepare_environments();

extern int malloc_remote_memory(const char* name, size_t size);

extern void* mmap_remote_memory(const char* name, size_t size);

extern int flush_shared_memory(void* start, size_t size);

extern int munmap_shared_memory(void* start, size_t size);

extern int free_remote_memory(const char* name);

extern void* seek_shared_memory(void* start, size_t off, size_t* size, size_t* offset);

extern int rename_remote_memory(const char* from, const char* to);

extern int remote_name_exist(const char* name, bool* exist);

extern int shared_addr_exist(void* addr, bool* exist);

extern int get_used_size(void* start, size_t* size);

extern int total_memory_info(size_t* used, size_t* alloc, size_t* total);

#ifdef __cplusplus
};
#endif

#endif // UB_WRAPPER_MEMORY_FILE_H