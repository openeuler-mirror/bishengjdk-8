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

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketAttachSession.hpp"
#include "matrix/ubSocket/ubSocketDataInfo.hpp"
#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/ubSocket/ubSocketIO.hpp"
#include "matrix/ubSocket/ubSocketMemMapping.hpp"
#include "matrix/ubSocket/ubSocket.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"

#include "matrix/ubSocket/ubSocketUtils.hpp"

/***************************static fields****************************/

PtrTable<int, UnreadMsgList*, mtInternal> UnreadMsgTable::_table(NULL);
JavaThread* UnreadMsgTable::_thread = NULL;
bool UnreadMsgTable::_timer_starting = false;
volatile bool UnreadMsgTable::_should_terminate = false;
volatile bool UnreadMsgTable::_exited = true;
Monitor* UnreadMsgTable::_wait_monitor = NULL;
Monitor* UnreadMsgTable::_unread_table_lock = NULL;

volatile uint32_t* UBSocketBlkBitmap::_words = NULL;
volatile uint32_t UBSocketBlkBitmap::_next_hint = 0;
size_t UBSocketBlkBitmap::_word_count = 0;
uint32_t UBSocketBlkBitmap::_blk_count = 0;

UBSocketEarlyReqQueue::EarlyRequest* UBSocketEarlyReqQueue::_head = NULL;
UBSocketEarlyReqQueue::EarlyRequest* UBSocketEarlyReqQueue::_tail = NULL;

Mutex* UBSocketSessionCaches::_cache_lock = NULL;
UBSocketAttachSession* UBSocketSessionCaches::_cache_head = NULL;

static const int UB_SOCKET_EARLY_REQUEST_MAX = 128;
static const size_t BITS_PER_WORD = sizeof(uint32_t) * BitsPerByte;

/***************************UnreadMsgList***************************/

void UnreadMsgList::append(uintptr_t meta_addr, uint32_t start_blk, uint32_t blk_count) {
  UnreadMsgNode* newNode = new UnreadMsgNode(meta_addr, start_blk, blk_count, NULL);
  _tail->next = newNode;
  _tail = newNode;
}

bool UnreadMsgList::release_pin() {
  if (_active_count <= 0) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d unread msg release without active pin\n",
           _socket_fd);
    return false;
  }
  _active_count--;
  return _closing && _active_count == 0;
}

static void read_unread_msg_meta(int fd, uintptr_t meta_addr, UBSocketBlkMeta* meta) {
  memset(meta, 0, sizeof(UBSocketBlkMeta));
  if (meta_addr != 0) {
    memcpy(meta, (void*)meta_addr, sizeof(UBSocketBlkMeta));
  }
  if (meta->fd != 0 && (int)meta->fd != fd) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "fd=%d meta fd mismatch meta_fd=%u state=%u\n",
           fd, meta->fd, meta->state);
  }
}

void UnreadMsgList::reclaim_read_blocks_locked(
    GrowableArray<UBSocketBlkItem>* reclaim_items) {
  UnreadMsgNode* prev = _head;
  UnreadMsgNode* cur = _head->next;
  while (cur != NULL) {
    UnreadMsgNode* next = cur->next;
    UBSocketBlkMeta meta;
    read_unread_msg_meta(_socket_fd, cur->meta_addr, &meta);
    if (meta.state == UB_SOCKET_BLK_READ) {
      UnreadMsgNode* need_del = cur;
      prev->next = cur->next;
      if (_tail == need_del) {
        _tail = prev;
      }
      UBSocketBlkItem item = {need_del->meta_addr, need_del->start_blk, need_del->blk_count};
      reclaim_items->append(item);
      delete need_del;
    } else {
      prev = cur;
    }
    cur = next;
  }
}

void UnreadMsgList::process_unread_msgs(uint64_t now_nanos,
                                        GrowableArray<UBSocketBlkItem>* reclaim_items,
                                        bool* recv_timeout, bool* read_timeout) {
  *recv_timeout = false;
  *read_timeout = false;
  reclaim_read_blocks_locked(reclaim_items);
  UnreadMsgNode* cur = _head->next;
  while (cur != NULL) {
    UnreadMsgNode* next = cur->next;
    UBSocketBlkMeta meta;
    read_unread_msg_meta(_socket_fd, cur->meta_addr, &meta);
    if (meta.send_nanos != 0) {
      if (meta.state == UB_SOCKET_BLK_SEND &&
          now_nanos - meta.send_nanos >
              (uint64_t)UB_SOCKET_RECV_TIMEOUT_MS * NANOSECS_PER_MILLISEC) {
        *recv_timeout = true;
      } else if (meta.state == UB_SOCKET_BLK_RECV &&
                 UBSocketManager::package_timeout > 0 &&
                 now_nanos - meta.send_nanos > UBSocketManager::package_timeout) {
        *read_timeout = true;
      }
    }
    cur = next;
  }
}

