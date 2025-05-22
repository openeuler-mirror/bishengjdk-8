/*
 * Copyright (c) 2001, 2015, Oracle and/or its affiliates. All rights reserved.
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
#include "gc_implementation/g1/concurrentMarkThread.inline.hpp"
#include "gc_implementation/g1/g1CollectedHeap.inline.hpp"
#include "gc_implementation/g1/g1CollectorPolicy.hpp"
#include "gc_implementation/g1/g1Log.hpp"
#include "gc_implementation/g1/vm_operations_g1.hpp"
#include "gc_implementation/shared/gcTimer.hpp"
#include "gc_implementation/shared/gcTraceTime.hpp"
#include "gc_implementation/shared/isGCActiveMark.hpp"
#include "gc_implementation/g1/vm_operations_g1.hpp"
#include "runtime/interfaceSupport.hpp"

VM_G1CollectForAllocation::VM_G1CollectForAllocation(uint gc_count_before,
                                                     size_t word_size)
  : VM_G1OperationWithAllocRequest(gc_count_before, word_size,
                                   GCCause::_allocation_failure) {
  guarantee(word_size != 0, "An allocation should always be requested with this operation.");
}

void VM_G1CollectForAllocation::doit() {
  G1CollectedHeap* g1h = G1CollectedHeap::heap();
  GCCauseSetter x(g1h, _gc_cause);

  _result = g1h->satisfy_failed_allocation(_word_size, allocation_context(), &_pause_succeeded);
  assert(_result == NULL || _pause_succeeded,
         "if we get back a result, the pause should have succeeded");
}

void VM_G1CollectFull::doit() {
  G1CollectedHeap* g1h = G1CollectedHeap::heap();
  GCCauseSetter x(g1h, _gc_cause);
  g1h->do_full_collection(false /* clear_all_soft_refs */);
}

VM_G1IncCollectionPause::VM_G1IncCollectionPause(uint           gc_count_before,
                                                 size_t         word_size,
                                                 bool           should_initiate_conc_mark,
                                                 double         target_pause_time_ms,
                                                 GCCause::Cause gc_cause)
  : VM_G1OperationWithAllocRequest(gc_count_before, word_size, gc_cause),
    _should_initiate_conc_mark(should_initiate_conc_mark),
    _target_pause_time_ms(target_pause_time_ms),
    _should_retry_gc(false),
    _old_marking_cycles_completed_before(0) {
  guarantee(target_pause_time_ms > 0.0,
            err_msg("target_pause_time_ms = %1.6lf should be positive",
                    target_pause_time_ms));
  _gc_cause = gc_cause;
}

bool VM_G1IncCollectionPause::doit_prologue() {
  bool res = VM_G1OperationWithAllocRequest::doit_prologue();
  if (!res) {
    if (_should_initiate_conc_mark) {
      // The prologue can fail for a couple of reasons. The first is that another GC
      // got scheduled and prevented the scheduling of the initial mark GC. The
      // second is that the GC locker may be active and the heap can't be expanded.
      // In both cases we want to retry the GC so that the initial mark pause is
      // actually scheduled. In the second case, however, we should stall until
      // until the GC locker is no longer active and then retry the initial mark GC.
      _should_retry_gc = true;
    }
  }
  return res;
}

