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

#include "precompiled.hpp"
#include "gc_implementation/g1/concurrentG1Refine.hpp"
#include "gc_implementation/g1/concurrentG1RefineThread.hpp"
#include "gc_implementation/g1/g1BlockOffsetTable.inline.hpp"
#include "gc_implementation/g1/g1CollectedHeap.inline.hpp"
#include "gc_implementation/g1/g1CollectorPolicy.hpp"
#include "gc_implementation/g1/g1HotCardCache.hpp"
#include "gc_implementation/g1/g1GCPhaseTimes.hpp"
#include "gc_implementation/g1/g1OopClosures.inline.hpp"
#include "gc_implementation/g1/g1RemSet.inline.hpp"
#include "gc_implementation/g1/heapRegionManager.inline.hpp"
#include "gc_implementation/g1/heapRegionRemSet.hpp"
#include "memory/iterator.hpp"
#include "oops/oop.inline.hpp"
#include "utilities/intHisto.hpp"

PRAGMA_FORMAT_MUTE_WARNINGS_FOR_GCC

#define CARD_REPEAT_HISTO 0

#if CARD_REPEAT_HISTO
static size_t ct_freq_sz;
static jbyte* ct_freq = NULL;

void init_ct_freq_table(size_t heap_sz_bytes) {
  if (ct_freq == NULL) {
    ct_freq_sz = heap_sz_bytes/CardTableModRefBS::card_size;
    ct_freq = new jbyte[ct_freq_sz];
    for (size_t j = 0; j < ct_freq_sz; j++) ct_freq[j] = 0;
  }
}

void ct_freq_note_card(size_t index) {
  assert(0 <= index && index < ct_freq_sz, "Bounds error.");
  if (ct_freq[index] < 100) { ct_freq[index]++; }
}

static IntHistogram card_repeat_count(10, 10);

void ct_freq_update_histo_and_reset() {
  for (size_t j = 0; j < ct_freq_sz; j++) {
    card_repeat_count.add_entry(ct_freq[j]);
    ct_freq[j] = 0;
  }

}
#endif

// Collects information about the overall remembered set scan progress during an evacuation.
class G1RemSetScanState : public CHeapObj<mtGC> {
 private:
  size_t _max_regions;

  // Scan progress for the remembered set of a single region. Transitions from
  // Unclaimed -> Claimed -> Complete.
  // At each of the transitions the thread that does the transition needs to perform
  // some special action once. This is the reason for the extra "Claimed" state.
  typedef jint G1RemsetIterState;

  static const G1RemsetIterState Unclaimed = 0; // The remembered set has not been scanned yet.
  static const G1RemsetIterState Claimed = 1;   // The remembered set is currently being scanned.
  static const G1RemsetIterState Complete = 2;  // The remembered set has been completely scanned.

  G1RemsetIterState volatile* _iter_states;
  // The current location where the next thread should continue scanning in a region's
  // remembered set.
  size_t volatile* _iter_claims;

 public:
  G1RemSetScanState() :
    _max_regions(0),
    _iter_states(NULL),
    _iter_claims(NULL),
    _scan_top(NULL) {

  }

  ~G1RemSetScanState() {
    if (_iter_states != NULL) {
      FREE_C_HEAP_ARRAY(G1RemsetIterState, _iter_states, mtGC);
    }
    if (_iter_claims != NULL) {
      FREE_C_HEAP_ARRAY(size_t, _iter_claims, mtGC);
    }
    if (_scan_top != NULL) {
      FREE_C_HEAP_ARRAY(HeapWord*, _scan_top, mtGC);
    }
  }

  void initialize(uint max_regions) {
    assert(_iter_states == NULL, "Must not be initialized twice");
    assert(_iter_claims == NULL, "Must not be initialized twice");
    _max_regions = max_regions;
    _iter_states = NEW_C_HEAP_ARRAY(G1RemsetIterState, max_regions, mtGC);
    _iter_claims = NEW_C_HEAP_ARRAY(size_t, max_regions, mtGC);
    _scan_top = NEW_C_HEAP_ARRAY(HeapWord*, max_regions, mtGC);
  }

  void reset() {
    for (uint i = 0; i < _max_regions; i++) {
      _iter_states[i] = Unclaimed;
    }

    G1ResetScanTopClosure cl(_scan_top);
    G1CollectedHeap::heap()->heap_region_iterate(&cl);

    memset((void*)_iter_claims, 0, _max_regions * sizeof(size_t));
  }

  // Attempt to claim the remembered set of the region for iteration. Returns true
  // if this call caused the transition from Unclaimed to Claimed.
  inline bool claim_iter(uint region) {
    assert(region < _max_regions, err_msg("Tried to access invalid region %u", region));
    if (_iter_states[region] != Unclaimed) {
      return false;
    }
    jint res = Atomic::cmpxchg(Claimed, (jint*)(&_iter_states[region]), Unclaimed);
    return (res == Unclaimed);
  }

