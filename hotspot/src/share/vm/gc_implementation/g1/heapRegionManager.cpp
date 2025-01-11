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
#include "gc_implementation/g1/heapRegion.hpp"
#include "gc_implementation/g1/heapRegionManager.inline.hpp"
#include "gc_implementation/g1/heapRegionSet.inline.hpp"
#include "gc_implementation/g1/g1CollectedHeap.inline.hpp"
#include "gc_implementation/g1/g1NUMA.hpp"
#include "gc_implementation/g1/concurrentG1Refine.hpp"
#include "memory/allocation.hpp"

HeapRegionClaimer::HeapRegionClaimer(uint n_workers) :
  _n_workers(n_workers), _n_regions(G1CollectedHeap::heap()->_hrm._allocated_heapregions_length), _claims(NULL) {
  uint* new_claims = NEW_C_HEAP_ARRAY(uint, _n_regions, mtGC);
  memset(new_claims, Unclaimed, sizeof(*_claims) * _n_regions);
  _claims = new_claims;
}

HeapRegionClaimer::~HeapRegionClaimer() {
  if (_claims != NULL) {
    FREE_C_HEAP_ARRAY(uint, _claims, mtGC);
  }
}

uint HeapRegionClaimer::offset_for_worker(uint worker_id) const {
  assert(worker_id < _n_workers, "Invalid worker_id.");
  return _n_regions * worker_id / _n_workers;
}

bool HeapRegionClaimer::is_region_claimed(uint region_index) const {
  assert(region_index < _n_regions, "Invalid index.");
  return _claims[region_index] == Claimed;
}

bool HeapRegionClaimer::claim_region(uint region_index) {
  assert(region_index < _n_regions, "Invalid index.");
  uint old_val = Atomic::cmpxchg(Claimed, &_claims[region_index], Unclaimed);
  return old_val == Unclaimed;
}


void HeapRegionManager::initialize(G1RegionToSpaceMapper* heap_storage,
                               G1RegionToSpaceMapper* prev_bitmap,
                               G1RegionToSpaceMapper* next_bitmap,
                               G1RegionToSpaceMapper* bot,
                               G1RegionToSpaceMapper* cardtable,
                               G1RegionToSpaceMapper* card_counts) {
  _allocated_heapregions_length = 0;

  _heap_mapper = heap_storage;

  _prev_bitmap_mapper = prev_bitmap;
  _next_bitmap_mapper = next_bitmap;

  _bot_mapper = bot;
  _cardtable_mapper = cardtable;

  _card_counts_mapper = card_counts;

  MemRegion reserved = heap_storage->reserved();
  _regions.initialize(reserved.start(), reserved.end(), HeapRegion::GrainBytes);

  _available_map.resize(_regions.length(), false);
  _available_map.clear();
  _uncommit_list_filled = false;
}

bool HeapRegionManager::is_available(uint region) const {
  HeapRegion* hr = _regions.get_by_index(region);
  if (hr != NULL && hr->in_uncommit_list()) {
    return false;
  }
  return _available_map.at(region);
}

bool HeapRegionManager::can_expand(uint region) const {
  HeapRegion* hr = _regions.get_by_index(region);
  if (hr != NULL && hr->in_uncommit_list()) {
    return false;
  }
  return !_available_map.at(region);
}

HeapRegion* HeapRegionManager::allocate_free_region(bool is_old, uint requested_node_index) {
  HeapRegion* hr = NULL;
  bool from_head = is_old;
  G1NUMA* numa = G1NUMA::numa();

  if (requested_node_index != G1NUMA::AnyNodeIndex && numa->is_enabled()) {
    // Try to allocate with requested node index.
    hr = _free_list.remove_region_with_node_index(from_head, requested_node_index);
  }

  if (hr == NULL) {
    // If there's a single active node or we did not get a region from our requested node,
    // try without requested node index.
    hr = _free_list.remove_region(from_head);
  }

  if (hr != NULL) {
    assert(hr->next() == NULL, "Single region should not have next");
    assert(is_available(hr->hrm_index()), "Must be committed");

    if (numa->is_enabled() && hr->node_index() < numa->num_active_nodes()) {
      numa->update_statistics(G1NUMAStats::NewRegionAlloc, requested_node_index, hr->node_index());
    }
  }

  return hr;
}

