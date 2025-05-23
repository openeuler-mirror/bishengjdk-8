/*
 * Copyright (c) 2001, 2013, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_GC_IMPLEMENTATION_G1_G1MARKSWEEP_HPP
#define SHARE_VM_GC_IMPLEMENTATION_G1_G1MARKSWEEP_HPP

#include "gc_implementation/g1/g1CollectedHeap.inline.hpp"
#include "gc_implementation/g1/heapRegion.hpp"
#include "memory/genMarkSweep.hpp"
#include "memory/generation.hpp"
#include "memory/universe.hpp"
#include "oops/markOop.hpp"
#include "oops/oop.hpp"
#include "runtime/timer.hpp"
#include "utilities/growableArray.hpp"

class ReferenceProcessor;

// G1MarkSweep takes care of global mark-compact garbage collection for a
// G1CollectedHeap using a four-phase pointer forwarding algorithm.  All
// generations are assumed to support marking; those that can also support
// compaction.
//
// Class unloading will only occur when a full gc is invoked.
class G1PrepareCompactClosure;
class G1FullGCCompactionPoints;
class G1FullGCCompactionPoint;

class G1RePrepareClosure : public StackObj {
  G1FullGCCompactionPoint* _cp;
  HeapRegion* _current;

  public:
  G1RePrepareClosure(G1FullGCCompactionPoint* hrcp,
                     HeapRegion* hr) :
      _cp(hrcp),
      _current(hr) { }

  size_t apply(oop obj);
};

class G1MarkSweep : AllStatic {
  friend class VM_G1MarkSweep;
  friend class Scavenge;

 public:

  static void invoke_at_safepoint(ReferenceProcessor* rp,
                                  bool clear_all_softrefs);

  static STWGCTimer* gc_timer();
  static SerialOldTracer* gc_tracer();

 private:
  static bool _parallel_prepare_compact;
  static bool _parallel_adjust;
  static bool _parallel_mark;
  static uint _active_workers;

 private:
  static void run_task(AbstractGangTask* task);
  // Mark live objects
  static void mark_sweep_phase1(bool& marked_for_deopt,
                                bool clear_all_softrefs,
                                GenMarkSweep* marks);
  // Calculate new addresses
  static void mark_sweep_phase2(G1FullGCCompactionPoints* cps);
  // Update pointers
  static void mark_sweep_phase3(GenMarkSweep* marks);
  // Move objects to new positions
  static void mark_sweep_phase4(G1FullGCCompactionPoints* cps);

  static void allocate_stacks();
  static void prepare_compaction();
  static void prepare_compaction_work(G1PrepareCompactClosure* blk);
};

#endif // SHARE_VM_GC_IMPLEMENTATION_G1_G1MARKSWEEP_HPP
