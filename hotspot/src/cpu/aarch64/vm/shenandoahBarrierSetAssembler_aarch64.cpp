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
#include "c1/c1_MacroAssembler.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "macroAssembler_aarch64.hpp"
#include "shenandoahBarrierSetAssembler_aarch64.hpp"
#include "gc_implementation/shenandoah/shenandoahBarrierSet.hpp"
#include "gc_implementation/shenandoah/shenandoahForwarding.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.hpp"
#include "gc_implementation/shenandoah/shenandoahHeapRegion.hpp"
#include "gc_implementation/shenandoah/shenandoahRuntime.hpp"
#include "gc_implementation/shenandoah/c1/shenandoahBarrierSetC1.hpp"
#include "runtime/stubCodeGenerator.hpp"
#include "runtime/thread.hpp"

ShenandoahBarrierSetAssembler* ShenandoahBarrierSetAssembler::bsasm() {
  return ShenandoahBarrierSet::barrier_set()->bsasm();
}

#define __ masm->

void ShenandoahBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, bool dest_uninitialized,
                                                       Register src, Register dst, Register count) {
  if ((ShenandoahSATBBarrier && !dest_uninitialized) || ShenandoahStoreValEnqueueBarrier || ShenandoahLoadRefBarrier) {

    Label done;

    // Avoid calling runtime if count == 0
    __ cbz(count, done);

    // Is GC active?
    Address gc_state(rthread, in_bytes(JavaThread::gc_state_offset()));
    __ ldrb(rscratch1, gc_state);
    if (ShenandoahSATBBarrier && dest_uninitialized) {
      __ tbz(rscratch1, ShenandoahHeap::HAS_FORWARDED_BITPOS, done);
    } else {
      __ mov(rscratch2, ShenandoahHeap::HAS_FORWARDED | ShenandoahHeap::MARKING);
      __ tst(rscratch1, rscratch2);
      __ br(Assembler::EQ, done);
    }

    __ push_call_clobbered_registers();
    if (UseCompressedOops) {
        __ call_VM_leaf(CAST_FROM_FN_PTR(address, ShenandoahRuntime::arraycopy_barrier_narrow_oop_entry), src, dst, count);
    } else {
        __ call_VM_leaf(CAST_FROM_FN_PTR(address, ShenandoahRuntime::arraycopy_barrier_oop_entry), src, dst, count);
    }
    __ pop_call_clobbered_registers();
    __ bind(done);
  }
}

void ShenandoahBarrierSetAssembler::resolve_forward_pointer(MacroAssembler* masm, Register dst, Register tmp) {
  assert(ShenandoahCASBarrier, "should be enabled");
  Label is_null;
  __ cbz(dst, is_null);
  resolve_forward_pointer_not_null(masm, dst, tmp);
  __ bind(is_null);
}

// IMPORTANT: This must preserve all registers, even rscratch1 and rscratch2, except those explicitely
// passed in.
void ShenandoahBarrierSetAssembler::resolve_forward_pointer_not_null(MacroAssembler* masm, Register dst, Register tmp) {
  assert(ShenandoahCASBarrier || ShenandoahLoadRefBarrier, "should be enabled");
  // The below loads the mark word, checks if the lowest two bits are
  // set, and if so, clear the lowest two bits and copy the result
  // to dst. Otherwise it leaves dst alone.
  // Implementing this is surprisingly awkward. I do it here by:
  // - Inverting the mark word
  // - Test lowest two bits == 0
  // - If so, set the lowest two bits
  // - Invert the result back, and copy to dst

  bool borrow_reg = (tmp == noreg);
  if (borrow_reg) {
    // No free registers available. Make one useful.
    tmp = rscratch1;
    if (tmp == dst) {
      tmp = rscratch2;
    }
    __ push(RegSet::of(tmp), sp);
  }

  assert_different_registers(tmp, dst);

  Label done;
  __ ldr(tmp, Address(dst, oopDesc::mark_offset_in_bytes()));
  __ eon(tmp, tmp, zr);
  __ ands(zr, tmp, markOopDesc::lock_mask_in_place);
  __ br(Assembler::NE, done);
  __ orr(tmp, tmp, markOopDesc::marked_value);
  __ eon(dst, tmp, zr);
  __ bind(done);

  if (borrow_reg) {
    __ pop(RegSet::of(tmp), sp);
  }
}