#ifdef ASSERT
bool HeapRegionManager::is_free(HeapRegion* hr) const {
  return _free_list.contains(hr);
}
#endif

HeapRegion* HeapRegionManager::new_heap_region(uint hrm_index) {
  G1CollectedHeap* g1h = G1CollectedHeap::heap();
  HeapWord* bottom = g1h->bottom_addr_for_region(hrm_index);
  MemRegion mr(bottom, bottom + HeapRegion::GrainWords);
  assert(reserved().contains(mr), "invariant");
  return g1h->allocator()->new_heap_region(hrm_index, g1h->bot_shared(), mr);
}

void HeapRegionManager::commit_regions(uint index, size_t num_regions) {
  guarantee(num_regions > 0, "Must commit more than zero regions");
  guarantee(_num_committed + num_regions <= max_length(), "Cannot commit more than the maximum amount of regions");

  Atomic::add((int)num_regions, (volatile int*)&_num_committed);

  _heap_mapper->commit_regions(index, num_regions);

  // Also commit auxiliary data
  _prev_bitmap_mapper->commit_regions(index, num_regions);
  _next_bitmap_mapper->commit_regions(index, num_regions);

  _bot_mapper->commit_regions(index, num_regions);
  _cardtable_mapper->commit_regions(index, num_regions);

  _card_counts_mapper->commit_regions(index, num_regions);
}

void HeapRegionManager::uncommit_regions(uint start, size_t num_regions) {
  guarantee(num_regions >= 1, err_msg("Need to specify at least one region to uncommit, tried to uncommit zero regions at %u", start));
  guarantee(_num_committed >= num_regions, "pre-condition");
  // Reset node index to distinguish with committed regions.
  for (uint i = start; i < start + num_regions; i++) {
    at(i)->set_node_index(G1NUMA::UnknownNodeIndex);
  }

  // Print before uncommitting.
  if (G1CollectedHeap::heap()->hr_printer()->is_active()) {
    for (uint i = start; i < start + num_regions; i++) {
      HeapRegion* hr = at(i);
      G1CollectedHeap::heap()->hr_printer()->uncommit(hr->bottom(), hr->end());
    }
  }

  Atomic::add(-num_regions, (volatile int*)&_num_committed);
  _available_map.par_clear_range(start, start + num_regions, BitMap::unknown_range);

  _heap_mapper->uncommit_regions(start, num_regions);

  // Also uncommit auxiliary data
  _prev_bitmap_mapper->uncommit_regions(start, num_regions);
  _next_bitmap_mapper->uncommit_regions(start, num_regions);

  _bot_mapper->uncommit_regions(start, num_regions);
  _cardtable_mapper->uncommit_regions(start, num_regions);

  _card_counts_mapper->uncommit_regions(start, num_regions);
}

void HeapRegionManager::make_regions_available(uint start, uint num_regions) {
  guarantee(num_regions > 0, "No point in calling this for zero regions");
  commit_regions(start, num_regions);
  for (uint i = start; i < start + num_regions; i++) {
    if (_regions.get_by_index(i) == NULL) {
      HeapRegion* new_hr = new_heap_region(i);
      OrderAccess::storestore();
      _regions.set_by_index(i, new_hr);
      _allocated_heapregions_length = MAX2(_allocated_heapregions_length, i + 1);
    }
  }

  _available_map.par_set_range(start, start + num_regions, BitMap::unknown_range);

  for (uint i = start; i < start + num_regions; i++) {
    assert(is_available(i), err_msg("Just made region %u available but is apparently not.", i));
    HeapRegion* hr = at(i);
    if (G1CollectedHeap::heap()->hr_printer()->is_active()) {
      G1CollectedHeap::heap()->hr_printer()->commit(hr->bottom(), hr->end());
    }
    HeapWord* bottom = G1CollectedHeap::heap()->bottom_addr_for_region(i);
    MemRegion mr(bottom, bottom + HeapRegion::GrainWords);

    hr->initialize(mr);
    hr->set_node_index(G1NUMA::numa()->index_for_region(hr));
    insert_into_free_list(at(i));
  }
}

