/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "runtime/globals.hpp"
#include "runtime/os.hpp"
#include "utilities/ostream.hpp"

enum UBSocketProfileEventKind {
  UB_PROFILE_TIMING,
  UB_PROFILE_COUNTER
};

struct UBSocketProfileEventDef {
  const char* name;
  uint8_t level;
  UBSocketProfileEventKind kind;
};

struct UBSocketProfileCounter {
  jlong count;
  jlong total_ns;
  jlong max_ns;
  jlong bytes;
};

static const UBSocketProfileEventDef ub_socket_profile_events[UB_PROF_COUNT] = {
  { "nio_write_total", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
  { "ub_write_memcpy", UB_PROFILE_SUMMARY, UB_PROFILE_TIMING },
  { "metadata_mark_send", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
  { "descriptor_send_total", UB_PROFILE_SUMMARY, UB_PROFILE_TIMING },
  { "descriptor_send_syscall", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
  { "send_wait", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
  { "send_eagain", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
  { "ub_tcp_fallback_write", UB_PROFILE_SUMMARY, UB_PROFILE_TIMING },

  { "nio_read_total", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
  { "ub_first_hit", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
  { "ub_first_miss", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
  { "descriptor_recv_total", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
  { "descriptor_recv_syscall", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
  { "descriptor_handle_total", UB_PROFILE_SUMMARY, UB_PROFILE_TIMING },
  { "descriptor_frame_count", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
  { "append_unread_total", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
  { "metadata_mark_recv", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
  { "ub_read_memcpy", UB_PROFILE_SUMMARY, UB_PROFILE_TIMING },
  { "metadata_mark_read", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },

  { "ub_attach_success", UB_PROFILE_SUMMARY, UB_PROFILE_COUNTER },
  { "ub_attach_fallback", UB_PROFILE_SUMMARY, UB_PROFILE_COUNTER }
};

static const uint32_t UB_PROFILE_SUMMARY_TIMING_SAMPLE_RATE = 16;
static __thread uint32_t ub_socket_profile_sample_seq[UB_PROF_COUNT];
static UBSocketProfileCounter ub_socket_profile_counters[UB_PROF_COUNT];

bool UBSocketProfiler::enabled(UBSocketProfileEvent event) {
  return event >= 0 && event < UB_PROF_COUNT &&
         UBSocketProfile >= ub_socket_profile_events[event].level;
}

uint64_t UBSocketProfiler::start(UBSocketProfileEvent event) {
  if (!enabled(event) ||
      ub_socket_profile_events[event].kind != UB_PROFILE_TIMING) {
    return 0;
  }
  if (UBSocketProfile == UB_PROFILE_SUMMARY &&
      (++ub_socket_profile_sample_seq[event] %
       UB_PROFILE_SUMMARY_TIMING_SAMPLE_RATE) != 0) {
    return 0;
  }
  return (uint64_t)os::javaTimeNanos();
}

void UBSocketProfiler::end(UBSocketProfileEvent event, uint64_t start_ns,
                           uint64_t bytes) {
  if (!enabled(event) ||
      ub_socket_profile_events[event].kind != UB_PROFILE_TIMING) {
    return;
  }
  UBSocketProfileCounter* counter = &ub_socket_profile_counters[event];
  counter->count++;
  if (bytes != 0) {
    counter->bytes += (jlong)bytes;
  }
  if (start_ns == 0) {
    return;
  }
  uint64_t ns = (uint64_t)os::javaTimeNanos() - start_ns;
  uint64_t total_ns = ns;
  if (UBSocketProfile == UB_PROFILE_SUMMARY) {
    total_ns *= UB_PROFILE_SUMMARY_TIMING_SAMPLE_RATE;
  }
  counter->total_ns += (jlong)total_ns;
  if ((jlong)ns > counter->max_ns) {
    counter->max_ns = (jlong)ns;
  }
}

void UBSocketProfiler::count(UBSocketProfileEvent event, uint64_t bytes) {
  if (!enabled(event) ||
      ub_socket_profile_events[event].kind != UB_PROFILE_COUNTER) {
    return;
  }
  UBSocketProfileCounter* counter = &ub_socket_profile_counters[event];
  counter->count++;
  if (bytes != 0) {
    counter->bytes += (jlong)bytes;
  }
}

void UBSocketProfiler::record(UBSocketProfileEvent event, uint64_t elapsed_ns,
                              uint64_t bytes, uint64_t count) {
  if (count == 0 || !enabled(event)) {
    return;
  }
  UBSocketProfileCounter* counter = &ub_socket_profile_counters[event];
  counter->count += (jlong)count;
  if (bytes != 0) {
    counter->bytes += (jlong)bytes;
  }
  if (ub_socket_profile_events[event].kind != UB_PROFILE_TIMING) {
    return;
  }
  counter->total_ns += (jlong)elapsed_ns;
  if ((jlong)elapsed_ns > counter->max_ns) {
    counter->max_ns = (jlong)elapsed_ns;
  }
}

void UBSocketProfiler::print_summary() {
  if (UBSocketProfile == UB_PROFILE_OFF) { return; }
  outputStream* st = MatrixLog::stream(UB_SOCKET);
  if (UBSocketProfile >= UB_PROFILE_DETAIL) {
    st->print_cr("UBSocketProfile mode=detail timing=all");
  } else {
    st->print_cr("UBSocketProfile mode=summary timing=sampled sample_rate=%u",
                 UB_PROFILE_SUMMARY_TIMING_SAMPLE_RATE);
  }
  st->print_cr("UBSocketProfile %-28s %12s %14s %12s %12s %14s %12s",
               "event", "count", "total_ns", "avg_ns", "max_ns", "bytes",
               "avg_bytes");
  st->print_cr("UBSocketProfile %-28s %12s %14s %12s %12s %14s %12s",
               "----------------------------", "------------",
               "--------------", "------------", "------------",
               "--------------", "------------");
  for (int32_t i = 0; i < UB_PROF_COUNT; i++) {
    if (UBSocketProfile < ub_socket_profile_events[i].level) {
      continue;
    }
    UBSocketProfileCounter* counter = &ub_socket_profile_counters[i];
    jlong count = counter->count;
    if (count == 0) { continue; }
    jlong total_ns = counter->total_ns;
    jlong max_ns = counter->max_ns;
    jlong bytes = counter->bytes;
    jlong avg_ns = total_ns / count;
    jlong avg_bytes = bytes / count;
    st->print_cr("UBSocketProfile %-28s " INT64_FORMAT_W(12) " " INT64_FORMAT_W(14)
                 " " INT64_FORMAT_W(12) " " INT64_FORMAT_W(12)
                 " " INT64_FORMAT_W(14) " " INT64_FORMAT_W(12),
                 ub_socket_profile_events[i].name, (int64_t)count,
                 (int64_t)total_ns, (int64_t)avg_ns, (int64_t)max_ns,
                 (int64_t)bytes, (int64_t)avg_bytes);
  }
  st->flush();
}
