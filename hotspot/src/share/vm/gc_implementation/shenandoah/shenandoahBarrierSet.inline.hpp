/*
 * Copyright (c) 2015, 2018, Red Hat, Inc. All rights reserved.
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

#ifndef SHARE_VM_GC_SHENANDOAH_SHENANDOAHBARRIERSET_INLINE_HPP
#define SHARE_VM_GC_SHENANDOAH_SHENANDOAHBARRIERSET_INLINE_HPP

#include "gc_implementation/shenandoah/shenandoahBarrierSet.hpp"
#include "gc_implementation/shenandoah/shenandoahCollectionSet.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahForwarding.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahMarkingContext.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/oop.inline.hpp"

inline oop ShenandoahBarrierSet::resolve_forwarded_not_null(oop p) {
  return ShenandoahForwarding::get_forwardee(p);
}

inline oop ShenandoahBarrierSet::resolve_forwarded(oop p) {
  if (((HeapWord*) p) != NULL) {
    return resolve_forwarded_not_null(p);
  } else {
    return p;
  }
}

inline oop ShenandoahBarrierSet::resolve_forwarded_not_null_mutator(oop p) {
  return ShenandoahForwarding::get_forwardee_mutator(p);
}

inline oop ShenandoahBarrierSet::load_reference_barrier_mutator(oop obj) {
  assert(ShenandoahLoadRefBarrier, "should be enabled");
  shenandoah_assert_in_cset(NULL, obj);

  oop fwd = resolve_forwarded_not_null_mutator(obj);
  if (obj == fwd) {
    assert(_heap->is_evacuation_in_progress(),
           "evac should be in progress");
    ShenandoahEvacOOMScope scope;
    fwd = _heap->evacuate_object(obj, Thread::current());
  }

  return fwd;
}

template <class T, bool HAS_FWD, bool EVAC, bool ENQUEUE>
void ShenandoahBarrierSet::arraycopy_work(T* src, size_t count) {
  assert(HAS_FWD == _heap->has_forwarded_objects(), "Forwarded object status is sane");

  JavaThread* thread = JavaThread::current();
  ObjPtrQueue& queue = thread->satb_mark_queue();
  ShenandoahMarkingContext* ctx = _heap->marking_context();
  const ShenandoahCollectionSet* const cset = _heap->collection_set();
  T* end = src + count;
  for (T* elem_ptr = src; elem_ptr < end; elem_ptr++) {
    T o = oopDesc::load_heap_oop(elem_ptr);
    if (!oopDesc::is_null(o)) {
      oop obj = oopDesc::decode_heap_oop_not_null(o);
      if (HAS_FWD && cset->is_in(obj)) {
        oop fwd = resolve_forwarded_not_null(obj);
        if (EVAC && obj == fwd) {
          fwd = _heap->evacuate_object(obj, thread);
        }
        assert(obj != fwd || _heap->cancelled_gc(), "must be forwarded");
        oop witness = ShenandoahHeap::cas_oop(fwd, elem_ptr, o);
        obj = fwd;
      }
      if (ENQUEUE && !ctx->is_marked(obj)) {
        queue.enqueue_known_active(obj);
      }
    }
  }
}

template <class T>
void ShenandoahBarrierSet::arraycopy_barrier(T* src, T* dst, size_t count) {
  if (count == 0) {
    return;
  }
  int gc_state = _heap->gc_state();
  if ((gc_state & ShenandoahHeap::MARKING) != 0) {
    arraycopy_marking(src, dst, count);
  } else if ((gc_state & ShenandoahHeap::EVACUATION) != 0) {
    arraycopy_evacuation(src, count);
  } else if ((gc_state & ShenandoahHeap::UPDATEREFS) != 0) {
    arraycopy_update(src, count);
  }
}

template <class T>
void ShenandoahBarrierSet::arraycopy_marking(T* src, T* dst, size_t count) {
  assert(_heap->is_concurrent_mark_in_progress(), "only during marking");
  T* array = ShenandoahSATBBarrier ? dst : src;
  if (!_heap->marking_context()->allocated_after_mark_start(reinterpret_cast<HeapWord*>(array))) {
    arraycopy_work<T, false, false, true>(array, count);
  }
}

inline bool ShenandoahBarrierSet::need_bulk_update(HeapWord* ary) {
  return ary < _heap->heap_region_containing(ary)->get_update_watermark();
}

template <class T>
void ShenandoahBarrierSet::arraycopy_evacuation(T* src, size_t count) {
  assert(_heap->is_evacuation_in_progress(), "only during evacuation");
  if (need_bulk_update(reinterpret_cast<HeapWord*>(src))) {
    ShenandoahEvacOOMScope oom_evac;
    arraycopy_work<T, true, true, false>(src, count);
  }
}

template <class T>
void ShenandoahBarrierSet::arraycopy_update(T* src, size_t count) {
  assert(_heap->is_update_refs_in_progress(), "only during update-refs");
  if (need_bulk_update(reinterpret_cast<HeapWord*>(src))) {
    arraycopy_work<T, true, false, false>(src, count);
  }
}

#endif //SHARE_VM_GC_SHENANDOAH_SHENANDOAHBARRIERSET_INLINE_HPP
