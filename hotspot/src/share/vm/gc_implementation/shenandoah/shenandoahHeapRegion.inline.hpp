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

#ifndef SHARE_VM_GC_SHENANDOAH_SHENANDOAHHEAPREGION_INLINE_HPP
#define SHARE_VM_GC_SHENANDOAH_SHENANDOAHHEAPREGION_INLINE_HPP

#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahHeapRegion.hpp"
#include "gc_implementation/shenandoah/shenandoahPacer.inline.hpp"
#include "runtime/atomic.hpp"

HeapWord* ShenandoahHeapRegion::allocate(size_t size, ShenandoahAllocRequest::Type type) {
  shenandoah_assert_heaplocked_or_safepoint();

  assert(is_object_aligned((intptr_t)size), err_msg("alloc size breaks alignment: " SIZE_FORMAT, size));

  HeapWord* obj = top();
  if (pointer_delta(end(), obj) >= size) {
    make_regular_allocation();
    adjust_alloc_metadata(type, size);

    HeapWord* new_top = obj + size;
    set_top(new_top);

    assert(is_object_aligned((intptr_t)new_top), err_msg("new top breaks alignment: " PTR_FORMAT, p2i(new_top)));
    assert(is_object_aligned((intptr_t)obj),     err_msg("obj is not aligned: "       PTR_FORMAT, p2i(obj)));

    return obj;
  } else {
    return NULL;
  }
}

inline void ShenandoahHeapRegion::adjust_alloc_metadata(ShenandoahAllocRequest::Type type, size_t size) {
  switch (type) {
    case ShenandoahAllocRequest::_alloc_shared:
    case ShenandoahAllocRequest::_alloc_shared_gc:
      // Counted implicitly by tlab/gclab allocs
      break;
    case ShenandoahAllocRequest::_alloc_tlab:
      _tlab_allocs += size;
      break;
    case ShenandoahAllocRequest::_alloc_gclab:
      _gclab_allocs += size;
      break;
    default:
      ShouldNotReachHere();
  }
}

void ShenandoahHeapRegion::clear_live_data() {
  OrderAccess::release_store_fence((volatile jint*)&_live_data, 0);
}

inline void ShenandoahHeapRegion::increase_live_data_alloc_words(size_t s) {
  internal_increase_live_data(s);
}

inline void ShenandoahHeapRegion::increase_live_data_gc_words(size_t s) {
  internal_increase_live_data(s);
  if (ShenandoahPacing) {
    ShenandoahHeap::heap()->pacer()->report_mark(s);
  }
}

inline void ShenandoahHeapRegion::internal_increase_live_data(size_t s) {
  assert(s < (size_t)max_jint, "sanity");
  size_t new_live_data = (size_t)(Atomic::add((jint)s, &_live_data));
#ifdef ASSERT
  size_t live_bytes = new_live_data * HeapWordSize;
  size_t used_bytes = used();
  assert(live_bytes <= used_bytes,
         err_msg("can't have more live data than used: " SIZE_FORMAT ", " SIZE_FORMAT, live_bytes, used_bytes));
#endif
}

size_t ShenandoahHeapRegion::get_live_data_words() const {
  jint v = OrderAccess::load_acquire((volatile jint*)&_live_data);
  assert(v >= 0, "sanity");
  return (size_t)v;
}

size_t ShenandoahHeapRegion::get_live_data_bytes() const {
  return get_live_data_words() * HeapWordSize;
}

bool ShenandoahHeapRegion::has_live() const {
  return get_live_data_words() != 0;
}

size_t ShenandoahHeapRegion::garbage() const {
  assert(used() >= get_live_data_bytes(), err_msg("Live Data must be a subset of used() live: " SIZE_FORMAT " used: " SIZE_FORMAT,
          get_live_data_bytes(), used()));
  size_t result = used() - get_live_data_bytes();
  return result;
}

inline HeapWord* ShenandoahHeapRegion::get_update_watermark() const {
  HeapWord* watermark = (HeapWord*)OrderAccess::load_ptr_acquire(&_update_watermark);
  assert(bottom() <= watermark && watermark <= top(), "within bounds");
  return watermark;
}

inline void ShenandoahHeapRegion::set_update_watermark(HeapWord* w) {
  assert(bottom() <= w && w <= top(), "within bounds");
  OrderAccess::release_store_ptr(&_update_watermark, w);
}

// Fast version that avoids synchronization, only to be used at safepoints.
inline void ShenandoahHeapRegion::set_update_watermark_at_safepoint(HeapWord* w) {
  assert(bottom() <= w && w <= top(), "within bounds");
  assert(SafepointSynchronize::is_at_safepoint(), "Should be at Shenandoah safepoint");
  _update_watermark = w;
}

#endif // SHARE_VM_GC_SHENANDOAH_SHENANDOAHHEAPREGION_INLINE_HPP
