/*
 * Copyright (c) 2001, 2016, Oracle and/or its affiliates. All rights reserved.
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
#include "classfile/metadataOnStackMark.hpp"
#include "classfile/symbolTable.hpp"
#include "code/codeCache.hpp"
#include "gc_implementation/g1/concurrentMark.inline.hpp"
#include "gc_implementation/g1/concurrentMarkThread.inline.hpp"
#include "gc_implementation/g1/g1CollectedHeap.inline.hpp"
#include "gc_implementation/g1/g1CollectorPolicy.hpp"
#include "gc_implementation/g1/g1RegionMarkStatsCache.inline.hpp"
#include "gc_implementation/g1/g1ErgoVerbose.hpp"
#include "gc_implementation/g1/g1Log.hpp"
#include "gc_implementation/g1/g1OopClosures.inline.hpp"
#include "gc_implementation/g1/g1RemSet.hpp"
#include "gc_implementation/g1/heapRegion.inline.hpp"
#include "gc_implementation/g1/heapRegionManager.inline.hpp"
#include "gc_implementation/g1/heapRegionRemSet.hpp"
#include "gc_implementation/g1/heapRegionSet.inline.hpp"
#include "gc_implementation/shared/vmGCOperations.hpp"
#include "gc_implementation/shared/gcTimer.hpp"
#include "gc_implementation/shared/gcTrace.hpp"
#include "gc_implementation/shared/gcTraceTime.hpp"
#include "memory/allocation.hpp"
#include "memory/genOopClosures.inline.hpp"
#include "memory/referencePolicy.hpp"
#include "memory/resourceArea.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/prefetch.inline.hpp"
#include "services/memTracker.hpp"

// Concurrent marking bit map wrapper

CMBitMapRO::CMBitMapRO(int shifter) :
  _bm(),
  _shifter(shifter) {
  _bmStartWord = 0;
  _bmWordSize = 0;
}

HeapWord* CMBitMapRO::getNextMarkedWordAddress(const HeapWord* addr,
                                               const HeapWord* limit) const {
  // First we must round addr *up* to a possible object boundary.
  addr = (HeapWord*)align_size_up((intptr_t)addr,
                                  HeapWordSize << _shifter);
  size_t addrOffset = heapWordToOffset(addr);
  if (limit == NULL) {
    limit = _bmStartWord + _bmWordSize;
  }
  size_t limitOffset = heapWordToOffset(limit);
  size_t nextOffset = _bm.get_next_one_offset(addrOffset, limitOffset);
  HeapWord* nextAddr = offsetToHeapWord(nextOffset);
  assert(nextAddr >= addr, "get_next_one postcondition");
  assert(nextAddr == limit || isMarked(nextAddr),
         "get_next_one postcondition");
  return nextAddr;
}

HeapWord* CMBitMapRO::getNextUnmarkedWordAddress(const HeapWord* addr,
                                                 const HeapWord* limit) const {
  size_t addrOffset = heapWordToOffset(addr);
  if (limit == NULL) {
    limit = _bmStartWord + _bmWordSize;
  }
  size_t limitOffset = heapWordToOffset(limit);
  size_t nextOffset = _bm.get_next_zero_offset(addrOffset, limitOffset);
  HeapWord* nextAddr = offsetToHeapWord(nextOffset);
  assert(nextAddr >= addr, "get_next_one postcondition");
  assert(nextAddr == limit || !isMarked(nextAddr),
         "get_next_one postcondition");
  return nextAddr;
}

int CMBitMapRO::heapWordDiffToOffsetDiff(size_t diff) const {
  assert((diff & ((1 << _shifter) - 1)) == 0, "argument check");
  return (int) (diff >> _shifter);
}

#ifndef PRODUCT
bool CMBitMapRO::covers(MemRegion heap_rs) const {
  // assert(_bm.map() == _virtual_space.low(), "map inconsistency");
  assert(((size_t)_bm.size() * ((size_t)1 << _shifter)) == _bmWordSize,
         "size inconsistency");
  return _bmStartWord == (HeapWord*)(heap_rs.start()) &&
         _bmWordSize  == heap_rs.word_size();
}
#endif

void CMBitMapRO::print_on_error(outputStream* st, const char* prefix) const {
  _bm.print_on_error(st, prefix);
}

size_t CMBitMap::compute_size(size_t heap_size) {
  return ReservedSpace::allocation_align_size_up(heap_size / mark_distance());
}

size_t CMBitMap::mark_distance() {
  return MinObjAlignmentInBytes * BitsPerByte;
}

void CMBitMap::initialize(MemRegion heap, G1RegionToSpaceMapper* storage) {
  _bmStartWord = heap.start();
  _bmWordSize = heap.word_size();

  _bm.set_map((BitMap::bm_word_t*) storage->reserved().start());
  _bm.set_size(_bmWordSize >> _shifter);

  storage->set_mapping_changed_listener(&_listener);
}

void CMBitMapMappingChangedListener::on_commit(uint start_region, size_t num_regions, bool zero_filled) {
  if (zero_filled) {
    return;
  }
  // We need to clear the bitmap on commit, removing any existing information.
  MemRegion mr(G1CollectedHeap::heap()->bottom_addr_for_region(start_region), num_regions * HeapRegion::GrainWords);
  _bm->clearRange(mr);
}

// Closure used for clearing the given mark bitmap.
class ClearBitmapHRClosure : public HeapRegionClosure {
 private:
  ConcurrentMark* _cm;
  CMBitMap* _bitmap;
  bool _may_yield;      // The closure may yield during iteration. If yielded, abort the iteration.
 public:
  ClearBitmapHRClosure(ConcurrentMark* cm, CMBitMap* bitmap, bool may_yield) : HeapRegionClosure(), _cm(cm), _bitmap(bitmap), _may_yield(may_yield) {
    assert(!may_yield || cm != NULL, "CM must be non-NULL if this closure is expected to yield.");
  }

  virtual bool doHeapRegion(HeapRegion* r) {
    size_t const chunk_size_in_words = M / HeapWordSize;

    HeapWord* cur = r->bottom();
    HeapWord* const end = r->end();

    while (cur < end) {
      MemRegion mr(cur, MIN2(cur + chunk_size_in_words, end));
      _bitmap->clearRange(mr);

      cur += chunk_size_in_words;

      // Abort iteration if after yielding the marking has been aborted.
      if (_may_yield && _cm->do_yield_check() && _cm->has_aborted()) {
        return true;
      }
      // Repeat the asserts from before the start of the closure. We will do them
      // as asserts here to minimize their overhead on the product. However, we
      // will have them as guarantees at the beginning / end of the bitmap
      // clearing to get some checking in the product.
      assert(!_may_yield || _cm->cmThread()->during_cycle(), "invariant");
      assert(!_may_yield || !G1CollectedHeap::heap()->mark_in_progress(), "invariant");
    }

    return false;
  }
};

void CMBitMap::clearAll() {
  ClearBitmapHRClosure cl(NULL, this, false /* may_yield */);
  G1CollectedHeap::heap()->heap_region_iterate(&cl);
  guarantee(cl.complete(), "Must have completed iteration.");
  return;
}

void CMBitMap::markRange(MemRegion mr) {
  mr.intersection(MemRegion(_bmStartWord, _bmWordSize));
  assert(!mr.is_empty(), "unexpected empty region");
  assert((offsetToHeapWord(heapWordToOffset(mr.end())) ==
          ((HeapWord *) mr.end())),
         "markRange memory region end is not card aligned");
  // convert address range into offset range
  _bm.at_put_range(heapWordToOffset(mr.start()),
                   heapWordToOffset(mr.end()), true);
}

void CMBitMap::clearRange(MemRegion mr) {
  mr.intersection(MemRegion(_bmStartWord, _bmWordSize));
  assert(!mr.is_empty(), "unexpected empty region");
  // convert address range into offset range
  _bm.at_put_range(heapWordToOffset(mr.start()),
                   heapWordToOffset(mr.end()), false);
}

MemRegion CMBitMap::getAndClearMarkedRegion(HeapWord* addr,
                                            HeapWord* end_addr) {
  HeapWord* start = getNextMarkedWordAddress(addr);
  start = MIN2(start, end_addr);
  HeapWord* end   = getNextUnmarkedWordAddress(start);
  end = MIN2(end, end_addr);
  assert(start <= end, "Consistency check");
  MemRegion mr(start, end);
  if (!mr.is_empty()) {
    clearRange(mr);
  }
  return mr;
}

CMMarkStack::CMMarkStack(ConcurrentMark* cm) :
  _base(NULL), _cm(cm)
#ifdef ASSERT
  , _drain_in_progress(false)
  , _drain_in_progress_yields(false)
#endif
{}

bool CMMarkStack::allocate(size_t capacity) {
  // allocate a stack of the requisite depth
  ReservedSpace rs(ReservedSpace::allocation_align_size_up(capacity * sizeof(oop)));
  if (!rs.is_reserved()) {
    warning("ConcurrentMark MarkStack allocation failure");
    return false;
  }
  MemTracker::record_virtual_memory_type((address)rs.base(), mtGC);
  if (!_virtual_space.initialize(rs, rs.size())) {
    warning("ConcurrentMark MarkStack backing store failure");
    // Release the virtual memory reserved for the marking stack
    rs.release();
    return false;
  }
  assert(_virtual_space.committed_size() == rs.size(),
         "Didn't reserve backing store for all of ConcurrentMark stack?");
  _base = (oop*) _virtual_space.low();
  setEmpty();
  _capacity = (jint) capacity;
  _saved_index = -1;
  NOT_PRODUCT(_max_depth = 0);
  return true;
}

void CMMarkStack::expand() {
  // Called, during remark, if we've overflown the marking stack during marking.
  assert(isEmpty(), "stack should been emptied while handling overflow");
  assert(_capacity <= (jint) MarkStackSizeMax, "stack bigger than permitted");
  if (_capacity == (jint) MarkStackSizeMax) {
    if (PrintGCDetails && Verbose) {
      gclog_or_tty->print_cr(" (benign) Can't expand marking stack capacity, at max size limit");
    }
    return;
  }
  // Double capacity if possible
  jint new_capacity = MIN2(_capacity*2, (jint) MarkStackSizeMax);
  // Do not give up existing stack until we have managed to
  // get the double capacity that we desired.
  ReservedSpace rs(ReservedSpace::allocation_align_size_up(new_capacity *
                                                           sizeof(oop)));
  if (rs.is_reserved()) {
    // Release the backing store associated with old stack
    _virtual_space.release();
    // Reinitialize virtual space for new stack
    if (!_virtual_space.initialize(rs, rs.size())) {
      fatal("Not enough swap for expanded marking stack capacity");
    }
    _base = (oop*)(_virtual_space.low());
    _index = 0;
    _capacity = new_capacity;
  } else {
    if (PrintGCDetails && Verbose) {
      // Failed to double capacity, continue;
      gclog_or_tty->print(" (benign) Failed to expand marking stack capacity from "
                          SIZE_FORMAT "K to " SIZE_FORMAT "K",
                          _capacity / K, new_capacity / K);
    }
  }
}

CMMarkStack::~CMMarkStack() {
  if (_base != NULL) {
    _base = NULL;
    _virtual_space.release();
  }
}

void CMMarkStack::par_push(oop ptr) {
  while (true) {
    if (isFull()) {
      _overflow = true;
      return;
    }
    // Otherwise...
    jint index = _index;
    jint next_index = index+1;
    jint res = Atomic::cmpxchg(next_index, &_index, index);
    if (res == index) {
      _base[index] = ptr;
      // Note that we don't maintain this atomically.  We could, but it
      // doesn't seem necessary.
      NOT_PRODUCT(_max_depth = MAX2(_max_depth, next_index));
      return;
    }
    // Otherwise, we need to try again.
  }
}

void CMMarkStack::par_adjoin_arr(oop* ptr_arr, int n) {
  while (true) {
    if (isFull()) {
      _overflow = true;
      return;
    }
    // Otherwise...
    jint index = _index;
    jint next_index = index + n;
    if (next_index > _capacity) {
      _overflow = true;
      return;
    }
    jint res = Atomic::cmpxchg(next_index, &_index, index);
    if (res == index) {
      for (int i = 0; i < n; i++) {
        int  ind = index + i;
        assert(ind < _capacity, "By overflow test above.");
        _base[ind] = ptr_arr[i];
      }
      NOT_PRODUCT(_max_depth = MAX2(_max_depth, next_index));
      return;
    }
    // Otherwise, we need to try again.
  }
}

void CMMarkStack::par_push_arr(oop* ptr_arr, int n) {
  MutexLockerEx x(ParGCRareEvent_lock, Mutex::_no_safepoint_check_flag);
  jint start = _index;
  jint next_index = start + n;
  if (next_index > _capacity) {
    _overflow = true;
    return;
  }
  // Otherwise.
  _index = next_index;
  for (int i = 0; i < n; i++) {
    int ind = start + i;
    assert(ind < _capacity, "By overflow test above.");
    _base[ind] = ptr_arr[i];
  }
  NOT_PRODUCT(_max_depth = MAX2(_max_depth, next_index));
}

bool CMMarkStack::par_pop_arr(oop* ptr_arr, int max, int* n) {
  MutexLockerEx x(ParGCRareEvent_lock, Mutex::_no_safepoint_check_flag);
  jint index = _index;
  if (index == 0) {
    *n = 0;
    return false;
  } else {
    int k = MIN2(max, index);
    jint  new_ind = index - k;
    for (int j = 0; j < k; j++) {
      ptr_arr[j] = _base[new_ind + j];
    }
    _index = new_ind;
    *n = k;
    return true;
  }
}

template<class OopClosureClass>
bool CMMarkStack::drain(OopClosureClass* cl, CMBitMap* bm, bool yield_after) {
  assert(!_drain_in_progress || !_drain_in_progress_yields || yield_after
         || SafepointSynchronize::is_at_safepoint(),
         "Drain recursion must be yield-safe.");
  bool res = true;
  debug_only(_drain_in_progress = true);
  debug_only(_drain_in_progress_yields = yield_after);
  while (!isEmpty()) {
    oop newOop = pop();
    assert(G1CollectedHeap::heap()->is_in_reserved(newOop), "Bad pop");
    assert(newOop->is_oop(), "Expected an oop");
    assert(bm == NULL || bm->isMarked((HeapWord*)newOop),
           "only grey objects on this stack");
    newOop->oop_iterate(cl);
    if (yield_after && _cm->do_yield_check()) {
      res = false;
      break;
    }
  }
  debug_only(_drain_in_progress = false);
  return res;
}

void CMMarkStack::note_start_of_gc() {
  assert(_saved_index == -1,
         "note_start_of_gc()/end_of_gc() bracketed incorrectly");
  _saved_index = _index;
}

void CMMarkStack::note_end_of_gc() {
  // This is intentionally a guarantee, instead of an assert. If we
  // accidentally add something to the mark stack during GC, it
  // will be a correctness issue so it's better if we crash. we'll
  // only check this once per GC anyway, so it won't be a performance
  // issue in any way.
  guarantee(_saved_index == _index,
            err_msg("saved index: %d index: %d", _saved_index, _index));
  _saved_index = -1;
}

void CMMarkStack::oops_do(OopClosure* f) {
  assert(_saved_index == _index,
         err_msg("saved index: %d index: %d", _saved_index, _index));
  for (int i = 0; i < _index; i += 1) {
    f->do_oop(&_base[i]);
  }
}

CMRootRegions::CMRootRegions() :
  _young_list(NULL), _cm(NULL), _scan_in_progress(false),
  _should_abort(false),  _next_survivor(NULL) { }

void CMRootRegions::init(G1CollectedHeap* g1h, ConcurrentMark* cm) {
  _young_list = g1h->young_list();
  _cm = cm;
}

void CMRootRegions::prepare_for_scan() {
  assert(!scan_in_progress(), "pre-condition");

  // Currently, only survivors can be root regions.
  assert(_next_survivor == NULL, "pre-condition");
  _next_survivor = _young_list->first_survivor_region();
  _scan_in_progress = (_next_survivor != NULL);
  _should_abort = false;
}

HeapRegion* CMRootRegions::claim_next() {
  if (_should_abort) {
    // If someone has set the should_abort flag, we return NULL to
    // force the caller to bail out of their loop.
    return NULL;
  }

  // Currently, only survivors can be root regions.
  HeapRegion* res = _next_survivor;
  if (res != NULL) {
    MutexLockerEx x(RootRegionScan_lock, Mutex::_no_safepoint_check_flag);
    // Read it again in case it changed while we were waiting for the lock.
    res = _next_survivor;
    if (res != NULL) {
      if (res == _young_list->last_survivor_region()) {
        // We just claimed the last survivor so store NULL to indicate
        // that we're done.
        _next_survivor = NULL;
      } else {
        _next_survivor = res->get_next_young_region();
      }
    } else {
      // Someone else claimed the last survivor while we were trying
      // to take the lock so nothing else to do.
    }
  }
  assert(res == NULL || res->is_survivor(), "post-condition");

  return res;
}

void CMRootRegions::notify_scan_done() {
  MutexLockerEx x(RootRegionScan_lock, Mutex::_no_safepoint_check_flag);
  _scan_in_progress = false;
  RootRegionScan_lock->notify_all();
}

void CMRootRegions::cancel_scan() {
  notify_scan_done();
}

void CMRootRegions::scan_finished() {
  assert(scan_in_progress(), "pre-condition");

  // Currently, only survivors can be root regions.
  if (!_should_abort) {
    assert(_next_survivor == NULL, "we should have claimed all survivors");
  }
  _next_survivor = NULL;

  notify_scan_done();
}

bool CMRootRegions::wait_until_scan_finished() {
  if (!scan_in_progress()) return false;

  {
    MutexLockerEx x(RootRegionScan_lock, Mutex::_no_safepoint_check_flag);
    while (scan_in_progress()) {
      RootRegionScan_lock->wait(Mutex::_no_safepoint_check_flag);
    }
  }
  return true;
}

#ifdef _MSC_VER // the use of 'this' below gets a warning, make it go away
#pragma warning( disable:4355 ) // 'this' : used in base member initializer list
#endif // _MSC_VER

uint ConcurrentMark::scale_parallel_threads(uint n_par_threads) {
  return MAX2((n_par_threads + 2) / 4, 1U);
}

