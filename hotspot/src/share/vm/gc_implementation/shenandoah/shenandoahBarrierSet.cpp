/*
 * Copyright (c) 2013, 2018, Red Hat, Inc. All rights reserved.
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
#include "gc_implementation/g1/g1SATBCardTableModRefBS.hpp"
#include "gc_implementation/shenandoah/shenandoahAsserts.hpp"
#include "gc_implementation/shenandoah/shenandoahBarrierSet.hpp"
#include "gc_implementation/shenandoah/shenandoahBarrierSetClone.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahCollectorPolicy.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"
#include "gc_implementation/shenandoah/heuristics/shenandoahHeuristics.hpp"
#include "runtime/interfaceSupport.hpp"
#include "utilities/macros.hpp"

#ifdef COMPILER1
#include "gc_implementation/shenandoah/c1/shenandoahBarrierSetC1.hpp"
#endif
#ifdef COMPILER2
#include "gc_implementation/shenandoah/c2/shenandoahBarrierSetC2.hpp"
#endif

#if defined(TARGET_ARCH_aarch64)
#include "shenandoahBarrierSetAssembler_aarch64.hpp"
#elif defined(TARGET_ARCH_x86)
#include "shenandoahBarrierSetAssembler_x86.hpp"
#else
#include "shenandoahBarrierSetAssembler_stub.hpp"
#endif

ShenandoahBarrierSet::ShenandoahBarrierSet(ShenandoahHeap* heap) :
  BarrierSet(),
  _heap(heap),
  _bsasm(new ShenandoahBarrierSetAssembler()),
  _bsc1(COMPILER1_PRESENT(new ShenandoahBarrierSetC1()) NOT_COMPILER1(NULL)),
  _bsc2(COMPILER2_PRESENT(new ShenandoahBarrierSetC2()) NOT_COMPILER2(NULL))
{
  _kind = BarrierSet::ShenandoahBarrierSet;
}

ShenandoahBarrierSetAssembler* ShenandoahBarrierSet::bsasm() const {
  return _bsasm;
}

ShenandoahBarrierSetC1* ShenandoahBarrierSet::bsc1() const {
  return _bsc1;
}

ShenandoahBarrierSetC2* ShenandoahBarrierSet::bsc2() const {
  return _bsc2;
}

void ShenandoahBarrierSet::print_on(outputStream* st) const {
  st->print("ShenandoahBarrierSet");
}

bool ShenandoahBarrierSet::is_a(BarrierSet::Name bsn) {
  return bsn == BarrierSet::ShenandoahBarrierSet;
}

bool ShenandoahBarrierSet::has_read_prim_array_opt() {
  return true;
}

bool ShenandoahBarrierSet::has_read_prim_barrier() {
  return false;
}

bool ShenandoahBarrierSet::has_read_ref_array_opt() {
  return true;
}

bool ShenandoahBarrierSet::has_read_ref_barrier() {
  return false;
}

bool ShenandoahBarrierSet::has_read_region_opt() {
  return true;
}

bool ShenandoahBarrierSet::has_write_prim_array_opt() {
  return true;
}

bool ShenandoahBarrierSet::has_write_prim_barrier() {
  return false;
}

bool ShenandoahBarrierSet::has_write_ref_array_opt() {
  return true;
}

bool ShenandoahBarrierSet::has_write_ref_barrier() {
  return true;
}

bool ShenandoahBarrierSet::has_write_ref_pre_barrier() {
  return true;
}

bool ShenandoahBarrierSet::has_write_region_opt() {
  return true;
}

bool ShenandoahBarrierSet::is_aligned(HeapWord* hw) {
  return true;
}

bool ShenandoahBarrierSet::read_prim_needs_barrier(HeapWord* hw, size_t s) {
  return false;
}

void ShenandoahBarrierSet::read_ref_field(void* v) {
  //    tty->print_cr("read_ref_field: v = "PTR_FORMAT, v);
  // return *v;
}

template <class T>
inline void ShenandoahBarrierSet::inline_write_ref_field_pre(T* field, oop newVal) {
  newVal = load_reference_barrier(newVal);
  storeval_barrier(newVal);
  if (ShenandoahSATBBarrier && _heap->is_concurrent_mark_in_progress()) {
    T heap_oop = oopDesc::load_heap_oop(field);
    shenandoah_assert_not_in_cset_loc_except(field, ShenandoahHeap::heap()->cancelled_gc());
    if (!oopDesc::is_null(heap_oop)) {
      ShenandoahBarrierSet::barrier_set()->enqueue(oopDesc::decode_heap_oop(heap_oop));
    }
  }
}

// These are the more general virtual versions.
void ShenandoahBarrierSet::write_ref_field_pre_work(oop* field, oop new_val) {
  inline_write_ref_field_pre(field, new_val);
}

void ShenandoahBarrierSet::write_ref_field_pre_work(narrowOop* field, oop new_val) {
  inline_write_ref_field_pre(field, new_val);
}

void ShenandoahBarrierSet::write_ref_field_work(void* v, oop o, bool release) {
  shenandoah_assert_not_in_cset_loc_except(v, _heap->cancelled_gc());
  shenandoah_assert_not_forwarded_except  (v, o, o == NULL || _heap->cancelled_gc() || !_heap->is_concurrent_mark_in_progress());
  shenandoah_assert_not_in_cset_except    (v, o, o == NULL || _heap->cancelled_gc() || !_heap->is_concurrent_mark_in_progress());
}

oop ShenandoahBarrierSet::load_reference_barrier_not_null(oop obj) {
  assert(obj != NULL, "");
  if (ShenandoahLoadRefBarrier && _heap->has_forwarded_objects()) {
    return load_reference_barrier_impl(obj);
  } else {
    return obj;
  }
}

oop ShenandoahBarrierSet::load_reference_barrier(oop obj) {
  if (obj != NULL) {
    return load_reference_barrier_not_null(obj);
  } else {
    return obj;
  }
}


oop ShenandoahBarrierSet::load_reference_barrier_impl(oop obj) {
  assert(ShenandoahLoadRefBarrier, "should be enabled");
  if (!oopDesc::is_null(obj)) {
    oop fwd = resolve_forwarded_not_null(obj);
    if (_heap->is_evacuation_in_progress() &&
        _heap->in_collection_set(obj) &&
        obj == fwd) {
      Thread *t = Thread::current();
      ShenandoahEvacOOMScope oom_evac_scope;
      return _heap->evacuate_object(obj, t);
    } else {
      return fwd;
    }
  } else {
    return obj;
  }
}

void ShenandoahBarrierSet::storeval_barrier(oop obj) {
  if (ShenandoahStoreValEnqueueBarrier && !oopDesc::is_null(obj) && _heap->is_concurrent_mark_in_progress()) {
    enqueue(obj);
  }
}

void ShenandoahBarrierSet::keep_alive_barrier(oop obj) {
  if (_heap->is_concurrent_mark_in_progress()) {
    enqueue(obj);
  }
}

void ShenandoahBarrierSet::enqueue(oop obj) {
  assert(JavaThread::satb_mark_queue_set().shared_satb_queue()->is_active(), "only get here when SATB active");

  // Filter marked objects before hitting the SATB queues. The same predicate would
  // be used by SATBMQ::filter to eliminate already marked objects downstream, but
  // filtering here helps to avoid wasteful SATB queueing work to begin with.
  if (!_heap->requires_marking(obj)) return;

  Thread* thr = Thread::current();
  if (thr->is_Java_thread()) {
    JavaThread* jt = (JavaThread*)thr;
    jt->satb_mark_queue().enqueue_known_active(obj);
  } else {
    MutexLockerEx x(Shared_SATB_Q_lock, Mutex::_no_safepoint_check_flag);
    JavaThread::satb_mark_queue_set().shared_satb_queue()->enqueue_known_active(obj);
  }
}

oop ShenandoahBarrierSet::atomic_compare_exchange_oop(oop exchange_value,
                                                      volatile HeapWord *dest,
                                                      oop compare_value) {
  if (UseCompressedOops) {
    // encode exchange and compare value from oop to T
    narrowOop val = oopDesc::encode_heap_oop(exchange_value);
    narrowOop cmp = oopDesc::encode_heap_oop(compare_value);

    narrowOop old = (narrowOop) Atomic::cmpxchg(val, (narrowOop*)dest, cmp);
    // decode old from T to oop
    return oopDesc::decode_heap_oop(old);
  } else {
    return (oop)Atomic::cmpxchg_ptr(exchange_value, (oop*)dest, compare_value);
  }
}

oop ShenandoahBarrierSet::oop_atomic_cmpxchg_in_heap(oop new_value, volatile HeapWord* dest, oop compare_value) {
  oop expected;
  bool success;
  do {
    expected = compare_value;
    compare_value = atomic_compare_exchange_oop(new_value, dest, expected);
    success = (compare_value == expected);
  } while ((! success) && resolve_forwarded(compare_value) == resolve_forwarded(expected));
  oop result = load_reference_barrier(compare_value);
  if (ShenandoahSATBBarrier && success && result != NULL &&
        ShenandoahHeap::heap()->is_concurrent_mark_in_progress()) {
    enqueue(result);
  }
  if (new_value != NULL) {
    storeval_barrier(new_value);
  }
  return result;
}

void ShenandoahBarrierSet::clone_barrier_runtime(oop src) {
  if (_heap->has_forwarded_objects() || (ShenandoahStoreValEnqueueBarrier && _heap->is_concurrent_mark_in_progress())) {
    clone_barrier(src);
  }
}
