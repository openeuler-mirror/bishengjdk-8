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
#include "matrix/ubSocket/ubSocketConfig.hpp"
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
  // Fixed internal deadlines for control send and attach handshake.
  UB_CONTROL_SEND_TIMEOUT_MS = 50,
  // Server accept waits past the attach agent idle poll interval before TCP fallback.
  UB_ATTACH_REQUEST_WAIT_MS = 200,
  UB_ATTACH_TIMEOUT_MS = 500,
  // Enabled-mode progress publication while the peer Ring stays non-empty.
  UB_POLL_NOTIFY_PROGRESS_BYTES = 256*K
};

class Monitor;
class outputStream;
struct UBSocketProcessHeader;
struct UBSocketSelectorReadyEvent;

enum UBSocketRegisterResult {
  UB_SOCKET_REGISTER_FALLBACK = 0,
  UB_SOCKET_REGISTER_SUCCESS = 1,
  UB_SOCKET_REGISTER_ABORT = 2,
  UB_SOCKET_REGISTER_SKIPPED = 3
};

class UBSocketManager : public AllStatic {
 public:
  static Symbol* shared_memory_name;
  static void* shared_memory_addr;

  static bool check_stack();
  static void check_options();
  static bool reload_config(const char* path, outputStream* out);

  static void init();
  static void before_exit();
  static void clean_ub_resources();

  static long read_data(void* buf, int socket_fd, size_t len);
  static long write_data(void* buf, int socket_fd, size_t len);
  static int64_t transfer_from_file(int src_fd, int socket_fd,
                                    int64_t offset, int64_t count);
  static long parse_control(int socket_fd, const char* msg, size_t msg_len);

  static int32_t register_fd(int socket_fd, bool is_server);
  static bool unregister_fd(int socket_fd);
  static bool detach_fd(int socket_fd);
  static bool mark_control_closed(int socket_fd);
  static bool shutdown_output(int socket_fd);
  static bool peer_close_ready(int socket_fd);
  static bool fail_close(int socket_fd);
  static bool has_registered(int socket_fd);
  static bool has_pending_data(int socket_fd);
  static bool wait_fd_ready(int socket_fd);
  static bool polling_notify_enabled() { return UseUBSocket && _initialized; }
  static bool spin_enabled() {
    return UseUBSocket && _initialized && UBSocketSpinNanos > 0;
  }
  static bool spin_wait_data(int socket_fd);
  static bool register_selector_notifier(int event_fd);
  static void unregister_selector_notifier(int event_fd);
  static jlong subscribe_selector(int socket_fd, int event_fd);
  static void unsubscribe_selector(jlong subscription_id);
  static int32_t drain_selector_ready(int event_fd,
                                      UBSocketSelectorReadyEvent* events,
                                      int32_t capacity,
                                      jlong* profile_complete_ns);
  static void complete_selector_notification(jlong complete_ns, bool injected);

 private:
  static UBSocketConfig* _config;
  static Monitor* _config_lock;
  static int _config_readers;
  static bool _config_reloading;

  static bool _initialized;
  static size_t ring_memory_size();
  static size_t mapping_size();
  static UBSocketProcessHeader* process_header();
  static UBSocketConfig* acquire_config(bool* reloading);
  static void release_config(UBSocketConfig* config);
  static bool unregister_fd(int socket_fd, bool send_close);
};

#endif  // SHARE_VM_MATRIX_UBSOCKET_HPP
