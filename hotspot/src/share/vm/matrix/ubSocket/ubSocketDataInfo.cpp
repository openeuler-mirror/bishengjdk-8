/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocket.hpp"
#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/ubSocket/ubSocketMemMapping.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"

#include "matrix/ubSocket/ubSocketDataInfo.hpp"

Monitor* SocketDataInfoTable::_info_table_lock = NULL;
PtrTable<int, UBSocketInfoList*, mtInternal> SocketDataInfoTable::_table(NULL);

static void ub_socket_profile_read_memcpy_bucket(uint64_t elapsed_ns,
                                                 size_t bytes) {
  if (elapsed_ns < 1000) {
    UBSocketProfiler::count(UB_PROF_UB_READ_MEMCPY_LT_1US, bytes);
  } else if (elapsed_ns < 2000) {
    UBSocketProfiler::count(UB_PROF_UB_READ_MEMCPY_1_2US, bytes);
  } else if (elapsed_ns < 5000) {
    UBSocketProfiler::count(UB_PROF_UB_READ_MEMCPY_2_5US, bytes);
  } else if (elapsed_ns < 10000) {
    UBSocketProfiler::count(UB_PROF_UB_READ_MEMCPY_5_10US, bytes);
  } else if (elapsed_ns < 20000) {
    UBSocketProfiler::count(UB_PROF_UB_READ_MEMCPY_10_20US, bytes);
  } else if (elapsed_ns < 30000) {
    UBSocketProfiler::count(UB_PROF_UB_READ_MEMCPY_20_30US, bytes);
  } else {
    UBSocketProfiler::count(UB_PROF_UB_READ_MEMCPY_GE_30US, bytes);
  }
}

/************************UBSocketFallbackState**********************/

bool UBSocketFallbackState::append_tcp_tail(const char* src, size_t len) {
  if (len == 0) { return true; }
  if (_tcp_pos != 0 && _tcp_pos < _tcp_len) {
    size_t remain = _tcp_len - _tcp_pos;
    memmove(_tcp_buf, _tcp_buf + _tcp_pos, remain);
    _tcp_len = remain;
    _tcp_pos = 0;
  } else if (_tcp_pos >= _tcp_len) {
    _tcp_len = 0;
    _tcp_pos = 0;
  }
  if (_tcp_len + len > sizeof(_tcp_buf)) {
    errno = EMSGSIZE;
    return false;
  }
  memcpy(_tcp_buf + _tcp_len, src, len);
  _tcp_len += len;
  return true;
}

size_t UBSocketFallbackState::read_tcp_tail(void* dst, size_t len) {
  if (_tcp_pos >= _tcp_len) {
    _tcp_pos = 0;
    _tcp_len = 0;
    return 0;
  }
  size_t remain = _tcp_len - _tcp_pos;
  size_t ncopy = MIN2(remain, len);
  memcpy(dst, _tcp_buf + _tcp_pos, ncopy);
  _tcp_pos += ncopy;
  if (_tcp_pos >= _tcp_len) {
    _tcp_pos = 0;
    _tcp_len = 0;
  }
  return ncopy;
}

/**************************UBSocketInfoList*************************/

bool UBSocketInfoList::unpin() {
  if (_active_count <= 0) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d unpin without active pin\n", _socket_fd);
    return false;
  }
  _active_count--;
  return _closing && _active_count == 0;
}

UBSocketInfoList::SocketListNode* UBSocketInfoList::alloc_node(size_t offset,
                                                               size_t size) {
  SocketListNode* node = _free_nodes;
  if (node != NULL) {
    _free_nodes = node->next;
    node->offset = offset;
    node->size = size;
    node->next = NULL;
    return node;
  }
  return new SocketListNode(offset, size, NULL);
}

void UBSocketInfoList::append(size_t offset, size_t size) {
  SocketListNode* newNode = alloc_node(offset, size);
  if (_head == NULL) {
    _head = _tail = _cursor = newNode;
  } else {
    _tail->next = newNode;
    _tail = newNode;
  }
}

