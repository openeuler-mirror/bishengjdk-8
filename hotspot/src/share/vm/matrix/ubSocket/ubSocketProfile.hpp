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

enum UBSocketProfileMode {
  UB_PROFILE_OFF = 0,
  UB_PROFILE_SUMMARY = 1,
  UB_PROFILE_DETAIL = 2
};

// UBSocketProfileEvent nesting. Upper-case names are summary events.
//	nio_write_total: FileDispatcherImpl.write0 native wrapper total cost.
//		RING_WRITE_TOTAL: write payload into the peer receiver-owned ring.
//			RING_WRITE_MEMCPY: memcpy payload bytes into the peer ring.
//			ring_write_full: count writes that find the peer ring full.
//			ring_write_partial: count writes that only write part of the request.
//		WAKEUP_SEND_TOTAL: send TCP wakeup control frame for ring readiness.
//			wakeup_send_syscall: actual wakeup send syscall/wrapper cost.
//			wakeup_send_wait: wait for wakeup TCP fd writable after EAGAIN.
//			wakeup_send_eagain: count wakeup send EAGAIN events and remaining bytes.
//	nio_read_total: FileDispatcherImpl.read0 native wrapper total cost.
//		ring_read_hit: count read calls that hit ring payload.
//		ring_read_empty: count read calls that find no ring payload.
//		RING_READ_TOTAL: read payload from the local receiver-owned ring.
//			RING_READ_MEMCPY: memcpy payload bytes out of the local ring.
//		wakeup_drain_total: selector drains TCP wakeup control frames.
//			wakeup_drain_syscall: actual wakeup drain read syscall cost.
//			wakeup_frame_count: count parsed wakeup/control frames.
//		selector_pending_check: count selector pending-data checks.
//		selector_pending_ready: count selector checks that find pending ring data.
//	UB_ATTACH_SUCCESS: count fds that successfully attach to the UB ring data path.
//	UB_ATTACH_FALLBACK: count fds whose attach/register path falls back to TCP.
//	ring_attach_no_slot: count attach attempts that fail because no ring slot is free.
//	wakeup_request_total: count ring writes that request a TCP wakeup.
//	wakeup_skip_nonempty: count ring writes coalesced while peer ring is non-empty.
//	wakeup_skip_sending: count ring writes skipped while a wakeup send is in progress.
//	wakeup_request_threshold: count threshold-driven wakeups for a non-empty peer ring.
//	wakeup_request_retry: count wakeups retried after a previous send failure.
//	selector_probe_check: count selector probe checks for unresolved wakeups.
//	selector_probe_ready: count selector probes that find ring payload.
//	selector_probe_empty: count selector probes that still find no ring payload.
//	selector_ready_inject: count ready fds injected from UB ring state.
enum UBSocketProfileEvent {
  UB_PROF_NIO_WRITE_TOTAL = 0,
  UB_PROF_RING_WRITE_TOTAL,
  UB_PROF_RING_WRITE_MEMCPY,
  UB_PROF_RING_WRITE_FULL,
  UB_PROF_RING_WRITE_PARTIAL,
  UB_PROF_WAKEUP_SEND_TOTAL,
  UB_PROF_WAKEUP_SEND_SYSCALL,
  UB_PROF_WAKEUP_SEND_WAIT,
  UB_PROF_WAKEUP_SEND_EAGAIN,

  UB_PROF_NIO_READ_TOTAL,
  UB_PROF_RING_READ_HIT,
  UB_PROF_RING_READ_EMPTY,
  UB_PROF_RING_READ_TOTAL,
  UB_PROF_RING_READ_MEMCPY,
  UB_PROF_WAKEUP_DRAIN_TOTAL,
  UB_PROF_WAKEUP_DRAIN_SYSCALL,
  UB_PROF_WAKEUP_FRAME_COUNT,
  UB_PROF_SELECTOR_PENDING_CHECK,
  UB_PROF_SELECTOR_PENDING_READY,

  UB_PROF_UB_ATTACH_SUCCESS,
  UB_PROF_UB_ATTACH_FALLBACK,
  UB_PROF_RING_ATTACH_NO_SLOT,
  UB_PROF_WAKEUP_REQUEST_TOTAL,
  UB_PROF_WAKEUP_SKIP_NONEMPTY,
  UB_PROF_WAKEUP_SKIP_SENDING,
  UB_PROF_WAKEUP_REQUEST_THRESHOLD,
  UB_PROF_WAKEUP_REQUEST_RETRY,
  UB_PROF_SELECTOR_PROBE_CHECK,
  UB_PROF_SELECTOR_PROBE_READY,
  UB_PROF_SELECTOR_PROBE_EMPTY,
  UB_PROF_SELECTOR_READY_INJECT,

  UB_PROF_COUNT
};

class UBSocketProfiler : public AllStatic {
 public:
  static bool enabled(UBSocketProfileEvent event);
  static uint64_t start(UBSocketProfileEvent event);
  static uint64_t end(UBSocketProfileEvent event, uint64_t start_ns,
                      uint64_t bytes = 0);
  static void count(UBSocketProfileEvent event, uint64_t bytes = 0);
  static void record(UBSocketProfileEvent event, uint64_t elapsed_ns,
                     uint64_t bytes, uint64_t count);
  static void print_summary();
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