MemoryUsage HeapRegionManager::get_auxiliary_data_memory_usage() const {
  size_t used_sz =
    _prev_bitmap_mapper->committed_size() +
    _next_bitmap_mapper->committed_size() +
    _bot_mapper->committed_size() +
    _cardtable_mapper->committed_size() +
    _card_counts_mapper->committed_size();

  size_t committed_sz =
    _prev_bitmap_mapper->reserved_size() +
    _next_bitmap_mapper->reserved_size() +
    _bot_mapper->reserved_size() +
    _cardtable_mapper->reserved_size() +
    _card_counts_mapper->reserved_size();

  return MemoryUsage(0, used_sz, committed_sz, committed_sz);
}

uint HeapRegionManager::expand_by(uint num_regions) {
  return expand_at(0, num_regions);
}

uint HeapRegionManager::expand_at(uint start, uint num_regions) {
  if (num_regions == 0) {
    return 0;
  }

  uint cur = start;
  uint idx_last_found = 0;
  uint num_last_found = 0;

  uint expanded = 0;

  while (expanded < num_regions &&
         (num_last_found = find_unavailable_from_idx(cur, &idx_last_found)) > 0) {
    uint to_expand = MIN2(num_regions - expanded, num_last_found);
    make_regions_available(idx_last_found, to_expand);
    expanded += to_expand;
    cur = idx_last_found + num_last_found + 1;
  }

  verify_optional();
  return expanded;
}

uint HeapRegionManager::expand_on_preferred_node(uint preferred_index) {
  uint expand_candidate = UINT_MAX;
  for (uint i = 0; i < max_length(); i++) {
    if (!can_expand(i)) {
      // Already in use or in uncommit list continue
      continue;
    }
    // Always save the candidate so we can expand later on.
    expand_candidate = i;
    if (is_on_preferred_index(expand_candidate, preferred_index)) {
      // We have found a candidate on the preffered node, break.
      break;
    }
  }

  if (expand_candidate == UINT_MAX) {
     // No regions left, expand failed.
    return 0;
  }

  make_regions_available(expand_candidate, 1);
  return 1;
}

bool HeapRegionManager::is_on_preferred_index(uint region_index, uint preferred_node_index) {
  uint region_node_index = G1NUMA::numa()->preferred_node_index_for_index(region_index);
  return region_node_index == preferred_node_index;
}

uint HeapRegionManager::find_contiguous(size_t num, bool empty_only) {
  uint found = 0;
  size_t length_found = 0;
  uint cur = 0;

  while (length_found < num && cur < max_length()) {
    HeapRegion* hr = _regions.get_by_index(cur);
    if ((!empty_only && can_expand(cur)) || (is_available(cur) && hr != NULL && hr->is_empty())) {
      // This region is a potential candidate for allocation into.
      length_found++;
    } else {
      // This region is not a candidate. The next region is the next possible one.
      found = cur + 1;
      length_found = 0;
    }
    cur++;
  }

  if (length_found == num) {
    for (uint i = found; i < (found + num); i++) {
      HeapRegion* hr = _regions.get_by_index(i);
      // sanity check
      guarantee((!empty_only && can_expand(i)) || (is_available(i) && hr != NULL && hr->is_empty()),
                err_msg("Found region sequence starting at " UINT32_FORMAT ", length " SIZE_FORMAT
                        " that is not empty at " UINT32_FORMAT ". Hr is " PTR_FORMAT, found, num, i, p2i(hr)));
    }
    return found;
  } else {
    return G1_NO_HRM_INDEX;
  }
}

HeapRegion* HeapRegionManager::next_region_in_heap(const HeapRegion* r) const {
  guarantee(r != NULL, "Start region must be a valid region");
  guarantee(is_available(r->hrm_index()), err_msg("Trying to iterate starting from region %u which is not in the heap", r->hrm_index()));
  for (uint i = r->hrm_index() + 1; i < _allocated_heapregions_length; i++) {
    HeapRegion* hr = _regions.get_by_index(i);
    if (is_available(i)) {
      return hr;
    }
  }
  return NULL;
}