void ShenandoahBarrierSetAssembler::load_reference_barrier(MacroAssembler* masm, Register dst, Address load_addr) {
  if (!ShenandoahLoadRefBarrier) {
    return;
  }

  assert(dst != rscratch2, "need rscratch2");
  assert_different_registers(load_addr.base(), load_addr.index(), rscratch1, rscratch2);

  bool is_narrow  = UseCompressedOops;

  Label heap_stable, not_cset;
  __ enter();
  Address gc_state(rthread, in_bytes(JavaThread::gc_state_offset()));
  __ ldrb(rscratch2, gc_state);

  // Check for heap stability
  __ tbz(rscratch2, ShenandoahHeap::HAS_FORWARDED_BITPOS, heap_stable);

  // use r1 for load address
  Register result_dst = dst;
  if (dst == r1) {
    __ mov(rscratch1, dst);
    dst = rscratch1;
  }

  // Save r0 and r1, unless it is an output register
  RegSet to_save = RegSet::of(r0, r1) - result_dst;
  __ push(to_save, sp);
  __ lea(r1, load_addr);
  __ mov(r0, dst);

  // Test for in-cset
  __ mov(rscratch2, ShenandoahHeap::in_cset_fast_test_addr());
  __ lsr(rscratch1, r0, ShenandoahHeapRegion::region_size_bytes_shift_jint());
  __ ldrb(rscratch2, Address(rscratch2, rscratch1));
  __ tbz(rscratch2, 0, not_cset);

  __ push_call_clobbered_registers();
  if (is_narrow) {
    __ mov(lr, CAST_FROM_FN_PTR(address, ShenandoahRuntime::load_reference_barrier_narrow));
  } else {
    __ mov(lr, CAST_FROM_FN_PTR(address, ShenandoahRuntime::load_reference_barrier));
  }
  __ blr(lr);
  __ mov(rscratch1, r0);
  __ pop_call_clobbered_registers();
  __ mov(r0, rscratch1);

  __ bind(not_cset);

  __ mov(result_dst, r0);
  __ pop(to_save, sp);

  __ bind(heap_stable);
  __ leave();
}

void ShenandoahBarrierSetAssembler::storeval_barrier(MacroAssembler* masm, Register dst, Register tmp) {
  if (ShenandoahStoreValEnqueueBarrier) {
    // Save possibly live regs.
    RegSet live_regs = RegSet::range(r0, r4) - dst;
    __ push(live_regs, sp);
    __ strd(v0, __ pre(sp, 2 * -wordSize));

    __ g1_write_barrier_pre(noreg, dst, rthread, tmp, true, false);

    // Restore possibly live regs.
    __ ldrd(v0, __ post(sp, 2 * wordSize));
    __ pop(live_regs, sp);
  }
}

void ShenandoahBarrierSetAssembler::load_heap_oop(MacroAssembler* masm, Register dst, Address src) {
  Register result_dst = dst;

  // Preserve src location for LRB
  if (dst == src.base() || dst == src.index()) {
    dst = rscratch1;
  }
  assert_different_registers(dst, src.base(), src.index());

  if (UseCompressedOops) {
    __ ldrw(dst, src);
    __ decode_heap_oop(dst);
  } else {
    __ ldr(dst, src);
  }

  load_reference_barrier(masm, dst, src);

  if (dst != result_dst) {
    __ mov(result_dst, dst);
    dst = result_dst;
  }
}

