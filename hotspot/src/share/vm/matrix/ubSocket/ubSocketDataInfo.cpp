/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

#include <errno.h>
#include <string.h>

#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocket.hpp"
#include "matrix/ubSocket/ubSocketMemMapping.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "runtime/globals.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "utilities/growableArray.hpp"
#include "matrix/ubSocket/ubSocketDataInfo.hpp"

Monitor* UBSocketConnectionTable::_table_lock = NULL;
PtrTable<int, UBSocketConnection*, mtInternal> UBSocketConnectionTable::_table(NULL);
bool UBSocketConnectionTable::_shutting_down = false;

UBSocketConnection::UBSocketConnection(int fd, UBSocketMemMapping* mapping,
                                       uint32_t local_ring_slot,
                                       uint64_t local_ring_size,
                                       uint64_t remote_ring_offset,
                                       uint64_t remote_ring_size)
    : _socket_fd(fd),
      _remote_mapping(mapping),
      _local_ring_slot(local_ring_slot),
      _lock(new Monitor(Mutex::leaf, "UBSocketConnection_lock")),
      _rx_ring(UBSocketRingSlots::slot_addr(local_ring_slot), local_ring_size),
      _tx_ring((char*)mapping->addr() + remote_ring_offset, remote_ring_size),
      _tx_wakeup_sending(false),
      _tx_wakeup_pending(false),
      _ring_error(false),
      _rx_closed(false),
      _tx_closed(false),
      _tx_last_wakeup_offset(0),
      _rx_ready(false),
      _rx_read_offset(0),
      _tx_write_offset(0),
      _closing(false),
      _active_count(0),
      _frame_residue_len(0) {}

UBSocketConnection::~UBSocketConnection() {
  if (_local_ring_slot != UB_SOCKET_RING_INVALID_SLOT) {
    UBSocketRingSlots::release(_local_ring_slot);
    _local_ring_slot = UB_SOCKET_RING_INVALID_SLOT;
  }
  if (_remote_mapping != NULL) {
    UBSocketMemMapping::release_mapping(_remote_mapping);
    _remote_mapping = NULL;
  }
  delete _lock;
}

bool UBSocketConnection::unpin() {
  if (_active_count <= 0) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d unpin without active pin\n", _socket_fd);
    return false;
  }
  _active_count--;
  return _closing && _active_count == 0;
}

void UBSocketConnection::mark_error_locked() {
  _ring_error = true;
  _rx_ready = false;
  _tx_wakeup_sending = false;
  _tx_wakeup_pending = false;
}

void UBSocketConnection::mark_error() {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  mark_error_locked();
}

bool UBSocketConnection::ready() {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  return !_ring_error;
}

bool UBSocketConnection::refresh_rx_ready() {
  if (_ring_error) {
    _rx_ready = false;
    return false;
  }
  bool valid_window = true;
  bool is_empty = _rx_ring.empty(_rx_read_offset, &valid_window);
  if (!valid_window) {
    mark_error_locked();
    return false;
  }
  _rx_ready = !is_empty;
  return _rx_ready;
}

