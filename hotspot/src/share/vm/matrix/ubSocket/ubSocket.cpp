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

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketAttach.hpp"
#include "matrix/ubSocket/ubSocketDataInfo.hpp"
#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/ubSocket/ubSocketIO.hpp"
#include "matrix/ubSocket/ubSocketMemMapping.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "matrix/ubSocket/ubSocketRing.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/orderAccess.inline.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/thread.hpp"
#include "utilities/growableArray.hpp"

#include "matrix/ubSocket/ubSocket.hpp"

static const uint64_t UB_SOCKET_MAX_MEMORY_SIZE = 4ULL * G;
static const int64_t UB_SOCKET_TRANSFER_BUF_SIZE = 64 * K;
static const int UB_SOCKET_TCP_BUFFER_SIZE = 4 * M;

enum UBSocketControlSendResult {
  UB_SOCKET_CONTROL_SEND_OK,
  UB_SOCKET_CONTROL_SEND_RETRYABLE,
  UB_SOCKET_CONTROL_SEND_ABORT
};

static bool ub_socket_wakeup_send_aborts_fd(int error_code, size_t bytes_sent) {
  return bytes_sent > 0 || error_code == ECONNRESET || error_code == EPIPE ||
         error_code == ECONNABORTED || error_code == ENOTCONN;
}

static UBSocketControlSendResult ub_socket_send_control_frame(int socket_fd,
                                                              uint16_t kind,
                                                              const char* name) {
  UBSocketWakeupFrame frame = ub_socket_wakeup_frame(kind);
  size_t bytes_sent = 0;
  if (kind == UB_SOCKET_WAKEUP) {
    OrderAccess::fence();
  }
  ssize_t nsend = ub_socket_wakeup_send(socket_fd, frame, &bytes_sent);
  int send_errno = errno;
  if (nsend == UB_SOCKET_WAKEUP_FRAME_WIRE_SIZE) {
    return UB_SOCKET_CONTROL_SEND_OK;
  }

  UB_LOG(UB_SOCKET, UB_LOG_WARNING,
         "fd=%d send %s frame failed rc=%ld err=%d bytes_sent=" SIZE_FORMAT "\n",
         socket_fd, name, (long)nsend, send_errno, bytes_sent);
  errno = send_errno;
  return ub_socket_wakeup_send_aborts_fd(send_errno, bytes_sent)
      ? UB_SOCKET_CONTROL_SEND_ABORT : UB_SOCKET_CONTROL_SEND_RETRYABLE;
}

static void ub_socket_abort_fd_after_wakeup_failure(int socket_fd) {
  (void)shutdown(socket_fd, SHUT_RDWR);
  UBSocketManager::unregister_fd(socket_fd);
}

static UBSocketControlSendResult ub_socket_send_wakeup_with_retry(
    int socket_fd, UBSocketConnection* conn, uint64_t bytes) {
  UBSocketControlSendResult send_result =
      ub_socket_send_control_frame(socket_fd, UB_SOCKET_WAKEUP, "WAKEUP");
  if (send_result == UB_SOCKET_CONTROL_SEND_OK) {
    conn->end_tx_wakeup();
    return send_result;
  }

  conn->cancel_tx_wakeup();
  if (send_result == UB_SOCKET_CONTROL_SEND_ABORT) {
    return send_result;
  }

  int first_errno = errno;
  UBSocketProfiler::count(UB_PROF_WAKEUP_REQUEST_RETRY, bytes);
  send_result = ub_socket_send_control_frame(socket_fd, UB_SOCKET_WAKEUP, "WAKEUP");
  if (send_result == UB_SOCKET_CONTROL_SEND_OK) {
    conn->end_tx_wakeup();
  } else if (send_result == UB_SOCKET_CONTROL_SEND_RETRYABLE) {
    errno = first_errno;
  }
  return send_result;
}

Symbol *UBSocketManager::shared_memory_name = NULL;
void *UBSocketManager::shared_memory_addr = NULL;
bool UBSocketManager::_initialized = false;

AllowListTable* UBSocketManager::_allow_list_table = NULL;

size_t UBSocketManager::memory_size() {
  return (size_t)UBSocketMemorySize;
}