void UnreadMsgTable::init() {
  _unread_table_lock = new Monitor(Mutex::leaf + 1, "UnreadMsgTable_lock");
  _wait_monitor = new Monitor(Mutex::safepoint - 1, "UBSocketCheckMonitor");
  _thread = NULL;
  _timer_starting = false;
  _should_terminate = false;
  _exited = true;
}

void UnreadMsgTable::register_fd(int fd) {
  UnreadMsgList* list = new UnreadMsgList(fd);
  bool inserted = false;
  {
    MutexLocker locker(_unread_table_lock);
    if (_table.get(fd) == NULL) {
      _table.add(fd, list);
      inserted = true;
    }
  }
  if (inserted) {
    UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d register unread tracking\n", fd);
    start_timer();
  } else {
    delete list;
  }
}

UnreadMsgList* UnreadMsgTable::pin_list_locked(int fd) {
  UnreadMsgList* list = _table.get(fd);
  if (list == NULL || list->closing()) { return NULL; }
  list->acquire_pin();
  return list;
}

void UnreadMsgTable::unpin_list(UnreadMsgList* list) {
  MonitorLockerEx locker(_unread_table_lock, Mutex::_no_safepoint_check_flag);
  if (list->release_pin()) { locker.notify_all(); }
}

UnreadMsgList* UnreadMsgTable::pin_list(int fd) {
  MonitorLockerEx locker(_unread_table_lock, Mutex::_no_safepoint_check_flag);
  return pin_list_locked(fd);
}

void UnreadMsgTable::unregister_fd(int fd) {
  UnreadMsgList* list = NULL;
  GrowableArray<UBSocketBlkItem> reclaim_items(UB_INIT_ARRAY_CAP, true, mtInternal);
  bool has_pending = false;
  {
    MonitorLockerEx table_locker(_unread_table_lock, Mutex::_no_safepoint_check_flag);
    list = _table.get(fd);
    if (list == NULL) { return; }
    list->set_closing();
    while (list->has_active()) {
      table_locker.wait(Mutex::_no_safepoint_check_flag);
    }
    {
      MutexLocker list_locker(list->lock());
      list->reclaim_read_blocks_locked(&reclaim_items);
      has_pending = list->has_pending_locked();
    }
    _table.remove(fd);
  }
  if (has_pending) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "fd=%d unregister pending unread msg; unread blocks are kept until process exit\n",
           fd);
  }
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "fd=%d unregister unread tracking\n", fd);
  delete list;
  for (int i = 0; i < reclaim_items.length(); i++) {
    UBSocketBlkItem item = reclaim_items.at(i);
    UBSocketManager::free_blocks(item.start_blk, item.blk_count);
  }
}

bool UnreadMsgTable::add_msg(int fd, uintptr_t meta_addr, uint32_t start_blk, uint32_t blk_count) {
  UnreadMsgList* list = pin_list(fd);
  if (list == NULL) { return false; }
  {
    MutexLocker list_locker(list->lock());
    list->append(meta_addr, start_blk, blk_count);
  }
  unpin_list(list);
  return true;
}

void UnreadMsgTable::add_pinned_msg(UnreadMsgList* list,
                                    uintptr_t meta_addr,
                                    uint32_t start_blk,
                                    uint32_t blk_count) {
  MutexLocker list_locker(list->lock());
  list->append(meta_addr, start_blk, blk_count);
}

bool UnreadMsgTable::has_pending_msg(int fd) {
  UnreadMsgList* list = NULL;
  {
    MonitorLockerEx table_locker(_unread_table_lock, Mutex::_no_safepoint_check_flag);
    list = pin_list_locked(fd);
    if (list == NULL) { return false; }
  }
  bool has_pending = false;
  {
    MutexLocker list_locker(list->lock());
    has_pending = !list->is_empty();
  }
  unpin_list(list);
  return has_pending;
}

int UnreadMsgTable::process_unread_msgs() {
  GrowableArray<UnreadMsgList*> lists(UB_INIT_ARRAY_CAP, true, mtInternal);
  GrowableArray<UBSocketBlkItem> reclaim_items(UB_INIT_ARRAY_CAP, true, mtInternal);
  GrowableArray<int> drained_fds(UB_INIT_ARRAY_CAP, true, mtInternal);
  GrowableArray<int> fallback_fds(UB_INIT_ARRAY_CAP, true, mtInternal);
  GrowableArray<int> heartbeat_fds(UB_INIT_ARRAY_CAP, true, mtInternal);
  {
    MonitorLockerEx locker(_unread_table_lock, Mutex::_no_safepoint_check_flag);
    _table.begin_iteration();
    UnreadMsgList* list = _table.next();
    while (list != NULL) {
      if (!list->closing()) {
        list->acquire_pin();
        lists.append(list);
      }
      list = _table.next();
    }
  }

  uint64_t now_nanos = os::javaTimeNanos();
  for (int i = 0; i < lists.length(); i++) {
    UnreadMsgList* list = lists.at(i);
    int fd = list->socket_fd();
    {
      MutexLocker list_locker(list->lock());
      bool recv_timeout = false;
      bool read_timeout = false;
      list->process_unread_msgs(now_nanos, &reclaim_items,
                                &recv_timeout, &read_timeout);
      if (list->is_empty()) {
        drained_fds.append(fd);
      } else if (recv_timeout) {
        fallback_fds.append(fd);
      } else if (read_timeout) {
        heartbeat_fds.append(fd);
      }
    }
    unpin_list(list);
  }

  for (int i = 0; i < drained_fds.length(); i++) {
    UBSocketManager::unregister_if_fallback_drained(drained_fds.at(i));
  }
  for (int i = 0; i < reclaim_items.length(); i++) {
    UBSocketBlkItem item = reclaim_items.at(i);
    UBSocketManager::free_blocks(item.start_blk, item.blk_count);
  }
  for (int i = 0; i < fallback_fds.length(); i++) {
    SocketDataInfoTable::request_fallback(fallback_fds.at(i), "recv_timeout");
  }
  for (int i = 0; i < heartbeat_fds.length(); i++) {
    int fd = heartbeat_fds.at(i);
    // After fallback starts, all following bytes belong to the TCP data stream.
    if (SocketDataInfoTable::can_send_frame(fd)) {
      UBSocketManager::send_heartbeat(fd);
    }
  }
  return 0;
}

