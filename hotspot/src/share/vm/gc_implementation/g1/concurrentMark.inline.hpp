/*
 * Copyright (c) 2001, 2014, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_GC_IMPLEMENTATION_G1_CONCURRENTMARK_INLINE_HPP
#define SHARE_VM_GC_IMPLEMENTATION_G1_CONCURRENTMARK_INLINE_HPP

#include "gc_implementation/g1/concurrentMark.hpp"
#include "gc_implementation/g1/g1CollectedHeap.inline.hpp"
#include "gc_implementation/shared/suspendibleThreadSet.hpp"
#include "gc_implementation/g1/g1ConcurrentMarkObjArrayProcessor.inline.hpp"
#include "gc_implementation/g1/g1RegionMarkStatsCache.inline.hpp"
#include "gc_implementation/g1/g1RemSetTrackingPolicy.hpp"
#include "gc_implementation/g1/heapRegionRemSet.hpp"
#include "gc_implementation/g1/heapRegion.hpp"

inline bool ConcurrentMark::mark_in_next_bitmap(uint const worker_id, oop const obj, size_t const obj_size) {
  HeapRegion* const hr = _g1h->heap_region_containing(obj);
  return mark_in_next_bitmap(worker_id, hr, obj, obj_size);
}

inline bool ConcurrentMark::mark_in_next_bitmap(uint const worker_id,
                                                HeapRegion* const hr, oop const obj, size_t const obj_size) {
  assert(hr != NULL, "just checking");
  assert(hr->is_in_reserved(obj), err_msg("Attempting to mark object at " PTR_FORMAT " that"
         " is not contained in the given region %u", p2i(obj), hr->hrm_index()));

  if (hr->obj_allocated_since_next_marking(obj)) {
    return false;
  }

  // Some callers may have stale objects to mark above nTAMS after humongous reclaim.
  assert(obj->is_oop(true /* ignore mark word */), err_msg("Address " PTR_FORMAT " to mark is not an oop", p2i(obj)));
  assert(!hr->continuesHumongous(), err_msg("Should not try to mark object " PTR_FORMAT " in Humongous"
         " continues region %u above nTAMS " PTR_FORMAT, p2i(obj), hr->hrm_index(), p2i(hr->next_top_at_mark_start())));

  HeapWord* const obj_addr = (HeapWord*)obj;
  // Dirty read to avoid CAS.
  if (_nextMarkBitMap->isMarked(obj_addr)) {
    return false;
  }

  bool success = _nextMarkBitMap->parMark(obj_addr);
  if (success) {
    add_to_liveness(worker_id, obj, obj_size == 0 ? obj->size() : obj_size);
  }
  return success;
}

inline bool CMBitMapRO::iterate(BitMapClosure* cl, MemRegion mr) {
  HeapWord* start_addr = MAX2(startWord(), mr.start());
  HeapWord* end_addr = MIN2(endWord(), mr.end());

  if (end_addr > start_addr) {
    // Right-open interval [start-offset, end-offset).
    BitMap::idx_t start_offset = heapWordToOffset(start_addr);
    BitMap::idx_t end_offset = heapWordToOffset(end_addr);

    start_offset = _bm.get_next_one_offset(start_offset, end_offset);
    while (start_offset < end_offset) {
      if (!cl->do_bit(start_offset)) {
        return false;
      }
      HeapWord* next_addr = MIN2(nextObject(offsetToHeapWord(start_offset)), end_addr);
      BitMap::idx_t next_offset = heapWordToOffset(next_addr);
      start_offset = _bm.get_next_one_offset(next_offset, end_offset);
    }
  }
  return true;
}

inline bool CMBitMapRO::iterate(BitMapClosure* cl) {
  MemRegion mr(startWord(), sizeInWords());
  return iterate(cl, mr);
}

#define check_mark(addr)                                                       \
  assert(_bmStartWord <= (addr) && (addr) < (_bmStartWord + _bmWordSize),      \
         "outside underlying space?");                                         \
  assert(G1CollectedHeap::heap()->is_in_exact(addr),                           \
         err_msg("Trying to access not available bitmap " PTR_FORMAT           \
                 " corresponding to " PTR_FORMAT " (%u)",                      \
                 p2i(this), p2i(addr), G1CollectedHeap::heap()->addr_to_region(addr)));

