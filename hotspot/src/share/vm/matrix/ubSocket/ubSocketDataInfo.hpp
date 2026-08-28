/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

#ifndef SHARE_VM_MATRIX_UBSOCKETDATAINFO_HPP
#define SHARE_VM_MATRIX_UBSOCKETDATAINFO_HPP

#include <stddef.h>
#include <stdint.h>

#include "matrix/matrixUtils.hpp"
#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/ubSocket/ubSocketMemMapping.hpp"
#include "matrix/ubSocket/ubSocketRing.hpp"
#include "memory/allocation.hpp"
#include "runtime/mutexLocker.hpp"
#include "utilities/growableArray.hpp"

class Monitor;
class UBSocketSelectorNotifiers;
struct UBSocketSelectorSubscription;

// Per-fd UBSocket state. It is the sole owner of the local ring slot and the
// remote mapping reference after attach publishes the connection.
class UBSocketConnection : public CHeapObj<mtInternal> {
 public:
  UBSocketConnection(int fd, UBSocketMemMapping* mapping,
                     uint32_t local_ring_slot,
                     uint32_t local_connection_generation,
                     uint64_t local_ring_size, uint32_t remote_ring_slot,
                     uint32_t remote_connection_generation,
                     uint64_t remote_ring_offset, uint64_t remote_ring_size);
  ~UBSocketConnection();

  long read_data(void* dst, size_t len);
  long write_data(const void* src, size_t len);
  bool poll_rx_ready();
  bool spin_rx_ready(uint64_t budget_ns);
  void mark_error();
  void mark_control_closed();
  bool prepare_control_frame(uint16_t kind, UBSocketControlFrame* frame);
  bool accept_control_frame(const UBSocketControlFrame& frame);
  bool peer_close_ready();
  bool has_pending_data();
  bool ready();
  bool take_control_residue(char* dst, size_t dst_len, size_t* len);
  bool store_control_residue(const char* src, size_t len);

 private:
  friend class UBSocketConnectionTable;
  friend class UBSocketSelectorNotifiers;

  int _socket_fd;
  UBSocketMemMapping* _remote_mapping;
  uint32_t _local_ring_slot;
  uint32_t _local_connection_generation;
  uint32_t _remote_ring_slot;
  uint32_t _remote_connection_generation;
  Monitor* _lock;
  UBSocketRing _rx_ring;
  UBSocketRing _tx_ring;
  volatile jint* _remote_process_generation;
  bool _ring_error;
  bool _control_closed;
  bool _peer_close_received;
  uint16_t _peer_control_kind;
  bool _tx_closed;
  uint64_t _peer_final_write_offset;
  bool _rx_ready;
  volatile uint64_t _rx_read_offset;
  uint64_t _tx_write_offset;
  uint64_t _tx_last_notify_offset;
  UBSocketSelectorSubscription* _selector_subscriptions;
  bool _published;
  bool _closing;
  int _active_count;
  char _control_residue_buf[UB_SOCKET_CONTROL_FRAME_WIRE_SIZE];
  size_t _control_residue_len;

  bool closing() const { return _closing; }
  uint32_t local_ring_slot() const { return _local_ring_slot; }
  uint32_t local_connection_generation() const {
    return _local_connection_generation;
  }
  void mark_published() { _published = true; }
  void set_closing() { _closing = true; }
  void mark_error_locked();
  void publish_poll_notification_locked();
  bool refresh_rx_ready();
  bool peer_close_ready_locked();
  void pin() { _active_count++; }
  bool unpin();
  bool has_active() const { return _active_count > 0; }
};

// Thin fd -> connection registry. The table lock only protects registry and
// lifetime bookkeeping; it must not perform ring IO or acquire connection locks.
class UBSocketConnectionTable : public AllStatic {
 public:
  static void init(uint32_t slot_count);
  static bool publish(int fd, UBSocketConnection* conn);
  static bool contains(int fd);
  static UBSocketConnection* pin(int fd);
  static void unpin(UBSocketConnection* conn);
  static void start_shutdown();
  static UBSocketConnection* begin_close(int fd);
  static void finish_close(int fd, UBSocketConnection* conn);
  static void collect_write_offset_and_pin(
      GrowableArray<uint32_t>* changed_slots,
      GrowableArray<UBSocketConnection*>* ready);
  static void unpin_all(
      GrowableArray<UBSocketConnection*>* connections);
  static int unregister_abnormal_fds();

 private:
  static Monitor* _table_lock;
  static PtrTable<int, UBSocketConnection*, mtInternal> _table;
  static UBSocketConnection** _slot_table;
  static uint32_t _slot_count;
  static bool _shutting_down;
};

class UBSocketConnectionHandle : public StackObj {
 public:
  explicit UBSocketConnectionHandle(int fd);
  ~UBSocketConnectionHandle();

  UBSocketConnection* get() const { return _conn; }

 private:
  UBSocketConnection* _conn;

  UBSocketConnectionHandle(const UBSocketConnectionHandle&);
  UBSocketConnectionHandle& operator=(const UBSocketConnectionHandle&);
};

#endif  // SHARE_VM_MATRIX_UBSOCKETDATAINFO_HPP
