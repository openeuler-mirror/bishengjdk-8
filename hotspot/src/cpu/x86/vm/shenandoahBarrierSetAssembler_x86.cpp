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

void ShenandoahBarrierSetAssembler::load_reference_barrier_not_null(MacroAssembler* masm, Register dst) {
  assert(ShenandoahLoadRefBarrier, "Should be enabled");

  Label done;

#ifdef _LP64
  Register thread = r15_thread;
#else
  Register thread = rcx;
  if (thread == dst) {
    thread = rbx;
  }
  __ push(thread);
  __ get_thread(thread);
#endif
  assert_different_registers(dst, thread);

  Address gc_state(thread, in_bytes(JavaThread::gc_state_offset()));
  __ testb(gc_state, ShenandoahHeap::HAS_FORWARDED);
  __ jcc(Assembler::zero, done);

  {
    __ save_vector_registers();

    __ subptr(rsp, LP64_ONLY(16) NOT_LP64(8) * wordSize);

    __ movptr(Address(rsp,  0 * wordSize), rax);
    __ movptr(Address(rsp,  1 * wordSize), rcx);
    __ movptr(Address(rsp,  2 * wordSize), rdx);
    __ movptr(Address(rsp,  3 * wordSize), rbx);
    // skip rsp
    __ movptr(Address(rsp,  5 * wordSize), rbp);
    __ movptr(Address(rsp,  6 * wordSize), rsi);
    __ movptr(Address(rsp,  7 * wordSize), rdi);
#ifdef _LP64
    __ movptr(Address(rsp,  8 * wordSize),  r8);
    __ movptr(Address(rsp,  9 * wordSize),  r9);
    __ movptr(Address(rsp, 10 * wordSize), r10);
    __ movptr(Address(rsp, 11 * wordSize), r11);
    __ movptr(Address(rsp, 12 * wordSize), r12);
    __ movptr(Address(rsp, 13 * wordSize), r13);
    __ movptr(Address(rsp, 14 * wordSize), r14);
    __ movptr(Address(rsp, 15 * wordSize), r15);
#endif
  }
  __ super_call_VM_leaf(CAST_FROM_FN_PTR(address, ShenandoahRuntime::load_reference_barrier_interpreter), dst);
  {
#ifdef _LP64
    __ movptr(r15, Address(rsp, 15 * wordSize));
    __ movptr(r14, Address(rsp, 14 * wordSize));
    __ movptr(r13, Address(rsp, 13 * wordSize));
    __ movptr(r12, Address(rsp, 12 * wordSize));
    __ movptr(r11, Address(rsp, 11 * wordSize));
    __ movptr(r10, Address(rsp, 10 * wordSize));
    __ movptr(r9,  Address(rsp,  9 * wordSize));
    __ movptr(r8,  Address(rsp,  8 * wordSize));
#endif
    __ movptr(rdi, Address(rsp,  7 * wordSize));
    __ movptr(rsi, Address(rsp,  6 * wordSize));
    __ movptr(rbp, Address(rsp,  5 * wordSize));
    // skip rsp
    __ movptr(rbx, Address(rsp,  3 * wordSize));
    __ movptr(rdx, Address(rsp,  2 * wordSize));
    __ movptr(rcx, Address(rsp,  1 * wordSize));
    if (dst != rax) {
      __ movptr(dst, rax);
      __ movptr(rax, Address(rsp, 0 * wordSize));
    }
    __ addptr(rsp, LP64_ONLY(16) NOT_LP64(8) * wordSize);

    __ restore_vector_registers();
  }
  __ bind(done);

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

void ShenandoahBarrierSetAssembler::load_reference_barrier(MacroAssembler* masm, Register dst) {
  if (ShenandoahLoadRefBarrier) {
    Label done;
    __ testptr(dst, dst);
    __ jcc(Assembler::zero, done);
    load_reference_barrier_not_null(masm, dst);
    __ bind(done);
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

  if (res != obj) {
    __ mov(res, obj);
  }

  // Check for null.
  __ testptr(res, res);
  __ jcc(Assembler::zero, done);

  load_reference_barrier_not_null(ce->masm(), res);

  __ bind(done);
  __ jmp(*stub->continuation());
}

#undef __

#endif // COMPILER1