void UnreadMsgTable::start_timer() {
  {
    MonitorLockerEx locker(_wait_monitor, Mutex::_no_safepoint_check_flag);
    if (_thread != NULL || _timer_starting || !_exited) { return; }
    _timer_starting = true;
    _should_terminate = false;
    _exited = false;
  }

  JavaThread* thread =
      UBSocketThreadUtils::start_daemon(&check_unread_entry, "Check Timer",
                                        NearMaxPriority);

  {
    MonitorLockerEx locker(_wait_monitor, Mutex::_no_safepoint_check_flag);
    _timer_starting = false;
    if (thread == NULL) {
      _exited = true;
      locker.notify_all();
      vm_exit_during_initialization("java.lang.OutOfMemoryError",
                                    "unable to create ub timer thread");
    }
    if (!_exited) { _thread = thread; }
    locker.notify_all();
  }
}

void UnreadMsgTable::check_unread_entry(JavaThread* thread, TRAPS) {
  while (true) {
    {
      // Need state transition ThreadBlockInVM so that this thread
      // will be handled by safepoint correctly when this thread is
      // notified at a safepoint.
      ThreadBlockInVM tbivm(thread);
      MonitorLockerEx locker(_wait_monitor, Mutex::_no_safepoint_check_flag);
      if (!_should_terminate) {
        locker.wait(Mutex::_no_safepoint_check_flag, UB_RECLAIM_POLL_MS);
      }
      if (_should_terminate) { break; }
    }
    process_unread_msgs();
  }
  {
    MonitorLockerEx locker(_wait_monitor, Mutex::_no_safepoint_check_flag);
    _exited = true;
    locker.notify_all();
  }
}

void UnreadMsgTable::stop_timer() {
  if (_wait_monitor == NULL) { return; }

  MonitorLockerEx locker(_wait_monitor, Mutex::_no_safepoint_check_flag);
  if (_thread == NULL && !_timer_starting && _exited) { return; }

  _should_terminate = true;
  locker.notify_all();
  while (_timer_starting || !_exited) {
    locker.wait(Mutex::_no_safepoint_check_flag);
  }
  _thread = NULL;
}

void UnreadMsgTable::cleanup() {
  GrowableArray<int> fds(UB_INIT_ARRAY_CAP, true, mtInternal);
  GrowableArray<UnreadMsgList*> lists(UB_INIT_ARRAY_CAP, true, mtInternal);
  GrowableArray<UBSocketBlkItem> reclaim_items(UB_INIT_ARRAY_CAP, true, mtInternal);
  {
    MonitorLockerEx locker(_unread_table_lock, Mutex::_no_safepoint_check_flag);
    _table.begin_iteration();
    UnreadMsgList* list = _table.next();
    while (list != NULL) {
      fds.append(_table.get_cur_iter_key());
      list->set_closing();
      lists.append(list);
      list = _table.next();
    }
    for (int i = 0; i < lists.length(); i++) {
      while (lists.at(i)->has_active()) {
        locker.wait(Mutex::_no_safepoint_check_flag);
      }
    }
    for (int i = 0; i < lists.length(); i++) {
      MutexLocker list_locker(lists.at(i)->lock());
      lists.at(i)->reclaim_read_blocks_locked(&reclaim_items);
    }
    for (int i = 0; i < fds.length(); i++) {
      _table.remove(fds.at(i));
    }
  }
  for (int i = 0; i < lists.length(); i++) {
    delete lists.at(i);
  }
  for (int i = 0; i < reclaim_items.length(); i++) {
    UBSocketBlkItem item = reclaim_items.at(i);
    UBSocketManager::free_blocks(item.start_blk, item.blk_count);
  }
}

/*************************UBSocketBlkBitmap**************************/