ConcurrentMark::ConcurrentMark(G1CollectedHeap* g1h, G1RegionToSpaceMapper* prev_bitmap_storage, G1RegionToSpaceMapper* next_bitmap_storage) :
  _g1h(g1h),
  _markBitMap1(),
  _markBitMap2(),
  _parallel_marking_threads(0),
  _max_parallel_marking_threads(0),
  _sleep_factor(0.0),
  _marking_task_overhead(1.0),
  _cleanup_sleep_factor(0.0),
  _cleanup_task_overhead(1.0),
  _cleanup_list("Cleanup List"),

  _prevMarkBitMap(&_markBitMap1),
  _nextMarkBitMap(&_markBitMap2),

  _markStack(this),
  // _finger set in set_non_marking_state
  _worker_id_offset(DirtyCardQueueSet::num_par_ids() + G1ConcRefinementThreads),
  _max_worker_id(MAX2((uint)ParallelGCThreads, 1U)),
  // _active_tasks set in set_non_marking_state
  // _tasks set inside the constructor
  _task_queues(new CMTaskQueueSet((int) _max_worker_id)),
  _terminator((int) _max_worker_id, _task_queues),

  _has_overflown(false),
  _concurrent(false),
  _has_aborted(false),
  _aborted_gc_id(GCId::undefined()),
  _restart_for_overflow(false),
  _concurrent_marking_in_progress(false),

  // _verbose_level set below

  _init_times(),
  _remark_times(), _remark_mark_times(), _remark_weak_ref_times(),
  _cleanup_times(),
  _total_counting_time(0.0),

  _parallel_workers(NULL),
  _region_mark_stats(NEW_C_HEAP_ARRAY(G1RegionMarkStats, _g1h->max_regions(), mtGC)),
  _top_at_rebuild_starts(NEW_C_HEAP_ARRAY(HeapWord*, _g1h->max_regions(), mtGC)),
  _completed_initialization(false) {
  CMVerboseLevel verbose_level = (CMVerboseLevel) G1MarkingVerboseLevel;
  if (verbose_level < no_verbose) {
    verbose_level = no_verbose;
  }
  if (verbose_level > high_verbose) {
    verbose_level = high_verbose;
  }
  _verbose_level = verbose_level;

  if (verbose_low()) {
    gclog_or_tty->print_cr("[global] init, heap start = " PTR_FORMAT", "
                           "heap end = " INTPTR_FORMAT, p2i(_heap_start), p2i(_heap_end));
  }

  _markBitMap1.initialize(g1h->reserved_region(), prev_bitmap_storage);
  _markBitMap2.initialize(g1h->reserved_region(), next_bitmap_storage);

  // Create & start a ConcurrentMark thread.
  _cmThread = new ConcurrentMarkThread(this);
  assert(cmThread() != NULL, "CM Thread should have been created");
  assert(cmThread()->cm() != NULL, "CM Thread should refer to this cm");
  if (_cmThread->osthread() == NULL) {
      vm_shutdown_during_initialization("Could not create ConcurrentMarkThread");
  }

  assert(CGC_lock != NULL, "Where's the CGC_lock?");
  assert(_markBitMap1.covers(g1h->reserved_region()), "_markBitMap1 inconsistency");
  assert(_markBitMap2.covers(g1h->reserved_region()), "_markBitMap2 inconsistency");

  SATBMarkQueueSet& satb_qs = JavaThread::satb_mark_queue_set();
  satb_qs.set_buffer_size(G1SATBBufferSize);

  _root_regions.init(_g1h, this);

  if (ConcGCThreads > ParallelGCThreads) {
    warning("Can't have more ConcGCThreads (" UINTX_FORMAT ") "
            "than ParallelGCThreads (" UINTX_FORMAT ").",
            ConcGCThreads, ParallelGCThreads);
    return;
  }
  if (ParallelGCThreads == 0) {
    // if we are not running with any parallel GC threads we will not
    // spawn any marking threads either
    _parallel_marking_threads =       0;
    _max_parallel_marking_threads =   0;
    _sleep_factor             =     0.0;
    _marking_task_overhead    =     1.0;
  } else {
    if (!FLAG_IS_DEFAULT(ConcGCThreads) && ConcGCThreads > 0) {
      // Note: ConcGCThreads has precedence over G1MarkingOverheadPercent
      // if both are set
      _sleep_factor             = 0.0;
      _marking_task_overhead    = 1.0;
    } else if (G1MarkingOverheadPercent > 0) {
      // We will calculate the number of parallel marking threads based
      // on a target overhead with respect to the soft real-time goal
      double marking_overhead = (double) G1MarkingOverheadPercent / 100.0;
      double overall_cm_overhead =
        (double) MaxGCPauseMillis * marking_overhead /
        (double) GCPauseIntervalMillis;
      double cpu_ratio = 1.0 / os::initial_active_processor_count();
      double marking_thread_num = ceil(overall_cm_overhead / cpu_ratio);
      double marking_task_overhead =
        overall_cm_overhead / marking_thread_num * os::initial_active_processor_count();
      double sleep_factor =
                         (1.0 - marking_task_overhead) / marking_task_overhead;

      FLAG_SET_ERGO(uintx, ConcGCThreads, (uint) marking_thread_num);
      _sleep_factor             = sleep_factor;
      _marking_task_overhead    = marking_task_overhead;
    } else {
      // Calculate the number of parallel marking threads by scaling
      // the number of parallel GC threads.
      uint marking_thread_num = scale_parallel_threads((uint) ParallelGCThreads);
      FLAG_SET_ERGO(uintx, ConcGCThreads, marking_thread_num);
      _sleep_factor             = 0.0;
      _marking_task_overhead    = 1.0;
    }

    assert(ConcGCThreads > 0, "Should have been set");
    _parallel_marking_threads = (uint) ConcGCThreads;
    _max_parallel_marking_threads = _parallel_marking_threads;

    if (parallel_marking_threads() > 1) {
      _cleanup_task_overhead = 1.0;
    } else {
      _cleanup_task_overhead = marking_task_overhead();
    }
    _cleanup_sleep_factor =
                     (1.0 - cleanup_task_overhead()) / cleanup_task_overhead();

#if 0
    gclog_or_tty->print_cr("Marking Threads          %d", parallel_marking_threads());
    gclog_or_tty->print_cr("CM Marking Task Overhead %1.4lf", marking_task_overhead());
    gclog_or_tty->print_cr("CM Sleep Factor          %1.4lf", sleep_factor());
    gclog_or_tty->print_cr("CL Marking Task Overhead %1.4lf", cleanup_task_overhead());
    gclog_or_tty->print_cr("CL Sleep Factor          %1.4lf", cleanup_sleep_factor());
#endif

    guarantee(parallel_marking_threads() > 0, "peace of mind");
    _parallel_workers = new FlexibleWorkGang("G1 Parallel Marking Threads",
         _max_parallel_marking_threads, false, true);
    if (_parallel_workers == NULL) {
      vm_exit_during_initialization("Failed necessary allocation.");
    } else {
      _parallel_workers->initialize_workers();
    }
  }

  if (FLAG_IS_DEFAULT(MarkStackSize)) {
    uintx mark_stack_size =
      MIN2(MarkStackSizeMax,
          MAX2(MarkStackSize, (uintx) (parallel_marking_threads() * TASKQUEUE_SIZE)));
    // Verify that the calculated value for MarkStackSize is in range.
    // It would be nice to use the private utility routine from Arguments.
    if (!(mark_stack_size >= 1 && mark_stack_size <= MarkStackSizeMax)) {
      warning("Invalid value calculated for MarkStackSize (" UINTX_FORMAT "): "
              "must be between " UINTX_FORMAT " and " UINTX_FORMAT,
              mark_stack_size, (uintx) 1, MarkStackSizeMax);
      return;
    }
    FLAG_SET_ERGO(uintx, MarkStackSize, mark_stack_size);
  } else {
    // Verify MarkStackSize is in range.
    if (FLAG_IS_CMDLINE(MarkStackSize)) {
      if (FLAG_IS_DEFAULT(MarkStackSizeMax)) {
        if (!(MarkStackSize >= 1 && MarkStackSize <= MarkStackSizeMax)) {
          warning("Invalid value specified for MarkStackSize (" UINTX_FORMAT "): "
                  "must be between " UINTX_FORMAT " and " UINTX_FORMAT,
                  MarkStackSize, (uintx) 1, MarkStackSizeMax);
          return;
        }
      } else if (FLAG_IS_CMDLINE(MarkStackSizeMax)) {
        if (!(MarkStackSize >= 1 && MarkStackSize <= MarkStackSizeMax)) {
          warning("Invalid value specified for MarkStackSize (" UINTX_FORMAT ")"
                  " or for MarkStackSizeMax (" UINTX_FORMAT ")",
                  MarkStackSize, MarkStackSizeMax);
          return;
        }
      }
    }
  }

  if (!_markStack.allocate(MarkStackSize)) {
    warning("Failed to allocate CM marking stack");
    return;
  }

  _tasks = NEW_C_HEAP_ARRAY(CMTask*, _max_worker_id, mtGC);
  _accum_task_vtime = NEW_C_HEAP_ARRAY(double, _max_worker_id, mtGC);

  // so that the assertion in MarkingTaskQueue::task_queue doesn't fail
  _active_tasks = _max_worker_id;

  for (uint i = 0; i < _max_worker_id; ++i) {
    CMTaskQueue* task_queue = new CMTaskQueue();
    task_queue->initialize();
    _task_queues->register_queue(i, task_queue);

    _tasks[i] = new CMTask(i, this, task_queue, _task_queues,  _region_mark_stats, _g1h->max_regions());

    _accum_task_vtime[i] = 0.0;
  }

  // so that the call below can read a sensible value
  _heap_start = g1h->reserved_region().start();
  set_non_marking_state();
  _completed_initialization = true;
}

void ConcurrentMark::reset() {
  // Starting values for these two. This should be called in a STW
  // phase.
  MemRegion reserved = _g1h->g1_reserved();
  _heap_start = reserved.start();
  _heap_end   = reserved.end();

  // Separated the asserts so that we know which one fires.
  assert(_heap_start != NULL, "heap bounds should look ok");
  assert(_heap_end != NULL, "heap bounds should look ok");
  assert(_heap_start < _heap_end, "heap bounds should look ok");

  // Reset all the marking data structures and any necessary flags
  reset_marking_state();

  if (verbose_low()) {
    gclog_or_tty->print_cr("[global] resetting");
  }

  // Reset all tasks, since different phases will use different number of active
  // threads. So, it's easiest to have all of them ready.
  for (uint i = 0; i < _max_worker_id; ++i) {
    _tasks[i]->reset(_nextMarkBitMap);
  }

  uint max_regions = _g1h->max_regions();
  for (uint i = 0; i < max_regions; i++) {
    _top_at_rebuild_starts[i] = NULL;
    _region_mark_stats[i].clear();
  }

  // we need this to make sure that the flag is on during the evac
  // pause with initial mark piggy-backed
  set_concurrent_marking_in_progress();
}

void ConcurrentMark::clear_statistics_in_region(uint region_idx) {
  for (uint j = 0; j < _max_worker_id; ++j) {
    _tasks[j]->clear_mark_stats_cache(region_idx);
  }
  _top_at_rebuild_starts[region_idx] = NULL;
  _region_mark_stats[region_idx].clear();
}

void ConcurrentMark::humongous_object_eagerly_reclaimed(HeapRegion* r) {
  assert(SafepointSynchronize::is_at_safepoint(), "May only be called at a safepoint.");
  // Need to clear mark bit of the humongous object if already set and during a marking cycle.
  if (_nextMarkBitMap->isMarked(r->bottom())) {
    _nextMarkBitMap->clear(r->bottom());
  }

  // Clear any statistics about the region gathered so far.
  uint const region_idx = r->hrm_index();
  if (r->isHumongous()) {
    assert(r->startsHumongous(), "Got humongous continues region here");
    uint const size_in_regions = (uint)_g1h->humongous_obj_size_in_regions(oop(r->humongous_start_region()->bottom())->size());
    for (uint j = region_idx; j < (region_idx + size_in_regions); j++) {
      clear_statistics_in_region(j);
    }
  } else {
    clear_statistics_in_region(region_idx);
  }
}


void ConcurrentMark::reset_marking_state(bool clear_overflow) {
  _markStack.setEmpty();        // Also clears the _markStack overflow flag

  // Expand the marking stack, if we have to and if we can.
  if (has_overflown()) {
    _markStack.expand();

	  uint max_regions = _g1h->max_regions();
    for (uint i = 0; i < max_regions; i++) {
      _region_mark_stats[i].clear_during_overflow();
    }
  }

  if (clear_overflow) {
    clear_has_overflown();
  } else {
    assert(has_overflown(), "pre-condition");
  }
  _finger = _heap_start;

  for (uint i = 0; i < _max_worker_id; ++i) {
    CMTaskQueue* queue = _task_queues->queue(i);
    queue->set_empty();
  }
}

void ConcurrentMark::set_concurrency(uint active_tasks) {
  assert(active_tasks <= _max_worker_id, "we should not have more");

  _active_tasks = active_tasks;
  // Need to update the three data structures below according to the
  // number of active threads for this phase.
  _terminator = TaskTerminator((int) active_tasks, _task_queues);
  _first_overflow_barrier_sync.set_n_workers((int) active_tasks);
  _second_overflow_barrier_sync.set_n_workers((int) active_tasks);
}

void ConcurrentMark::set_concurrency_and_phase(uint active_tasks, bool concurrent) {
  set_concurrency(active_tasks);

  _concurrent = concurrent;
  // We propagate this to all tasks, not just the active ones.
  for (uint i = 0; i < _max_worker_id; ++i)
    _tasks[i]->set_concurrent(concurrent);

  if (concurrent) {
    set_concurrent_marking_in_progress();
  } else {
    // We currently assume that the concurrent flag has been set to
    // false before we start remark. At this point we should also be
    // in a STW phase.
    assert(!concurrent_marking_in_progress(), "invariant");
    assert(out_of_regions(),
           err_msg("only way to get here: _finger: " PTR_FORMAT ", _heap_end: " PTR_FORMAT,
                   p2i(_finger), p2i(_heap_end)));
  }
}

void ConcurrentMark::set_non_marking_state() {
  // We set the global marking state to some default values when we're
  // not doing marking.
  reset_marking_state();
  _active_tasks = 0;
  clear_concurrent_marking_in_progress();
}

ConcurrentMark::~ConcurrentMark() {
  FREE_C_HEAP_ARRAY(HeapWord*, _top_at_rebuild_starts, mtGC);
  FREE_C_HEAP_ARRAY(G1RegionMarkStats, _region_mark_stats, mtGC);
  // The ConcurrentMark instance is never freed.
  ShouldNotReachHere();
}

void ConcurrentMark::clearNextBitmap() {
  G1CollectedHeap* g1h = G1CollectedHeap::heap();

  // Make sure that the concurrent mark thread looks to still be in
  // the current cycle.
  guarantee(cmThread()->during_cycle(), "invariant");

  // We are finishing up the current cycle by clearing the next
  // marking bitmap and getting it ready for the next cycle. During
  // this time no other cycle can start. So, let's make sure that this
  // is the case.
  guarantee(!g1h->mark_in_progress(), "invariant");

  ClearBitmapHRClosure cl(this, _nextMarkBitMap, true /* may_yield */);
  g1h->heap_region_iterate(&cl);

  // Repeat the asserts from above.
  guarantee(cmThread()->during_cycle(), "invariant");
  guarantee(!g1h->mark_in_progress(), "invariant");
}

class CheckBitmapClearHRClosure : public HeapRegionClosure {
  CMBitMap* _bitmap;
  bool _error;
 public:
  CheckBitmapClearHRClosure(CMBitMap* bitmap) : _bitmap(bitmap) {
  }

  virtual bool doHeapRegion(HeapRegion* r) {
    // This closure can be called concurrently to the mutator, so we must make sure
    // that the result of the getNextMarkedWordAddress() call is compared to the
    // value passed to it as limit to detect any found bits.
    // end never changes in G1.
    HeapWord* end = r->end();
    return _bitmap->getNextMarkedWordAddress(r->bottom(), end) != end;
  }
};

bool ConcurrentMark::nextMarkBitmapIsClear() {
  CheckBitmapClearHRClosure cl(_nextMarkBitMap);
  _g1h->heap_region_iterate(&cl);
  return cl.complete();
}

class NoteStartOfMarkHRClosure: public HeapRegionClosure {
public:
  bool doHeapRegion(HeapRegion* r) {
    r->note_start_of_marking();
    return false;
  }
};

void ConcurrentMark::checkpointRootsInitialPre() {
  G1CollectedHeap*   g1h = G1CollectedHeap::heap();
  G1CollectorPolicy* g1p = g1h->g1_policy();

  _has_aborted = false;

#ifndef PRODUCT
  if (G1PrintReachableAtInitialMark) {
    print_reachable("at-cycle-start",
                    VerifyOption_G1UsePrevMarking, true /* all */);
  }
#endif

  // Initialise marking structures. This has to be done in a STW phase.
  reset();

  // For each region note start of marking.
  NoteStartOfMarkHRClosure startcl;
  g1h->heap_region_iterate(&startcl);
}


void ConcurrentMark::checkpointRootsInitialPost() {
  G1CollectedHeap*   g1h = G1CollectedHeap::heap();

  // If we force an overflow during remark, the remark operation will
  // actually abort and we'll restart concurrent marking. If we always
  // force an oveflow during remark we'll never actually complete the
  // marking phase. So, we initilize this here, at the start of the
  // cycle, so that at the remaining overflow number will decrease at
  // every remark and we'll eventually not need to cause one.
  force_overflow_stw()->init();

  // Start Concurrent Marking weak-reference discovery.
  ReferenceProcessor* rp = g1h->ref_processor_cm();
  // enable ("weak") refs discovery
  rp->enable_discovery(true /*verify_disabled*/, true /*verify_no_refs*/);
  rp->setup_policy(false); // snapshot the soft ref policy to be used in this cycle

  SATBMarkQueueSet& satb_mq_set = JavaThread::satb_mark_queue_set();
  // This is the start of  the marking cycle, we're expected all
  // threads to have SATB queues with active set to false.
  satb_mq_set.set_active_all_threads(true, /* new active value */
                                     false /* expected_active */);

  _root_regions.prepare_for_scan();

  // update_g1_committed() will be called at the end of an evac pause
  // when marking is on. So, it's also called at the end of the
  // initial-mark pause to update the heap end, if the heap expands
  // during it. No need to call it here.
}

/*
 * Notice that in the next two methods, we actually leave the STS
 * during the barrier sync and join it immediately afterwards. If we
 * do not do this, the following deadlock can occur: one thread could
 * be in the barrier sync code, waiting for the other thread to also
 * sync up, whereas another one could be trying to yield, while also
 * waiting for the other threads to sync up too.
 *
 * Note, however, that this code is also used during remark and in
 * this case we should not attempt to leave / enter the STS, otherwise
 * we'll either hit an asseert (debug / fastdebug) or deadlock
 * (product). So we should only leave / enter the STS if we are
 * operating concurrently.
 *
 * Because the thread that does the sync barrier has left the STS, it
 * is possible to be suspended for a Full GC or an evacuation pause
 * could occur. This is actually safe, since the entering the sync
 * barrier is one of the last things do_marking_step() does, and it
 * doesn't manipulate any data structures afterwards.
 */

void ConcurrentMark::enter_first_sync_barrier(uint worker_id) {
  if (verbose_low()) {
    gclog_or_tty->print_cr("[%u] entering first barrier", worker_id);
  }

  if (concurrent()) {
    SuspendibleThreadSet::leave();
  }

  bool barrier_aborted = !_first_overflow_barrier_sync.enter();

  if (concurrent()) {
    SuspendibleThreadSet::join();
  }
  // at this point everyone should have synced up and not be doing any
  // more work

  if (verbose_low()) {
    if (barrier_aborted) {
      gclog_or_tty->print_cr("[%u] aborted first barrier", worker_id);
    } else {
      gclog_or_tty->print_cr("[%u] leaving first barrier", worker_id);
    }
  }

  if (barrier_aborted) {
    // If the barrier aborted we ignore the overflow condition and
    // just abort the whole marking phase as quickly as possible.
    return;
  }


}

void ConcurrentMark::enter_second_sync_barrier(uint worker_id) {
  if (verbose_low()) {
    gclog_or_tty->print_cr("[%u] entering second barrier", worker_id);
  }

  if (concurrent()) {
    SuspendibleThreadSet::leave();
  }

  bool barrier_aborted = !_second_overflow_barrier_sync.enter();

  if (concurrent()) {
    SuspendibleThreadSet::join();
  }
  // at this point everything should be re-initialized and ready to go

  if (verbose_low()) {
    if (barrier_aborted) {
      gclog_or_tty->print_cr("[%u] aborted second barrier", worker_id);
    } else {
      gclog_or_tty->print_cr("[%u] leaving second barrier", worker_id);
    }
  }
}

