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
#include "memory/resourceArea.hpp"
#include "runtime/atomic.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/orderAccess.inline.hpp"
#include "runtime/os.hpp"
#include "utilities/growableArray.hpp"
#include "matrix/ubSocket/ubSocketDataInfo.hpp"

Monitor* UBSocketConnectionTable::_table_lock = NULL;
PtrTable<int, UBSocketConnection*, mtInternal> UBSocketConnectionTable::_table(NULL);
UBSocketConnection** UBSocketConnectionTable::_slot_table = NULL;
uint32_t UBSocketConnectionTable::_slot_count = 0;
bool UBSocketConnectionTable::_shutting_down = false;

UBSocketConnection::UBSocketConnection(int fd, UBSocketMemMapping* mapping,
                                       uint32_t local_ring_slot,
                                       uint32_t local_connection_generation,
                                       uint64_t local_ring_size,
                                       uint32_t remote_ring_slot,
                                       uint32_t remote_connection_generation,
                                       uint64_t remote_ring_offset,
                                       uint64_t remote_ring_size)
    : _socket_fd(fd),
      _remote_mapping(mapping),
      _local_ring_slot(local_ring_slot),
      _local_connection_generation(local_connection_generation),
      _remote_ring_slot(remote_ring_slot),
      _remote_connection_generation(remote_connection_generation),
      _lock(new Monitor(Mutex::leaf, "UBSocketConnection_lock")),
      _rx_ring(UBSocketRingSlots::write_state(local_ring_slot),
               UBSocketRingSlots::peer_state(local_ring_slot),
               UBSocketRingSlots::payload_addr(local_ring_slot),
               local_ring_size),
      _tx_ring((char*)mapping->addr() +
                   UBSocketRingSlots::write_state_mapping_offset(remote_ring_slot),
               (char*)mapping->addr() +
                   UBSocketRingSlots::peer_state_mapping_offset(remote_ring_slot),
               (char*)mapping->addr() + remote_ring_offset,
               remote_ring_size),
      _remote_process_generation(
          &reinterpret_cast<UBSocketProcessHeader*>(mapping->addr())->
              notify_generation),
      _ring_error(false),
      _control_closed(false),
      _peer_close_received(false),
      _peer_control_kind(0),
      _tx_closed(false),
      _peer_final_write_offset(0),
      _rx_ready(false),
      _rx_read_offset(0),
      _tx_write_offset(0),
      _tx_last_notify_offset(0),
      _selector_subscriptions(NULL),
      _published(false),
      _closing(false),
      _active_count(0),
      _control_residue_len(0) {}

UBSocketConnection::~UBSocketConnection() {
  if (_local_ring_slot != UB_SOCKET_RING_INVALID_SLOT) {
    bool quiesced = _rx_ring.load_peer_quiesced_generation() ==
                    _local_connection_generation;
    if (!_published || quiesced) {
      UBSocketRingSlots::release(_local_ring_slot);
    } else {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "fd=%d quarantine local ring slot=%u generation=%u\n",
             _socket_fd, _local_ring_slot, _local_connection_generation);
      UBSocketRingSlots::retire(
          _local_ring_slot, _local_connection_generation);
    }
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
  uint64_t read_offset = (uint64_t)OrderAccess::load_acquire(
      (volatile julong*)&_rx_read_offset);
  if (_peer_close_received &&
      _rx_ring.load_write_offset() > _peer_final_write_offset) {
    errno = EBADMSG;
    mark_error_locked();
    return false;
  }
  bool is_empty = _rx_ring.empty(read_offset, &valid_window);
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
  uint64_t read_offset = (uint64_t)OrderAccess::load_acquire(
      (volatile julong*)&_rx_read_offset);
  uint64_t new_read_offset = read_offset;
  long result = (long)_rx_ring.read_available(dst, len, read_offset,
                                              &new_read_offset);
  if (result > 0) {
    OrderAccess::release_store((volatile julong*)&_rx_read_offset,
                               (julong)new_read_offset);
    // Publish the receive acknowledgement through the peer-owned reverse
    // ring. This keeps our receiver-owned mapping read-only on the data path.
    _tx_ring.publish_peer_read_ack_offset(new_read_offset);
    if (UBSocketSpinNanos > 0) {
      // Pair with the Sender fence before its second ack load. Either the
      // Sender observes this drain or this Receiver observes the new write.
      OrderAccess::fence();
    }
  }
  if (result < 0 && errno == EINVAL) {
    mark_error_locked();
  } else {
    refresh_rx_ready();
  }
  if (result == 0 && peer_close_ready_locked()) {
    errno = ESHUTDOWN;
    return -1;
  }
  return result;
}

