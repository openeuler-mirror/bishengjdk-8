/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

#ifndef SHARE_VM_MATRIX_UBSOCKET_UBSOCKETPROFILE_HPP
#define SHARE_VM_MATRIX_UBSOCKET_UBSOCKETPROFILE_HPP

#include <stdint.h>

#include "memory/allocation.hpp"

class outputStream;

enum UBSocketProfileMode {
  UB_PROFILE_OFF = 0,
  UB_PROFILE_SUMMARY = 1,
  UB_PROFILE_DETAIL = 2
};

enum {
  UB_PROFILE_LATENCY_BUCKET_NS = 1000,
  UB_PROFILE_LATENCY_BUCKET_COUNT = 16384,
  UB_PROFILE_LATENCY_MAX_NS =
      (UB_PROFILE_LATENCY_BUCKET_COUNT - 1) * UB_PROFILE_LATENCY_BUCKET_NS
};

// UBSocketProfileEvent IDs are shared with the JDK native layer. The groups
// below describe the logical path; counters are observations, not children
// whose values should be subtracted from a parent timing.
//
//   nio_write_total: FileDispatcherImpl.write0 wrapper cost.
//     ring_write_total: copy and publish a payload to the peer-owned ring.
//       ring_write_memcpy: payload memcpy portion of ring_write_total.
//     ring_write_full/partial: ring capacity outcomes.
//     poll_notify_publish: publish the process notification generation.
//   nio_read_total: FileDispatcherImpl.read0 wrapper cost.
//     ring_read_total: read a payload from the local-owned ring.
//       ring_read_memcpy: payload memcpy portion of ring_read_total.
//     ring_read_hit/empty: read-result counters.
//   selector_pending_check/ready: initial selector pending-data checks.
//   selector_ready_inject: readiness synthesized from retained ring state.
//   poll_notify_observed: process notifications observed by the poller.
//   selector_poll_ready_inject: readiness synthesized from a poller queue.
//   ring_*_max_used_bytes: ring high-water observations.
//   ub_attach_* and ring_attach_no_slot: attach outcomes.
//   publish_to_observe_ns through eventfd_complete_to_selector_inject_ns:
//     local notification-stage latency samples.
//   poll_notify_request_*, selector_spin_*, poll_write_offset_*:
//     notification and spin diagnostic counters/timing.
//   selector_ready_queued/drained and selector_eventfd_*: ready-queue flow.
//   close_wait_final_offset/close_failure_timeout: close-drain outcomes.
enum UBSocketProfileEvent {
  UB_PROF_NIO_WRITE_TOTAL = 0,
  UB_PROF_RING_WRITE_TOTAL,
  UB_PROF_RING_WRITE_MEMCPY,
  UB_PROF_RING_WRITE_FULL,
  UB_PROF_RING_WRITE_PARTIAL,
  UB_PROF_NIO_READ_TOTAL,
  UB_PROF_RING_READ_HIT,
  UB_PROF_RING_READ_EMPTY,
  UB_PROF_RING_READ_TOTAL,
  UB_PROF_RING_READ_MEMCPY,
  UB_PROF_SELECTOR_PENDING_CHECK,
  UB_PROF_SELECTOR_PENDING_READY,
  UB_PROF_UB_ATTACH_SUCCESS,
  UB_PROF_UB_ATTACH_FALLBACK,
  UB_PROF_RING_ATTACH_NO_SLOT,
  UB_PROF_SELECTOR_READY_INJECT,
  UB_PROF_RING_WRITE_MAX_USED_BYTES,
  UB_PROF_RING_READ_MAX_USED_BYTES,
  UB_PROF_POLL_NOTIFY_PUBLISH,
  UB_PROF_POLL_NOTIFY_OBSERVED,
  UB_PROF_SELECTOR_POLL_READY_INJECT,
  UB_PROF_PUBLISH_TO_OBSERVE_NS,
  UB_PROF_OBSERVE_TO_EVENTFD_COMPLETE_NS,
  UB_PROF_EVENTFD_TO_SELECTOR_INJECT_NS,
  UB_PROF_POLL_PROFILE_INVALID,
  UB_PROF_UB_ATTACH_SKIP,
  UB_PROF_POLL_NOTIFY_REQUEST_EMPTY,
  UB_PROF_POLL_NOTIFY_REQUEST_THRESHOLD,
  UB_PROF_POLL_NOTIFY_REQUEST_RACE,
  UB_PROF_POLL_NOTIFY_SKIP_NONEMPTY,
  UB_PROF_SELECTOR_SPIN_ATTEMPT,
  UB_PROF_SELECTOR_SPIN_READY,
  UB_PROF_SELECTOR_SPIN_TIMEOUT,
  UB_PROF_SELECTOR_SPIN_NANOS,
  UB_PROF_POLL_WRITE_OFFSET_SCANNED,
  UB_PROF_POLL_WRITE_OFFSET_CHANGED,
  UB_PROF_POLL_WRITE_OFFSET_STALE,
  UB_PROF_SELECTOR_READY_QUEUED,
  UB_PROF_SELECTOR_READY_DRAINED,
  UB_PROF_SELECTOR_EVENTFD_WRITE,
  UB_PROF_SELECTOR_EVENTFD_COALESCED,
  UB_PROF_CLOSE_WAIT_FINAL_OFFSET,
  UB_PROF_CLOSE_FAILURE_TIMEOUT,