inline void CMBitMap::mark(HeapWord* addr) {
  check_mark(addr);
  _bm.set_bit(heapWordToOffset(addr));
}

inline void CMBitMap::clear(HeapWord* addr) {
  check_mark(addr);
  _bm.clear_bit(heapWordToOffset(addr));
}

inline bool CMBitMap::parMark(HeapWord* addr) {
  check_mark(addr);
  return _bm.par_set_bit(heapWordToOffset(addr));
}

inline bool CMBitMap::parClear(HeapWord* addr) {
  check_mark(addr);
  return _bm.par_clear_bit(heapWordToOffset(addr));
}

#undef check_mark

inline void CMTask::push(oop obj) {
  HeapWord* objAddr = (HeapWord*) obj;
  assert(G1CMObjArrayProcessor::is_array_slice(obj) || _g1h->is_in_g1_reserved(objAddr), "invariant");
  assert(G1CMObjArrayProcessor::is_array_slice(obj) || !_g1h->is_on_master_free_list(
              _g1h->heap_region_containing((HeapWord*) objAddr)), "invariant");
  assert(G1CMObjArrayProcessor::is_array_slice(obj) || !_g1h->is_obj_ill(obj), "invariant");
  assert(G1CMObjArrayProcessor::is_array_slice(obj) || _nextMarkBitMap->isMarked(objAddr), "invariant");

  if (_cm->verbose_high()) {
    gclog_or_tty->print_cr("[%u] pushing " PTR_FORMAT, _worker_id, p2i((void*) obj));
  }

  if (!_task_queue->push(obj)) {
    // The local task queue looks full. We need to push some entries
    // to the global stack.

    if (_cm->verbose_medium()) {
      gclog_or_tty->print_cr("[%u] task queue overflow, "
                             "moving entries to the global stack",
                             _worker_id);
    }
    move_entries_to_global_stack();

    // this should succeed since, even if we overflow the global
    // stack, we should have definitely removed some entries from the
    // local queue. So, there must be space on it.
    bool success = _task_queue->push(obj);
    assert(success, "invariant");
  }

  statsOnly( int tmp_size = _task_queue->size();
             if (tmp_size > _local_max_size) {
               _local_max_size = tmp_size;
             }
             ++_local_pushes );
}

inline bool CMTask::is_below_finger(oop obj, HeapWord* global_finger) const {
  // If obj is above the global finger, then the mark bitmap scan
  // will find it later, and no push is needed.  Similarly, if we have
  // a current region and obj is between the local finger and the
  // end of the current region, then no push is needed.  The tradeoff
  // of checking both vs only checking the global finger is that the
  // local check will be more accurate and so result in fewer pushes,
  // but may also be a little slower.
  HeapWord* objAddr = (HeapWord*)obj;
  if (_finger != NULL) {
    // We have a current region.

    // Finger and region values are all NULL or all non-NULL.  We
    // use _finger to check since we immediately use its value.
    assert(_curr_region != NULL, "invariant");
    assert(_region_limit != NULL, "invariant");
    assert(_region_limit <= global_finger, "invariant");

    // True if obj is less than the local finger, or is between
    // the region limit and the global finger.
    if (objAddr < _finger) {
      return true;
    } else if (objAddr < _region_limit) {
      return false;
    } // Else check global finger.
  }
  // Check global finger.
  return objAddr < global_finger;
}

inline void CMTask::abort_marking_if_regular_check_fail() {
  if (!regular_clock_call()) {
    set_has_aborted();
  }
}

inline void CMTask::update_liveness(oop const obj, const size_t obj_size) {
  _mark_stats_cache.add_live_words(_g1h->addr_to_region((HeapWord*)obj), obj_size);
}

inline void ConcurrentMark::add_to_liveness(uint worker_id, oop const obj, size_t size) {
  task(worker_id)->update_liveness(obj, size);
}

