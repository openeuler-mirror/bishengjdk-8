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
#include <unistd.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketAttach.hpp"
#include "matrix/ubSocket/ubSocketDataInfo.hpp"
#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/ubSocket/ubSocketIO.hpp"
#include "matrix/ubSocket/ubSocketMemMapping.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/atomic.inline.hpp"
#include "runtime/thread.hpp"
#include "utilities/growableArray.hpp"

#include "matrix/ubSocket/ubSocket.hpp"

static const uint32_t UB_SOCKET_DEFAULT_BLOCK_SIZE = 2 * K;
static const uint32_t UB_SOCKET_DEFAULT_BLOCK_COUNT = 128 * K; // 256M total
static const uint64_t UB_SOCKET_MAX_MEMORY_SIZE = 4ULL * G;
static const size_t UB_SOCKET_DATA_FRAME_SIZE = UB_SOCKET_DATA_FRAME_WIRE_SIZE;

Symbol *UBSocketManager::shared_memory_name = NULL;
void *UBSocketManager::shared_memory_addr = NULL;
uint64_t UBSocketManager::package_timeout = 0;
bool UBSocketManager::_initialized = false;
uint32_t UBSocketManager::_blk_size = UB_SOCKET_DEFAULT_BLOCK_SIZE;
uint32_t UBSocketManager::_blk_count = UB_SOCKET_DEFAULT_BLOCK_COUNT;

AllowListTable* UBSocketManager::_allow_list_table = NULL;

static void release_untracked_blocks(GrowableArray<UBSocketBlkItem>* blk_items,
                                     int start_index = 0) {
  for (int i = start_index; i < blk_items->length(); i++) {
    UBSocketBlkItem item = blk_items->at(i);
    UBSocketManager::clear_mark(item.meta_addr + sizeof(UBSocketBlkMeta));
    UBSocketBlkBitmap::release(item.start_blk, item.blk_count);
  }
}

size_t UBSocketManager::memory_size() {
  return (size_t)_blk_size * _blk_count;
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

  if (UBSocketPort <= 0 || UBSocketPort > 65535) {
    tty->print_cr("UBSocket port(" UINTX_FORMAT ") invalid, UBSocket is disabled.",
                  UBSocketPort);
    return;
  }

  if (UBSocketTimeout != 0 && UBSocketTimeout < UB_SOCKET_READ_TIMEOUT_DEFAULT_MS) {
    tty->print_cr("UBSocket timeout(" UINTX_FORMAT ") invalid, set to %d ms\n",
                  UBSocketTimeout, UB_SOCKET_READ_TIMEOUT_DEFAULT_MS);
    UBSocketTimeout = UB_SOCKET_READ_TIMEOUT_DEFAULT_MS;
  }
  package_timeout = UBSocketTimeout * NANOSECS_PER_MILLISEC;

  if ((uint64_t)UBSocketMemorySize > UB_SOCKET_MAX_MEMORY_SIZE ||
      UBSocketMemorySize < _blk_size || UBSocketMemorySize % _blk_size != 0) {
    tty->print_cr("UBSocket memory size(" UINTX_FORMAT ") invalid, set to default.",
                  UBSocketMemorySize);
    UBSocketMemorySize = UB_SOCKET_DEFAULT_BLOCK_COUNT * UB_SOCKET_DEFAULT_BLOCK_SIZE;
    _blk_count = UB_SOCKET_DEFAULT_BLOCK_COUNT;
  } else {
    _blk_count = (uint32_t)(UBSocketMemorySize / _blk_size);
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
         " block_size=" UINT32_FORMAT " block_count=" UINT32_FORMAT
         " cost=" UINT64_FORMAT " ns\n",
         mem_name, memory_size(), _blk_size, _blk_count, malloc_cost_ns);
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

  UBSocketBlkBitmap::init(_blk_count);
  UBSocketMemMapping::init();
  SocketDataInfoTable::init();
  UnreadMsgTable::init();
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
    shared_memory_name = NULL;
  }
  UBSocketBlkBitmap::cleanup();
  uint64_t cost_ns = os::javaTimeNanos() - start_time;
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "cleanup ub=%s cost=" UINT64_FORMAT " ns\n", mem_name, cost_ns);
}