#ifndef PRODUCT
void ForceOverflowSettings::init() {
  _num_remaining = G1ConcMarkForceOverflow;
  _force = false;
  update();
}

void ForceOverflowSettings::update() {
  if (_num_remaining > 0) {
    _num_remaining -= 1;
    _force = true;
  } else {
    _force = false;
  }
}

bool ForceOverflowSettings::should_force() {
  if (_force) {
    _force = false;
    return true;
  } else {
    return false;
  }
}
#endif // !PRODUCT

class CMConcurrentMarkingTask: public AbstractGangTask {
private:
  ConcurrentMark*       _cm;
  ConcurrentMarkThread* _cmt;

public:
  void work(uint worker_id) {
    assert(Thread::current()->is_ConcurrentGC_thread(),
           "this should only be done by a conc GC thread");
    ResourceMark rm;

    double start_vtime = os::elapsedVTime();

    SuspendibleThreadSet::join();

    assert(worker_id < _cm->active_tasks(), "invariant");
    CMTask* the_task = _cm->task(worker_id);
    the_task->record_start_time();
    if (!_cm->has_aborted()) {
      do {
        double start_vtime_sec = os::elapsedVTime();
        double mark_step_duration_ms = G1ConcMarkStepDurationMillis;

        the_task->do_marking_step(mark_step_duration_ms,
                                  true  /* do_termination */,
                                  false /* is_serial*/);

        double end_vtime_sec = os::elapsedVTime();
        double elapsed_vtime_sec = end_vtime_sec - start_vtime_sec;
        _cm->clear_has_overflown();

        _cm->do_yield_check();

        jlong sleep_time_ms;
        if (!_cm->has_aborted() && the_task->has_aborted()) {
          sleep_time_ms =
            (jlong) (elapsed_vtime_sec * _cm->sleep_factor() * 1000.0);
          SuspendibleThreadSet::leave();
          os::sleep(Thread::current(), sleep_time_ms, false);
          SuspendibleThreadSet::join();
        }
      } while (!_cm->has_aborted() && the_task->has_aborted());
    }
    the_task->record_end_time();
    guarantee(!the_task->has_aborted() || _cm->has_aborted(), "invariant");

    SuspendibleThreadSet::leave();

    double end_vtime = os::elapsedVTime();
    _cm->update_accum_task_vtime(worker_id, end_vtime - start_vtime);
  }

  CMConcurrentMarkingTask(ConcurrentMark* cm,
                          ConcurrentMarkThread* cmt) :
      AbstractGangTask("Concurrent Mark"), _cm(cm), _cmt(cmt) { }

  ~CMConcurrentMarkingTask() { }
};

// Calculates the number of active workers for a concurrent
// phase.
uint ConcurrentMark::calc_parallel_marking_threads() {
  if (G1CollectedHeap::use_parallel_gc_threads()) {
    uint n_conc_workers = 0;
    if (!UseDynamicNumberOfGCThreads ||
        (!FLAG_IS_DEFAULT(ConcGCThreads) &&
         !ForceDynamicNumberOfGCThreads)) {
      n_conc_workers = max_parallel_marking_threads();
    } else {
      n_conc_workers =
        AdaptiveSizePolicy::calc_default_active_workers(
                                     max_parallel_marking_threads(),
                                     1, /* Minimum workers */
                                     parallel_marking_threads(),
                                     Threads::number_of_non_daemon_threads());
      // Don't scale down "n_conc_workers" by scale_parallel_threads() because
      // that scaling has already gone into "_max_parallel_marking_threads".
    }
    assert(n_conc_workers > 0, "Always need at least 1");
    return n_conc_workers;
  }
  // If we are not running with any parallel GC threads we will not
  // have spawned any marking threads either. Hence the number of
  // concurrent workers should be 0.
  return 0;
}

void ConcurrentMark::scanRootRegion(HeapRegion* hr, uint worker_id) {
  // Currently, only survivors can be root regions.
  assert(hr->next_top_at_mark_start() == hr->bottom(), "invariant");
  G1RootRegionScanClosure cl(_g1h, this, worker_id);

  const uintx interval = PrefetchScanIntervalInBytes;
  HeapWord* curr = hr->bottom();
  const HeapWord* end = hr->top();
  while (curr < end) {
    Prefetch::read(curr, interval);
    oop obj = oop(curr);
    int size = obj->oop_iterate(&cl);
    assert(size == obj->size(), "sanity");
    curr += size;
  }
}

class CMRootRegionScanTask : public AbstractGangTask {
private:
  ConcurrentMark* _cm;

public:
  CMRootRegionScanTask(ConcurrentMark* cm) :
    AbstractGangTask("Root Region Scan"), _cm(cm) { }

  void work(uint worker_id) {
    assert(Thread::current()->is_ConcurrentGC_thread(),
           "this should only be done by a conc GC thread");

    CMRootRegions* root_regions = _cm->root_regions();
    HeapRegion* hr = root_regions->claim_next();
    while (hr != NULL) {
      _cm->scanRootRegion(hr, worker_id);
      hr = root_regions->claim_next();
    }
  }
};

void ConcurrentMark::scanRootRegions() {
  // scan_in_progress() will have been set to true only if there was
  // at least one root region to scan. So, if it's false, we
  // should not attempt to do any further work.
  if (root_regions()->scan_in_progress()) {
    assert(!has_aborted(), "Aborting before root region scanning is finished not supported.");
    _parallel_marking_threads = calc_parallel_marking_threads();
    assert(parallel_marking_threads() <= max_parallel_marking_threads(),
           "Maximum number of marking threads exceeded");
    uint active_workers = MAX2(1U, parallel_marking_threads());

    CMRootRegionScanTask task(this);
    if (use_parallel_marking_threads()) {
      _parallel_workers->set_active_workers((int) active_workers);
      _parallel_workers->run_task(&task);
    } else {
      task.work(0);
    }

    // It's possible that has_aborted() is true here without actually
    // aborting the survivor scan earlier. This is OK as it's
    // mainly used for sanity checking.
    root_regions()->scan_finished();
  }
}

void ConcurrentMark::markFromRoots() {
  // we might be tempted to assert that:
  // assert(asynch == !SafepointSynchronize::is_at_safepoint(),
  //        "inconsistent argument?");
  // However that wouldn't be right, because it's possible that
  // a safepoint is indeed in progress as a younger generation
  // stop-the-world GC happens even as we mark in this generation.

  _restart_for_overflow = false;
  force_overflow_conc()->init();

  // _g1h has _n_par_threads
  _parallel_marking_threads = calc_parallel_marking_threads();
  assert(parallel_marking_threads() <= max_parallel_marking_threads(),
    "Maximum number of marking threads exceeded");

  uint active_workers = MAX2(1U, parallel_marking_threads());

  // Parallel task terminator is set in "set_concurrency_and_phase()"
  set_concurrency_and_phase(active_workers, true /* concurrent */);

  CMConcurrentMarkingTask markingTask(this, cmThread());
  if (use_parallel_marking_threads()) {
    _parallel_workers->set_active_workers((int)active_workers);
    // Don't set _n_par_threads because it affects MT in process_roots()
    // and the decisions on that MT processing is made elsewhere.
    assert(_parallel_workers->active_workers() > 0, "Should have been set");
    _parallel_workers->run_task(&markingTask);
  } else {
    markingTask.work(0);
  }
  print_stats();
}

class G1UpdateRemSetTrackingBeforeRebuild : public HeapRegionClosure {
  G1CollectedHeap* _g1h;
  ConcurrentMark* _cm;

  uint _num_regions_selected_for_rebuild;  // The number of regions actually selected for rebuild.

  void update_remset_before_rebuild(HeapRegion * hr) {
    G1RemSetTrackingPolicy* tracking_policy = _g1h->g1_policy()->remset_tracker();

    size_t live_bytes = _cm->liveness(hr->hrm_index()) * HeapWordSize;
    bool selected_for_rebuild = tracking_policy->update_before_rebuild(hr, live_bytes);
    if (selected_for_rebuild) {
      _num_regions_selected_for_rebuild++;
    }
    _cm->update_top_at_rebuild_start(hr);
  }

 public:
  G1UpdateRemSetTrackingBeforeRebuild(G1CollectedHeap* g1h, ConcurrentMark* cm) :
    _g1h(g1h), _cm(cm), _num_regions_selected_for_rebuild(0) { }

  virtual bool doHeapRegion(HeapRegion* r) {
    update_remset_before_rebuild(r);
    return false;
  }

  uint num_selected_for_rebuild() const { return _num_regions_selected_for_rebuild; }
};

class G1UpdateRemSetTrackingAfterRebuild : public HeapRegionClosure {
  G1CollectedHeap* _g1h;
 public:
  G1UpdateRemSetTrackingAfterRebuild(G1CollectedHeap* g1h) : _g1h(g1h) { }

  virtual bool doHeapRegion(HeapRegion* r) {
    _g1h->g1_policy()->remset_tracker()->update_after_rebuild(r);
    return false;
  }
};

void ConcurrentMark::checkpointRootsFinal(bool clear_all_soft_refs) {
  // world is stopped at this checkpoint
  assert(SafepointSynchronize::is_at_safepoint(),
         "world should be stopped");

  G1CollectedHeap* g1h = G1CollectedHeap::heap();

  // If a full collection has happened, we shouldn't do this.
  if (has_aborted()) {
    g1h->set_marking_complete(); // So bitmap clearing isn't confused
    return;
  }

  SvcGCMarker sgcm(SvcGCMarker::OTHER);

  if (VerifyDuringGC) {
    HandleMark hm;  // handle scope
    Universe::heap()->prepare_for_verify();
    Universe::verify(VerifyOption_G1UsePrevMarking,
                     " VerifyDuringGC:(Remark before)");
  }
  g1h->check_bitmaps("Remark Start");

  G1CollectorPolicy* g1p = g1h->g1_policy();
  g1p->record_concurrent_mark_remark_start();

  double start = os::elapsedTime();

  checkpointRootsFinalWork();

  double mark_work_end = os::elapsedTime();

  weakRefsWork(clear_all_soft_refs);

  if (has_overflown()) {
    // Oops.  We overflowed.  Restart concurrent marking.
    _restart_for_overflow = true;
    if (G1TraceMarkStackOverflow) {
      gclog_or_tty->print_cr("\nRemark led to restart for overflow.");
    }

    // Verify the heap w.r.t. the previous marking bitmap.
    if (VerifyDuringGC) {
      HandleMark hm;  // handle scope
      Universe::heap()->prepare_for_verify();
      Universe::verify(VerifyOption_G1UsePrevMarking,
                       " VerifyDuringGC:(Remark overflow)");
    }

    // Clear the marking state because we will be restarting
    // marking due to overflowing the global mark stack.
    reset_marking_state();
  } else {
    SATBMarkQueueSet& satb_mq_set = JavaThread::satb_mark_queue_set();
    // We're done with marking.
    // This is the end of  the marking cycle, we're expected all
    // threads to have SATB queues with active set to true.
    satb_mq_set.set_active_all_threads(false, /* new active value */
                                       true /* expected_active */);

    {
      GCTraceTime t("Flush Task Caches", G1Log::finer(), false, g1h->gc_timer_cm(), concurrent_gc_id());
      flush_all_task_caches();
    }

    {
      GCTraceTime t("Update Remembered Set Tracking Before Rebuild", G1Log::finer(), false, g1h->gc_timer_cm(), concurrent_gc_id());
      G1UpdateRemSetTrackingBeforeRebuild cl(_g1h, this);
      g1h->heap_region_iterate(&cl);
      if (verbose_low()) {
        gclog_or_tty->print_cr("Remembered Set Tracking update regions total %u, selected %u",
                               _g1h->num_regions(), cl.num_selected_for_rebuild());
      }
    }

    g1h->shrink_heap_at_remark();
    if (VerifyDuringGC) {
      HandleMark hm;  // handle scope
      Universe::heap()->prepare_for_verify();
      Universe::verify(VerifyOption_G1UseNextMarking,
                       " VerifyDuringGC:(Remark after)");
    }
    g1h->check_bitmaps("Remark End");
    assert(!restart_for_overflow(), "sanity");
    // Completely reset the marking state since marking completed
    set_non_marking_state();
  }

  // Statistics
  double now = os::elapsedTime();
  _remark_mark_times.add((mark_work_end - start) * 1000.0);
  _remark_weak_ref_times.add((now - mark_work_end) * 1000.0);
  _remark_times.add((now - start) * 1000.0);

  g1p->record_concurrent_mark_remark_end();

  G1CMIsAliveClosure is_alive(g1h);
  g1h->gc_tracer_cm()->report_object_count_after_gc(&is_alive);
}

class G1ParNoteEndTask;

class G1NoteEndOfConcMarkClosure : public HeapRegionClosure {
  G1CollectedHeap* _g1;
  size_t _max_live_bytes;
  uint _regions_claimed;
  size_t _freed_bytes;
  FreeRegionList* _local_cleanup_list;
  HeapRegionSetCount _old_regions_removed;
  HeapRegionSetCount _humongous_regions_removed;
  HRRSCleanupTask* _hrrs_cleanup_task;
  double _claimed_region_time;
  double _max_region_time;

public:
  G1NoteEndOfConcMarkClosure(G1CollectedHeap* g1,
                             FreeRegionList* local_cleanup_list,
                             HRRSCleanupTask* hrrs_cleanup_task) :
    _g1(g1),
    _max_live_bytes(0), _regions_claimed(0),
    _freed_bytes(0),
    _claimed_region_time(0.0), _max_region_time(0.0),
    _local_cleanup_list(local_cleanup_list),
    _old_regions_removed(),
    _humongous_regions_removed(),
    _hrrs_cleanup_task(hrrs_cleanup_task) { }

  size_t freed_bytes() { return _freed_bytes; }
  const HeapRegionSetCount& old_regions_removed() { return _old_regions_removed; }
  const HeapRegionSetCount& humongous_regions_removed() { return _humongous_regions_removed; }

  bool doHeapRegion(HeapRegion *hr) {
    // We use a claim value of zero here because all regions
    // were claimed with value 1 in the FinalCount task.
    _g1->reset_gc_time_stamps(hr);
    double start = os::elapsedTime();
    _regions_claimed++;
    hr->note_end_of_marking();
    _max_live_bytes += hr->max_live_bytes();

    if (hr->used() > 0 && hr->max_live_bytes() == 0 && !hr->is_young()) {
      _freed_bytes += hr->used();
      hr->set_containing_set(NULL);
      if (hr->isHumongous()) {
        _humongous_regions_removed.increment(1u, hr->capacity());
        _g1->free_humongous_region(hr, _local_cleanup_list, true);
      } else {
        _old_regions_removed.increment(1u, hr->capacity());
        _g1->free_region(hr, _local_cleanup_list, true);
      }
    } else {
      hr->rem_set()->do_cleanup_work(_hrrs_cleanup_task);
    }

    double region_time = (os::elapsedTime() - start);
    _claimed_region_time += region_time;
    if (region_time > _max_region_time) {
      _max_region_time = region_time;
    }
    return false;
  }

  size_t max_live_bytes() { return _max_live_bytes; }
  uint regions_claimed() { return _regions_claimed; }
  double claimed_region_time_sec() { return _claimed_region_time; }
  double max_region_time_sec() { return _max_region_time; }
};

class G1ParNoteEndTask: public AbstractGangTask {
  friend class G1NoteEndOfConcMarkClosure;

protected:
  G1CollectedHeap* _g1h;
  size_t _max_live_bytes;
  size_t _freed_bytes;
  FreeRegionList* _cleanup_list;
  HeapRegionClaimer _hrclaimer;

public:
  G1ParNoteEndTask(G1CollectedHeap* g1h,
                   FreeRegionList* cleanup_list,
                   uint n_workers) :
    AbstractGangTask("G1 note end"), _g1h(g1h),
    _max_live_bytes(0), _freed_bytes(0),
    _cleanup_list(cleanup_list), _hrclaimer(n_workers) { }

  void work(uint worker_id) {
    double start = os::elapsedTime();
    FreeRegionList local_cleanup_list("Local Cleanup List");
    HRRSCleanupTask hrrs_cleanup_task;
    G1NoteEndOfConcMarkClosure g1_note_end(_g1h, &local_cleanup_list,
                                           &hrrs_cleanup_task);
    if (G1CollectedHeap::use_parallel_gc_threads()) {
      _g1h->heap_region_par_iterate_chunked(&g1_note_end, worker_id, &_hrclaimer);
    } else {
      _g1h->heap_region_iterate(&g1_note_end);
    }
    assert(g1_note_end.complete(), "Shouldn't have yielded!");

    // Now update the lists
    _g1h->remove_from_old_sets(g1_note_end.old_regions_removed(), g1_note_end.humongous_regions_removed());
    {
      MutexLockerEx x(ParGCRareEvent_lock, Mutex::_no_safepoint_check_flag);
      _g1h->decrement_summary_bytes(g1_note_end.freed_bytes());
      _max_live_bytes += g1_note_end.max_live_bytes();
      _freed_bytes += g1_note_end.freed_bytes();

      // If we iterate over the global cleanup list at the end of
      // cleanup to do this printing we will not guarantee to only
      // generate output for the newly-reclaimed regions (the list
      // might not be empty at the beginning of cleanup; we might
      // still be working on its previous contents). So we do the
      // printing here, before we append the new regions to the global
      // cleanup list.

      G1HRPrinter* hr_printer = _g1h->hr_printer();
      if (hr_printer->is_active()) {
        FreeRegionListIterator iter(&local_cleanup_list);
        while (iter.more_available()) {
          HeapRegion* hr = iter.get_next();
          hr_printer->cleanup(hr);
        }
      }

      _cleanup_list->add_ordered(&local_cleanup_list);
      assert(local_cleanup_list.is_empty(), "post-condition");

      HeapRegionRemSet::finish_cleanup_task(&hrrs_cleanup_task);
    }
  }
  size_t max_live_bytes() { return _max_live_bytes; }
  size_t freed_bytes() { return _freed_bytes; }
};