void UBSocketManager::init() {
  if (!UseUBSocket) { return; }
  if (ub_option_blank(UBSocketConf)) {
    tty->print_cr("UBSocketConf path is NULL, UBSocket is disabled.");
    return;
  }

  _allow_list_table = new AllowListTable(UB_SOCKET);
  if (_allow_list_table->load_from_file(UBSocketConf) == 0) {
    tty->print_cr("UBSocket load allow-list failed or empty: %s\n", UBSocketConf);
    return;
  }
  UBSocketEndpointMap::init();
  UBSocketEndpointMap::load_from_file(UBSocketConf);

  if (UBSocketPort <= 0 || UBSocketPort > UB_SOCKET_PORT_MAX) {
    tty->print_cr("UBSocket port(" UINTX_FORMAT ") invalid, UBSocket is disabled.",
                  UBSocketPort);
    return;
  }

  if ((uint64_t)UBSocketMemorySize == 0 ||
      (uint64_t)UBSocketMemorySize > UB_SOCKET_MAX_MEMORY_SIZE) {
    tty->print_cr("UBSocket memory size(" UINTX_FORMAT ") invalid, UBSocket is disabled.",
                  UBSocketMemorySize);
    return;
  }

  uint64_t memory_bytes = (uint64_t)memory_size();
  if ((uint64_t)UBSocketRingCount == 0 ||
      (uint64_t)UBSocketRingCount > (uint64_t)UINT32_MAX ||
      memory_bytes % (uint64_t)UBSocketRingCount != 0) {
    tty->print_cr("UBSocket ring count(" UINTX_FORMAT
                  ") invalid for memory size(" SIZE_FORMAT
                  "), UBSocket is disabled.",
                  UBSocketRingCount, memory_size());
    return;
  }
  uint64_t ring_slot_size = memory_bytes / (uint64_t)UBSocketRingCount;
  if (ring_slot_size <= sizeof(UBSocketRingHeader) ||
      ring_slot_size > (uint64_t)UINT32_MAX) {
    tty->print_cr("UBSocket ring size(" UINT64_FORMAT
                  ") invalid for memory size(" SIZE_FORMAT
                  ") ring count(" UINTX_FORMAT "), UBSocket is disabled.",
                  ring_slot_size, memory_size(), UBSocketRingCount);
    return;
  }

  char mem_name[UB_SOCKET_MEM_NAME_BUF_LEN];
  char* mem_name_pos = mem_name;
  memcpy(mem_name_pos, UB_SOCKET_MEM_PREFIX, UB_SOCKET_MEM_PREFIX_LEN);
  mem_name_pos += UB_SOCKET_MEM_PREFIX_LEN;

  char hostname[UB_SOCKET_MEM_NAME_BUF_LEN] = {0};
  if (gethostname(hostname, sizeof(hostname)) != 0) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "gethostname failed, UBSocket disabled\n");
    return;
  }
  hostname[sizeof(hostname) - 1] = '\0';
  memset(mem_name_pos, '_', UB_SOCKET_MEM_HOST_LEN);
  size_t host_num = strnlen(hostname, sizeof(hostname));
  if (host_num >= UB_SOCKET_MEM_HOST_LEN) {
    memcpy(mem_name_pos, hostname + (host_num - UB_SOCKET_MEM_HOST_LEN),
           UB_SOCKET_MEM_HOST_LEN);
  } else {
    memcpy(mem_name_pos + (UB_SOCKET_MEM_HOST_LEN - host_num), hostname, host_num);
  }
  mem_name_pos += UB_SOCKET_MEM_HOST_LEN;

  uint32_t pid = (uint32_t)getpid();
  jio_snprintf(mem_name_pos, UB_SOCKET_MEM_PID_LEN + 1, "%08x", pid);
  mem_name_pos += UB_SOCKET_MEM_PID_LEN;

  uint32_t time_low32 = (uint32_t)os::javaTimeMillis();
  jio_snprintf(mem_name_pos, UB_SOCKET_MEM_TIME_LEN + 1, "%08x", time_low32);
  mem_name[UB_SOCKET_MEM_NAME_LEN] = '\0';
  if (mem_name_pos + UB_SOCKET_MEM_TIME_LEN != mem_name + UB_SOCKET_MEM_NAME_LEN) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "unexpected mem_name length, UBSocket disabled\n");
    return;
  }

  jlong malloc_start_ns = os::javaTimeNanos();
  int error_code = os::Linux::ub_malloc(mem_name, memory_size());
  uint64_t malloc_cost_ns = os::javaTimeNanos() - malloc_start_ns;
  if (error_code != 0) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "init malloc failed name=%s err=%d, UBSocket disabled\n", mem_name, error_code);
    return;
  }
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
         "init malloc success name=%s size=" SIZE_FORMAT
         " ring_count=" UINTX_FORMAT " ring_size=" UINT64_FORMAT
         " cost=" UINT64_FORMAT " ns\n",
         mem_name, memory_size(), UBSocketRingCount, ring_slot_size, malloc_cost_ns);
  shared_memory_name = SymbolTable::new_symbol(mem_name, JavaThread::current());
  jlong mmap_start_ns = os::javaTimeNanos();
  shared_memory_addr = os::Linux::ub_mmap(mem_name, memory_size(), &error_code);
  uint64_t mmap_cost_ns = os::javaTimeNanos() - mmap_start_ns;
  if (shared_memory_addr == NULL) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "init mmap failed name=%s err=%d, UBSocket disabled\n", mem_name, error_code);
    clean_ub_resources();
    return;
  }
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
         "init mmap success name=%s addr=%p size=" SIZE_FORMAT
         " cost=" UINT64_FORMAT " ns\n",
         mem_name, shared_memory_addr, memory_size(), mmap_cost_ns);

  if (!UBSocketRingSlots::init(shared_memory_addr, memory_size(),
                               (uint32_t)UBSocketRingCount)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "init ring slots failed size=" SIZE_FORMAT
           " ring_count=" UINTX_FORMAT ", UBSocket disabled\n",
           memory_size(), UBSocketRingCount);
    clean_ub_resources();
    return;
  }
  UBSocketMemMapping::init();
  UBSocketConnectionTable::init();
  UBSocketAttachAgent::init();
  UBSocketSessionCaches::init();
  UBSocketEarlyReqQueue::init();

  _initialized = true;
}

