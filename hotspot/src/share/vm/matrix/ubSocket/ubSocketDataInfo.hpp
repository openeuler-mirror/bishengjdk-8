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

class Monitor;

// Per-fd UBSocket state. It is the sole owner of the local ring slot and the
// remote mapping reference after attach publishes the connection.
class UBSocketConnection : public CHeapObj<mtInternal> {
 public:
  UBSocketConnection(int fd, UBSocketMemMapping* mapping,
                     uint32_t local_ring_slot, uint64_t local_ring_size,
                     uint64_t remote_ring_offset, uint64_t remote_ring_size);
  ~UBSocketConnection();

  long read_data(void* dst, size_t len);
  long write_data(const void* src, size_t len, bool* need_wakeup);
  void end_tx_wakeup();
  void cancel_tx_wakeup();
  bool mark_rx_wakeup();
  void mark_error();
  void mark_control_closed();
  void mark_peer_closed();
  bool has_pending_data();
  bool ready();
  bool take_frame_residue(char* dst, size_t dst_len, size_t* len);
  bool store_frame_residue(const char* src, size_t len);

 private:
  friend class UBSocketConnectionTable;

  int _socket_fd;
  UBSocketMemMapping* _remote_mapping;
  uint32_t _local_ring_slot;
  Monitor* _lock;
  UBSocketRing _rx_ring;
  UBSocketRing _tx_ring;
  bool _tx_wakeup_sending;
  bool _tx_wakeup_pending;
  bool _ring_error;
  bool _rx_closed;
  bool _tx_closed;
  uint64_t _tx_last_wakeup_offset;
  bool _rx_ready;
  uint64_t _rx_read_offset;
  uint64_t _tx_write_offset;
  bool _closing;
  int _active_count;
  char _frame_residue_buf[UB_SOCKET_WAKEUP_FRAME_WIRE_SIZE];
  size_t _frame_residue_len;

  bool closing() const { return _closing; }
  void set_closing() { _closing = true; }
  void mark_error_locked();
  bool refresh_rx_ready();
  void pin() { _active_count++; }
  bool unpin();
  bool has_active() const { return _active_count > 0; }
};

// Thin fd -> connection registry. The table lock only protects registry and
// lifetime bookkeeping; it must not perform ring IO or acquire connection locks.
class UBSocketConnectionTable : public AllStatic {
 public:
  static void init();
  static bool publish(int fd, UBSocketConnection* conn);
  static bool contains(int fd);
  static UBSocketConnection* pin(int fd);
  static void unpin(UBSocketConnection* conn);
  static void start_shutdown();
  static UBSocketConnection* begin_close(int fd);
  static void finish_close(int fd, UBSocketConnection* conn);
  static int unregister_abnormal_fds();

 private:
  static Monitor* _table_lock;
  static PtrTable<int, UBSocketConnection*, mtInternal> _table;
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
