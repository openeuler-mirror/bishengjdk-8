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

#ifndef SHARE_VM_MATRIX_UBSOCKETUTILS_HPP
#define SHARE_VM_MATRIX_UBSOCKETUTILS_HPP

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixUtils.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"

class Monitor;
class UBSocketAttachSession;
struct UBSocketEndpoint;
struct UBSocketAttachFrame;
template <typename E> class GrowableArray;

struct UBSocketBlkItem {
  uintptr_t meta_addr;
  uint32_t start_blk;
  uint32_t blk_count;
};

// Per-fd list of descriptors sent to the peer but not yet observed as read.
// UnreadMsgTable reclaims read entries and checks timeout state periodically
// or when block allocation needs a reclaim pass.
class UnreadMsgList : public CHeapObj<mtInternal> {
 public:
  class UnreadMsgNode : public CHeapObj<mtInternal> {
   public:
    explicit UnreadMsgNode(uintptr_t meta_addr = 0, uint32_t start_blk = 0,
                           uint32_t blk_count = 0, UnreadMsgNode* n = NULL)
        : meta_addr(meta_addr), start_blk(start_blk), blk_count(blk_count), next(n) {}

    uintptr_t meta_addr;
    uint32_t start_blk;
    uint32_t blk_count;
    UnreadMsgNode* next;
  };

  explicit UnreadMsgList(int fd)
      : _head(NULL), _tail(NULL), _socket_fd(fd),
        _lock(new Monitor(Mutex::leaf, "UnreadMsgList_lock")),
        _closing(false), _active_count(0) {
    _head = _tail = new UnreadMsgNode();
  }
  ~UnreadMsgList() {
    UnreadMsgNode* cur = _head;
    while (cur != NULL) {
      UnreadMsgNode* tmp = cur;
      cur = cur->next;
      delete tmp;
    }
    delete _lock;
  }
  void append(uintptr_t meta_addr, uint32_t start_blk, uint32_t blk_count);
  int socket_fd() const { return _socket_fd; }
  bool is_empty() const { return _head->next == NULL; }
  Monitor* lock() const { return _lock; }
  bool closing() const { return _closing; }
  void set_closing() { _closing = true; }
  void acquire_pin() { _active_count++; }
  bool release_pin();
  bool has_active() const { return _active_count > 0; }
  bool has_pending_locked() const { return _head->next != NULL; }
  void reclaim_read_blocks_locked(GrowableArray<UBSocketBlkItem>* reclaim_items);
  void process_unread_msgs(uint64_t now_nanos,
                           GrowableArray<UBSocketBlkItem>* reclaim_items,
                           bool* recv_timeout, bool* read_timeout);

 private:
  UnreadMsgNode* _head;
  UnreadMsgNode* _tail;
  int _socket_fd;
  Monitor* _lock;
  bool _closing;
  int _active_count;
};

class UnreadMsgTable : public AllStatic {
 public:
  static void init();
  static void register_fd(int fd);
  static void unregister_fd(int fd);
  static bool add_msg(int fd, uintptr_t meta_addr, uint32_t start_blk, uint32_t blk_count);
  static UnreadMsgList* pin_list(int fd);
  static void unpin_list(UnreadMsgList* list);
  static void add_pinned_msgs(UnreadMsgList* list,
                              const GrowableArray<UBSocketBlkItem>* blk_items,
                              int count);
  static bool has_pending_msg(int fd);
  static void start_timer();
  static void stop_timer();
  static void cleanup();
  static void check_unread_entry(JavaThread* thread, TRAPS);
  // Reclaim read descriptors and advance timeout/fallback maintenance.
  static int process_unread_msgs();

 private:
  static JavaThread* _thread;
  static bool _timer_starting;
  static volatile bool _should_terminate;
  static volatile bool _exited;
  static Monitor* _wait_monitor;
  static Monitor* _unread_table_lock;
  static PtrTable<int, UnreadMsgList*, mtInternal> _table;

  static UnreadMsgList* pin_list_locked(int fd);
};

class UBSocketSessionCaches : public AllStatic {
 public:
  static void init();
  static void add(UBSocketAttachSession* session);
  static UBSocketAttachSession* find(const UBSocketEndpoint* local_ep,
                                     const UBSocketEndpoint* remote_ep);
  static void release(UBSocketAttachSession* session);
  static void remove(const UBSocketEndpoint* local_ep, const UBSocketEndpoint* remote_ep);
  static void cleanup();

 private:
  static Mutex* _cache_lock;
  static UBSocketAttachSession* _cache_head;
};

class UBSocketEarlyReqQueue : public AllStatic {
 public:
  static void init() {_head = _tail = NULL; }
  static bool has_requests() { return _head != NULL; }
  static bool cache(int control_fd, const UBSocketAttachFrame* request, uint64_t ddl_ns);
  static int count();
  static bool take_one(int* control_fd, UBSocketAttachFrame* request, uint64_t* ddl_ns);
  static void cleanup();

 private:
  struct EarlyRequest;
  static EarlyRequest* _head;
  static EarlyRequest* _tail;
};

class UBSocketBlkBitmap : public AllStatic {
 public:
  static void init(uint32_t blk_count);
  static void cleanup();
  static bool alloc(uint32_t blk_need, uint32_t* start_blk);
  static void release(uint32_t start_blk, uint32_t blk_count);

 private:
  static volatile uint32_t* _words;
  static size_t _word_count;
  static uint32_t _blk_count;
  static volatile uint32_t _next_hint;

  static uint32_t mask_for_word(uint32_t word_idx, uint32_t start_blk, uint32_t blk_count);
  static bool try_set_range(uint32_t start_blk, uint32_t blk_count);
  static void clear_range(uint32_t start_blk, uint32_t blk_count);
};

class UBSocketThreadUtils : public AllStatic {
 public:
  static JavaThread* start_daemon(ThreadFunction entry, const char* name,
                                  ThreadPriority priority = NoPriority);
};

#endif  // SHARE_VM_MATRIX_UBSOCKETUTILS_HPP