void UBSocketBlkBitmap::init(uint32_t blk_count) {
  _word_count = (blk_count + BITS_PER_WORD - 1) / BITS_PER_WORD;
  _blk_count = blk_count;
  _words = NEW_C_HEAP_ARRAY(uint32_t, _word_count, mtInternal);
  memset((void*)_words, 0, _word_count * sizeof(uint32_t));
  _next_hint = 0;
}

void UBSocketBlkBitmap::cleanup() {
  if (_words != NULL) {
    FREE_C_HEAP_ARRAY(uint32_t, (uint32_t*)_words, mtInternal);
    _words = NULL;
  }
  _word_count = 0;
  _blk_count = 0;
  _next_hint = 0;
}

uint32_t UBSocketBlkBitmap::mask_for_word(uint32_t word_idx, uint32_t start_blk,
                                          uint32_t blk_count) {
  uint32_t word_start = word_idx * BITS_PER_WORD;
  uint32_t word_end = word_start + BITS_PER_WORD;
  uint32_t range_end = start_blk + blk_count;
  uint32_t begin = MAX2(start_blk, word_start);
  uint32_t end = MIN2(range_end, word_end);
  if (begin >= end) return 0;
  uint32_t width = end - begin;
  uint32_t shift = begin - word_start;
  return width == BITS_PER_WORD ? 0xffffffffu : (((1u << width) - 1u) << shift);
}

void UBSocketBlkBitmap::clear_range(uint32_t start_blk, uint32_t blk_count) {
  uint32_t first_word = start_blk / BITS_PER_WORD;
  uint32_t last_word = (start_blk + blk_count - 1) / BITS_PER_WORD;
  for (uint32_t word = first_word; word <= last_word; word++) {
    uint32_t mask = mask_for_word(word, start_blk, blk_count);
    while (mask != 0) {
      uint32_t old_value = _words[word];
      uint32_t new_value = old_value & ~mask;
      if (Atomic::cmpxchg(new_value, &_words[word], old_value) == old_value) {
        break;
      }
    }
  }
}

bool UBSocketBlkBitmap::try_set_range(uint32_t start_blk, uint32_t blk_count) {
  uint32_t first_word = start_blk / BITS_PER_WORD;
  uint32_t last_word = (start_blk + blk_count - 1) / BITS_PER_WORD;

  for (uint32_t word = first_word; word <= last_word; word++) {
    uint32_t mask = mask_for_word(word, start_blk, blk_count);
    while (mask != 0) {
      uint32_t old_value = _words[word];
      if ((old_value & mask) != 0) {
        if (word > first_word) {
          uint32_t rollback_count = word * BITS_PER_WORD - start_blk;
          clear_range(start_blk, rollback_count);
        }
        return false;
      }
      uint32_t new_value = old_value | mask;
      if (Atomic::cmpxchg(new_value, &_words[word], old_value) == old_value) {
        break;
      }
    }
  }
  return true;
}

bool UBSocketBlkBitmap::alloc(uint32_t blk_need, uint32_t* start_blk) {
  if (_words == NULL || blk_need == 0 || blk_need > _blk_count) {
    errno = ENOMEM;
    return false;
  }
  uint32_t limit = _blk_count - blk_need + 1;
  uint32_t hint = MIN2(_next_hint, limit - 1);
  // Circular first-fit scan: try from the last hint to the end
  for (uint32_t blk = hint; blk < limit; blk++) {
    if (!try_set_range(blk, blk_need)) continue;
    Atomic::xchg((blk + blk_need) % _blk_count, &_next_hint);
    if (start_blk != NULL) *start_blk = blk;
    return true;
  }
  for (uint32_t blk = 0; blk < hint; blk++) {
    if (!try_set_range(blk, blk_need)) continue;
    Atomic::xchg((blk + blk_need) % _blk_count, &_next_hint);
    if (start_blk != NULL) *start_blk = blk;
    return true;
  }
  errno = ENOMEM;
  return false;
}

void UBSocketBlkBitmap::release(uint32_t start_blk, uint32_t blk_count) {
  if (_words == NULL || blk_count == 0) return;
  clear_range(start_blk, blk_count);
  Atomic::xchg(start_blk, &_next_hint);
}

/************************UBSocketThreadUtils************************/

JavaThread* UBSocketThreadUtils::start_daemon(ThreadFunction entry, const char* name,
                                              ThreadPriority priority) {
  EXCEPTION_MARK;
  instanceKlassHandle klass(THREAD, SystemDictionary::Thread_klass());
  instanceHandle thread_oop = klass->allocate_instance_handle(THREAD);
  if (HAS_PENDING_EXCEPTION) {
    CLEAR_PENDING_EXCEPTION;
    errno = ENOMEM;
    return NULL;
  }

  Handle thread_name_handle = java_lang_String::create_from_str(name, THREAD);
  if (HAS_PENDING_EXCEPTION) {
    CLEAR_PENDING_EXCEPTION;
    errno = ENOMEM;
    return NULL;
  }

  Handle thread_group(THREAD, Universe::system_thread_group());
  JavaValue result(T_VOID);
  JavaCalls::call_special(&result, thread_oop, klass,
                          vmSymbols::object_initializer_name(),
                          vmSymbols::threadgroup_string_void_signature(),
                          thread_group, thread_name_handle, THREAD);
  if (HAS_PENDING_EXCEPTION) {
    CLEAR_PENDING_EXCEPTION;
    errno = ENOMEM;
    return NULL;
  }

  JavaThread* thread = new JavaThread(entry);
  if (thread == NULL || thread->osthread() == NULL) {
    delete thread;
    errno = ENOMEM;
    return NULL;
  }

  MutexLocker mu(Threads_lock);
  java_lang_Thread::set_thread(thread_oop(), thread);
  if (priority != NoPriority) {
    java_lang_Thread::set_priority(thread_oop(), priority);
  }
  java_lang_Thread::set_daemon(thread_oop());
  thread->set_threadObj(thread_oop());
  Threads::add(thread, true);
  Thread::start(thread);
  return thread;
}

