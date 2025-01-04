/*
 * Copyright (c) 2001, 2018, Oracle and/or its affiliates. All rights reserved.
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
#include "classfile/javaClasses.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/vmSymbols.hpp"
#include "code/codeCache.hpp"
#include "code/icBuffer.hpp"
#include "gc_implementation/g1/g1FullGCScope.hpp"
#include "gc_implementation/g1/g1Log.hpp"
#include "gc_implementation/g1/g1MarkSweep.hpp"
#include "gc_implementation/g1/g1RootProcessor.hpp"
#include "gc_implementation/g1/g1StringDedup.hpp"
#include "gc_implementation/shared/gcHeapSummary.hpp"
#include "gc_implementation/shared/liveRange.hpp"
#include "gc_implementation/shared/gcTimer.hpp"
#include "gc_implementation/shared/gcTrace.hpp"
#include "gc_implementation/shared/gcTraceTime.hpp"
#include "memory/gcLocker.hpp"
#include "memory/genCollectedHeap.hpp"
#include "memory/modRefBarrierSet.hpp"
#include "memory/referencePolicy.hpp"
#include "memory/space.hpp"
#include "oops/instanceRefKlass.hpp"
#include "oops/oop.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/fprofiler.hpp"
#include "runtime/synchronizer.hpp"
#include "runtime/thread.hpp"
#include "runtime/vmThread.hpp"
#include "utilities/copy.hpp"
#include "utilities/events.hpp"
#if INCLUDE_JFR
#include "jfr/jfr.hpp"
#endif // INCLUDE_JFR

class HeapRegion;


class G1FullGCCompactionPoint : public CompactPoint {
  HeapRegion*  _current_region;
  HeapWord*   _threshold;
  HeapWord*   _compaction_top;
  GrowableArray<HeapRegion*>* _compaction_regions;
  GrowableArrayIterator<HeapRegion*> _compaction_region_iterator;
  GrowableArray<HeapRegion*>* _marked_huge_regions;

  virtual HeapRegion* next_compaction_space() {
    HeapRegion* next = *(++_compaction_region_iterator);
    assert(next != NULL, "Must return valid region");
    return next;
  }

public:
  G1FullGCCompactionPoint() :
    _current_region(NULL),
    _threshold(NULL),
    _compaction_top(NULL),
    _compaction_regions(new (ResourceObj::C_HEAP, mtGC)
                          GrowableArray<HeapRegion*>(32/* initial size */, true, mtGC)),
    _compaction_region_iterator(_compaction_regions->begin()),
    _marked_huge_regions(new (ResourceObj::C_HEAP, mtGC)
                           GrowableArray<HeapRegion*>(32/* initial size */, true, mtGC)) {
  }
  virtual ~G1FullGCCompactionPoint() {
    delete _compaction_regions;
    delete _marked_huge_regions;
  }

  bool is_initialized() {
    return _current_region != NULL;
  }

  void initialize(HeapRegion* hr, bool init_threshold) {
    _current_region = hr;
    initialize_values(init_threshold);
  }

  void initialize_values(bool init_threshold) {
    _compaction_top = _current_region->compaction_top();
    if (init_threshold) {
      _threshold = _current_region->initialize_threshold();
    }
  }

  void update() {
    if (is_initialized()) {
      _current_region->set_compaction_top(_compaction_top);
    }
  }

  bool object_will_fit(size_t size) {
    size_t space_left = pointer_delta(_current_region->end(), _compaction_top);
    return size <= space_left;
  }

  void switch_region() {
    // Save compaction top in the region.
    _current_region->set_compaction_top(_compaction_top);
    // Get the next region and re-initialize the values.
    _current_region = next_compaction_space();
    initialize_values(true);
  }

  void forward(oop object, size_t size) {
    assert(_current_region != NULL, "Must have been initialized");

    // Ensure the object fit in the current region.
    while (!object_will_fit(size)) {
      switch_region();
    }

    if ((HeapWord*)object != _compaction_top) {
      object->forward_to(oop(_compaction_top));
    } else {
      object->init_mark();
    }

    // Update compaction values.
    _compaction_top += size;
    if (_compaction_top > _threshold) {
      _threshold = _current_region->cross_threshold(_compaction_top - size, _compaction_top);
    }
  }

  void add(HeapRegion* hr) {
    _compaction_regions->append(hr);
  }
  void add_huge(HeapRegion* hr) {
    _marked_huge_regions->append(hr);
  }
  HeapRegion* current_region() {
    return *_compaction_region_iterator;
  }
  const GrowableArray<HeapRegion*>* regions() const {
    return _compaction_regions;
  }
  const GrowableArray<HeapRegion*>* huge_regions() const {
    return _marked_huge_regions;
  }

  HeapRegion* remove_last() {
    return _compaction_regions->pop();
  }

  bool has_region() {
    return !_compaction_regions->is_empty();
  }
};

