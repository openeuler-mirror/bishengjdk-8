/*
 * Copyright (c) 1997, 2013, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_GC_IMPLEMENTATION_SHARED_MARKSWEEP_HPP
#define SHARE_VM_GC_IMPLEMENTATION_SHARED_MARKSWEEP_HPP

#include "gc_interface/collectedHeap.hpp"
#include "memory/universe.hpp"
#include "oops/markOop.hpp"
#include "oops/oop.hpp"
#include "runtime/timer.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/stack.hpp"
#include "utilities/taskqueue.hpp"

class ReferenceProcessor;
class DataLayout;
class SerialOldTracer;
class STWGCTimer;

// MarkSweep takes care of global mark-compact garbage collection for a
// GenCollectedHeap using a four-phase pointer forwarding algorithm.  All
// generations are assumed to support marking; those that can also support
// compaction.
//
// Class unloading will only occur when a full gc is invoked.

// declared at end
class PreservedMark;

class MarkSweep : public ResourceObj {
  //
  // Inline closure decls
  //
  class FollowRootClosure: public OopsInGenClosure {
    MarkSweep* _mark;
   public:
    FollowRootClosure(MarkSweep* mark) : _mark(mark) { }
    virtual void do_oop(oop* p);
    virtual void do_oop(narrowOop* p);
  };

  class MarkAndPushClosure: public OopClosure {
    MarkSweep* _mark;
   public:
    MarkAndPushClosure(MarkSweep* mark) : _mark(mark) { }
    virtual void do_oop(oop* p);
    virtual void do_oop(narrowOop* p);
  };

  class FollowStackClosure: public VoidClosure {
    MarkSweep* _mark;
   public:
    FollowStackClosure(MarkSweep* mark) : _mark(mark) { }
    virtual void do_void();
  };

  class AdjustPointerClosure: public OopsInGenClosure {
    MarkSweep* _mark;
   public:
    AdjustPointerClosure(MarkSweep* mark) : _mark(mark) { }
    virtual void do_oop(oop* p);
    virtual void do_oop(narrowOop* p);
  };

  // Used for java/lang/ref handling
  class IsAliveClosure: public BoolObjectClosure {
    MarkSweep* _mark;
   public:
    IsAliveClosure(MarkSweep* mark) : _mark(mark) { }
    virtual bool do_object_b(oop p);
  };

  class KeepAliveClosure: public OopClosure {
    MarkSweep* _mark;
   protected:
    template <class T> void do_oop_work(T* p);
   public:
    KeepAliveClosure(MarkSweep* mark) : _mark(mark) { }
    virtual void do_oop(oop* p);
    virtual void do_oop(narrowOop* p);
  };

  //
  // Friend decls
  //
  friend class AdjustPointerClosure;
  friend class KeepAliveClosure;
  friend class VM_MarkSweep;
  friend void marksweep_init();

  //
  // Vars
  //
 protected:
  // Total invocations of a MarkSweep collection
  static uint _total_invocations;

  // Traversal stacks used during phase1
  Stack<oop, mtGC>                      _marking_stack;
  Stack<ObjArrayTask, mtGC>             _objarray_stack;

  // Space for storing/restoring mark word
  Stack<markOop, mtGC>                  _preserved_mark_stack;
  Stack<oop, mtGC>                      _preserved_oop_stack;
  size_t                          _preserved_count;
  size_t                          _preserved_count_max;
  PreservedMark*                  _preserved_marks;

  uint _worker_id;
  // Reference processing (used in ...follow_contents)
  static ReferenceProcessor*             _ref_processor;

  static STWGCTimer*                     _gc_timer;
  static SerialOldTracer*                _gc_tracer;

  // Debugging
  static void trace(const char* msg) PRODUCT_RETURN;
  bool par_mark(oop obj);

 public:
  static MarkSweep* the_mark();
  KeepAliveClosure keep_alive;
  // Public closures
  IsAliveClosure       is_alive;
  FollowRootClosure    follow_root_closure;
  MarkAndPushClosure   mark_and_push_closure;
  FollowStackClosure   follow_stack_closure;
  CLDToOopClosure      follow_cld_closure;
  AdjustPointerClosure adjust_pointer_closure;
  CLDToOopClosure      adjust_cld_closure;

  MarkSweep() :
    is_alive(this),
    follow_root_closure(this),
    mark_and_push_closure(this),
    follow_stack_closure(this),
    follow_cld_closure(&mark_and_push_closure),
    adjust_pointer_closure(this),
    adjust_cld_closure(&adjust_pointer_closure),
    keep_alive(this)
    { }

  // Accessors
  static uint total_invocations() { return _total_invocations; }

  // Reference Processing
  static ReferenceProcessor* const ref_processor() { return _ref_processor; }

  static STWGCTimer* gc_timer() { return _gc_timer; }
  static SerialOldTracer* gc_tracer() { return _gc_tracer; }

  void set_worker_id(uint worker_id) { _worker_id = worker_id; }
  // Call backs for marking
  bool mark_object(oop obj);
  // Mark pointer and follow contents.  Empty marking stack afterwards.
  template <class T> inline void follow_root(T* p);

  // Check mark and maybe push on marking stack
  template <class T> void mark_and_push(T* p);

  inline void push_objarray(oop obj, size_t index);
  void follow_stack();   // Empty marking st
  void follow_klass(Klass* klass);
  void follow_class_loader(ClassLoaderData* cld);
  void preserve_mark(oop p, markOop mark);
                         // Save the mark word so it can be restored later
  void adjust_marks();   // Adjust the pointers in the preserved marks table
  void restore_marks();  // Restore the marks that we saved in preserve_mark

  template <class T> static inline void adjust_pointer(T* p);
};

class PreservedMark VALUE_OBJ_CLASS_SPEC {
private:
  oop _obj;
  markOop _mark;

public:
  void init(oop obj, markOop mark) {
    _obj = obj;
    _mark = mark;
  }

  void adjust_pointer() {
    MarkSweep::adjust_pointer(&_obj);
  }

  void restore() {
    _obj->set_mark(_mark);
  }
};

#endif // SHARE_VM_GC_IMPLEMENTATION_SHARED_MARKSWEEP_HPP