/************************UBSocketEndpointMap**********************/

struct UBSocketSelfPortMapEntry : public CHeapObj<mtInternal> {
  uint16_t public_port;
  uint16_t local_port;
  UBSocketSelfPortMapEntry* next;
};

struct UBSocketRemoteControlEntry : public CHeapObj<mtInternal> {
  UBSocketEndpoint public_data;
  UBSocketEndpoint public_control;
  UBSocketRemoteControlEntry* next;
};

static UBSocketEndpoint ub_socket_self_public_ip;
static UBSocketEndpoint ub_socket_self_local_ip;
static bool ub_socket_has_self_ip_map = false;
static bool ub_socket_self_local_ip_wildcard = false;
static UBSocketSelfPortMapEntry* ub_socket_self_ports = NULL;
static UBSocketRemoteControlEntry* ub_socket_remote_controls = NULL;

static char* ub_socket_conf_trim(char* str) {
  while (*str != '\0' && isspace((unsigned char)*str)) {
    str++;
  }
  char* end = str + strlen(str);
  while (end > str && isspace((unsigned char)*(end - 1))) {
    end--;
  }
  *end = '\0';
  return str;
}

static bool ub_socket_parse_port(const char* str, uint16_t* port) {
  char* end = NULL;
  long value = strtol(str, &end, 10);
  if (end == str || *end != '\0' || value <= 0 || value > 65535) {
    return false;
  }
  *port = (uint16_t)value;
  return true;
}

static bool ub_socket_parse_endpoint(char* value, bool allow_wildcard,
                                     UBSocketEndpoint* endpoint, bool* wildcard) {
  *wildcard = false;
  char* host = value;
  char* port_str = NULL;
  if (*host == '[') {
    char* close = strchr(host, ']');
    if (close == NULL || close[1] != ':') {
      return false;
    }
    *close = '\0';
    host++;
    port_str = close + 2;
  } else {
    char* colon = strrchr(host, ':');
    if (colon == NULL || colon == host || colon[1] == '\0') {
      return false;
    }
    *colon = '\0';
    port_str = colon + 1;
  }

  uint16_t port = 0;
  if (!ub_socket_parse_port(port_str, &port)) {
    return false;
  }

  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->port = port;
  if (strcmp(host, "*") == 0) {
    if (!allow_wildcard) {
      return false;
    }
    *wildcard = true;
    endpoint->family = AF_UNSPEC;
    return true;
  }

  endpoint->family = AF_INET;
  if (inet_pton(AF_INET, host, endpoint->addr) == 1) {
    return true;
  }
  endpoint->family = AF_INET6;
  if (inet_pton(AF_INET6, host, endpoint->addr) == 1) {
    return true;
  }
  return false;
}

static bool ub_socket_parse_ip(char* value, bool allow_wildcard,
                               UBSocketEndpoint* endpoint, bool* wildcard) {
  char endpoint_buf[256];
  jio_snprintf(endpoint_buf, sizeof(endpoint_buf), "%s:1", value);
  return ub_socket_parse_endpoint(endpoint_buf, allow_wildcard, endpoint, wildcard);
}

static bool ub_socket_split_mapping(char* value, char** lhs, char** rhs) {
  char* arrow = strstr(value, "->");
  if (arrow == NULL) {
    return false;
  }
  *arrow = '\0';
  *lhs = ub_socket_conf_trim(value);
  *rhs = ub_socket_conf_trim(arrow + 2);
  return **lhs != '\0' && **rhs != '\0';
}

static bool ub_socket_endpoint_ip_equals(const UBSocketEndpoint* lhs,
                                         const UBSocketEndpoint* rhs) {
  if (lhs->family == rhs->family) {
    return memcmp(lhs->addr, rhs->addr, sizeof(lhs->addr)) == 0;
  }
  if (lhs->family == AF_INET && rhs->family == AF_INET6 &&
      IN6_IS_ADDR_V4MAPPED((const struct in6_addr*)rhs->addr)) {
    return memcmp(lhs->addr, rhs->addr + 12, 4) == 0;
  }
  if (lhs->family == AF_INET6 && rhs->family == AF_INET &&
      IN6_IS_ADDR_V4MAPPED((const struct in6_addr*)lhs->addr)) {
    return memcmp(lhs->addr + 12, rhs->addr, 4) == 0;
  }
  return false;
}

