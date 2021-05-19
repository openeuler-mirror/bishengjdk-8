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

#ifndef CPU_X86_GC_SHENANDOAH_SHENANDOAHBARRIERSETASSEMBLER_X86_HPP
#define CPU_X86_GC_SHENANDOAH_SHENANDOAHBARRIERSETASSEMBLER_X86_HPP

#include "asm/macroAssembler.hpp"
#include "memory/allocation.hpp"
#ifdef COMPILER1
class LIR_Assembler;
class ShenandoahLoadReferenceBarrierStub;
class StubAssembler;
class StubCodeGenerator;
#endif

class ShenandoahBarrierSetAssembler : public CHeapObj<mtGC> {
private:

  void storeval_barrier_impl(MacroAssembler* masm, Register dst, Register tmp);

public:
  static ShenandoahBarrierSetAssembler* bsasm();

  void storeval_barrier(MacroAssembler* masm, Register dst, Register tmp);
#ifdef COMPILER1
  void gen_load_reference_barrier_stub(LIR_Assembler* ce, ShenandoahLoadReferenceBarrierStub* stub);
#endif

  void load_reference_barrier(MacroAssembler* masm, Register dst, Address src);

  void load_heap_oop(MacroAssembler* masm, Register dst, Address src);

  virtual void arraycopy_prologue(MacroAssembler* masm, bool dest_uninitialized,
                                  Register src, Register dst, Register count);
  virtual void cmpxchg_oop(MacroAssembler* masm,
                           Register res, Address addr, Register oldval, Register newval,
                           bool exchange, Register tmp1, Register tmp2);
};

#endif // CPU_X86_GC_SHENANDOAH_SHENANDOAHBARRIERSETASSEMBLER_X86_HPP