long UBSocketConnection::write_data(const void* src, size_t len) {
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
  long result = (long)_tx_ring.write(src, len, old_write_offset,
                                     peer_read_ack_offset, &new_write_offset);
  if (result < 0 && errno == EINVAL) {
    mark_error_locked();
    return result;
  }
  if (result <= 0) { return result; }

  _tx_write_offset = new_write_offset;
  bool need_notification = false;
  if (UBSocketSpinNanos == 0) {
    need_notification = true;
  } else {
    uint64_t effective_threshold = MIN2(
        (uint64_t)UB_POLL_NOTIFY_PROGRESS_BYTES, _tx_ring.capacity());
    if (peer_read_ack_offset == old_write_offset) {
      UBSocketProfiler::count(UB_PROF_POLL_NOTIFY_REQUEST_EMPTY,
                              (uint64_t)result);
      need_notification = true;
    } else if (new_write_offset - _tx_last_notify_offset >=
               effective_threshold) {
      UBSocketProfiler::count(UB_PROF_POLL_NOTIFY_REQUEST_THRESHOLD,
                              (uint64_t)result);
      need_notification = true;
    } else {
      // Close the store-buffering window with a Receiver that publishes its ack
      // while deciding whether retained readiness can be cleared.
      OrderAccess::fence();
      uint64_t latest_ack = _rx_ring.load_peer_read_ack_offset();
      if (latest_ack >= old_write_offset && latest_ack <= new_write_offset) {
        UBSocketProfiler::count(UB_PROF_POLL_NOTIFY_REQUEST_RACE,
                                (uint64_t)result);
        need_notification = true;
      } else {
        UBSocketProfiler::count(UB_PROF_POLL_NOTIFY_SKIP_NONEMPTY,
                                (uint64_t)result);
      }
    }
  }
  if (need_notification) {
    _tx_last_notify_offset = new_write_offset;
    publish_poll_notification_locked();
  }
  return result;
}

void UBSocketConnection::publish_poll_notification_locked() {
  UBSocketProfiler::count(UB_PROF_POLL_NOTIFY_PUBLISH);
  // Publish payload/write-offset state before the Receiver observes the
  // process-level notification generation on a different cache line.
  OrderAccess::fence();
  Atomic::inc(_remote_process_generation);
}

bool UBSocketConnection::poll_rx_ready() {
  uint64_t read_offset = (uint64_t)OrderAccess::load_acquire(
      (volatile julong*)&_rx_read_offset);
  uint64_t write_offset = _rx_ring.load_write_offset();
  return write_offset != read_offset;
}

bool UBSocketConnection::spin_rx_ready(uint64_t budget_ns) {
  uint64_t start_ns = (uint64_t)os::javaTimeNanos();
  UBSocketProfiler::count(UB_PROF_SELECTOR_SPIN_ATTEMPT);
  for (;;) {
    if (poll_rx_ready()) {
      uint64_t elapsed_ns = (uint64_t)os::javaTimeNanos() - start_ns;
      UBSocketProfiler::count(UB_PROF_SELECTOR_SPIN_READY);
      UBSocketProfiler::record(UB_PROF_SELECTOR_SPIN_NANOS,
                               elapsed_ns, 0, 1);
      return true;
    }
    uint64_t elapsed_ns = (uint64_t)os::javaTimeNanos() - start_ns;
    if (elapsed_ns >= budget_ns) {
      UBSocketProfiler::count(UB_PROF_SELECTOR_SPIN_TIMEOUT);
      UBSocketProfiler::record(UB_PROF_SELECTOR_SPIN_NANOS,
                               elapsed_ns, 0, 1);
      return false;
    }
    SpinPause();
  }
}

void UBSocketConnection::mark_control_closed() {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  _control_closed = true;
}

bool UBSocketConnection::prepare_control_frame(uint16_t kind,
                                               UBSocketControlFrame* frame) {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (frame == NULL ||
      (kind != UB_SOCKET_SHUTDOWN_OUTPUT && kind != UB_SOCKET_CLOSE)) {
    errno = EINVAL;
    return false;
  }
  _tx_closed = true;
  *frame = ub_socket_control_frame(kind, _remote_connection_generation, _tx_write_offset);
  OrderAccess::fence();
  _tx_ring.publish_peer_quiesced_generation(
      _remote_connection_generation);
  publish_poll_notification_locked();
  return true;
}

