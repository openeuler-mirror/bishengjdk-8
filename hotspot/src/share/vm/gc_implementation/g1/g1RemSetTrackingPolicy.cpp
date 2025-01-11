/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
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
#include "gc_implementation/g1/collectionSetChooser.hpp"
#include "gc_implementation/g1/g1RemSetTrackingPolicy.hpp"
#include "gc_implementation/g1/heapRegion.inline.hpp"
#include "gc_implementation/g1/heapRegionRemSet.hpp"
#include "runtime/safepoint.hpp"

bool G1RemSetTrackingPolicy::is_interesting_humongous_region(HeapRegion* r) const {
  return r->startsHumongous() && oop(r->bottom())->is_typeArray();
}

bool G1RemSetTrackingPolicy::needs_scan_for_rebuild(HeapRegion* r) const {
  // All non-young and non-closed archive regions need to be scanned for references;
  // At every gc we gather references to other regions in young, and closed archive
  // regions by definition do not have references going outside the closed archive.
  return !(r->is_young() || r->is_free());
}

void G1RemSetTrackingPolicy::update_at_allocate(HeapRegion* r) {
  if (r->is_young()) {
    // Always collect remembered set for young regions.
    r->rem_set()->set_state_complete();
  } else if (r->isHumongous()) {
    // Collect remembered sets for humongous regions by default to allow eager reclaim.
    r->rem_set()->set_state_complete();
  } else if (r->is_old()) {
    // By default, do not create remembered set for new old regions.
    r->rem_set()->set_state_empty();
  } else {
    guarantee(false, err_msg("Unhandled region %u with heap region type %s", r->hrm_index(), r->get_type_str()));
  }
}

void G1RemSetTrackingPolicy::update_at_free(HeapRegion* r) {
  r->rem_set()->set_state_empty();
}

bool G1RemSetTrackingPolicy::update_before_rebuild(HeapRegion* r, size_t live_bytes) {
  assert(SafepointSynchronize::is_at_safepoint(), "should be at safepoint");

  bool selected_for_rebuild = false;

  // Only consider updating the remembered set for old gen regions - excluding archive regions
  // which never move (but are "Old" regions).
  if (r->is_old_or_humongous()) {
    size_t between_ntams_and_top = (r->top() - r->next_top_at_mark_start()) * HeapWordSize;
    size_t total_live_bytes = live_bytes + between_ntams_and_top;
    // Completely free regions after rebuild are of no interest wrt rebuilding the
    // remembered set.
    assert(!r->rem_set()->is_updating(), err_msg("Remembered set of region %u is updating before rebuild", r->hrm_index()));
    // To be of interest for rebuilding the remembered set the following must apply:
    // - They must contain some live data in them.
    // - We always try to update the remembered sets of humongous regions containing
    // type arrays if they are empty as they might have been reset after full gc.
    // - Only need to rebuild non-complete remembered sets.
    // - Otherwise only add those old gen regions which occupancy is low enough that there
    // is a chance that we will ever evacuate them in the mixed gcs.
    if ((total_live_bytes > 0) &&
        (is_interesting_humongous_region(r) || CollectionSetChooser::region_occupancy_low_enough_for_evac(total_live_bytes)) &&
        !r->rem_set()->is_tracked()) {

      r->rem_set()->set_state_updating();
      selected_for_rebuild = true;
    }
  }

  return selected_for_rebuild;
}

void G1RemSetTrackingPolicy::update_after_rebuild(HeapRegion* r) {
  assert(SafepointSynchronize::is_at_safepoint(), "should be at safepoint");

  if (r->is_old_or_humongous()) {
    if (r->rem_set()->is_updating()) {
      r->rem_set()->set_state_complete();
    }
    // We can drop remembered sets of humongous regions that have a too large remembered set:
    // We will never try to eagerly reclaim or move them anyway until the next concurrent
    // cycle as e.g. remembered set entries will always be added.
    if (r->isHumongous() && !G1CollectedHeap::heap()->is_potential_eager_reclaim_candidate(r)) {
      r->rem_set()->clear_locked(true /* only_cardset */);
    }
     assert(!r->continuesHumongous() || r->rem_set()->is_empty(), "Continues humongous object remsets should be empty");
  }
}