void VM_G1IncCollectionPause::doit() {
  G1CollectedHeap* g1h = G1CollectedHeap::heap();
  assert(!_should_initiate_conc_mark || g1h->should_do_concurrent_full_gc(_gc_cause),
      "only a GC locker, a System.gc(), stats update, whitebox, or a hum allocation induced GC should start a cycle");

  if (_word_size > 0) {
    // An allocation has been requested. So, try to do that first.
    _result = g1h->attempt_allocation_at_safepoint(_word_size, allocation_context(),
                                     false /* expect_null_cur_alloc_region */);
    if (_result != NULL) {
      // If we can successfully allocate before we actually do the
      // pause then we will consider this pause successful.
      _pause_succeeded = true;
      return;
    }
  }

  GCCauseSetter x(g1h, _gc_cause);
  if (_should_initiate_conc_mark) {
    // It's safer to read old_marking_cycles_completed() here, given
    // that noone else will be updating it concurrently. Since we'll
    // only need it if we're initiating a marking cycle, no point in
    // setting it earlier.
    _old_marking_cycles_completed_before = g1h->old_marking_cycles_completed();

    // At this point we are supposed to start a concurrent cycle. We
    // will do so if one is not already in progress.
    bool res = g1h->g1_policy()->force_initial_mark_if_outside_cycle(_gc_cause);

    // The above routine returns true if we were able to force the
    // next GC pause to be an initial mark; it returns false if a
    // marking cycle is already in progress.
    //
    // If a marking cycle is already in progress just return and skip the
    // pause below - if the reason for requesting this initial mark pause
    // was due to a System.gc() then the requesting thread should block in
    // doit_epilogue() until the marking cycle is complete.
    //
    // If this initial mark pause was requested as part of a humongous
    // allocation then we know that the marking cycle must just have
    // been started by another thread (possibly also allocating a humongous
    // object) as there was no active marking cycle when the requesting
    // thread checked before calling collect() in
    // attempt_allocation_humongous(). Retrying the GC, in this case,
    // will cause the requesting thread to spin inside collect() until the
    // just started marking cycle is complete - which may be a while. So
    // we do NOT retry the GC.
    if (!res) {
      assert(_word_size == 0, "Concurrent Full GC/Humongous Object IM shouldn't be allocating");
      if (_gc_cause != GCCause::_g1_humongous_allocation) {
        _should_retry_gc = true;
      }
      return;
    }
  }

  _pause_succeeded =
    g1h->do_collection_pause_at_safepoint(_target_pause_time_ms);
  if (_pause_succeeded && _word_size > 0) {
    // An allocation had been requested.
    _result = g1h->attempt_allocation_at_safepoint(_word_size, allocation_context(),
                                      true /* expect_null_cur_alloc_region */);
  } else {
    assert(_result == NULL, "invariant");
    if (!_pause_succeeded) {
      // Another possible reason reason for the pause to not be successful
      // is that, again, the GC locker is active (and has become active
      // since the prologue was executed). In this case we should retry
      // the pause after waiting for the GC locker to become inactive.
      _should_retry_gc = true;
    }
  }
}

void VM_G1IncCollectionPause::doit_epilogue() {
  VM_G1OperationWithAllocRequest::doit_epilogue();

  // If the pause was initiated by a System.gc() and
  // +ExplicitGCInvokesConcurrent, we have to wait here for the cycle
  // that just started (or maybe one that was already in progress) to
  // finish.
  if (_gc_cause == GCCause::_java_lang_system_gc &&
      _should_initiate_conc_mark) {
    assert(ExplicitGCInvokesConcurrent,
           "the only way to be here is if ExplicitGCInvokesConcurrent is set");

    G1CollectedHeap* g1h = G1CollectedHeap::heap();

    // In the doit() method we saved g1h->old_marking_cycles_completed()
    // in the _old_marking_cycles_completed_before field. We have to
    // wait until we observe that g1h->old_marking_cycles_completed()
    // has increased by at least one. This can happen if a) we started
    // a cycle and it completes, b) a cycle already in progress
    // completes, or c) a Full GC happens.

    // If the condition has already been reached, there's no point in
    // actually taking the lock and doing the wait.
    if (g1h->old_marking_cycles_completed() <=
                                          _old_marking_cycles_completed_before) {
      // The following is largely copied from CMS

      Thread* thr = Thread::current();
      assert(thr->is_Java_thread(), "invariant");
      JavaThread* jt = (JavaThread*)thr;
      ThreadToNativeFromVM native(jt);

      MutexLockerEx x(FullGCCount_lock, Mutex::_no_safepoint_check_flag);
      while (g1h->old_marking_cycles_completed() <=
                                          _old_marking_cycles_completed_before) {
        FullGCCount_lock->wait(Mutex::_no_safepoint_check_flag);
      }
    }
  }
}