static UBSocketSelfPortMapEntry* ub_socket_find_self_port(uint16_t public_port) {
  for (UBSocketSelfPortMapEntry* port = ub_socket_self_ports;
       port != NULL; port = port->next) {
    if (port->public_port == public_port) {
      return port;
    }
  }
  return NULL;
}

void UBSocketEndpointMap::init() {
  memset(&ub_socket_self_public_ip, 0, sizeof(ub_socket_self_public_ip));
  memset(&ub_socket_self_local_ip, 0, sizeof(ub_socket_self_local_ip));
  ub_socket_has_self_ip_map = false;
  ub_socket_self_local_ip_wildcard = false;
  ub_socket_self_ports = NULL;
  ub_socket_remote_controls = NULL;
}

int UBSocketEndpointMap::load_from_file(const char* conf_path) {
  FILE* conf_file = fopen(conf_path, "r");
  if (conf_file == NULL) {
    return 0;
  }

  enum ConfSection {
    CONF_SECTION_NONE,
    CONF_SECTION_STACK,
    CONF_SECTION_ADDRESS
  };
  ConfSection section = CONF_SECTION_NONE;
  bool has_self_public_ip = false;
  bool has_self_local_ip = false;
  int32_t map_count = 0;
  char line[256];
  int line_no = 0;
  while (fgets(line, sizeof(line), conf_file) != NULL) {
    line_no++;
    line[strcspn(line, "\n")] = '\0';
    char* entry = ub_socket_conf_trim(line);
    if (strcmp(entry, "#stack") == 0) {
      section = CONF_SECTION_STACK;
      continue;
    }
    if (strcmp(entry, "#address") == 0) {
      section = CONF_SECTION_ADDRESS;
      continue;
    }
    if (*entry == '\0' || *entry == '#' || section != CONF_SECTION_ADDRESS) {
      continue;
    }

    char original_entry[256];
    strncpy(original_entry, entry, sizeof(original_entry) - 1);
    original_entry[sizeof(original_entry) - 1] = '\0';

    char* equal = strchr(entry, '=');
    if (equal == NULL) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING, "Ignore invalid endpoint config %s:%d: %s\n",
             conf_path, line_no, original_entry);
      continue;
    }
    *equal = '\0';
    char* key = ub_socket_conf_trim(entry);
    char* value = ub_socket_conf_trim(equal + 1);

    if (strcmp(key, "self.ip") == 0) {
      char* public_ip = NULL;
      char* local_ip = NULL;
      bool public_wildcard = false;
      bool local_wildcard = false;
      if (!ub_socket_split_mapping(value, &public_ip, &local_ip) ||
          !ub_socket_parse_ip(public_ip, false, &ub_socket_self_public_ip,
                              &public_wildcard) ||
          !ub_socket_parse_ip(local_ip, true, &ub_socket_self_local_ip,
                              &local_wildcard)) {
        UB_LOG(UB_SOCKET, UB_LOG_WARNING, "Ignore invalid self ip mapping %s:%d: %s\n",
               conf_path, line_no, original_entry);
        continue;
      }
      has_self_public_ip = true;
      has_self_local_ip = true;
      ub_socket_self_local_ip_wildcard = local_wildcard;
      map_count++;
      UB_LOG(UB_SOCKET, UB_LOG_INFO, "Load self ip map\n");
    } else if (strcmp(key, "self.port") == 0) {
      char* public_port_str = NULL;
      char* local_port_str = NULL;
      uint16_t public_port = 0;
      uint16_t local_port = 0;
      if (!ub_socket_split_mapping(value, &public_port_str, &local_port_str) ||
          !ub_socket_parse_port(public_port_str, &public_port) ||
          !ub_socket_parse_port(local_port_str, &local_port)) {
        UB_LOG(UB_SOCKET, UB_LOG_WARNING, "Ignore invalid self port mapping %s:%d: %s\n",
               conf_path, line_no, original_entry);
        continue;
      }
      UBSocketSelfPortMapEntry* self_port = new UBSocketSelfPortMapEntry();
      self_port->public_port = public_port;
      self_port->local_port = local_port;
      self_port->next = ub_socket_self_ports;
      ub_socket_self_ports = self_port;
      map_count++;
      UB_LOG(UB_SOCKET, UB_LOG_INFO,
             "Load self port map public_port=%u local_port=%u\n",
             public_port, local_port);
    } else if (strcmp(key, "remote.mapping") == 0) {
      char* public_data = NULL;
      char* public_control = NULL;
      UBSocketRemoteControlEntry* remote = new UBSocketRemoteControlEntry();
      bool wildcard = false;
      if (!ub_socket_split_mapping(value, &public_data, &public_control) ||
          !ub_socket_parse_endpoint(public_data, false, &remote->public_data, &wildcard) ||
          !ub_socket_parse_endpoint(public_control, false, &remote->public_control, &wildcard)) {
        delete remote;
        UB_LOG(UB_SOCKET, UB_LOG_WARNING, "Ignore invalid remote mapping %s:%d: %s\n",
               conf_path, line_no, original_entry);
        continue;
      }
      remote->next = ub_socket_remote_controls;
      ub_socket_remote_controls = remote;
      map_count++;
      UB_LOG(UB_SOCKET, UB_LOG_INFO,
             "Load remote endpoint map data_port=%u control_port=%u\n",
             remote->public_data.port, remote->public_control.port);
    } else {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING, "Ignore unknown endpoint config %s:%d: %s\n",
             conf_path, line_no, original_entry);
    }
  }

  if (has_self_public_ip && has_self_local_ip && ub_socket_self_ports != NULL) {
    ub_socket_has_self_ip_map = true;
  } else if (has_self_public_ip || has_self_local_ip || ub_socket_self_ports != NULL) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "Ignore incomplete self endpoint map public_ip=%d local_ip=%d port=%d\n",
           has_self_public_ip ? 1 : 0, has_self_local_ip ? 1 : 0,
           ub_socket_self_ports != NULL ? 1 : 0);
    ub_socket_has_self_ip_map = false;
  }
  if (fclose(conf_file) != 0) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fclose %s failed: %s\n",
           conf_path, strerror(errno));
  }
  return map_count;
}

