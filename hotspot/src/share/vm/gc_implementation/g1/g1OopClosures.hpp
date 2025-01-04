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

#ifndef SHARE_VM_GC_IMPLEMENTATION_G1_G1OOPCLOSURES_HPP
#define SHARE_VM_GC_IMPLEMENTATION_G1_G1OOPCLOSURES_HPP

#include "gc_implementation/g1/g1InCSetState.hpp"
#include "memory/iterator.hpp"
#include "oops/markOop.hpp"

class HeapRegion;
class G1CollectedHeap;
class G1RemSet;
class ConcurrentMark;
class DirtyCardToOopClosure;
class CMBitMap;
class CMMarkStack;
class G1ParScanThreadState;
class CMTask;
class ReferenceProcessor;

// A class that scans oops in a given heap region (much as OopsInGenClosure
// scans oops in a generation.)
class OopsInHeapRegionClosure: public ExtendedOopClosure {
protected:
  HeapRegion* _from;
public:
  void set_region(HeapRegion* from) { _from = from; }
};

class G1ScanClosureBase : public OopsInHeapRegionClosure {
protected:
  G1CollectedHeap* _g1;
  G1ParScanThreadState* _par_scan_state;

  template <class T>
  inline void prefetch_and_push(T* p, oop const obj);

  template <class T>
  inline void handle_non_cset_obj_common(InCSetState const state, T* p, oop const obj);

public:
  // Initializes the instance, leaving _par_scan_state uninitialized. Must be done
  // later using the set_par_scan_thread_state() method.
  G1ScanClosureBase(G1CollectedHeap* g1);
  G1ScanClosureBase(G1CollectedHeap* g1, G1ParScanThreadState* par_scan_state);
  bool apply_to_weak_ref_discovered_field() { return true; }

  void set_par_scan_thread_state(G1ParScanThreadState* par_scan_state);
};

// Used during the Update RS phase to refine remaining cards in the DCQ during garbage collection.
class G1ScanObjsDuringUpdateRSClosure: public G1ScanClosureBase {
  uint _worker_i;
  bool _has_refs_into_cset;

 public:
  G1ScanObjsDuringUpdateRSClosure(G1CollectedHeap* g1h,
                                  G1ParScanThreadState* pss,
                                  uint worker_i) :
    G1ScanClosureBase(g1h, pss), _has_refs_into_cset(false), _worker_i(worker_i) { }

  void reset_has_refs_into_cset() { _has_refs_into_cset = false; }
  bool has_refs_into_cset() const { return _has_refs_into_cset; }

  template <class T> void do_oop_nv(T* p);
  virtual void do_oop(narrowOop* p) { do_oop_nv(p); }
  virtual void do_oop(oop* p) { do_oop_nv(p); }
};

// Used during the Scan RS phase to scan cards from the remembered set during garbage collection.
class G1ScanObjsDuringScanRSClosure : public G1ScanClosureBase {
 public:
  G1ScanObjsDuringScanRSClosure(G1CollectedHeap* g1,
                                G1ParScanThreadState* par_scan_state):
    G1ScanClosureBase(g1, par_scan_state) { }

  template <class T> void do_oop_nv(T* p);
  virtual void do_oop(oop* p)          { do_oop_nv(p); }
  virtual void do_oop(narrowOop* p)    { do_oop_nv(p); }
};

// This closure is applied to the fields of the objects that have just been copied during evacuation.
class G1ScanEvacuatedObjClosure : public G1ScanClosureBase {
public:
  G1ScanEvacuatedObjClosure(G1CollectedHeap* g1, ReferenceProcessor* rp) :
    G1ScanClosureBase(g1) {
    assert(_ref_processor == NULL, "sanity");
    _ref_processor = rp;
  }

  template <class T> void do_oop_nv(T* p);
  virtual void do_oop(oop* p)          { do_oop_nv(p); }
  virtual void do_oop(narrowOop* p)    { do_oop_nv(p); }
};

// Add back base class for metadata
class G1ParCopyHelper : public G1ScanClosureBase {
protected:
  Klass* _scanned_klass;
  ConcurrentMark* _cm;
  uint _worker_id;              // Cache value from par_scan_state.
  // Mark the object if it's not already marked. This is used to mark
  // objects pointed to by roots that are guaranteed not to move
  // during the GC (i.e., non-CSet objects). It is MT-safe.
  void mark_object(oop obj);

  // Mark the object if it's not already marked. This is used to mark
  // objects pointed to by roots that have been forwarded during a
  // GC. It is MT-safe.
  void mark_forwarded_object(oop from_obj, oop to_obj);
 public:
  G1ParCopyHelper(G1CollectedHeap* g1,  G1ParScanThreadState* par_scan_state);