inline void CMTask::make_reference_grey(oop obj) {
  if (!_cm->mark_in_next_bitmap(_worker_id, obj)) {
    return;
  }

  if (_cm->verbose_high()) {
    gclog_or_tty->print_cr("[%u] marked object " PTR_FORMAT,
                           _worker_id, p2i(obj));
  }

  // No OrderAccess:store_load() is needed. It is implicit in the
  // CAS done in CMBitMap::parMark() call in the routine above.
  HeapWord* global_finger = _cm->finger();

  // We only need to push a newly grey object on the mark
  // stack if it is in a section of memory the mark bitmap
  // scan has already examined.  Mark bitmap scanning
  // maintains progress "fingers" for determining that.
  //
  // Notice that the global finger might be moving forward
  // concurrently. This is not a problem. In the worst case, we
  // mark the object while it is above the global finger and, by
  // the time we read the global finger, it has moved forward
  // past this object. In this case, the object will probably
  // be visited when a task is scanning the region and will also
  // be pushed on the stack. So, some duplicate work, but no
  // correctness problems.
  if (is_below_finger(obj, global_finger)) {
    if (obj->is_typeArray()) {
      // Immediately process arrays of primitive types, rather
      // than pushing on the mark stack.  This keeps us from
      // adding humongous objects to the mark stack that might
      // be reclaimed before the entry is processed - see
      // selection of candidates for eager reclaim of humongous
      // objects.  The cost of the additional type test is
      // mitigated by avoiding a trip through the mark stack,
      // by only doing a bookkeeping update and avoiding the
      // actual scan of the object - a typeArray contains no
      // references, and the metadata is built-in.
      process_grey_object<false>(obj);
    } else {
      if (_cm->verbose_high()) {
        gclog_or_tty->print_cr("[%u] below a finger (local: " PTR_FORMAT
                               ", global: " PTR_FORMAT ") pushing "
                               PTR_FORMAT " on mark stack",
                               _worker_id, p2i(_finger),
                               p2i(global_finger), p2i(obj));
      }
      push(obj);
    }
  }
}

template <class T>
inline void CMTask::deal_with_reference(T *p) {
  oop obj = oopDesc::load_decode_heap_oop(p);
  if (_cm->verbose_high()) {
    gclog_or_tty->print_cr("[%u] we're dealing with reference = " PTR_FORMAT,
                           _worker_id, p2i((void*) obj));
  }
  increment_refs_reached();
  if (obj == NULL) {
    return;
  }
  make_reference_grey(obj);
}

inline size_t CMTask::scan_objArray(objArrayOop obj, MemRegion mr) {
  obj->oop_iterate(_cm_oop_closure, mr);
  return mr.word_size();
}

inline HeapWord* ConcurrentMark::top_at_rebuild_start(uint region) const {
  assert(region < _g1h->max_regions(), err_msg("Tried to access TARS for region %u out of bounds", region));
  return _top_at_rebuild_starts[region];
}

inline void ConcurrentMark::update_top_at_rebuild_start(HeapRegion* r) {
  uint const region = r->hrm_index();
  assert(region < _g1h->max_regions(), err_msg("Tried to access TARS for region %u out of bounds", region));
  assert(_top_at_rebuild_starts[region] == NULL,
         err_msg("TARS for region %u has already been set to " PTR_FORMAT " should be NULL",
         region, p2i(_top_at_rebuild_starts[region])));
  G1RemSetTrackingPolicy* tracker = _g1h->g1_policy()->remset_tracker();
  if (tracker->needs_scan_for_rebuild(r)) {
    _top_at_rebuild_starts[region] = r->top();
  } else {
    // We could leave the TARS for this region at NULL, but we would not catch
    // accidental double assignment then.
    _top_at_rebuild_starts[region] = r->bottom();
  }
}

inline void ConcurrentMark::markPrev(oop p) {
  assert(!_prevMarkBitMap->isMarked((HeapWord*) p), "sanity");
  // Note we are overriding the read-only view of the prev map here, via
  // the cast.
  ((CMBitMap*)_prevMarkBitMap)->mark((HeapWord*) p);
}

// We take a break if someone is trying to stop the world.
inline bool ConcurrentMark::do_yield_check() {
  if (SuspendibleThreadSet::should_yield()) {
    SuspendibleThreadSet::yield();
    return true;
  } else {
    return false;
  }
}

#endif // SHARE_VM_GC_IMPLEMENTATION_G1_CONCURRENTMARK_INLINE_HPP
