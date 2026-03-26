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
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>

#include "ub_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

int prepare_environments(int log_level, const char* log_path) {
  return 0;
}

int finalize_environments(void) {
  return 0;
}

int malloc_remote_memory(const char* name, size_t size) {
  if (name == NULL) {
    std::cout << "ub malloc NULL name, skip shm_open" << std::endl;
    return 0;
  }

  int fd = shm_open(name, O_CREAT | O_RDWR | O_TRUNC, 0666);
  if (fd == -1) {
    std::cerr << "shm_open failed: " << strerror(errno) << std::endl;
    return -1;
  }

  if (ftruncate(fd, size) == -1) {
    std::cerr << "ftruncate failed: " << strerror(errno) << std::endl;
    close(fd);
    shm_unlink(name);
    return -1;
  }

  close(fd);
  return 0;
}

void* mmap_remote_memory(const char* name, size_t length, int* ret_code, void *start, int prot) {
  if (name == NULL) {
    std::cout << "ub mmap NULL name, skip shm_open" << std::endl;
    return 0;
  }
  int fd = shm_open(name, O_RDWR, 0666);
  if (fd == -1) {
    std::cerr << "shm_open failed: " << strerror(errno) << std::endl;
    *ret_code = errno;
    return nullptr;
  }

  void* addr = mmap(start, length, prot, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    std::cerr << "mmap failed: " << strerror(errno) << std::endl;
    close(fd);
    *ret_code = errno;
    return nullptr;
  }

  close(fd);
  return addr;
}

int munmap_shared_memory(void* start, size_t size) {
  if (munmap(start, size) == -1) {
      std::cerr << "munmap failed: " << strerror(errno) << std::endl;
      return -1;
  }
  return 0;
}

int free_remote_memory(const char* name) {
  if (name == NULL) {
    std::cout << "ub free NULL name, skip shm_open" << std::endl;
    return 0;
  }
  if (shm_unlink(name) == -1) {
    std::cerr << "shm_unlink failed: " << strerror(errno) << std::endl;
    return -1;
  }
  return 0;
}

int flush_shared_memory(void* start, size_t size) {
  return 0;
}

int rename_remote_memory(const char* from, const char* to) {
  return 0;
}

int remote_name_exist(const char* name, bool* exist) {
  *exist = true;
  return 0;
}

int remote_addr_exist(void* addr, bool* exist) {
  *exist = true;
  return 0;
}

void* borrow_memory(size_t size, int* ret_code, void* start) {
  int map_type = MAP_SHARED;
  int flag = start ? MAP_FIXED : 0;
  flag |= map_type;
  flag |= MAP_ANONYMOUS;
  void* addr = mmap(start, size, PROT_READ | PROT_WRITE,
                    flag, -1, 0);
  if (addr == MAP_FAILED) {
    std::cerr << "mmap failed: " << strerror(errno) << std::endl;
    *ret_code = errno;
    return nullptr;
  }
  return addr;
}

int return_memory(void* addr, size_t size) {
  int ret = munmap(addr, size);
  return ret;
}

#ifdef __cplusplus
}
#endif