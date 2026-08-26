/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 */

#ifndef SHARE_VM_MATRIX_UBSOCKET_UBSOCKETSELECTORNOTIFIER_HPP
#define SHARE_VM_MATRIX_UBSOCKET_UBSOCKETSELECTORNOTIFIER_HPP

#include <stdint.h>

#include "memory/allocation.hpp"

class Monitor;
class UBSocketConnection;
struct UBSocketSelectorNotifierEntry;
struct UBSocketSelectorSubscription;

struct UBSocketSelectorReadyEvent {
  int32_t fd;
  uint32_t connection_generation;
};

// Registry of Linux EPollArrayWrapper eventfds and their connection
// subscriptions. The Poller routes ready connections while holding the
// registry lock, so notifier close cannot race an eventfd write or fd reuse.
class UBSocketSelectorNotifiers : public AllStatic {
 public:
  static void init();
  static bool register_fd(int fd);
  static void unregister_fd(int fd);
  static jlong subscribe(UBSocketConnection* connection, int event_fd);
  static void unsubscribe(jlong subscription_id);
  static void detach_connection(UBSocketConnection* connection);
  static int32_t drain_ready(int event_fd,
                             UBSocketSelectorReadyEvent* events,
                             int32_t capacity, jlong* profile_complete_ns);
  static int notify_ready(UBSocketConnection* const* ready,
                          int ready_count, uint64_t observed_ns);
  static void shutdown();

 private:
  static Monitor* _lock;
  static UBSocketSelectorNotifierEntry* _head;
  static UBSocketSelectorSubscription* _subscriptions;
  static uint32_t _next_subscription_id;

  static UBSocketSelectorNotifierEntry* find_fd_locked(int fd);
  static void unlink_subscription_locked(UBSocketSelectorSubscription* sub);
  static jlong next_subscription_id_locked(uint32_t connection_generation);
  static void enqueue_ready_locked(UBSocketSelectorSubscription* sub);
  static void unlink_ready_locked(UBSocketSelectorSubscription* sub);
  static bool notify_entry_locked(UBSocketSelectorNotifierEntry* entry,
                                  uint64_t observed_ns);
};

#endif // SHARE_VM_MATRIX_UBSOCKET_UBSOCKETSELECTORNOTIFIER_HPP
