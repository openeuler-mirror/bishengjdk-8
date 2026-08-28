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
#include <limits.h>
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
#include "matrix/ubSocket/ubSocketPoller.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "matrix/ubSocket/ubSocketRing.hpp"
#include "matrix/ubSocket/ubSocketSelectorNotifier.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "memory/resourceArea.hpp"
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

static bool ub_socket_control_send_aborts_fd(int error_code,
                                             size_t bytes_sent) {
  return bytes_sent > 0 || error_code == ECONNRESET || error_code == EPIPE ||
         error_code == ECONNABORTED || error_code == ENOTCONN;
}

static UBSocketControlSendResult ub_socket_send_control_frame(
    int socket_fd, const UBSocketControlFrame& frame) {
  size_t bytes_sent = 0;
  ssize_t nsend = ub_socket_control_send(socket_fd, frame, &bytes_sent);
  int send_errno = errno;
  if (nsend == UB_SOCKET_CONTROL_FRAME_WIRE_SIZE) {
    return UB_SOCKET_CONTROL_SEND_OK;
  }

  UB_LOG(UB_SOCKET, UB_LOG_WARNING,
         "fd=%d send control kind=%u failed rc=%ld err=%d bytes_sent="
         SIZE_FORMAT "\n",
         socket_fd, frame.kind, (long)nsend, send_errno, bytes_sent);
  errno = send_errno;
  return ub_socket_control_send_aborts_fd(send_errno, bytes_sent)
      ? UB_SOCKET_CONTROL_SEND_ABORT : UB_SOCKET_CONTROL_SEND_RETRYABLE;
}

Symbol *UBSocketManager::shared_memory_name = NULL;
void *UBSocketManager::shared_memory_addr = NULL;
bool UBSocketManager::_initialized = false;
UBSocketConfig* UBSocketManager::_config = NULL;
Monitor* UBSocketManager::_config_lock = NULL;
int UBSocketManager::_config_readers = 0;
bool UBSocketManager::_config_reloading = false;

size_t UBSocketManager::ring_memory_size() {
  return (size_t)UBSocketMemorySize;
}

size_t UBSocketManager::mapping_size() {
  UBSocketMemoryLayout layout;
  if (!UBSocketMemoryLayout::create((uint64_t)ring_memory_size(),
                                    (uint32_t)UBSocketRingCount,
                                    &layout)) {
    return 0;
  }
  return (size_t)layout.mapping_size;
}

UBSocketProcessHeader* UBSocketManager::process_header() {
  return reinterpret_cast<UBSocketProcessHeader*>(shared_memory_addr);
}

