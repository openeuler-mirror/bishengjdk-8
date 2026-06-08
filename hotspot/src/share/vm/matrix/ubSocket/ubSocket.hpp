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
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "runtime/os.hpp"

static const char UB_SOCKET_MEM_PREFIX[] = "sock_";
static const int UB_SOCKET_PORT_MAX = 65535;
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
  UB_ATTACH_IO_IDLE_POLL_MS = 100,
  // Fixed internal deadlines for wakeup send and attach handshake.
  UB_WAKEUP_SEND_TIMEOUT_MS = 50,
  // Server accept waits past the attach agent idle poll interval before TCP fallback.
  UB_ATTACH_REQUEST_WAIT_MS = 200,
  UB_ATTACH_TIMEOUT_MS = 500,
};

enum UBSocketRegisterResult {
  UB_SOCKET_REGISTER_FALLBACK = 0,
  UB_SOCKET_REGISTER_SUCCESS = 1,
  UB_SOCKET_REGISTER_ABORT = 2
};

class UBSocketManager : public AllStatic {
 public:
  static Symbol* shared_memory_name;
  static void* shared_memory_addr;

  static bool check_stack();
  static void check_options();

  static void init();
  static void before_exit();
  static void clean_ub_resources();

  static long read_data(void* buf, int socket_fd, size_t len);
  static long write_data(void* buf, int socket_fd, size_t len);
  static int64_t transfer_from_file(int src_fd, int socket_fd,
                                    int64_t offset, int64_t count);
  static long parse_msg(int socket_fd, const char* ub_msg, size_t ub_msg_len);

  static int32_t register_fd(int socket_fd, bool is_server);
  static bool unregister_fd(int socket_fd);
  static bool detach_fd(int socket_fd);
  static bool mark_control_closed(int socket_fd);
  static bool has_registered(int socket_fd);
  static bool has_pending_data(int socket_fd);
  static bool wait_fd_ready(int socket_fd);

 private:
  static AllowListTable* _allow_list_table;

  static bool _initialized;
  static uint32_t _blk_size;
  static uint32_t _blk_count;
  static size_t memory_size();
  static bool unregister_fd(int socket_fd, bool send_close);
};

#endif  // SHARE_VM_MATRIX_UBSOCKET_HPP