void ConcurrentMark::cleanup() {
  // world is stopped at this checkpoint
  assert(SafepointSynchronize::is_at_safepoint(),
         "world should be stopped");
  G1CollectedHeap* g1h = G1CollectedHeap::heap();

  // If a full collection has happened, we shouldn't do this.
  if (has_aborted()) {
    g1h->set_marking_complete(); // So bitmap clearing isn't confused
    return;
  }

  g1h->verify_region_sets_optional();

  if (VerifyDuringGC) { // While rebuilding the remembered set we used the next marking...
    HandleMark hm;  // handle scope
    Universe::heap()->prepare_for_verify();
    Universe::verify(VerifyOption_G1UseNextMarking,
                     " VerifyDuringGC:(Cleanup before)");
  }
  g1h->check_bitmaps("Cleanup Start");

  G1CollectorPolicy* g1p = G1CollectedHeap::heap()->g1_policy();
  g1p->record_concurrent_mark_cleanup_start();

  double start = os::elapsedTime();

  HeapRegionRemSet::reset_for_cleanup_tasks();

  uint n_workers = G1CollectedHeap::use_parallel_gc_threads() ?
                   g1h->workers()->active_workers() :
                   1;
  {
    G1UpdateRemSetTrackingAfterRebuild cl(_g1h);
    g1h->heap_region_iterate(&cl);
  }

  size_t start_used_bytes = g1h->used();
  g1h->set_marking_complete();

  double count_end = os::elapsedTime();
  double this_final_counting_time = (count_end - start);
  _total_counting_time += this_final_counting_time;

  if (G1PrintRegionLivenessInfo) {
    G1PrintRegionLivenessInfoClosure cl(gclog_or_tty, "Post-Cleanup");
    _g1h->heap_region_iterate(&cl);
  }

  // Install newly created mark bitMap as "prev".
  swapMarkBitMaps();

  g1h->reset_gc_time_stamp();

  // Note end of marking in all heap regions.
  G1ParNoteEndTask g1_par_note_end_task(g1h, &_cleanup_list, n_workers);
  if (G1CollectedHeap::use_parallel_gc_threads()) {
    g1h->set_par_threads((int)n_workers);
    g1h->workers()->run_task(&g1_par_note_end_task);
    g1h->set_par_threads(0);
  } else {
    g1_par_note_end_task.work(0);
  }
  g1h->check_gc_time_stamps();

  if (!cleanup_list_is_empty()) {
    // The cleanup list is not empty, so we'll have to process it
    // concurrently. Notify anyone else that might be wanting free
    // regions that there will be more free regions coming soon.
    g1h->set_free_regions_coming();
  }

  // this will also free any regions totally full of garbage objects,
  // and sort the regions.
  g1h->g1_policy()->record_concurrent_mark_cleanup_end((int)n_workers);

  // Statistics.
  double end = os::elapsedTime();
  _cleanup_times.add((end - start) * 1000.0);

  if (G1Log::fine()) {
    g1h->print_size_transition(gclog_or_tty,
                               start_used_bytes,
                               g1h->used(),
                               g1h->capacity());
  }

  // Clean up will have freed any regions completely full of garbage.
  // Update the soft reference policy with the new heap occupancy.
  Universe::update_heap_info_at_gc();

  if (VerifyDuringGC) {
    HandleMark hm;  // handle scope
    Universe::heap()->prepare_for_verify();
    Universe::verify(VerifyOption_G1UsePrevMarking,
                     " VerifyDuringGC:(after)");
  }
  g1h->check_bitmaps("Cleanup End");

  g1h->verify_region_sets_optional();

  // We need to make this be a "collection" so any collection pause that
  // races with it goes around and waits for completeCleanup to finish.
  g1h->increment_total_collections();

  // Clean out dead classes and update Metaspace sizes.
  if (ClassUnloadingWithConcurrentMark) {
    ClassLoaderDataGraph::purge();
  }
  MetaspaceGC::compute_new_size();

  // We reclaimed old regions so we should calculate the sizes to make
  // sure we update the old gen/space data.
  g1h->g1mm()->update_sizes();
  g1h->allocation_context_stats().update_after_mark();

  g1h->trace_heap_after_concurrent_cycle();
}

void ConcurrentMark::completeCleanup() {
  if (has_aborted()) return;

  G1CollectedHeap* g1h = G1CollectedHeap::heap();

  _cleanup_list.verify_optional();
  FreeRegionList tmp_free_list("Tmp Free List");

  if (G1ConcRegionFreeingVerbose) {
    gclog_or_tty->print_cr("G1ConcRegionFreeing [complete cleanup] : "
                           "cleanup list has %u entries",
                           _cleanup_list.length());
  }

  // No one else should be accessing the _cleanup_list at this point,
  // so it is not necessary to take any locks
  while (!_cleanup_list.is_empty()) {
    HeapRegion* hr = _cleanup_list.remove_region(true /* from_head */);
    assert(hr != NULL, "Got NULL from a non-empty list");
    hr->par_clear();
    tmp_free_list.add_ordered(hr);

    // Instead of adding one region at a time to the secondary_free_list,
    // we accumulate them in the local list and move them a few at a
    // time. This also cuts down on the number of notify_all() calls
    // we do during this process. We'll also append the local list when
    // _cleanup_list is empty (which means we just removed the last
    // region from the _cleanup_list).
    if ((tmp_free_list.length() % G1SecondaryFreeListAppendLength == 0) ||
        _cleanup_list.is_empty()) {
      if (G1ConcRegionFreeingVerbose) {
        gclog_or_tty->print_cr("G1ConcRegionFreeing [complete cleanup] : "
                               "appending %u entries to the secondary_free_list, "
                               "cleanup list still has %u entries",
                               tmp_free_list.length(),
                               _cleanup_list.length());
      }

      {
        MutexLockerEx x(SecondaryFreeList_lock, Mutex::_no_safepoint_check_flag);
        g1h->secondary_free_list_add(&tmp_free_list);
        SecondaryFreeList_lock->notify_all();
      }

      if (G1StressConcRegionFreeing) {
        for (uintx i = 0; i < G1StressConcRegionFreeingDelayMillis; ++i) {
          os::sleep(Thread::current(), (jlong) 1, false);
        }
      }
    }
  }
  assert(tmp_free_list.is_empty(), "post-condition");
}

// Supporting Object and Oop closures for reference discovery
// and processing in during marking

bool G1CMIsAliveClosure::do_object_b(oop obj) {
  HeapWord* addr = (HeapWord*)obj;
  return addr != NULL &&
         (!_g1->is_in_g1_reserved(addr) || !_g1->is_obj_ill(obj));
}

// 'Keep Alive' oop closure used by both serial parallel reference processing.
// Uses the CMTask associated with a worker thread (for serial reference
// processing the CMTask for worker 0 is used) to preserve (mark) and
// trace referent objects.
//
// Using the CMTask and embedded local queues avoids having the worker
// threads operating on the global mark stack. This reduces the risk
// of overflowing the stack - which we would rather avoid at this late
// state. Also using the tasks' local queues removes the potential
// of the workers interfering with each other that could occur if
// operating on the global stack.

class G1CMKeepAliveAndDrainClosure: public OopClosure {
  ConcurrentMark* _cm;
  CMTask*         _task;
  int             _ref_counter_limit;
  int             _ref_counter;
  bool            _is_serial;
 public:
  G1CMKeepAliveAndDrainClosure(ConcurrentMark* cm, CMTask* task, bool is_serial) :
    _cm(cm), _task(task), _is_serial(is_serial),
    _ref_counter_limit(G1RefProcDrainInterval) {
    assert(_ref_counter_limit > 0, "sanity");
    assert(!_is_serial || _task->worker_id() == 0, "only task 0 for serial code");
    _ref_counter = _ref_counter_limit;
  }

  virtual void do_oop(narrowOop* p) { do_oop_work(p); }
  virtual void do_oop(      oop* p) { do_oop_work(p); }

  template <class T> void do_oop_work(T* p) {
    if (!_cm->has_overflown()) {
      if (_cm->verbose_high()) {
        gclog_or_tty->print_cr("\t[%u] we're looking at location " PTR_FORMAT "",
                               _task->worker_id(), p2i(p));
      }

      _task->deal_with_reference(p);
      _ref_counter--;

      if (_ref_counter == 0) {
        // We have dealt with _ref_counter_limit references, pushing them
        // and objects reachable from them on to the local stack (and
        // possibly the global stack). Call CMTask::do_marking_step() to
        // process these entries.
        //
        // We call CMTask::do_marking_step() in a loop, which we'll exit if
        // there's nothing more to do (i.e. we're done with the entries that
        // were pushed as a result of the CMTask::deal_with_reference() calls
        // above) or we overflow.
        //
        // Note: CMTask::do_marking_step() can set the CMTask::has_aborted()
        // flag while there may still be some work to do. (See the comment at
        // the beginning of CMTask::do_marking_step() for those conditions -
        // one of which is reaching the specified time target.) It is only
        // when CMTask::do_marking_step() returns without setting the
        // has_aborted() flag that the marking step has completed.
        do {
          double mark_step_duration_ms = G1ConcMarkStepDurationMillis;
          _task->do_marking_step(mark_step_duration_ms,
                                 false      /* do_termination */,
                                 _is_serial);
        } while (_task->has_aborted() && !_cm->has_overflown());
        _ref_counter = _ref_counter_limit;
      }
    } else {
      if (_cm->verbose_high()) {
         gclog_or_tty->print_cr("\t[%u] CM Overflow", _task->worker_id());
      }
    }
  }
};

// 'Drain' oop closure used by both serial and parallel reference processing.
// Uses the CMTask associated with a given worker thread (for serial
// reference processing the CMtask for worker 0 is used). Calls the
// do_marking_step routine, with an unbelievably large timeout value,
// to drain the marking data structures of the remaining entries
// added by the 'keep alive' oop closure above.

class G1CMDrainMarkingStackClosure: public VoidClosure {
  ConcurrentMark* _cm;
  CMTask*         _task;
  bool            _is_serial;
 public:
  G1CMDrainMarkingStackClosure(ConcurrentMark* cm, CMTask* task, bool is_serial) :
    _cm(cm), _task(task), _is_serial(is_serial) {
    assert(!_is_serial || _task->worker_id() == 0, "only task 0 for serial code");
  }

  void do_void() {
    do {
      if (_cm->verbose_high()) {
        gclog_or_tty->print_cr("\t[%u] Drain: Calling do_marking_step - serial: %s",
                               _task->worker_id(), BOOL_TO_STR(_is_serial));
      }

      // We call CMTask::do_marking_step() to completely drain the local
      // and global marking stacks of entries pushed by the 'keep alive'
      // oop closure (an instance of G1CMKeepAliveAndDrainClosure above).
      //
      // CMTask::do_marking_step() is called in a loop, which we'll exit
      // if there's nothing more to do (i.e. we'completely drained the
      // entries that were pushed as a a result of applying the 'keep alive'
      // closure to the entries on the discovered ref lists) or we overflow
      // the global marking stack.
      //
      // Note: CMTask::do_marking_step() can set the CMTask::has_aborted()
      // flag while there may still be some work to do. (See the comment at
      // the beginning of CMTask::do_marking_step() for those conditions -
      // one of which is reaching the specified time target.) It is only
      // when CMTask::do_marking_step() returns without setting the
      // has_aborted() flag that the marking step has completed.

      _task->do_marking_step(1000000000.0 /* something very large */,
                             true         /* do_termination */,
                             _is_serial);
    } while (_task->has_aborted() && !_cm->has_overflown());
  }
};

// Implementation of AbstractRefProcTaskExecutor for parallel
// reference processing at the end of G1 concurrent marking

class G1CMRefProcTaskExecutor: public AbstractRefProcTaskExecutor {
private:
  G1CollectedHeap* _g1h;
  ConcurrentMark*  _cm;
  WorkGang*        _workers;
  int              _active_workers;

public:
  G1CMRefProcTaskExecutor(G1CollectedHeap* g1h,
                        ConcurrentMark* cm,
                        WorkGang* workers,
                        int n_workers) :
    _g1h(g1h), _cm(cm),
    _workers(workers), _active_workers(n_workers) { }

  // Executes the given task using concurrent marking worker threads.
  virtual void execute(ProcessTask& task);
  virtual void execute(EnqueueTask& task);
};

class G1CMRefProcTaskProxy: public AbstractGangTask {
  typedef AbstractRefProcTaskExecutor::ProcessTask ProcessTask;
  ProcessTask&     _proc_task;
  G1CollectedHeap* _g1h;
  ConcurrentMark*  _cm;

public:
  G1CMRefProcTaskProxy(ProcessTask& proc_task,
                     G1CollectedHeap* g1h,
                     ConcurrentMark* cm) :
    AbstractGangTask("Process reference objects in parallel"),
    _proc_task(proc_task), _g1h(g1h), _cm(cm) {
    ReferenceProcessor* rp = _g1h->ref_processor_cm();
    assert(rp->processing_is_mt(), "shouldn't be here otherwise");
  }

  virtual void work(uint worker_id) {
    ResourceMark rm;
    HandleMark hm;
    CMTask* task = _cm->task(worker_id);
    G1CMIsAliveClosure g1_is_alive(_g1h);
    G1CMKeepAliveAndDrainClosure g1_par_keep_alive(_cm, task, false /* is_serial */);
    G1CMDrainMarkingStackClosure g1_par_drain(_cm, task, false /* is_serial */);

    _proc_task.work(worker_id, g1_is_alive, g1_par_keep_alive, g1_par_drain);
  }
};

void G1CMRefProcTaskExecutor::execute(ProcessTask& proc_task) {
  assert(_workers != NULL, "Need parallel worker threads.");
  assert(_g1h->ref_processor_cm()->processing_is_mt(), "processing is not MT");

  G1CMRefProcTaskProxy proc_task_proxy(proc_task, _g1h, _cm);

  // We need to reset the concurrency level before each
  // proxy task execution, so that the termination protocol
  // and overflow handling in CMTask::do_marking_step() knows
  // how many workers to wait for.
  _cm->set_concurrency(_active_workers);
  _g1h->set_par_threads(_active_workers);
  _workers->run_task(&proc_task_proxy);
  _g1h->set_par_threads(0);
}

class G1CMRefEnqueueTaskProxy: public AbstractGangTask {
  typedef AbstractRefProcTaskExecutor::EnqueueTask EnqueueTask;
  EnqueueTask& _enq_task;

public:
  G1CMRefEnqueueTaskProxy(EnqueueTask& enq_task) :
    AbstractGangTask("Enqueue reference objects in parallel"),
    _enq_task(enq_task) { }

  virtual void work(uint worker_id) {
    _enq_task.work(worker_id);
  }
};

void G1CMRefProcTaskExecutor::execute(EnqueueTask& enq_task) {
  assert(_workers != NULL, "Need parallel worker threads.");
  assert(_g1h->ref_processor_cm()->processing_is_mt(), "processing is not MT");

  G1CMRefEnqueueTaskProxy enq_task_proxy(enq_task);

  // Not strictly necessary but...
  //
  // We need to reset the concurrency level before each
  // proxy task execution, so that the termination protocol
  // and overflow handling in CMTask::do_marking_step() knows
  // how many workers to wait for.
  _cm->set_concurrency(_active_workers);
  _g1h->set_par_threads(_active_workers);
  _workers->run_task(&enq_task_proxy);
  _g1h->set_par_threads(0);
}

void ConcurrentMark::weakRefsWorkParallelPart(BoolObjectClosure* is_alive, bool purged_classes) {
  G1CollectedHeap::heap()->parallel_cleaning(is_alive, true, true, purged_classes);
}

// Helper class to get rid of some boilerplate code.
class G1RemarkGCTraceTime : public GCTraceTime {
  static bool doit_and_prepend(bool doit) {
    if (doit) {
      gclog_or_tty->put(' ');
    }
    return doit;
  }

 public:
  G1RemarkGCTraceTime(const char* title, bool doit)
    : GCTraceTime(title, doit_and_prepend(doit), false, G1CollectedHeap::heap()->gc_timer_cm(),
        G1CollectedHeap::heap()->concurrent_mark()->concurrent_gc_id()) {
  }
};

void ConcurrentMark::weakRefsWork(bool clear_all_soft_refs) {
  if (has_overflown()) {
    // Skip processing the discovered references if we have
    // overflown the global marking stack. Reference objects
    // only get discovered once so it is OK to not
    // de-populate the discovered reference lists. We could have,
    // but the only benefit would be that, when marking restarts,
    // less reference objects are discovered.
    return;
  }

  ResourceMark rm;
  HandleMark   hm;

  G1CollectedHeap* g1h = G1CollectedHeap::heap();

  // Is alive closure.
  G1CMIsAliveClosure g1_is_alive(g1h);

  // Inner scope to exclude the cleaning of the string and symbol
  // tables from the displayed time.
  {
    if (G1Log::finer()) {
      gclog_or_tty->put(' ');
    }
    GCTraceTime t("GC ref-proc", G1Log::finer(), false, g1h->gc_timer_cm(), concurrent_gc_id());

    ReferenceProcessor* rp = g1h->ref_processor_cm();

    // See the comment in G1CollectedHeap::ref_processing_init()
    // about how reference processing currently works in G1.

    // Set the soft reference policy
    rp->setup_policy(clear_all_soft_refs);
    assert(_markStack.isEmpty(), "mark stack should be empty");

    // Instances of the 'Keep Alive' and 'Complete GC' closures used
    // in serial reference processing. Note these closures are also
    // used for serially processing (by the the current thread) the
    // JNI references during parallel reference processing.
    //
    // These closures do not need to synchronize with the worker
    // threads involved in parallel reference processing as these
    // instances are executed serially by the current thread (e.g.
    // reference processing is not multi-threaded and is thus
    // performed by the current thread instead of a gang worker).
    //
    // The gang tasks involved in parallel reference procssing create
    // their own instances of these closures, which do their own
    // synchronization among themselves.
    G1CMKeepAliveAndDrainClosure g1_keep_alive(this, task(0), true /* is_serial */);
    G1CMDrainMarkingStackClosure g1_drain_mark_stack(this, task(0), true /* is_serial */);

    // We need at least one active thread. If reference processing
    // is not multi-threaded we use the current (VMThread) thread,
    // otherwise we use the work gang from the G1CollectedHeap and
    // we utilize all the worker threads we can.
    bool processing_is_mt = rp->processing_is_mt() && g1h->workers() != NULL;
    uint active_workers = (processing_is_mt ? g1h->workers()->active_workers() : 1U);
    active_workers = MAX2(MIN2(active_workers, _max_worker_id), 1U);

    // Parallel processing task executor.
    G1CMRefProcTaskExecutor par_task_executor(g1h, this,
                                              g1h->workers(), active_workers);
    AbstractRefProcTaskExecutor* executor = (processing_is_mt ? &par_task_executor : NULL);

    // Set the concurrency level. The phase was already set prior to
    // executing the remark task.
    set_concurrency(active_workers);

    // Set the degree of MT processing here.  If the discovery was done MT,
    // the number of threads involved during discovery could differ from
    // the number of active workers.  This is OK as long as the discovered
    // Reference lists are balanced (see balance_all_queues() and balance_queues()).
    rp->set_active_mt_degree(active_workers);

    // Process the weak references.
    const ReferenceProcessorStats& stats =
        rp->process_discovered_references(&g1_is_alive,
                                          &g1_keep_alive,
                                          &g1_drain_mark_stack,
                                          executor,
                                          g1h->gc_timer_cm(),
                                          concurrent_gc_id());
    g1h->gc_tracer_cm()->report_gc_reference_stats(stats);

    // The do_oop work routines of the keep_alive and drain_marking_stack
    // oop closures will set the has_overflown flag if we overflow the
    // global marking stack.

    assert(_markStack.overflow() || _markStack.isEmpty(),
            "mark stack should be empty (unless it overflowed)");

    if (_markStack.overflow()) {
      // This should have been done already when we tried to push an
      // entry on to the global mark stack. But let's do it again.
      set_has_overflown();
    }

    assert(rp->num_q() == active_workers, "why not");

    rp->enqueue_discovered_references(executor);

    rp->verify_no_references_recorded();
    assert(!rp->discovery_enabled(), "Post condition");
  }

  if (has_overflown()) {
    // We can not trust g1_is_alive if the marking stack overflowed
    return;
  }

  assert(_markStack.isEmpty(), "Marking should have completed");

  // Unload Klasses, String, Symbols, Code Cache, etc.
  {
    G1RemarkGCTraceTime trace("Unloading", G1Log::finer());

    if (ClassUnloadingWithConcurrentMark) {
      // Cleaning of klasses depends on correct information from MetadataMarkOnStack. The CodeCache::mark_on_stack
      // part is too slow to be done serially, so it is handled during the weakRefsWorkParallelPart phase.
      // Defer the cleaning until we have complete on_stack data.
      MetadataOnStackMark md_on_stack(false /* Don't visit the code cache at this point */);

      bool purged_classes;

      {
        G1RemarkGCTraceTime trace("System Dictionary Unloading", G1Log::finest());
        purged_classes = SystemDictionary::do_unloading(&g1_is_alive, false /* Defer klass cleaning */);
      }

      {
        G1RemarkGCTraceTime trace("Parallel Unloading", G1Log::finest());
        weakRefsWorkParallelPart(&g1_is_alive, purged_classes);
      }

      {
        G1RemarkGCTraceTime trace("Deallocate Metadata", G1Log::finest());
        ClassLoaderDataGraph::free_deallocate_lists();
      }
    }

    if (G1StringDedup::is_enabled()) {
      G1RemarkGCTraceTime trace("String Deduplication Unlink", G1Log::finest());
      G1StringDedup::unlink(&g1_is_alive);
    }
  }
}