void UBSocketManager::init() {
  STATIC_ASSERT(sizeof(UBSocketProcessHeader) == DEFAULT_CACHE_LINE_SIZE);
  STATIC_ASSERT(sizeof(UBSocketRingWriteState) == DEFAULT_CACHE_LINE_SIZE);
  STATIC_ASSERT(sizeof(UBSocketRingPeerState) == DEFAULT_CACHE_LINE_SIZE);
  if (!UseUBSocket) { return; }
  if (ub_option_blank(UBSocketConf)) {
    tty->print_cr("UBSocketConf path is NULL, UBSocket is disabled.");
    return;
  }

  if (UBSocketAttach == NULL ||
      (strcmp(UBSocketAttach, "limit") != 0 && strcmp(UBSocketAttach, "all") != 0)) {
    tty->print_cr("UBSocket attach policy(%s) invalid, expected limit or all; UBSocket is disabled.",
                  UBSocketAttach == NULL ? "<null>" : UBSocketAttach);
    return;
  }

  char config_error[512] = {0};
  _config = UBSocketConfig::load(UBSocketConf, false,
                                 config_error, sizeof(config_error));
  if (_config == NULL || _config->stack_count() == 0) {
    tty->print_cr("UBSocket load configuration failed or empty: %s (%s)\n",
                  UBSocketConf, config_error[0] == '\0' ? "no stack entries" : config_error);
    delete _config;
    _config = NULL;
    return;
  }
  _config_lock = new Monitor(Mutex::leaf, "UBSocketConfig_lock");
  _config_readers = 0;
  _config_reloading = false;

  if (UBSocketPort <= 0 || UBSocketPort > UB_SOCKET_PORT_MAX) {
    tty->print_cr("UBSocket port(" UINTX_FORMAT ") invalid, UBSocket is disabled.",
                  UBSocketPort);
    return;
  }

  if (UBSocketPollerCpu < -1 ||
      (UBSocketPollerCpu >= 0 && (uintx)UBSocketPollerCpu > max_juint)) {
    tty->print_cr("UBSocket poller CPU(" INTX_FORMAT
                  ") invalid, UBSocket is disabled.", UBSocketPollerCpu);
    return;
  }

  if ((uint64_t)UBSocketMemorySize == 0 ||
      (uint64_t)UBSocketMemorySize > UB_SOCKET_MAX_MEMORY_SIZE) {
    tty->print_cr("UBSocket memory size(" UINTX_FORMAT ") invalid, UBSocket is disabled.",
                  UBSocketMemorySize);
    return;
  }

  uint64_t memory_bytes = (uint64_t)ring_memory_size();
  uint32_t max_ring_count =
      UBSocketMemoryLayout::max_ring_count(memory_bytes);
  if ((uint64_t)UBSocketRingCount == 0 ||
      (uint64_t)UBSocketRingCount > max_ring_count) {
    tty->print_cr("UBSocket ring count(" UINTX_FORMAT
                  ") invalid, maximum=%u for memory size(" SIZE_FORMAT
                  "), UBSocket is disabled.",
                  UBSocketRingCount, max_ring_count, ring_memory_size());
    return;
  }
  UBSocketMemoryLayout layout;
  if (!UBSocketMemoryLayout::create(memory_bytes,
                                    (uint32_t)UBSocketRingCount,
                                    &layout)) {
    uint64_t ring_slot_size = memory_bytes / (uint64_t)UBSocketRingCount;
    tty->print_cr("UBSocket ring size(" UINT64_FORMAT
                  ") invalid for memory size(" SIZE_FORMAT
                  ") ring count(" UINTX_FORMAT "), UBSocket is disabled.",
                  ring_slot_size, ring_memory_size(), UBSocketRingCount);
    return;
  }

  char mem_name[UB_SOCKET_MEM_NAME_BUF_LEN];
  char* mem_name_pos = mem_name;
  memcpy(mem_name_pos, UB_SOCKET_MEM_PREFIX, UB_SOCKET_MEM_PREFIX_LEN);
  mem_name_pos += UB_SOCKET_MEM_PREFIX_LEN;

  char hostname[HOST_NAME_MAX + 1] = {0};
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
  int error_code = os::Linux::ub_malloc(mem_name, mapping_size());
  uint64_t malloc_cost_ns = os::javaTimeNanos() - malloc_start_ns;
  if (error_code != 0) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "init malloc failed name=%s err=%d, UBSocket disabled\n", mem_name, error_code);
    return;
  }
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
         "init malloc success name=%s size=" SIZE_FORMAT
         " ring_count=" UINTX_FORMAT " payload_size=" UINT64_FORMAT
         " cost=" UINT64_FORMAT " ns\n",
         mem_name, mapping_size(), UBSocketRingCount, layout.payload_size,
         malloc_cost_ns);
  shared_memory_name = SymbolTable::new_symbol(mem_name, JavaThread::current());
  jlong mmap_start_ns = os::javaTimeNanos();
  shared_memory_addr = os::Linux::ub_mmap(mem_name, mapping_size(), &error_code);
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
         mem_name, shared_memory_addr, mapping_size(), mmap_cost_ns);

  memset(shared_memory_addr, 0, (size_t)layout.payload_base_offset);
  UBSocketProtocolInfo protocol = ub_socket_protocol_info(
      layout.mapping_size, layout.ring_memory_size, layout.ring_count,
      (uint64_t)UBSocketSpinNanos);
  UBSocketProcessHeader* header = process_header();
  header->version = UB_SOCKET_PROTOCOL_VERSION;
  header->capabilities = protocol.capabilities;
  header->ring_count = layout.ring_count;
  header->reserved = 0;
  header->mapping_size = layout.mapping_size;
  header->ring_memory_size = layout.ring_memory_size;
  header->notify_generation = 0;
  if (!UBSocketRingSlots::init(shared_memory_addr, layout)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "init ring slots failed size=" SIZE_FORMAT
           " ring_count=" UINTX_FORMAT ", UBSocket disabled\n",
           ring_memory_size(), UBSocketRingCount);
    clean_ub_resources();
    return;
  }
  UBSocketMemMapping::init();
  UBSocketConnectionTable::init((uint32_t)UBSocketRingCount);
  UBSocketSelectorNotifiers::init();
  UBSocketPoller::init(&header->notify_generation);
  UBSocketAttachAgent::init();
  UBSocketSessionCaches::init();
  UBSocketEarlyReqQueue::init();

  if (!UBSocketPoller::start()) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "start process poller failed, UBSocket disabled\n");
    clean_ub_resources();
    return;
  }
  _initialized = true;
}