void UBSocketManager::clean_ub_resources() {
  ResourceMark rm;
  jlong start_time = os::javaTimeNanos();
  const char* mem_name = "<none>";
  if (shared_memory_addr != NULL) {
    int error_code = os::Linux::ub_munmap(shared_memory_addr, memory_size());
    if (error_code != 0) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING, "cleanup munmap failed addr=%p err=%d\n",
             shared_memory_addr, error_code);
    }
    shared_memory_addr = NULL;
  }
  if (shared_memory_name != NULL) {
    mem_name = shared_memory_name->as_C_string();
    int error_code = os::Linux::ub_free(mem_name);
    if (error_code != 0) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "cleanup free failed name=%s err=%d\n", mem_name, error_code);
    }
    shared_memory_name->decrement_refcount();
    shared_memory_name = NULL;
  }
  uint64_t cost_ns = os::javaTimeNanos() - start_time;
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "cleanup ub=%s cost=" UINT64_FORMAT " ns\n", mem_name, cost_ns);
}

void UBSocketManager::before_exit() {
  if (!_initialized) return;
  UBSocketConnectionTable::start_shutdown();
  UBSocketAttachAgent::shutdown();
  UBSocketEarlyReqQueue::cleanup();
  UBSocketEndpointMap::cleanup();
  int abnormal_fds = UBSocketConnectionTable::unregister_abnormal_fds();
  if (abnormal_fds > 0) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "shutdown cleaned %d abnormal fds\n", abnormal_fds);
  }
  clean_ub_resources();
}

bool UBSocketManager::check_stack() {
  if (!_initialized) return false;
  return _allow_list_table->check_stack();
}

void UBSocketManager::check_options() {
  if (UBSocketConf != NULL && UBSocketConf[0] != '\0') {
    tty->print_cr("UBSocket is disabled, but conf path is set.");
  }
  if (UBSocketPort != 0) {
    tty->print_cr("UBSocket is disabled, but control port is set.");
  }
  if (!FLAG_IS_DEFAULT(UBSocketMemorySize)) {
    tty->print_cr("UBSocket is disabled, but memory size is set.");
  }
  if (!FLAG_IS_DEFAULT(UBSocketRingCount)) {
    tty->print_cr("UBSocket is disabled, but ring count is set.");
  }
}