void UBSocketManager::before_exit() {
  if (!_initialized) return;
  UnreadMsgTable::stop_timer();
  UnreadMsgTable::cleanup();
  UBSocketAttachAgent::shutdown();
  int abnormal_fds = SocketDataInfoTable::unregister_abnormal_fds();
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
  if (UBSocketTimeout != UB_SOCKET_READ_TIMEOUT_DEFAULT_MS) {
    tty->print_cr("UBSocket is disabled, but timeout is set.");
  }
  if (UBSocketPort != 0) {
    tty->print_cr("UBSocket is disabled, but control port is set.");
  }
  if (UBSocketMemorySize != UB_SOCKET_DEFAULT_BLOCK_SIZE * UB_SOCKET_DEFAULT_BLOCK_COUNT) {
    tty->print_cr("UBSocket is disabled, but memory size is set.");
  }
}

void *UBSocketManager::get_free_memory(uint64_t len, uint64_t *offset, uint64_t *size,
                                       uint32_t* start_blk, uint32_t* blk_count_ptr) {
  if (len == 0) { return NULL; }
  uint64_t block_size = _blk_size;
  uint64_t capacity_bytes = block_size * _blk_count;
  uint64_t max_payload = capacity_bytes - sizeof(UBSocketBlkMeta);
  if (len > max_payload) {
    errno = ENOMEM;
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "UBSocket buffer request too large: payload=" UINT64_FORMAT " bytes, "
           "max_payload=" UINT64_FORMAT " bytes, block=" UINT64_FORMAT " bytes, "
           "total=" UINT32_FORMAT " blocks (" UINT64_FORMAT " bytes), err=%d\n",
           len, max_payload, block_size, _blk_count, capacity_bytes, errno);
    return NULL;
  }
  uint32_t blk_need = (uint32_t)((len + sizeof(UBSocketBlkMeta) +
                                  block_size - 1) / block_size);
  uint64_t request_bytes = (uint64_t)blk_need * block_size;
  uint32_t start = 0;
  if (!UBSocketBlkBitmap::alloc(blk_need, &start)) {
    UnreadMsgTable::process_unread_msgs();
    if (!UBSocketBlkBitmap::alloc(blk_need, &start)) {
      int alloc_errno = errno;
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "UBSocket buffer allocation failed: payload=" UINT64_FORMAT " bytes, "
             "meta=" SIZE_FORMAT " bytes, block=" UINT64_FORMAT " bytes, "
             "need=" UINT32_FORMAT " contiguous blocks (" UINT64_FORMAT " bytes), "
             "total=" UINT32_FORMAT " blocks (" UINT64_FORMAT " bytes), "
             "reason=no_free_range, err=%d\n",
             len, sizeof(UBSocketBlkMeta), block_size,
             blk_need, request_bytes, _blk_count, capacity_bytes, alloc_errno);
      errno = alloc_errno;
      return NULL;
    }
  }
  uint64_t block_offset = (uint64_t)_blk_size * start;
  uintptr_t addr = uintptr_t(shared_memory_addr) + (uintptr_t)block_offset;
  uintptr_t data_addr = addr + sizeof(UBSocketBlkMeta);
  *offset = block_offset + sizeof(UBSocketBlkMeta);
  *size = (uint64_t)_blk_size * blk_need - sizeof(UBSocketBlkMeta);
  if (start_blk != NULL) *start_blk = start;
  if (blk_count_ptr != NULL) *blk_count_ptr = blk_need;
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
         "get_free_memory success payload=" UINT64_FORMAT " off=" UINT64_FORMAT
         " size=" UINT64_FORMAT " start_blk=%u blk_count=%u\n",
         len, *offset, *size, start, blk_need);
  return (void *)data_addr;
}

