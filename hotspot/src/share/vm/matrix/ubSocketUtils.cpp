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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixManager.hpp"
#include "matrix/ubSocket.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"

/***************************static fields****************************/

SocketDataInfoTable* SocketDataInfoTable::_instance = NULL;
SocketNameAddrTable* SocketNameAddrTable::_instance = NULL;
SocketBufferTable* SocketBufferTable::_instance = NULL;
UnreadMsgTable* UnreadMsgTable::_instance = NULL;

volatile bool UnreadMsgTable::_should_terminate = false;
Monitor* UnreadMsgTable::_wait_monitor = NULL;

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
  if (_cursor == NULL) {
    UB_LOG("DEBUG", "UB Socket read len %ld from %d, but no more data\n", len,
           _socket_fd);
    return 0;
  }

  uintptr_t read_addr = (uintptr_t)_mem_addr + _cursor->offset + _cur_loc;
  if (len + _cur_loc <= _cursor->size) {
    memcpy(dst, (void*)read_addr, len);
    UB_LOG("DEBUG",
           "UB Socket read len %ld from %p to %p, _cur_loc %ld cursor off %ld "
           "size %ld\n",
           len, (void*)read_addr, dst, _cur_loc, _cursor->offset,
           _cursor->size);
    _cur_loc += len;
    if (_cur_loc ==
        _cursor->size) {  // in case the last blk and no more read op
      uintptr_t block_need_clean = (uintptr_t)_mem_addr + _cursor->offset;
      UBSocketManager::clean_mark(block_need_clean);
    }
    return len;
  } else {
    // need read current & next cursor
    size_t cur_cursor_remain = _cursor->size - _cur_loc;
    if (cur_cursor_remain > 0) {
      memcpy(dst, (void*)read_addr, cur_cursor_remain);
      UB_LOG("DEBUG",
             "UB Socket read len %ld from %p to %p, _cur_loc %ld cursor off "
             "%ld size %ld next %p\n",
             cur_cursor_remain, (void*)read_addr, dst, _cur_loc,
             _cursor->offset, _cursor->size, _cursor->next);
      _cur_loc += cur_cursor_remain;
      uintptr_t block_need_clean = (uintptr_t)_mem_addr + _cursor->offset;
      UBSocketManager::clean_mark(block_need_clean);
    }
    if (_cursor->next == NULL) {
      // no more data
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

/*************************SocketBufferTable**************************/

int SocketBufferTable::read_buffer(int fd, char* msg) {
  char* buffer = get(fd);
  guarantee(buffer != NULL, "must be");
  char error_msg[128];
  jio_snprintf(error_msg, sizeof(error_msg), "fd %d buffer %p(%s) to msg %p",
               fd, buffer, buffer, error_msg);
  guarantee(strlen(buffer) > 0, error_msg);
  int msg_len = strlen(buffer);
  strncpy(msg, buffer, msg_len);
  memset(buffer, '\0', msg_len);
  return msg_len;
}

void SocketBufferTable::update_buffer(int fd, char* msg) {
  char* buffer = get(fd);
  if (buffer == NULL) {
    buffer = (char*)malloc(_buffer_len);
    memset(buffer, '\0', _buffer_len);
    add(fd, buffer);
  }
  guarantee(strlen(buffer) == 0, "must be");
  strncpy(buffer, msg, strlen(msg));
}

void SocketBufferTable::free_buffer(int fd) {
  char* buffer = get(fd);
  guarantee(buffer != NULL, "must be");
  // if server dont care client msg?
  guarantee(strlen(buffer) == 0, "must be");
  free(buffer);
  remove(fd);
}

/***************************UnreadMsgList***************************/

void UnreadMsgList::append(void* addr) {
  UnreadMsgNode* newNode = new UnreadMsgNode(addr, NULL);
  pthread_mutex_lock(&_mutex);
  _tail->next = newNode;
  _tail = newNode;
  pthread_mutex_unlock(&_mutex);
}

int UnreadMsgList::check_timeout() {
  pthread_mutex_lock(&_mutex);
  UnreadMsgNode* end = _tail;  // ending of this turn
  pthread_mutex_unlock(&_mutex);

  if (_head == end) return 0;
  int timeout_cnt = 0;
  uint64_t now_nanos = os::javaTimeNanos();
  UnreadMsgNode* prev = _head;
  UnreadMsgNode* cur = _head->next;

  while (cur != NULL) {
    bool is_ending = (cur == end);
    uint64_t send_time = 0;
    memcpy(&send_time, cur->addr, sizeof(uint64_t));
    if (send_time == 0) {
      pthread_mutex_lock(&_mutex);
      UnreadMsgNode* need_del = cur;
      prev->next = cur->next;
      if (need_del == _tail) {
        // update tail if deleting last node
        _tail = prev;
      }
      cur = cur->next;
      delete need_del;
      pthread_mutex_unlock(&_mutex);
    } else {
      uint64_t expired_ns = now_nanos - send_time;
      if (expired_ns > UBSocketManager::package_timeout) {
        ++timeout_cnt;
        UB_LOG("WARNING",
               "UB Socket %d exists timeout, %d msg %p expired %ld ns\n",
               _socket_fd, timeout_cnt, cur->addr, expired_ns);
      }
      prev = cur;
      cur = cur->next;
    }
    if (is_ending) break;  // stop this loop
  }
  if (timeout_cnt > 0) {
    UBSocketManager::send_heartbeat(_socket_fd);
  }
  return timeout_cnt;
}

int UnreadMsgTable::check_timeout() {
  int timeout_count = 0;
  begin_iteration();
  UnreadMsgList* list = next();
  while (list != NULL) {
    timeout_count += list->check_timeout();
    list = next();
  }
  return timeout_count;
}

void UnreadMsgTable::start_timer() {
  if (_thread != NULL)  // timer has been running
    return;
  _wait_monitor = new Monitor(Mutex::safepoint - 1, "UBSocketCheckMonitor");

  EXCEPTION_MARK;
  instanceKlassHandle klass(THREAD, SystemDictionary::Thread_klass());
  instanceHandle thread_oop = klass->allocate_instance_handle(CHECK);
  Handle string =
      java_lang_String::create_from_str("UB Socket Check Timer", CATCH);
  Handle thread_group(THREAD, Universe::system_thread_group());
  // Initialize thread_oop to put it into the system threadGroup
  JavaValue result(T_VOID);
  JavaCalls::call_special(&result, thread_oop, klass,
                          vmSymbols::object_initializer_name(),
                          vmSymbols::threadgroup_string_void_signature(),
                          thread_group, string, CHECK);
  {
    MutexLocker mu(Threads_lock);

    JavaThread* thread = new JavaThread(&check_unread_entry);
    if (thread == NULL || thread->osthread() == NULL) {
      vm_exit_during_initialization("java.lang.OutOfMemoryError",
                                    "unable to create ub timer thread");
    }
    java_lang_Thread::set_thread(thread_oop(), thread);
    java_lang_Thread::set_priority(thread_oop(), NearMaxPriority);
    java_lang_Thread::set_daemon(thread_oop());
    thread->set_threadObj(thread_oop());
    _thread = thread;

    Threads::add(thread);
    Thread::start(thread);
  }
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