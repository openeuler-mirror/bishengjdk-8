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

#ifndef SHARE_VM_MATRIX_UBSOCKET_HPP
#define SHARE_VM_MATRIX_UBSOCKET_HPP

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixAllowList.hpp"
#include "matrix/matrixUtils.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "runtime/os.hpp"

static const char* UB_SOCKET_MEM_PREFIX = "Sock";
static const size_t UB_SOCKET_MEM_PREFIX_LEN = 4;
static const size_t UB_SOCKET_MEM_HOST_LEN = 8;
static const size_t UB_SOCKET_MEM_PID_LEN = 8;
static const size_t UB_SOCKET_MEM_NAME_LEN =
    UB_SOCKET_MEM_PREFIX_LEN + UB_SOCKET_MEM_HOST_LEN + UB_SOCKET_MEM_PID_LEN;
static const int UB_SOCKET_MEM_NAME_BUF_LEN = UB_SOCKET_MEM_NAME_LEN + 1;
static const long UB_SOCKET_WAIT_MIN_TIMEOUT_MS = 1;

struct UBSocketBlkMeta {
  uint64_t nanos;
  uint32_t blk_count;
};

class UBSocketManager : public AllStatic {
 public:
  static Symbol* shared_memory_name;
  static void* shared_memory_addr;
  static uint64_t package_timeout;

  static bool check_stack();
  static void init();
  static void before_exit();
  static void clean_ub_resources();

  static void* get_free_memory(long len, long* offset, long* size);
  static long buffer_data(int socket_fd, char* name, long off, long len);
  static long read_data(void* buf, int socket_fd, long len);
  static long write_data(void* buf, int socket_fd, long len);
  static int send_msg(int socket_fd, void* socket_addr, long ub_offset, long len);
  static int send_heartbeat(int socket_fd);
  static long parse_msg(int socket_fd, const char* ub_msg, size_t ub_msg_len);

  static bool register_fd(int socket_fd, bool is_server);
  static bool unregister_fd(int socket_fd);
  static bool has_registered(int socket_fd);
  static bool wait_fd_ready(int socket_fd);

  static void mark(void* addr, uint64_t blk_count) {
    UBSocketBlkMeta meta = {os::javaTimeNanos(), blk_count};
    memcpy(addr, &meta, _blk_meta_size);
  };
  static void clean_mark(uintptr_t data_addr) {
    void* addr = reinterpret_cast<char*>(data_addr) - _blk_meta_size;
    memset(addr, 0, _blk_meta_size);
  };
  static bool is_marked(const void* addr) {
    return reinterpret_cast<const UBSocketBlkMeta*>(addr)->nanos != 0;
  };

 private:
  static AllowListTable* _allow_list_table;

  static bool _initialized;
  static uint64_t _mem_blk_idx;
  static size_t _blk_size;
  static size_t _blk_meta_size;
  static size_t _blk_count;
};

#endif  // SHARE_VM_MATRIX_UBSOCKET_HPP