long UBSocketManager::write_data(void *buf, int socket_fd, long len) {
  if (!UseUBSocket || !_initialized || len <= 0) {
    return 0;
  }

  if (SocketDataInfoTable::is_fallback_draining(socket_fd)) {
    ssize_t sent_res = ensure_fallback_sent(socket_fd, "write_fallback");
    if (sent_res <= 0) { return sent_res; }
    long tcp_write = (long)UBSocketIO::write(socket_fd, buf, (size_t)len);
    if (tcp_write > 0) {
      // try to unregister if fallback is drained after write
      unregister_if_fallback_drained(socket_fd);
    }
    UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
           "fd=%d write_data fallback_tcp requested=%ld written=%ld\n",
           socket_fd, len, tcp_write);
    return tcp_write;
  }

  UBSocketInfoList* info_list = SocketDataInfoTable::pin_list(socket_fd);
  if (info_list == NULL) { return -1; }
  UnreadMsgList* unread_list = UnreadMsgTable::pin_list(socket_fd);
  if (unread_list == NULL) {
    SocketDataInfoTable::unpin_list(info_list);
    UB_LOG(UB_SOCKET, UB_LOG_ERROR,
           "fd=%d write failed: datainfo exists but unreadlist unavailable\n", socket_fd);
    return -1;
  }

  ResourceMark rm;
  const char* mem_name = shared_memory_name->as_C_string();
  GrowableArray<UBSocketDataFrame> frames(UB_INIT_ARRAY_CAP, true, mtInternal);
  GrowableArray<UBSocketBlkItem> blk_items(UB_INIT_ARRAY_CAP, true, mtInternal);
  long total_write = 0;
  while (total_write < len) {
    uint64_t ub_offset;
    uint64_t ub_size;
    uint32_t start_blk = 0;
    uint32_t blk_count = 0;
    uint64_t remaining = (uint64_t)(len - total_write);
    void *socket_addr = get_free_memory(remaining, &ub_offset, &ub_size,
                                        &start_blk, &blk_count);
    if (socket_addr == NULL) {
      release_untracked_blocks(&blk_items);
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "fd=%d UBSocket send buffer allocation failed for payload=%ld bytes "
             "err=%d; switching to TCP fallback\n", socket_fd, len, errno);
      if (!SocketDataInfoTable::request_fallback(socket_fd, "alloc_failed")) {
        UnreadMsgTable::unpin_list(unread_list);
        SocketDataInfoTable::unpin_list(info_list);
        return -1;
      }
      ssize_t sent_res = ensure_fallback_sent(socket_fd, "alloc_failed");
      if (sent_res <= 0) {
        UnreadMsgTable::unpin_list(unread_list);
        SocketDataInfoTable::unpin_list(info_list);
        return sent_res;
      }
      long tcp_write = (long)UBSocketIO::write(socket_fd, buf, (size_t)len);
      UnreadMsgTable::unpin_list(unread_list);
      SocketDataInfoTable::unpin_list(info_list);
      if (tcp_write > 0) {
        unregister_if_fallback_drained(socket_fd);
      }
      UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
             "fd=%d write_data fallback_tcp requested=%ld written=%ld reason=alloc_failed\n",
             socket_fd, len, tcp_write);
      return tcp_write;
    }

    uint64_t write_size = MIN2(ub_size, remaining);
    uintptr_t data_addr = (uintptr_t)socket_addr;
    uintptr_t meta_addr = data_addr - sizeof(UBSocketBlkMeta);
    mark_send((void*)meta_addr, socket_fd);
    memcpy(socket_addr, (void*)((uintptr_t)buf + total_write), (size_t)write_size);
    frames.append(ub_socket_data_frame(UB_SOCKET_DATA_DESCRIPTOR, mem_name,
                                       ub_offset, write_size));
    UBSocketBlkItem item = {meta_addr, start_blk, blk_count};
    blk_items.append(item);
    total_write += (long)write_size;
  }

  size_t expected_bytes = (size_t)frames.length() * UB_SOCKET_DATA_FRAME_SIZE;
  size_t bytes_sent = 0;
  ssize_t send_res = ub_socket_data_send(socket_fd, frames.adr_at(0),
                                         (size_t)frames.length(), &bytes_sent);
  if (send_res != (ssize_t)expected_bytes) {
    int send_errno = errno;
    int complete_frames = (int)(bytes_sent / UB_SOCKET_DATA_FRAME_SIZE);
    bool partial_frame = (bytes_sent % UB_SOCKET_DATA_FRAME_SIZE) != 0;
    long complete_write = 0;
    for (int i = 0; i < complete_frames; i++) {
      complete_write += (long)frames.at(i).length;
    }
    // Only complete descriptors are visible to the peer; track them before
    // releasing blocks whose descriptors never reached a frame boundary.
    UnreadMsgTable::add_pinned_msgs(unread_list, &blk_items, complete_frames);
    release_untracked_blocks(&blk_items, complete_frames);
    UB_LOG(UB_SOCKET, UB_LOG_ERROR,
           "fd=%d send descriptor batch failed len=%ld frames=%d rc=%ld "
           "bytes_sent=" SIZE_FORMAT " complete_frames=%d partial=%d err=%d\n",
           socket_fd, len, frames.length(), (long)send_res,
           bytes_sent, complete_frames, partial_frame ? 1 : 0, send_errno);
    UnreadMsgTable::unpin_list(unread_list);
    SocketDataInfoTable::unpin_list(info_list);
    if (partial_frame) {
      // A partial descriptor corrupts the stream; do not fallback on this fd.
      errno = send_errno != 0 ? send_errno : EIO;
      return -1;
    }
    if (complete_write > 0) { return complete_write; }
    errno = send_errno;
    return (long)send_res;
  }

  UnreadMsgTable::add_pinned_msgs(unread_list, &blk_items, blk_items.length());
  UnreadMsgTable::unpin_list(unread_list);
  SocketDataInfoTable::unpin_list(info_list);
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
         "fd=%d write_data success requested=%ld written=%ld frames=%d\n",
         socket_fd, len, total_write, frames.length());
  return total_write;
}