long UBSocketManager::write_data(void *buf, int socket_fd, size_t len) {
  if (!UseUBSocket || !_initialized || len == 0) {
    return 0;
  }

  bool need_wakeup = false;
  long ring_write = 0;
  bool abort_fd = false;
  bool detach_error = false;
  int abort_errno = 0;
  int detach_errno = 0;
  {
    UBSocketConnectionHandle conn(socket_fd);
    UBSocketConnection* connection = conn.get();
    if (connection == NULL) {
      errno = ENOTCONN;
      return -1;
    }
    ring_write = connection->write_data(buf, len, &need_wakeup);
    if (ring_write < 0 && errno == EINVAL) {
      detach_errno = errno;
      detach_error = true;
    }
    if (ring_write > 0 && need_wakeup) {
      UBSocketControlSendResult send_result =
          ub_socket_send_wakeup_with_retry(socket_fd, connection,
                                           (uint64_t)ring_write);
      if (send_result == UB_SOCKET_CONTROL_SEND_ABORT) {
        abort_errno = errno;
        abort_fd = true;
      }
    }
  }
  if (abort_fd) {
    ub_socket_abort_fd_after_wakeup_failure(socket_fd);
    errno = abort_errno;
    return -1;
  }
  if (detach_error) {
    detach_fd(socket_fd);
    errno = detach_errno;
    return -1;
  }
  if (ring_write >= 0) {
    return ring_write;
  }
  return -1;
}

int64_t UBSocketManager::transfer_from_file(int src_fd, int socket_fd,
                                            int64_t offset, int64_t count) {
  if (!UseUBSocket || !_initialized || count <= 0) {
    return 0;
  }
  if (!has_registered(socket_fd)) {
    return 0;
  }

  size_t buf_size = (size_t)MIN2(count, UB_SOCKET_TRANSFER_BUF_SIZE);
  char* buffer = NEW_C_HEAP_ARRAY(char, buf_size, mtInternal);
  int64_t total_write = 0;

  while (total_write < count) {
    int64_t read_target = MIN2((int64_t)buf_size, count - total_write);
    int64_t total_read = 0;
    while (total_read < read_target) {
      ssize_t nread = pread64(src_fd, buffer + total_read,
                              (size_t)(read_target - total_read),
                              (off64_t)(offset + total_write + total_read));
      if (nread < 0) {
        if (errno == EINTR) { continue; }
        if (total_write > 0) {
          FREE_C_HEAP_ARRAY(char, buffer, mtInternal);
          return total_write;
        }
        FREE_C_HEAP_ARRAY(char, buffer, mtInternal);
        return -1;
      }
      if (nread == 0) {
        FREE_C_HEAP_ARRAY(char, buffer, mtInternal);
        UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
               "transfer_from_file src=%d fd=%d offset=%ld count=%ld written=%ld eof=1\n",
               src_fd, socket_fd, (long)offset, (long)count, (long)total_write);
        return total_write;
      }
      total_read += (int64_t)nread;
    }

    int64_t chunk_write = 0;
    while (chunk_write < total_read) {
      long nwrite = write_data(buffer + chunk_write, socket_fd,
                               (size_t)(total_read - chunk_write));
      if (nwrite <= 0) {
        if (total_write + chunk_write > 0) {
          FREE_C_HEAP_ARRAY(char, buffer, mtInternal);
          return total_write + chunk_write;
        }
        FREE_C_HEAP_ARRAY(char, buffer, mtInternal);
        return nwrite;
      }
      chunk_write += (int64_t)nwrite;
    }
    total_write += chunk_write;
  }

  FREE_C_HEAP_ARRAY(char, buffer, mtInternal);
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
         "transfer_from_file src=%d fd=%d offset=%ld count=%ld written=%ld\n",
         src_fd, socket_fd, (long)offset, (long)count, (long)total_write);
  return total_write;
}