void UBSocketManager::clean_ub_resources() {
  ResourceMark rm;
  jlong start_time = os::javaTimeNanos();
  const char* mem_name = "<none>";
  if (shared_memory_addr != NULL) {
    int error_code = os::Linux::ub_munmap(shared_memory_addr, mapping_size());
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
  UBSocketPoller::shutdown();
  UBSocketAttachAgent::shutdown();
  UBSocketEarlyReqQueue::cleanup();
  UBSocketSelectorNotifiers::shutdown();
  int abnormal_fds = UBSocketConnectionTable::unregister_abnormal_fds();
  if (abnormal_fds > 0) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "shutdown cleaned %d abnormal fds\n", abnormal_fds);
  }
  clean_ub_resources();
  UBSocketConfig* old_config = NULL;
  if (_config_lock != NULL) {
    MonitorLockerEx locker(_config_lock, Mutex::_no_safepoint_check_flag);
    while (_config_readers != 0) {
      locker.wait(Mutex::_no_safepoint_check_flag);
    }
    old_config = _config;
    _config = NULL;
  }
  delete old_config;
}

UBSocketConfig* UBSocketManager::acquire_config(bool* reloading) {
  *reloading = false;
  if (_config_lock == NULL) { return NULL; }
  MonitorLockerEx locker(_config_lock, Mutex::_no_safepoint_check_flag);
  if (_config_reloading) {
    *reloading = true;
    return NULL;
  }
  if (_config != NULL) { _config_readers++; }
  return _config;
}

void UBSocketManager::release_config(UBSocketConfig* config) {
  if (config == NULL || _config_lock == NULL) { return; }
  MonitorLockerEx locker(_config_lock, Mutex::_no_safepoint_check_flag);
  _config_readers--;
  if (_config_readers == 0) { locker.notify_all(); }
}

bool UBSocketManager::check_stack() {
  if (!_initialized) return false;
  bool reloading = false;
  UBSocketConfig* config = acquire_config(&reloading);
  bool allowed = config != NULL && config->check_stack();
  release_config(config);
  return allowed;
}