void ShenandoahBarrierSetAssembler::cmpxchg_oop(MacroAssembler* masm, Register addr, Register expected, Register new_val,
                                                bool acquire, bool release, bool weak, bool is_cae,
                                                Register result) {

  Register tmp1 = rscratch1;
  Register tmp2 = rscratch2;
  bool is_narrow = UseCompressedOops;
  Assembler::operand_size size = is_narrow ? Assembler::word : Assembler::xword;

  assert_different_registers(addr, expected, new_val, tmp1, tmp2);

  Label retry, done, fail;

  // CAS, using LL/SC pair.
  __ bind(retry);
  __ load_exclusive(tmp1, addr, size, acquire);
  if (is_narrow) {
    __ cmpw(tmp1, expected);
  } else {
    __ cmp(tmp1, expected);
  }
  __ br(Assembler::NE, fail);
  __ store_exclusive(tmp2, new_val, addr, size, release);
  if (weak) {
    __ cmpw(tmp2, 0u); // If the store fails, return NE to our caller
  } else {
    __ cbnzw(tmp2, retry);
  }
  __ b(done);

  __ bind(fail);
  // Check if rb(expected)==rb(tmp1)
  // Shuffle registers so that we have memory value ready for next expected.
  __ mov(tmp2, expected);
  __ mov(expected, tmp1);
  if (is_narrow) {
    __ decode_heap_oop(tmp1, tmp1);
    __ decode_heap_oop(tmp2, tmp2);
  }
  resolve_forward_pointer(masm, tmp1);
  resolve_forward_pointer(masm, tmp2);
  __ cmp(tmp1, tmp2);
  // Retry with expected now being the value we just loaded from addr.
  __ br(Assembler::EQ, retry);
  if (is_cae && is_narrow) {
    // For cmp-and-exchange and narrow oops, we need to restore
    // the compressed old-value. We moved it to 'expected' a few lines up.
    __ mov(result, expected);
  }
  __ bind(done);

  if (is_cae) {
    __ mov(result, tmp1);
  } else {
    __ cset(result, Assembler::EQ);
  }
}

#undef __

#ifdef COMPILER1

#define __ ce->masm()->

void ShenandoahBarrierSetAssembler::gen_load_reference_barrier_stub(LIR_Assembler* ce, ShenandoahLoadReferenceBarrierStub* stub) {
  __ bind(*stub->entry());

  Register obj = stub->obj()->as_register();
  Register res = stub->result()->as_register();
  Register addr = stub->addr()->as_pointer_register();
  Register tmp1 = stub->tmp1()->as_register();
  Register tmp2 = stub->tmp2()->as_register();

  assert(res == r0, "result must arrive in r0");

  if (res != obj) {
    __ mov(res, obj);
  }

  // Check for null.
  __ cbz(res, *stub->continuation());

  // Check for object in cset.
  __ mov(tmp2, ShenandoahHeap::in_cset_fast_test_addr());
  __ lsr(tmp1, res, ShenandoahHeapRegion::region_size_bytes_shift_jint());
  __ ldrb(tmp2, Address(tmp2, tmp1));
  __ cbz(tmp2, *stub->continuation());

  // Check if object is already forwarded.
  Label slow_path;
  __ ldr(tmp1, Address(res, oopDesc::mark_offset_in_bytes()));
  __ eon(tmp1, tmp1, zr);
  __ ands(zr, tmp1, markOopDesc::lock_mask_in_place);
  __ br(Assembler::NE, slow_path);

  // Decode forwarded object.
  __ orr(tmp1, tmp1, markOopDesc::marked_value);
  __ eon(res, tmp1, zr);
  __ b(*stub->continuation());

  __ bind(slow_path);
  ce->store_parameter(res, 0);
  ce->store_parameter(addr, 1);
  __ far_call(RuntimeAddress(Runtime1::entry_for(Runtime1::shenandoah_lrb_slow_id)));

  __ b(*stub->continuation());
}

#undef __

#endif // COMPILER1