class G1FullGCCompactionPoints : StackObj {
private:
  G1FullGCCompactionPoint** _cps;
  uint _num_workers;
  G1FullGCCompactionPoint* _serial_compaction_point;
public:
  G1FullGCCompactionPoints(uint num_workers) : _num_workers(num_workers) {
    _cps = NEW_C_HEAP_ARRAY(G1FullGCCompactionPoint*, _num_workers, mtGC);
    for (uint i = 0; i < _num_workers; i++) {
      _cps[i] = new G1FullGCCompactionPoint();
    }
    _serial_compaction_point = new G1FullGCCompactionPoint();
  }
  ~G1FullGCCompactionPoints() {
    for (uint i = 0; i < _num_workers; i++) {
      delete _cps[i];
    }
    FREE_C_HEAP_ARRAY(G1FullGCCompactionPoint*, _cps, mtGC);
    delete _serial_compaction_point;
  }

  G1FullGCCompactionPoint* cp_at(uint i) { return _cps[i]; }
  uint num_workers() { return _num_workers; }

  G1FullGCCompactionPoint* serial_compaction_point() { return _serial_compaction_point; }
};

size_t G1RePrepareClosure::apply(oop obj) {
    // We only re-prepare objects forwarded within the current region, so
    // skip objects that are already forwarded to another region.
    oop forwarded_to = obj->forwardee();

    if (forwarded_to != NULL && !_current->is_in(forwarded_to)) {
      return obj->size();
    }

    // Get size and forward.
    size_t size = obj->size();
    _cp->forward(obj, size);

    return size;
}

bool G1MarkSweep::_parallel_prepare_compact = false;
bool G1MarkSweep::_parallel_adjust = false;
bool G1MarkSweep::_parallel_mark = false;
uint G1MarkSweep::_active_workers = 0;

void G1MarkSweep::invoke_at_safepoint(ReferenceProcessor* rp,
                                      bool clear_all_softrefs) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at a safepoint");
  HandleMark hm;  // Discard invalid handles created during gc

  COMPILER2_PRESENT(DerivedPointerTable::clear());

  _active_workers = G1CollectedHeap::heap()->workers()->active_workers();

  if (G1ParallelFullGC) {
    _parallel_mark = true;
    _parallel_prepare_compact = true;
    _parallel_adjust = true;
  }

  SharedHeap* sh = SharedHeap::heap();
#ifdef ASSERT
  if (sh->collector_policy()->should_clear_all_soft_refs()) {
    assert(clear_all_softrefs, "Policy should have been checked earler");
  }
