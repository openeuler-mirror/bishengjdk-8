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

#ifndef SHARE_VM_GC_IMPLEMENTATION_G1_G1ALLOCATOR_HPP
#define SHARE_VM_GC_IMPLEMENTATION_G1_G1ALLOCATOR_HPP

#include "gc_implementation/g1/g1AllocationContext.hpp"
#include "gc_implementation/g1/g1AllocRegion.hpp"
#include "gc_implementation/g1/g1InCSetState.hpp"
#include "gc_implementation/shared/parGCAllocBuffer.hpp"

class G1NUMA;

// Base class for G1 allocators.
class G1Allocator : public CHeapObj<mtGC> {
  friend class VMStructs;
protected:
  G1CollectedHeap* _g1h;

  // Outside of GC pauses, the number of bytes used in all regions other
  // than the current allocation region.
  size_t _summary_bytes_used;

public:
   G1Allocator(G1CollectedHeap* heap) :
     _g1h(heap), _summary_bytes_used(0) { }
   virtual ~G1Allocator() { }

   // Node index of current thread.
   virtual uint current_node_index() const = 0;

   static G1Allocator* create_allocator(G1CollectedHeap* g1h);

   virtual void init_mutator_alloc_regions() = 0;
   virtual void release_mutator_alloc_regions() = 0;

   virtual void init_gc_alloc_regions(EvacuationInfo& evacuation_info) = 0;
   virtual void release_gc_alloc_regions(uint no_of_gc_workers, EvacuationInfo& evacuation_info) = 0;
   virtual void abandon_gc_alloc_regions() = 0;

#ifdef ASSERT
   // Do we currently have an active mutator region to allocate into?
   bool has_mutator_alloc_region();
#endif

   virtual MutatorAllocRegion*    mutator_alloc_region(uint node_index) = 0;
   virtual SurvivorGCAllocRegion* survivor_gc_alloc_region(uint node_index) = 0;
   virtual MutatorAllocRegion*    mutator_alloc_region() = 0;
   virtual SurvivorGCAllocRegion* survivor_gc_alloc_region() = 0;
   virtual OldGCAllocRegion*      old_gc_alloc_region(AllocationContext_t context) = 0;
   virtual size_t                 used() = 0;
   virtual bool                   is_retained_old_region(HeapRegion* hr) = 0;

   void                           reuse_retained_old_region(EvacuationInfo& evacuation_info,
                                                            OldGCAllocRegion* old,
                                                            HeapRegion** retained);

   virtual HeapWord* attempt_allocation_locked(size_t word_size, bool bot_updates, uint &node) = 0;
   virtual HeapWord* attempt_allocation_force(size_t word_size, bool bot_updates, uint node = G1NUMA::AnyNodeIndex) = 0;

   size_t used_unlocked() const {
     return _summary_bytes_used;
   }

   void increase_used(size_t bytes) {
     _summary_bytes_used += bytes;
   }

   void decrease_used(size_t bytes) {
     assert(_summary_bytes_used >= bytes,
            err_msg("invariant: _summary_bytes_used: " SIZE_FORMAT " should be >= bytes: " SIZE_FORMAT,
                _summary_bytes_used, bytes));
     _summary_bytes_used -= bytes;
   }

   void set_used(size_t bytes) {
     _summary_bytes_used = bytes;
   }

   virtual HeapRegion* new_heap_region(uint hrs_index,
                                       G1BlockOffsetSharedArray* sharedOffsetArray,
                                       MemRegion mr) {
     return new HeapRegion(hrs_index, sharedOffsetArray, mr);
   }
};

// The default allocator for G1.
class G1DefaultAllocator : public G1Allocator {
protected:
  // Alloc region used to satisfy mutator allocation requests.
  MutatorAllocRegion* _mutator_alloc_regions;

  // Alloc region used to satisfy allocation requests by the GC for
  // survivor objects.
  SurvivorGCAllocRegion* _survivor_gc_alloc_regions;

  // Alloc region used to satisfy allocation requests by the GC for
  // old objects.
  OldGCAllocRegion _old_gc_alloc_region;

  HeapRegion* _retained_old_gc_alloc_region;

  G1NUMA* _numa;
    // The number of MutatorAllocRegions used, one per memory node.
  size_t _num_alloc_regions;

public:
  G1DefaultAllocator(G1CollectedHeap* heap);
  virtual ~G1DefaultAllocator();