void VM_CGC_Operation::acquire_pending_list_lock() {
  assert(_needs_pll, "don't call this otherwise");
  // The caller may block while communicating
  // with the SLT thread in order to acquire/release the PLL.
  SurrogateLockerThread* slt = ConcurrentMarkThread::slt();
  if (slt != NULL) {
    slt->manipulatePLL(SurrogateLockerThread::acquirePLL);
  } else {
    SurrogateLockerThread::report_missing_slt();
  }
}

void VM_CGC_Operation::release_and_notify_pending_list_lock() {
  assert(_needs_pll, "don't call this otherwise");
  // The caller may block while communicating
  // with the SLT thread in order to acquire/release the PLL.
  ConcurrentMarkThread::slt()->
    manipulatePLL(SurrogateLockerThread::releaseAndNotifyPLL);
}

void VM_CGC_Operation::doit() {
  TraceCPUTime tcpu(G1Log::finer(), true, gclog_or_tty);
  GCTraceTime t(_printGCMessage, G1Log::fine(), true, G1CollectedHeap::heap()->gc_timer_cm(), G1CollectedHeap::heap()->concurrent_mark()->concurrent_gc_id());
  SharedHeap* sh = SharedHeap::heap();
  // This could go away if CollectedHeap gave access to _gc_is_active...
  if (sh != NULL) {
    IsGCActiveMark x;
    _cl->do_void();
  } else {
    _cl->do_void();
  }
}

bool VM_CGC_Operation::doit_prologue() {
  // Note the relative order of the locks must match that in
  // VM_GC_Operation::doit_prologue() or deadlocks can occur
  if (_needs_pll) {
    acquire_pending_list_lock();
  }

  Heap_lock->lock();
  SharedHeap::heap()->_thread_holds_heap_lock_for_gc = true;
  return true;
}

void VM_CGC_Operation::doit_epilogue() {
  // Note the relative order of the unlocks must match that in
  // VM_GC_Operation::doit_epilogue()
  SharedHeap::heap()->_thread_holds_heap_lock_for_gc = false;
  Heap_lock->unlock();
  if (_needs_pll) {
    release_and_notify_pending_list_lock();
  }
}

G1_ChangeMaxHeapOp::G1_ChangeMaxHeapOp(size_t new_max_heap) :
  VM_ChangeMaxHeapOp(new_max_heap) {
}

/*
 * No need calculate young/old size, shrink will adjust young automatically.
 * ensure young_list_length, _young_list_max_length, _young_list_target_length align.
 *
 * 1. check if need perform gc: new_heap_max >= minimum_desired_capacity
 * 2. perform full GC if necessary
 * 3. update new limit
 * 4. validation
 */
void G1_ChangeMaxHeapOp::doit() {
  G1CollectedHeap* heap      = (G1CollectedHeap*)Universe::heap();
  G1CollectorPolicy* policy  = heap->g1_policy();
  const size_t min_heap_size = policy->min_heap_byte_size();
  const size_t max_heap_size = heap->current_max_heap_size();
  bool is_shrink             = _new_max_heap < max_heap_size;
  bool is_valid              = false;

  // step1. calculate maximum_used_percentage for shrink validity check
  const double minimum_free_percentage = (double) MinHeapFreeRatio / 100.0;
  const double maximum_used_percentage = 1.0 - minimum_free_percentage;

  // step2 trigger GC as needed and resize
  if (is_shrink) {
    trigger_gc_shrink(_new_max_heap, maximum_used_percentage, max_heap_size, is_valid);
    if (!is_valid) {
      // We should not reach here because we have already checked the existence of
      // the ACC and disabled this feature when the ACC is absent.
      DMH_LOG("G1_ChangeMaxHeapOp fail for missing ACC");
      return;
    }
  }

  DMH_LOG("G1_ChangeMaxHeapOp: current capacity " SIZE_FORMAT "K, new max heap " SIZE_FORMAT "K",
          heap->capacity() / K, _new_max_heap / K);

  // step3 check if can update new limit
  if (heap->capacity() <= _new_max_heap) {
    uint dynamic_max_heap_len = os::Linux::dmh_g1_get_region_limit(_new_max_heap, HeapRegion::GrainBytes, is_valid);
    if (!is_valid) {
      // We should not reach here because we have already checked the existence of
      // the ACC and disabled this feature when the ACC is absent.
      DMH_LOG("G1_ChangeMaxHeapOp fail for missing ACC");
      return;
    }
    heap->set_current_max_heap_size(_new_max_heap);
    heap->hrm()->set_dynamic_max_heap_length(dynamic_max_heap_len);
    // G1 young/old share same max size
    heap->update_gen_max_counter(_new_max_heap);
    _resize_success = true;
    DMH_LOG("G1_ChangeMaxHeapOp success");
  } else {
    DMH_LOG("G1_ChangeMaxHeapOp fail");
  }
}