static bool ub_socket_handle_parsed_frame(int socket_fd,
                                          UBSocketConnection* conn,
                                          const UBSocketWakeupFrame& frame) {
  UB_PROFILE_COUNT(UB_PROF_WAKEUP_FRAME_COUNT, UB_SOCKET_WAKEUP_FRAME_WIRE_SIZE);
  if (frame.kind == UB_SOCKET_WAKEUP) {
    if (!conn->mark_rx_wakeup()) {
      return false;
    }
    UB_LOG(UB_SOCKET, UB_LOG_DEBUG, "fd=%d recv WAKEUP frame\n", socket_fd);
    return true;
  }
  if (frame.kind == UB_SOCKET_CLOSE) {
    conn->mark_peer_closed();
    UB_LOG(UB_SOCKET, UB_LOG_DEBUG, "fd=%d recv CLOSE frame\n", socket_fd);
    return true;
  }
  errno = EBADMSG;
  UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d recv unsupported control frame kind=%u\n",
         socket_fd, frame.kind);
  conn->mark_error();
  return false;
}

long UBSocketManager::parse_msg(int socket_fd, const char* ub_msg, size_t ub_msg_len) {
  if (!UseUBSocket || !_initialized || ub_msg == NULL || ub_msg_len == 0) {
    return 0;
  }

  size_t consumed = 0;
  char frame_buf[UB_SOCKET_WAKEUP_FRAME_WIRE_SIZE];
  size_t residue_len = 0;
  UBSocketConnectionHandle conn(socket_fd);
  if (conn.get() == NULL) { return -1; }
  if (!conn.get()->take_frame_residue(frame_buf, sizeof(frame_buf), &residue_len)) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d residue take failed: %s\n",
           socket_fd, strerror(errno));
    return -1;
  }

  if (residue_len > 0) {
    size_t need = UB_SOCKET_WAKEUP_FRAME_WIRE_SIZE - residue_len;
    if (ub_msg_len < need) {
      memcpy(frame_buf + residue_len, ub_msg, ub_msg_len);
      if (!conn.get()->store_frame_residue(frame_buf, residue_len + ub_msg_len)) {
        UB_LOG(UB_SOCKET, UB_LOG_ERROR,
               "fd=%d residue append failed len=" SIZE_FORMAT "\n",
               socket_fd, ub_msg_len);
        return -1;
      }
      UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
             "fd=%d parse_msg stored partial residue=" SIZE_FORMAT
             " consumed=" SIZE_FORMAT "\n",
             socket_fd, residue_len + ub_msg_len, ub_msg_len);
      return 0;
    }
    memcpy(frame_buf + residue_len, ub_msg, need);
    consumed += need;
    UBSocketWakeupFrame frame;
    if (!ub_socket_wakeup_parse(frame_buf, &frame)) {
      UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d recv data frame invalid: %s\n",
             socket_fd, strerror(errno));
      conn.get()->mark_error();
      return -1;
    }
    if (!ub_socket_handle_parsed_frame(socket_fd, conn.get(), frame)) { return -1; }
  }

  while (ub_msg_len - consumed >= UB_SOCKET_WAKEUP_FRAME_WIRE_SIZE) {
    UBSocketWakeupFrame frame;
    if (!ub_socket_wakeup_parse(ub_msg + consumed, &frame)) {
      UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d recv data frame invalid: %s\n",
             socket_fd, strerror(errno));
      conn.get()->mark_error();
      return -1;
    }
    consumed += UB_SOCKET_WAKEUP_FRAME_WIRE_SIZE;
    if (!ub_socket_handle_parsed_frame(socket_fd, conn.get(), frame)) { return -1; }
  }

  if (ub_msg_len > consumed) {
    size_t remain = ub_msg_len - consumed;
    if (!conn.get()->store_frame_residue(ub_msg + consumed, remain)) {
      UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d residue store failed len=" SIZE_FORMAT "\n",
             socket_fd, remain);
      return -1;
    }
    UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
           "fd=%d parse_msg stored tail residue=" SIZE_FORMAT " consumed=" SIZE_FORMAT "\n",
           socket_fd, remain, consumed);
  }

  if (consumed == 0) { return 0; }
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
         "fd=%d parse_msg consumed=" SIZE_FORMAT " input=" SIZE_FORMAT "\n",
         socket_fd, consumed, ub_msg_len);
  return (long)consumed;
}