  UB_PROF_COUNT
};

class UBSocketProfiler : public AllStatic {
 private:
  static bool enabled_impl(UBSocketProfileEvent event);
  static uint64_t start_impl(UBSocketProfileEvent event);
  static uint64_t end_impl(UBSocketProfileEvent event, uint64_t start_ns,
                           uint64_t bytes);
  static void count_impl(UBSocketProfileEvent event, uint64_t bytes);
  static void max_bytes_impl(UBSocketProfileEvent event, uint64_t bytes);
  static void record_impl(UBSocketProfileEvent event, uint64_t elapsed_ns,
                          uint64_t bytes, uint64_t count);
  static void record_latency_impl(UBSocketProfileEvent event,
                                  uint64_t elapsed_ns);

 public:
  // Keep the disabled path inline: no profiler call, clock read, or atomic.
  static bool active() {
    return UBSocketProfile != (uintx)UB_PROFILE_OFF;
  }

  static bool enabled(UBSocketProfileEvent event) {
    return active() && enabled_impl(event);
  }

  static uint64_t start(UBSocketProfileEvent event) {
    return active() ? start_impl(event) : 0;
  }

  static uint64_t end(UBSocketProfileEvent event, uint64_t start_ns,
                      uint64_t bytes = 0) {
    return active() ? end_impl(event, start_ns, bytes) : 0;
  }

  static void count(UBSocketProfileEvent event, uint64_t bytes = 0) {
    if (active()) {
      count_impl(event, bytes);
    }
  }

  static void max_bytes(UBSocketProfileEvent event, uint64_t bytes) {
    if (active()) {
      max_bytes_impl(event, bytes);
    }
  }

  static void record(UBSocketProfileEvent event, uint64_t elapsed_ns,
                     uint64_t bytes, uint64_t count) {
    if (active()) {
      record_impl(event, elapsed_ns, bytes, count);
    }
  }

  static void record_latency(UBSocketProfileEvent event, uint64_t elapsed_ns) {
    if (active()) {
      record_latency_impl(event, elapsed_ns);
    }
  }

  static void print_summary(outputStream* force_output);
};

class UBSocketProfileScope {
 public:
  UBSocketProfileScope(UBSocketProfileEvent event, uint64_t bytes = 0)
      : _event(event), _bytes(bytes), _start_ns(UBSocketProfiler::start(event)) {}

  ~UBSocketProfileScope() {
    UBSocketProfiler::end(_event, _start_ns, _bytes);
  }

 private:
  UBSocketProfileEvent _event;
  uint64_t _bytes;
  uint64_t _start_ns;
};

#define UB_PROFILE_NANOS(event, var) \
  uint64_t var = UBSocketProfiler::start(event)

#define UB_PROFILE_RECORD(event, start_ns, bytes) \
  UBSocketProfiler::end((event), (start_ns), (bytes))

#define UB_PROFILE_COUNT(event, bytes) \
  UBSocketProfiler::count((event), (bytes))

#endif // SHARE_VM_MATRIX_UBSOCKET_UBSOCKETPROFILE_HPP