void ConcurrentMark::swapMarkBitMaps() {
  CMBitMapRO* temp = _prevMarkBitMap;
  _prevMarkBitMap  = (CMBitMapRO*)_nextMarkBitMap;
  _nextMarkBitMap  = (CMBitMap*)  temp;
}

// Closure for marking entries in SATB buffers.
class CMSATBBufferClosure : public SATBBufferClosure {
private:
  CMTask* _task;
  G1CollectedHeap* _g1h;

  // This is very similar to CMTask::deal_with_reference, but with
  // more relaxed requirements for the argument, so this must be more
  // circumspect about treating the argument as an object.
  void do_entry(void* entry) const {
    _task->increment_refs_reached();
    oop const obj = static_cast<oop>(entry);
    _task->make_reference_grey(obj);
  }

public:
  CMSATBBufferClosure(CMTask* task, G1CollectedHeap* g1h)
    : _task(task), _g1h(g1h) { }

  virtual void do_buffer(void** buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      do_entry(buffer[i]);
    }
  }
};

class G1RemarkThreadsClosure : public ThreadClosure {
  CMSATBBufferClosure _cm_satb_cl;
  G1CMOopClosure _cm_cl;
  MarkingCodeBlobClosure _code_cl;
  int _thread_parity;
  bool _is_par;

 public:
  G1RemarkThreadsClosure(G1CollectedHeap* g1h, CMTask* task, bool is_par) :
    _cm_satb_cl(task, g1h),
    _cm_cl(g1h, g1h->concurrent_mark(), task),
    _code_cl(&_cm_cl, !CodeBlobToOopClosure::FixRelocations),
    _thread_parity(SharedHeap::heap()->strong_roots_parity()), _is_par(is_par) {}

  void do_thread(Thread* thread) {
    if (thread->is_Java_thread()) {
      if (thread->claim_oops_do(_is_par, _thread_parity)) {
        JavaThread* jt = (JavaThread*)thread;

        // In theory it should not be neccessary to explicitly walk the nmethods to find roots for concurrent marking
        // however the liveness of oops reachable from nmethods have very complex lifecycles:
        // * Alive if on the stack of an executing method
        // * Weakly reachable otherwise
        // Some objects reachable from nmethods, such as the class loader (or klass_holder) of the receiver should be
        // live by the SATB invariant but other oops recorded in nmethods may behave differently.
        jt->nmethods_do(&_code_cl);

        jt->satb_mark_queue().apply_closure_and_empty(&_cm_satb_cl);
      }
    } else if (thread->is_VM_thread()) {
      if (thread->claim_oops_do(_is_par, _thread_parity)) {
        JavaThread::satb_mark_queue_set().shared_satb_queue()->apply_closure_and_empty(&_cm_satb_cl);
      }
    }
  }
};

class CMRemarkTask: public AbstractGangTask {
private:
  ConcurrentMark* _cm;
  bool            _is_serial;
public:
  void work(uint worker_id) {
    // Since all available tasks are actually started, we should
    // only proceed if we're supposed to be actived.
    if (worker_id < _cm->active_tasks()) {
      CMTask* task = _cm->task(worker_id);
      task->record_start_time();
      {
        ResourceMark rm;
        HandleMark hm;

        G1RemarkThreadsClosure threads_f(G1CollectedHeap::heap(), task, !_is_serial);
        Threads::threads_do(&threads_f);
      }

      do {
        task->do_marking_step(1000000000.0 /* something very large */,
                              true         /* do_termination       */,
                              _is_serial);
      } while (task->has_aborted() && !_cm->has_overflown());
      // If we overflow, then we do not want to restart. We instead
      // want to abort remark and do concurrent marking again.
      task->record_end_time();
    }
  }

  CMRemarkTask(ConcurrentMark* cm, int active_workers, bool is_serial) :
    AbstractGangTask("Par Remark"), _cm(cm), _is_serial(is_serial) {
    _cm->terminator()->reset_for_reuse(active_workers);
  }
};

void ConcurrentMark::checkpointRootsFinalWork() {
  ResourceMark rm;
  HandleMark   hm;
  G1CollectedHeap* g1h = G1CollectedHeap::heap();

  G1RemarkGCTraceTime trace("Finalize Marking", G1Log::finer());

  g1h->ensure_parsability(false);

  if (G1CollectedHeap::use_parallel_gc_threads()) {
    G1CollectedHeap::StrongRootsScope srs(g1h);
    // this is remark, so we'll use up all active threads
    uint active_workers = g1h->workers()->active_workers();
    if (active_workers == 0) {
      assert(active_workers > 0, "Should have been set earlier");
      active_workers = (uint) ParallelGCThreads;
      g1h->workers()->set_active_workers(active_workers);
    }
    set_concurrency_and_phase(active_workers, false /* concurrent */);
    // Leave _parallel_marking_threads at it's
    // value originally calculated in the ConcurrentMark
    // constructor and pass values of the active workers
    // through the gang in the task.

    CMRemarkTask remarkTask(this, active_workers, false /* is_serial */);
    // We will start all available threads, even if we decide that the
    // active_workers will be fewer. The extra ones will just bail out
    // immediately.
    g1h->set_par_threads(active_workers);
    g1h->workers()->run_task(&remarkTask);
    g1h->set_par_threads(0);
  } else {
    G1CollectedHeap::StrongRootsScope srs(g1h);
    uint active_workers = 1;
    set_concurrency_and_phase(active_workers, false /* concurrent */);

    // Note - if there's no work gang then the VMThread will be
    // the thread to execute the remark - serially. We have
    // to pass true for the is_serial parameter so that
    // CMTask::do_marking_step() doesn't enter the sync
    // barriers in the event of an overflow. Doing so will
    // cause an assert that the current thread is not a
    // concurrent GC thread.
    CMRemarkTask remarkTask(this, active_workers, true /* is_serial*/);
    remarkTask.work(0);
  }
  SATBMarkQueueSet& satb_mq_set = JavaThread::satb_mark_queue_set();
  guarantee(has_overflown() ||
            satb_mq_set.completed_buffers_num() == 0,
            err_msg("Invariant: has_overflown = %s, num buffers = %d",
                    BOOL_TO_STR(has_overflown()),
                    satb_mq_set.completed_buffers_num()));

  print_stats();
}

void ConcurrentMark::flush_all_task_caches() {
  size_t hits = 0;
  size_t misses = 0;
  for (uint i = 0; i < _max_worker_id; i++) {
    Pair<size_t, size_t> stats = _tasks[i]->flush_mark_stats_cache();
    hits += stats.first;
    misses += stats.second;
  }
  size_t sum = hits + misses;
  if (G1Log::finer()) {
    gclog_or_tty->print("Mark stats cache hits " SIZE_FORMAT " misses " SIZE_FORMAT " ratio %1.3lf",
                        hits, misses, sum != 0 ? double(hits) / sum * 100.0 : 0.0);
  }
}

#ifndef PRODUCT

class PrintReachableOopClosure: public OopClosure {
private:
  G1CollectedHeap* _g1h;
  outputStream*    _out;
  VerifyOption     _vo;
  bool             _all;

public:
  PrintReachableOopClosure(outputStream* out,
                           VerifyOption  vo,
                           bool          all) :
    _g1h(G1CollectedHeap::heap()),
    _out(out), _vo(vo), _all(all) { }

  void do_oop(narrowOop* p) { do_oop_work(p); }
  void do_oop(      oop* p) { do_oop_work(p); }

  template <class T> void do_oop_work(T* p) {
    oop         obj = oopDesc::load_decode_heap_oop(p);
    const char* str = NULL;
    const char* str2 = "";

    if (obj == NULL) {
      str = "";
    } else if (!_g1h->is_in_g1_reserved(obj)) {
      str = " O";
    } else {
      HeapRegion* hr  = _g1h->heap_region_containing(obj);
      bool over_tams = _g1h->allocated_since_marking(obj, hr, _vo);
      bool marked = _g1h->is_marked(obj, _vo);

      if (over_tams) {
        str = " >";
        if (marked) {
          str2 = " AND MARKED";
        }
      } else if (marked) {
        str = " M";
      } else {
        str = " NOT";
      }
    }

    _out->print_cr("  " PTR_FORMAT ": " PTR_FORMAT "%s%s",
                   p2i(p), p2i((void*) obj), str, str2);
  }
};

class PrintReachableObjectClosure : public ObjectClosure {
private:
  G1CollectedHeap* _g1h;
  outputStream*    _out;
  VerifyOption     _vo;
  bool             _all;
  HeapRegion*      _hr;

public:
  PrintReachableObjectClosure(outputStream* out,
                              VerifyOption  vo,
                              bool          all,
                              HeapRegion*   hr) :
    _g1h(G1CollectedHeap::heap()),
    _out(out), _vo(vo), _all(all), _hr(hr) { }

  void do_object(oop o) {
    bool over_tams = _g1h->allocated_since_marking(o, _hr, _vo);
    bool marked = _g1h->is_marked(o, _vo);
    bool print_it = _all || over_tams || marked;

    if (print_it) {
      _out->print_cr(" " PTR_FORMAT "%s",
                     p2i((void *)o), (over_tams) ? " >" : (marked) ? " M" : "");
      PrintReachableOopClosure oopCl(_out, _vo, _all);
      o->oop_iterate_no_header(&oopCl);
    }
  }
};

class PrintReachableRegionClosure : public HeapRegionClosure {
private:
  G1CollectedHeap* _g1h;
  outputStream*    _out;
  VerifyOption     _vo;
  bool             _all;

public:
  bool doHeapRegion(HeapRegion* hr) {
    HeapWord* b = hr->bottom();
    HeapWord* e = hr->end();
    HeapWord* t = hr->top();
    HeapWord* p = _g1h->top_at_mark_start(hr, _vo);
    _out->print_cr("** [" PTR_FORMAT ", " PTR_FORMAT "] top: " PTR_FORMAT " "
                   "TAMS: " PTR_FORMAT, p2i(b), p2i(e), p2i(t), p2i(p));
    _out->cr();

    HeapWord* from = b;
    HeapWord* to   = t;

    if (to > from) {
      _out->print_cr("Objects in [" PTR_FORMAT ", " PTR_FORMAT "]", p2i(from), p2i(to));
      _out->cr();
      PrintReachableObjectClosure ocl(_out, _vo, _all, hr);
      hr->object_iterate_mem_careful(MemRegion(from, to), &ocl);
      _out->cr();
    }

    return false;
  }

  PrintReachableRegionClosure(outputStream* out,
                              VerifyOption  vo,
                              bool          all) :
    _g1h(G1CollectedHeap::heap()), _out(out), _vo(vo), _all(all) { }
};

void ConcurrentMark::print_reachable(const char* str,
                                     VerifyOption vo,
                                     bool all) {
  gclog_or_tty->cr();
  gclog_or_tty->print_cr("== Doing heap dump... ");

  if (G1PrintReachableBaseFile == NULL) {
    gclog_or_tty->print_cr("  #### error: no base file defined");
    return;
  }

  if (strlen(G1PrintReachableBaseFile) + 1 + strlen(str) >
      (JVM_MAXPATHLEN - 1)) {
    gclog_or_tty->print_cr("  #### error: file name too long");
    return;
  }

  // fix gcc 12 build jdk8 fastdebug compiler error:
  // directive writing up to 4096 bytes into a region of size between 0 and 4096 [-Werror=format-overflow=]
  // about old code:
  // char file_name[JVM_MAXPATHLEN];
  // Leave L 2911~2915 code unchanged, so not affect original logic.
  char *file_name = (char *) NEW_C_HEAP_ARRAY(char, strlen(G1PrintReachableBaseFile) + 2 + strlen(str), mtGC);
  if (NULL == file_name) {
    gclog_or_tty->print_cr("  #### error: NEW_C_HEAP_ARRAY failed.");
    return;
  }
  sprintf(file_name, "%s.%s", G1PrintReachableBaseFile, str);
  gclog_or_tty->print_cr("  dumping to file %s", file_name);

  fileStream fout(file_name);
  if (!fout.is_open()) {
    gclog_or_tty->print_cr("  #### error: could not open file");
    FREE_C_HEAP_ARRAY(char, file_name, mtGC);
    return;
  }

  outputStream* out = &fout;
  out->print_cr("-- USING %s", _g1h->top_at_mark_start_str(vo));
  out->cr();

  out->print_cr("--- ITERATING OVER REGIONS");
  out->cr();
  PrintReachableRegionClosure rcl(out, vo, all);
  _g1h->heap_region_iterate(&rcl);
  out->cr();

  gclog_or_tty->print_cr("  done");
  gclog_or_tty->flush();
  FREE_C_HEAP_ARRAY(char, file_name, mtGC);
}

#endif // PRODUCT

void ConcurrentMark::clearRangePrevBitmap(MemRegion mr) {
  // Note we are overriding the read-only view of the prev map here, via
  // the cast.
  ((CMBitMap*)_prevMarkBitMap)->clearRange(mr);
}

void ConcurrentMark::clearRangeNextBitmap(MemRegion mr) {
  _nextMarkBitMap->clearRange(mr);
}

HeapRegion*
ConcurrentMark::claim_region(uint worker_id) {
  // "checkpoint" the finger
  HeapWord* finger = _finger;

  // _heap_end will not change underneath our feet; it only changes at
  // yield points.
  while (finger < _heap_end) {
    assert(_g1h->is_in_g1_reserved(finger), "invariant");

    HeapRegion* curr_region = _g1h->heap_region_containing(finger);

    // Make sure that the reads below do not float before loading curr_region.
    OrderAccess::loadload();
    // Above heap_region_containing_raw may return NULL as we always scan claim
    // until the end of the heap. In this case, just jump to the next region.
    HeapWord* end = curr_region != NULL ? curr_region->end() : finger + HeapRegion::GrainWords;

    // Is the gap between reading the finger and doing the CAS too long?
    HeapWord* res = (HeapWord*) Atomic::cmpxchg_ptr(end, &_finger, finger);
    if (res == finger && curr_region != NULL) {
      // we succeeded
      HeapWord*   bottom        = curr_region->bottom();
      HeapWord*   limit         = curr_region->next_top_at_mark_start();

      if (verbose_low()) {
        gclog_or_tty->print_cr("[%u] curr_region = " PTR_FORMAT " "
                               "[" PTR_FORMAT ", " PTR_FORMAT "), "
                               "limit = " PTR_FORMAT,
                               worker_id, p2i(curr_region), p2i(bottom), p2i(end), p2i(limit));
      }

      // notice that _finger == end cannot be guaranteed here since,
      // someone else might have moved the finger even further
      assert(_finger >= end, "the finger should have moved forward");

      if (verbose_low()) {
        gclog_or_tty->print_cr("[%u] we were successful with region = "
                               PTR_FORMAT, worker_id, p2i(curr_region));
      }

      if (limit > bottom) {
        if (verbose_low()) {
          gclog_or_tty->print_cr("[%u] region " PTR_FORMAT " is not empty, "
                                 "returning it ", worker_id, p2i(curr_region));
        }
        return curr_region;
      } else {
        assert(limit == bottom,
               "the region limit should be at bottom");
        if (verbose_low()) {
          gclog_or_tty->print_cr("[%u] region " PTR_FORMAT " is empty, "
                                 "returning NULL", worker_id, p2i(curr_region));
        }
        // we return NULL and the caller should try calling
        // claim_region() again.
        return NULL;
      }
    } else {
      assert(_finger > finger, "the finger should have moved forward");
      if (verbose_low()) {
        if (curr_region == NULL) {
          gclog_or_tty->print_cr("[%u] found uncommitted region, moving finger, "
                                 "global finger = " PTR_FORMAT ", "
                                 "our finger = " PTR_FORMAT,
                                 worker_id, p2i(_finger), p2i(finger));
        } else {
          gclog_or_tty->print_cr("[%u] somebody else moved the finger, "
                                 "global finger = " PTR_FORMAT ", "
                                 "our finger = " PTR_FORMAT,
                                 worker_id, p2i(_finger), p2i(finger));
        }
      }

      // read it again
      finger = _finger;
    }
  }

  return NULL;
}

#ifndef PRODUCT
enum VerifyNoCSetOopsPhase {
  VerifyNoCSetOopsStack,
  VerifyNoCSetOopsQueues
};

class VerifyNoCSetOopsClosure : public OopClosure, public ObjectClosure  {
private:
  G1CollectedHeap* _g1h;
  VerifyNoCSetOopsPhase _phase;
  int _info;

  const char* phase_str() {
    switch (_phase) {
    case VerifyNoCSetOopsStack:         return "Stack";
    case VerifyNoCSetOopsQueues:        return "Queue";
    default:                            ShouldNotReachHere();
    }
    return NULL;
  }

  void do_object_work(oop obj) {
    guarantee(G1CMObjArrayProcessor::is_array_slice(obj) || obj->is_oop(),
              err_msg("Non-oop " PTR_FORMAT ", phase: %s, info: %d",
                      p2i((void*) obj), phase_str(), _info));
    guarantee(G1CMObjArrayProcessor::is_array_slice(obj) || !_g1h->obj_in_cs(obj),
              err_msg("obj: " PTR_FORMAT " in CSet, phase: %s, info: %d",
                      p2i((void*) obj), phase_str(), _info));
  }

public:
  VerifyNoCSetOopsClosure() : _g1h(G1CollectedHeap::heap()) { }

  void set_phase(VerifyNoCSetOopsPhase phase, int info = -1) {
    _phase = phase;
    _info = info;
  }

  virtual void do_oop(oop* p) {
    oop obj = oopDesc::load_decode_heap_oop(p);
    do_object_work(obj);
  }

  virtual void do_oop(narrowOop* p) {
    // We should not come across narrow oops while scanning marking
    // stacks
    ShouldNotReachHere();
  }

  virtual void do_object(oop obj) {
    do_object_work(obj);
  }
};