  uint current_node_index() const;
  uint num_nodes() { return (uint)_num_alloc_regions; }

  virtual void init_mutator_alloc_regions();
  virtual void release_mutator_alloc_regions();

  virtual void init_gc_alloc_regions(EvacuationInfo& evacuation_info);
  virtual void release_gc_alloc_regions(uint no_of_gc_workers, EvacuationInfo& evacuation_info);
  virtual void abandon_gc_alloc_regions();

  virtual HeapWord* attempt_allocation_locked(size_t word_size, bool bot_updates, uint &node);
  virtual HeapWord* attempt_allocation_force(size_t word_size, bool bot_updates, uint node = G1NUMA::AnyNodeIndex);
  virtual bool is_retained_old_region(HeapRegion* hr) {
    return _retained_old_gc_alloc_region == hr;
  }

  virtual MutatorAllocRegion* mutator_alloc_region() {
    return &_mutator_alloc_regions[current_node_index()];
  }

  virtual SurvivorGCAllocRegion* survivor_gc_alloc_region() {
    return &_survivor_gc_alloc_regions[current_node_index()];
  }

  virtual MutatorAllocRegion* mutator_alloc_region(uint node_index) {
    assert(node_index < _num_alloc_regions, err_msg("Invalid index: %u", node_index));
    return &_mutator_alloc_regions[node_index];
  }

  virtual SurvivorGCAllocRegion* survivor_gc_alloc_region(uint node_index) {
    assert(node_index < _num_alloc_regions, err_msg("Invalid index: %u", node_index));
    return &_survivor_gc_alloc_regions[node_index];
  }

  virtual OldGCAllocRegion* old_gc_alloc_region(AllocationContext_t context) {
    return &_old_gc_alloc_region;
  }

  virtual size_t used() {
    assert(Heap_lock->owner() != NULL,
           "Should be owned on this thread's behalf.");
    size_t result = _summary_bytes_used;

    // Read only once in case it is set to NULL concurrently
    for (uint i = 0; i < _num_alloc_regions; i++) {
      HeapRegion* hr = mutator_alloc_region(i)->get();
      if (hr != NULL) {
        result += hr->used();
      }
    }
    return result;
  }
};

class G1ParGCAllocBuffer: public ParGCAllocBuffer {
private:
  bool _retired;

public:
  G1ParGCAllocBuffer(size_t gclab_word_size);
  virtual ~G1ParGCAllocBuffer() {
    guarantee(_retired, "Allocation buffer has not been retired");
  }

  virtual void set_buf(HeapWord* buf) {
    ParGCAllocBuffer::set_buf(buf);
    _retired = false;
  }

  virtual void retire(bool end_of_gc, bool retain) {
    if (_retired) {
      return;
    }
    ParGCAllocBuffer::retire(end_of_gc, retain);
    _retired = true;
  }
};

class G1ParGCAllocator : public CHeapObj<mtGC> {
  friend class G1ParScanThreadState;
protected:
  G1CollectedHeap* _g1h;

  typedef InCSetState::in_cset_state_t in_cset_state_t;
  // The survivor alignment in effect in bytes.
  // == 0 : don't align survivors
  // != 0 : align survivors to that alignment
  // These values were chosen to favor the non-alignment case since some
  // architectures have a special compare against zero instructions.
  const uint _survivor_alignment_bytes;

  size_t _alloc_buffer_waste;
  size_t _undo_waste;

  void add_to_alloc_buffer_waste(size_t waste) { _alloc_buffer_waste += waste; }
  void add_to_undo_waste(size_t waste)         { _undo_waste += waste; }

  virtual void retire_alloc_buffers() = 0;
  virtual G1ParGCAllocBuffer* alloc_buffer(InCSetState dest, AllocationContext_t context, uint node_index) = 0;

  // Returns the number of allocation buffers for the given dest.
  // There is only 1 buffer for Old while Young may have multiple buffers depending on
  // active NUMA nodes.
  inline uint alloc_buffers_length(in_cset_state_t dest) const;

