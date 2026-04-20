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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketAttachSession.hpp"
#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/ubSocket/ubSocket.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"

#include "matrix/ubSocket/ubSocketUtils.hpp"

static const size_t SOCKET_DESCRIPTOR_BUFFER_INITIAL_CAPACITY = 256;
static const size_t SOCKET_DESCRIPTOR_BUFFER_GROWTH_FACTOR = 2;

/***************************static fields****************************/

SocketDataInfoTable* SocketDataInfoTable::_instance = NULL;
Monitor* SocketDataInfoTable::_table_lock = NULL;
SocketBufferTable* SocketBufferTable::_instance = NULL;
Monitor* SocketBufferTable::_table_lock = NULL;
UnreadMsgTable* UnreadMsgTable::_instance = NULL;

volatile bool UnreadMsgTable::_should_terminate = false;
Monitor* UnreadMsgTable::_wait_monitor = NULL;
Monitor* UnreadMsgTable::_table_lock = NULL;

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

/**************************UBSocketInfoList*************************/

void UBSocketInfoList::append(size_t offset, size_t size) {
  SocketListNode* newNode = new SocketListNode(offset, size, NULL);
  if (_head == NULL) {
    _head = _tail = _cursor = newNode;
  } else {
    _tail->next = newNode;
    _tail = newNode;
  }
}