bool UBSocketInfoList::append_ranges(const UBSocketDataFrame* frames,
                                     int count,
                                     long* total_len) {
  *total_len = 0;
  if (count <= 0) {
    return true;
  }
  uint64_t start_ns = UBSocketProfiler::start(UB_PROF_APPEND_UNREAD_TOTAL);
  uint64_t mapping_size = _mem_mapping->size();
  uintptr_t mapping_addr = (uintptr_t)_mem_mapping->addr();
  int appended = 0;
  for (int i = 0; i < count; i++) {
    uint64_t off = frames[i].offset;
    uint64_t len = frames[i].length;
    if (off < sizeof(UBSocketBlkMeta) || off > mapping_size ||
        len == 0 || len > mapping_size - off || len > (uint64_t)LONG_MAX) {
      errno = ERANGE;
      UB_LOG(UB_SOCKET, UB_LOG_ERROR,
             "fd=%d data frame range invalid off=" UINT64_FORMAT
             " len=" UINT64_FORMAT " size=" UINT64_FORMAT "\n",
             _socket_fd, off, len, mapping_size);
      return false;
    }
    size_t range_off = (size_t)off;
    size_t range_len = (size_t)len;
    if (!UBSocketManager::mark_recv(mapping_addr + range_off)) {
      errno = EBADMSG;
      return false;
    }
    append(range_off, range_len);
    *total_len += (long)range_len;
    appended++;
  }
  if (appended > 0) {
    uint64_t elapsed_ns = start_ns == 0 ? 0 :
        (uint64_t)os::javaTimeNanos() - start_ns;
    UBSocketProfiler::record(UB_PROF_APPEND_UNREAD_TOTAL, elapsed_ns,
                             (uint64_t)*total_len, (uint64_t)appended);
  }
  return true;
}

long UBSocketInfoList::read_data(void* dst, size_t len) {
  if (len == 0) return 0;

  size_t total = 0;
  char* out = (char*)dst;
  while (total < len && _cursor != NULL) {
    size_t cursor_remain = _cursor->size - _cur_loc;
    size_t ncopy = MIN2(len - total, cursor_remain);
    uintptr_t read_addr = (uintptr_t)_mem_mapping->addr() + _cursor->offset + _cur_loc;

    uint64_t memcpy_start = UBSocketProfiler::start(UB_PROF_UB_READ_MEMCPY);
    memcpy(out + total, (void*)read_addr, ncopy);
    uint64_t memcpy_ns = UBSocketProfiler::end(UB_PROF_UB_READ_MEMCPY,
                                               memcpy_start, ncopy);
    ub_socket_profile_read_memcpy_bucket(memcpy_ns, ncopy);
    total += ncopy;

    size_t next_loc = _cur_loc + ncopy;
    if (next_loc == _cursor->size) {
      if (!finish_current_range()) {
        errno = EBADMSG;
        return -1;
      }
    } else {
      _cur_loc = next_loc;
    }
  }

  if (total > 0) { return (long)total; }
  return (long)_fallback.read_tcp_tail(dst, len);
}

bool UBSocketInfoList::append_fallback_tail(const char* src, size_t len) {
  return _fallback.append_tcp_tail(src, len);
}

bool UBSocketInfoList::take_frame_residue(char* dst, size_t dst_len, size_t* len) {
  *len = 0;
  if (_frame_residue_len == 0) { return true; }
  if (_frame_residue_len > dst_len) {
    errno = EMSGSIZE;
    return false;
  }
  memcpy(dst, _frame_residue_buf, _frame_residue_len);
  *len = _frame_residue_len;
  _frame_residue_len = 0;
  return true;
}

bool UBSocketInfoList::store_frame_residue(const char* src, size_t len) {
  if (len > sizeof(_frame_residue_buf)) {
    errno = EMSGSIZE;
    return false;
  }
  if (len > 0) { memcpy(_frame_residue_buf, src, len); }
  _frame_residue_len = len;
  return true;
}