#endif
  // hook up weak ref data so it can be used during Mark-Sweep
  assert(GenMarkSweep::ref_processor() == NULL, "no stomping");
  assert(rp != NULL, "should be non-NULL");
  assert(rp == G1CollectedHeap::heap()->ref_processor_stw(), "Precondition");

  GenMarkSweep* marks = new GenMarkSweep[_active_workers];

  if (!_parallel_mark) {
    allocate_stacks();
  } else {
    for (uint i = 0; i < _active_workers; i++) {
      marks[i]._preserved_count_max = 0;
      marks[i]._preserved_marks = NULL;
      marks[i]._preserved_count = 0;
      marks[i].set_worker_id(i);
    }
  }

  GenMarkSweep::_ref_processor = rp;
  rp->setup_policy(clear_all_softrefs);

  // When collecting the permanent generation Method*s may be moving,
  // so we either have to flush all bcp data or convert it into bci.
  CodeCache::gc_prologue();
  Threads::gc_prologue();

  bool marked_for_unloading = false;

  // We should save the marks of the currently locked biased monitors.
  // The marking doesn't preserve the marks of biased objects.
  BiasedLocking::preserve_marks();

  {
    G1FullGCCompactionPoints cps(_active_workers);

    mark_sweep_phase1(marked_for_unloading, clear_all_softrefs, marks);

    mark_sweep_phase2(&cps);

    // Don't add any more derived pointers during phase3
    COMPILER2_PRESENT(DerivedPointerTable::set_active(false));

    mark_sweep_phase3(marks);

    mark_sweep_phase4(&cps);
  }

  if (!_parallel_mark) {
    GenMarkSweep::the_gen_mark()->restore_marks();
  } else {
    for (uint i = 0; i < _active_workers; i++) {
      marks[i].restore_marks();
    }
  }

  BiasedLocking::restore_marks();

  if (!_parallel_mark) {
    GenMarkSweep::the_gen_mark()->deallocate_stacks();
  } else {
    for (uint i = 0; i < _active_workers; i++) {
      marks[i].deallocate_stacks();
    }
  }

  // Now update the derived pointers.
  COMPILER2_PRESENT(DerivedPointerTable::update_pointers());

  // "free at last gc" is calculated from these.
  // CHF: cheating for now!!!
  //  Universe::set_heap_capacity_at_last_gc(Universe::heap()->capacity());
  //  Universe::set_heap_used_at_last_gc(Universe::heap()->used());

  Threads::gc_epilogue();
  CodeCache::gc_epilogue();
  JvmtiExport::gc_epilogue();
  // refs processing: clean slate
  GenMarkSweep::_ref_processor = NULL;
}

STWGCTimer* G1MarkSweep::gc_timer() {
  return G1FullGCScope::instance()->timer();
}

SerialOldTracer* G1MarkSweep::gc_tracer() {
  return G1FullGCScope::instance()->tracer();
}

void G1MarkSweep::run_task(AbstractGangTask* task) {
  G1CollectedHeap::heap()->workers()->run_task(task);
}

void G1MarkSweep::allocate_stacks() {
  GenMarkSweep::the_gen_mark()->_preserved_count_max = 0;
  GenMarkSweep::the_gen_mark()->_preserved_marks = NULL;
  GenMarkSweep::the_gen_mark()->_preserved_count = 0;
}

class G1FullGCMarkTask : public AbstractGangTask {
protected:
  G1RootProcessor _root_processor;
  GenMarkSweep* _marks;

public:
  G1FullGCMarkTask(GenMarkSweep* marks, uint active_workers) :
    AbstractGangTask("G1 mark task"),
    _root_processor(G1CollectedHeap::heap()),
    _marks(marks) {
    _root_processor.set_num_workers(active_workers);
  }
  virtual ~G1FullGCMarkTask() { }

  void work(uint worker_id) {
    Ticks start = Ticks::now();

    ResourceMark rm;

    MarkingCodeBlobClosure follow_code_closure(&_marks[worker_id].follow_root_closure,
                                               !CodeBlobToOopClosure::FixRelocations);
    {

      if (ClassUnloading) {
        _root_processor.process_strong_roots(&_marks[worker_id].follow_root_closure,
                                             &_marks[worker_id].follow_cld_closure,
                                             &follow_code_closure,
                                             worker_id);
      } else {
        _root_processor.process_all_roots_no_string_table(&_marks[worker_id].follow_root_closure,
                                                          &_marks[worker_id].follow_cld_closure,
                                                          &follow_code_closure);
      }
      _marks[worker_id].follow_stack();
    }
  }
};


