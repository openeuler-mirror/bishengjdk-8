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

#include "precompiled.hpp"

#include "classfile/classLoaderData.hpp"
#include "classfile/systemDictionary.hpp"
#include "code/codeCache.hpp"
#include "gc_implementation/shenandoah/shenandoahClosures.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahRootProcessor.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahFreeSet.hpp"
#include "gc_implementation/shenandoah/shenandoahCollectorPolicy.hpp"
#include "gc_implementation/shenandoah/shenandoahPhaseTimings.hpp"
#include "gc_implementation/shenandoah/shenandoahStringDedup.hpp"
#include "gc_implementation/shenandoah/shenandoahSynchronizerIterator.hpp"
#include "gc_implementation/shenandoah/shenandoahWorkGroup.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/fprofiler.hpp"
#include "runtime/thread.hpp"
#include "services/management.hpp"

#if INCLUDE_JFR
#include "jfr/leakprofiler/leakProfiler.hpp"
#endif

ShenandoahSerialRoot::ShenandoahSerialRoot(ShenandoahSerialRoot::OopsDo oops_do, ShenandoahPhaseTimings::Phase phase, ShenandoahPhaseTimings::ParPhase par_phase) :
  _claimed(0), _oops_do(oops_do), _phase(phase), _par_phase(par_phase) {
}

void ShenandoahSerialRoot::oops_do(OopClosure* cl, uint worker_id) {
  if (_claimed == 0 && Atomic::cmpxchg(1, &_claimed, 0) == 0) {
    ShenandoahWorkerTimingsTracker timer(_phase, _par_phase, worker_id);
    _oops_do(cl);
  }
}

static void universe_oops_do(OopClosure* closure) {
  Universe::oops_do(closure);
}

ShenandoahSerialRoots::ShenandoahSerialRoots(ShenandoahPhaseTimings::Phase phase) :
  _phase(phase),
  _universe_roots(&universe_oops_do, phase, ShenandoahPhaseTimings::UniverseRoots),
  _management_roots(&Management::oops_do, phase, ShenandoahPhaseTimings::ManagementRoots),
  _jvmti_roots(&JvmtiExport::oops_do, phase, ShenandoahPhaseTimings::JVMTIRoots),
  _jni_handle_roots(&JNIHandles::oops_do, phase, ShenandoahPhaseTimings::JNIRoots),
  _flat_profiler_roots(&FlatProfiler::oops_do, phase, ShenandoahPhaseTimings::FlatProfilerRoots) {
}

void ShenandoahSerialRoots::oops_do(OopClosure* cl, uint worker_id) {
  _universe_roots.oops_do(cl, worker_id);
  _management_roots.oops_do(cl, worker_id);
  _jvmti_roots.oops_do(cl, worker_id);
  _jni_handle_roots.oops_do(cl, worker_id);
  _flat_profiler_roots.oops_do(cl, worker_id);

  {
    ShenandoahWorkerTimingsTracker timer(_phase, ShenandoahPhaseTimings::ObjectSynchronizerRoots, worker_id);
    while(_om_iterator.parallel_oops_do(cl));
  }
}

ShenandoahSystemDictionaryRoots::ShenandoahSystemDictionaryRoots(ShenandoahPhaseTimings::Phase phase) :
  _phase(phase), _claimed(0) {
}

void ShenandoahSystemDictionaryRoots::strong_oops_do(OopClosure* oops, uint worker_id) {
  if (_claimed == 0 && Atomic::cmpxchg(1, &_claimed, 0) == 0) {
    ShenandoahWorkerTimingsTracker timer(_phase, ShenandoahPhaseTimings::SystemDictionaryRoots, worker_id);
    SystemDictionary::roots_oops_do(oops, NULL);
  }
}

void ShenandoahSystemDictionaryRoots::oops_do(OopClosure* oops, uint worker_id) {
  if (_claimed == 0 && Atomic::cmpxchg(1, &_claimed, 0) == 0) {
    ShenandoahWorkerTimingsTracker timer(_phase, ShenandoahPhaseTimings::SystemDictionaryRoots, worker_id);
    SystemDictionary::roots_oops_do(oops, oops);
  }
}