void ConcurrentMark::verify_no_cset_oops() {
  assert(SafepointSynchronize::is_at_safepoint(), "should be at a safepoint");
  if (!G1CollectedHeap::heap()->mark_in_progress()) {
    return;
  }

  VerifyNoCSetOopsClosure cl;

  // Verify entries on the global mark stack
  cl.set_phase(VerifyNoCSetOopsStack);
  _markStack.oops_do(&cl);

  // Verify entries on the task queues
  for (uint i = 0; i < _max_worker_id; i += 1) {
    cl.set_phase(VerifyNoCSetOopsQueues, i);
    CMTaskQueue* queue = _task_queues->queue(i);
    queue->oops_do(&cl);
  }

  // Verify the global finger
  HeapWord* global_finger = finger();
  if (global_finger != NULL && global_finger < _heap_end) {
    // Since we always iterate over all regions, we might get a NULL HeapRegion
    // here.
    HeapRegion* global_hr = _g1h->heap_region_containing(global_finger);
    guarantee(global_hr == NULL || global_finger == global_hr->bottom(),
              err_msg("global finger: " PTR_FORMAT " region: " HR_FORMAT,
                      p2i(global_finger), HR_FORMAT_PARAMS(global_hr)));
  }

  // Verify the task fingers
  assert(parallel_marking_threads() <= _max_worker_id, "sanity");
  for (int i = 0; i < (int) parallel_marking_threads(); i += 1) {
    CMTask* task = _tasks[i];
    HeapWord* task_finger = task->finger();
    if (task_finger != NULL && task_finger < _heap_end) {
      // See above note on the global finger verification.
      HeapRegion* task_hr = _g1h->heap_region_containing(task_finger);
      guarantee(task_hr == NULL || task_finger == task_hr->bottom() ||
                !task_hr->in_collection_set(),
                err_msg("task finger: " PTR_FORMAT " region: " HR_FORMAT,
                        p2i(task_finger), HR_FORMAT_PARAMS(task_hr)));
    }
  }
}
#endif // PRODUCT

void ConcurrentMark::rebuild_rem_set_concurrently() {
  uint num_workers = MAX2(1U, calc_parallel_marking_threads());
  bool use_parallel = use_parallel_marking_threads();
  _g1h->g1_rem_set()->rebuild_rem_set(this, _parallel_workers, use_parallel, num_workers, _worker_id_offset);
}

void ConcurrentMark::print_stats() {
  if (verbose_stats()) {
    gclog_or_tty->print_cr("---------------------------------------------------------------------");
    for (size_t i = 0; i < _active_tasks; ++i) {
      _tasks[i]->print_stats();
      gclog_or_tty->print_cr("---------------------------------------------------------------------");
    }
  }
}

// abandon current marking iteration due to a Full GC
void ConcurrentMark::abort() {
  // Clear all marks in the next bitmap for the next marking cycle. This will allow us to skip the next
  // concurrent bitmap clearing.
  _nextMarkBitMap->clearAll();

  // Note we cannot clear the previous marking bitmap here
  // since VerifyDuringGC verifies the objects marked during
  // a full GC against the previous bitmap.

  // Empty mark stack
  reset_marking_state();
  for (uint i = 0; i < _max_worker_id; ++i) {
    _tasks[i]->clear_region_fields();
  }
  _first_overflow_barrier_sync.abort();
  _second_overflow_barrier_sync.abort();
  const GCId& gc_id = _g1h->gc_tracer_cm()->gc_id();
  if (!gc_id.is_undefined()) {
    // We can do multiple full GCs before ConcurrentMarkThread::run() gets a chance
    // to detect that it was aborted. Only keep track of the first GC id that we aborted.
    _aborted_gc_id = gc_id;
   }
  _has_aborted = true;

  SATBMarkQueueSet& satb_mq_set = JavaThread::satb_mark_queue_set();
  satb_mq_set.abandon_partial_marking();
  // This can be called either during or outside marking, we'll read
  // the expected_active value from the SATB queue set.
  satb_mq_set.set_active_all_threads(
                                 false, /* new active value */
                                 satb_mq_set.is_active() /* expected_active */);

  _g1h->trace_heap_after_concurrent_cycle();
  _g1h->register_concurrent_cycle_end();
}

const GCId& ConcurrentMark::concurrent_gc_id() {
  if (has_aborted()) {
    return _aborted_gc_id;
  }
  return _g1h->gc_tracer_cm()->gc_id();
}

static void print_ms_time_info(const char* prefix, const char* name,
                               NumberSeq& ns) {
  gclog_or_tty->print_cr("%s%5d %12s: total time = %8.2f s (avg = %8.2f ms).",
                         prefix, ns.num(), name, ns.sum()/1000.0, ns.avg());
  if (ns.num() > 0) {
    gclog_or_tty->print_cr("%s         [std. dev = %8.2f ms, max = %8.2f ms]",
                           prefix, ns.sd(), ns.maximum());
  }
}

void ConcurrentMark::print_summary_info() {
  gclog_or_tty->print_cr(" Concurrent marking:");
  print_ms_time_info("  ", "init marks", _init_times);
  print_ms_time_info("  ", "remarks", _remark_times);
  {
    print_ms_time_info("     ", "final marks", _remark_mark_times);
    print_ms_time_info("     ", "weak refs", _remark_weak_ref_times);

  }
  print_ms_time_info("  ", "cleanups", _cleanup_times);
  gclog_or_tty->print_cr("    Finalize live data total time = %8.2f s (avg = %8.2f ms).",
                         _total_counting_time,
                         (_cleanup_times.num() > 0 ? _total_counting_time * 1000.0 /
                          (double)_cleanup_times.num()
                         : 0.0));

  gclog_or_tty->print_cr("  Total stop_world time = %8.2f s.",
                         (_init_times.sum() + _remark_times.sum() +
                          _cleanup_times.sum())/1000.0);
  gclog_or_tty->print_cr("  Total concurrent time = %8.2f s "
                "(%8.2f s marking).",
                cmThread()->vtime_accum(),
                cmThread()->vtime_mark_accum());
}

void ConcurrentMark::print_worker_threads_on(outputStream* st) const {
  if (use_parallel_marking_threads()) {
    _parallel_workers->print_worker_threads_on(st);
  }
}

void ConcurrentMark::print_on_error(outputStream* st) const {
  st->print_cr("Marking Bits (Prev, Next): (CMBitMap*) " PTR_FORMAT ", (CMBitMap*) " PTR_FORMAT,
      p2i(_prevMarkBitMap), p2i(_nextMarkBitMap));
  _prevMarkBitMap->print_on_error(st, " Prev Bits: ");
  _nextMarkBitMap->print_on_error(st, " Next Bits: ");
}

#ifndef PRODUCT
// for debugging purposes
void ConcurrentMark::print_finger() {
  gclog_or_tty->print_cr("heap [" PTR_FORMAT ", " PTR_FORMAT "), global finger = " PTR_FORMAT,
                         p2i(_heap_start), p2i(_heap_end), p2i(_finger));
  for (uint i = 0; i < _max_worker_id; ++i) {
    gclog_or_tty->print("   %u: " PTR_FORMAT, i, p2i(_tasks[i]->finger()));
  }
  gclog_or_tty->cr();
}
#endif

template<bool scan>
inline void CMTask::process_grey_object(oop obj) {
  assert(scan || obj->is_typeArray(), "Skipping scan of grey non-typeArray");

  if (_cm->verbose_high()) {
    gclog_or_tty->print_cr("[%u] processing grey object " PTR_FORMAT,
                           _worker_id, p2i((void*) obj));
  }

  assert(G1CMObjArrayProcessor::is_array_slice(obj) || _nextMarkBitMap->isMarked((HeapWord*) obj),
         "Any stolen object should be a slice or marked");

  if (scan) {
    if (G1CMObjArrayProcessor::is_array_slice(obj)) {
      _words_scanned += _objArray_processor.process_slice(obj);
    } else if (G1CMObjArrayProcessor::should_be_sliced(obj)) {
      _words_scanned += _objArray_processor.process_obj(obj);
    } else {
      size_t obj_size = obj->size();
      _words_scanned += obj_size;
      obj->oop_iterate(_cm_oop_closure);;
    }
  }
  statsOnly( ++_objs_scanned );
  check_limits();
}

template void CMTask::process_grey_object<true>(oop);
template void CMTask::process_grey_object<false>(oop);

// Closure for iteration over bitmaps
class CMBitMapClosure : public BitMapClosure {
private:
  // the bitmap that is being iterated over
  CMBitMap*                   _nextMarkBitMap;
  ConcurrentMark*             _cm;
  CMTask*                     _task;

public:
  CMBitMapClosure(CMTask *task, ConcurrentMark* cm, CMBitMap* nextMarkBitMap) :
    _task(task), _cm(cm), _nextMarkBitMap(nextMarkBitMap) { }

  bool do_bit(size_t offset) {
    HeapWord* addr = _nextMarkBitMap->offsetToHeapWord(offset);
    assert(_nextMarkBitMap->isMarked(addr), "invariant");
    assert( addr < _cm->finger(), "invariant");

    statsOnly( _task->increase_objs_found_on_bitmap() );
    assert(addr >= _task->finger(), "invariant");

    // We move that task's local finger along.
    _task->move_finger_to(addr);

    _task->scan_object(oop(addr));
    // we only partially drain the local queue and global stack
    _task->drain_local_queue(true);
    _task->drain_global_stack(true);

    // if the has_aborted flag has been raised, we need to bail out of
    // the iteration
    return !_task->has_aborted();
  }
};

G1CMOopClosure::G1CMOopClosure(G1CollectedHeap* g1h,
                               ConcurrentMark* cm,
                               CMTask* task)
  : _g1h(g1h), _cm(cm), _task(task) {
  assert(_ref_processor == NULL, "should be initialized to NULL");

  if (G1UseConcMarkReferenceProcessing) {
    _ref_processor = g1h->ref_processor_cm();
    assert(_ref_processor != NULL, "should not be NULL");
  }
}

void CMTask::setup_for_region(HeapRegion* hr) {
  assert(hr != NULL,
        "claim_region() should have filtered out NULL regions");

  if (_cm->verbose_low()) {
    gclog_or_tty->print_cr("[%u] setting up for region " PTR_FORMAT,
                           _worker_id, p2i(hr));
  }

  _curr_region  = hr;
  _finger       = hr->bottom();
  update_region_limit();
}

void CMTask::update_region_limit() {
  HeapRegion* hr            = _curr_region;
  HeapWord* bottom          = hr->bottom();
  HeapWord* limit           = hr->next_top_at_mark_start();

  if (limit == bottom) {
    if (_cm->verbose_low()) {
      gclog_or_tty->print_cr("[%u] found an empty region "
                             "[" PTR_FORMAT ", " PTR_FORMAT ")",
                             _worker_id, p2i(bottom), p2i(limit));
    }
    // The region was collected underneath our feet.
    // We set the finger to bottom to ensure that the bitmap
    // iteration that will follow this will not do anything.
    // (this is not a condition that holds when we set the region up,
    // as the region is not supposed to be empty in the first place)
    _finger = bottom;
  } else if (limit >= _region_limit) {
    assert(limit >= _finger, "peace of mind");
  } else {
    assert(limit < _region_limit, "only way to get here");
    // This can happen under some pretty unusual circumstances.  An
    // evacuation pause empties the region underneath our feet (NTAMS
    // at bottom). We then do some allocation in the region (NTAMS
    // stays at bottom), followed by the region being used as a GC
    // alloc region (NTAMS will move to top() and the objects
    // originally below it will be grayed). All objects now marked in
    // the region are explicitly grayed, if below the global finger,
    // and we do not need in fact to scan anything else. So, we simply
    // set _finger to be limit to ensure that the bitmap iteration
    // doesn't do anything.
    _finger = limit;
  }

  _region_limit = limit;
}

void CMTask::giveup_current_region() {
  assert(_curr_region != NULL, "invariant");
  if (_cm->verbose_low()) {
    gclog_or_tty->print_cr("[%u] giving up region " PTR_FORMAT,
                           _worker_id, p2i(_curr_region));
  }
  clear_region_fields();
}

void CMTask::clear_region_fields() {
  // Values for these three fields that indicate that we're not
  // holding on to a region.
  _curr_region   = NULL;
  _finger        = NULL;
  _region_limit  = NULL;
}

void CMTask::set_cm_oop_closure(G1CMOopClosure* cm_oop_closure) {
  if (cm_oop_closure == NULL) {
    assert(_cm_oop_closure != NULL, "invariant");
  } else {
    assert(_cm_oop_closure == NULL, "invariant");
  }
  _cm_oop_closure = cm_oop_closure;
}

void CMTask::reset(CMBitMap* nextMarkBitMap) {
  guarantee(nextMarkBitMap != NULL, "invariant");

  if (_cm->verbose_low()) {
    gclog_or_tty->print_cr("[%u] resetting", _worker_id);
  }

  _nextMarkBitMap                = nextMarkBitMap;
  clear_region_fields();

  _calls                         = 0;
  _elapsed_time_ms               = 0.0;
  _termination_time_ms           = 0.0;
  _termination_start_time_ms     = 0.0;
  _mark_stats_cache.reset();


#if _MARKING_STATS_
  _local_pushes                  = 0;
  _local_pops                    = 0;
  _local_max_size                = 0;
  _objs_scanned                  = 0;
  _global_pushes                 = 0;
  _global_pops                   = 0;
  _global_max_size               = 0;
  _global_transfers_to           = 0;
  _global_transfers_from         = 0;
  _regions_claimed               = 0;
  _objs_found_on_bitmap          = 0;
  _satb_buffers_processed        = 0;
  _steal_attempts                = 0;
  _steals                        = 0;
  _aborted                       = 0;
  _aborted_overflow              = 0;
  _aborted_cm_aborted            = 0;
  _aborted_yield                 = 0;
  _aborted_timed_out             = 0;
  _aborted_satb                  = 0;
  _aborted_termination           = 0;
#endif // _MARKING_STATS_
}

bool CMTask::should_exit_termination() {
  if (!regular_clock_call()) {
    return true;
  }
  // This is called when we are in the termination protocol. We should
  // quit if, for some reason, this task wants to abort or the global
  // stack is not empty (this means that we can get work from it).
  return !_cm->mark_stack_empty() || has_aborted();
}

void CMTask::reached_limit() {
  assert(_words_scanned >= _words_scanned_limit ||
         _refs_reached >= _refs_reached_limit ,
         "shouldn't have been called otherwise");
  abort_marking_if_regular_check_fail();
}

bool CMTask::regular_clock_call() {
  if (has_aborted()) {
    return false;
  };

  // First, we need to recalculate the words scanned and refs reached
  // limits for the next clock call.
  recalculate_limits();

  // During the regular clock call we do the following

  // (1) If an overflow has been flagged, then we abort.
  if (_cm->has_overflown()) {
    return false;
  }

  // If we are not concurrent (i.e. we're doing remark) we don't need
  // to check anything else. The other steps are only needed during
  // the concurrent marking phase.
  if (!concurrent()) {
    return true;
}

  // (2) If marking has been aborted for Full GC, then we also abort.
  if (_cm->has_aborted()) {
    statsOnly( ++_aborted_cm_aborted );
    return false;
  }

  double curr_time_ms = os::elapsedVTime() * 1000.0;

  // (3) If marking stats are enabled, then we update the step history.
#if _MARKING_STATS_
  if (_words_scanned >= _words_scanned_limit) {
    ++_clock_due_to_scanning;
  }
  if (_refs_reached >= _refs_reached_limit) {
    ++_clock_due_to_marking;
  }

  double last_interval_ms = curr_time_ms - _interval_start_time_ms;
  _interval_start_time_ms = curr_time_ms;
  _all_clock_intervals_ms.add(last_interval_ms);

  if (_cm->verbose_medium()) {
      gclog_or_tty->print_cr("[%u] regular clock, interval = %1.2lfms, "
                        "scanned = " SIZE_FORMAT "%s, refs reached = " SIZE_FORMAT "%s",
                        _worker_id, last_interval_ms,
                        _words_scanned,
                        (_words_scanned >= _words_scanned_limit) ? " (*)" : "",
                        _refs_reached,
                        (_refs_reached >= _refs_reached_limit) ? " (*)" : "");
  }
#endif // _MARKING_STATS_

  // (4) We check whether we should yield. If we have to, then we abort.
  if (SuspendibleThreadSet::should_yield()) {
    // We should yield. To do this we abort the task. The caller is
    // responsible for yielding.
    statsOnly( ++_aborted_yield );
    return false;
  }

  // (5) We check whether we've reached our time quota. If we have,
  // then we abort.
  double elapsed_time_ms = curr_time_ms - _start_time_ms;
  if (elapsed_time_ms > _time_target_ms) {
    _has_timed_out = true;
    statsOnly( ++_aborted_timed_out );
    return false;
  }

  // (6) Finally, we check whether there are enough completed STAB
  // buffers available for processing. If there are, we abort.
  SATBMarkQueueSet& satb_mq_set = JavaThread::satb_mark_queue_set();
  if (!_draining_satb_buffers && satb_mq_set.process_completed_buffers()) {
    if (_cm->verbose_low()) {
      gclog_or_tty->print_cr("[%u] aborting to deal with pending SATB buffers",
                             _worker_id);
    }
    // we do need to process SATB buffers, we'll abort and restart
    // the marking task to do so
    statsOnly( ++_aborted_satb );
    return false;
  }
  return true;
}

void CMTask::recalculate_limits() {
  _real_words_scanned_limit = _words_scanned + words_scanned_period;
  _words_scanned_limit      = _real_words_scanned_limit;

  _real_refs_reached_limit  = _refs_reached  + refs_reached_period;
  _refs_reached_limit       = _real_refs_reached_limit;
}

void CMTask::decrease_limits() {
  // This is called when we believe that we're going to do an infrequent
  // operation which will increase the per byte scanned cost (i.e. move
  // entries to/from the global stack). It basically tries to decrease the
  // scanning limit so that the clock is called earlier.

  if (_cm->verbose_medium()) {
    gclog_or_tty->print_cr("[%u] decreasing limits", _worker_id);
  }

  _words_scanned_limit = _real_words_scanned_limit -
    3 * words_scanned_period / 4;
  _refs_reached_limit  = _real_refs_reached_limit -
    3 * refs_reached_period / 4;
}

void CMTask::move_entries_to_global_stack() {
  // local array where we'll store the entries that will be popped
  // from the local queue
  oop buffer[global_stack_transfer_size];

  int n = 0;
  oop obj;
  while (n < global_stack_transfer_size && _task_queue->pop_local(obj)) {
    buffer[n] = obj;
    ++n;
  }

  if (n > 0) {
    // we popped at least one entry from the local queue

    statsOnly( ++_global_transfers_to; _local_pops += n );

    if (!_cm->mark_stack_push(buffer, n)) {
      if (_cm->verbose_low()) {
        gclog_or_tty->print_cr("[%u] aborting due to global stack overflow",
                               _worker_id);
      }
      set_has_aborted();
    } else {
      // the transfer was successful

      if (_cm->verbose_medium()) {
        gclog_or_tty->print_cr("[%u] pushed %d entries to the global stack",
                               _worker_id, n);
      }
      statsOnly( int tmp_size = _cm->mark_stack_size();
                 if (tmp_size > _global_max_size) {
                   _global_max_size = tmp_size;
                 }
                 _global_pushes += n );
    }
  }

  // this operation was quite expensive, so decrease the limits
  decrease_limits();
}

