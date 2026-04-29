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
#include <sys/types.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixAllowList.hpp"
#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "runtime/os.hpp"

static const char UB_SOCKET_MEM_PREFIX[] = "SOCK_";
enum {
  UB_SOCKET_MEM_PREFIX_LEN   = sizeof(UB_SOCKET_MEM_PREFIX) - 1,
  UB_SOCKET_MEM_HOST_LEN     = 10,
  UB_SOCKET_MEM_PID_LEN      = 8,
  UB_SOCKET_MEM_TIME_LEN     = 8,
  UB_SOCKET_MEM_NAME_LEN     = UB_SOCKET_MEM_PREFIX_LEN +
                               UB_SOCKET_MEM_HOST_LEN +
                               UB_SOCKET_MEM_PID_LEN +
                               UB_SOCKET_MEM_TIME_LEN,
  UB_SOCKET_MEM_NAME_BUF_LEN = UB_SOCKET_MEM_NAME_LEN + 1
};

enum {
  // Minimum poll wait used when a computed IO deadline rounds down to zero.
  UB_SOCKET_IO_POLL_MIN_MS = 1,
  // Attach agent poll intervals for pending early requests and idle listening.
  UB_ATTACH_IO_BUSY_POLL_MS = 10,
  // Periodic unread-message reclaim and timeout maintenance interval.
  UB_RECLAIM_POLL_MS = 100,
  // Always-on timeout for descriptors not observed by the peer.
  UB_SOCKET_RECV_TIMEOUT_MS = 100,
  // User-facing read timeout default and lower bound for heartbeat.
  UB_SOCKET_READ_TIMEOUT_DEFAULT_MS = 200,
  // Fixed internal deadlines for data-frame send and attach handshake.
  UB_DATA_FRAME_SEND_TIMEOUT_MS = 300,
  // Server accept only waits briefly for ATTACH_REQ before falling back to TCP.
  UB_ATTACH_REQUEST_WAIT_MS = 50,
  UB_ATTACH_TIMEOUT_MS = 500,
};

enum UBSocketBlkState {
  UB_SOCKET_BLK_INIT = 0,
  UB_SOCKET_BLK_SEND = 1,
  UB_SOCKET_BLK_RECV = 2,
  UB_SOCKET_BLK_READ = 3
};

struct UBSocketBlkMeta {
  uint32_t state;
  uint32_t fd;
  uint64_t send_nanos;
  uint64_t recv_nanos;
  uint64_t read_nanos;
};

class UBSocketManager : public AllStatic {
 public:
  static Symbol* shared_memory_name;
  static void* shared_memory_addr;
  static uint64_t package_timeout;

  static bool check_stack();
  static void check_options();

  static void init();
  static void before_exit();
  static void clean_ub_resources();

  static void* get_free_memory(uint64_t len, uint64_t* offset, uint64_t* size,
                               uint32_t* start_blk = NULL, uint32_t* blk_count = NULL);
  static long read_data(void* buf, int socket_fd, long len);
  static long write_data(void* buf, int socket_fd, long len);
  static ssize_t send_heartbeat(int socket_fd);
  static long parse_msg(int socket_fd, const char* ub_msg, size_t ub_msg_len);
  static ssize_t ensure_fallback_sent(int socket_fd, const char* reason);
  static bool unregister_if_fallback_drained(int socket_fd);

  static bool register_fd(int socket_fd, bool is_server);
  static bool unregister_fd(int socket_fd);
  static bool has_registered(int socket_fd);
  static bool wait_fd_ready(int socket_fd);

  static void mark_send(void* addr, int socket_fd) {
    UBSocketBlkMeta meta = {
        UB_SOCKET_BLK_SEND, (uint32_t)socket_fd,
        (uint64_t)os::javaTimeNanos(), 0, 0};
    memcpy(addr, &meta, sizeof(UBSocketBlkMeta));
  };
  static bool mark_recv(uintptr_t data_addr) {
    void* addr = reinterpret_cast<char*>(data_addr) - sizeof(UBSocketBlkMeta);
    UBSocketBlkMeta meta;
    memcpy(&meta, addr, sizeof(UBSocketBlkMeta));
    if (meta.state != UB_SOCKET_BLK_SEND) {
      UB_LOG(UB_SOCKET, UB_LOG_ERROR,
             "mark_recv skipped state=" UINT32_FORMAT " socket_fd=" UINT32_FORMAT "\n",
             meta.state, meta.fd);
      return false;
    }
    meta.state = UB_SOCKET_BLK_RECV;
    meta.recv_nanos = (uint64_t)os::javaTimeNanos();
    memcpy(addr, &meta, sizeof(UBSocketBlkMeta));
    return true;
  };
  static bool mark_read(uintptr_t data_addr) {
    void* addr = reinterpret_cast<char*>(data_addr) - sizeof(UBSocketBlkMeta);
    UBSocketBlkMeta meta;
    memcpy(&meta, addr, sizeof(UBSocketBlkMeta));
    if (meta.state != UB_SOCKET_BLK_RECV) {
      UB_LOG(UB_SOCKET, UB_LOG_ERROR,
             "mark_read skipped state=" UINT32_FORMAT " socket_fd=" UINT32_FORMAT "\n",
             meta.state, meta.fd);
      return false;
    }
    meta.state = UB_SOCKET_BLK_READ;
    meta.read_nanos = (uint64_t)os::javaTimeNanos();
    memcpy(addr, &meta, sizeof(UBSocketBlkMeta));
    return true;
  };
  static void clear_mark(uintptr_t data_addr) {
    void* addr = reinterpret_cast<char*>(data_addr) - sizeof(UBSocketBlkMeta);
    memset(addr, 0, sizeof(UBSocketBlkMeta));
  };
  static bool is_marked(const void* addr) {
    return reinterpret_cast<const UBSocketBlkMeta*>(addr)->state != UB_SOCKET_BLK_INIT;
  };
  static void free_blocks(uint32_t start_blk, uint32_t blk_count) {
    UBSocketBlkBitmap::release(start_blk, blk_count);
  }

 private:
  static AllowListTable* _allow_list_table;

  static bool _initialized;
  static uint32_t _blk_size;
  static uint32_t _blk_count;
  static size_t memory_size();
};

#endif  // SHARE_VM_MATRIX_UBSOCKET_HPP