ShenandoahStringTableRoots::ShenandoahStringTableRoots(ShenandoahPhaseTimings::Phase phase) :
  _phase(phase)
{}

void ShenandoahStringTableRoots::oops_do(OopClosure* oops, uint worker_id) {
  ShenandoahWorkerTimingsTracker timer(_phase, ShenandoahPhaseTimings::StringTableRoots, worker_id);
  StringTable::possibly_parallel_oops_do_shenandoah(oops);
}

ShenandoahThreadRoots::ShenandoahThreadRoots(ShenandoahPhaseTimings::Phase phase) :
  _phase(phase) {
  ShenandoahHeap* const heap = ShenandoahHeap::heap();
  heap->set_par_threads(heap->workers()->active_workers());
}

void ShenandoahThreadRoots::oops_do(OopClosure* oops_cl, CLDClosure* cld_cl, CodeBlobClosure* code_cl, uint worker_id) {
  ShenandoahWorkerTimingsTracker timer(_phase, ShenandoahPhaseTimings::ThreadRoots, worker_id);
  ResourceMark rm;
  Threads::possibly_parallel_oops_do(oops_cl, cld_cl, code_cl);
}

ShenandoahWeakRoot::ShenandoahWeakRoot(ShenandoahPhaseTimings::Phase phase, ShenandoahPhaseTimings::ParPhase par_phase, ShenandoahWeakRoot::WeakOopsDo oops_do) :
  _phase(phase), _par_phase(par_phase), _claimed(0), _weak_oops_do(oops_do) {
}

void ShenandoahWeakRoot::oops_do(OopClosure* keep_alive, uint worker_id) {
  AlwaysTrueClosure always_true;
  weak_oops_do(&always_true, keep_alive, worker_id);
}

void ShenandoahWeakRoot::weak_oops_do(BoolObjectClosure* is_alive, OopClosure* keep_alive, uint worker_id) {
  if (_claimed == 0 && Atomic::cmpxchg(1, &_claimed, 0) == 0) {
    ShenandoahWorkerTimingsTracker t(_phase, _par_phase, worker_id);
    _weak_oops_do(is_alive, keep_alive);
  }
}

ShenandoahWeakRoots::ShenandoahWeakRoots(ShenandoahPhaseTimings::Phase phase) :
#if INCLUDE_JFR
  _jfr_weak_roots(phase, ShenandoahPhaseTimings::JFRWeakRoots, &LeakProfiler::oops_do),
#endif // INCLUDE_JFR
  _jni_weak_roots(phase, ShenandoahPhaseTimings::JNIWeakRoots, &JNIHandles::weak_oops_do) {
}

void ShenandoahWeakRoots::oops_do(OopClosure* keep_alive, uint worker_id) {
  JFR_ONLY(_jfr_weak_roots.oops_do(keep_alive, worker_id);)
  _jni_weak_roots.oops_do(keep_alive, worker_id);
}

void ShenandoahWeakRoots::weak_oops_do(BoolObjectClosure* is_alive, OopClosure* keep_alive, uint worker_id) {
  JFR_ONLY(_jfr_weak_roots.weak_oops_do(is_alive, keep_alive, worker_id);)
  _jni_weak_roots.weak_oops_do(is_alive, keep_alive, worker_id);
}

ShenandoahStringDedupRoots::ShenandoahStringDedupRoots(ShenandoahPhaseTimings::Phase phase) : _phase(phase) {
  if (ShenandoahStringDedup::is_enabled()) {
    ShenandoahStringDedup::clear_claimed();
  }
}

void ShenandoahStringDedupRoots::oops_do(OopClosure* oops, uint worker_id) {
  if (ShenandoahStringDedup::is_enabled()) {
    ShenandoahStringDedup::parallel_oops_do(_phase, oops, worker_id);
  }
}

ShenandoahClassLoaderDataRoots::ShenandoahClassLoaderDataRoots(ShenandoahPhaseTimings::Phase phase) :
  _phase(phase) {
  ClassLoaderDataGraph::clear_claimed_marks();
}

