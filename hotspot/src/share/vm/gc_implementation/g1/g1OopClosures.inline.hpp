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

#ifndef SHARE_VM_GC_IMPLEMENTATION_G1_G1OOPCLOSURES_INLINE_HPP
#define SHARE_VM_GC_IMPLEMENTATION_G1_G1OOPCLOSURES_INLINE_HPP

#include "gc_implementation/g1/concurrentMark.inline.hpp"
#include "gc_implementation/g1/g1CollectedHeap.hpp"
#include "gc_implementation/g1/g1OopClosures.hpp"
#include "gc_implementation/g1/g1ParScanThreadState.inline.hpp"
#include "gc_implementation/g1/g1RemSet.hpp"
#include "gc_implementation/g1/g1RemSet.inline.hpp"
#include "gc_implementation/g1/heapRegionRemSet.hpp"
#include "memory/iterator.inline.hpp"
#include "runtime/prefetch.inline.hpp"

// This closure is applied to the fields of the objects that have just been copied.
template <class T>
inline void G1ScanClosureBase::prefetch_and_push(T* p, const oop obj) {
    // We're not going to even bother checking whether the object is
    // already forwarded or not, as this usually causes an immediate
    // stall. We'll try to prefetch the object (for write, given that
    // we might need to install the forwarding reference) and we'll
    // get back to it when pop it from the queue
    Prefetch::write(obj->mark_addr(), 0);
    Prefetch::read(obj->mark_addr(), (HeapWordSize*2));

    // slightly paranoid test; I'm trying to catch potential
    // problems before we go into push_on_queue to know where the
    // problem is coming from
    assert((obj == oopDesc::load_decode_heap_oop(p)) ||
           (obj->is_forwarded() &&
               obj->forwardee() == oopDesc::load_decode_heap_oop(p)),
           "p should still be pointing to obj or to its forwardee");

    _par_scan_state->push_on_queue(p);
}

template <class T>
inline void G1ScanClosureBase::handle_non_cset_obj_common(InCSetState const state, T* p, oop const obj) {
    if (state.is_humongous()) {
      _g1->set_humongous_is_live(obj);
    }
}

template <class T>
inline void G1ScanEvacuatedObjClosure::do_oop_nv(T* p) {
  T heap_oop = oopDesc::load_heap_oop(p);

  if (oopDesc::is_null(heap_oop)) {
    return;
  }

  oop obj = oopDesc::decode_heap_oop_not_null(heap_oop);
  const InCSetState state = _g1->in_cset_state(obj);
  if (state.is_in_cset()) {
    prefetch_and_push(p, obj);
  } else {
    handle_non_cset_obj_common(state, p, obj);
    _par_scan_state->update_rs(_from, p, _par_scan_state->queue_num());
  }
}

template <class T>
inline void G1CMOopClosure::do_oop_nv(T* p) {
  if (_cm->verbose_high()) {
    gclog_or_tty->print_cr("[%u] we're looking at location " PTR_FORMAT "",
                           _task->worker_id(), p2i(p));
  }
  _task->deal_with_reference(p);
}

template <class T>
inline void G1RootRegionScanClosure::do_oop_nv(T* p) {
  T heap_oop = oopDesc::load_heap_oop(p);
  if (oopDesc::is_null(heap_oop)) {
    return;
  }
  oop obj = oopDesc::decode_heap_oop_not_null(heap_oop);
  _cm->mark_in_next_bitmap(_worker_id, obj);
}

template <class T>
inline void G1Mux2Closure::do_oop_nv(T* p) {
  // Apply first closure; then apply the second.
  _c1->do_oop(p);
  _c2->do_oop(p);
}

