/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 */

#include "precompiled.hpp"

#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketDataInfo.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"

#include "matrix/ubSocket/ubSocketSelectorNotifier.hpp"

struct UBSocketSelectorNotifierEntry : public CHeapObj<mtInternal> {
  explicit UBSocketSelectorNotifierEntry(int event_fd)
      : fd(event_fd),
        wakeup_pending(false),
        profile_write_complete_ns(0),
        subscriptions(NULL),
        ready_head(NULL),
        ready_tail(NULL),
        next(NULL) {}

  int fd;
  bool wakeup_pending;
  jlong profile_write_complete_ns;
  UBSocketSelectorSubscription* subscriptions;
  UBSocketSelectorSubscription* ready_head;
  UBSocketSelectorSubscription* ready_tail;
  UBSocketSelectorNotifierEntry* next;
};

struct UBSocketSelectorSubscription : public CHeapObj<mtInternal> {
  UBSocketSelectorSubscription(jlong subscription_id,
                               UBSocketConnection* conn,
                               UBSocketSelectorNotifierEntry* entry)
      : id(subscription_id),
        connection(conn),
        notifier(entry),
        ready_queued(false),
        next_connection(NULL),
        next_notifier(NULL),
        next_global(NULL),
        ready_prev(NULL),
        ready_next(NULL) {}

  jlong id;
  UBSocketConnection* connection;
  UBSocketSelectorNotifierEntry* notifier;
  bool ready_queued;
  UBSocketSelectorSubscription* next_connection;
  UBSocketSelectorSubscription* next_notifier;
  UBSocketSelectorSubscription* next_global;
  UBSocketSelectorSubscription* ready_prev;
  UBSocketSelectorSubscription* ready_next;
};

Monitor* UBSocketSelectorNotifiers::_lock = NULL;
UBSocketSelectorNotifierEntry* UBSocketSelectorNotifiers::_head = NULL;
UBSocketSelectorSubscription* UBSocketSelectorNotifiers::_subscriptions = NULL;
uint32_t UBSocketSelectorNotifiers::_next_subscription_id = 0;

void UBSocketSelectorNotifiers::init() {
  _lock = new Monitor(Mutex::leaf, "UBSocketSelectorNotifiers_lock");
  _head = NULL;
  _subscriptions = NULL;
  _next_subscription_id = 0;
}

UBSocketSelectorNotifierEntry* UBSocketSelectorNotifiers::find_fd_locked(int fd) {
  for (UBSocketSelectorNotifierEntry* cur = _head; cur != NULL;
       cur = cur->next) {
    if (cur->fd == fd) { return cur; }
  }
  return NULL;
}

jlong UBSocketSelectorNotifiers::next_subscription_id_locked(
    uint32_t connection_generation) {
  _next_subscription_id++;
  if (_next_subscription_id == 0) { _next_subscription_id = 1; }
  uint64_t token = ((uint64_t)connection_generation << 32) |
                   _next_subscription_id;
  return (jlong)token;
}

void UBSocketSelectorNotifiers::enqueue_ready_locked(
    UBSocketSelectorSubscription* sub) {
  if (sub->ready_queued) { return; }
  UBSocketSelectorNotifierEntry* entry = sub->notifier;
  sub->ready_queued = true;
  sub->ready_prev = entry->ready_tail;
  sub->ready_next = NULL;
  if (entry->ready_tail == NULL) {
    entry->ready_head = sub;
  } else {
    entry->ready_tail->ready_next = sub;
  }
  entry->ready_tail = sub;
  UBSocketProfiler::count(UB_PROF_SELECTOR_READY_QUEUED);
}

void UBSocketSelectorNotifiers::unlink_ready_locked(
    UBSocketSelectorSubscription* sub) {
  if (!sub->ready_queued) { return; }
  UBSocketSelectorNotifierEntry* entry = sub->notifier;
  if (sub->ready_prev == NULL) {
    entry->ready_head = sub->ready_next;
  } else {
    sub->ready_prev->ready_next = sub->ready_next;
  }
  if (sub->ready_next == NULL) {
    entry->ready_tail = sub->ready_prev;
  } else {
    sub->ready_next->ready_prev = sub->ready_prev;
  }
  sub->ready_queued = false;
  sub->ready_prev = NULL;
  sub->ready_next = NULL;
}

