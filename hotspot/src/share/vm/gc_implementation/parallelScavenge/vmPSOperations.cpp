/*
 * Copyright (c) 2007, 2015, Oracle and/or its affiliates. All rights reserved.
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
#include "gc_implementation/parallelScavenge/parallelScavengeHeap.inline.hpp"
#include "gc_implementation/parallelScavenge/psMarkSweep.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.inline.hpp"
#include "gc_implementation/parallelScavenge/vmPSOperations.hpp"
#include "memory/gcLocker.inline.hpp"
#include "utilities/dtrace.hpp"

// The following methods are used by the parallel scavenge collector
VM_ParallelGCFailedAllocation::VM_ParallelGCFailedAllocation(size_t word_size,
                                                             uint gc_count) :
    VM_CollectForAllocation(word_size, gc_count, GCCause::_allocation_failure) {
  assert(word_size != 0, "An allocation should always be requested with this operation.");
}

void VM_ParallelGCFailedAllocation::doit() {
  SvcGCMarker sgcm(SvcGCMarker::MINOR);

  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "must be a ParallelScavengeHeap");

  GCCauseSetter gccs(heap, _gc_cause);
  _result = heap->failed_mem_allocate(_word_size);

  if (_result == NULL && GC_locker::is_active_and_needs_gc()) {
    set_gc_locked();
  }
}

static bool is_cause_full(GCCause::Cause cause) {
  return (cause != GCCause::_gc_locker) && (cause != GCCause::_wb_young_gc)
         DEBUG_ONLY(&& (cause != GCCause::_scavenge_alot));
}

// Only used for System.gc() calls
VM_ParallelGCSystemGC::VM_ParallelGCSystemGC(uint gc_count,
                                             uint full_gc_count,
                                             GCCause::Cause gc_cause) :
  VM_GC_Operation(gc_count, gc_cause, full_gc_count, is_cause_full(gc_cause))
{
}

void VM_ParallelGCSystemGC::doit() {
  SvcGCMarker sgcm(SvcGCMarker::FULL);

  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap,
    "must be a ParallelScavengeHeap");

  GCCauseSetter gccs(heap, _gc_cause);
  if (!_full) {
    // If (and only if) the scavenge fails, this will invoke a full gc.
    heap->invoke_scavenge();
  } else {
    heap->do_full_collection(false);
  }
}

PS_ChangeMaxHeapOp::PS_ChangeMaxHeapOp(size_t new_max_heap) :
  VM_ChangeMaxHeapOp(new_max_heap) {
}

/*
 * 1. calculate new young/old gen limit size.
 * 2. trigger Full GC if necessary
 * 3. check and reset new limitation
 */
