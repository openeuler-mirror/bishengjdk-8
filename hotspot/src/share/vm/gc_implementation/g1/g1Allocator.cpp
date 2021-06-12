/*
 * Copyright (c) 2014, Oracle and/or its affiliates. All rights reserved.
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
#include "gc_implementation/g1/g1Allocator.hpp"
#include "gc_implementation/g1/g1CollectedHeap.hpp"
#include "gc_implementation/g1/g1CollectorPolicy.hpp"
#include "gc_implementation/g1/g1NUMA.hpp"
#include "gc_implementation/g1/heapRegion.inline.hpp"
#include "gc_implementation/g1/heapRegionSet.inline.hpp"

void G1DefaultAllocator::init_mutator_alloc_regions() {
  for (uint i = 0; i < _num_alloc_regions; i++) {
    assert(mutator_alloc_region(i)->get() == NULL, "pre-condition");
    mutator_alloc_region(i)->init();
  }
}

void G1DefaultAllocator::release_mutator_alloc_regions() {
  for (uint i = 0; i < _num_alloc_regions; i++) {
    mutator_alloc_region(i)->release();
    assert(mutator_alloc_region(i)->get() == NULL, "post-condition");
  }
}

inline HeapWord* G1DefaultAllocator::attempt_allocation_locked(size_t word_size, bool bot_updates, uint &node_index) {
  node_index = current_node_index();
  HeapWord* result = mutator_alloc_region(node_index)->attempt_allocation_locked(word_size, bot_updates);
  assert(result != NULL || mutator_alloc_region(node_index)->get() == NULL,
         err_msg("Must not have a mutator alloc region if there is no memory, but is " PTR_FORMAT, p2i(mutator_alloc_region(node_index)->get())));
  return result;
}

inline HeapWord* G1DefaultAllocator::attempt_allocation_force(size_t word_size, bool bot_updates, uint node_index) {
  if (node_index == G1NUMA::AnyNodeIndex) {
    return NULL;
  }
  assert(node_index < _num_alloc_regions, err_msg("Invalid index: %u", node_index));
  return mutator_alloc_region(node_index)->attempt_allocation_force(word_size, bot_updates);
}

G1DefaultAllocator::G1DefaultAllocator(G1CollectedHeap* heap) :
  G1Allocator(heap),
  _numa(heap->numa()),
  _num_alloc_regions(_numa->num_active_nodes()),
  _mutator_alloc_regions(NULL),
  _survivor_gc_alloc_regions(NULL),
  _old_gc_alloc_region(),
  _retained_old_gc_alloc_region(NULL) {

  _mutator_alloc_regions = NEW_C_HEAP_ARRAY(MutatorAllocRegion, _num_alloc_regions, mtGC);
  _survivor_gc_alloc_regions = NEW_C_HEAP_ARRAY(SurvivorGCAllocRegion, _num_alloc_regions, mtGC);
  for (uint i = 0; i < _num_alloc_regions; i++) {
    ::new(_mutator_alloc_regions + i) MutatorAllocRegion(i);
    ::new(_survivor_gc_alloc_regions + i) SurvivorGCAllocRegion(i);
  }
}

G1DefaultAllocator::~G1DefaultAllocator() {
  for (uint i = 0; i < _num_alloc_regions; i++) {
    _mutator_alloc_regions[i].~MutatorAllocRegion();
    _survivor_gc_alloc_regions[i].~SurvivorGCAllocRegion();
  }
  FREE_C_HEAP_ARRAY(MutatorAllocRegion, _mutator_alloc_regions, mtGC);
  FREE_C_HEAP_ARRAY(SurvivorGCAllocRegion, _survivor_gc_alloc_regions, mtGC);
}

#ifdef ASSERT
bool G1Allocator::has_mutator_alloc_region() {
  uint node_index = current_node_index();
  return mutator_alloc_region(node_index)->get() != NULL;
}
#endif

void G1Allocator::reuse_retained_old_region(EvacuationInfo& evacuation_info,
                                            OldGCAllocRegion* old,
                                            HeapRegion** retained_old) {
  HeapRegion* retained_region = *retained_old;
  *retained_old = NULL;

  // We will discard the current GC alloc region if:
  // a) it's in the collection set (it can happen!),
  // b) it's already full (no point in using it),
  // c) it's empty (this means that it was emptied during
  // a cleanup and it should be on the free list now), or
  // d) it's humongous (this means that it was emptied
  // during a cleanup and was added to the free list, but
  // has been subsequently used to allocate a humongous
  // object that may be less than the region size).
  if (retained_region != NULL &&
      !retained_region->in_collection_set() &&
      !(retained_region->top() == retained_region->end()) &&
      !retained_region->is_empty() &&
      !retained_region->isHumongous()) {
    retained_region->record_timestamp();
    // The retained region was added to the old region set when it was
    // retired. We have to remove it now, since we don't allow regions
    // we allocate to in the region sets. We'll re-add it later, when
    // it's retired again.
    _g1h->_old_set.remove(retained_region);
    bool during_im = _g1h->g1_policy()->during_initial_mark_pause();
    retained_region->note_start_of_copying(during_im);
    old->set(retained_region);
    _g1h->_hr_printer.reuse(retained_region);
    evacuation_info.set_alloc_regions_used_before(retained_region->used());
  }
}

void G1DefaultAllocator::init_gc_alloc_regions(EvacuationInfo& evacuation_info) {
  assert_at_safepoint(true /* should_be_vm_thread */);

  for (uint i = 0; i < _num_alloc_regions; i++) {
    survivor_gc_alloc_region(i)->init();
  }
  _old_gc_alloc_region.init();
  reuse_retained_old_region(evacuation_info,
                            &_old_gc_alloc_region,
                            &_retained_old_gc_alloc_region);
}

