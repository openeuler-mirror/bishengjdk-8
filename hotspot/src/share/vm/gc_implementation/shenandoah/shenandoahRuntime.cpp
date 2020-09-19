/*
 * Copyright (c) 2018, Red Hat, Inc. All rights reserved.
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
#include "gc_implementation/shenandoah/shenandoahBarrierSet.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahBarrierSetClone.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahRuntime.hpp"
#include "runtime/interfaceSupport.hpp"
#include "oops/oop.inline.hpp"

void ShenandoahRuntime::arraycopy_barrier_oop_entry(oop* src, oop* dst, size_t length) {
  ShenandoahBarrierSet *bs = ShenandoahBarrierSet::barrier_set();
  bs->arraycopy_barrier(src, dst, length);
}

void ShenandoahRuntime::arraycopy_barrier_narrow_oop_entry(narrowOop* src, narrowOop* dst, size_t length) {
  ShenandoahBarrierSet *bs = ShenandoahBarrierSet::barrier_set();
  bs->arraycopy_barrier(src, dst, length);
}

// Shenandoah pre write barrier slowpath
JRT_LEAF(void, ShenandoahRuntime::write_ref_field_pre_entry(oopDesc* orig, JavaThread *thread))
  assert(orig != NULL, "should be optimized out");
  shenandoah_assert_correct(NULL, orig);
  // store the original value that was in the field reference
  assert(thread->satb_mark_queue().is_active(), "Shouldn't be here otherwise");
  thread->satb_mark_queue().enqueue_known_active(orig);
JRT_END

JRT_LEAF(oopDesc*, ShenandoahRuntime::load_reference_barrier(oopDesc* src))
  oop result = ShenandoahBarrierSet::barrier_set()->load_reference_barrier_mutator(oop(src));
  return (oopDesc*) result;
JRT_END

IRT_LEAF(oopDesc*, ShenandoahRuntime::load_reference_barrier_interpreter(oopDesc* src))
  oop result = ShenandoahBarrierSet::barrier_set()->load_reference_barrier(oop(src));
  return (oopDesc*) result;
IRT_END

// Shenandoah clone barrier: makes sure that references point to to-space
// in cloned objects.
JRT_LEAF(void, ShenandoahRuntime::shenandoah_clone_barrier(oopDesc* src))
  oop s = oop(src);
  shenandoah_assert_correct(NULL, s);
  ShenandoahBarrierSet::barrier_set()->clone_barrier_runtime(s);
JRT_END