bool UBSocketManager::reload_config(const char* path, outputStream* out) {
  if (!_initialized || _config_lock == NULL) {
    out->print_cr("ERROR: UBSocket is not initialized; current configuration unchanged");
    return false;
  }
  {
    MonitorLockerEx locker(_config_lock, Mutex::_no_safepoint_check_flag);
    if (_config_reloading) {
      out->print_cr("ERROR: UBSocket configuration reload already in progress; current configuration unchanged");
      return false;
    }
    _config_reloading = true;
  }
  UB_LOG(UB_SOCKET, UB_LOG_INFO,
         "configuration reload begin path=%s\n", path == NULL ? "<null>" : path);

  char error[512] = {0};
  UBSocketConfig* replacement = UBSocketConfig::load(path, true, error, sizeof(error));
  if (replacement == NULL) {
    MonitorLockerEx locker(_config_lock, Mutex::_no_safepoint_check_flag);
    _config_reloading = false;
    locker.notify_all();
    out->print_cr("ERROR: UBSocket configuration reload failed");
    out->print_cr("path=%s", path == NULL ? "<null>" : path);
    out->print_cr("reason=%s", error[0] == '\0' ? "invalid configuration" : error);
    out->print_cr("current configuration unchanged");
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "configuration reload failed path=%s reason=%s current unchanged\n",
           path == NULL ? "<null>" : path,
           error[0] == '\0' ? "invalid configuration" : error);
    return false;
  }

  UBSocketConfig* old_config = NULL;
  {
    MonitorLockerEx locker(_config_lock, Mutex::_no_safepoint_check_flag);
    while (_config_readers != 0) {
      locker.wait(Mutex::_no_safepoint_check_flag);
    }
    old_config = _config;
    _config = replacement;
    _config_reloading = false;
    locker.notify_all();
  }
  delete old_config;
  out->print_cr("UBSocket configuration reloaded successfully");
  out->print_cr("path=%s", path);
  replacement->print_on(out);
  UB_LOG(UB_SOCKET, UB_LOG_INFO,
         "configuration reload success path=%s\n", path);
  return true;
}

void UBSocketManager::check_options() {
  if (UBSocketConf != NULL && UBSocketConf[0] != '\0') {
    tty->print_cr("UBSocket is disabled, but conf path is set.");
  }
  if (!FLAG_IS_DEFAULT(UBSocketAttach)) {
    tty->print_cr("UBSocket is disabled, but attach policy is set.");
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
  if (!FLAG_IS_DEFAULT(UBSocketPollerCpu)) {
    tty->print_cr("UBSocket is disabled, but poller CPU is set.");
  }
  if (!FLAG_IS_DEFAULT(UBSocketSpinNanos)) {
    tty->print_cr("UBSocket is disabled, but spin nanos is set.");
  }
}

long UBSocketManager::write_data(void *buf, int socket_fd, size_t len) {
  if (!UseUBSocket || !_initialized || len == 0) {
    return 0;
  }

  long ring_write = 0;
  bool io_error = false;
  int detach_errno = 0;
  {
    UBSocketConnectionHandle conn(socket_fd);
    UBSocketConnection* connection = conn.get();
    if (connection == NULL) {
      errno = ENOTCONN;
      return -1;
    }
    ring_write = connection->write_data(buf, len);
    if (ring_write < 0 && errno == EINVAL) {
      detach_errno = errno;
      io_error = true;
    }
  }
  if (io_error) {
    fail_close(socket_fd);
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

static bool ub_socket_handle_control_frame(int socket_fd,
                                           UBSocketConnection* conn,
                                           const UBSocketControlFrame& frame) {
  if (!conn->accept_control_frame(frame)) {
    errno = EBADMSG;
    return false;
  }
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
         "fd=%d recv control kind=%u final_write_offset=" UINT64_FORMAT "\n",
         socket_fd, frame.kind, frame.final_write_offset);
  return true;
}

long UBSocketManager::parse_control(int socket_fd, const char* msg,
                                    size_t msg_len) {
  if (!UseUBSocket || !_initialized || msg == NULL || msg_len == 0) {
    return 0;
  }

  size_t consumed = 0;
  char frame_buf[UB_SOCKET_CONTROL_FRAME_WIRE_SIZE];
  size_t residue_len = 0;
  UBSocketConnectionHandle conn(socket_fd);
  if (conn.get() == NULL) { return -1; }
  if (!conn.get()->take_control_residue(frame_buf, sizeof(frame_buf),
                                        &residue_len)) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d control residue take failed: %s\n",
           socket_fd, strerror(errno));
    return -1;
  }

  if (residue_len > 0) {
    size_t need = UB_SOCKET_CONTROL_FRAME_WIRE_SIZE - residue_len;
    if (msg_len < need) {
      memcpy(frame_buf + residue_len, msg, msg_len);
      if (!conn.get()->store_control_residue(frame_buf,
                                             residue_len + msg_len)) {
        UB_LOG(UB_SOCKET, UB_LOG_ERROR,
               "fd=%d control residue append failed len=" SIZE_FORMAT "\n",
               socket_fd, msg_len);
        return -1;
      }
      return 0;
    }
    memcpy(frame_buf + residue_len, msg, need);
    consumed += need;
    UBSocketControlFrame frame;
    if (!ub_socket_control_parse(frame_buf, &frame) ||
        !ub_socket_handle_control_frame(socket_fd, conn.get(), frame)) {
      UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d recv control frame invalid: %s\n",
             socket_fd, strerror(errno));
      conn.get()->mark_error();
      return -1;
    }
  }

  while (msg_len - consumed >= UB_SOCKET_CONTROL_FRAME_WIRE_SIZE) {
    UBSocketControlFrame frame;
    if (!ub_socket_control_parse(msg + consumed, &frame) ||
        !ub_socket_handle_control_frame(socket_fd, conn.get(), frame)) {
      UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d recv control frame invalid: %s\n",
             socket_fd, strerror(errno));
      conn.get()->mark_error();
      return -1;
    }
    consumed += UB_SOCKET_CONTROL_FRAME_WIRE_SIZE;
  }

  if (msg_len > consumed) {
    size_t remain = msg_len - consumed;
    if (!conn.get()->store_control_residue(msg + consumed, remain)) {
      UB_LOG(UB_SOCKET, UB_LOG_ERROR,
             "fd=%d control residue store failed len=" SIZE_FORMAT "\n",
             socket_fd, remain);
      return -1;
    }
  }

  return (long)consumed;
}

