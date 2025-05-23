/*
 * Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
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
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "gc_implementation/g1/g1FullGCScope.hpp"
#include "gc_implementation/g1/g1Log.hpp"

G1FullGCScope* G1FullGCScope::_instance = NULL;

G1FullGCScope* G1FullGCScope::instance() {
  assert(_instance != NULL, "Must be setup already");
  return _instance;
}

G1FullGCScope::G1FullGCScope(bool explicit_gc, bool clear_soft) :
    _rm(),
    _explicit_gc(explicit_gc),
    _g1h(G1CollectedHeap::heap()),
    _svc_marker(SvcGCMarker::FULL),
    _timer(),
    _tracer(),
    _active(),
    _cpu_time(G1Log::finer(), true, gclog_or_tty),
    _soft_refs(clear_soft, _g1h->collector_policy()),
    _memory_stats(true, _g1h->gc_cause()),
    _collector_stats(_g1h->g1mm()->full_collection_counters()) {
  assert(_instance == NULL, "Only one scope at a time");
  _timer.register_gc_start();
  _tracer.report_gc_start(_g1h->gc_cause(), _timer.gc_start());
  _g1h->pre_full_gc_dump(&_timer);
  _g1h->trace_heap_before_gc(&_tracer);
  _instance = this;
}

G1FullGCScope::~G1FullGCScope() {
  // We must call G1MonitoringSupport::update_sizes() in the same scoping level
  // as an active TraceMemoryManagerStats object (i.e. before the destructor for the
  // TraceMemoryManagerStats is called) so that the G1 memory pools are updated
  // before any GC notifications are raised.
  _g1h->g1mm()->update_sizes();
  _g1h->trace_heap_after_gc(&_tracer);
  _g1h->post_full_gc_dump(&_timer);
  _timer.register_gc_end();
  _tracer.report_gc_end(_timer.gc_end(), _timer.time_partitions());
  _instance = NULL;
}

bool G1FullGCScope::is_explicit_gc() {
  return _explicit_gc;
}

bool G1FullGCScope::should_clear_soft_refs() {
  return _soft_refs.should_clear();
}

STWGCTimer* G1FullGCScope::timer() {
  return &_timer;
}

SerialOldTracer* G1FullGCScope::tracer() {
  return &_tracer;
}