  // Calculate the survivor space object alignment in bytes. Returns that or 0 if
  // there are no restrictions on survivor alignment.
  static uint calc_survivor_alignment_bytes() {
    assert(SurvivorAlignmentInBytes >= ObjectAlignmentInBytes, "sanity");
    if (SurvivorAlignmentInBytes == ObjectAlignmentInBytes) {
      // No need to align objects in the survivors differently, return 0
      // which means "survivor alignment is not used".
      return 0;
    } else {
      assert(SurvivorAlignmentInBytes > 0, "sanity");
      return SurvivorAlignmentInBytes;
    }
  }

  G1NUMA* _numa;
    // The number of MutatorAllocRegions used, one per memory node.
  size_t _num_alloc_regions;

public:
  G1ParGCAllocator(G1CollectedHeap* g1h);
  virtual ~G1ParGCAllocator() { }

  static G1ParGCAllocator* create_allocator(G1CollectedHeap* g1h);

  size_t alloc_buffer_waste() { return _alloc_buffer_waste; }
  size_t undo_waste() {return _undo_waste; }

  uint num_nodes() const { return (uint)_num_alloc_regions; }
  // Allocate word_sz words in dest, either directly into the regions or by
  // allocating a new PLAB. Returns the address of the allocated memory, NULL if
  // not successful.
  HeapWord* allocate_direct_or_new_plab(InCSetState dest,
                                        size_t word_sz,
                                        AllocationContext_t context,
                                        uint node_index);

  // Allocate word_sz words in the PLAB of dest.  Returns the address of the
  // allocated memory, NULL if not successful.
  HeapWord* plab_allocate(InCSetState dest,
                          size_t word_sz,
                          AllocationContext_t context,
                          uint node_index) {
    G1ParGCAllocBuffer* buffer = alloc_buffer(dest, context, node_index);
    if (_survivor_alignment_bytes == 0) {
      return buffer->allocate(word_sz);
    } else {
      return buffer->allocate_aligned(word_sz, _survivor_alignment_bytes);
    }
  }

  HeapWord* allocate(InCSetState dest, size_t word_sz,
                     AllocationContext_t context, uint node_index) {
    HeapWord* const obj = plab_allocate(dest, word_sz, context, node_index);
    if (obj != NULL) {
      return obj;
    }
    return allocate_direct_or_new_plab(dest, word_sz, context, node_index);
  }

  void undo_allocation(InCSetState dest, HeapWord* obj, size_t word_sz, AllocationContext_t context, uint node_index) {
    if (alloc_buffer(dest, context, node_index)->contains(obj)) {
      assert(alloc_buffer(dest, context, node_index)->contains(obj + word_sz - 1),
             "should contain whole object");
      alloc_buffer(dest, context, node_index)->undo_allocation(obj, word_sz);
    } else {
      CollectedHeap::fill_with_object(obj, word_sz);
      add_to_undo_waste(word_sz);
    }
  }
};

class G1DefaultParGCAllocator : public G1ParGCAllocator {
  G1ParGCAllocBuffer** _alloc_buffers[InCSetState::Num];

public:
  G1DefaultParGCAllocator(G1CollectedHeap* g1h);
  virtual ~G1DefaultParGCAllocator();

  virtual G1ParGCAllocBuffer* alloc_buffer(InCSetState dest, AllocationContext_t context, uint node_index) {
    assert(dest.is_valid(),
           err_msg("Allocation buffer index out-of-bounds: " CSETSTATE_FORMAT, dest.value()));
    assert(_alloc_buffers[dest.value()] != NULL,
           err_msg("Allocation buffer is NULL: " CSETSTATE_FORMAT, dest.value()));
    return alloc_buffer(dest.value(), node_index);
  }

  inline G1ParGCAllocBuffer* alloc_buffer(in_cset_state_t dest, uint node_index) const {
    assert(dest < InCSetState::Num, err_msg("Allocation buffer index out of bounds: %u", dest));

    if (dest == InCSetState::Young) {
      assert(node_index < alloc_buffers_length(dest),
           err_msg("Allocation buffer index out of bounds: %u, %u", dest, node_index));
      return _alloc_buffers[dest][node_index];
    } else {
      return _alloc_buffers[dest][0];
    }
  }

  inline uint alloc_buffers_length(in_cset_state_t dest) const {
    if (dest == InCSetState::Young) {
      return num_nodes();
    } else {
      return 1;
    }
  }

  virtual void retire_alloc_buffers() ;
};

#endif // SHARE_VM_GC_IMPLEMENTATION_G1_G1ALLOCATOR_HPP