void G1MarkSweep::mark_sweep_phase1(bool& marked_for_unloading,
                                    bool clear_all_softrefs,
                                    GenMarkSweep* marks) {
  // Recursively traverse all live objects and mark them
  GCTraceTime tm("phase 1", G1Log::fine() && Verbose, true, gc_timer(), gc_tracer()->gc_id());
  GenMarkSweep::trace(" 1");

  G1CollectedHeap* g1h = G1CollectedHeap::heap();

  // Need cleared claim bits for the roots processing
  ClassLoaderDataGraph::clear_claimed_marks();

  if (!_parallel_mark) {

    MarkingCodeBlobClosure follow_code_closure(&GenMarkSweep::the_gen_mark()->follow_root_closure,
                                               !CodeBlobToOopClosure::FixRelocations);
    {
      G1RootProcessor root_processor(g1h);
      if (ClassUnloading) {
        root_processor.process_strong_roots(&GenMarkSweep::the_gen_mark()->follow_root_closure,
                                            &GenMarkSweep::the_gen_mark()->follow_cld_closure,
                                            &follow_code_closure);
      } else {
        root_processor.process_all_roots_no_string_table(&GenMarkSweep::the_gen_mark()->follow_root_closure,
                                                         &GenMarkSweep::the_gen_mark()->follow_cld_closure,
                                                         &follow_code_closure);
      }
    }

    // Process reference objects found during marking
    ReferenceProcessor* rp = GenMarkSweep::ref_processor();
    assert(rp == g1h->ref_processor_stw(), "Sanity");

    rp->setup_policy(clear_all_softrefs);
    const ReferenceProcessorStats& stats =
      rp->process_discovered_references(&GenMarkSweep::the_gen_mark()->is_alive,
                                        &GenMarkSweep::the_gen_mark()->keep_alive,
                                        &GenMarkSweep::the_gen_mark()->follow_stack_closure,
                                        NULL,
                                        gc_timer(),
                                        gc_tracer()->gc_id());
    gc_tracer()->report_gc_reference_stats(stats);


    // This is the point where the entire marking should have completed.
    assert(GenMarkSweep::the_gen_mark()->_marking_stack.is_empty(), "Marking should have completed");

    if (ClassUnloading) {
      // Unload classes and purge the SystemDictionary.
      bool purged_class = SystemDictionary::do_unloading(&GenMarkSweep::the_gen_mark()->is_alive);
      // Unload nmethods.
      CodeCache::do_unloading(&GenMarkSweep::the_gen_mark()->is_alive, purged_class);
      // Prune dead klasses from subklass/sibling/implementor lists.
      Klass::clean_weak_klass_links(&GenMarkSweep::the_gen_mark()->is_alive);
    }
    // Delete entries for dead interned string and clean up unreferenced symbols in symbol table.
    G1CollectedHeap::heap()->unlink_string_and_symbol_table(&GenMarkSweep::the_gen_mark()->is_alive);
  } else {
    G1FullGCMarkTask task(marks, _active_workers);
    FlexibleWorkGang* flexible = G1CollectedHeap::heap()->workers();
    SharedHeap::heap()->set_par_threads(_active_workers);
    flexible->run_task(&task);
    SharedHeap::heap()->set_par_threads(0);

    // Process reference objects found during marking
    ReferenceProcessor* rp = MarkSweep::ref_processor();
    assert(rp == g1h->ref_processor_stw(), "Sanity");

    rp->setup_policy(clear_all_softrefs);

    const ReferenceProcessorStats& stats =
      rp->process_discovered_references(&marks[0].is_alive,
                                        &marks[0].keep_alive,
                                        &marks[0].follow_stack_closure,
                                        NULL,
                                        gc_timer(),
                                        gc_tracer()->gc_id());
    gc_tracer()->report_gc_reference_stats(stats);

    if (ClassUnloading) {

       // Unload classes and purge the SystemDictionary.
       bool purged_class = SystemDictionary::do_unloading(&marks[0].is_alive);

       // Unload nmethods.
       CodeCache::do_unloading(&marks[0].is_alive, purged_class);

       // Prune dead klasses from subklass/sibling/implementor lists.
       Klass::clean_weak_klass_links(&marks[0].is_alive);
    }
    // Delete entries for dead interned string and clean up unreferenced symbols in symbol table.
    G1CollectedHeap::heap()->unlink_string_and_symbol_table(&marks[0].is_alive);
  }

  if (VerifyDuringGC) {
    HandleMark hm;  // handle scope
    COMPILER2_PRESENT(DerivedPointerTableDeactivate dpt_deact);
    Universe::heap()->prepare_for_verify();
    // Note: we can verify only the heap here. When an object is
    // marked, the previous value of the mark word (including
    // identity hash values, ages, etc) is preserved, and the mark
    // word is set to markOop::marked_value - effectively removing
    // any hash values from the mark word. These hash values are
    // used when verifying the dictionaries and so removing them
    // from the mark word can make verification of the dictionaries
    // fail. At the end of the GC, the orginal mark word values
    // (including hash values) are restored to the appropriate
    // objects.
    if (!VerifySilently) {
      gclog_or_tty->print(" VerifyDuringGC:(full)[Verifying ");
    }
    Universe::heap()->verify(VerifySilently, VerifyOption_G1UseMarkWord);
    if (!VerifySilently) {
      gclog_or_tty->print_cr("]");
    }
  }

  gc_tracer()->report_object_count_after_gc(&GenMarkSweep::the_gen_mark()->is_alive);
}