void UBSocketEndpointMap::cleanup() {
  UBSocketSelfPortMapEntry* self_port = ub_socket_self_ports;
  while (self_port != NULL) {
    UBSocketSelfPortMapEntry* next = self_port->next;
    delete self_port;
    self_port = next;
  }
  ub_socket_self_ports = NULL;
  ub_socket_has_self_ip_map = false;
  UBSocketRemoteControlEntry* remote = ub_socket_remote_controls;
  while (remote != NULL) {
    UBSocketRemoteControlEntry* next = remote->next;
    delete remote;
    remote = next;
  }
  ub_socket_remote_controls = NULL;
}

bool UBSocketEndpointMap::control_endpoint_for_data(const UBSocketEndpoint* data_ep,
                                                    UBSocketEndpoint* control_ep) {
  for (UBSocketRemoteControlEntry* remote = ub_socket_remote_controls;
       remote != NULL; remote = remote->next) {
    if (!ub_socket_endpoint_equals(&remote->public_data, data_ep)) {
      continue;
    }
    *control_ep = remote->public_control;
    return true;
  }
  return false;
}

bool UBSocketEndpointMap::has_mapping_for_data(const UBSocketEndpoint* data_ep) {
  return ub_socket_has_self_ip_map &&
         ub_socket_endpoint_ip_equals(&ub_socket_self_public_ip, data_ep) &&
         ub_socket_find_self_port(data_ep->port) != NULL;
}

bool UBSocketEndpointMap::matches_local_data(const UBSocketEndpoint* request_ep,
                                             const UBSocketEndpoint* session_ep) {
  UBSocketSelfPortMapEntry* port_map = NULL;
  if (!ub_socket_has_self_ip_map ||
      !ub_socket_endpoint_ip_equals(&ub_socket_self_public_ip, request_ep) ||
      (port_map = ub_socket_find_self_port(request_ep->port)) == NULL) {
    return ub_socket_endpoint_equals(request_ep, session_ep);
  }
  if (port_map->local_port != session_ep->port) {
    return false;
  }
  if (ub_socket_self_local_ip_wildcard) {
    return true;
  }
  return ub_socket_endpoint_ip_equals(&ub_socket_self_local_ip, session_ep);
}

/************************UBSocketSessionCaches**********************/

void UBSocketSessionCaches::init() {
  _cache_lock = new Mutex(Mutex::leaf, "UBSocketSessionCaches_lock");
  _cache_head = NULL;
}

void UBSocketSessionCaches::add(UBSocketAttachSession* session) {
  MutexLockerEx locker(_cache_lock, Mutex::_no_safepoint_check_flag);
  session->set_next(_cache_head);
  _cache_head = session;
}

UBSocketAttachSession* UBSocketSessionCaches::find(const UBSocketEndpoint* local_ep,
                                                   const UBSocketEndpoint* remote_ep) {
  MutexLockerEx locker(_cache_lock, Mutex::_no_safepoint_check_flag);
  UBSocketAttachSession* cur = _cache_head;
  while (cur != NULL) {
    if (cur->matches(local_ep, remote_ep)) {
      return cur->try_pin() ? cur : NULL;
    }
    cur = cur->next();
  }
  return NULL;
}

UBSocketAttachSession* UBSocketSessionCaches::find(const UBSocketAttachFrame* request) {
  MutexLockerEx locker(_cache_lock, Mutex::_no_safepoint_check_flag);
  UBSocketAttachSession* matched = NULL;
  int32_t match_count = 0;
  for (UBSocketAttachSession* cur = _cache_head; cur != NULL; cur = cur->next()) {
    if (!cur->matches_request(request)) {
      continue;
    }
    matched = cur;
    match_count++;
  }
  if (match_count != 1) {
    if (match_count > 1) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "ambiguous attach request local_port=%u remote_port=%u matches=%d\n",
             request->local_endpoint.port, request->remote_endpoint.port, match_count);
    }
    return NULL;
  }
  return matched->try_pin() ? matched : NULL;
}