void CMTask::get_entries_from_global_stack() {
  // local array where we'll store the entries that will be popped
  // from the global stack.
  oop buffer[global_stack_transfer_size];
  int n;
  _cm->mark_stack_pop(buffer, global_stack_transfer_size, &n);
  assert(n <= global_stack_transfer_size,
         "we should not pop more than the given limit");
  if (n > 0) {
    // yes, we did actually pop at least one entry

    statsOnly( ++_global_transfers_from; _global_pops += n );
    if (_cm->verbose_medium()) {
      gclog_or_tty->print_cr("[%u] popped %d entries from the global stack",
                             _worker_id, n);
    }
    for (int i = 0; i < n; ++i) {
      assert(G1CMObjArrayProcessor::is_array_slice(buffer[i]) || buffer[i]->is_oop(),
             err_msg("Element " PTR_FORMAT " must be an array slice or oop", p2i(buffer[i])));
      bool success = _task_queue->push(buffer[i]);
      // We only call this when the local queue is empty or under a
      // given target limit. So, we do not expect this push to fail.
      assert(success, "invariant");
    }

    statsOnly( int tmp_size = _task_queue->size();
               if (tmp_size > _local_max_size) {
                 _local_max_size = tmp_size;
               }
               _local_pushes += n );
  }

  // this operation was quite expensive, so decrease the limits
  decrease_limits();
}

void CMTask::drain_local_queue(bool partially) {
  if (has_aborted()) {
    return;
  }

  // Decide what the target size is, depending whether we're going to
  // drain it partially (so that other tasks can steal if they run out
  // of things to do) or totally (at the very end).
  size_t target_size;
  if (partially) {
    target_size = MIN2((size_t)_task_queue->max_elems()/3, GCDrainStackTargetSize);
  } else {
    target_size = 0;
  }

  if (_task_queue->size() > target_size) {
    if (_cm->verbose_high()) {
      gclog_or_tty->print_cr("[%u] draining local queue, target size = " SIZE_FORMAT,
                             _worker_id, target_size);
    }

    oop obj;
    bool ret = _task_queue->pop_local(obj);
    while (ret) {
      statsOnly( ++_local_pops );

      if (_cm->verbose_high()) {
        gclog_or_tty->print_cr("[%u] popped " PTR_FORMAT, _worker_id,
                               p2i((void*) obj));
      }

      scan_object(obj);

      if (_task_queue->size() <= target_size || has_aborted()) {
        ret = false;
      } else {
        ret = _task_queue->pop_local(obj);
      }
    }

    if (_cm->verbose_high()) {
      gclog_or_tty->print_cr("[%u] drained local queue, size = %d",
                             _worker_id, _task_queue->size());
    }
  }
}

void CMTask::drain_global_stack(bool partially) {
  if (has_aborted()) return;

  // We have a policy to drain the local queue before we attempt to
  // drain the global stack.
  assert(partially || _task_queue->size() == 0, "invariant");

  // Decide what the target size is, depending whether we're going to
  // drain it partially (so that other tasks can steal if they run out
  // of things to do) or totally (at the very end).  Notice that,
  // because we move entries from the global stack in chunks or
  // because another task might be doing the same, we might in fact
  // drop below the target. But, this is not a problem.
  size_t target_size;
  if (partially) {
    target_size = _cm->partial_mark_stack_size_target();
  } else {
    target_size = 0;
  }

  if (_cm->mark_stack_size() > target_size) {
    if (_cm->verbose_low()) {
      gclog_or_tty->print_cr("[%u] draining global_stack, target size " SIZE_FORMAT,
                             _worker_id, target_size);
    }

    while (!has_aborted() && _cm->mark_stack_size() > target_size) {
      get_entries_from_global_stack();
      drain_local_queue(partially);
    }

    if (_cm->verbose_low()) {
      gclog_or_tty->print_cr("[%u] drained global stack, size = " SIZE_FORMAT,
                             _worker_id, _cm->mark_stack_size());
    }
  }
}

// SATB Queue has several assumptions on whether to call the par or
// non-par versions of the methods. this is why some of the code is
// replicated. We should really get rid of the single-threaded version
// of the code to simplify things.
void CMTask::drain_satb_buffers() {
  if (has_aborted()) return;

  // We set this so that the regular clock knows that we're in the
  // middle of draining buffers and doesn't set the abort flag when it
  // notices that SATB buffers are available for draining. It'd be
  // very counter productive if it did that. :-)
  _draining_satb_buffers = true;

  CMSATBBufferClosure satb_cl(this, _g1h);
  SATBMarkQueueSet& satb_mq_set = JavaThread::satb_mark_queue_set();

  // This keeps claiming and applying the closure to completed buffers
  // until we run out of buffers or we need to abort.
  while (!has_aborted() &&
         satb_mq_set.apply_closure_to_completed_buffer(&satb_cl)) {
    if (_cm->verbose_medium()) {
      gclog_or_tty->print_cr("[%u] processed an SATB buffer", _worker_id);
    }
    statsOnly( ++_satb_buffers_processed );
    abort_marking_if_regular_check_fail();
  }

  _draining_satb_buffers = false;

  assert(has_aborted() ||
         concurrent() ||
         satb_mq_set.completed_buffers_num() == 0, "invariant");

  // again, this was a potentially expensive operation, decrease the
  // limits to get the regular clock call early
  decrease_limits();
}

void CMTask::clear_mark_stats_cache(uint region_idx) {
  _mark_stats_cache.reset(region_idx);
}

Pair<size_t, size_t> CMTask::flush_mark_stats_cache() {
  return _mark_stats_cache.evict_all();
}

void CMTask::print_stats() {
  gclog_or_tty->print_cr("Marking Stats, task = %u, calls = %d",
                         _worker_id, _calls);
  gclog_or_tty->print_cr("  Elapsed time = %1.2lfms, Termination time = %1.2lfms",
                         _elapsed_time_ms, _termination_time_ms);
  gclog_or_tty->print_cr("  Step Times (cum): num = %d, avg = %1.2lfms, sd = %1.2lfms",
                         _step_times_ms.num(), _step_times_ms.avg(),
                         _step_times_ms.sd());
  gclog_or_tty->print_cr("                    max = %1.2lfms, total = %1.2lfms",
                         _step_times_ms.maximum(), _step_times_ms.sum());
  size_t const hits = _mark_stats_cache.hits();
  size_t const misses = _mark_stats_cache.misses();
  gclog_or_tty->print_cr("  Mark Stats Cache: hits " SIZE_FORMAT " misses " SIZE_FORMAT " ratio %.3f",
                         hits, misses,
                         hits + misses != 0 ? double(hits) / (hits + misses) * 100.0 : 0.0);
#if _MARKING_STATS_
  gclog_or_tty->print_cr("  Clock Intervals (cum): num = %d, avg = %1.2lfms, sd = %1.2lfms",
                         _all_clock_intervals_ms.num(), _all_clock_intervals_ms.avg(),
                         _all_clock_intervals_ms.sd());
  gclog_or_tty->print_cr("                         max = %1.2lfms, total = %1.2lfms",
                         _all_clock_intervals_ms.maximum(),
                         _all_clock_intervals_ms.sum());
  gclog_or_tty->print_cr("  Clock Causes (cum): scanning = %d, marking = %d",
                         _clock_due_to_scanning, _clock_due_to_marking);
  gclog_or_tty->print_cr("  Objects: scanned = %d, found on the bitmap = %d",
                         _objs_scanned, _objs_found_on_bitmap);
  gclog_or_tty->print_cr("  Local Queue:  pushes = %d, pops = %d, max size = %d",
                         _local_pushes, _local_pops, _local_max_size);
  gclog_or_tty->print_cr("  Global Stack: pushes = %d, pops = %d, max size = %d",
                         _global_pushes, _global_pops, _global_max_size);
  gclog_or_tty->print_cr("                transfers to = %d, transfers from = %d",
                         _global_transfers_to,_global_transfers_from);
  gclog_or_tty->print_cr("  Regions: claimed = %d", _regions_claimed);
  gclog_or_tty->print_cr("  SATB buffers: processed = %d", _satb_buffers_processed);
  gclog_or_tty->print_cr("  Steals: attempts = %d, successes = %d",
                         _steal_attempts, _steals);
  gclog_or_tty->print_cr("  Aborted: %d, due to", _aborted);
  gclog_or_tty->print_cr("    overflow: %d, global abort: %d, yield: %d",
                         _aborted_overflow, _aborted_cm_aborted, _aborted_yield);
  gclog_or_tty->print_cr("    time out: %d, SATB: %d, termination: %d",
                         _aborted_timed_out, _aborted_satb, _aborted_termination);
#endif // _MARKING_STATS_
}

/*****************************************************************************

    The do_marking_step(time_target_ms, ...) method is the building
    block of the parallel marking framework. It can be called in parallel
    with other invocations of do_marking_step() on different tasks
    (but only one per task, obviously) and concurrently with the
    mutator threads, or during remark, hence it eliminates the need
    for two versions of the code. When called during remark, it will
    pick up from where the task left off during the concurrent marking
    phase. Interestingly, tasks are also claimable during evacuation
    pauses too, since do_marking_step() ensures that it aborts before
    it needs to yield.

    The data structures that it uses to do marking work are the
    following:

      (1) Marking Bitmap. If there are gray objects that appear only
      on the bitmap (this happens either when dealing with an overflow
      or when the initial marking phase has simply marked the roots
      and didn't push them on the stack), then tasks claim heap
      regions whose bitmap they then scan to find gray objects. A
      global finger indicates where the end of the last claimed region
      is. A local finger indicates how far into the region a task has
      scanned. The two fingers are used to determine how to gray an
      object (i.e. whether simply marking it is OK, as it will be
      visited by a task in the future, or whether it needs to be also
      pushed on a stack).

      (2) Local Queue. The local queue of the task which is accessed
      reasonably efficiently by the task. Other tasks can steal from
      it when they run out of work. Throughout the marking phase, a
      task attempts to keep its local queue short but not totally
      empty, so that entries are available for stealing by other
      tasks. Only when there is no more work, a task will totally
      drain its local queue.

      (3) Global Mark Stack. This handles local queue overflow. During
      marking only sets of entries are moved between it and the local
      queues, as access to it requires a mutex and more fine-grain
      interaction with it which might cause contention. If it
      overflows, then the marking phase should restart and iterate
      over the bitmap to identify gray objects. Throughout the marking
      phase, tasks attempt to keep the global mark stack at a small
      length but not totally empty, so that entries are available for
      popping by other tasks. Only when there is no more work, tasks
      will totally drain the global mark stack.

      (4) SATB Buffer Queue. This is where completed SATB buffers are
      made available. Buffers are regularly removed from this queue
      and scanned for roots, so that the queue doesn't get too
      long. During remark, all completed buffers are processed, as
      well as the filled in parts of any uncompleted buffers.

    The do_marking_step() method tries to abort when the time target
    has been reached. There are a few other cases when the
    do_marking_step() method also aborts:

      (1) When the marking phase has been aborted (after a Full GC).

      (2) When a global overflow (on the global stack) has been
      triggered. Before the task aborts, it will actually sync up with
      the other tasks to ensure that all the marking data structures
      (local queues, stacks, fingers etc.)  are re-initialized so that
      when do_marking_step() completes, the marking phase can
      immediately restart.

      (3) When enough completed SATB buffers are available. The
      do_marking_step() method only tries to drain SATB buffers right
      at the beginning. So, if enough buffers are available, the
      marking step aborts and the SATB buffers are processed at
      the beginning of the next invocation.

      (4) To yield. when we have to yield then we abort and yield
      right at the end of do_marking_step(). This saves us from a lot
      of hassle as, by yielding we might allow a Full GC. If this
      happens then objects will be compacted underneath our feet, the
      heap might shrink, etc. We save checking for this by just
      aborting and doing the yield right at the end.

    From the above it follows that the do_marking_step() method should
    be called in a loop (or, otherwise, regularly) until it completes.

    If a marking step completes without its has_aborted() flag being
    true, it means it has completed the current marking phase (and
    also all other marking tasks have done so and have all synced up).

    A method called regular_clock_call() is invoked "regularly" (in
    sub ms intervals) throughout marking. It is this clock method that
    checks all the abort conditions which were mentioned above and
    decides when the task should abort. A work-based scheme is used to
    trigger this clock method: when the number of object words the
    marking phase has scanned or the number of references the marking
    phase has visited reach a given limit. Additional invocations to
    the method clock have been planted in a few other strategic places
    too. The initial reason for the clock method was to avoid calling
    vtime too regularly, as it is quite expensive. So, once it was in
    place, it was natural to piggy-back all the other conditions on it
    too and not constantly check them throughout the code.

    If do_termination is true then do_marking_step will enter its
    termination protocol.

    The value of is_serial must be true when do_marking_step is being
    called serially (i.e. by the VMThread) and do_marking_step should
    skip any synchronization in the termination and overflow code.
    Examples include the serial remark code and the serial reference
    processing closures.

    The value of is_serial must be false when do_marking_step is
    being called by any of the worker threads in a work gang.
    Examples include the concurrent marking code (CMMarkingTask),
    the MT remark code, and the MT reference processing closures.

 *****************************************************************************/