  // Try to atomically sets the iteration state to "complete". Returns true for the
  // thread that caused the transition.
  inline bool set_iter_complete(uint region) {
    if (iter_is_complete(region)) {
      return false;
    }
    jint res = Atomic::cmpxchg(Complete, (jint*)(&_iter_states[region]), Claimed);
    return (res == Claimed);
  }

  // Returns true if the region's iteration is complete.
  inline bool iter_is_complete(uint region) const {
    assert(region < _max_regions, err_msg("Tried to access invalid region %u", region));
    return _iter_states[region] == Complete;
  }

  // The current position within the remembered set of the given region.
  inline size_t iter_claimed(uint region) const {
    assert(region < _max_regions, err_msg("Tried to access invalid region %u", region));
    return _iter_claims[region];
  }

  // Claim the next block of cards within the remembered set of the region with
  // step size.
  inline size_t iter_claimed_next(uint region, size_t step) {
    return Atomic::add(step, &_iter_claims[region]) - step;
  }

  HeapWord* scan_top(uint region_idx) const {
    return _scan_top[region_idx];
  }


  // Creates a snapshot of the current _top values at the start of collection to
  // filter out card marks that we do not want to scan.
  class G1ResetScanTopClosure : public HeapRegionClosure {
   private:
    HeapWord** _scan_top;
   public:
    G1ResetScanTopClosure(HeapWord** scan_top) : _scan_top(scan_top) { }

    virtual bool doHeapRegion(HeapRegion* r) {
      uint hrm_index = r->hrm_index();
      if (!r->in_collection_set() && r->is_old_or_humongous()) {
        _scan_top[hrm_index] = r->top();
      } else {
        _scan_top[hrm_index] = r->bottom();
      }
      return false;
    }
  };

  // For each region, contains the maximum top() value to be used during this garbage
  // collection. Subsumes common checks like filtering out everything but old and
  // humongous regions outside the collection set.
  // This is valid because we are not interested in scanning stray remembered set
  // entries from free or archive regions.
  HeapWord** _scan_top;
};

G1RemSet::G1RemSet(G1CollectedHeap* g1, CardTableModRefBS* ct_bs)
  : _g1(g1),
    _scan_state(new G1RemSetScanState()),
    _conc_refine_cards(0),
    _ct_bs(ct_bs), _g1p(_g1->g1_policy()),
    _cg1r(g1->concurrent_g1_refine()),
    _cards_scanned(NULL), _total_cards_scanned(0),
    _prev_period_summary()
{
  guarantee(n_workers() > 0, "There should be some workers");

  if (G1SummarizeRSetStats) {
    _prev_period_summary.initialize(this);
  }
}

G1RemSet::~G1RemSet() {
  if (_scan_state != NULL) {
    delete _scan_state;
  }
}

void CountNonCleanMemRegionClosure::do_MemRegion(MemRegion mr) {
  if (_g1->is_in_g1_reserved(mr.start())) {
    _n += (int) ((mr.byte_size() / CardTableModRefBS::card_size));
    if (_start_first == NULL) _start_first = mr.start();
  }
}

void G1RemSet::initialize(size_t capacity, uint max_regions) {
  _scan_state->initialize(max_regions);
}

class ScanRSClosure : public HeapRegionClosure {
  G1RemSetScanState* _scan_state;
  size_t _cards_done, _cards;
  G1CollectedHeap* _g1h;

  G1ScanObjsDuringScanRSClosure* _scan_objs_on_card_cl;
  CodeBlobClosure* _code_root_cl;

  G1BlockOffsetSharedArray* _bot_shared;
  G1SATBCardTableModRefBS *_ct_bs;

  double _strong_code_root_scan_time_sec;
  uint   _worker_i;
  int    _block_size;

public:
  ScanRSClosure(G1RemSetScanState* scan_state,
                G1ScanObjsDuringScanRSClosure* scan_obj_on_card,
                CodeBlobClosure* code_root_cl,
                uint worker_i) :
    _scan_state(scan_state),
    _scan_objs_on_card_cl(scan_obj_on_card),
    _code_root_cl(code_root_cl),
    _strong_code_root_scan_time_sec(0.0),
    _cards(0),
    _cards_done(0),
    _worker_i(worker_i)
  {
    _g1h = G1CollectedHeap::heap();
    _bot_shared = _g1h->bot_shared();
    _ct_bs = _g1h->g1_barrier_set();
    _block_size = MAX2<int>(G1RSetScanBlockSize, 1);
  }