long UBSocketManager::read_data(void *buf, int socket_fd, size_t len) {
  if (!UseUBSocket || !_initialized) return 0;
  long nread = 0;
  bool io_error = false;
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
      io_error = true;
    }
  }
  if (io_error) {
    fail_close(socket_fd);
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
  bool reloading = false;
  UBSocketConfig* config = acquire_config(&reloading);
  if (reloading) {
    UB_PROFILE_COUNT(UB_PROF_UB_ATTACH_FALLBACK, 0);
    UB_LOG(UB_SOCKET, UB_LOG_INFO,
           "fd=%d attach skipped during configuration reload, use=tcp\n", socket_fd);
    return UB_SOCKET_REGISTER_FALLBACK;
  }
  if (config == NULL) return UB_SOCKET_REGISTER_FALLBACK;

  UBSocketEndpoint local_ep;
  UBSocketEndpoint remote_ep;
  if (!ub_socket_endpoint_get(socket_fd, &local_ep, &remote_ep)) {
    release_config(config);
    return UB_SOCKET_REGISTER_FALLBACK;
  }
  bool selected = true;
  if (strcmp(UBSocketAttach, "limit") == 0) {
    UBSocketEndpoint control_ep;
    selected = is_server ? config->matches_local_service(&local_ep)
                         : config->control_endpoint_for_data(&remote_ep, &control_ep);
    if (!selected) {
      UB_PROFILE_COUNT(UB_PROF_UB_ATTACH_SKIP, 0);
      UB_LOG(UB_SOCKET, UB_LOG_INFO,
             "fd=%d attach policy=limit mapping=unmatched use=tcp\n", socket_fd);
      release_config(config);
      return UB_SOCKET_REGISTER_SKIPPED;
    }
  }
  if (!config->check_stack()) {
    release_config(config);
    return UB_SOCKET_REGISTER_FALLBACK;
  }

  if (has_registered(socket_fd)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d register skipped: already registered\n", socket_fd);
    release_config(config);
    return UB_SOCKET_REGISTER_SUCCESS;
  }

  UBSocketAttachHandshake socket_attach(socket_fd, is_server, shared_memory_name,
                                        mapping_size(), config);
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
    release_config(config);
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
  release_config(config);
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
    UBSocketControlFrame frame;
    if (conn->prepare_control_frame(UB_SOCKET_CLOSE, &frame)) {
      (void)ub_socket_send_control_frame(socket_fd, frame);
    }
  }
  UBSocketSelectorNotifiers::detach_connection(conn);
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