ssize_t UBSocketManager::send_heartbeat(int socket_fd) {
  UBSocketDataFrame frame = ub_socket_data_frame(UB_SOCKET_DATA_HEARTBEAT, "", 0, 0);
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d send HEARTBEAT frame\n", socket_fd);
  return ub_socket_data_send(socket_fd, &frame, 1);
}

ssize_t UBSocketManager::ensure_fallback_sent(int socket_fd, const char* reason) {
  if (!SocketDataInfoTable::begin_fallback_mark_send(socket_fd)) { return 1; }
  UBSocketDataFrame frame = ub_socket_data_frame(UB_SOCKET_DATA_FALLBACK, "", 0, 0);
  ssize_t nsend = ub_socket_data_send(socket_fd, &frame, 1);
  if (nsend != UB_SOCKET_DATA_FRAME_WIRE_SIZE) {
    SocketDataInfoTable::abort_fallback_mark_send(socket_fd);
    UB_LOG(UB_SOCKET, UB_LOG_ERROR,
           "fd=%d send DATA_FALLBACK frame failed reason=%s rc=%ld err=%d\n",
           socket_fd, reason == NULL ? "<none>" : reason, (long)nsend, errno);
    return nsend;
  }
  SocketDataInfoTable::complete_fallback_mark_send(socket_fd);
  UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d send DATA_FALLBACK frame mark reason=%s\n",
         socket_fd, reason == NULL ? "<none>" : reason);
  return nsend;
}

bool UBSocketManager::unregister_if_fallback_drained(int socket_fd) {
  if (!SocketDataInfoTable::fallback_drained(socket_fd)) { return false; }
  if (UnreadMsgTable::has_pending_msg(socket_fd)) { return false; }
  UB_LOG(UB_SOCKET, UB_LOG_WARNING,
         "fd=%d fallback drained, unregister UBSocket and continue as TCP\n",
         socket_fd);
  return unregister_fd(socket_fd);
}

static long ub_socket_handle_fallback_frame(int socket_fd,
                                            const char* raw_tail,
                                            size_t raw_tail_len) {
  if (!SocketDataInfoTable::receive_fallback_mark(socket_fd, raw_tail, raw_tail_len)) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR,
           "fd=%d receive DATA_FALLBACK mark failed tail=" SIZE_FORMAT " err=%d\n",
           socket_fd, raw_tail_len, errno);
    return -1;
  }
  UB_LOG(UB_SOCKET, UB_LOG_WARNING,
         "fd=%d recv DATA_FALLBACK frame mark tail=" SIZE_FORMAT "\n",
         socket_fd, raw_tail_len);
  return 0;
}

static long ub_socket_handle_data_frame(int socket_fd, const UBSocketDataFrame& frame) {
  if (frame.kind == UB_SOCKET_DATA_HEARTBEAT) {
    UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d recv HEARTBEAT frame\n", socket_fd);
    return 0;
  }
  if (frame.kind == UB_SOCKET_DATA_FALLBACK) {
    return 0;
  }
  return SocketDataInfoTable::append_range(socket_fd, frame.mem_name,
                                           frame.offset, frame.length);
}