  void scanCard(size_t index, HeapWord* card_start, HeapRegion *r) {
    MemRegion card_region(card_start, G1BlockOffsetSharedArray::N_words);
    MemRegion pre_gc_allocated(r->bottom(), _scan_state->scan_top(r->hrm_index()));
    MemRegion mr = pre_gc_allocated.intersection(card_region);
    if (!mr.is_empty() && !_ct_bs->is_card_claimed(index)) {
      // We make the card as "claimed" lazily (so races are possible
      // but they're benign), which reduces the number of duplicate
      // scans (the rsets of the regions in the cset can intersect).
      _ct_bs->set_card_claimed(index);
      _scan_objs_on_card_cl->set_region(r);
      r->oops_on_card_seq_iterate_careful<true>(mr, _scan_objs_on_card_cl);
      _cards_done++;
    }
  }


  void scan_strong_code_roots(HeapRegion* r) {
    double scan_start = os::elapsedTime();
    r->strong_code_roots_do(_code_root_cl);
    _strong_code_root_scan_time_sec += (os::elapsedTime() - scan_start);
  }

  bool doHeapRegion(HeapRegion* r) {
    assert(r->in_collection_set(), "should only be called on elements of CS.");
    uint region_idx = r->hrm_index();
    if (_scan_state->iter_is_complete(region_idx)) {
      return false;
    }
    if (_scan_state->claim_iter(region_idx)) {
      // If we ever free the collection set concurrently, we should also
      // clear the card table concurrently therefore we won't need to
      // add regions of the collection set to the dirty cards region.
      _g1h->push_dirty_cards_region(r);
    }

    HeapRegionRemSetIterator iter(r->rem_set());
    size_t card_index;

    // We claim cards in block so as to recude the contention. The block size is determined by
    // the G1RSetScanBlockSize parameter.
    size_t claimed_card_block = _scan_state->iter_claimed_next(region_idx, _block_size);
    for (size_t current_card = 0; iter.has_next(card_index); current_card++) {
      if (current_card >= claimed_card_block + _block_size) {
        claimed_card_block = _scan_state->iter_claimed_next(region_idx, _block_size);
      }
      if (current_card < claimed_card_block) {
        continue;
      }
      HeapWord* card_start = _g1h->bot_shared()->address_for_index(card_index);
#if 0
      gclog_or_tty->print("Rem set iteration yielded card [" PTR_FORMAT ", " PTR_FORMAT ").\n",
                          card_start, card_start + CardTableModRefBS::card_size_in_words);
#endif

      HeapRegion* card_region = _g1h->heap_region_containing(card_start);
      if (!_g1h->_hrm.is_available(card_region->hrm_index())) {
        continue;
      }
      _cards++;

      if (!card_region->is_on_dirty_cards_region_list()) {
        _g1h->push_dirty_cards_region(card_region);
      }

      // If the card is dirty, then we will scan it during updateRS.
      if (!card_region->in_collection_set() &&
          !_ct_bs->is_card_dirty(card_index)) {
        scanCard(card_index, card_start, card_region);
      }
    }
    if (_scan_state->set_iter_complete(region_idx)) {
      // Scan the strong code root list attached to the current region
      scan_strong_code_roots(r);
    }
    return false;
  }

  double strong_code_root_scan_time_sec() {
    return _strong_code_root_scan_time_sec;
  }

  size_t cards_done() { return _cards_done;}
  size_t cards_looked_up() { return _cards;}
};

void G1RemSet::scanRS(G1ParScanThreadState* pss,
                      CodeBlobClosure* code_root_cl,
                      uint worker_i) {
  double rs_time_start = os::elapsedTime();
  G1ScanObjsDuringScanRSClosure scan_cl(_g1, pss);
  ScanRSClosure cl(_scan_state, &scan_cl, code_root_cl, worker_i);

  HeapRegion *startRegion = _g1->start_cset_region_for_worker(worker_i);
  _g1->collection_set_iterate_from(startRegion, &cl);

  double scan_rs_time_sec = (os::elapsedTime() - rs_time_start)
                            - cl.strong_code_root_scan_time_sec();

  assert(_cards_scanned != NULL, "invariant");
  _cards_scanned[worker_i] = cl.cards_done();

  _g1p->phase_times()->record_time_secs(G1GCPhaseTimes::ScanRS, worker_i, scan_rs_time_sec);
  _g1p->phase_times()->record_time_secs(G1GCPhaseTimes::CodeRoots, worker_i, cl.strong_code_root_scan_time_sec());
}

// Closure used for updating RSets and recording references that
// point into the collection set. Only called during an
// evacuation pause.