long UBSocketManager::read_data(void *buf, int socket_fd, size_t len) {
  if (!UseUBSocket || !_initialized) return 0;
  long nread = 0;
  bool detach_error = false;
  int detach_errno = 0;
  {
    UBSocketConnectionHandle conn(socket_fd);
    if (conn.get() == NULL) {
      errno = ENOTCONN;
      return -1;
    }
    nread = conn.get()->read_data(buf, len);
    if (nread < 0 && errno == EINVAL) {
      detach_errno = errno;
      detach_error = true;
    }
  }
  if (detach_error) {
    detach_fd(socket_fd);
    errno = detach_errno;
    return -1;
  }
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
         "fd=%d read_data requested=" SIZE_FORMAT " read=%ld\n", socket_fd, len, nread);
  return nread;
}

int32_t UBSocketManager::register_fd(int socket_fd, bool is_server) {
  if (!UseUBSocket || !_initialized) return UB_SOCKET_REGISTER_FALLBACK;
  long start_time = os::javaTimeNanos();

  if (has_registered(socket_fd)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d register skipped: already registered\n", socket_fd);
    return UB_SOCKET_REGISTER_SUCCESS;
  }

  UBSocketAttach socket_attach(socket_fd, is_server, shared_memory_name, memory_size());
  int32_t attach_result = socket_attach.do_attach();
  if (attach_result != UB_SOCKET_REGISTER_SUCCESS) {
    UB_PROFILE_COUNT(UB_PROF_UB_ATTACH_FALLBACK, 0);
    char peer[UB_SOCKET_PEER_TEXT_BUF_LEN];
    ub_socket_peer_to_string(socket_fd, peer, sizeof(peer));
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "fd=%d peer=%s register attach failed role=%s %s\n",
           socket_fd, peer, is_server ? "server" : "client",
           attach_result == UB_SOCKET_REGISTER_ABORT ? "abort" : "fallback=tcp");
    if (has_registered(socket_fd)) { unregister_fd(socket_fd); }
    return attach_result;
  }

  int buffer_size = UB_SOCKET_TCP_BUFFER_SIZE;
  (void)setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF,
                   (const char*)&buffer_size, sizeof(buffer_size));
  (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF,
                   (const char*)&buffer_size, sizeof(buffer_size));

  long cost_time = os::javaTimeNanos() - start_time;
  UB_PROFILE_COUNT(UB_PROF_UB_ATTACH_SUCCESS, 0);
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d register success role=%s cost_ns=%ld\n",
         socket_fd, is_server ? "server" : "client", cost_time);
  return UB_SOCKET_REGISTER_SUCCESS;
}

bool UBSocketManager::unregister_fd(int socket_fd) {
  return unregister_fd(socket_fd, true);
}

bool UBSocketManager::detach_fd(int socket_fd) {
  return unregister_fd(socket_fd, false);
}

bool UBSocketManager::unregister_fd(int socket_fd, bool send_close) {
  if (!UseUBSocket || !_initialized) return false;
  long start_time = os::javaTimeNanos();
  UBSocketConnection* conn = UBSocketConnectionTable::begin_close(socket_fd);
  if (conn == NULL) {
    return false;
  }
  if (send_close) {
    (void)ub_socket_send_control_frame(socket_fd, UB_SOCKET_CLOSE, "CLOSE");
  }
  UBSocketConnectionTable::finish_close(socket_fd, conn);
  delete conn;
  long cost_time = os::javaTimeNanos() - start_time;
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d unregister cost_ns=%ld\n", socket_fd, cost_time);
  return true;
}

bool UBSocketManager::mark_control_closed(int socket_fd) {
  if (!UseUBSocket || !_initialized) { return false; }
  UBSocketConnectionHandle conn(socket_fd);
  if (conn.get() == NULL) { return false; }
  conn.get()->mark_control_closed();
  return true;
}

bool UBSocketManager::has_registered(int socket_fd) {
  if (!UseUBSocket || !_initialized) return false;
  return UBSocketConnectionTable::contains(socket_fd);
}

bool UBSocketManager::has_pending_data(int socket_fd) {
  if (!UseUBSocket || !_initialized) return false;
  UBSocketConnectionHandle conn(socket_fd);
  return conn.get() != NULL && conn.get()->has_pending_data();
}

bool UBSocketManager::wait_fd_ready(int socket_fd) {
  if (!UseUBSocket || !_initialized) { return false; }
  UBSocketConnectionHandle conn(socket_fd);
  return conn.get() != NULL && conn.get()->ready();
}
