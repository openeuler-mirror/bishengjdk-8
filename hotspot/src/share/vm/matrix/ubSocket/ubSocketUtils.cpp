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

#include <errno.h>
#include <string.h>
#include <unistd.h>

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

UBSocketEarlyReqQueue::EarlyRequest* UBSocketEarlyReqQueue::_head = NULL;
UBSocketEarlyReqQueue::EarlyRequest* UBSocketEarlyReqQueue::_tail = NULL;

Mutex* UBSocketSessionCaches::_cache_lock = NULL;
UBSocketAttachSession* UBSocketSessionCaches::_cache_head = NULL;

static const int UB_SOCKET_EARLY_REQUEST_MAX = 128;

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

  KlassHandle group(THREAD, SystemDictionary::ThreadGroup_klass());
  JavaCalls::call_special(&result, thread_group, group,
                          vmSymbols::add_method_name(),
                          vmSymbols::thread_void_signature(),
                          thread_oop, THREAD);
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

/************************UBSocketSessionCaches**********************/

void UBSocketSessionCaches::init() {
  // Cache lookup pins a matching session while this lock is held. Give the
  // registry a higher rank than the per-session monitor to make that lifetime
  // hand-off explicit and deadlock-checkable in slowdebug builds.
  _cache_lock = new Mutex(Mutex::leaf + 1, "UBSocketSessionCaches_lock");
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

int UBSocketEarlyReqQueue::cleanup() {
  int closed = 0;
  EarlyRequest* entry = _head;
  _head = NULL;
  _tail = NULL;
  while (entry != NULL) {
    EarlyRequest* next = entry->next;
    if (entry->control_fd >= 0) {
      close(entry->control_fd);
      closed++;
    }
    delete entry;
    entry = next;
  }
  return closed;
}