class RefineRecordRefsIntoCSCardTableEntryClosure: public CardTableEntryClosure {
  G1RemSet* _g1rs;
  DirtyCardQueue* _into_cset_dcq;
  G1ScanObjsDuringUpdateRSClosure* _update_rs_cl;
public:
  RefineRecordRefsIntoCSCardTableEntryClosure(G1CollectedHeap* g1h,
                                              DirtyCardQueue* into_cset_dcq,
                                              G1ScanObjsDuringUpdateRSClosure* update_rs_cl) :
    _g1rs(g1h->g1_rem_set()), _into_cset_dcq(into_cset_dcq), _update_rs_cl(update_rs_cl)
  {}
  bool do_card_ptr(jbyte* card_ptr, uint worker_i) {
    // The only time we care about recording cards that
    // contain references that point into the collection set
    // is during RSet updating within an evacuation pause.
    // In this case worker_i should be the id of a GC worker thread.
    assert(SafepointSynchronize::is_at_safepoint(), "not during an evacuation pause");

    if (_g1rs->refine_card_during_gc(card_ptr, _update_rs_cl)) {
      // 'card_ptr' contains references that point into the collection
      // set. We need to record the card in the DCQS
      // (G1CollectedHeap::into_cset_dirty_card_queue_set())
      // that's used for that purpose.
      //
      // Enqueue the card
      _into_cset_dcq->enqueue(card_ptr);
    }
    return true;
  }
};

void G1RemSet::updateRS(DirtyCardQueue* into_cset_dcq,
                        G1ParScanThreadState* pss,
						uint worker_i) {
  G1ScanObjsDuringUpdateRSClosure update_rs_cl(_g1, pss, worker_i);
  RefineRecordRefsIntoCSCardTableEntryClosure into_cset_update_rs_cl(_g1, into_cset_dcq, &update_rs_cl);
  G1GCParPhaseTimesTracker x(_g1p->phase_times(), G1GCPhaseTimes::UpdateRS, worker_i);
  {
    // Apply the closure to the entries of the hot card cache.
    G1GCParPhaseTimesTracker y(_g1p->phase_times(), G1GCPhaseTimes::ScanHCC, worker_i);
    _g1->iterate_hcc_closure(&into_cset_update_rs_cl, worker_i);
  }
  // Apply the closure to all remaining log entries.
  _g1->iterate_dirty_card_closure(&into_cset_update_rs_cl, worker_i);
}

void G1RemSet::cleanupHRRS() {
  HeapRegionRemSet::cleanup();
}

void G1RemSet::oops_into_collection_set_do(G1ParScanThreadState* pss,
                                           CodeBlobClosure* code_root_cl,
                                           uint worker_i) {
#if CARD_REPEAT_HISTO
  ct_freq_update_histo_and_reset();
#endif

  // A DirtyCardQueue that is used to hold cards containing references
  // that point into the collection set. This DCQ is associated with a
  // special DirtyCardQueueSet (see g1CollectedHeap.hpp).  Under normal
  // circumstances (i.e. the pause successfully completes), these cards
  // are just discarded (there's no need to update the RSets of regions
  // that were in the collection set - after the pause these regions
  // are wholly 'free' of live objects. In the event of an evacuation
  // failure the cards/buffers in this queue set are passed to the
  // DirtyCardQueueSet that is used to manage RSet updates
  DirtyCardQueue into_cset_dcq(&_g1->into_cset_dirty_card_queue_set());

  assert((ParallelGCThreads > 0) || worker_i == 0, "invariant");

  updateRS(&into_cset_dcq, pss, worker_i);
  scanRS(pss, code_root_cl, worker_i);

}

void G1RemSet::prepare_for_oops_into_collection_set_do() {
  _g1->set_refine_cte_cl_concurrency(false);
  DirtyCardQueueSet& dcqs = JavaThread::dirty_card_queue_set();
  dcqs.concatenate_logs();

  _scan_state->reset();

  guarantee( _cards_scanned == NULL, "invariant" );
  _cards_scanned = NEW_C_HEAP_ARRAY(size_t, n_workers(), mtGC);
  for (uint i = 0; i < n_workers(); ++i) {
    _cards_scanned[i] = 0;
  }
  _total_cards_scanned = 0;
}

void G1RemSet::cleanup_after_oops_into_collection_set_do() {
  guarantee( _cards_scanned != NULL, "invariant" );
  _total_cards_scanned = 0;
  for (uint i = 0; i < n_workers(); ++i) {
    _total_cards_scanned += _cards_scanned[i];
  }
  FREE_C_HEAP_ARRAY(size_t, _cards_scanned, mtGC);
  _cards_scanned = NULL;
  // Cleanup after copy
  _g1->set_refine_cte_cl_concurrency(true);
  // Set all cards back to clean.
  _g1->cleanUpCardTable();

  DirtyCardQueueSet& into_cset_dcqs = _g1->into_cset_dirty_card_queue_set();
  int into_cset_n_buffers = into_cset_dcqs.completed_buffers_num();

  if (_g1->evacuation_failed()) {
    double restore_remembered_set_start = os::elapsedTime();

    // Restore remembered sets for the regions pointing into the collection set.
    // We just need to transfer the completed buffers from the DirtyCardQueueSet
    // used to hold cards that contain references that point into the collection set
    // to the DCQS used to hold the deferred RS updates.
    _g1->dirty_card_queue_set().merge_bufferlists(&into_cset_dcqs);
    _g1->g1_policy()->phase_times()->record_evac_fail_restore_remsets((os::elapsedTime() - restore_remembered_set_start) * 1000.0);
  }

  // Free any completed buffers in the DirtyCardQueueSet used to hold cards
  // which contain references that point into the collection.
  _g1->into_cset_dirty_card_queue_set().clear();
  assert(!_g1->into_cset_dirty_card_queue_set().completed_buffers_exist_dirty(),
         "all buffers should be freed");
}