void HeapRegionManager::iterate(HeapRegionClosure* blk) const {
  uint len = max_length();

  for (uint i = 0; i < len; i++) {
    HeapRegion* r = _regions.get_by_index(i);
    if (r != NULL && r->in_uncommit_list() || !_available_map.at(i)) {
      continue;
    }
    guarantee(at(i) != NULL, err_msg("Tried to access region %u that has a NULL HeapRegion*", i));
    bool res = blk->doHeapRegion(at(i));
    if (res) {
      blk->incomplete();
      return;
    }
  }
}

uint HeapRegionManager::find_unavailable_from_idx(uint start_idx, uint* res_idx) const {
  guarantee(res_idx != NULL, "checking");
  guarantee(start_idx <= (max_length() + 1), "checking");

  uint num_regions = 0;

  uint cur = start_idx;
  while (cur < max_length() && is_available(cur)) {
    cur++;
  }
  if (cur == max_length()) {
    return num_regions;
  }
  *res_idx = cur;
  while (cur < max_length() && can_expand(cur)) {
    cur++;
  }
  num_regions = cur - *res_idx;
#ifdef ASSERT
  for (uint i = *res_idx; i < (*res_idx + num_regions); i++) {
    assert(can_expand(i), "just checking");
  }
  assert(cur == max_length() || num_regions == 0 || (!G1Uncommit && is_available(cur)) || G1Uncommit,
         err_msg("The region at the current position %u must be available or at the end of the heap.", cur));
#endif
  return num_regions;
}

uint HeapRegionManager::start_region_for_worker(uint worker_i, uint num_workers, uint num_regions) const {
  return num_regions * worker_i / num_workers;
}

void HeapRegionManager::par_iterate(HeapRegionClosure* blk, uint worker_id, HeapRegionClaimer* hrclaimer) const {
  const uint start_index = hrclaimer->offset_for_worker(worker_id);

  // Every worker will actually look at all regions, skipping over regions that
  // are currently not committed.
  // This also (potentially) iterates over regions newly allocated during GC. This
  // is no problem except for some extra work.
  const uint n_regions = hrclaimer->n_regions();
  for (uint count = 0; count < n_regions; count++) {
    const uint index = (start_index + count) % n_regions;
    assert(0 <= index && index < n_regions, "sanity");
    // Skip over unavailable regions
    HeapRegion* r = _regions.get_by_index(index);
    if (r != NULL && r->in_uncommit_list() || !_available_map.at(index)) {
      continue;
    }
    // We'll ignore regions already claimed.
    if (hrclaimer->is_region_claimed(index)) {
      continue;
    }
    // OK, try to claim it
    if (!hrclaimer->claim_region(index)) {
      continue;
    }
    bool res = blk->doHeapRegion(r);
    if (res) {
      return;
    }
  }
}

uint HeapRegionManager::shrink_by(uint num_regions_to_remove) {
  assert(length() > 0, "the region sequence should not be empty");
  assert(length() <= _allocated_heapregions_length, "invariant");
  assert(_allocated_heapregions_length > 0, "we should have at least one region committed");
  assert(num_regions_to_remove < length(), "We should never remove all regions");

  if (num_regions_to_remove == 0) {
    return 0;
  }

  uint removed = 0;
  uint cur = _allocated_heapregions_length - 1;
  uint idx_last_found = 0;
  uint num_last_found = 0;

  while ((removed < num_regions_to_remove) &&
      (num_last_found = find_empty_from_idx_reverse(cur, &idx_last_found)) > 0) {
    uint to_remove = MIN2(num_regions_to_remove - removed, num_last_found);

    uncommit_regions(idx_last_found + num_last_found - to_remove, to_remove);

    cur -= num_last_found;
    removed += to_remove;
  }

  verify_optional();

  return removed;
}

