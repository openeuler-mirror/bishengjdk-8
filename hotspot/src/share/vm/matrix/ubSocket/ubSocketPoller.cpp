/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 */

#include "precompiled.hpp"

#include <errno.h>
#include <string.h>

#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketDataInfo.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "matrix/ubSocket/ubSocketSelectorNotifier.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "runtime/globals.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/orderAccess.inline.hpp"

#include "matrix/ubSocket/ubSocketPoller.hpp"

Monitor* UBSocketPoller::_lock = NULL;
volatile bool UBSocketPoller::_should_stop = false;
bool UBSocketPoller::_started = false;
bool UBSocketPoller::_exited = true;
volatile jint* UBSocketPoller::_process_generation = NULL;
jint UBSocketPoller::_last_process_generation = 0;
uint32_t UBSocketPoller::_ring_count = 0;
uint64_t* UBSocketPoller::_last_write_offsets = NULL;
uint32_t* UBSocketPoller::_last_slot_generations = NULL;
GrowableArray<uint32_t>* UBSocketPoller::_changed_slots = NULL;
GrowableArray<UBSocketConnection*>* UBSocketPoller::_ready = NULL;

static inline uint64_t load_write_offset(uint32_t slot) {
  return (uint64_t)OrderAccess::load_acquire(
      (volatile julong*)&UBSocketRingSlots::write_state(slot)->write_offset);
}

void UBSocketPoller::init(volatile jint* process_generation) {
  _lock = new Monitor(Mutex::leaf, "UBSocketPoller_lock");
  _should_stop = false;
  _started = false;
  _exited = true;
  _process_generation = process_generation;
  _last_process_generation = process_generation == NULL ? 0 :
      OrderAccess::load_acquire(process_generation);
  _ring_count = UBSocketRingSlots::slot_count();
  _last_write_offsets =
      NEW_C_HEAP_ARRAY(uint64_t, _ring_count, mtInternal);
  _last_slot_generations =
      NEW_C_HEAP_ARRAY(uint32_t, _ring_count, mtInternal);
  for (uint32_t slot = 0; slot < _ring_count; slot++) {
    UBSocketRing ring(UBSocketRingSlots::write_state(slot),
                      UBSocketRingSlots::peer_state(slot),
                      UBSocketRingSlots::payload_addr(slot),
                      UBSocketRingSlots::payload_size());
    _last_write_offsets[slot] = ring.load_write_offset();
    _last_slot_generations[slot] =
        UBSocketRingSlots::slot_generation(slot);
  }
  _changed_slots = new (ResourceObj::C_HEAP, mtInternal)
      GrowableArray<uint32_t>(UB_INIT_ARRAY_CAP, true, mtInternal);
  _ready = new (ResourceObj::C_HEAP, mtInternal)
      GrowableArray<UBSocketConnection*>(UB_INIT_ARRAY_CAP, true, mtInternal);
}

void UBSocketPoller::poller_entry(JavaThread* thread, TRAPS) {
  if (UBSocketPollerCpu >= 0) {
    uint cpu = (uint)UBSocketPollerCpu;
    if (os::bind_to_processor(cpu)) {
      UB_LOG(UB_SOCKET, UB_LOG_INFO, "process poller bound cpu=%u\n", cpu);
    } else {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "process poller bind cpu=%u failed: %s\n", cpu, strerror(errno));
    }
  }
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "process poller started\n");
  {
    // Idle polling does not touch VM-managed state. Staying native makes the
    // thread safepoint-safe without adding transitions to the hot spin loop.
    ThreadToNativeFromVM native(thread);
    while (!_should_stop) {
      jint process_generation =
          OrderAccess::load_acquire(_process_generation);
      if (process_generation == _last_process_generation) {
        SpinPause();
        continue;
      }
      _last_process_generation = process_generation;
      uint64_t observed_ns = 0;
      if (UBSocketProfiler::enabled(
              UB_PROF_OBSERVE_TO_EVENTFD_COMPLETE_NS)) {
        observed_ns = (uint64_t)os::javaTimeNanos();
      }
      _changed_slots->clear();
      for (uint32_t slot = 0; slot < _ring_count; slot++) {
        uint32_t generation_before;
        uint32_t generation_after;
        uint64_t write_offset;
        do {
          generation_before = UBSocketRingSlots::slot_generation(slot);
          write_offset = load_write_offset(slot);
          generation_after = UBSocketRingSlots::slot_generation(slot);
        } while (generation_before != generation_after);
        uint64_t last_write_offset = _last_write_offsets[slot];
        if (generation_before != _last_slot_generations[slot]) {
          _last_slot_generations[slot] = generation_before;
          _last_write_offsets[slot] = write_offset;
          if (generation_before != 0 && write_offset != 0) {
            _changed_slots->append(slot);
          }
        } else if (write_offset != last_write_offset) {
          _last_write_offsets[slot] = write_offset;
          _changed_slots->append(slot);
        }
      }
      UBSocketProfiler::record(UB_PROF_POLL_WRITE_OFFSET_SCANNED, 0, 0,
                               _ring_count);
      UBSocketProfiler::record(UB_PROF_POLL_WRITE_OFFSET_CHANGED, 0, 0,
                               _changed_slots->length());
      if (_changed_slots->is_empty()) { continue; }
      ThreadInVMfromNative in_vm(thread);
      UBSocketProfiler::count(UB_PROF_POLL_NOTIFY_OBSERVED);
      UBSocketConnectionTable::collect_write_offset_and_pin(
          _changed_slots, _ready);
      int32_t stale_slots = _changed_slots->length() - _ready->length();
      if (stale_slots > 0) {
        UBSocketProfiler::record(UB_PROF_POLL_WRITE_OFFSET_STALE, 0, 0,
                                 (uint64_t)stale_slots);
      }
      if (!_ready->is_empty()) {
        UBSocketSelectorNotifiers::notify_ready(
            _ready->adr_at(0), _ready->length(), observed_ns);
      }
      UBSocketConnectionTable::unpin_all(_ready);
    }
  }

  // Return to the VM state before acquiring a VM monitor and publishing exit.
  {
    MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
    _exited = true;
    locker.notify_all();
  }
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "process poller stopped\n");
}

bool UBSocketPoller::start() {
  {
    MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
    if (_started) { return true; }
    if (_process_generation == NULL || _ring_count == 0) { return false; }
    _should_stop = false;
    _exited = false;
  }

  // Java thread construction invokes Java code and therefore must not run
  // while a VM monitor is owned by the current thread.
  JavaThread* thread =
      UBSocketThreadUtils::start_daemon(&poller_entry, "UBSocket Poller",
                                        NearMaxPriority);
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (thread == NULL) {
    _exited = true;
    return false;
  }
  _started = true;
  return true;
}

void UBSocketPoller::shutdown() {
  if (_lock == NULL) { return; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  if (_started || !_exited) {
    _should_stop = true;
    _started = false;
    while (!_exited) {
      locker.wait(Mutex::_no_safepoint_check_flag);
    }
  }
  FREE_C_HEAP_ARRAY(uint64_t, _last_write_offsets, mtInternal);
  _last_write_offsets = NULL;
  FREE_C_HEAP_ARRAY(uint32_t, _last_slot_generations, mtInternal);
  _last_slot_generations = NULL;
  delete _changed_slots;
  _changed_slots = NULL;
  delete _ready;
  _ready = NULL;
}