bool UBSocketInfoList::finish_current_range() {
  uintptr_t block_addr = (uintptr_t)_mem_mapping->addr() + _cursor->offset;
  if (!UBSocketManager::mark_read(block_addr)) {
    return false;
  }

  SocketListNode* next = _cursor->next;
  if (next != NULL) {
    _cursor = next;
    recycle_nodes(_head, _cursor);
    _head = _cursor;
  } else {
    recycle_nodes(_head, NULL);
    _head = _tail = _cursor = NULL;
  }
  _cur_loc = 0;
  return true;
}

int UBSocketInfoList::recycle_nodes(SocketListNode* start, SocketListNode* end) {
  int recycle_count = 0;
  while (start != end && start != NULL) {
    SocketListNode* next = start->next;
    start->next = _free_nodes;
    _free_nodes = start;
    start = next;
    recycle_count++;
  }
  return recycle_count;
}

int UBSocketInfoList::delete_nodes(SocketListNode* start, SocketListNode* end) {
  int delete_count = 0;
  while (start != end && start != NULL) {
    SocketListNode* next = start->next;
    delete start;
    start = next;
    delete_count++;
  }
  return delete_count;
}

void SocketDataInfoTable::init() {
  _info_table_lock = new Monitor(Mutex::leaf, "SocketDataInfoTable_lock");
}

bool SocketDataInfoTable::publish(int fd, UBSocketInfoList* info) {
  MutexLocker locker(_info_table_lock);
  if (_table.get(fd) != NULL) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d publish skipped: already exists\n", fd);
    return false;
  }
  _table.add(fd, info);
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d publish data info\n", fd);
  return true;
}