bool UBSocketManager::shutdown_output(int socket_fd) {
  if (!UseUBSocket || !_initialized) { return true; }
  UBSocketControlSendResult send_result = UB_SOCKET_CONTROL_SEND_OK;
  {
    UBSocketConnectionHandle conn(socket_fd);
    if (conn.get() == NULL) { return true; }
    UBSocketControlFrame frame;
    if (!conn.get()->prepare_control_frame(UB_SOCKET_SHUTDOWN_OUTPUT, &frame)) {
      return false;
    }
    send_result = ub_socket_send_control_frame(socket_fd, frame);
  }
  if (send_result == UB_SOCKET_CONTROL_SEND_OK) { return true; }
  fail_close(socket_fd);
  return false;
}

bool UBSocketManager::peer_close_ready(int socket_fd) {
  if (!UseUBSocket || !_initialized) { return false; }
  UBSocketConnectionHandle conn(socket_fd);
  return conn.get() != NULL && conn.get()->peer_close_ready();
}

bool UBSocketManager::fail_close(int socket_fd) {
  if (!UseUBSocket || !_initialized) { return false; }
  {
    UBSocketConnectionHandle conn(socket_fd);
    if (conn.get() == NULL) { return false; }
    conn.get()->mark_error();
  }
  os::socket_shutdown(socket_fd, SHUT_RDWR);
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

bool UBSocketManager::spin_wait_data(int socket_fd) {
  if (!spin_enabled()) { return false; }
  UBSocketConnectionHandle conn(socket_fd);
  return conn.get() != NULL &&
         conn.get()->spin_rx_ready((uint64_t)UBSocketSpinNanos);
}

bool UBSocketManager::wait_fd_ready(int socket_fd) {
  if (!UseUBSocket || !_initialized) { return false; }
  UBSocketConnectionHandle conn(socket_fd);
  return conn.get() != NULL && conn.get()->ready();
}

bool UBSocketManager::register_selector_notifier(int event_fd) {
  return polling_notify_enabled() &&
         UBSocketSelectorNotifiers::register_fd(event_fd);
}

void UBSocketManager::unregister_selector_notifier(int event_fd) {
  UBSocketSelectorNotifiers::unregister_fd(event_fd);
}

jlong UBSocketManager::subscribe_selector(int socket_fd, int event_fd) {
  if (!polling_notify_enabled() || event_fd < 0) { return 0; }
  UBSocketConnectionHandle connection(socket_fd);
  if (connection.get() == NULL) { return 0; }
  return UBSocketSelectorNotifiers::subscribe(connection.get(), event_fd);
}

void UBSocketManager::unsubscribe_selector(jlong subscription_id) {
  UBSocketSelectorNotifiers::unsubscribe(subscription_id);
}

int32_t UBSocketManager::drain_selector_ready(
    int event_fd, UBSocketSelectorReadyEvent* events, int32_t capacity,
    jlong* profile_complete_ns) {
  return UBSocketSelectorNotifiers::drain_ready(
      event_fd, events, capacity, profile_complete_ns);
}

void UBSocketManager::complete_selector_notification(jlong complete_ns, bool injected) {
  if (!UBSocketProfiler::enabled(UB_PROF_EVENTFD_TO_SELECTOR_INJECT_NS)) {
    return;
  }
  uint64_t injected_ns = (uint64_t)os::javaTimeNanos();
  if (!injected || complete_ns <= 0 ||
      injected_ns < (uint64_t)complete_ns) {
    UBSocketProfiler::count(UB_PROF_POLL_PROFILE_INVALID);
    return;
  }
  UBSocketProfiler::record_latency(
      UB_PROF_EVENTFD_TO_SELECTOR_INJECT_NS,
      injected_ns - (uint64_t)complete_ns);
}