class G1ParallelPrepareCompactClosure : public HeapRegionClosure {
protected:
  G1CollectedHeap* _g1h;
  ModRefBarrierSet* _mrbs;
  G1FullGCCompactionPoint* _cp;
  GrowableArray<HeapRegion*>* _start_humongous_regions_to_be_freed;

protected:
  virtual void prepare_for_compaction(HeapRegion* hr, HeapWord* end) {
    if (_cp->space == NULL) {
      _cp->space = hr;
      _cp->threshold = hr->initialize_threshold();
    }
    _cp->add(hr);
    hr->prepare_for_compaction(_cp);
    // Also clear the part of the card table that will be unused after compaction.
    _mrbs->clear(MemRegion(hr->compaction_top(), end));
  }

public:
  G1ParallelPrepareCompactClosure(G1FullGCCompactionPoint* cp) :
    _g1h(G1CollectedHeap::heap()),
    _mrbs(_g1h->g1_barrier_set()),
    _cp(cp),
    _start_humongous_regions_to_be_freed(
      new (ResourceObj::C_HEAP, mtGC) GrowableArray<HeapRegion*>(32, true, mtGC)) {
  }

  ~G1ParallelPrepareCompactClosure() {
    delete _start_humongous_regions_to_be_freed;
  }

  const GrowableArray<HeapRegion*>* start_humongous_regions_to_be_freed() const {
    return _start_humongous_regions_to_be_freed;
  }

  bool doHeapRegion(HeapRegion* hr) {
    if (hr->isHumongous()) {
      if (hr->startsHumongous()) {
        oop obj = oop(hr->bottom());
        if (obj->is_gc_marked()) {
          obj->forward_to(obj);
          _cp->add_huge(hr);
        } else  {
          _start_humongous_regions_to_be_freed->append(hr);
        }
      } else {
        assert(hr->continuesHumongous(), "Invalid humongous.");
      }
    } else {
      prepare_for_compaction(hr, hr->end());
    }
    return false;
  }

  bool freed_regions() {
    if (_start_humongous_regions_to_be_freed->length() != 0) {
      return true;
    }

    if (!_cp->has_region()) {
      return false;
    }

    if (_cp->current_region() != _cp->regions()->top()) {
      return true;
    }

    return false;
  }
};

class G1FullGCPrepareTask : public AbstractGangTask {
protected:
  HeapRegionClaimer _hrclaimer;
  G1FullGCCompactionPoints* _cps;
  GrowableArray<HeapRegion*>* _all_start_humongous_regions_to_be_freed;
  HeapRegionSetCount _humongous_regions_removed;
  bool _freed_regions;

protected:
  void free_humongous_region(HeapRegion* hr) {
    FreeRegionList dummy_free_list("Dummy Free List for G1MarkSweep");
    G1CollectedHeap* g1h = G1CollectedHeap::heap();
    do {
      HeapRegion* next = g1h->next_region_in_humongous(hr);
      hr->set_containing_set(NULL);
      _humongous_regions_removed.increment(1u, hr->capacity());
      g1h->free_humongous_region(hr, &dummy_free_list, false);
      hr = next;
    } while (hr != NULL);
    dummy_free_list.remove_all();
  }

  void update_sets() {
    // We'll recalculate total used bytes and recreate the free list
    // at the end of the GC, so no point in updating those values here.
    HeapRegionSetCount empty_set;
    G1CollectedHeap::heap()->remove_from_old_sets(empty_set, _humongous_regions_removed);
  }

public:
  G1FullGCPrepareTask(G1FullGCCompactionPoints* cps) :
    AbstractGangTask("G1 Prepare Task"),
    _hrclaimer(G1CollectedHeap::heap()->workers()->active_workers()),
    _cps(cps),
    _all_start_humongous_regions_to_be_freed(
      new (ResourceObj::C_HEAP, mtGC) GrowableArray<HeapRegion*>(32, true, mtGC)),
    _humongous_regions_removed(),
    _freed_regions(false) { }

