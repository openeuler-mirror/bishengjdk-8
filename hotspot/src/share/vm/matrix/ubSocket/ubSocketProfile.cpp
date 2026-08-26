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
#include "runtime/atomic.hpp"
#include "runtime/globals.hpp"
#include "runtime/os.hpp"
#include "runtime/orderAccess.inline.hpp"
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
  volatile jlong count;
  volatile jlong total_ns;
  volatile jlong max_ns;
  volatile jlong bytes;
};

static const UBSocketProfileEventDef ub_socket_profile_events[UB_PROF_COUNT] = {
    { "nio_write_total", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
    { "ring_write_total", UB_PROFILE_SUMMARY, UB_PROFILE_TIMING },
    { "ring_write_memcpy", UB_PROFILE_SUMMARY, UB_PROFILE_TIMING },
    { "ring_write_full", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "ring_write_partial", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "nio_read_total", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
    { "ring_read_hit", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "ring_read_empty", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "ring_read_total", UB_PROFILE_SUMMARY, UB_PROFILE_TIMING },
    { "ring_read_memcpy", UB_PROFILE_SUMMARY, UB_PROFILE_TIMING },
    { "selector_pending_check", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "selector_pending_ready", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "ub_attach_success", UB_PROFILE_SUMMARY, UB_PROFILE_COUNTER },
    { "ub_attach_fallback", UB_PROFILE_SUMMARY, UB_PROFILE_COUNTER },
    { "ring_attach_no_slot", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "selector_ready_inject", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "ring_write_max_used_bytes", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "ring_read_max_used_bytes", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "poll_notify_publish", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "poll_notify_observed", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "selector_poll_ready_inject", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "publish_to_observe_ns", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
    { "observe_to_eventfd_complete_ns", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
    { "eventfd_complete_to_selector_inject_ns", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
    { "poll_profile_invalid", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "ub_attach_skip", UB_PROFILE_SUMMARY, UB_PROFILE_COUNTER },
    { "poll_notify_request_empty", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "poll_notify_request_threshold", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "poll_notify_request_race", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "poll_notify_skip_nonempty", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "selector_spin_attempt", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "selector_spin_ready", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "selector_spin_timeout", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "selector_spin_nanos", UB_PROFILE_DETAIL, UB_PROFILE_TIMING },
    { "poll_write_offset_scanned", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "poll_write_offset_changed", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "poll_write_offset_stale", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "selector_ready_queued", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "selector_ready_drained", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "selector_eventfd_write", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "selector_eventfd_coalesced", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "close_wait_final_offset", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER },
    { "close_failure_timeout", UB_PROFILE_DETAIL, UB_PROFILE_COUNTER }
};

struct UBSocketLatencyHistogram {
  volatile jlong buckets[UB_PROFILE_LATENCY_BUCKET_COUNT];
  volatile jlong overflow;
};

static const uint32_t UB_PROFILE_SUMMARY_TIMING_SAMPLE_RATE = 16;
static __thread uint32_t ub_socket_profile_sample_seq[UB_PROF_COUNT];
static UBSocketProfileCounter ub_socket_profile_counters[UB_PROF_COUNT];
static UBSocketLatencyHistogram ub_socket_latency_histograms[3];

static jlong ub_socket_profile_load(volatile jlong* value) {
  return OrderAccess::load_acquire(value);
}

static void ub_socket_profile_add(volatile jlong* value, jlong delta) {
  if (delta != 0) {
    Atomic::add(delta, value);
  }
}

static void ub_socket_profile_max(volatile jlong* value, jlong candidate) {
  jlong current = ub_socket_profile_load(value);
  while (candidate > current) {
    jlong observed = Atomic::cmpxchg(candidate, value, current);
    if (observed == current) {
      return;
    }
    current = observed;
  }
}

static bool ub_socket_is_latency_event(UBSocketProfileEvent event) {
  return event >= UB_PROF_PUBLISH_TO_OBSERVE_NS &&
         event <= UB_PROF_EVENTFD_TO_SELECTOR_INJECT_NS;
}

static UBSocketLatencyHistogram* ub_socket_latency_histogram(
    UBSocketProfileEvent event) {
  if (!ub_socket_is_latency_event(event)) { return NULL; }
  return &ub_socket_latency_histograms[
      event - UB_PROF_PUBLISH_TO_OBSERVE_NS];
}

static void ub_socket_latency_percentiles(
    UBSocketLatencyHistogram* histogram, jlong count,
    jlong* p50, bool* p50_in_range, jlong* p90, bool* p90_in_range,
    jlong* p99, bool* p99_in_range) {
  *p50 = 0;
  *p90 = 0;
  *p99 = 0;
  *p50_in_range = false;
  *p90_in_range = false;
  *p99_in_range = false;
  if (histogram == NULL || count <= 0) { return; }

  const jlong target50 = (count * 50 + 99) / 100;
  const jlong target90 = (count * 90 + 99) / 100;
  const jlong target99 = (count * 99 + 99) / 100;
  jlong cumulative = 0;
  for (int32_t i = 0; i < UB_PROFILE_LATENCY_BUCKET_COUNT; i++) {
    cumulative += ub_socket_profile_load(&histogram->buckets[i]);
    if (!*p50_in_range && cumulative >= target50) {
      *p50 = (jlong)i * UB_PROFILE_LATENCY_BUCKET_NS;
      *p50_in_range = true;
    }
    if (!*p90_in_range && cumulative >= target90) {
      *p90 = (jlong)i * UB_PROFILE_LATENCY_BUCKET_NS;
      *p90_in_range = true;
    }
    if (!*p99_in_range && cumulative >= target99) {
      *p99 = (jlong)i * UB_PROFILE_LATENCY_BUCKET_NS;
      *p99_in_range = true;
      break;
    }
  }
}

static void ub_socket_format_latency_percentile(char* buf, size_t len,
                                                bool in_range, jlong value) {
  if (!in_range) {
    jio_snprintf(buf, len, ">" INT64_FORMAT,
                 (int64_t)UB_PROFILE_LATENCY_MAX_NS);
    return;
  }
  jio_snprintf(buf, len, INT64_FORMAT, (int64_t)value);
}

bool UBSocketProfiler::enabled_impl(UBSocketProfileEvent event) {
  return event >= 0 && event < UB_PROF_COUNT &&
         UBSocketProfile >= ub_socket_profile_events[event].level;
}

uint64_t UBSocketProfiler::start_impl(UBSocketProfileEvent event) {
  if (!enabled_impl(event) ||
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

uint64_t UBSocketProfiler::end_impl(UBSocketProfileEvent event,
                                    uint64_t start_ns, uint64_t bytes) {
  if (!enabled_impl(event) ||
      ub_socket_profile_events[event].kind != UB_PROFILE_TIMING) {
    return 0;
  }
  UBSocketProfileCounter* counter = &ub_socket_profile_counters[event];
  ub_socket_profile_add(&counter->count, 1);
  if (bytes != 0) {
    ub_socket_profile_add(&counter->bytes, (jlong)bytes);
  }
  if (start_ns == 0) {
    return 0;
  }
  uint64_t ns = (uint64_t)os::javaTimeNanos() - start_ns;
  uint64_t total_ns = ns;
  if (UBSocketProfile == UB_PROFILE_SUMMARY) {
    total_ns *= UB_PROFILE_SUMMARY_TIMING_SAMPLE_RATE;
  }
  ub_socket_profile_add(&counter->total_ns, (jlong)total_ns);
  ub_socket_profile_max(&counter->max_ns, (jlong)ns);
  return ns;
}

void UBSocketProfiler::count_impl(UBSocketProfileEvent event, uint64_t bytes) {
  if (!enabled_impl(event) ||
      ub_socket_profile_events[event].kind != UB_PROFILE_COUNTER) {
    return;
  }
  UBSocketProfileCounter* counter = &ub_socket_profile_counters[event];
  ub_socket_profile_add(&counter->count, 1);
  if (bytes != 0) {
    ub_socket_profile_add(&counter->bytes, (jlong)bytes);
  }
}

void UBSocketProfiler::max_bytes_impl(UBSocketProfileEvent event,
                                      uint64_t bytes) {
  if (!enabled_impl(event) ||
      ub_socket_profile_events[event].kind != UB_PROFILE_COUNTER) {
    return;
  }
  UBSocketProfileCounter* counter = &ub_socket_profile_counters[event];
  ub_socket_profile_add(&counter->count, 1);
  ub_socket_profile_max(&counter->bytes, (jlong)bytes);
}

void UBSocketProfiler::record_impl(UBSocketProfileEvent event,
                                   uint64_t elapsed_ns, uint64_t bytes,
                                   uint64_t count) {
  if (count == 0 || !enabled_impl(event)) {
    return;
  }
  UBSocketProfileCounter* counter = &ub_socket_profile_counters[event];
  ub_socket_profile_add(&counter->count, (jlong)count);
  if (bytes != 0) {
    ub_socket_profile_add(&counter->bytes, (jlong)bytes);
  }
  if (ub_socket_profile_events[event].kind != UB_PROFILE_TIMING) {
    return;
  }
  ub_socket_profile_add(&counter->total_ns, (jlong)elapsed_ns);
  ub_socket_profile_max(&counter->max_ns, (jlong)elapsed_ns);
}

void UBSocketProfiler::record_latency_impl(UBSocketProfileEvent event,
                                           uint64_t elapsed_ns) {
  if (elapsed_ns == 0 || !enabled_impl(event) ||
      !ub_socket_is_latency_event(event)) {
    return;
  }
  record_impl(event, elapsed_ns, 0, 1);
  UBSocketLatencyHistogram* histogram = ub_socket_latency_histogram(event);
  uint64_t bucket = elapsed_ns / UB_PROFILE_LATENCY_BUCKET_NS;
  if (bucket >= UB_PROFILE_LATENCY_BUCKET_COUNT) {
    ub_socket_profile_add(&histogram->overflow, 1);
  } else {
    ub_socket_profile_add(&histogram->buckets[bucket], 1);
  }
}

void UBSocketProfiler::print_summary(outputStream* force_output) {
  if (UBSocketProfile == UB_PROFILE_OFF) { return; }
  outputStream* st = force_output != NULL
      ? force_output : MatrixLog::stream(UB_SOCKET);
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
    jlong count = ub_socket_profile_load(&counter->count);
    if (count == 0) { continue; }
    jlong total_ns = ub_socket_profile_load(&counter->total_ns);
    jlong max_ns = ub_socket_profile_load(&counter->max_ns);
    jlong bytes = ub_socket_profile_load(&counter->bytes);
    jlong avg_ns = total_ns / count;
    jlong avg_bytes = bytes / count;
    st->print_cr("UBSocketProfile %-28s " INT64_FORMAT_W(12) " " INT64_FORMAT_W(14)
                 " " INT64_FORMAT_W(12) " " INT64_FORMAT_W(12)
                 " " INT64_FORMAT_W(14) " " INT64_FORMAT_W(12),
                 ub_socket_profile_events[i].name, (int64_t)count,
                 (int64_t)total_ns, (int64_t)avg_ns, (int64_t)max_ns,
                 (int64_t)bytes, (int64_t)avg_bytes);
    UBSocketProfileEvent event = (UBSocketProfileEvent)i;
    UBSocketLatencyHistogram* histogram = ub_socket_latency_histogram(event);
    if (histogram != NULL) {
      jlong p50 = 0;
      jlong p90 = 0;
      jlong p99 = 0;
      bool p50_in_range;
      bool p90_in_range;
      bool p99_in_range;
      ub_socket_latency_percentiles(
          histogram, count, &p50, &p50_in_range, &p90, &p90_in_range,
          &p99, &p99_in_range);
      char p50_text[32];
      char p90_text[32];
      char p99_text[32];
      ub_socket_format_latency_percentile(
          p50_text, sizeof(p50_text), p50_in_range, p50);
      ub_socket_format_latency_percentile(
          p90_text, sizeof(p90_text), p90_in_range, p90);
      ub_socket_format_latency_percentile(
          p99_text, sizeof(p99_text), p99_in_range, p99);
      st->print_cr("UBSocketProfileLatency %s count=" INT64_FORMAT
                   " p50_ns=%s p90_ns=%s p99_ns=%s max_ns=" INT64_FORMAT
                   " overflow=" INT64_FORMAT,
                   ub_socket_profile_events[i].name, (int64_t)count,
                   p50_text, p90_text, p99_text, (int64_t)max_ns,
                   (int64_t)ub_socket_profile_load(&histogram->overflow));
    }
  }
  st->flush();
}