void G1_ChangeMaxHeapOp::trigger_gc_shrink(size_t _new_max_heap,
                                           double maximum_used_percentage,
                                           size_t max_heap_size,
                                           bool &is_valid) {
    G1CollectedHeap* heap = (G1CollectedHeap*)Universe::heap();
    G1CollectorPolicy* policy = heap->g1_policy();
    bool triggered_full_gc = false;
    bool can_shrink = os::Linux::dmh_g1_can_shrink((double)heap->used(), _new_max_heap, maximum_used_percentage, max_heap_size, is_valid);
    if (!is_valid) {
      return;
    }
    if (!can_shrink) {
      // trigger Young GC
      policy->set_gcs_are_young(true);
      GCCauseSetter gccs(heap, _gc_cause);
      bool minor_gc_succeeded = heap->do_collection_pause_at_safepoint(policy->max_pause_time_ms());
      if (minor_gc_succeeded) {
        DMH_LOG("G1_ChangeMaxHeapOp heap after Young GC");
        if (TraceDynamicMaxHeap) {
          heap->print_on(tty);
        }
      }
      can_shrink = os::Linux::dmh_g1_can_shrink((double)heap->used(), _new_max_heap, maximum_used_percentage, max_heap_size, is_valid);
      if (!is_valid) {
        return;
      }
      if (!can_shrink) {
        // trigger Full GC and adjust everything in resize_if_necessary_after_full_collection
        heap->set_exp_dynamic_max_heap_size(_new_max_heap);
        heap->do_full_collection(true);
        DMH_LOG("G1_ChangeMaxHeapOp heap after Full GC");
        if (TraceDynamicMaxHeap) {
          heap->print_on(tty);
        }
        heap->set_exp_dynamic_max_heap_size(0);
        triggered_full_gc = true;
      }
    }
    if (!triggered_full_gc) {
      // there may be two situations when entering this branch:
      //     1. first check passed, no GC triggered
      //     2. first check failed, triggered Young GC,
      //        second check passed
      // so the shrink has not been completed and it must be valid to shrink
      g1_shrink_without_full_gc(_new_max_heap);
    }
}

void G1_ChangeMaxHeapOp::g1_shrink_without_full_gc(size_t _new_max_heap) {
  G1CollectedHeap* heap = (G1CollectedHeap*)Universe::heap();
  size_t capacity_before_shrink = heap->capacity();
  // _new_max_heap is large enough, do nothing
  if (_new_max_heap >= capacity_before_shrink) {
    return;
  }
  // Capacity too large, compute shrinking size and shrink
  size_t shrink_bytes = capacity_before_shrink - _new_max_heap;
  heap->verify_region_sets_optional();
  heap->tear_down_region_sets(true /* free_list_only */);
  heap->shrink_helper(shrink_bytes);
  heap->rebuild_region_sets(true /* free_list_only */, true /* is_dynamic_max_heap_shrink */);
  heap->_hrm.verify_optional();
  heap->verify_region_sets_optional();
  heap->verify_after_gc();

  DMH_LOG("G1_ChangeMaxHeapOp: attempt heap shrinking for dynamic max heap %s "
          "origin capacity " SIZE_FORMAT "K "
          "new capacity " SIZE_FORMAT "K "
          "shrink by " SIZE_FORMAT "K",
          heap->capacity() <= _new_max_heap ? "success":"fail",
          capacity_before_shrink / K,
          heap->capacity() / K,
          shrink_bytes / K);
}