template <class T>
inline static void check_obj_during_refinement(T* p, oop const obj) {
#ifdef ASSERT
  G1CollectedHeap* g1 = G1CollectedHeap::heap();
  // can't do because of races
  // assert(obj == NULL || obj->is_oop(), "expected an oop");
  assert(check_obj_alignment(obj), "not oop aligned");
  assert(g1->is_in_reserved(obj), "must be in heap");

  HeapRegion* from = g1->heap_region_containing(p);

  assert(from != NULL, "from region must be non-NULL");
  assert(from->is_in_reserved(p) ||
         (from->isHumongous() &&
          g1->heap_region_containing(p)->isHumongous() &&
          from->humongous_start_region() == g1->heap_region_containing(p)->humongous_start_region()),
          err_msg("p " PTR_FORMAT " is not in the same region %u or part of the correct humongous object"
              " starting at region %u.", p2i(p), from->hrm_index(), from->humongous_start_region()->hrm_index()));
#endif // ASSERT
}

template <class T>
inline void G1ConcurrentRefineOopClosure::do_oop_nv(T* p) {
  T o = oopDesc::load_heap_oop(p);
  if (oopDesc::is_null(o)) {
    return;
  }
  oop obj = oopDesc::decode_heap_oop_not_null(o);

  check_obj_during_refinement(p, obj);

  if (HeapRegion::is_in_same_region(p, obj)) {
    // Normally this closure should only be called with cross-region references.
    // But since Java threads are manipulating the references concurrently and we
    // reload the values things may have changed.
    // Also this check lets slip through references from a humongous continues region
    // to its humongous start region, as they are in different regions, and adds a
    // remembered set entry. This is benign (apart from memory usage), as we never
    // try to either evacuate or eager reclaim humonguous arrays of j.l.O.
    return;
  }

  HeapRegionRemSet* to_rem_set = _g1->heap_region_containing(obj)->rem_set();

  assert(to_rem_set != NULL, "Need per-region 'into' remsets.");
  if (to_rem_set->is_tracked()) {
    to_rem_set->add_reference(p, _worker_i);
  }
}


template <class T>
inline void G1ScanObjsDuringUpdateRSClosure::do_oop_nv(T* p) {
  T o = oopDesc::load_heap_oop(p);
  if (oopDesc::is_null(o)) {
    return;
  }
  oop obj = oopDesc::decode_heap_oop_not_null(o);
  check_obj_during_refinement(p, obj);

  assert(!_g1->is_in_cset((HeapWord*)p),
         err_msg("Oop originates from " PTR_FORMAT " (region: %u) which is in the collection set.", p2i(p),
         _g1->addr_to_region((HeapWord*)p)))
         ;
  const InCSetState state = _g1->in_cset_state(obj);
  if (state.is_in_cset()) {
    // Since the source is always from outside the collection set, here we implicitly know
    // that this is a cross-region reference too.
    prefetch_and_push(p, obj);

    _has_refs_into_cset = true;
  } else {
    HeapRegion* to = _g1->heap_region_containing(obj);
    if (_from == to) {
      // Normally this closure should only be called with cross-region references.
      // But since Java threads are manipulating the references concurrently and we
      // reload the values things may have changed.
      return;
    }
    handle_non_cset_obj_common(state, p, obj);
    to->rem_set()->add_reference(p, _worker_i);
  }
}

template <class T>
inline void G1ScanObjsDuringScanRSClosure::do_oop_nv(T* p) {
  T heap_oop = oopDesc::load_heap_oop(p);
  if (oopDesc::is_null(heap_oop)) {
    return;
  }
  oop obj = oopDesc::decode_heap_oop_not_null(heap_oop);

  const InCSetState state = _g1->in_cset_state(obj);
  if (state.is_in_cset()) {
    prefetch_and_push(p, obj);
  } else {
    handle_non_cset_obj_common(state, p, obj);
  }
}

template <class T> void G1RebuildRemSetClosure::do_oop_nv(T* p) {
  T heap_oop = oopDesc::load_heap_oop(p);
  if (oopDesc::is_null(heap_oop)) {
    return;
  }
  oop obj = oopDesc::decode_heap_oop_not_null(heap_oop);

  if (HeapRegion::is_in_same_region(p, obj)) {
    return;
  }

  HeapRegion* to = _g1->heap_region_containing(obj);
  HeapRegionRemSet* rem_set = to->rem_set();
  rem_set->add_reference(p, _worker_id);
}

#endif // SHARE_VM_GC_IMPLEMENTATION_G1_G1OOPCLOSURES_INLINE_HPP