  virtual ~G1FullGCPrepareTask() {
    delete _all_start_humongous_regions_to_be_freed;
  }

  void work(uint worker_id) {
    Ticks start = Ticks::now();
    G1ParallelPrepareCompactClosure closure(_cps->cp_at(worker_id));
    G1CollectedHeap::heap()->heap_region_par_iterate_chunked(&closure, worker_id, &_hrclaimer);
    {
      MutexLockerEx mu(FreeHumongousRegions_lock, Mutex::_no_safepoint_check_flag);
      _all_start_humongous_regions_to_be_freed->appendAll(closure.start_humongous_regions_to_be_freed());
      if (closure.freed_regions()) {
        _freed_regions = true;
      }
    }
  }

  void free_humongous_regions() {
    for (GrowableArrayIterator<HeapRegion*> it = _all_start_humongous_regions_to_be_freed->begin();
         it != _all_start_humongous_regions_to_be_freed->end();
         ++it) {
      free_humongous_region(*it);
    }
    update_sets();
  }

  bool freed_regions() {
    return _freed_regions;
  }

  void prepare_serial_compaction() {
    for (uint i = 0; i < _cps->num_workers(); i++) {
      G1FullGCCompactionPoint* cp = _cps->cp_at(i);
      if (cp->has_region()) {
        _cps->serial_compaction_point()->add(cp->remove_last());
      }
    }

    G1FullGCCompactionPoint* cp = _cps->serial_compaction_point();
    for (GrowableArrayIterator<HeapRegion*> it = cp->regions()->begin(); it != cp->regions()->end(); ++it) {
      HeapRegion* current = *it;
      if (!cp->is_initialized()) {
        // Initialize the compaction point. Nothing more is needed for the first heap region
        // since it is already prepared for compaction.
        cp->initialize(current, false);
      } else {
        G1RePrepareClosure re_prepare(cp, current);
        current->set_compaction_top(current->bottom());
        current->apply_to_marked_objects(&re_prepare);
      }
    }
    cp->update();
  }
};

void G1MarkSweep::mark_sweep_phase2(G1FullGCCompactionPoints* cps) {
  // Now all live objects are marked, compute the new object addresses.

  // It is not required that we traverse spaces in the same order in
  // phase2, phase3 and phase4, but the ValidateMarkSweep live oops
  // tracking expects us to do so. See comment under phase4.

  GCTraceTime tm("phase 2", G1Log::fine() && Verbose, true, gc_timer(), gc_tracer()->gc_id());
  GenMarkSweep::trace("2");

  if (!_parallel_prepare_compact) {
    prepare_compaction();
  } else {
    G1FullGCPrepareTask task(cps);
    FlexibleWorkGang* flexible = G1CollectedHeap::heap()->workers();
    flexible->run_task(&task);
    task.free_humongous_regions();

    if (!task.freed_regions()) {
      task.prepare_serial_compaction();
    }
  }
}


class G1AdjustPointersClosure: public HeapRegionClosure {
 public:
  bool doHeapRegion(HeapRegion* r) {
    if (r->isHumongous()) {
      if (r->startsHumongous()) {
        // We must adjust the pointers on the single H object.
        oop obj = oop(r->bottom());
        // point all the oops to the new location
        obj->adjust_pointers();
      }
    } else {
      // This really ought to be "as_CompactibleSpace"...
      r->adjust_pointers();
    }
    return false;
  }
};

class G1FullGCAdjustTask : public AbstractGangTask {
  HeapRegionClaimer         _hrclaimer;
  G1AdjustPointersClosure   _adjust;

public:
  G1FullGCAdjustTask() :
    AbstractGangTask("G1 Adjust Task"),
    _hrclaimer(G1CollectedHeap::heap()->workers()->active_workers()),
    _adjust() {
  }
  virtual ~G1FullGCAdjustTask() { }

  void work(uint worker_id) {
    Ticks start = Ticks::now();
    G1AdjustPointersClosure blk;
    G1CollectedHeap::heap()->heap_region_par_iterate_chunked(&blk, worker_id, &_hrclaimer);
  }
};