void PS_ChangeMaxHeapOp::doit() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  GenCollectorPolicy* policy = (GenCollectorPolicy*)(heap->collector_policy());
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "must be a ParallelScavengeHeap");

  // step1
  PSOldGen* old_gen      = heap->old_gen();
  PSYoungGen* young_gen  = heap->young_gen();
  size_t cur_heap_limit  = heap->current_max_heap_size();
  size_t cur_old_limit   = old_gen->gen_size_limit();
  size_t cur_young_limit = young_gen->gen_size_limit();
  size_t gen_alignment   = policy->gen_alignment();
  bool is_shrink         = _new_max_heap < cur_heap_limit;

  const size_t young_reserved_size = young_gen->reserved().byte_size();
  const size_t young_min_size =  young_gen->min_gen_size();
  const size_t old_reserved_size = old_gen->reserved().byte_size();
  const size_t old_min_size = old_gen->min_gen_size();

  guarantee(cur_old_limit + cur_young_limit == cur_heap_limit, "must be");

  // fix with young gen size limitation
  size_t new_young_limit = policy->scale_by_NewRatio_aligned(_new_max_heap);
  new_young_limit = MIN2(new_young_limit, young_reserved_size);
  new_young_limit = MAX2(new_young_limit, young_min_size);
  // align shrink/expand direction
  if ((is_shrink && (new_young_limit > cur_young_limit)) ||
      (!is_shrink && (new_young_limit < cur_young_limit))) {
    new_young_limit = cur_young_limit;
  }
  size_t new_old_limit = _new_max_heap - new_young_limit;

  if (new_old_limit > old_reserved_size) {
    new_old_limit = old_reserved_size;
    new_young_limit = _new_max_heap - new_old_limit;
  }

  // keep the new_old_limit aligned with shrink/expand direction
  if ((is_shrink && (new_old_limit > cur_old_limit)) ||
      (!is_shrink && (new_old_limit < cur_old_limit))) {
    new_old_limit = cur_old_limit;
    new_young_limit = _new_max_heap - new_old_limit;
  }

  // After the final calcuation, check the leagle limit
  if ((new_old_limit < old_min_size) ||
      (new_old_limit > old_reserved_size) ||
      (new_young_limit < young_min_size) ||
      (new_young_limit > young_reserved_size)) {
    DMH_LOG("PS_ElasticMaxHeapOp abort: can not calculate new legal limit:"
             " new_old_limit: "SIZE_FORMAT"K, " "old gen min size: "SIZE_FORMAT"K, old gen reserved size: "SIZE_FORMAT"K"
             " new_young_limit: "SIZE_FORMAT"K, ""young gen min size: "SIZE_FORMAT"K, young gen reserved size: "SIZE_FORMAT"K",
             (new_old_limit / K), (old_min_size / K), (old_reserved_size / K),
             (new_young_limit / K), (young_min_size / K), (young_reserved_size / K));
    return;
  }

  DMH_LOG("PS_ElasticMaxHeapOp plan: "
          "desired young gen size (" SIZE_FORMAT "K" "->" SIZE_FORMAT "K), "
          "desired old gen size (" SIZE_FORMAT "K" "->" SIZE_FORMAT "K)",
          (cur_young_limit / K),
          (new_young_limit / K),
          (cur_old_limit / K),
          (new_old_limit / K));
  if (is_shrink) {
    guarantee(new_old_limit <= cur_old_limit && new_young_limit <= cur_young_limit, "must be");
  } else {
    guarantee(new_old_limit >= cur_old_limit && new_young_limit >= cur_young_limit, "must be");
  }

  // step2
  // Check resize legality
  // 1. expand: always legal
  // 2. shrink:
  //    1 young: must be empty after full gc
  //    2 old: accordign to PSAdaptiveSizePolicy::calculated_old_free_size_in_bytes
  //      (new_old_limit - old_used) >= min_free
  if (is_shrink) {
    // check whether old/young can be resized, trigger full gc as needed
    if (!ps_old_gen_can_shrink(new_old_limit, false) || !ps_young_gen_can_shrink(new_young_limit)) {
      GCCauseSetter gccs(heap, _gc_cause);
      heap->do_full_collection(true);
      DMH_LOG("PS_ElasticMaxHeapOp heap after Full GC");
      if (TraceDynamicMaxHeap) {
        heap->print_on(gclog_or_tty);
      }
      if (young_gen->used_in_bytes() != 0) {
        DMH_LOG("PS_ElasticMaxHeapOp abort: young is not empty after full gc");
        return;
      }
    }

    if (!ps_old_gen_can_shrink(new_old_limit, true)) {
      // still not enough old free after full gc, cannot shrink and print log
      return;
    }

    // step3
    // shrink generation committed size if needed
    // 1. old gen
    //    1 old gen can shrink capacity without full gc
    //    2 old gen have passed shrink valid check since the code is executed here
    //    3 old gen can shrink capacity if needed
    // 2. young gen
    //    1 young gen must shrink capacity after full gc
    //    2 there may be three situations after shrink valid check in step2:
    //        1) both old gen and young gen have passed the check,
    //           indicating new_young_limit is big enough,
    //           there's no need to shrink capacity
    //        2) old gen failed the check and triggered full gc
    //        3) young gen failed the check and triggered full gc
    if (old_gen->capacity_in_bytes() > new_old_limit) {
      size_t desried_free = new_old_limit - old_gen->used_in_bytes();
      char* old_high = old_gen->virtual_space()->committed_high_addr();
      old_gen->resize(desried_free);
      char* new_old_high = old_gen->virtual_space()->committed_high_addr();
      if (old_gen->capacity_in_bytes() > new_old_limit) {
        DMH_LOG("PS_ElasticMaxHeapOp abort: resize old fail " SIZE_FORMAT "K",
                old_gen->capacity_in_bytes() / K);
        return;
      }
      DMH_LOG("PS_ElasticMaxHeapOp continue: shrink old success " SIZE_FORMAT "K",
              old_gen->capacity_in_bytes() / K);
      if (old_high > new_old_high) {
        // shrink is caused by dynamic max heap, free physical memory
        size_t shrink_bytes = old_high - new_old_high;
        guarantee((shrink_bytes > 0) && (shrink_bytes % os::vm_page_size() == 0), "should be");
        bool result = os::free_heap_physical_memory(new_old_high, shrink_bytes);
        guarantee(result, "free heap physical memory should be successful");
      }
    }

    if (young_gen->virtual_space()->committed_size() > new_young_limit) {
      // entering this branch means full gc must have been triggered
      guarantee(young_gen->eden_space()->is_empty() &&
                young_gen->to_space()->is_empty() &&
                young_gen->from_space()->is_empty(),
                "must be empty");

      char* young_high = young_gen->virtual_space()->committed_high_addr();
      if (young_gen->shrink_after_full_gc(new_young_limit) == false) {
        DMH_LOG("PS_ElasticMaxHeapOp abort: shrink young fail");
        return;
      }
      char* new_young_high = young_gen->virtual_space()->committed_high_addr();
      DMH_LOG("PS_ElasticMaxHeapOp continue: shrink young success " SIZE_FORMAT "K",
               young_gen->virtual_space()->committed_size() / K);
      if (young_high > new_young_high) {
        // shrink is caused by dynamic max heap, free physical memory
        size_t shrink_bytes = young_high - new_young_high;
        guarantee((shrink_bytes > 0) && (shrink_bytes % os::vm_page_size() == 0), "should be");
        bool result = os::free_heap_physical_memory(new_young_high, shrink_bytes);
        guarantee(result, "free heap physical memory should be successful");
      }
    }
  }
  // update young/old gen limit, avoid further expand
  old_gen->set_cur_max_gen_size(new_old_limit);
  young_gen->set_cur_max_gen_size(new_young_limit);
  heap->set_current_max_heap_size(_new_max_heap);
  _resize_success = true;
  DMH_LOG("PS_ElasticMaxHeapOp success");
}