void UBSocketSessionCaches::release(UBSocketAttachSession* session) {
  if (session != NULL) { session->unpin(); }
}

void UBSocketSessionCaches::remove(const UBSocketEndpoint* local_ep,
                                   const UBSocketEndpoint* remote_ep) {
  UBSocketAttachSession* removed = NULL;
  {
    MutexLockerEx locker(_cache_lock, Mutex::_no_safepoint_check_flag);
    UBSocketAttachSession* prev = NULL;
    UBSocketAttachSession* cur = _cache_head;
    while (cur != NULL) {
      if (cur->matches(local_ep, remote_ep)) {
        if (prev == NULL) {
          _cache_head = cur->next();
        } else {
          prev->set_next(cur->next());
        }
        removed = cur;
        break;
      }
      prev = cur;
      cur = cur->next();
    }
  }
  if (removed != NULL) {
    removed->close_and_wait();
    delete removed;
  }
}

void UBSocketSessionCaches::cleanup() {
  GrowableArray<UBSocketAttachSession*> sessions(UB_INIT_ARRAY_CAP,
                                                 true, mtInternal);
  {
    MutexLockerEx locker(_cache_lock, Mutex::_no_safepoint_check_flag);
    UBSocketAttachSession* cur = _cache_head;
    while (cur != NULL) {
      sessions.append(cur);
      cur = cur->next();
    }
    _cache_head = NULL;
  }
  for (int i = 0; i < sessions.length(); i++) {
    UBSocketAttachSession* session = sessions.at(i);
    session->close_and_wait();
    delete session;
  }
}

/************************UBSocketEarlyReqQueue**********************/

struct UBSocketEarlyReqQueue::EarlyRequest : public CHeapObj<mtInternal> {
  int control_fd;
  uint64_t ddl_ns;
  UBSocketAttachFrame request;
  EarlyRequest* next;

  EarlyRequest(int control_fd, const UBSocketAttachFrame* request, uint64_t ddl_ns)
      : control_fd(control_fd), ddl_ns(ddl_ns), request(*request), next(NULL) {}
};

static bool ub_socket_attach_request_equals(const UBSocketAttachFrame* lhs,
                                            const UBSocketAttachFrame* rhs) {
  return lhs->kind == rhs->kind &&
         lhs->request_id == rhs->request_id &&
         strncmp(lhs->mem_name, rhs->mem_name, UB_SOCKET_MEM_NAME_BUF_LEN) == 0 &&
         ub_socket_endpoint_equals(&lhs->local_endpoint, &rhs->local_endpoint) &&
         ub_socket_endpoint_equals(&lhs->remote_endpoint, &rhs->remote_endpoint);
}

bool UBSocketEarlyReqQueue::cache(int control_fd, const UBSocketAttachFrame* request,
                                  uint64_t ddl_ns) {
  EarlyRequest* new_entry = new EarlyRequest(control_fd, request, ddl_ns);
  int old_fd = -1;
  if (new_entry == NULL) { return false; }

  int entry_count = 0;
  for (EarlyRequest* entry = _head; entry != NULL; entry = entry->next) {
    entry_count++;
    if (!ub_socket_attach_request_equals(&entry->request, request)) { continue; }
    old_fd = entry->control_fd;
    entry->control_fd = control_fd;
    entry->ddl_ns = ddl_ns;
    entry->request = *request;
    delete new_entry;
    if (old_fd >= 0) {
      close(old_fd);
    }
    return true;
  }
  if (entry_count >= UB_SOCKET_EARLY_REQUEST_MAX) {
    delete new_entry;
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "early request queue full limit=%d control_fd=%d\n",
           UB_SOCKET_EARLY_REQUEST_MAX, control_fd);
    return false;
  }
  if (_tail == NULL) {
    _head = _tail = new_entry;
  } else {
    _tail->next = new_entry;
    _tail = new_entry;
  }
  return true;
}

int UBSocketEarlyReqQueue::count() {
  int count = 0;
  for (EarlyRequest* entry = _head; entry != NULL; entry = entry->next) {
    count++;
  }
  return count;
}

bool UBSocketEarlyReqQueue::take_one(int* control_fd, UBSocketAttachFrame* request,
                                     uint64_t* ddl_ns) {
  if (_head == NULL) { return false; }
  EarlyRequest* entry = _head;
  _head = entry->next;
  if (_head == NULL) { _tail = NULL; }
  *control_fd = entry->control_fd;
  *request = entry->request;
  *ddl_ns = entry->ddl_ns;
  delete entry;
  return true;
}

void UBSocketEarlyReqQueue::cleanup() {
  while (_head != NULL) {
    EarlyRequest* next = _head->next;
    close(_head->control_fd);
    delete _head;
    _head = next;
  }
  _tail = NULL;
}