bool UBSocketConnection::accept_control_frame(
    const UBSocketControlFrame& frame) {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (frame.connection_generation != _local_connection_generation ||
      frame.final_write_offset < _rx_read_offset ||
      frame.final_write_offset - _rx_read_offset > _rx_ring.capacity()) {
    errno = EBADMSG;
    mark_error_locked();
    return false;
  }
  if (_peer_close_received &&
      (frame.final_write_offset != _peer_final_write_offset ||
       (_peer_control_kind == UB_SOCKET_CLOSE &&
        frame.kind != UB_SOCKET_CLOSE))) {
    errno = EBADMSG;
    mark_error_locked();
    return false;
  }
  uint64_t write_offset = _rx_ring.load_write_offset();
  if (write_offset > frame.final_write_offset) {
    errno = EBADMSG;
    mark_error_locked();
    return false;
  }
  _peer_close_received = true;
  _peer_control_kind = frame.kind;
  _peer_final_write_offset = frame.final_write_offset;
  if (frame.kind == UB_SOCKET_CLOSE) {
    _tx_closed = true;
    OrderAccess::fence();
    _tx_ring.publish_peer_quiesced_generation(
        _remote_connection_generation);
  }
  return true;
}

bool UBSocketConnection::peer_close_ready_locked() {
  if (!_peer_close_received || _ring_error) { return false; }
  uint64_t write_offset = _rx_ring.load_write_offset();
  if (write_offset > _peer_final_write_offset) {
    mark_error_locked();
    return false;
  }
  return write_offset == _peer_final_write_offset &&
         _rx_read_offset == _peer_final_write_offset;
}

bool UBSocketConnection::peer_close_ready() {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  bool ready = peer_close_ready_locked();
  if (!ready && _peer_close_received && !_ring_error) {
    UBSocketProfiler::count(UB_PROF_CLOSE_WAIT_FINAL_OFFSET);
  }
  return ready;
}

bool UBSocketConnection::has_pending_data() {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (_ring_error) { return false; }
  if (_rx_ready) { return true; }
  return refresh_rx_ready();
}

bool UBSocketConnection::take_control_residue(char* dst, size_t dst_len,
                                              size_t* len) {
  *len = 0;
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (_control_residue_len == 0) { return true; }
  if (_control_residue_len > dst_len) {
    errno = EMSGSIZE;
    return false;
  }
  memcpy(dst, _control_residue_buf, _control_residue_len);
  *len = _control_residue_len;
  _control_residue_len = 0;
  return true;
}

bool UBSocketConnection::store_control_residue(const char* src, size_t len) {
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (len > sizeof(_control_residue_buf)) {
    errno = EMSGSIZE;
    return false;
  }
  if (len > 0) { memcpy(_control_residue_buf, src, len); }
  _control_residue_len = len;
  return true;
}

void UBSocketConnectionTable::init(uint32_t slot_count) {
  _table_lock = new Monitor(Mutex::leaf, "UBSocketConnectionTable_lock");
  _slot_table = NEW_C_HEAP_ARRAY(UBSocketConnection*, slot_count, mtInternal);
  memset(_slot_table, 0, (size_t)slot_count * sizeof(UBSocketConnection*));
  _slot_count = slot_count;
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
  uint32_t slot = conn->local_ring_slot();
  if (slot >= _slot_count || _slot_table[slot] != NULL) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "fd=%d publish skipped: local slot=%u already exists\n", fd, slot);
    return false;
  }
  conn->mark_published();
  _table.add(fd, conn);
  _slot_table[slot] = conn;
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
    uint32_t slot = conn->local_ring_slot();
    if (slot < _slot_count && _slot_table[slot] == conn) {
      _slot_table[slot] = NULL;
    }
    UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d detach connection\n", fd);
  }
}

void UBSocketConnectionTable::collect_write_offset_and_pin(
    GrowableArray<uint32_t>* changed_slots,
    GrowableArray<UBSocketConnection*>* ready) {
  ready->clear();
  MonitorLockerEx locker(_table_lock, Mutex::_no_safepoint_check_flag);
  if (_shutting_down) { return; }
  for (int i = 0; i < changed_slots->length(); i++) {
    uint32_t slot = changed_slots->at(i);
    UBSocketConnection* conn = slot < _slot_count ? _slot_table[slot] : NULL;
    if (conn != NULL && !conn->closing() && conn->poll_rx_ready()) {
      conn->pin();
      ready->append(conn);
    }
  }
}

void UBSocketConnectionTable::unpin_all(
    GrowableArray<UBSocketConnection*>* connections) {
  if (connections->is_empty()) { return; }
  MonitorLockerEx locker(_table_lock, Mutex::_no_safepoint_check_flag);
  bool notify = false;
  for (int i = 0; i < connections->length(); i++) {
    if (connections->at(i)->unpin()) { notify = true; }
  }
  connections->clear();
  if (notify) { locker.notify_all(); }
}

int UBSocketConnectionTable::unregister_abnormal_fds() {
  ResourceMark rm;
  int success = 0;
  GrowableArray<int> fds(UB_INIT_ARRAY_CAP);
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