long UBSocketManager::parse_msg(int socket_fd, const char* ub_msg, size_t ub_msg_len) {
  if (!UseUBSocket || !_initialized || ub_msg == NULL || ub_msg_len == 0) {
    return 0;
  }

  long total_parse_size = 0;
  size_t consumed = 0;
  char frame_buf[UB_SOCKET_DATA_FRAME_WIRE_SIZE];
  size_t residue_len = 0;
  if (!SocketDataInfoTable::take_frame_residue(socket_fd, frame_buf, sizeof(frame_buf),
                                               &residue_len)) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d residue take failed: %s\n",
           socket_fd, strerror(errno));
    return -1;
  }

  if (residue_len > 0) {
    size_t need = UB_SOCKET_DATA_FRAME_SIZE - residue_len;
    if (ub_msg_len < need) {
      memcpy(frame_buf + residue_len, ub_msg, ub_msg_len);
      if (!SocketDataInfoTable::store_frame_residue(socket_fd, frame_buf,
                                                    residue_len + ub_msg_len)) {
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
    UBSocketDataFrame frame;
    if (!ub_socket_data_parse(frame_buf, &frame)) {
      UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d recv data frame invalid: %s\n",
             socket_fd, strerror(errno));
      return -1;
    }
    if (frame.kind == UB_SOCKET_DATA_FALLBACK) {
      return ub_socket_handle_fallback_frame(socket_fd, ub_msg + consumed,
                                             ub_msg_len - consumed);
    }
    long parse_size = ub_socket_handle_data_frame(socket_fd, frame);
    if (parse_size < 0) {
      return -1;
    }
    total_parse_size += parse_size;
  }

  while (ub_msg_len - consumed >= UB_SOCKET_DATA_FRAME_SIZE) {
    UBSocketDataFrame frame;
    if (!ub_socket_data_parse(ub_msg + consumed, &frame)) {
      UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d recv data frame invalid: %s\n",
             socket_fd, strerror(errno));
      return -1;
    }
    if (frame.kind == UB_SOCKET_DATA_FALLBACK) {
      consumed += UB_SOCKET_DATA_FRAME_SIZE;
      return ub_socket_handle_fallback_frame(socket_fd, ub_msg + consumed,
                                             ub_msg_len - consumed);
    }
    long parse_size = ub_socket_handle_data_frame(socket_fd, frame);
    if (parse_size < 0) {
      return -1;
    }
    total_parse_size += parse_size;
    consumed += UB_SOCKET_DATA_FRAME_SIZE;
  }

  if (ub_msg_len > consumed) {
    size_t remain = ub_msg_len - consumed;
    if (!SocketDataInfoTable::store_frame_residue(socket_fd, ub_msg + consumed, remain)) {
      UB_LOG(UB_SOCKET, UB_LOG_ERROR,
             "fd=%d residue store failed len=" SIZE_FORMAT "\n",
             socket_fd, remain);
      return -1;
    }
    UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
           "fd=%d parse_msg stored tail residue=" SIZE_FORMAT
           " consumed=" SIZE_FORMAT "\n",
           socket_fd, remain, consumed);
  }

  if (total_parse_size == 0 && consumed == 0) {
    return 0;
  }
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
         "fd=%d parse_msg result parsed=%ld consumed=" SIZE_FORMAT
         " input=" SIZE_FORMAT "\n",
         socket_fd, total_parse_size, consumed, ub_msg_len);
  return total_parse_size;
}

long UBSocketManager::read_data(void *buf, int socket_fd, long len) {
  if (!UseUBSocket || !_initialized) return 0;
  long nread = SocketDataInfoTable::read_data(socket_fd, buf, len);
  if (nread >= 0) {
    unregister_if_fallback_drained(socket_fd);
  }
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG,
         "fd=%d read_data requested=%ld read=%ld\n", socket_fd, len, nread);
  return nread;
}

bool UBSocketManager::register_fd(int socket_fd, bool is_server) {
  if (!UseUBSocket || !_initialized) return false;
  long start_time = os::javaTimeNanos();

  if (has_registered(socket_fd)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d register skipped: already registered\n", socket_fd);
    return true;
  }

  UBSocketAttach socket_attach(socket_fd, is_server, shared_memory_name, memory_size());
  bool attach_ok = socket_attach.do_attach();
  if (!attach_ok) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d register attach failed role=%s fallback=tcp\n",
           socket_fd, is_server ? "server" : "client");
    if (has_registered(socket_fd)) { unregister_fd(socket_fd); }
    return false;
  }

  long cost_time = os::javaTimeNanos() - start_time;
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d register success role=%s cost_ns=%ld\n",
         socket_fd, is_server ? "server" : "client", cost_time);
  return true;
}

bool UBSocketManager::unregister_fd(int socket_fd) {
  if (!UseUBSocket || !_initialized) return false;
  if (!has_registered(socket_fd)) {
    return false;
  }
  long start_time = os::javaTimeNanos();
  bool unbound = UBSocketMemMapping::unbind(socket_fd);
  if (!unbound) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d unregister failed: no mapping bound\n", socket_fd);
    return false;
  }
  long cost_time = os::javaTimeNanos() - start_time;
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d unregister cost_ns=%ld\n", socket_fd, cost_time);
  return true;
}

bool UBSocketManager::has_registered(int socket_fd) {
  if (!UseUBSocket || !_initialized) return false;
  return SocketDataInfoTable::contains(socket_fd);
}

bool UBSocketManager::wait_fd_ready(int socket_fd) {
  if (!UseUBSocket || !_initialized) { return false; }
  return SocketDataInfoTable::ready_for_ub_io(socket_fd);
}