inline void check_card_ptr(jbyte* card_ptr, CardTableModRefBS* ct_bs) {
#ifdef ASSERT
  G1CollectedHeap* g1 = G1CollectedHeap::heap();
  assert(g1->is_in_exact(ct_bs->addr_for(card_ptr)),
         err_msg("Card at " PTR_FORMAT " index " SIZE_FORMAT " representing heap"
                 " at " PTR_FORMAT " (%u) must be in committed heap",
                 p2i(card_ptr),
                 ct_bs->index_for(ct_bs->addr_for(card_ptr)),
                 p2i(ct_bs->addr_for(card_ptr)),
                 g1->addr_to_region(ct_bs->addr_for(card_ptr))));
#endif
}

G1Mux2Closure::G1Mux2Closure(OopClosure *c1, OopClosure *c2) :
  _c1(c1), _c2(c2) { }

// Returns true if the given card contains references that point
// into the collection set, if we're checking for such references;
// false otherwise.

void G1RemSet::refine_card_concurrently(jbyte *card_ptr, uint worker_i) {
  assert(!_g1->is_gc_active(), "Only call concurrently");
  check_card_ptr(card_ptr, _ct_bs);

  // If the card is no longer dirty, nothing to do.
  if (*card_ptr != CardTableModRefBS::dirty_card_val()) {
    // No need to return that this card contains refs that point
    // into the collection set.
    return ;
  }

  // Construct the region representing the card.
  HeapWord* start = _ct_bs->addr_for(card_ptr);
  // And find the region containing it.
  HeapRegion* r = _g1->heap_region_containing(start);

  // This check is needed for some uncommon cases where we should
  // ignore the card.
  //
  // The region could be young.  Cards for young regions are
  // distinctly marked (set to g1_young_gen), so the post-barrier will
  // filter them out.  However, that marking is performed
  // concurrently.  A write to a young object could occur before the
  // card has been marked young, slipping past the filter.
  //
  // The card could be stale, because the region has been freed since
  // the card was recorded. In this case the region type could be
  // anything.  If (still) free or (reallocated) young, just ignore
  // it.  If (reallocated) old or humongous, the later card trimming
  // and additional checks in iteration may detect staleness.  At
  // worst, we end up processing a stale card unnecessarily.
  //
  // In the normal (non-stale) case, the synchronization between the
  // enqueueing of the card and processing it here will have ensured
  // we see the up-to-date region type here.
  if (!r->is_old_or_humongous()) {
    return ;
  }

  // While we are processing RSet buffers during the collection, we
  // actually don't want to scan any cards on the collection set,
  // since we don't want to update remebered sets with entries that
  // point into the collection set, given that live objects from the
  // collection set are about to move and such entries will be stale
  // very soon. This change also deals with a reliability issue which
  // involves scanning a card in the collection set and coming across
  // an array that was being chunked and looking malformed. Note,
  // however, that if evacuation fails, we have to scan any objects
  // that were not moved and create any missing entries.
  if (r->in_collection_set()) {
    return ;
  }

  // The result from the hot card cache insert call is either:
  //   * pointer to the current card
  //     (implying that the current card is not 'hot'),
  //   * null
  //     (meaning we had inserted the card ptr into the "hot" card cache,
  //     which had some headroom),
  //   * a pointer to a "hot" card that was evicted from the "hot" cache.
  //

  G1HotCardCache* hot_card_cache = _cg1r->hot_card_cache();
  if (hot_card_cache->use_cache()) {
    assert(!SafepointSynchronize::is_at_safepoint(), "sanity");

    const jbyte* orig_card_ptr = card_ptr;
    card_ptr = hot_card_cache->insert(card_ptr);
    if (card_ptr == NULL) {
      // There was no eviction. Nothing to do.
      return ;
    } else if (card_ptr != orig_card_ptr) {
      // Original card was inserted and an old card was evicted.
      start = _ct_bs->addr_for(card_ptr);
      r = _g1->heap_region_containing(start);

      // Check whether the region formerly in the cache should be
      // ignored, as discussed earlier for the original card.  The
      // region could have been freed while in the cache.  The cset is
      // not relevant here, since we're in concurrent phase.
      if (!r->is_old_or_humongous()) {
        return ;
      }
    } // Else we still have the original card.
  }

  // Trim the region designated by the card to what's been allocated
  // in the region.  The card could be stale, or the card could cover
  // (part of) an object at the end of the allocated space and extend
  // beyond the end of allocation.

  // Non-humongous objects are only allocated in the old-gen during
  // GC, so if region is old then top is stable.  Humongous object
  // allocation sets top last; if top has not yet been set, this is
  // a stale card and we'll end up with an empty intersection.  If
  // this is not a stale card, the synchronization between the
  // enqueuing of the card and processing it here will have ensured
  // we see the up-to-date top here.
  HeapWord* scan_limit = r->top();

  if (scan_limit <= start) {
    // If the trimmed region is empty, the card must be stale.
    return ;
  }

  // Okay to clean and process the card now.  There are still some
  // stale card cases that may be detected by iteration and dealt with
  // as iteration failure.
  *const_cast<volatile jbyte*>(card_ptr) = CardTableModRefBS::clean_card_val();

  // This fence serves two purposes.  First, the card must be cleaned
  // before processing the contents.  Second, we can't proceed with
  // processing until after the read of top, for synchronization with
  // possibly concurrent humongous object allocation.  It's okay that
  // reading top and reading type were racy wrto each other.  We need
  // both set, in any order, to proceed.
  OrderAccess::fence();

  // Don't use addr_for(card_ptr + 1) which can ask for
  // a card beyond the heap.
  HeapWord* end = start + CardTableModRefBS::card_size_in_words;
  MemRegion dirty_region(start, MIN2(scan_limit, end));
  assert(!dirty_region.is_empty(), "sanity");

  G1ConcurrentRefineOopClosure conc_refine_cl(_g1, worker_i);

  // The region for the current card may be a young region. The
  // current card may have been a card that was evicted from the
  // card cache. When the card was inserted into the cache, we had
  // determined that its region was non-young. While in the cache,
  // the region may have been freed during a cleanup pause, reallocated
  // and tagged as young.
  //
  // We wish to filter out cards for such a region but the current
  // thread, if we're running concurrently, may "see" the young type
  // change at any time (so an earlier "is_young" check may pass or
  // fail arbitrarily). We tell the iteration code to perform this
  // filtering when it has been determined that there has been an actual
  // allocation in this region and making it safe to check the young type.

  bool card_processed = r->oops_on_card_seq_iterate_careful<false>(dirty_region, &conc_refine_cl);


  // If unable to process the card then we encountered an unparsable
  // part of the heap (e.g. a partially allocated object) while
  // processing a stale card.  Despite the card being stale, redirty
  // and re-enqueue, because we've already cleaned the card.  Without
  // this we could incorrectly discard a non-stale card.
  if (!card_processed) {
    // The card might have gotten re-dirtied and re-enqueued while we
    // worked.  (In fact, it's pretty likely.)
    if (*card_ptr != CardTableModRefBS::dirty_card_val()) {
      *card_ptr = CardTableModRefBS::dirty_card_val();
      MutexLockerEx x(Shared_DirtyCardQ_lock,
                      Mutex::_no_safepoint_check_flag);
      DirtyCardQueue* sdcq =
        JavaThread::dirty_card_queue_set().shared_dirty_card_queue();
      sdcq->enqueue(card_ptr);
    }
  } else {
    _conc_refine_cards++;
  }
}