void ShenandoahClassLoaderDataRoots::cld_do(CLDClosure* clds, uint worker_id) {
  ShenandoahWorkerTimingsTracker timer(_phase, ShenandoahPhaseTimings::CLDGRoots, worker_id);
  ClassLoaderDataGraph::roots_cld_do(clds, clds);
}

void ShenandoahClassLoaderDataRoots::always_strong_cld_do(CLDClosure* clds, uint worker_id) {
  ShenandoahWorkerTimingsTracker timer(_phase, ShenandoahPhaseTimings::CLDGRoots, worker_id);
  ClassLoaderDataGraph::always_strong_cld_do(clds);
}

ShenandoahRootProcessor::ShenandoahRootProcessor(ShenandoahPhaseTimings::Phase phase) :
  _srs(ShenandoahHeap::heap()),
  _heap(ShenandoahHeap::heap()),
  _phase(phase) {
  assert(SafepointSynchronize::is_at_safepoint(), "Must at safepoint");
  _heap->phase_timings()->record_workers_start(_phase);
}

ShenandoahRootProcessor::~ShenandoahRootProcessor() {
  assert(SafepointSynchronize::is_at_safepoint(), "Must at safepoint");
  _heap->phase_timings()->record_workers_end(_phase);
}

ShenandoahRootEvacuator::ShenandoahRootEvacuator(ShenandoahPhaseTimings::Phase phase) :
  ShenandoahRootProcessor(phase),
  _serial_roots(phase),
  _dict_roots(phase),
  _cld_roots(phase),
  _thread_roots(phase),
  _weak_roots(phase),
  _dedup_roots(phase),
  _string_table_roots(phase),
  _code_roots(phase)
{}

ShenandoahHeapIterationRootScanner::ShenandoahHeapIterationRootScanner() :
  ShenandoahRootProcessor(ShenandoahPhaseTimings::heap_iteration_roots),
  _serial_roots(ShenandoahPhaseTimings::heap_iteration_roots),
  _dict_roots(ShenandoahPhaseTimings::heap_iteration_roots),
  _thread_roots(ShenandoahPhaseTimings::heap_iteration_roots),
  _cld_roots(ShenandoahPhaseTimings::heap_iteration_roots),
  _weak_roots(ShenandoahPhaseTimings::heap_iteration_roots),
  _dedup_roots(ShenandoahPhaseTimings::heap_iteration_roots),
  _string_table_roots(ShenandoahPhaseTimings::heap_iteration_roots),
  _code_roots(ShenandoahPhaseTimings::heap_iteration_roots)
{}

 void ShenandoahHeapIterationRootScanner::roots_do(OopClosure* oops) {
   assert(Thread::current()->is_VM_thread(), "Only by VM thread");
   // Must use _claim_none to avoid interfering with concurrent CLDG iteration
   CLDToOopClosure clds(oops, false /* must claim */);
   MarkingCodeBlobClosure code(oops, !CodeBlobToOopClosure::FixRelocations);
   ResourceMark rm;

   _serial_roots.oops_do(oops, 0);
   _dict_roots.oops_do(oops, 0);
   _cld_roots.cld_do(&clds, 0);
   _thread_roots.oops_do(oops, NULL, NULL, 0);
   _code_roots.code_blobs_do(&code, 0);

   _weak_roots.oops_do(oops, 0);
   _string_table_roots.oops_do(oops, 0);
   _dedup_roots.oops_do(oops, 0);
 }

