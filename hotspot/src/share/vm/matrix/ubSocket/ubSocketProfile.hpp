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
//		UB_WRITE_MEMCPY: copy payload from Java write buffer to local UB shared memory.
//		metadata_mark_send: sender initializes local metadata before descriptor send.
//		DESCRIPTOR_SEND_TOTAL: build and send data descriptor on the original TCP fd.
//			descriptor_send_syscall: actual descriptor send syscall/wrapper calls.
//			send_wait: wait for descriptor TCP fd to become writable after EAGAIN.
//			send_eagain: count descriptor send EAGAIN events and remaining bytes.
//		UB_TCP_FALLBACK_WRITE: write payload through TCP after UB data fallback is requested.
//	nio_read_total: FileDispatcherImpl.read0 native wrapper total cost.
//		ub_first_hit: count read calls that directly hit already parsed UB payload.
//		ub_first_miss: count read calls that miss UB payload and need descriptor receive/parse.
//		descriptor_recv_total: read descriptors from TCP and parse/append them into unread list.
//			descriptor_recv_syscall: actual descriptor read syscall/wrapper calls.
//			DESCRIPTOR_HANDLE_TOTAL: HotSpot descriptor parse/append/metadata handling cost.
//				descriptor_frame_count: count parsed descriptor frames and their payload bytes.
//				append_unread_total: append parsed descriptor ranges to the per-fd unread list.
//					metadata_mark_recv: receiver marks remote metadata after descriptor parse.
//		UB_READ_MEMCPY: copy payload from remote UB shared memory to Java read buffer.
//			metadata_mark_read: receiver marks remote metadata after payload drain.
//	UB_ATTACH_SUCCESS: count fds that successfully attach to the UB data path.
//	UB_ATTACH_FALLBACK: count fds whose attach/register path falls back to TCP.
enum UBSocketProfileEvent {
  UB_PROF_NIO_WRITE_TOTAL = 0,
  UB_PROF_UB_WRITE_MEMCPY,
  UB_PROF_METADATA_MARK_SEND,
  UB_PROF_DESCRIPTOR_SEND_TOTAL,
  UB_PROF_DESCRIPTOR_SEND_SYSCALL,
  UB_PROF_SEND_WAIT,
  UB_PROF_SEND_EAGAIN,
  UB_PROF_UB_TCP_FALLBACK_WRITE,

  UB_PROF_NIO_READ_TOTAL,
  UB_PROF_UB_FIRST_HIT,
  UB_PROF_UB_FIRST_MISS,
  UB_PROF_DESCRIPTOR_RECV_TOTAL,
  UB_PROF_DESCRIPTOR_RECV_SYSCALL,
  UB_PROF_DESCRIPTOR_HANDLE_TOTAL,
  UB_PROF_DESCRIPTOR_FRAME_COUNT,
  UB_PROF_APPEND_UNREAD_TOTAL,
  UB_PROF_METADATA_MARK_RECV,
  UB_PROF_UB_READ_MEMCPY,
  UB_PROF_METADATA_MARK_READ,

  UB_PROF_UB_ATTACH_SUCCESS,
  UB_PROF_UB_ATTACH_FALLBACK,

  UB_PROF_COUNT
};

class UBSocketProfiler : public AllStatic {
 public:
  static bool enabled(UBSocketProfileEvent event);
  static uint64_t start(UBSocketProfileEvent event);
  static void end(UBSocketProfileEvent event, uint64_t start_ns,
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