bool G1RemSet::refine_card_during_gc(jbyte* card_ptr,
                                     G1ScanObjsDuringUpdateRSClosure* update_rs_cl) {
  assert(_g1->is_gc_active(), "Only call during GC");

  check_card_ptr(card_ptr, _ct_bs);

  // If the card is no longer dirty, nothing to do. This covers cards that were already
  // scanned as parts of the remembered sets.
  if (*card_ptr != CardTableModRefBS::dirty_card_val()) {
    // No need to return that this card contains refs that point
    // into the collection set.
    return false;
  }

  // During GC we can immediately clean the card since we will not re-enqueue stale
  // cards as we know they can be disregarded.
  *card_ptr = CardTableModRefBS::clean_card_val();

  // Construct the region representing the card.
  HeapWord* card_start = _ct_bs->addr_for(card_ptr);
  // And find the region containing it.
  HeapRegion* r = _g1->heap_region_containing(card_start);

  HeapWord* scan_limit = _scan_state->scan_top(r->hrm_index());

  if (scan_limit <= card_start) {
    // If the card starts above the area in the region containing objects to scan, skip it.
    return false;
  }

  // Don't use addr_for(card_ptr + 1) which can ask for
  // a card beyond the heap.
  HeapWord* card_end = card_start + CardTableModRefBS::card_size_in_words;
  MemRegion dirty_region(card_start, MIN2(scan_limit, card_end));
  assert(!dirty_region.is_empty(), "sanity");

#if CARD_REPEAT_HISTO
  init_ct_freq_table(_g1->max_capacity());
  ct_freq_note_card(_ct_bs->index_for(start));
#endif
  update_rs_cl->set_region(r);
  update_rs_cl->reset_has_refs_into_cset();

  bool card_processed = r->oops_on_card_seq_iterate_careful<true>(dirty_region, update_rs_cl);
  assert(card_processed, "must be");
  _conc_refine_cards++;

  return update_rs_cl->has_refs_into_cset();
}

