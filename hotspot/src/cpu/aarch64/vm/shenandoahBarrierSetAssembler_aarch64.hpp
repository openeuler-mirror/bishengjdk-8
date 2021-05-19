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

#ifndef CPU_AARCH64_GC_SHENANDOAH_SHENANDOAHBARRIERSETASSEMBLER_AARCH64_HPP
#define CPU_AARCH64_GC_SHENANDOAH_SHENANDOAHBARRIERSETASSEMBLER_AARCH64_HPP

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

  void resolve_forward_pointer(MacroAssembler* masm, Register dst, Register tmp = noreg);
  void resolve_forward_pointer_not_null(MacroAssembler* masm, Register dst, Register tmp = noreg);

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
                           Register addr, Register expected, Register new_val,
                           bool acquire, bool release, bool weak, bool is_cae,
                           Register result);
};

#endif // CPU_AARCH64_GC_SHENANDOAH_SHENANDOAHBARRIERSETASSEMBLER_AARCH64_HPP