size_t UBSocketInfoList::read_data(void* dst, size_t len) {
  if (len == 0) return 0;
  if (_cursor == NULL) { return 0; }

  uintptr_t read_addr = (uintptr_t)_mem_addr + _cursor->offset + _cur_loc;
  if (len + _cur_loc <= _cursor->size) {
    memcpy(dst, (void*)read_addr, len);
    _cur_loc += len;
    if (_cur_loc ==
        _cursor->size) {  // in case the last blk and no more read op
      uintptr_t block_need_clean = (uintptr_t)_mem_addr + _cursor->offset;
      UBSocketManager::clean_mark(block_need_clean);
    }
    return len;
  } else {
    size_t cur_cursor_remain = _cursor->size - _cur_loc;
    if (cur_cursor_remain > 0) {
      memcpy(dst, (void*)read_addr, cur_cursor_remain);
      _cur_loc += cur_cursor_remain;
      uintptr_t block_need_clean = (uintptr_t)_mem_addr + _cursor->offset;
      UBSocketManager::clean_mark(block_need_clean);
    }
    if (_cursor->next == NULL) {
      return cur_cursor_remain;
    }
    uintptr_t next_dst = (uintptr_t)dst + cur_cursor_remain;
    _cursor = _cursor->next;
    delete_nodes(_head, _cursor);  // delete nodes before new cursor
    _head = _cursor;
    _cur_loc = 0;
    return cur_cursor_remain +
           read_data((void*)next_dst, len - cur_cursor_remain);
  }
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

/*************************SocketDataInfoTable***********************/

void SocketDataInfoTable::init() {
  guarantee(_instance == NULL, "must be");
  _instance = new SocketDataInfoTable();
  _table_lock = new Monitor(Mutex::leaf, "SocketDataInfoTable_lock");
}

void SocketDataInfoTable::publish(int fd, UBSocketInfoList* info) {
  MutexLocker locker(_table_lock);
  guarantee(_table.get(fd) == NULL, "must be");
  _table.add(fd, info);
}

bool SocketDataInfoTable::contains(int fd) {
  MutexLocker locker(_table_lock);
  return _table.get(fd) != NULL;
}

bool SocketDataInfoTable::append_range(int fd, size_t offset, size_t size) {
  MutexLocker locker(_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  if (info == NULL) {
    return false;
  }
  info->append(offset, size);
  return true;
}

size_t SocketDataInfoTable::read_data(int fd, void* dst, size_t len) {
  MutexLocker locker(_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  if (info == NULL) {
    return 0;
  }
  return info->read_data(dst, len);
}

UBSocketInfoList* SocketDataInfoTable::detach(int fd) {
  MutexLocker locker(_table_lock);
  UBSocketInfoList* info = _table.get(fd);
  if (info != NULL) {
    _table.remove(fd);
  }
  return info;
}

int SocketDataInfoTable::unregister_abnormal_fds() {
  int count = 0;
  SimpleList<int, mtInternal> fds(-1);
  {
    MutexLocker locker(_table_lock);
    _table.begin_iteration();
    UBSocketInfoList* info = _table.next();
    while (info != NULL) {
      fds.append(info->get_socket_fd());
      info = _table.next();
    }
  }

  fds.begin_iteration();
  int fd = fds.next();
  while (fd != -1) {
    UBSocketManager::unregister_fd(fd);
    count++;
    fd = fds.next();
  }
  return count;
}

/************************SocketDescriptorBuffer**********************/

bool SocketDescriptorBuffer::append(const char* msg, size_t len) {
  if (len == 0) {
    return true;
  }
  size_t required = _len + len;
  if (required > _cap) {
    size_t new_cap = _cap == 0 ? SOCKET_DESCRIPTOR_BUFFER_INITIAL_CAPACITY : _cap;
    while (new_cap < required) {
      new_cap *= SOCKET_DESCRIPTOR_BUFFER_GROWTH_FACTOR;
    }
    char* new_data = (char*)malloc(new_cap);
    if (new_data == NULL) { return false; }
    if (_data != NULL && _len > 0) {
      memcpy(new_data, _data, _len);
    }
    free(_data);
    _data = new_data;
    _cap = new_cap;
  }
  memcpy(_data + _len, msg, len);
  _len += len;
  return true;
}

void SocketDescriptorBuffer::consume(size_t len) {
  if (len >= _len) {
    _len = 0;
    return;
  }
  memmove(_data, _data + len, _len - len);
  _len -= len;
}

/*************************SocketBufferTable**************************/

bool SocketBufferTable::extract_complete_descriptors(int fd, const char* msg, size_t len,
                                                     char** descriptors,
                                                     size_t* descriptors_len) {
  *descriptors = NULL;
  *descriptors_len = 0;

  MutexLocker locker(_table_lock);
  SocketDescriptorBuffer* buffer = PtrTable<int, SocketDescriptorBuffer*, mtInternal>::get(fd);
  if (buffer == NULL) {
    buffer = new SocketDescriptorBuffer();
    PtrTable<int, SocketDescriptorBuffer*, mtInternal>::add(fd, buffer);
  }
  if (!buffer->append(msg, len)) {
    return false;
  }

  char* data = buffer->data();
  size_t available = buffer->len();
  size_t ready_len = 0;
  size_t consumed = 0;
  while (consumed < available) {
    const char* end_pos = (const char*)memchr(data + consumed, ';', available - consumed);
    if (end_pos == NULL) {
      break;
    }
    ready_len = (size_t)(end_pos - data) + 1;
    consumed = ready_len;
  }

  if (ready_len == 0) {
    return true;
  }

  char* out = (char*)malloc(ready_len);
  if (out == NULL) {
    return false;
  }
  memcpy(out, data, ready_len);
  buffer->consume(ready_len);
  if (buffer->len() == 0) {
    PtrTable<int, SocketDescriptorBuffer*, mtInternal>::remove(fd);
    delete buffer;
  }

  *descriptors = out;
  *descriptors_len = ready_len;
  return true;
}

/***************************UnreadMsgList***************************/

void UnreadMsgList::append(void* addr) {
  UnreadMsgNode* newNode = new UnreadMsgNode(addr, NULL);
  _tail->next = newNode;
  _tail = newNode;
}

int UnreadMsgList::check_timeout() {
  UnreadMsgNode* end = _tail;  // ending of this turn
  if (_head == end) return 0;
  int timeout_cnt = 0;
  uint64_t now_nanos = os::javaTimeNanos();
  UnreadMsgNode* prev = _head;
  UnreadMsgNode* cur = _head->next;

  while (cur != NULL) {
    bool is_ending = (cur == end);
    uint64_t send_time = 0;
    if (cur->addr != NULL) {
      memcpy(&send_time, cur->addr, sizeof(uint64_t));
    }
    if (send_time == 0) {
      UnreadMsgNode* need_del = cur;
      prev->next = cur->next;
      if (need_del == _tail) {
        // update tail if deleting last node
        _tail = prev;
      }
      cur = cur->next;
      delete need_del;
    } else {
      uint64_t expired_ns = now_nanos - send_time;
      if (expired_ns > UBSocketManager::package_timeout) {
        ++timeout_cnt;
        UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d timeout msg=%p expired_ns=%ld count=%d\n",
               _socket_fd, cur->addr, expired_ns, timeout_cnt);
      }
      prev = cur;
      cur = cur->next;
    }
    if (is_ending) break;  // stop this loop
  }
  return timeout_cnt;
}

int UnreadMsgTable::check_timeout() {
  int timeout_count = 0;
  SimpleList<int, mtInternal> heartbeat_fds(-1);
  {
    MutexLocker locker(_table_lock);
    begin_iteration();
    UnreadMsgList* list = next();
    while (list != NULL) {
      int list_timeout = list->check_timeout();
      timeout_count += list_timeout;
      if (list_timeout > 0) {
        heartbeat_fds.append(list->socket_fd());
      }
      list = next();
    }
  }

  heartbeat_fds.begin_iteration();
  int fd = heartbeat_fds.next();
  while (fd != -1) {
    UBSocketManager::send_heartbeat(fd);
    fd = heartbeat_fds.next();
  }
  return timeout_count;
}

void UnreadMsgTable::start_timer() {
  if (_thread != NULL)  // timer has been running
    return;
  _wait_monitor = new Monitor(Mutex::safepoint - 1, "UBSocketCheckMonitor");

  JavaThread* thread =
      UBSocketThreadUtils::start_daemon(&check_unread_entry, "Check Timer",
                                        NearMaxPriority);
  if (thread == NULL) {
    vm_exit_during_initialization("java.lang.OutOfMemoryError",
                                  "unable to create ub timer thread");
  }
  _thread = thread;
}

void UnreadMsgTable::check_unread_entry(JavaThread* thread, TRAPS) {
  while (!_should_terminate) {
    {
      // Need state transition ThreadBlockInVM so that this thread
      // will be handled by safepoint correctly when this thread is
      // notified at a safepoint.
      ThreadBlockInVM tbivm(thread);
      MonitorLockerEx locker(_wait_monitor, Mutex::_no_safepoint_check_flag);
      locker.wait(Mutex::_no_safepoint_check_flag, UBSocketTimeout);
    }
    instance()->check_timeout();
  }
}

void UnreadMsgTable::stop_timer() {
  if (_thread == NULL) return;
  _should_terminate = true;
  if (_wait_monitor != NULL) {
    MonitorLockerEx locker(_wait_monitor, Mutex::_no_safepoint_check_flag);
    _wait_monitor->notify();
  }
}

void UnreadMsgTable::cleanup() {
  MutexLocker locker(_table_lock);
  for (int fd = 0; fd < MATRIX_TABLE_SIZE; fd++) {
    UnreadMsgList* list = get(fd);
    if (list != NULL) {
      remove(fd);
      delete list;
    }
  }
}

/************************UBSocketAttachSessionTable**********************/

class UBSocketAttachSessionTableState : public CHeapObj<mtInternal> {
 public:
  Monitor* lock;
  UBSocketAttachSession* head;

  UBSocketAttachSessionTableState()
    : lock(new Monitor(Mutex::leaf, "UBSocketAttachSessionTable_lock")), head(NULL) {}
};

static UBSocketAttachSessionTableState* g_attach_session_table = NULL;

static UBSocketAttachSessionTableState* attach_session_table() {
  if (g_attach_session_table == NULL) {
    g_attach_session_table = new UBSocketAttachSessionTableState();
  }
  return g_attach_session_table;
}

static UBSocketAttachSession* find_locked(UBSocketAttachSessionTableState* state,
                                          const UBSocketEndpoint* local_ep,
                                          const UBSocketEndpoint* remote_ep) {
  UBSocketAttachSession* cur = state->head;
  while (cur != NULL) {
    if (cur->matches(local_ep, remote_ep)) {
      return cur;
    }
    cur = cur->next();
  }
  return NULL;
}

bool UBSocketAttachSessionTable::add(UBSocketAttachSession* session) {
  UBSocketAttachSessionTableState* state = attach_session_table();
  MonitorLockerEx locker(state->lock, Mutex::_no_safepoint_check_flag);
  session->set_next(state->head);
  state->head = session;
  return true;
}

UBSocketAttachSession* UBSocketAttachSessionTable::find(const UBSocketEndpoint* local_ep,
                                                        const UBSocketEndpoint* remote_ep) {
  UBSocketAttachSessionTableState* state = attach_session_table();
  MonitorLockerEx locker(state->lock, Mutex::_no_safepoint_check_flag);
  return find_locked(state, local_ep, remote_ep);
}

void UBSocketAttachSessionTable::remove(const UBSocketEndpoint* local_ep,
                                        const UBSocketEndpoint* remote_ep) {
  UBSocketAttachSessionTableState* state = attach_session_table();
  MonitorLockerEx locker(state->lock, Mutex::_no_safepoint_check_flag);
  UBSocketAttachSession* prev = NULL;
  UBSocketAttachSession* cur = state->head;
  while (cur != NULL) {
    if (cur->matches(local_ep, remote_ep)) {
      if (prev == NULL) {
        state->head = cur->next();
      } else {
        prev->set_next(cur->next());
      }
      delete cur;
      return;
    }
    prev = cur;
    cur = cur->next();
  }
}

void UBSocketAttachSessionTable::cleanup() {
  UBSocketAttachSessionTableState* state = attach_session_table();
  MonitorLockerEx locker(state->lock, Mutex::_no_safepoint_check_flag);
  while (state->head != NULL) {
    UBSocketAttachSession* next = state->head->next();
    delete state->head;
    state->head = next;
  }
}

/************************UBSocketEarlyRequestQueue**********************/

struct UBSocketEarlyRequest : public CHeapObj<mtInternal> {
  int control_fd;
  uint64_t ddl_ns;
  UBSocketControlFrame request;
  UBSocketEarlyRequest* next;
};

class UBSocketEarlyRequestQueueState : public CHeapObj<mtInternal> {
 public:
  Monitor* lock;
  UBSocketEarlyRequest* head;
  UBSocketEarlyRequest* tail;

  UBSocketEarlyRequestQueueState()
    : lock(new Monitor(Mutex::leaf, "UBSocketEarlyRequestQueue_lock")), head(NULL), tail(NULL) {}
};

static UBSocketEarlyRequestQueueState* g_early_request_queue = NULL;

static UBSocketEarlyRequestQueueState* early_request_queue() {
  if (g_early_request_queue == NULL) {
    g_early_request_queue = new UBSocketEarlyRequestQueueState();
  }
  return g_early_request_queue;
}

bool UBSocketEarlyRequestQueue::cache(int control_fd, const UBSocketControlFrame* request,
                                      uint64_t ddl_ns) {
  UBSocketEarlyRequestQueueState* state = early_request_queue();
  UBSocketEarlyRequest* new_entry = (UBSocketEarlyRequest*)calloc(1, sizeof(*new_entry));
  int old_fd = -1;
  if (new_entry == NULL) {
    errno = ENOMEM;
    return false;
  }

  new_entry->control_fd = control_fd;
  new_entry->ddl_ns = ddl_ns;
  new_entry->request = *request;

  {
    MonitorLockerEx locker(state->lock, Mutex::_no_safepoint_check_flag);
    for (UBSocketEarlyRequest* entry = state->head; entry != NULL; entry = entry->next) {
      if (!ub_frame_request_equals(&entry->request, request)) {
        continue;
      }
      old_fd = entry->control_fd;
      entry->control_fd = control_fd;
      entry->ddl_ns = ddl_ns;
      entry->request = *request;
      free(new_entry);
      if (old_fd >= 0) {
        close(old_fd);
      }
      return true;
    }
    new_entry->next = NULL;
    if (state->tail == NULL) {
      state->head = state->tail = new_entry;
    } else {
      state->tail->next = new_entry;
      state->tail = new_entry;
    }
  }
  return true;
}

bool UBSocketEarlyRequestQueue::has_requests() {
  UBSocketEarlyRequestQueueState* state = early_request_queue();
  MonitorLockerEx locker(state->lock, Mutex::_no_safepoint_check_flag);
  return state->head != NULL;
}

int UBSocketEarlyRequestQueue::count() {
  UBSocketEarlyRequestQueueState* state = early_request_queue();
  MonitorLockerEx locker(state->lock, Mutex::_no_safepoint_check_flag);
  int count = 0;
  for (UBSocketEarlyRequest* entry = state->head; entry != NULL; entry = entry->next) {
    count++;
  }
  return count;
}

bool UBSocketEarlyRequestQueue::take_one(int* control_fd, UBSocketControlFrame* request,
                                         uint64_t* ddl_ns) {
  UBSocketEarlyRequestQueueState* state = early_request_queue();
  MonitorLockerEx locker(state->lock, Mutex::_no_safepoint_check_flag);
  if (state->head == NULL) {
    return false;
  }
  UBSocketEarlyRequest* entry = state->head;
  state->head = entry->next;
  if (state->head == NULL) {
    state->tail = NULL;
  }
  *control_fd = entry->control_fd;
  *request = entry->request;
  *ddl_ns = entry->ddl_ns;
  free(entry);
  return true;
}

void UBSocketEarlyRequestQueue::cleanup() {
  UBSocketEarlyRequestQueueState* state = early_request_queue();
  MonitorLockerEx locker(state->lock, Mutex::_no_safepoint_check_flag);
  while (state->head != NULL) {
    UBSocketEarlyRequest* next = state->head->next;
    close(state->head->control_fd);
    free(state->head);
    state->head = next;
  }
  state->tail = NULL;
}