void G1DefaultAllocator::release_gc_alloc_regions(uint no_of_gc_workers, EvacuationInfo& evacuation_info) {
  AllocationContext_t context = AllocationContext::current();
  uint survivor_region_count = 0;
  for (uint node_index = 0; node_index < _num_alloc_regions; node_index++) {
    survivor_region_count += survivor_gc_alloc_region(node_index)->count();
    survivor_gc_alloc_region(node_index)->release();
  }
  evacuation_info.set_allocation_regions(survivor_region_count +
                                         old_gc_alloc_region(context)->count());
  // If we have an old GC alloc region to release, we'll save it in
  // _retained_old_gc_alloc_region. If we don't
  // _retained_old_gc_alloc_region will become NULL. This is what we
  // want either way so no reason to check explicitly for either
  // condition.
  _retained_old_gc_alloc_region = old_gc_alloc_region(context)->release();
  if (_retained_old_gc_alloc_region != NULL) {
    _retained_old_gc_alloc_region->record_retained_region();
  }

  if (ResizePLAB) {
    _g1h->_survivor_plab_stats.adjust_desired_plab_sz(no_of_gc_workers);
    _g1h->_old_plab_stats.adjust_desired_plab_sz(no_of_gc_workers);
  }
}

void G1DefaultAllocator::abandon_gc_alloc_regions() {
  for (uint i = 0; i < _num_alloc_regions; i++) {
    assert(survivor_gc_alloc_region(i)->get() == NULL, "pre-condition");
  }
  assert(old_gc_alloc_region(AllocationContext::current())->get() == NULL, "pre-condition");
  _retained_old_gc_alloc_region = NULL;
}

G1ParGCAllocBuffer::G1ParGCAllocBuffer(size_t gclab_word_size) :
  ParGCAllocBuffer(gclab_word_size), _retired(true) { }

G1ParGCAllocator::G1ParGCAllocator(G1CollectedHeap* g1h) :
  _g1h(g1h), _survivor_alignment_bytes(calc_survivor_alignment_bytes()),
  _numa(g1h->numa()),
  _num_alloc_regions(_numa->num_active_nodes()),
  _alloc_buffer_waste(0), _undo_waste(0) {
}

HeapWord* G1ParGCAllocator::allocate_direct_or_new_plab(InCSetState dest,
                                                        size_t word_sz,
                                                        AllocationContext_t context,
                                                        uint node_index) {
  size_t gclab_word_size = _g1h->desired_plab_sz(dest);
  if (word_sz * 100 < gclab_word_size * ParallelGCBufferWastePct) {
    G1ParGCAllocBuffer* alloc_buf = alloc_buffer(dest, context, node_index);
    add_to_alloc_buffer_waste(alloc_buf->words_remaining());
    alloc_buf->retire(false /* end_of_gc */, false /* retain */);

    HeapWord* buf = _g1h->par_allocate_during_gc(dest, gclab_word_size, context, node_index);
    if (buf == NULL) {
      return NULL; // Let caller handle allocation failure.
    }
    // Otherwise.
    alloc_buf->set_word_size(gclab_word_size);
    alloc_buf->set_buf(buf);

    HeapWord* const obj = alloc_buf->allocate(word_sz);
    assert(obj != NULL, "buffer was definitely big enough...");
    return obj;
  } else {
    return _g1h->par_allocate_during_gc(dest, word_sz, context, node_index);
  }
}

G1DefaultParGCAllocator::G1DefaultParGCAllocator(G1CollectedHeap* g1h) :
  G1ParGCAllocator(g1h) {
  for (uint state = 0; state < InCSetState::Num; state++) {
    _alloc_buffers[state] = NULL;
    uint length = alloc_buffers_length(state);
    _alloc_buffers[state] = NEW_C_HEAP_ARRAY(G1ParGCAllocBuffer*, length, mtGC);
    for (uint node_index = 0; node_index < length; node_index++) {
      _alloc_buffers[state][node_index] = new G1ParGCAllocBuffer(_g1h->desired_plab_sz(state));
    }
  }
}

G1DefaultParGCAllocator::~G1DefaultParGCAllocator() {
  for (in_cset_state_t state = 0; state < InCSetState::Num; state++) {
    uint length = alloc_buffers_length(state);
    for (uint node_index = 0; node_index < length; node_index++) {
      delete _alloc_buffers[state][node_index];
    }
    FREE_C_HEAP_ARRAY(G1ParGCAllocBuffer*, _alloc_buffers[state], mtGC);
  }
}

void G1DefaultParGCAllocator::retire_alloc_buffers() {
  for (uint state = 0; state < InCSetState::Num; state++) {
    uint length = alloc_buffers_length(state);
    for (uint node_index = 0; node_index < length; node_index++) {
      G1ParGCAllocBuffer* const buf = _alloc_buffers[state][node_index];
      if (buf != NULL) {
        add_to_alloc_buffer_waste(buf->words_remaining());
        buf->flush_stats_and_retire(_g1h->alloc_buffer_stats(state),
                                  true /* end_of_gc */,
                                  false /* retain */);
      }
    }
  }
}

uint G1DefaultAllocator::current_node_index() const {
  return _numa->index_of_current_thread();
}