void G1MarkSweep::mark_sweep_phase3(GenMarkSweep* marks) {
  G1CollectedHeap* g1h = G1CollectedHeap::heap();

  // Adjust the pointers to reflect the new locations
  GCTraceTime tm("phase 3", G1Log::fine() && Verbose, true, gc_timer(), gc_tracer()->gc_id());
  GenMarkSweep::trace("3");

  // Need cleared claim bits for the roots processing
  ClassLoaderDataGraph::clear_claimed_marks();

  CodeBlobToOopClosure adjust_code_closure(&GenMarkSweep::the_gen_mark()->adjust_pointer_closure,
                                           CodeBlobToOopClosure::FixRelocations);
  {
    G1RootProcessor root_processor(g1h);
    root_processor.process_all_roots(&GenMarkSweep::the_gen_mark()->adjust_pointer_closure,
                                     &GenMarkSweep::the_gen_mark()->adjust_cld_closure,
                                     &adjust_code_closure);
  }

  assert(GenMarkSweep::ref_processor() == g1h->ref_processor_stw(), "Sanity");
  g1h->ref_processor_stw()->weak_oops_do(&GenMarkSweep::the_gen_mark()->adjust_pointer_closure);

  // Now adjust pointers in remaining weak roots.  (All of which should
  // have been cleared if they pointed to non-surviving objects.)
  JNIHandles::weak_oops_do(&GenMarkSweep::the_gen_mark()->adjust_pointer_closure);
  JFR_ONLY(Jfr::weak_oops_do(&GenMarkSweep::the_gen_mark()->adjust_pointer_closure));

  if (G1StringDedup::is_enabled()) {
    G1StringDedup::oops_do(&GenMarkSweep::the_gen_mark()->adjust_pointer_closure);
  }

  if (!_parallel_adjust) {
    GenMarkSweep::the_gen_mark()->adjust_marks();
    G1AdjustPointersClosure blk;
    g1h->heap_region_iterate(&blk);
  } else {
    if (!_parallel_mark) {
      GenMarkSweep::the_gen_mark()->adjust_marks();
    } else {
      for (uint i = 0; i < _active_workers; i++) {
        marks[i].adjust_marks();
      }
    }

    G1FullGCAdjustTask task;
    FlexibleWorkGang* flexible = G1CollectedHeap::heap()->workers();
    flexible->run_task(&task);
  }
}


class G1SpaceCompactClosure: public HeapRegionClosure {
public:

  bool doHeapRegion(HeapRegion* hr) {
    if (hr->isHumongous()) {
      if (hr->startsHumongous()) {
        oop obj = oop(hr->bottom());
        if (obj->is_gc_marked()) {
          obj->init_mark();
        } else {
          assert(hr->is_empty(), "Should have been cleared in phase 2.");
        }
      }
      hr->reset_during_compaction();
    } else {
      hr->compact();
    }
    return false;
  }
};

class G1FullGCCompactTask : public AbstractGangTask {
  HeapRegionClaimer _hrclaimer;
  G1FullGCCompactionPoints* _cps;

  void compact_region(HeapRegion* hr) {
    hr->compact();

    hr->reset_after_compaction();
    if (hr->used_region().is_empty()) {
      hr->reset_bot();
    }
  }

public:
  G1FullGCCompactTask(G1FullGCCompactionPoints* cps) :
    AbstractGangTask("G1 Compact Task"),
    _hrclaimer(G1CollectedHeap::heap()->workers()->active_workers()),
    _cps(cps) {
  }
  virtual ~G1FullGCCompactTask() { }

  void work(uint worker_id) {
    Ticks start = Ticks::now();
    const GrowableArray<HeapRegion*>* compaction_queue = _cps->cp_at(worker_id)->regions();
    for (GrowableArrayIterator<HeapRegion*> it = compaction_queue->begin();
         it != compaction_queue->end();
         ++it) {
      HeapRegion* hr = *it;
      compact_region(hr);
    }

    const GrowableArray<HeapRegion*>* marked_huge_regions = _cps->cp_at(worker_id)->huge_regions();
    G1CollectedHeap* g1h = G1CollectedHeap::heap();
    for (GrowableArrayIterator<HeapRegion*> it = marked_huge_regions->begin();
         it != marked_huge_regions->end();
         ++it) {
      HeapRegion* hr = *it;
      oop obj = oop(hr->bottom());
      assert(obj->is_gc_marked(), "Must be");
      obj->init_mark();
      do {
        HeapRegion* next = g1h->next_region_in_humongous(hr);
        hr->reset_during_compaction();
        hr = next;
      } while (hr != NULL);
    }
  }