void G1RemSet::print_periodic_summary_info(const char* header) {
  G1RemSetSummary current;
  current.initialize(this);

  _prev_period_summary.subtract_from(&current);
  print_summary_info(&_prev_period_summary, header);

  _prev_period_summary.set(&current);
}

void G1RemSet::print_summary_info() {
  G1RemSetSummary current;
  current.initialize(this);

  print_summary_info(&current, " Cumulative RS summary");
}

void G1RemSet::print_summary_info(G1RemSetSummary * summary, const char * header) {
  assert(summary != NULL, "just checking");

  if (header != NULL) {
    gclog_or_tty->print_cr("%s", header);
  }

#if CARD_REPEAT_HISTO
  gclog_or_tty->print_cr("\nG1 card_repeat count histogram: ");
  gclog_or_tty->print_cr("  # of repeats --> # of cards with that number.");
  card_repeat_count.print_on(gclog_or_tty);
#endif

  summary->print_on(gclog_or_tty);
}

void G1RemSet::prepare_for_verify() {
  if (G1HRRSFlushLogBuffersOnVerify &&
      (VerifyBeforeGC || VerifyAfterGC)
      &&  (!_g1->full_collection() || G1VerifyRSetsDuringFullGC)) {
    cleanupHRRS();
    _g1->set_refine_cte_cl_concurrency(false);
    if (SafepointSynchronize::is_at_safepoint()) {
      DirtyCardQueueSet& dcqs = JavaThread::dirty_card_queue_set();
      dcqs.concatenate_logs();
    }

    G1HotCardCache* hot_card_cache = _cg1r->hot_card_cache();
    bool use_hot_card_cache = hot_card_cache->use_cache();
    hot_card_cache->set_use_cache(false);

    DirtyCardQueue into_cset_dcq(&_g1->into_cset_dirty_card_queue_set());
    updateRS(&into_cset_dcq, NULL, 0);
    _g1->into_cset_dirty_card_queue_set().clear();

    hot_card_cache->set_use_cache(use_hot_card_cache);
    assert(JavaThread::dirty_card_queue_set().completed_buffers_num() == 0, "All should be consumed");
  }
}

class G1RebuildRemSetTask: public AbstractGangTask {
  // Aggregate the counting data that was constructed concurrently
  // with marking.
  class G1RebuildRemSetHeapRegionClosure : public HeapRegionClosure {
    ConcurrentMark* _cm;
    G1RebuildRemSetClosure _update_cl;

    void scan_for_references(oop const obj, MemRegion mr) {
      obj->oop_iterate(&_update_cl, mr);
    }

    void scan_for_references(oop const obj) {
      obj->oop_iterate(&_update_cl);
    }

    // A humongous object is live (with respect to the scanning) either
    // a) it is marked on the bitmap as such
    // b) its TARS is larger than nTAMS, i.e. has been allocated during marking.
    bool is_humongous_live(oop const humongous_obj, HeapWord* ntams, HeapWord* tars) const {
      return _cm->nextMarkBitMap()->isMarked(humongous_obj) || (tars > ntams);
    }