void ShenandoahRootEvacuator::roots_do(uint worker_id, OopClosure* oops) {
  {
    // Evacuate the PLL here so that the SurrogateLockerThread doesn't
    // have to. SurrogateLockerThread can execute write barrier in VMOperation
    // prolog. If the SLT runs into OOM during that evacuation, the VMOperation
    // may deadlock. Doing this evacuation the first thing makes that critical
    // OOM less likely to happen.  It is a bit excessive to perform WB by all
    // threads, but this guarantees the very first evacuation would be the PLL.
    //
    // This pre-evac can still silently fail with OOME here, and PLL would not
    // get evacuated. This would mean next VMOperation would try to evac PLL in
    // SLT thread. We make additional effort to recover from that OOME in SLT,
    // see ShenandoahHeap::oom_during_evacuation(). It seems to be the lesser evil
    // to do there, because we cannot trigger Full GC right here, when we are
    // in another VMOperation.

    ShenandoahHeap* const heap = ShenandoahHeap::heap();
    assert(heap->is_evacuation_in_progress(), "only when evacuating");
    HeapWord* pll_addr = java_lang_ref_Reference::pending_list_lock_addr();
    oop pll;
    if (UseCompressedOops) {
      pll = oopDesc::load_decode_heap_oop((narrowOop *)pll_addr);
    } else {
      pll = oopDesc::load_decode_heap_oop((oop*)pll_addr);
    }
    if (!oopDesc::is_null(pll) && heap->in_collection_set(pll)) {
      oop fwd = ShenandoahBarrierSet::resolve_forwarded_not_null(pll);
      if (pll == fwd) {
        Thread *t = Thread::current();
        heap->evacuate_object(pll, t);
      }
    }
  }

  MarkingCodeBlobClosure blobsCl(oops, CodeBlobToOopClosure::FixRelocations);
  CLDToOopClosure clds(oops);

  _serial_roots.oops_do(oops, worker_id);
  _dict_roots.oops_do(oops, worker_id);
  _thread_roots.oops_do(oops, NULL, NULL, worker_id);
  _cld_roots.cld_do(&clds, worker_id);
  _code_roots.code_blobs_do(&blobsCl, worker_id);

  _weak_roots.oops_do(oops, worker_id);
  _dedup_roots.oops_do(oops, worker_id);
  _string_table_roots.oops_do(oops, worker_id);
}

ShenandoahRootUpdater::ShenandoahRootUpdater(ShenandoahPhaseTimings::Phase phase) :
  ShenandoahRootProcessor(phase),
  _serial_roots(phase),
  _dict_roots(phase),
  _cld_roots(phase),
  _thread_roots(phase),
  _weak_roots(phase),
  _dedup_roots(phase),
  _string_table_roots(phase),
  _code_roots(phase)
{}

void ShenandoahRootUpdater::roots_do(uint worker_id, BoolObjectClosure* is_alive, OopClosure* keep_alive) {
  CodeBlobToOopClosure update_blobs(keep_alive, CodeBlobToOopClosure::FixRelocations);
  CLDToOopClosure clds(keep_alive);

  _serial_roots.oops_do(keep_alive, worker_id);
  _dict_roots.oops_do(keep_alive, worker_id);
  _thread_roots.oops_do(keep_alive, &clds, NULL, worker_id);
  _cld_roots.cld_do(&clds, worker_id);

  _code_roots.code_blobs_do(&update_blobs, worker_id);

  _weak_roots.weak_oops_do(is_alive, keep_alive, worker_id);
  _dedup_roots.oops_do(keep_alive, worker_id);
  _string_table_roots.oops_do(keep_alive, worker_id);
}


ShenandoahRootAdjuster::ShenandoahRootAdjuster(ShenandoahPhaseTimings::Phase phase) :
  ShenandoahRootProcessor(phase),
  _serial_roots(phase),
  _dict_roots(phase),
  _cld_roots(phase),
  _thread_roots(phase),
  _weak_roots(phase),
  _dedup_roots(phase),
  _string_table_roots(phase),
  _code_roots(phase)
{
  assert(ShenandoahHeap::heap()->is_full_gc_in_progress(), "Full GC only");
}

void ShenandoahRootAdjuster::roots_do(uint worker_id, OopClosure* oops) {
  CodeBlobToOopClosure adjust_code_closure(oops, CodeBlobToOopClosure::FixRelocations);
  CLDToOopClosure adjust_cld_closure(oops);

  _serial_roots.oops_do(oops, worker_id);
  _dict_roots.oops_do(oops, worker_id);
  _thread_roots.oops_do(oops, NULL, NULL, worker_id);
  _cld_roots.always_strong_cld_do(&adjust_cld_closure, worker_id);
  _code_roots.code_blobs_do(&adjust_code_closure, worker_id);

  _weak_roots.oops_do(oops, worker_id);
  _dedup_roots.oops_do(oops, worker_id);
  _string_table_roots.oops_do(oops, worker_id);
}
