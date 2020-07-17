/*
 * Copyright (c) 2017, 2019, Red Hat, Inc. All rights reserved.
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

#include "gc_implementation/shenandoah/shenandoahCollectorPolicy.hpp"
#include "gc_implementation/shenandoah/shenandoahMarkCompact.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahUtils.hpp"
#include "gc_implementation/shenandoah/shenandoahLogging.hpp"
#include "gc_implementation/shenandoah/heuristics/shenandoahHeuristics.hpp"
#include "gc_interface/gcCause.hpp"
#include "gc_implementation/shared/gcTimer.hpp"
#include "gc_implementation/shared/gcWhen.hpp"
#include "gc_implementation/shared/gcTrace.hpp"
#include "utilities/debug.hpp"

ShenandoahPhaseTimings::Phase ShenandoahGCPhase::_current_phase = ShenandoahPhaseTimings::_invalid_phase;

ShenandoahGCSession::ShenandoahGCSession(GCCause::Cause cause) :
  _heap(ShenandoahHeap::heap()),
  _timer(ShenandoahHeap::heap()->gc_timer()),
  _tracer(ShenandoahHeap::heap()->tracer()) {

  assert(!ShenandoahGCPhase::is_current_phase_valid(), "No current GC phase");

  _heap->set_gc_cause(cause);
  _timer->register_gc_start();
  _tracer->report_gc_start(cause, _timer->gc_start());
  _heap->trace_heap(GCWhen::BeforeGC, _tracer);

  _heap->shenandoah_policy()->record_cycle_start();
  _heap->heuristics()->record_cycle_start();
  _trace_cycle.initialize(false, cause,
          /* allMemoryPoolsAffected */    true,
          /* recordGCBeginTime = */       true,
          /* recordPreGCUsage = */        true,
          /* recordPeakUsage = */         true,
          /* recordPostGCUsage = */       true,
          /* recordAccumulatedGCTime = */ true,
          /* recordGCEndTime = */         true,
          /* countCollection = */         true
  );
}

ShenandoahGCSession::~ShenandoahGCSession() {
  ShenandoahHeap::heap()->heuristics()->record_cycle_end();
  _tracer->report_gc_end(_timer->gc_end(), _timer->time_partitions());
  _timer->register_gc_end();

  assert(!ShenandoahGCPhase::is_current_phase_valid(), "No current GC phase");
  _heap->set_gc_cause(GCCause::_no_gc);
}

ShenandoahGCPauseMark::ShenandoahGCPauseMark(SvcGCMarker::reason_type type) :
        _heap(ShenandoahHeap::heap()), _svc_gc_mark(type), _is_gc_active_mark() {
  // FIXME: It seems that JMC throws away level 0 events, which are the Shenandoah
  // pause events. Create this pseudo level 0 event to push real events to level 1.
  _heap->gc_timer()->register_gc_phase_start("Shenandoah", Ticks::now());
  _trace_pause.initialize(true, _heap->gc_cause(),
          /* allMemoryPoolsAffected */    true,
          /* recordGCBeginTime = */       true,
          /* recordPreGCUsage = */        false,
          /* recordPeakUsage = */         false,
          /* recordPostGCUsage = */       false,
          /* recordAccumulatedGCTime = */ true,
          /* recordGCEndTime = */         true,
          /* countCollection = */         true
  );

  _heap->heuristics()->record_gc_start();
}

ShenandoahGCPauseMark::~ShenandoahGCPauseMark() {
  _heap->gc_timer()->register_gc_phase_end(Ticks::now());
  _heap->heuristics()->record_gc_end();
}

ShenandoahGCPhase::ShenandoahGCPhase(const ShenandoahPhaseTimings::Phase phase) :
  _timings(ShenandoahHeap::heap()->phase_timings()), _phase(phase) {
  assert(Thread::current()->is_VM_thread() ||
         Thread::current()->is_ConcurrentGC_thread(),
        "Must be set by these threads");
  _parent_phase = _current_phase;
  _current_phase = phase;
  _start = os::elapsedTime();
}

ShenandoahGCPhase::~ShenandoahGCPhase() {
  _timings->record_phase_time(_phase, os::elapsedTime() - _start);
  _current_phase = _parent_phase;
}

bool ShenandoahGCPhase::is_current_phase_valid() {
  return _current_phase < ShenandoahPhaseTimings::_num_phases;
}

ShenandoahGCWorkerPhase::ShenandoahGCWorkerPhase(const ShenandoahPhaseTimings::Phase phase) :
    _timings(ShenandoahHeap::heap()->phase_timings()), _phase(phase) {
  _timings->record_workers_start(_phase);
}

ShenandoahGCWorkerPhase::~ShenandoahGCWorkerPhase() {
  _timings->record_workers_end(_phase);
}

ShenandoahWorkerSession::ShenandoahWorkerSession(uint worker_id) : _worker_id(worker_id) {
  Thread* thr = Thread::current();
  assert(thr->worker_id() == INVALID_WORKER_ID, "Already set");
  thr->set_worker_id(worker_id);
}

ShenandoahConcurrentWorkerSession::~ShenandoahConcurrentWorkerSession() {
  // Do nothing. Per-worker events are not supported in this JDK.
}

ShenandoahParallelWorkerSession::~ShenandoahParallelWorkerSession() {
  // Do nothing. Per-worker events are not supported in this JDK.
}

ShenandoahWorkerSession::~ShenandoahWorkerSession() {
#ifdef ASSERT
  Thread* thr = Thread::current();
  assert(thr->worker_id() != INVALID_WORKER_ID, "Must be set");
  thr->set_worker_id(INVALID_WORKER_ID);
#endif
}