bool SocketDataInfoTable::contains(int fd) {
  MutexLocker locker(_info_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  return info != NULL && !info->closing();
}

UBSocketInfoList* SocketDataInfoTable::pin_list_locked(int fd) {
  UBSocketInfoList* info = _table.get(fd);
  if (info == NULL || info->closing()) {
    return NULL;
  }
  info->pin();
  return info;
}

void SocketDataInfoTable::unpin_list(UBSocketInfoList* info) {
  MonitorLockerEx locker(_info_table_lock, Mutex::_no_safepoint_check_flag);
  if (info->unpin()) { locker.notify_all(); }
}

UBSocketInfoList* SocketDataInfoTable::pin_list(int fd) {
  MonitorLockerEx locker(_info_table_lock, Mutex::_no_safepoint_check_flag);
  return pin_list_locked(fd);
}

long SocketDataInfoTable::append_ranges(int fd, const UBSocketDataFrame* frames, int count) {
  if (count <= 0) { return 0; }
  UBSocketInfoList* info = NULL;
  {
    MonitorLockerEx locker(_info_table_lock, Mutex::_no_safepoint_check_flag);
    info = pin_list_locked(fd);
    if (info == NULL) { return -1; }
  }
  long total_len = 0;
  bool ok = info->append_ranges(frames, count, &total_len);
  unpin_list(info);
  return ok ? total_len : -1;
}

long SocketDataInfoTable::read_data(int fd, void* dst, size_t len) {
  UBSocketInfoList* info = NULL;
  {
    MonitorLockerEx locker(_info_table_lock, Mutex::_no_safepoint_check_flag);
    info = pin_list_locked(fd);
    if (info == NULL) { return -1; }
  }
  long result = info->read_data(dst, len);
  unpin_list(info);
  return result;
}

bool SocketDataInfoTable::is_fallback_draining(int fd) {
  MutexLocker locker(_info_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  return info != NULL && !info->closing() && info->fallback_draining();
}

bool SocketDataInfoTable::can_send_frame(int fd) {
  MutexLocker locker(_info_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  return info != NULL && !info->closing() && !info->fallback_draining();
}

bool SocketDataInfoTable::request_fallback(int fd, const char* reason) {
  MutexLocker locker(_info_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  if (info == NULL || info->closing()) { return false; }
  if (!info->fallback_draining()) {
    info->request_fallback();
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "fd=%d fallback draining requested reason=%s\n",
           fd, reason == NULL ? "<none>" : reason);
  }
  return true;
}

bool SocketDataInfoTable::begin_fallback_mark_send(int fd) {
  MutexLocker locker(_info_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  return info != NULL && !info->closing() && info->begin_fallback_mark_send();
}

void SocketDataInfoTable::complete_fallback_mark_send(int fd) {
  MutexLocker locker(_info_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  if (info == NULL || info->closing()) { return; }
  info->complete_fallback_mark_send();
}

void SocketDataInfoTable::abort_fallback_mark_send(int fd) {
  MutexLocker locker(_info_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  if (info == NULL || info->closing()) { return; }
  info->abort_fallback_mark_send();
}

bool SocketDataInfoTable::receive_fallback_mark(int fd, const char* src, size_t len) {
  UBSocketInfoList* info = NULL;
  {
    MonitorLockerEx locker(_info_table_lock, Mutex::_no_safepoint_check_flag);
    info = pin_list_locked(fd);
    if (info == NULL) { return false; }
    info->receive_fallback_mark();
  }
  bool result = info->append_fallback_tail(src, len);
  UB_LOG(UB_SOCKET, UB_LOG_INFO,
         "fd=%d fallback mark received tail=" SIZE_FORMAT " append=%d\n",
         fd, len, result ? 1 : 0);
  unpin_list(info);
  return result;
}

bool SocketDataInfoTable::fallback_drained(int fd) {
  MutexLocker locker(_info_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  return info != NULL && !info->closing() && info->fallback_drained();
}

bool SocketDataInfoTable::has_pending_data(int fd) {
  MutexLocker locker(_info_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  return info != NULL && !info->closing() && info->has_pending_data();
}

bool SocketDataInfoTable::ready_for_ub_io(int fd) {
  MutexLocker locker(_info_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  return info != NULL && !info->closing() && info->ready_for_ub_io();
}

bool SocketDataInfoTable::take_frame_residue(int fd, char* dst, size_t dst_len, size_t* len) {
  UBSocketInfoList* info = NULL;
  {
    MonitorLockerEx locker(_info_table_lock, Mutex::_no_safepoint_check_flag);
    info = pin_list_locked(fd);
    if (info == NULL) { return false; }
  }
  bool result = info->take_frame_residue(dst, dst_len, len);
  unpin_list(info);
  return result;
}

bool SocketDataInfoTable::store_frame_residue(int fd, const char* src, size_t len) {
  UBSocketInfoList* info = NULL;
  {
    MonitorLockerEx locker(_info_table_lock, Mutex::_no_safepoint_check_flag);
    info = pin_list_locked(fd);
    if (info == NULL) { return false; }
  }
  bool result = info->store_frame_residue(src, len);
  unpin_list(info);
  return result;
}

UBSocketInfoList* SocketDataInfoTable::detach(int fd) {
  MonitorLockerEx locker(_info_table_lock, Mutex::_no_safepoint_check_flag);
  UBSocketInfoList* info = _table.get(fd);
  if (info == NULL) { return NULL; }
  info->set_closing();
  while (info->has_active()) {
    locker.wait(Mutex::_no_safepoint_check_flag);
  }
  _table.remove(fd);
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d detach data info\n", fd);
  return info;
}

int SocketDataInfoTable::unregister_abnormal_fds() {
  int success = 0;
  GrowableArray<int> fds(UB_INIT_ARRAY_CAP, true, mtInternal);
  {
    MutexLocker locker(_info_table_lock);
    _table.begin_iteration();
    UBSocketInfoList* info = _table.next();
    while (info != NULL) {
      fds.append(_table.get_cur_iter_key());
      info = _table.next();
    }
  }
  for (int i = 0; i < fds.length(); i++) {
    if (UBSocketManager::unregister_fd(fds.at(i))) {
      success++;
    }
  }
  if (success != fds.length()) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "unregister abnormal fds success=%d total=%d\n",
           success, fds.length());
  }
  return success;
}