  void set_scanned_klass(Klass* k) { _scanned_klass = k; }
  template <class T> void do_klass_barrier(T* p, oop new_obj);
};

template <G1Barrier barrier, G1Mark do_mark_object>
class G1ParCopyClosure : public G1ParCopyHelper {
private:
  template <class T> void do_oop_work(T* p);

public:
  G1ParCopyClosure(G1CollectedHeap* g1, G1ParScanThreadState* par_scan_state,
                   ReferenceProcessor* rp) :
      G1ParCopyHelper(g1, par_scan_state) {
    assert(_ref_processor == NULL, "sanity");
  }

  template <class T> void do_oop_nv(T* p) { do_oop_work(p); }
  virtual void do_oop(oop* p)       { do_oop_nv(p); }
  virtual void do_oop(narrowOop* p) { do_oop_nv(p); }

  G1CollectedHeap*      g1()  { return _g1; };
  G1ParScanThreadState* pss() { return _par_scan_state; }
  ReferenceProcessor*   rp()  { return _ref_processor; };
};

typedef G1ParCopyClosure<G1BarrierNone,  G1MarkNone>             G1ParScanExtRootClosure;
typedef G1ParCopyClosure<G1BarrierNone,  G1MarkFromRoot>         G1ParScanAndMarkExtRootClosure;
typedef G1ParCopyClosure<G1BarrierNone,  G1MarkPromotedFromRoot> G1ParScanAndMarkWeakExtRootClosure;
// We use a separate closure to handle references during evacuation
// failure processing.

typedef G1ParCopyClosure<G1BarrierEvac, G1MarkNone> G1ParScanHeapEvacFailureClosure;

// Closure for iterating over object fields during concurrent marking
class G1CMOopClosure : public MetadataAwareOopClosure {
protected:
  ConcurrentMark*    _cm;
private:
  G1CollectedHeap*   _g1h;
  CMTask*            _task;
public:
  G1CMOopClosure(G1CollectedHeap* g1h, ConcurrentMark* cm, CMTask* task);
  template <class T> void do_oop_nv(T* p);
  virtual void do_oop(      oop* p) { do_oop_nv(p); }
  virtual void do_oop(narrowOop* p) { do_oop_nv(p); }
};

// Closure to scan the root regions during concurrent marking
class G1RootRegionScanClosure : public MetadataAwareOopClosure {
private:
  G1CollectedHeap* _g1h;
  ConcurrentMark*  _cm;
  uint _worker_id;
public:
  G1RootRegionScanClosure(G1CollectedHeap* g1h, ConcurrentMark* cm, uint worker_id) :
    _g1h(g1h), _cm(cm), _worker_id(worker_id) { }
  template <class T> void do_oop_nv(T* p);
  virtual void do_oop(      oop* p) { do_oop_nv(p); }
  virtual void do_oop(narrowOop* p) { do_oop_nv(p); }
};

// Closure that applies the given two closures in sequence.
// Used by the RSet refinement code (when updating RSets
// during an evacuation pause) to record cards containing
// pointers into the collection set.

class G1Mux2Closure : public ExtendedOopClosure {
  OopClosure* _c1;
  OopClosure* _c2;
public:
  G1Mux2Closure(OopClosure *c1, OopClosure *c2);
  template <class T> void do_oop_nv(T* p);
  virtual void do_oop(oop* p)        { do_oop_nv(p); }
  virtual void do_oop(narrowOop* p)  { do_oop_nv(p); }
};

class G1ConcurrentRefineOopClosure: public ExtendedOopClosure {
  G1CollectedHeap* _g1;
  uint _worker_i;

 public:
  G1ConcurrentRefineOopClosure(G1CollectedHeap* g1h, uint worker_i) :
    _g1(g1h),
    _worker_i(worker_i) {
  }

  template <class T> void do_oop_nv(T* p);
  virtual void do_oop(narrowOop* p) { do_oop_nv(p); }
  virtual void do_oop(oop* p) { do_oop_nv(p); }
};

class G1RebuildRemSetClosure : public ExtendedOopClosure {
  G1CollectedHeap* _g1;
  uint _worker_id;
 public:
  G1RebuildRemSetClosure(G1CollectedHeap* g1, uint worker_id) : _g1(g1), _worker_id(worker_id) {
  }

  template <class T> void do_oop_nv(T* p);
  virtual void do_oop(oop* p)       { do_oop_nv(p); }
  virtual void do_oop(narrowOop* p) { do_oop_nv(p); }
  // This closure needs special handling for InstanceRefKlass.
};

#endif // SHARE_VM_GC_IMPLEMENTATION_G1_G1OOPCLOSURES_HPP
