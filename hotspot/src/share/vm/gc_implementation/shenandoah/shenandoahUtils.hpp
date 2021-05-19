/*
 * Copyright (c) 2017, 2018, Red Hat, Inc. All rights reserved.
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

#ifndef SHARE_VM_GC_SHENANDOAHUTILS_HPP
#define SHARE_VM_GC_SHENANDOAHUTILS_HPP

#include "runtime/vmThread.hpp"
#include "gc_interface/gcCause.hpp"
#include "gc_implementation/shared/isGCActiveMark.hpp"
#include "gc_implementation/shared/vmGCOperations.hpp"
#include "memory/allocation.hpp"
#include "gc_implementation/shenandoah/shenandoahPhaseTimings.hpp"

class GCTimer;
class GCTracer;

class ShenandoahGCSession : public StackObj {
private:
  ShenandoahHeap* const _heap;
  GCTimer*  const _timer;
  GCTracer* const _tracer;

  TraceMemoryManagerStats _trace_cycle;
public:
  ShenandoahGCSession(GCCause::Cause cause);
  ~ShenandoahGCSession();
};

class ShenandoahGCPhase : public StackObj {
private:
  static ShenandoahPhaseTimings::Phase  _current_phase;

  ShenandoahPhaseTimings* const         _timings;
  const ShenandoahPhaseTimings::Phase   _phase;
  ShenandoahPhaseTimings::Phase         _parent_phase;
  double _start;

public:
  ShenandoahGCPhase(ShenandoahPhaseTimings::Phase phase);
  ~ShenandoahGCPhase();

  static ShenandoahPhaseTimings::Phase current_phase() { return _current_phase; }

  static bool is_current_phase_valid();
};

class ShenandoahGCWorkerPhase : public StackObj {
private:
  ShenandoahPhaseTimings* const       _timings;
  const ShenandoahPhaseTimings::Phase _phase;
public:
  ShenandoahGCWorkerPhase(ShenandoahPhaseTimings::Phase phase);
  ~ShenandoahGCWorkerPhase();
};

// Aggregates all the things that should happen before/after the pause.
class ShenandoahGCPauseMark : public StackObj {
private:
  ShenandoahHeap* const _heap;
  const SvcGCMarker       _svc_gc_mark;
  const IsGCActiveMark    _is_gc_active_mark;
  TraceMemoryManagerStats _trace_pause;
public:
  ShenandoahGCPauseMark(SvcGCMarker::reason_type type);
  ~ShenandoahGCPauseMark();
};

class ShenandoahSafepoint : public AllStatic {
public:
  // Check if Shenandoah GC safepoint is in progress. This is nominally
  // equivalent to calling SafepointSynchronize::is_at_safepoint(), but
  // it also checks the Shenandoah specifics, when it can.
  static inline bool is_at_shenandoah_safepoint() {
    if (!SafepointSynchronize::is_at_safepoint()) return false;

    Thread* const thr = Thread::current();
    // Shenandoah GC specific safepoints are scheduled by control thread.
    // So if we are enter here from control thread, then we are definitely not
    // at Shenandoah safepoint, but at something else.
    if (thr == ShenandoahHeap::heap()->control_thread()) return false;

    // This is not VM thread, cannot see what VM thread is doing,
    // so pretend this is a proper Shenandoah safepoint
    if (!thr->is_VM_thread()) return true;

    // Otherwise check we are at proper operation type
    VM_Operation* vm_op = VMThread::vm_operation();
    if (vm_op == NULL) return false;

    VM_Operation::VMOp_Type type = vm_op->type();
    return type == VM_Operation::VMOp_ShenandoahInitMark ||
           type == VM_Operation::VMOp_ShenandoahFinalMarkStartEvac ||
           type == VM_Operation::VMOp_ShenandoahInitUpdateRefs ||
           type == VM_Operation::VMOp_ShenandoahFinalUpdateRefs ||
           type == VM_Operation::VMOp_ShenandoahFullGC ||
           type == VM_Operation::VMOp_ShenandoahDegeneratedGC;
  }
};

class ShenandoahWorkerSession : public StackObj {
  static const uint INVALID_WORKER_ID = uint(-1);
protected:
  uint _worker_id;

  ShenandoahWorkerSession(uint worker_id);
  ~ShenandoahWorkerSession();

public:
  static inline uint worker_id() {
    Thread* thr = Thread::current();
    uint id = thr->worker_id();
    assert(id != INVALID_WORKER_ID, "Worker session has not been created");
    return id;
  }
};

class ShenandoahConcurrentWorkerSession : public ShenandoahWorkerSession {
public:
  ShenandoahConcurrentWorkerSession(uint worker_id) : ShenandoahWorkerSession(worker_id) { }
  ~ShenandoahConcurrentWorkerSession();
};

class ShenandoahParallelWorkerSession : public ShenandoahWorkerSession {
public:
  ShenandoahParallelWorkerSession(uint worker_id) : ShenandoahWorkerSession(worker_id) { }
  ~ShenandoahParallelWorkerSession();
};

class ShenandoahUtils {
public:
  static size_t round_up_power_of_2(size_t value);
};

#endif // SHARE_VM_GC_SHENANDOAHUTILS_HPP