  void serial_compaction() {
    const GrowableArray<HeapRegion*>* compaction_queue = _cps->serial_compaction_point()->regions();
    for (GrowableArrayIterator<HeapRegion*> it = compaction_queue->begin();
         it != compaction_queue->end();
         ++it) {
      compact_region(*it);
    }
  }
};

void G1MarkSweep::mark_sweep_phase4(G1FullGCCompactionPoints* cps) {
  // All pointers are now adjusted, move objects accordingly

  // The ValidateMarkSweep live oops tracking expects us to traverse spaces
  // in the same order in phase2, phase3 and phase4. We don't quite do that
  // here (code and comment not fixed for perm removal), so we tell the validate code
  // to use a higher index (saved from phase2) when verifying perm_gen.
  G1CollectedHeap* g1h = G1CollectedHeap::heap();

  GCTraceTime tm("phase 4", G1Log::fine() && Verbose, true, gc_timer(), gc_tracer()->gc_id());
  GenMarkSweep::trace("4");

  if (!_parallel_prepare_compact) {
    G1SpaceCompactClosure blk;
    g1h->heap_region_iterate(&blk);
  } else {
    G1FullGCCompactTask task(cps);
    FlexibleWorkGang* flexible = G1CollectedHeap::heap()->workers();
    flexible->run_task(&task);

    if (cps->serial_compaction_point()->has_region()) {
      task.serial_compaction();
    }
  }
}

class G1PrepareCompactClosure : public HeapRegionClosure {
protected:
  G1CollectedHeap* _g1h;
  ModRefBarrierSet* _mrbs;
  CompactPoint _cp;
  HeapRegionSetCount _humongous_regions_removed;

  virtual void prepare_for_compaction(HeapRegion* hr, HeapWord* end) {
    // If this is the first live region that we came across which we can compact,
    // initialize the CompactPoint.
    if (!is_cp_initialized()) {
      _cp.space = hr;
      _cp.threshold = hr->initialize_threshold();
    }
    prepare_for_compaction_work(&_cp, hr, end);
  }

  void prepare_for_compaction_work(CompactPoint* cp, HeapRegion* hr, HeapWord* end) {
    hr->prepare_for_compaction(cp);
    // Also clear the part of the card table that will be unused after
    // compaction.
    _mrbs->clear(MemRegion(hr->compaction_top(), end));
  }

  void free_humongous_region(HeapRegion* hr) {
    HeapWord* end = hr->end();
    FreeRegionList dummy_free_list("Dummy Free List for G1MarkSweep");

    hr->set_containing_set(NULL);
    _humongous_regions_removed.increment(1u, hr->capacity());

    _g1h->free_humongous_region(hr, &dummy_free_list, false /* par */);
    prepare_for_compaction(hr, end);
    dummy_free_list.remove_all();
  }

  bool is_cp_initialized() const { return _cp.space != NULL; }

public:
  G1PrepareCompactClosure() :
    _g1h(G1CollectedHeap::heap()),
    _mrbs(_g1h->g1_barrier_set()),
    _humongous_regions_removed() { }
  ~G1PrepareCompactClosure() { }

  void update_sets() {
    // We'll recalculate total used bytes and recreate the free list
    // at the end of the GC, so no point in updating those values here.
    HeapRegionSetCount empty_set;
    _g1h->remove_from_old_sets(empty_set, _humongous_regions_removed);
  }
  bool doHeapRegion(HeapRegion* hr) {
    if (hr->isHumongous()) {
        oop obj = oop(hr->humongous_start_region()->bottom());
        if (hr->startsHumongous() && obj->is_gc_marked()) {
          obj->forward_to(obj);
        }
        if (!obj->is_gc_marked()) {
          free_humongous_region(hr);
        }
    } else {
      prepare_for_compaction(hr, hr->end());
    }
    return false;
  }
};

void G1MarkSweep::prepare_compaction() {
  G1PrepareCompactClosure blk;
  G1MarkSweep::prepare_compaction_work(&blk);
}

void G1MarkSweep::prepare_compaction_work(G1PrepareCompactClosure* blk) {
  G1CollectedHeap* g1h = G1CollectedHeap::heap();
  g1h->heap_region_iterate(blk);
  blk->update_sets();
}
