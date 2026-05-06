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

#ifndef SHARE_VM_MATRIX_UBSOCKETDATAINFO_HPP
#define SHARE_VM_MATRIX_UBSOCKETDATAINFO_HPP

#include <stddef.h>
#include <stdint.h>

#include "matrix/matrixUtils.hpp"

class Monitor;
class UBSocketMemMapping;

static const int UB_SOCKET_FRAME_RESIDUE_BUF_LEN = 64;
// same as UB_SOCKET_PARSE_BATCH_BUF_LEN in FileDispatcherImpl.c
static const int UB_SOCKET_PARSE_BATCH_BUF_LEN = 4096;

enum UBSocketFallbackMarkState {
  FALLBACK_MARK_NOT_SENT,
  FALLBACK_MARK_SENDING,
  FALLBACK_MARK_SENT
};

class UBSocketFallbackState {
 public:
  UBSocketFallbackState()
      : _draining(false), _mark_state(FALLBACK_MARK_NOT_SENT),
        _tcp_len(0), _tcp_pos(0) {}

  bool append_tcp_tail(const char* src, size_t len);
  size_t read_tcp_tail(void* dst, size_t len);
  bool has_tcp_tail() const { return _tcp_pos < _tcp_len; }

  bool draining() const { return _draining; }
  bool mark_sent() const { return _mark_state == FALLBACK_MARK_SENT; }
  // Fallback mark sending assumes one writer per fd. SENDING prevents duplicate
  // DATA_FALLBACK frames; callers do not wait for another writer to finish.
  bool begin_mark_send() {
    if (!_draining || _mark_state != FALLBACK_MARK_NOT_SENT) { return false; }
    _mark_state = FALLBACK_MARK_SENDING;
    return true;
  }
  void complete_mark_send() { _mark_state = FALLBACK_MARK_SENT; }
  void abort_mark_send() { _mark_state = FALLBACK_MARK_NOT_SENT; }
  bool drained(bool has_pending_data) const {
    return _draining && mark_sent() && !has_pending_data;
  }
  bool ready_for_ub_io(bool has_pending_data) const {
    return !mark_sent() || has_pending_data;
  }
  void request() { _draining = true; }
  void receive_mark() {
    _draining = true;
    _mark_state = FALLBACK_MARK_SENT;
  }

 private:
  bool _draining;
  UBSocketFallbackMarkState _mark_state;
  size_t _tcp_len;
  size_t _tcp_pos;
  char _tcp_buf[UB_SOCKET_PARSE_BATCH_BUF_LEN];
};

// Per-fd UBSocket data-path state. It owns the remote memory mapping reference,
// queued descriptor ranges, fallback residue bytes, and drain-to-TCP state.
// SocketDataInfoTable pins lifetime; same-fd read/write/parse calls are serialized.
class UBSocketInfoList : public CHeapObj<mtInternal> {
 public:
  explicit UBSocketInfoList(int fd, UBSocketMemMapping* mapping)
      : _head(NULL), _tail(NULL), _cursor(NULL),
        _cur_loc(0), _socket_fd(fd),
        _mem_mapping(mapping),
        _closing(false), _active_count(0),
        _frame_residue_len(0) {}
  ~UBSocketInfoList() {
    while (_head) {
      SocketListNode* next = _head->next;
      delete _head;
      _head = next;
    }
  }

  bool append_range(const char* name, uint64_t off, uint64_t len); // cache <off,len>
  long read_data(void* dst, size_t len);  // read len data of this fd to dst
  bool take_frame_residue(char* dst, size_t dst_len, size_t* len);
  bool store_frame_residue(const char* src, size_t len);

  bool append_fallback_tail(const char* src, size_t len);
  bool has_pending_data() const {
    return has_ub_pending_data() || _fallback.has_tcp_tail();
  }
  bool fallback_draining() const { return _fallback.draining(); }
  bool fallback_drained() const { return _fallback.drained(has_pending_data()); }
  bool ready_for_ub_io() const {
    return _fallback.ready_for_ub_io(has_pending_data());
  }
  void request_fallback() { _fallback.request(); }
  bool begin_fallback_mark_send() { return _fallback.begin_mark_send(); }
  void complete_fallback_mark_send() { _fallback.complete_mark_send(); }
  void abort_fallback_mark_send() { _fallback.abort_mark_send(); }
  void receive_fallback_mark() { _fallback.receive_mark(); }

  UBSocketMemMapping* mapping() { return _mem_mapping; }

 private:
  friend class SocketDataInfoTable;

  class SocketListNode : public CHeapObj<mtInternal> {
   public:
    SocketListNode(size_t off, size_t size, UBSocketInfoList::SocketListNode* n)
        : offset(off), size(size), next(n) {}

    size_t offset;
    size_t size;
    SocketListNode* next;
  };
  SocketListNode* _head;
  SocketListNode* _tail;
  SocketListNode* _cursor;  // current node
  size_t _cur_loc;          // current node location

  int _socket_fd;     // one instance pre socket
  UBSocketMemMapping* _mem_mapping;  // lifecycle owner for the remote mapping

  UBSocketFallbackState _fallback;
  bool _closing;
  int _active_count;
  char _frame_residue_buf[UB_SOCKET_FRAME_RESIDUE_BUF_LEN];
  size_t _frame_residue_len;

  bool closing() const { return _closing; }
  void set_closing() { _closing = true; }
  void pin() { _active_count++; }
  bool unpin();
  bool has_active() const { return _active_count > 0; }

  bool has_ub_pending_data() const {
    return _cursor != NULL && (_cur_loc < _cursor->size || _cursor->next != NULL);
  }
  void append(size_t offset, size_t size);
  bool finish_current_range();
  int delete_nodes(SocketListNode* start, SocketListNode* end);
};

// Global fd -> UBSocketInfoList registry. The table lock protects publish,
// lookup, detach, and pin bookkeeping; the pinned info object carries the
// per-fd state used by read/write/fallback paths.
class SocketDataInfoTable : public AllStatic {
 public:
  static void init();
  static bool publish(int fd, UBSocketInfoList* info);
  static bool contains(int fd);
  static long append_range(int fd, const char* name, uint64_t off, uint64_t len);
  static long read_data(int fd, void* dst, size_t len);
  static bool is_fallback_draining(int fd);
  static bool can_send_frame(int fd);
  static bool request_fallback(int fd, const char* reason);
  static bool begin_fallback_mark_send(int fd);
  static void complete_fallback_mark_send(int fd);
  static void abort_fallback_mark_send(int fd);
  static bool receive_fallback_mark(int fd, const char* src, size_t len);
  static bool fallback_drained(int fd);
  static bool has_pending_data(int fd);
  static bool ready_for_ub_io(int fd);
  static bool take_frame_residue(int fd, char* dst, size_t dst_len, size_t* len);
  static bool store_frame_residue(int fd, const char* src, size_t len);
  static UBSocketInfoList* pin_list(int fd);
  static void unpin_list(UBSocketInfoList* info);
  static UBSocketInfoList* detach(int fd);
  static int unregister_abnormal_fds();  // unregister fds abnormal exit

 private:
  static Monitor* _info_table_lock;
  static PtrTable<int, UBSocketInfoList*, mtInternal> _table;

  static UBSocketInfoList* pin_list_locked(int fd);
};

#endif  // SHARE_VM_MATRIX_UBSOCKETDATAINFO_HPP
