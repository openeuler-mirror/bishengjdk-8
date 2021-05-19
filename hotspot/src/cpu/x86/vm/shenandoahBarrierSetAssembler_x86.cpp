/*
 * Copyright (c) 2018, 2020 Red Hat, Inc. All rights reserved.
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
#include "macroAssembler_x86.hpp"
#include "shenandoahBarrierSetAssembler_x86.hpp"
#include "gc_implementation/shenandoah/shenandoahBarrierSet.hpp"
#include "gc_implementation/shenandoah/shenandoahForwarding.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.hpp"
#include "gc_implementation/shenandoah/shenandoahHeapRegion.hpp"
#include "gc_implementation/shenandoah/shenandoahRuntime.hpp"
#include "gc_implementation/shenandoah/c1/shenandoahBarrierSetC1.hpp"
#include "runtime/stubCodeGenerator.hpp"

ShenandoahBarrierSetAssembler* ShenandoahBarrierSetAssembler::bsasm() {
  return ShenandoahBarrierSet::barrier_set()->bsasm();
}

#define __ masm->

static void save_xmm_registers(MacroAssembler* masm) {
    __ subptr(rsp, 64);
    __ movdbl(Address(rsp, 0), xmm0);
    __ movdbl(Address(rsp, 8), xmm1);
    __ movdbl(Address(rsp, 16), xmm2);
    __ movdbl(Address(rsp, 24), xmm3);
    __ movdbl(Address(rsp, 32), xmm4);
    __ movdbl(Address(rsp, 40), xmm5);
    __ movdbl(Address(rsp, 48), xmm6);
    __ movdbl(Address(rsp, 56), xmm7);
}

static void restore_xmm_registers(MacroAssembler* masm) {
    __ movdbl(xmm0, Address(rsp, 0));
    __ movdbl(xmm1, Address(rsp, 8));
    __ movdbl(xmm2, Address(rsp, 16));
    __ movdbl(xmm3, Address(rsp, 24));
    __ movdbl(xmm4, Address(rsp, 32));
    __ movdbl(xmm5, Address(rsp, 40));
    __ movdbl(xmm6, Address(rsp, 48));
    __ movdbl(xmm7, Address(rsp, 56));
    __ addptr(rsp, 64);
}

void ShenandoahBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, bool dest_uninitialized,
                                                       Register src, Register dst, Register count) {

  if ((ShenandoahSATBBarrier && !dest_uninitialized) || ShenandoahStoreValEnqueueBarrier || ShenandoahLoadRefBarrier) {
#ifdef _LP64
    Register thread = r15_thread;
#else
    Register thread = rax;
    if (thread == src || thread == dst || thread == count) {
      thread = rbx;
    }
    if (thread == src || thread == dst || thread == count) {
      thread = rcx;
    }
    if (thread == src || thread == dst || thread == count) {
      thread = rdx;
    }
    __ push(thread);
    __ get_thread(thread);
#endif
    assert_different_registers(src, dst, count, thread);

    Label done;
    // Short-circuit if count == 0.
    __ testptr(count, count);
    __ jcc(Assembler::zero, done);

    // Avoid runtime call when not active.
    Address gc_state(thread, in_bytes(JavaThread::gc_state_offset()));
    int flags;
    if (ShenandoahSATBBarrier && dest_uninitialized) {
      flags = ShenandoahHeap::HAS_FORWARDED;
    } else {
      flags = ShenandoahHeap::HAS_FORWARDED | ShenandoahHeap::MARKING;
    }
    __ testb(gc_state, flags);
    __ jcc(Assembler::zero, done);

    __ pusha();                      // push registers

#ifdef _LP64
    assert(src == rdi, "expected");
    assert(dst == rsi, "expected");
    // commented-out for generate_conjoint_long_oop_copy(), call_VM_leaf() will move
    // register into right place.
    // assert(count == rdx, "expected");
    if (UseCompressedOops) {
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, ShenandoahRuntime::arraycopy_barrier_narrow_oop_entry),
                        src, dst, count);
    } else
#endif
    {
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, ShenandoahRuntime::arraycopy_barrier_oop_entry),
                      src, dst, count);
    }

    __ popa();
    __ bind(done);
    NOT_LP64(__ pop(thread);)
  }
}

void ShenandoahBarrierSetAssembler::load_reference_barrier(MacroAssembler* masm, Register dst, Address src) {
  if (!ShenandoahLoadRefBarrier) {
    return;
  }

  bool is_narrow  = UseCompressedOops;

  Label heap_stable, not_cset;

  __ block_comment("load_reference_barrier { ");

  // Check if GC is active
#ifdef _LP64
  Register thread = r15_thread;
#else
  Register thread = rsi;
  if (thread == dst) {
    thread = rbx;
  }
  assert_different_registers(dst, src.base(), src.index(), thread);
  __ push(thread);
  __ get_thread(thread);
#endif

  Address gc_state(thread, in_bytes(JavaThread::gc_state_offset()));
  __ testb(gc_state, ShenandoahHeap::HAS_FORWARDED);
  __ jcc(Assembler::zero, heap_stable);

  Register tmp1 = noreg, tmp2 = noreg;

  // Test for object in cset
  // Allocate temporary registers
  for (int i = 0; i < 8; i++) {
    Register r = as_Register(i);
    if (r != rsp && r != rbp && r != dst && r != src.base() && r != src.index()) {
      if (tmp1 == noreg) {
        tmp1 = r;
      } else {
        tmp2 = r;
        break;
      }
    }
  }
  assert(tmp1 != noreg, "tmp1 allocated");
  assert(tmp2 != noreg, "tmp2 allocated");
  assert_different_registers(tmp1, tmp2, src.base(), src.index());
  assert_different_registers(tmp1, tmp2, dst);

  __ push(tmp1);
  __ push(tmp2);

  // Optimized cset-test
  __ movptr(tmp1, dst);
  __ shrptr(tmp1, ShenandoahHeapRegion::region_size_bytes_shift_jint());
  __ movptr(tmp2, (intptr_t) ShenandoahHeap::in_cset_fast_test_addr());
  __ movbool(tmp1, Address(tmp1, tmp2, Address::times_1));
  __ testbool(tmp1);
  __ jcc(Assembler::zero, not_cset);

  uint num_saved_regs = 4 + (dst != rax ? 1 : 0) LP64_ONLY(+4);
  __ subptr(rsp, num_saved_regs * wordSize);
  uint slot = num_saved_regs;
  if (dst != rax) {
    __ movptr(Address(rsp, (--slot) * wordSize), rax);
  }
  __ movptr(Address(rsp, (--slot) * wordSize), rcx);
  __ movptr(Address(rsp, (--slot) * wordSize), rdx);
  __ movptr(Address(rsp, (--slot) * wordSize), rdi);
  __ movptr(Address(rsp, (--slot) * wordSize), rsi);
#ifdef _LP64
  __ movptr(Address(rsp, (--slot) * wordSize), r8);
  __ movptr(Address(rsp, (--slot) * wordSize), r9);
  __ movptr(Address(rsp, (--slot) * wordSize), r10);
  __ movptr(Address(rsp, (--slot) * wordSize), r11);
  // r12-r15 are callee saved in all calling conventions
#endif
  assert(slot == 0, "must use all slots");

  // Shuffle registers such that dst is in c_rarg0 and addr in c_rarg1.
#ifdef _LP64
  Register arg0 = c_rarg0, arg1 = c_rarg1;
#else
  Register arg0 = rdi, arg1 = rsi;
#endif
  if (dst == arg1) {
    __ lea(arg0, src);
    __ xchgptr(arg1, arg0);
  } else {
    __ lea(arg1, src);
    __ movptr(arg0, dst);
  }

  save_xmm_registers(masm);
  if (is_narrow) {
    __ super_call_VM_leaf(CAST_FROM_FN_PTR(address, ShenandoahRuntime::load_reference_barrier_narrow), arg0, arg1);
  } else {
    __ super_call_VM_leaf(CAST_FROM_FN_PTR(address, ShenandoahRuntime::load_reference_barrier), arg0, arg1);
  }
  restore_xmm_registers(masm);

#ifdef _LP64
  __ movptr(r11, Address(rsp, (slot++) * wordSize));
  __ movptr(r10, Address(rsp, (slot++) * wordSize));
  __ movptr(r9,  Address(rsp, (slot++) * wordSize));
  __ movptr(r8,  Address(rsp, (slot++) * wordSize));
#endif
  __ movptr(rsi, Address(rsp, (slot++) * wordSize));
  __ movptr(rdi, Address(rsp, (slot++) * wordSize));
  __ movptr(rdx, Address(rsp, (slot++) * wordSize));
  __ movptr(rcx, Address(rsp, (slot++) * wordSize));

  if (dst != rax) {
    __ movptr(dst, rax);
    __ movptr(rax, Address(rsp, (slot++) * wordSize));
  }

  assert(slot == num_saved_regs, "must use all slots");
  __ addptr(rsp, num_saved_regs * wordSize);

  __ bind(not_cset);

  __ pop(tmp2);
  __ pop(tmp1);

  __ bind(heap_stable);

  __ block_comment("} load_reference_barrier");

#ifndef _LP64
    __ pop(thread);
#endif
}

void ShenandoahBarrierSetAssembler::storeval_barrier(MacroAssembler* masm, Register dst, Register tmp) {
  if (ShenandoahStoreValEnqueueBarrier) {
    storeval_barrier_impl(masm, dst, tmp);
  }
}

void ShenandoahBarrierSetAssembler::storeval_barrier_impl(MacroAssembler* masm, Register dst, Register tmp) {
  assert(ShenandoahStoreValEnqueueBarrier, "should be enabled");

  if (dst == noreg) return;

  if (ShenandoahStoreValEnqueueBarrier) {
    // The set of registers to be saved+restored is the same as in the write-barrier above.
    // Those are the commonly used registers in the interpreter.
    __ pusha();
    // __ push_callee_saved_registers();
    __ subptr(rsp, 2 * Interpreter::stackElementSize);
    __ movdbl(Address(rsp, 0), xmm0);

#ifdef _LP64
    Register thread = r15_thread;
#else
    Register thread = rcx;
    if (thread == dst || thread == tmp) {
      thread = rdi;
    }
    if (thread == dst || thread == tmp) {
      thread = rbx;
    }
    __ get_thread(thread);
#endif
    assert_different_registers(dst, tmp, thread);

    __ g1_write_barrier_pre(noreg, dst, thread, tmp, true, false);
    __ movdbl(xmm0, Address(rsp, 0));
    __ addptr(rsp, 2 * Interpreter::stackElementSize);
    //__ pop_callee_saved_registers();
    __ popa();
  }
}

void ShenandoahBarrierSetAssembler::load_heap_oop(MacroAssembler* masm, Register dst, Address src) {
  Register result_dst = dst;
  // Preserve src location for LRB
  if (dst == src.base() || dst == src.index()) {
    dst = rdi;
    __ push(dst);
    assert_different_registers(dst, src.base(), src.index());
  }

#ifdef _LP64
  // FIXME: Must change all places where we try to load the klass.
  if (UseCompressedOops) {
    __ movl(dst, src);
    __ decode_heap_oop(dst);
  } else
#endif
    __ movptr(dst, src);

  load_reference_barrier(masm, dst, src);

  // Move loaded oop to final destination
  if (dst != result_dst) {
    __ movptr(result_dst, dst);
    __ pop(dst);
  }
}

// Special Shenandoah CAS implementation that handles false negatives
// due to concurrent evacuation.
void ShenandoahBarrierSetAssembler::cmpxchg_oop(MacroAssembler* masm,
                                                Register res, Address addr, Register oldval, Register newval,
                                                bool exchange, Register tmp1, Register tmp2) {
  assert(ShenandoahCASBarrier, "Should only be used when CAS barrier is enabled");
  assert(oldval == rax, "must be in rax for implicit use in cmpxchg");
  assert_different_registers(oldval, newval, tmp1, tmp2);

  Label L_success, L_failure;

  // Remember oldval for retry logic below
#ifdef _LP64
  if (UseCompressedOops) {
    __ movl(tmp1, oldval);
  } else
#endif
  {
    __ movptr(tmp1, oldval);
  }

  // Step 1. Fast-path.
  //
  // Try to CAS with given arguments. If successful, then we are done.

  if (os::is_MP()) __ lock();
#ifdef _LP64
  if (UseCompressedOops) {
    __ cmpxchgl(newval, addr);
  } else
#endif
  {
    __ cmpxchgptr(newval, addr);
  }
  __ jcc(Assembler::equal, L_success);

  // Step 2. CAS had failed. This may be a false negative.
  //
  // The trouble comes when we compare the to-space pointer with the from-space
  // pointer to the same object. To resolve this, it will suffice to resolve
  // the value from memory -- this will give both to-space pointers.
  // If they mismatch, then it was a legitimate failure.
  //
  // Before reaching to resolve sequence, see if we can avoid the whole shebang
  // with filters.

  // Filter: when offending in-memory value is NULL, the failure is definitely legitimate
  __ testptr(oldval, oldval);
  __ jcc(Assembler::zero, L_failure);

  // Filter: when heap is stable, the failure is definitely legitimate
#ifdef _LP64
  const Register thread = r15_thread;
#else
  const Register thread = tmp2;
  __ get_thread(thread);
#endif
  Address gc_state(thread, in_bytes(JavaThread::gc_state_offset()));
  __ testb(gc_state, ShenandoahHeap::HAS_FORWARDED);
  __ jcc(Assembler::zero, L_failure);

#ifdef _LP64
  if (UseCompressedOops) {
    __ movl(tmp2, oldval);
    __ decode_heap_oop(tmp2);
  } else
#endif
  {
    __ movptr(tmp2, oldval);
  }

  // Decode offending in-memory value.
  // Test if-forwarded
  __ testb(Address(tmp2, oopDesc::mark_offset_in_bytes()), markOopDesc::marked_value);
  __ jcc(Assembler::noParity, L_failure);  // When odd number of bits, then not forwarded
  __ jcc(Assembler::zero, L_failure);      // When it is 00, then also not forwarded

  // Load and mask forwarding pointer
  __ movptr(tmp2, Address(tmp2, oopDesc::mark_offset_in_bytes()));
  __ shrptr(tmp2, 2);
  __ shlptr(tmp2, 2);

#ifdef _LP64
  if (UseCompressedOops) {
    __ decode_heap_oop(tmp1); // decode for comparison
  }
#endif

  // Now we have the forwarded offender in tmp2.
  // Compare and if they don't match, we have legitimate failure
  __ cmpptr(tmp1, tmp2);
  __ jcc(Assembler::notEqual, L_failure);

  // Step 3. Need to fix the memory ptr before continuing.
  //
  // At this point, we have from-space oldval in the register, and its to-space
  // address is in tmp2. Let's try to update it into memory. We don't care if it
  // succeeds or not. If it does, then the retrying CAS would see it and succeed.
  // If this fixup fails, this means somebody else beat us to it, and necessarily
  // with to-space ptr store. We still have to do the retry, because the GC might
  // have updated the reference for us.

#ifdef _LP64
  if (UseCompressedOops) {
    __ encode_heap_oop(tmp2); // previously decoded at step 2.
  }
#endif

  if (os::is_MP()) __ lock();
#ifdef _LP64
  if (UseCompressedOops) {
    __ cmpxchgl(tmp2, addr);
  } else
#endif
  {
    __ cmpxchgptr(tmp2, addr);
  }

  // Step 4. Try to CAS again.
  //
  // This is guaranteed not to have false negatives, because oldval is definitely
  // to-space, and memory pointer is to-space as well. Nothing is able to store
  // from-space ptr into memory anymore. Make sure oldval is restored, after being
  // garbled during retries.
  //
#ifdef _LP64
  if (UseCompressedOops) {
    __ movl(oldval, tmp2);
  } else
#endif
  {
    __ movptr(oldval, tmp2);
  }

  if (os::is_MP()) __ lock();
#ifdef _LP64
  if (UseCompressedOops) {
    __ cmpxchgl(newval, addr);
  } else
#endif
  {
    __ cmpxchgptr(newval, addr);
  }
  if (!exchange) {
    __ jccb(Assembler::equal, L_success); // fastpath, peeking into Step 5, no need to jump
  }

  // Step 5. If we need a boolean result out of CAS, set the flag appropriately.
  // and promote the result. Note that we handle the flag from both the 1st and 2nd CAS.
  // Otherwise, failure witness for CAE is in oldval on all paths, and we can return.

  if (exchange) {
    __ bind(L_failure);
    __ bind(L_success);
  } else {
    assert(res != NULL, "need result register");

    Label exit;
    __ bind(L_failure);
    __ xorptr(res, res);
    __ jmpb(exit);

    __ bind(L_success);
    __ movptr(res, 1);
    __ bind(exit);
  }
}

#undef __

#ifdef COMPILER1

#define __ ce->masm()->

void ShenandoahBarrierSetAssembler::gen_load_reference_barrier_stub(LIR_Assembler* ce, ShenandoahLoadReferenceBarrierStub* stub) {
  __ bind(*stub->entry());

  Label done;
  Register obj = stub->obj()->as_register();
  Register res = stub->result()->as_register();
  Register addr = stub->addr()->as_pointer_register();
  Register tmp1 = stub->tmp1()->as_register();
  Register tmp2 = stub->tmp2()->as_register();
  assert_different_registers(obj, res, addr, tmp1, tmp2);

  Label slow_path;

  assert(res == rax, "result must arrive in rax");

  if (res != obj) {
    __ mov(res, obj);
  }

  // Check for null.
  __ testptr(res, res);
  __ jcc(Assembler::zero, *stub->continuation());

  // Check for object being in the collection set.
  __ mov(tmp1, res);
  __ shrptr(tmp1, ShenandoahHeapRegion::region_size_bytes_shift_jint());
  __ movptr(tmp2, (intptr_t) ShenandoahHeap::in_cset_fast_test_addr());
#ifdef _LP64
  __ movbool(tmp2, Address(tmp2, tmp1, Address::times_1));
  __ testbool(tmp2);
#else
  // On x86_32, C1 register allocator can give us the register without 8-bit support.
  // Do the full-register access and test to avoid compilation failures.
  __ movptr(tmp2, Address(tmp2, tmp1, Address::times_1));
  __ testptr(tmp2, 0xFF);
#endif
  __ jcc(Assembler::zero, *stub->continuation());

  __ bind(slow_path);
  ce->store_parameter(res, 0);
  ce->store_parameter(addr, 1);
  __ call(RuntimeAddress(Runtime1::entry_for(Runtime1::shenandoah_lrb_slow_id)));

  __ jmp(*stub->continuation());
}

#undef __

#endif // COMPILER1