bool PS_ChangeMaxHeapOp::ps_old_gen_can_shrink(size_t new_limit, bool print_logs) {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  size_t old_used_bytes = heap->old_gen()->used_in_bytes();
  PSAdaptiveSizePolicy* size_policy = heap->size_policy();
  uintx min_heap_free_ration = MinHeapFreeRatio != 0 ? MinHeapFreeRatio : DynamicMaxHeapShrinkMinFreeRatio;
  size_t min_free = size_policy->calculate_free_based_on_live(old_used_bytes, min_heap_free_ration);
  min_free = align_size_up(min_free, heap->old_gen()->virtual_space()->alignment());
  bool can_shrink = (new_limit >= (old_used_bytes + min_free));
  if (!can_shrink && print_logs) {
    DMH_LOG("PS_ElasticMaxHeapOp abort: not enough old free for shrink");
  }
  return can_shrink;
}

bool PS_ChangeMaxHeapOp::ps_young_gen_can_shrink(size_t new_limit) {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  PSYoungGen* young_gen  = heap->young_gen();
  size_t committed_size = young_gen->virtual_space()->committed_size();
  bool can_shrink = (new_limit >= committed_size);
  return can_shrink;
}

// Resize for DynamicHeapSize, shrink to new_size
bool PSYoungGen::shrink_after_full_gc(size_t new_size) {
  const size_t alignment = virtual_space()->alignment();
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  size_t space_alignment = heap->space_alignment();
  size_t orig_size = virtual_space()->committed_size();
  guarantee(eden_space()->is_empty() && to_space()->is_empty() && from_space()->is_empty(), "must be empty");
  guarantee(new_size % alignment == 0, "must be");
  guarantee(new_size < orig_size, "must be");

  // shrink virtual space
  size_t shrink_bytes = virtual_space()->committed_size() - new_size;
  bool success = virtual_space()->shrink_by(shrink_bytes);
  DMH_LOG("PSYoungGen::shrink_after_full_gc: shrink virtual space %s "
          "orig committed " SIZE_FORMAT "K "
          "current committed " SIZE_FORMAT "K "
          "shrink by " SIZE_FORMAT "K",
          success ? "success" : "fail",
          orig_size / K,
          virtual_space()->committed_size() / K,
          shrink_bytes / K);

  if (!success) {
    return false;
  }

  // caculate new eden/survivor size
  // shrink with same ratio, let size policy adjust later
  size_t current_survivor_ratio = eden_space()->capacity_in_bytes() / from_space()->capacity_in_bytes();
  current_survivor_ratio = MAX2(current_survivor_ratio, (size_t)1);
  size_t new_survivor_size = new_size / (current_survivor_ratio + 2);
  new_survivor_size = align_size_down(new_survivor_size, space_alignment);
  new_survivor_size = MAX2(new_survivor_size, space_alignment);
  size_t new_eden_size = new_size - 2 * new_survivor_size;

  guarantee(new_eden_size % space_alignment == 0, "must be");
  DMH_LOG("PSYoungGen::shrink_after_full_gc: "
          "new eden size " SIZE_FORMAT "K "
          "new survivor size " SIZE_FORMAT "K "
          "new young gen size " SIZE_FORMAT "K",
          new_eden_size / K,
          new_survivor_size / K,
          new_size / K);

  // setup new eden/suvirvor space
  set_space_boundaries(new_eden_size, new_survivor_size);
  post_resize();
  if (TraceDynamicMaxHeap) {
    print_on(gclog_or_tty);
  }
  return true;
}