void UBSocketSelectorNotifiers::unlink_subscription_locked(
    UBSocketSelectorSubscription* sub) {
  if (sub == NULL) { return; }
  unlink_ready_locked(sub);

  UBSocketSelectorSubscription** global_link = &_subscriptions;
  while (*global_link != NULL && *global_link != sub) {
    global_link = &(*global_link)->next_global;
  }
  if (*global_link == sub) { *global_link = sub->next_global; }

  UBSocketSelectorSubscription** connection_link =
      &sub->connection->_selector_subscriptions;
  while (*connection_link != NULL && *connection_link != sub) {
    connection_link = &(*connection_link)->next_connection;
  }
  if (*connection_link == sub) {
    *connection_link = sub->next_connection;
  }

  UBSocketSelectorSubscription** notifier_link =
      &sub->notifier->subscriptions;
  while (*notifier_link != NULL && *notifier_link != sub) {
    notifier_link = &(*notifier_link)->next_notifier;
  }
  if (*notifier_link == sub) { *notifier_link = sub->next_notifier; }

  delete sub;
}

bool UBSocketSelectorNotifiers::register_fd(int fd) {
  if (_lock == NULL || fd < 0) { return false; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (find_fd_locked(fd) != NULL) { return true; }
  UBSocketSelectorNotifierEntry* entry =
      new UBSocketSelectorNotifierEntry(fd);
  if (entry == NULL) { return false; }
  entry->next = _head;
  _head = entry;
  return true;
}

void UBSocketSelectorNotifiers::unregister_fd(int fd) {
  if (_lock == NULL || fd < 0) { return; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  UBSocketSelectorNotifierEntry* prev = NULL;
  UBSocketSelectorNotifierEntry* cur = _head;
  while (cur != NULL) {
    if (cur->fd == fd) {
      while (cur->subscriptions != NULL) {
        unlink_subscription_locked(cur->subscriptions);
      }
      if (prev == NULL) {
        _head = cur->next;
      } else {
        prev->next = cur->next;
      }
      delete cur;
      return;
    }
    prev = cur;
    cur = cur->next;
  }
}

jlong UBSocketSelectorNotifiers::subscribe(UBSocketConnection* connection,
                                           int event_fd) {
  if (_lock == NULL || connection == NULL || event_fd < 0) { return 0; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  UBSocketSelectorNotifierEntry* entry = find_fd_locked(event_fd);
  if (entry == NULL) { return 0; }

  for (UBSocketSelectorSubscription* cur = connection->_selector_subscriptions;
       cur != NULL; cur = cur->next_connection) {
    if (cur->notifier == entry) { return cur->id; }
  }

  jlong id = next_subscription_id_locked(
      connection->local_connection_generation());
  UBSocketSelectorSubscription* sub =
      new UBSocketSelectorSubscription(id, connection, entry);
  if (sub == NULL) { return -1; }

  sub->next_connection = connection->_selector_subscriptions;
  connection->_selector_subscriptions = sub;
  sub->next_notifier = entry->subscriptions;
  entry->subscriptions = sub;
  sub->next_global = _subscriptions;
  _subscriptions = sub;
  if (connection->poll_rx_ready()) {
    enqueue_ready_locked(sub);
    notify_entry_locked(entry, 0);
  }
  return id;
}

void UBSocketSelectorNotifiers::unsubscribe(jlong subscription_id) {
  if (_lock == NULL || subscription_id <= 0) { return; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  for (UBSocketSelectorSubscription* cur = _subscriptions; cur != NULL;
       cur = cur->next_global) {
    if (cur->id == subscription_id) {
      unlink_subscription_locked(cur);
      return;
    }
  }
}

void UBSocketSelectorNotifiers::detach_connection(
    UBSocketConnection* connection) {
  if (_lock == NULL || connection == NULL) { return; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  while (connection->_selector_subscriptions != NULL) {
    unlink_subscription_locked(connection->_selector_subscriptions);
  }
}

int32_t UBSocketSelectorNotifiers::drain_ready(
    int event_fd, UBSocketSelectorReadyEvent* events, int32_t capacity,
    jlong* profile_complete_ns) {
  if (profile_complete_ns != NULL) { *profile_complete_ns = 0; }
  if (_lock == NULL || event_fd < 0 || events == NULL || capacity <= 0) {
    return 0;
  }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  UBSocketSelectorNotifierEntry* entry = find_fd_locked(event_fd);
  if (entry == NULL) { return 0; }

  int32_t count = 0;
  while (count < capacity && entry->ready_head != NULL) {
    UBSocketSelectorSubscription* sub = entry->ready_head;
    unlink_ready_locked(sub);
    if (!sub->connection->closing() && sub->connection->poll_rx_ready()) {
      events[count].fd = sub->connection->_socket_fd;
      events[count].connection_generation =
          sub->connection->local_connection_generation();
      count++;
      UBSocketProfiler::count(UB_PROF_SELECTOR_READY_DRAINED);
    }
  }

  if (entry->ready_head == NULL) {
    entry->wakeup_pending = false;
    if (profile_complete_ns != NULL) {
      *profile_complete_ns = entry->profile_write_complete_ns;
    }
    entry->profile_write_complete_ns = 0;
  } else {
    const uint64_t one = 1;
    ssize_t nwrite;
    do {
      nwrite = ::write(entry->fd, &one, sizeof(one));
    } while (nwrite < 0 && errno == EINTR);
    if (nwrite != (ssize_t)sizeof(one) &&
        !(nwrite < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "selector notifier fd=%d rearm failed rc=%ld errno=%d\n",
             entry->fd, (long)nwrite, errno);
      entry->wakeup_pending = false;
      return -1;
    }
    if (nwrite == (ssize_t)sizeof(one)) {
      UBSocketProfiler::count(UB_PROF_SELECTOR_EVENTFD_WRITE);
    }
  }
  return count;
}

bool UBSocketSelectorNotifiers::notify_entry_locked(
    UBSocketSelectorNotifierEntry* entry, uint64_t observed_ns) {
  if (entry->wakeup_pending) {
    UBSocketProfiler::count(UB_PROF_SELECTOR_EVENTFD_COALESCED);
    return true;
  }

  const uint64_t one = 1;
  ssize_t nwrite;
  do {
    nwrite = ::write(entry->fd, &one, sizeof(one));
  } while (nwrite < 0 && errno == EINTR);

  bool profile = observed_ns != 0 && UBSocketProfiler::enabled(
      UB_PROF_OBSERVE_TO_EVENTFD_COMPLETE_NS);
  if (nwrite == (ssize_t)sizeof(one)) {
    UBSocketProfiler::count(UB_PROF_SELECTOR_EVENTFD_WRITE);
    entry->wakeup_pending = true;
    if (profile) {
      uint64_t complete_ns = (uint64_t)os::javaTimeNanos();
      if (observed_ns == 0 || complete_ns < observed_ns ||
          entry->profile_write_complete_ns != 0) {
        UBSocketProfiler::count(UB_PROF_POLL_PROFILE_INVALID);
      } else {
        UBSocketProfiler::record_latency(
            UB_PROF_OBSERVE_TO_EVENTFD_COMPLETE_NS,
            complete_ns - observed_ns);
        entry->profile_write_complete_ns = (jlong)complete_ns;
      }
    }
    return true;
  }

  if (nwrite < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    UBSocketProfiler::count(UB_PROF_SELECTOR_EVENTFD_COALESCED);
    entry->wakeup_pending = true;
    if (profile) {
      UBSocketProfiler::count(UB_PROF_POLL_PROFILE_INVALID);
    }
    return true;
  }

  UB_LOG(UB_SOCKET, UB_LOG_WARNING,
         "selector notifier fd=%d write failed rc=%ld errno=%d\n",
         entry->fd, (long)nwrite, errno);
  return false;
}

int UBSocketSelectorNotifiers::notify_ready(
    UBSocketConnection* const* ready, int ready_count, uint64_t observed_ns) {
  if (_lock == NULL || ready == NULL || ready_count <= 0) { return 0; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);

  int notified = 0;
  for (int i = 0; i < ready_count; i++) {
    UBSocketConnection* connection = ready[i];
    for (UBSocketSelectorSubscription* sub =
             connection->_selector_subscriptions;
         sub != NULL; sub = sub->next_connection) {
      UBSocketSelectorNotifierEntry* entry = sub->notifier;
      enqueue_ready_locked(sub);
      if (notify_entry_locked(entry, observed_ns)) { notified++; }
    }
  }
  return notified;
}

void UBSocketSelectorNotifiers::shutdown() {
  if (_lock == NULL) { return; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  while (_subscriptions != NULL) {
    unlink_subscription_locked(_subscriptions);
  }
  while (_head != NULL) {
    UBSocketSelectorNotifierEntry* next = _head->next;
    delete _head;
    _head = next;
  }
}