    // Rebuilds the remembered sets by scanning the objects that were allocated before
    // rebuild start in the given region, applying the given closure to each of these objects.
    // Uses the bitmap to get live objects in the area from [bottom, nTAMS), and all
    // objects from [nTAMS, TARS).
    // Returns the number of bytes marked in that region between bottom and nTAMS.
    size_t rebuild_rem_set_in_region(CMBitMap* const mark_bitmap, HeapRegion* hr, HeapWord* const top_at_rebuild_start) {
      size_t marked_bytes = 0;

      HeapWord* start = hr->bottom();
      HeapWord* const ntams = hr->next_top_at_mark_start();

      if (top_at_rebuild_start <= start) {
        return 0;
      }

      if (hr->isHumongous()) {
        oop const humongous_obj = oop(hr->humongous_start_region()->bottom());
        if (is_humongous_live(humongous_obj, ntams, top_at_rebuild_start)) {
          // We need to scan both [bottom, nTAMS) and [nTAMS, top_at_rebuild_start);
          // however in case of humongous objects it is sufficient to scan the encompassing
          // area (top_at_rebuild_start is always larger or equal to nTAMS) as one of the
          // two areas will be zero sized. I.e. nTAMS is either
          // the same as bottom or top(_at_rebuild_start). There is no way ntams has a different
          // value: this would mean that nTAMS points somewhere into the object.
          assert(hr->top() == hr->next_top_at_mark_start() || hr->top() == top_at_rebuild_start,
                 "More than one object in the humongous region?");
          scan_for_references(humongous_obj, MemRegion(start, top_at_rebuild_start));
          return ntams != start ? pointer_delta(hr->next_top_at_mark_start(), start, 1) : 0;
        } else {
          return 0;
        }
      }

      assert(start <= hr->end() && start <= ntams &&
             ntams <= top_at_rebuild_start && top_at_rebuild_start <= hr->end(),
             err_msg("Inconsistency between bottom, nTAMS, TARS, end - "
             "start: " PTR_FORMAT ", nTAMS: " PTR_FORMAT ", TARS: " PTR_FORMAT ", end: " PTR_FORMAT,
             p2i(start), p2i(ntams), p2i(top_at_rebuild_start), p2i(hr->end())));

      // Iterate live objects between bottom and nTAMS.
      start = mark_bitmap->getNextMarkedWordAddress(start, ntams);
      while (start < ntams) {
        oop obj = oop(start);

        size_t obj_size = obj->size();
        HeapWord* obj_end = start + obj_size;

        assert(obj_end <= hr->end(), "Humongous objects must have been handled elsewhere.");

        scan_for_references(obj);

        // Add the size of this object to the number of marked bytes.
        marked_bytes += obj_size;

        // Find the next marked object after this one.
        start = mark_bitmap->getNextMarkedWordAddress(obj_end, ntams);
      }

      // Finally process live objects (all of them) between nTAMS and top_at_rebuild_start.
      // Objects between top_at_rebuild_start and top are implicitly managed by concurrent refinement.
      while (start < top_at_rebuild_start) {
        oop obj = oop(start);
        size_t obj_size = obj->size();
        HeapWord* obj_end = start + obj_size;

        assert(obj_end <= hr->end(), "Humongous objects must have been handled elsewhere.");

        scan_for_references(obj);
        start = obj_end;
      }
      return marked_bytes * HeapWordSize;
    }
   public:
    G1RebuildRemSetHeapRegionClosure(G1CollectedHeap* g1h,
                                     ConcurrentMark* cm,
                                     uint worker_id) :
      HeapRegionClosure(),
      _cm(cm),
      _update_cl(g1h, worker_id) { }

    bool doHeapRegion(HeapRegion* hr) {
      if (_cm->has_aborted()) {
        return true;
      }
      uint const region_idx = hr->hrm_index();
      HeapWord* const top_at_rebuild_start = _cm->top_at_rebuild_start(region_idx);
      // TODO: smaller increments to do yield checks with
      size_t marked_bytes = rebuild_rem_set_in_region(_cm->nextMarkBitMap(), hr, top_at_rebuild_start);
      if (marked_bytes > 0) {
        hr->add_to_marked_bytes(marked_bytes);
        assert(!hr->is_old() || marked_bytes == (_cm->liveness(hr->hrm_index()) * HeapWordSize),
               err_msg("Marked bytes " SIZE_FORMAT " for region %u do not match liveness during mark " SIZE_FORMAT,
               marked_bytes, hr->hrm_index(), _cm->liveness(hr->hrm_index()) * HeapWordSize));
      }
      _cm->do_yield_check();
      // Abort state may have changed after the yield check.
      return _cm->has_aborted();
    }
  };

  HeapRegionClaimer _hr_claimer;
  ConcurrentMark* _cm;

  uint _worker_id_offset;
 public:
  G1RebuildRemSetTask(ConcurrentMark* cm,
                      uint n_workers,
                      uint worker_id_offset) :
    AbstractGangTask("G1 Rebuild Remembered Set"),
    _cm(cm),
    _hr_claimer(n_workers),
    _worker_id_offset(worker_id_offset) {
  }

  void work(uint worker_id) {
    SuspendibleThreadSetJoiner sts_join;

    G1CollectedHeap* g1h = G1CollectedHeap::heap();

    G1RebuildRemSetHeapRegionClosure cl(g1h, _cm, _worker_id_offset + worker_id);
    g1h->heap_region_par_iterate_chunked(&cl, worker_id, &_hr_claimer);
  }
};

void G1RemSet::rebuild_rem_set(ConcurrentMark* cm,
                               FlexibleWorkGang* workers,
                               bool use_parallel,
                               uint num_workers,
                               uint worker_id_offset) {
  G1RebuildRemSetTask cl(cm,
                         num_workers,
                         worker_id_offset);
  if (use_parallel) {
    workers->set_active_workers((int) num_workers);
    workers->run_task(&cl);
  } else {
    cl.work(0);
  }
}