void CMTask::do_marking_step(double time_target_ms,
                             bool do_termination,
                             bool is_serial) {
  assert(time_target_ms >= 1.0, "minimum granularity is 1ms");
  assert(concurrent() == _cm->concurrent(), "they should be the same");

  G1CollectorPolicy* g1_policy = _g1h->g1_policy();
  assert(_task_queues != NULL, "invariant");
  assert(_task_queue != NULL, "invariant");
  assert(_task_queues->queue(_worker_id) == _task_queue, "invariant");

  assert(!_claimed,
         "only one thread should claim this task at any one time");

  // OK, this doesn't safeguard again all possible scenarios, as it is
  // possible for two threads to set the _claimed flag at the same
  // time. But it is only for debugging purposes anyway and it will
  // catch most problems.
  _claimed = true;

  _start_time_ms = os::elapsedVTime() * 1000.0;
  statsOnly( _interval_start_time_ms = _start_time_ms );

  // If do_stealing is true then do_marking_step will attempt to
  // steal work from the other CMTasks. It only makes sense to
  // enable stealing when the termination protocol is enabled
  // and do_marking_step() is not being called serially.
  bool do_stealing = do_termination && !is_serial;

  double diff_prediction_ms =
    g1_policy->get_new_prediction(&_marking_step_diffs_ms);
  _time_target_ms = time_target_ms - diff_prediction_ms;

  // set up the variables that are used in the work-based scheme to
  // call the regular clock method
  _words_scanned = 0;
  _refs_reached  = 0;
  recalculate_limits();

  // clear all flags
  clear_has_aborted();
  _has_timed_out = false;
  _draining_satb_buffers = false;

  ++_calls;

  if (_cm->verbose_low()) {
    gclog_or_tty->print_cr("[%u] >>>>>>>>>> START, call = %d, "
                           "target = %1.2lfms >>>>>>>>>>",
                           _worker_id, _calls, _time_target_ms);
  }

  // Set up the bitmap and oop closures. Anything that uses them is
  // eventually called from this method, so it is OK to allocate these
  // statically.
  CMBitMapClosure bitmap_closure(this, _cm, _nextMarkBitMap);
  G1CMOopClosure  cm_oop_closure(_g1h, _cm, this);
  set_cm_oop_closure(&cm_oop_closure);

  if (_cm->has_overflown()) {
    // This can happen if the mark stack overflows during a GC pause
    // and this task, after a yield point, restarts. We have to abort
    // as we need to get into the overflow protocol which happens
    // right at the end of this task.
    set_has_aborted();
  }

  // First drain any available SATB buffers. After this, we will not
  // look at SATB buffers before the next invocation of this method.
  // If enough completed SATB buffers are queued up, the regular clock
  // will abort this task so that it restarts.
  drain_satb_buffers();
  // ...then partially drain the local queue and the global stack
  drain_local_queue(true);
  drain_global_stack(true);

  do {
    if (!has_aborted() && _curr_region != NULL) {
      // This means that we're already holding on to a region.
      assert(_finger != NULL, "if region is not NULL, then the finger "
             "should not be NULL either");

      // We might have restarted this task after an evacuation pause
      // which might have evacuated the region we're holding on to
      // underneath our feet. Let's read its limit again to make sure
      // that we do not iterate over a region of the heap that
      // contains garbage (update_region_limit() will also move
      // _finger to the start of the region if it is found empty).
      update_region_limit();
      // We will start from _finger not from the start of the region,
      // as we might be restarting this task after aborting half-way
      // through scanning this region. In this case, _finger points to
      // the address where we last found a marked object. If this is a
      // fresh region, _finger points to start().
      MemRegion mr = MemRegion(_finger, _region_limit);

      if (_cm->verbose_low()) {
        gclog_or_tty->print_cr("[%u] we're scanning part "
                               "[" PTR_FORMAT ", " PTR_FORMAT ") "
                               "of region " HR_FORMAT,
                               _worker_id, p2i(_finger), p2i(_region_limit),
                               HR_FORMAT_PARAMS(_curr_region));
      }

      assert(!_curr_region->isHumongous() || mr.start() == _curr_region->bottom(),
             "humongous regions should go around loop once only");

      // Some special cases:
      // If the memory region is empty, we can just give up the region.
      // If the current region is humongous then we only need to check
      // the bitmap for the bit associated with the start of the object,
      // scan the object if it's live, and give up the region.
      // Otherwise, let's iterate over the bitmap of the part of the region
      // that is left.
      // If the iteration is successful, give up the region.
      if (mr.is_empty()) {
        giveup_current_region();
        abort_marking_if_regular_check_fail();
      } else if (_curr_region->isHumongous() && mr.start() == _curr_region->bottom()) {
        if (_nextMarkBitMap->isMarked(mr.start())) {
          // The object is marked - apply the closure
          BitMap::idx_t offset = _nextMarkBitMap->heapWordToOffset(mr.start());
          bitmap_closure.do_bit(offset);
        }
        // Even if this task aborted while scanning the humongous object
        // we can (and should) give up the current region.
        giveup_current_region();
        abort_marking_if_regular_check_fail();
      } else if (_nextMarkBitMap->iterate(&bitmap_closure, mr)) {
        giveup_current_region();
        abort_marking_if_regular_check_fail();
      } else {
        assert(has_aborted(), "currently the only way to do so");
        // The only way to abort the bitmap iteration is to return
        // false from the do_bit() method. However, inside the
        // do_bit() method we move the _finger to point to the
        // object currently being looked at. So, if we bail out, we
        // have definitely set _finger to something non-null.
        assert(_finger != NULL, "invariant");

        // Region iteration was actually aborted. So now _finger
        // points to the address of the object we last scanned. If we
        // leave it there, when we restart this task, we will rescan
        // the object. It is easy to avoid this. We move the finger by
        // enough to point to the next possible object header (the
        // bitmap knows by how much we need to move it as it knows its
        // granularity).
        assert(_finger < _region_limit, "invariant");
        HeapWord* new_finger = _nextMarkBitMap->nextObject(_finger);
        // Check if bitmap iteration was aborted while scanning the last object
        if (new_finger >= _region_limit) {
          giveup_current_region();
        } else {
          move_finger_to(new_finger);
        }
      }
    }
    // At this point we have either completed iterating over the
    // region we were holding on to, or we have aborted.

    // We then partially drain the local queue and the global stack.
    // (Do we really need this?)
    drain_local_queue(true);
    drain_global_stack(true);

    // Read the note on the claim_region() method on why it might
    // return NULL with potentially more regions available for
    // claiming and why we have to check out_of_regions() to determine
    // whether we're done or not.
    while (!has_aborted() && _curr_region == NULL && !_cm->out_of_regions()) {
      // We are going to try to claim a new region. We should have
      // given up on the previous one.
      // Separated the asserts so that we know which one fires.
      assert(_curr_region  == NULL, "invariant");
      assert(_finger       == NULL, "invariant");
      assert(_region_limit == NULL, "invariant");
      if (_cm->verbose_low()) {
        gclog_or_tty->print_cr("[%u] trying to claim a new region", _worker_id);
      }
      HeapRegion* claimed_region = _cm->claim_region(_worker_id);
      if (claimed_region != NULL) {
        // Yes, we managed to claim one
        statsOnly( ++_regions_claimed );

        if (_cm->verbose_low()) {
          gclog_or_tty->print_cr("[%u] we successfully claimed "
                                 "region " PTR_FORMAT,
                                 _worker_id, p2i(claimed_region));
        }

        setup_for_region(claimed_region);
        assert(_curr_region == claimed_region, "invariant");
      }
      // It is important to call the regular clock here. It might take
      // a while to claim a region if, for example, we hit a large
      // block of empty regions. So we need to call the regular clock
      // method once round the loop to make sure it's called
      // frequently enough.
      abort_marking_if_regular_check_fail();
    }

    if (!has_aborted() && _curr_region == NULL) {
      assert(_cm->out_of_regions(),
             "at this point we should be out of regions");
    }
  } while ( _curr_region != NULL && !has_aborted());

  if (!has_aborted()) {
    // We cannot check whether the global stack is empty, since other
    // tasks might be pushing objects to it concurrently.
    assert(_cm->out_of_regions(),
           "at this point we should be out of regions");

    if (_cm->verbose_low()) {
      gclog_or_tty->print_cr("[%u] all regions claimed", _worker_id);
    }

    // Try to reduce the number of available SATB buffers so that
    // remark has less work to do.
    drain_satb_buffers();
  }

  // Since we've done everything else, we can now totally drain the
  // local queue and global stack.
  drain_local_queue(false);
  drain_global_stack(false);

  // Attempt at work stealing from other task's queues.
  if (do_stealing && !has_aborted()) {
    // We have not aborted. This means that we have finished all that
    // we could. Let's try to do some stealing...

    // We cannot check whether the global stack is empty, since other
    // tasks might be pushing objects to it concurrently.
    assert(_cm->out_of_regions() && _task_queue->size() == 0,
           "only way to reach here");

    if (_cm->verbose_low()) {
      gclog_or_tty->print_cr("[%u] starting to steal", _worker_id);
    }

    while (!has_aborted()) {
      oop obj;
      statsOnly( ++_steal_attempts );

      if (_cm->try_stealing(_worker_id, obj)) {
        if (_cm->verbose_medium()) {
          gclog_or_tty->print_cr("[%u] stolen " PTR_FORMAT " successfully",
                                 _worker_id, p2i((void*) obj));
        }

        statsOnly( ++_steals );

        scan_object(obj);

        // And since we're towards the end, let's totally drain the
        // local queue and global stack.
        drain_local_queue(false);
        drain_global_stack(false);
      } else {
        break;
      }
    }
  }

  // If we are about to wrap up and go into termination, check if we
  // should raise the overflow flag.
  if (do_termination && !has_aborted()) {
    if (_cm->force_overflow()->should_force()) {
      _cm->set_has_overflown();
      regular_clock_call();
    }
  }

  // We still haven't aborted. Now, let's try to get into the
  // termination protocol.
  if (do_termination && !has_aborted()) {
    // We cannot check whether the global stack is empty, since other
    // tasks might be concurrently pushing objects on it.
    // Separated the asserts so that we know which one fires.
    assert(_cm->out_of_regions(), "only way to reach here");
    assert(_task_queue->size() == 0, "only way to reach here");

    if (_cm->verbose_low()) {
      gclog_or_tty->print_cr("[%u] starting termination protocol", _worker_id);
    }

    _termination_start_time_ms = os::elapsedVTime() * 1000.0;

    // The CMTask class also extends the TerminatorTerminator class,
    // hence its should_exit_termination() method will also decide
    // whether to exit the termination protocol or not.
    bool finished = (is_serial ||
                     _cm->terminator()->offer_termination(this));
    double termination_end_time_ms = os::elapsedVTime() * 1000.0;
    _termination_time_ms +=
      termination_end_time_ms - _termination_start_time_ms;

    if (finished) {
      // We're all done.

      if (_worker_id == 0) {
        // let's allow task 0 to do this
        if (concurrent()) {
          assert(_cm->concurrent_marking_in_progress(), "invariant");
          // we need to set this to false before the next
          // safepoint. This way we ensure that the marking phase
          // doesn't observe any more heap expansions.
          _cm->clear_concurrent_marking_in_progress();
        }
      }

      // We can now guarantee that the global stack is empty, since
      // all other tasks have finished. We separated the guarantees so
      // that, if a condition is false, we can immediately find out
      // which one.
      guarantee(_cm->out_of_regions(), "only way to reach here");
      guarantee(_cm->mark_stack_empty(), "only way to reach here");
      guarantee(_task_queue->size() == 0, "only way to reach here");
      guarantee(!_cm->has_overflown(), "only way to reach here");
      guarantee(!_cm->mark_stack_overflow(), "only way to reach here");
      guarantee(!has_aborted(), "should never happen if termination has completed");

      if (_cm->verbose_low()) {
        gclog_or_tty->print_cr("[%u] all tasks terminated", _worker_id);
      }
    } else {
      // Apparently there's more work to do. Let's abort this task. It
      // will restart it and we can hopefully find more things to do.

      if (_cm->verbose_low()) {
        gclog_or_tty->print_cr("[%u] apparently there is more work to do",
                               _worker_id);
      }

      set_has_aborted();
      statsOnly( ++_aborted_termination );
    }
  }

  // Mainly for debugging purposes to make sure that a pointer to the
  // closure which was statically allocated in this frame doesn't
  // escape it by accident.
  set_cm_oop_closure(NULL);
  double end_time_ms = os::elapsedVTime() * 1000.0;
  double elapsed_time_ms = end_time_ms - _start_time_ms;
  // Update the step history.
  _step_times_ms.add(elapsed_time_ms);

  if (has_aborted()) {
    // The task was aborted for some reason.

    statsOnly( ++_aborted );

    if (_has_timed_out) {
      double diff_ms = elapsed_time_ms - _time_target_ms;
      // Keep statistics of how well we did with respect to hitting
      // our target only if we actually timed out (if we aborted for
      // other reasons, then the results might get skewed).
      _marking_step_diffs_ms.add(diff_ms);
    }

    if (_cm->has_overflown()) {
      // This is the interesting one. We aborted because a global
      // overflow was raised. This means we have to restart the
      // marking phase and start iterating over regions. However, in
      // order to do this we have to make sure that all tasks stop
      // what they are doing and re-initialise in a safe manner. We
      // will achieve this with the use of two barrier sync points.

      if (_cm->verbose_low()) {
        gclog_or_tty->print_cr("[%u] detected overflow", _worker_id);
      }

      if (!is_serial) {
        // We only need to enter the sync barrier if being called
        // from a parallel context
        _cm->enter_first_sync_barrier(_worker_id);

        // When we exit this sync barrier we know that all tasks have
        // stopped doing marking work. So, it's now safe to
        // re-initialise our data structures.
      }

      statsOnly( ++_aborted_overflow );

      // We clear the local state of this task...
      clear_region_fields();
      flush_mark_stats_cache();

      if (!is_serial) {
        // If we're executing the concurrent phase of marking, reset the marking
        // state; otherwise the marking state is reset after reference processing,
        // during the remark pause.
        // If we reset here as a result of an overflow during the remark we will
        // see assertion failures from any subsequent set_concurrency_and_phase()
        // calls.
        if (_cm->concurrent() && _worker_id == 0) {
          // Worker 0 is responsible for clearing the global data structures because
          // of an overflow. During STW we should not clear the overflow flag (in
          // G1ConcurrentMark::reset_marking_state()) since we rely on it being true when we exit
          // method to abort the pause and restart concurrent marking.
          _cm->reset_marking_state();
          _cm->force_overflow()->update();

          if (G1Log::finer()) {
            gclog_or_tty->print_cr("Concurrent Mark reset for overflow");
          }
        }
        // ...and enter the second barrier.
        _cm->enter_second_sync_barrier(_worker_id);
      }
      // At this point, if we're during the concurrent phase of
      // marking, everything has been re-initialized and we're
      // ready to restart.
    }

    if (_cm->verbose_low()) {
      gclog_or_tty->print_cr("[%u] <<<<<<<<<< ABORTING, target = %1.2lfms, "
                             "elapsed = %1.2lfms <<<<<<<<<<",
                             _worker_id, _time_target_ms, elapsed_time_ms);
      if (_cm->has_aborted()) {
        gclog_or_tty->print_cr("[%u] ========== MARKING ABORTED ==========",
                               _worker_id);
      }
    }
  } else {
    if (_cm->verbose_low()) {
      gclog_or_tty->print_cr("[%u] <<<<<<<<<< FINISHED, target = %1.2lfms, "
                             "elapsed = %1.2lfms <<<<<<<<<<",
                             _worker_id, _time_target_ms, elapsed_time_ms);
    }
  }

  _claimed = false;
}

CMTask::CMTask(uint worker_id,
               ConcurrentMark* cm,
               CMTaskQueue* task_queue,
               CMTaskQueueSet* task_queues,
               G1RegionMarkStats* mark_stats,
               uint max_regions)
  : _g1h(G1CollectedHeap::heap()),
    _worker_id(worker_id), _cm(cm),
    _objArray_processor(this),
    _claimed(false),
    _nextMarkBitMap(NULL),
    _task_queue(task_queue),
    _mark_stats_cache(mark_stats, max_regions, RegionMarkStatsCacheSize),
    _task_queues(task_queues),
    _cm_oop_closure(NULL) {
  guarantee(task_queue != NULL, "invariant");
  guarantee(task_queues != NULL, "invariant");

  statsOnly( _clock_due_to_scanning = 0;
             _clock_due_to_marking  = 0 );

  _marking_step_diffs_ms.add(0.5);
}

// These are formatting macros that are used below to ensure
// consistent formatting. The *_H_* versions are used to format the
// header for a particular value and they should be kept consistent
// with the corresponding macro. Also note that most of the macros add
// the necessary white space (as a prefix) which makes them a bit
// easier to compose.

// All the output lines are prefixed with this string to be able to
// identify them easily in a large log file.
#define G1PPRL_LINE_PREFIX            "###"

#define G1PPRL_ADDR_BASE_FORMAT    " " PTR_FORMAT "-" PTR_FORMAT
#ifdef _LP64
#define G1PPRL_ADDR_BASE_H_FORMAT  " %37s"
#else // _LP64
#define G1PPRL_ADDR_BASE_H_FORMAT  " %21s"
#endif // _LP64

// For per-region info
#define G1PPRL_TYPE_FORMAT            "   %-4s"
#define G1PPRL_TYPE_H_FORMAT          "   %4s"
#define G1PPRL_BYTE_FORMAT            "  " SIZE_FORMAT_W(9)
#define G1PPRL_BYTE_H_FORMAT          "  %9s"
#define G1PPRL_DOUBLE_FORMAT          "  %14.1f"
#define G1PPRL_DOUBLE_H_FORMAT        "  %14s"

// For summary info
#define G1PPRL_SUM_ADDR_FORMAT(tag)    "  " tag ":" G1PPRL_ADDR_BASE_FORMAT
#define G1PPRL_SUM_BYTE_FORMAT(tag)    "  " tag ": " SIZE_FORMAT
#define G1PPRL_SUM_MB_FORMAT(tag)      "  " tag ": %1.2f MB"
#define G1PPRL_SUM_MB_PERC_FORMAT(tag) G1PPRL_SUM_MB_FORMAT(tag) " / %1.2f %%"

G1PrintRegionLivenessInfoClosure::
G1PrintRegionLivenessInfoClosure(outputStream* out, const char* phase_name)
  : _out(out),
    _total_used_bytes(0), _total_capacity_bytes(0),
    _total_prev_live_bytes(0), _total_next_live_bytes(0),
    _hum_used_bytes(0), _hum_capacity_bytes(0),
    _hum_prev_live_bytes(0), _hum_next_live_bytes(0),
    _total_remset_bytes(0), _total_strong_code_roots_bytes(0) {
  G1CollectedHeap* g1h = G1CollectedHeap::heap();
  MemRegion g1_reserved = g1h->g1_reserved();
  double now = os::elapsedTime();

  // Print the header of the output.
  _out->cr();
  _out->print_cr(G1PPRL_LINE_PREFIX" PHASE %s @ %1.3f", phase_name, now);
  _out->print_cr(G1PPRL_LINE_PREFIX" HEAP"
                 G1PPRL_SUM_ADDR_FORMAT("reserved")
                 G1PPRL_SUM_BYTE_FORMAT("region-size"),
                 p2i(g1_reserved.start()), p2i(g1_reserved.end()),
                 HeapRegion::GrainBytes);
  _out->print_cr(G1PPRL_LINE_PREFIX);
  _out->print_cr(G1PPRL_LINE_PREFIX
                G1PPRL_TYPE_H_FORMAT
                G1PPRL_ADDR_BASE_H_FORMAT
                G1PPRL_BYTE_H_FORMAT
                G1PPRL_BYTE_H_FORMAT
                G1PPRL_BYTE_H_FORMAT
                G1PPRL_DOUBLE_H_FORMAT
                G1PPRL_BYTE_H_FORMAT
                G1PPRL_BYTE_H_FORMAT,
                "type", "address-range",
                "used", "prev-live", "next-live", "gc-eff",
                "remset", "code-roots");
  _out->print_cr(G1PPRL_LINE_PREFIX
                G1PPRL_TYPE_H_FORMAT
                G1PPRL_ADDR_BASE_H_FORMAT
                G1PPRL_BYTE_H_FORMAT
                G1PPRL_BYTE_H_FORMAT
                G1PPRL_BYTE_H_FORMAT
                G1PPRL_DOUBLE_H_FORMAT
                G1PPRL_BYTE_H_FORMAT
                G1PPRL_BYTE_H_FORMAT,
                "", "",
                "(bytes)", "(bytes)", "(bytes)", "(bytes/ms)",
                "(bytes)", "(bytes)");
}

// It takes as a parameter a reference to one of the _hum_* fields, it
// deduces the corresponding value for a region in a humongous region
// series (either the region size, or what's left if the _hum_* field
// is < the region size), and updates the _hum_* field accordingly.
size_t G1PrintRegionLivenessInfoClosure::get_hum_bytes(size_t* hum_bytes) {
  size_t bytes = 0;
  // The > 0 check is to deal with the prev and next live bytes which
  // could be 0.
  if (*hum_bytes > 0) {
    bytes = MIN2(HeapRegion::GrainBytes, *hum_bytes);
    *hum_bytes -= bytes;
  }
  return bytes;
}

// It deduces the values for a region in a humongous region series
// from the _hum_* fields and updates those accordingly. It assumes
// that that _hum_* fields have already been set up from the "starts
// humongous" region and we visit the regions in address order.
void G1PrintRegionLivenessInfoClosure::get_hum_bytes(size_t* used_bytes,
                                                     size_t* capacity_bytes,
                                                     size_t* prev_live_bytes,
                                                     size_t* next_live_bytes) {
  assert(_hum_used_bytes > 0 && _hum_capacity_bytes > 0, "pre-condition");
  *used_bytes      = get_hum_bytes(&_hum_used_bytes);
  *capacity_bytes  = get_hum_bytes(&_hum_capacity_bytes);
  *prev_live_bytes = get_hum_bytes(&_hum_prev_live_bytes);
  *next_live_bytes = get_hum_bytes(&_hum_next_live_bytes);
}

bool G1PrintRegionLivenessInfoClosure::doHeapRegion(HeapRegion* r) {
  const char* type       = r->get_type_str();
  HeapWord* bottom       = r->bottom();
  HeapWord* end          = r->end();
  size_t capacity_bytes  = r->capacity();
  size_t used_bytes      = r->used();
  size_t prev_live_bytes = r->live_bytes();
  size_t next_live_bytes = r->next_live_bytes();
  double gc_eff          = r->gc_efficiency();
  size_t remset_bytes    = r->rem_set()->mem_size();
  size_t strong_code_roots_bytes = r->rem_set()->strong_code_roots_mem_size();

  if (r->startsHumongous()) {
    assert(_hum_used_bytes == 0 && _hum_capacity_bytes == 0 &&
           _hum_prev_live_bytes == 0 && _hum_next_live_bytes == 0,
           "they should have been zeroed after the last time we used them");
    // Set up the _hum_* fields.
    _hum_capacity_bytes  = capacity_bytes;
    _hum_used_bytes      = used_bytes;
    _hum_prev_live_bytes = prev_live_bytes;
    _hum_next_live_bytes = next_live_bytes;
    get_hum_bytes(&used_bytes, &capacity_bytes,
                  &prev_live_bytes, &next_live_bytes);
    end = bottom + HeapRegion::GrainWords;
  } else if (r->continuesHumongous()) {
    get_hum_bytes(&used_bytes, &capacity_bytes,
                  &prev_live_bytes, &next_live_bytes);
    assert(end == bottom + HeapRegion::GrainWords, "invariant");
  }

  _total_used_bytes      += used_bytes;
  _total_capacity_bytes  += capacity_bytes;
  _total_prev_live_bytes += prev_live_bytes;
  _total_next_live_bytes += next_live_bytes;
  _total_remset_bytes    += remset_bytes;
  _total_strong_code_roots_bytes += strong_code_roots_bytes;

  // Print a line for this particular region.
  _out->print_cr(G1PPRL_LINE_PREFIX
                 G1PPRL_TYPE_FORMAT
                 G1PPRL_ADDR_BASE_FORMAT
                 G1PPRL_BYTE_FORMAT
                 G1PPRL_BYTE_FORMAT
                 G1PPRL_BYTE_FORMAT
                 G1PPRL_DOUBLE_FORMAT
                 G1PPRL_BYTE_FORMAT
                 G1PPRL_BYTE_FORMAT,
                 type, p2i(bottom), p2i(end),
                 used_bytes, prev_live_bytes, next_live_bytes, gc_eff,
                 remset_bytes, strong_code_roots_bytes);

  return false;
}

G1PrintRegionLivenessInfoClosure::~G1PrintRegionLivenessInfoClosure() {
  // add static memory usages to remembered set sizes
  _total_remset_bytes += HeapRegionRemSet::fl_mem_size() + HeapRegionRemSet::static_mem_size();
  // Print the footer of the output.
  _out->print_cr(G1PPRL_LINE_PREFIX);
  _out->print_cr(G1PPRL_LINE_PREFIX
                 " SUMMARY"
                 G1PPRL_SUM_MB_FORMAT("capacity")
                 G1PPRL_SUM_MB_PERC_FORMAT("used")
                 G1PPRL_SUM_MB_PERC_FORMAT("prev-live")
                 G1PPRL_SUM_MB_PERC_FORMAT("next-live")
                 G1PPRL_SUM_MB_FORMAT("remset")
                 G1PPRL_SUM_MB_FORMAT("code-roots"),
                 bytes_to_mb(_total_capacity_bytes),
                 bytes_to_mb(_total_used_bytes),
                 perc(_total_used_bytes, _total_capacity_bytes),
                 bytes_to_mb(_total_prev_live_bytes),
                 perc(_total_prev_live_bytes, _total_capacity_bytes),
                 bytes_to_mb(_total_next_live_bytes),
                 perc(_total_next_live_bytes, _total_capacity_bytes),
                 bytes_to_mb(_total_remset_bytes),
                 bytes_to_mb(_total_strong_code_roots_bytes));
  _out->cr();
}