uint HeapRegionManager::find_empty_from_idx_reverse(uint start_idx, uint* res_idx) const {
  guarantee(start_idx < _allocated_heapregions_length, "checking");
  guarantee(res_idx != NULL, "checking");

  uint num_regions_found = 0;

  jlong cur = start_idx;
  while (cur != -1 && !(is_available(cur) && at(cur)->is_empty())) {
    cur--;
  }
  if (cur == -1) {
    return num_regions_found;
  }
  jlong old_cur = cur;
  // cur indexes the first empty region
  while (cur != -1 && is_available(cur) && at(cur)->is_empty()) {
    cur--;
  }
  *res_idx = cur + 1;
  num_regions_found = old_cur - cur;

#ifdef ASSERT
  for (uint i = *res_idx; i < (*res_idx + num_regions_found); i++) {
    assert(at(i)->is_empty(), "just checking");
  }
#endif
  return num_regions_found;
}

void HeapRegionManager::verify() {
  guarantee(length() <= _allocated_heapregions_length,
            err_msg("invariant: _length: %u _allocated_length: %u",
                    length(), _allocated_heapregions_length));
  guarantee(_allocated_heapregions_length <= max_length(),
            err_msg("invariant: _allocated_length: %u _max_length: %u",
                    _allocated_heapregions_length, max_length()));

  bool prev_committed = true;
  uint num_committed = 0;
  HeapWord* prev_end = heap_bottom();
  for (uint i = 0; i < _allocated_heapregions_length; i++) {
    HeapRegion* hr = _regions.get_by_index(i);
    if (hr != NULL && hr->in_uncommit_list() || !_available_map.at(i)) {
      prev_committed = false;
      continue;
    }
    num_committed++;
    guarantee(hr != NULL, err_msg("invariant: i: %u", i));
    guarantee(!prev_committed || hr->bottom() == prev_end,
              err_msg("invariant i: %u " HR_FORMAT " prev_end: " PTR_FORMAT,
                      i, HR_FORMAT_PARAMS(hr), p2i(prev_end)));
    guarantee(hr->hrm_index() == i,
              err_msg("invariant: i: %u hrm_index(): %u", i, hr->hrm_index()));
    // Asserts will fire if i is >= _length
    HeapWord* addr = hr->bottom();
    guarantee(addr_to_region(addr) == hr, "sanity");
    // We cannot check whether the region is part of a particular set: at the time
    // this method may be called, we have only completed allocation of the regions,
    // but not put into a region set.
    prev_committed = true;
    prev_end = hr->end();
  }
  for (uint i = _allocated_heapregions_length; i < max_length(); i++) {
    guarantee(_regions.get_by_index(i) == NULL, err_msg("invariant i: %u", i));
  }

  guarantee((!G1Uncommit && num_committed == _num_committed) || G1Uncommit, err_msg("Found %u committed regions, but should be %u", num_committed, _num_committed));
  _free_list.verify();
}

void HeapRegionManager::free_uncommit_list_memory() {
  if (_uncommit_list_filled) {
    _uncommit_list.remove_all(true);
    OrderAccess::storestore();
    _uncommit_list_filled = false;
  }
}

uint HeapRegionManager::extract_uncommit_list(uint num_candidate_to_remove) {
  assert_at_safepoint(true /* should_be_vm_thread */);
  double start_up_sec = os::elapsedTime();
  if (start_up_sec < G1UncommitDelay) {
    if (G1UncommitLog) {
      gclog_or_tty->date_stamp(PrintGCDateStamps);
      gclog_or_tty->stamp(PrintGCTimeStamps);
      gclog_or_tty->print_cr("start up seconds:%lf, less than G1UncommitDelay, will not uncommit.", start_up_sec);
    }
    return 0;
  }

  if (!_uncommit_list_filled) {
    uint num_regions_to_remove = num_candidate_to_remove * G1UncommitPercent;
    if (num_regions_to_remove >= 1 && num_regions_to_remove < _free_list.length()) {
      int count = _free_list.move_regions_to(&_uncommit_list, num_regions_to_remove);
      OrderAccess::storestore();
      _uncommit_list_filled = true;
      return count;
    }
  }
  return 0;
}

#ifndef PRODUCT
void HeapRegionManager::verify_optional() {
  verify();
}
#endif // PRODUCT