long UBSocketConnection::read_data(void* dst, size_t len) {
  if (len == 0) { return 0; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (_ring_error) {
    errno = EINVAL;
    return -1;
  }
  uint64_t new_read_offset = _rx_read_offset;
  long result = (long)_rx_ring.read_available(dst, len, _rx_read_offset,
                                              &new_read_offset);
  if (result > 0) {
    _rx_read_offset = new_read_offset;
    _tx_ring.publish_peer_read_ack_offset(_rx_read_offset);
  }
  if (result < 0 && errno == EINVAL) {
    mark_error_locked();
  } else {
    refresh_rx_ready();
  }
  if (result == 0 && _rx_closed) {
    errno = ESHUTDOWN;
    return -1;
  }
  return result;
}

long UBSocketConnection::write_data(const void* src, size_t len, bool* need_wakeup) {
  *need_wakeup = false;
  if (len == 0) { return 0; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (_ring_error) {
    errno = EINVAL;
    return -1;
  }
  if (_tx_closed) {
    errno = ESHUTDOWN;
    return -1;
  }
  uint64_t old_write_offset = _tx_write_offset;
  uint64_t peer_read_ack_offset = _rx_ring.load_peer_read_ack_offset();
  uint64_t new_write_offset = 0;
  long result = (long)_tx_ring.write(src, len, _tx_write_offset,
                                     peer_read_ack_offset, &new_write_offset);
  if (result < 0 && errno == EINVAL) {
    mark_error_locked();
  }
  if (result <= 0) { return result; }
  _tx_write_offset = new_write_offset;

  if (_tx_wakeup_sending) {
    UBSocketProfiler::count(UB_PROF_WAKEUP_SKIP_SENDING, (uint64_t)result);
    return result;
  }

  bool should_wakeup = false;
  if (_tx_wakeup_pending) {
    UBSocketProfiler::count(UB_PROF_WAKEUP_REQUEST_RETRY, (uint64_t)result);
    should_wakeup = true;
  } else if (peer_read_ack_offset == old_write_offset ||
             (new_write_offset - _tx_last_wakeup_offset) >=
                 (uint64_t)UBSocketWakeupThresholdBytes) {
    should_wakeup = true;
    if (peer_read_ack_offset != old_write_offset) {
      UBSocketProfiler::count(UB_PROF_WAKEUP_REQUEST_THRESHOLD,
                              (uint64_t)result);
    }
  } else {
    uint64_t latest_peer_read_ack_offset = _rx_ring.load_peer_read_ack_offset();
    if (latest_peer_read_ack_offset >= old_write_offset &&
        latest_peer_read_ack_offset <= new_write_offset) {
      should_wakeup = true;
    } else {
      UBSocketProfiler::count(UB_PROF_WAKEUP_SKIP_NONEMPTY, (uint64_t)result);
    }
  }

  if (should_wakeup) {
    UBSocketProfiler::count(UB_PROF_WAKEUP_REQUEST_TOTAL, (uint64_t)result);
    *need_wakeup = true;
    _tx_wakeup_sending = true;
  }
  return result;
}

void UBSocketConnection::end_tx_wakeup() {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  _tx_last_wakeup_offset = _tx_write_offset;
  _tx_wakeup_pending = false;
  _tx_wakeup_sending = false;
}

void UBSocketConnection::cancel_tx_wakeup() {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  _tx_wakeup_pending = true;
  _tx_wakeup_sending = false;
}

bool UBSocketConnection::mark_rx_wakeup() {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  return refresh_rx_ready();
}

void UBSocketConnection::mark_control_closed() {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  _rx_closed = true;
}

void UBSocketConnection::mark_peer_closed() {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  _rx_closed = true;
  _tx_closed = true;
  _tx_wakeup_sending = false;
  _tx_wakeup_pending = false;
}

bool UBSocketConnection::has_pending_data() {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (_ring_error) { return false; }
  if (_rx_ready) { return true; }
  return refresh_rx_ready() || _rx_closed;
}

bool UBSocketConnection::take_frame_residue(char* dst, size_t dst_len, size_t* len) {
  *len = 0;
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
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

bool UBSocketConnection::store_frame_residue(const char* src, size_t len) {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (len > sizeof(_frame_residue_buf)) {
    errno = EMSGSIZE;
    return false;
  }
  if (len > 0) { memcpy(_frame_residue_buf, src, len); }
  _frame_residue_len = len;
  return true;
}

void UBSocketConnectionTable::init() {
  _table_lock = new Monitor(Mutex::leaf, "UBSocketConnectionTable_lock");
  _shutting_down = false;
}

bool UBSocketConnectionTable::publish(int fd, UBSocketConnection* conn) {
  MonitorLockerEx locker(_table_lock, Mutex::_no_safepoint_check_flag);
  if (_shutting_down) {
    errno = ECANCELED;
    return false;
  }
  if (_table.get(fd) != NULL) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d publish skipped: already exists\n", fd);
    return false;
  }
  _table.add(fd, conn);
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d publish connection\n", fd);
  return true;
}

bool UBSocketConnectionTable::contains(int fd) {
  MonitorLockerEx locker(_table_lock, Mutex::_no_safepoint_check_flag);
  return _table.get(fd) != NULL;
}

UBSocketConnection* UBSocketConnectionTable::pin(int fd) {
  MonitorLockerEx locker(_table_lock, Mutex::_no_safepoint_check_flag);
  if (_shutting_down) {
    errno = ECANCELED;
    return NULL;
  }
  UBSocketConnection* conn = _table.get(fd);
  if (conn == NULL || conn->closing()) { return NULL; }
  conn->pin();
  return conn;
}

void UBSocketConnectionTable::unpin(UBSocketConnection* conn) {
  if (conn == NULL) { return; }
  MonitorLockerEx locker(_table_lock, Mutex::_no_safepoint_check_flag);
  if (conn->unpin()) { locker.notify_all(); }
}

void UBSocketConnectionTable::start_shutdown() {
  MonitorLockerEx locker(_table_lock, Mutex::_no_safepoint_check_flag);
  _shutting_down = true;
  locker.notify_all();
}

UBSocketConnection* UBSocketConnectionTable::begin_close(int fd) {
  MonitorLockerEx locker(_table_lock, Mutex::_no_safepoint_check_flag);
  UBSocketConnection* conn = _table.get(fd);
  if (conn == NULL || conn->closing()) { return NULL; }
  conn->set_closing();
  while (conn->has_active()) {
    locker.wait(Mutex::_no_safepoint_check_flag);
  }
  return conn;
}

void UBSocketConnectionTable::finish_close(int fd, UBSocketConnection* conn) {
  MonitorLockerEx locker(_table_lock, Mutex::_no_safepoint_check_flag);
  if (_table.get(fd) == conn) {
    _table.remove(fd);
    UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d detach connection\n", fd);
  }
}

int UBSocketConnectionTable::unregister_abnormal_fds() {
  int success = 0;
  GrowableArray<int> fds(UB_INIT_ARRAY_CAP, true, mtInternal);
  {
    MonitorLockerEx locker(_table_lock, Mutex::_no_safepoint_check_flag);
    _table.begin_iteration();
    UBSocketConnection* conn = _table.next();
    while (conn != NULL) {
      fds.append(_table.get_cur_iter_key());
      conn = _table.next();
    }
  }
  for (int i = 0; i < fds.length(); i++) {
    if (UBSocketManager::unregister_fd(fds.at(i))) { success++; }
  }
  if (success != fds.length()) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "unregister abnormal fds success=%d total=%d\n",
           success, fds.length());
  }
  return success;
}

UBSocketConnectionHandle::UBSocketConnectionHandle(int fd)
    : _conn(UBSocketConnectionTable::pin(fd)) {}

UBSocketConnectionHandle::~UBSocketConnectionHandle() {
  UBSocketConnectionTable::unpin(_conn);
}
