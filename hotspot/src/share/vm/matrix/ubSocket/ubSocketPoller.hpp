/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 */

#ifndef SHARE_VM_MATRIX_UBSOCKET_UBSOCKETPOLLER_HPP
#define SHARE_VM_MATRIX_UBSOCKET_UBSOCKETPOLLER_HPP

#include "memory/allocation.hpp"
#include "runtime/thread.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/growableArray.hpp"

class Monitor;
class UBSocketConnection;

// One busy polling thread per JVM observes sender-written generations and
// converts changed Ring slots into selector eventfd notifications.
class UBSocketPoller : public AllStatic {
 public:
  static void init(volatile jint* process_generation);
  static bool start();
  static void shutdown();

 private:
  static void poller_entry(JavaThread* thread, TRAPS);

  static Monitor* _lock;
  static volatile bool _should_stop;
  static bool _started;
  static bool _exited;
  static volatile jint* _process_generation;
  static jint _last_process_generation;
  static uint32_t _ring_count;
  static uint64_t* _last_write_offsets;
  static uint32_t* _last_slot_generations;
  static GrowableArray<uint32_t>* _changed_slots;
  static GrowableArray<UBSocketConnection*>* _ready;
};

#endif // SHARE_VM_MATRIX_UBSOCKET_UBSOCKETPOLLER_HPP
