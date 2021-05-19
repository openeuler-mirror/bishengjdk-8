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
#include "c1/c1_LIRGenerator.hpp"
#include "c1/c1_IR.hpp"
#include "gc_implementation/g1/satbQueue.hpp"
#include "gc_implementation/shenandoah/shenandoahForwarding.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahHeapRegion.hpp"
#include "gc_implementation/shenandoah/c1/shenandoahBarrierSetC1.hpp"

#ifdef TARGET_ARCH_aarch64
#include "shenandoahBarrierSetAssembler_aarch64.hpp"
#endif
#ifdef TARGET_ARCH_x86
#include "shenandoahBarrierSetAssembler_x86.hpp"
#endif

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void ShenandoahLoadReferenceBarrierStub::emit_code(LIR_Assembler* ce) {
  ShenandoahBarrierSetAssembler* bs = ShenandoahBarrierSetAssembler::bsasm();
  bs->gen_load_reference_barrier_stub(ce, this);
}

ShenandoahBarrierSetC1* ShenandoahBarrierSetC1::bsc1() {
  return ShenandoahBarrierSet::barrier_set()->bsc1();
}

LIR_Opr ShenandoahBarrierSetC1::load_reference_barrier(LIRGenerator* gen, LIR_Opr obj, LIR_Opr addr) {
  if (ShenandoahLoadRefBarrier) {
    return load_reference_barrier_impl(gen, obj, addr);
  } else {
    return obj;
  }
}

LIR_Opr ShenandoahBarrierSetC1::load_reference_barrier_impl(LIRGenerator* gen, LIR_Opr obj, LIR_Opr addr) {
  assert(ShenandoahLoadRefBarrier, "Should be enabled");
  obj = ensure_in_register(gen, obj, T_OBJECT);
  assert(obj->is_register(), "must be a register at this point");
  addr = ensure_in_register(gen, addr, T_ADDRESS);
  assert(addr->is_register(), "must be a register at this point");
  LIR_Opr result = gen->result_register_for(obj->value_type());
  __ move(obj, result);
  LIR_Opr tmp1 = gen->new_register(T_ADDRESS);
  LIR_Opr tmp2 = gen->new_register(T_ADDRESS);

  LIR_Opr thrd = gen->getThreadPointer();
  LIR_Address* active_flag_addr =
    new LIR_Address(thrd,
                    in_bytes(JavaThread::gc_state_offset()),
                    T_BYTE);
  // Read and check the gc-state-flag.
  LIR_Opr flag_val = gen->new_register(T_INT);
  __ load(active_flag_addr, flag_val);
  LIR_Opr mask = LIR_OprFact::intConst(ShenandoahHeap::HAS_FORWARDED |
                                       ShenandoahHeap::EVACUATION);
  LIR_Opr mask_reg = gen->new_register(T_INT);
  __ move(mask, mask_reg);

  if (TwoOperandLIRForm) {
    __ logical_and(flag_val, mask_reg, flag_val);
  } else {
    LIR_Opr masked_flag = gen->new_register(T_INT);
    __ logical_and(flag_val, mask_reg, masked_flag);
    flag_val = masked_flag;
  }
  __ cmp(lir_cond_notEqual, flag_val, LIR_OprFact::intConst(0));

  CodeStub* slow = new ShenandoahLoadReferenceBarrierStub(obj, addr, result, tmp1, tmp2);
  __ branch(lir_cond_notEqual, T_INT, slow);
  __ branch_destination(slow->continuation());

  return result;
}

LIR_Opr ShenandoahBarrierSetC1::ensure_in_register(LIRGenerator* gen, LIR_Opr obj, BasicType type) {
  if (!obj->is_register()) {
    LIR_Opr obj_reg;
    if (obj->is_constant()) {
      obj_reg = gen->new_register(type);
      __ move(obj, obj_reg);
    } else {
      obj_reg = gen->new_pointer_register();
      __ leal(obj, obj_reg);
    }
    obj = obj_reg;
  }
  return obj;
}

LIR_Opr ShenandoahBarrierSetC1::storeval_barrier(LIRGenerator* gen, LIR_Opr obj, CodeEmitInfo* info, bool patch) {
  if (ShenandoahStoreValEnqueueBarrier) {
    obj = ensure_in_register(gen, obj, T_OBJECT);
    gen->G1SATBCardTableModRef_pre_barrier(LIR_OprFact::illegalOpr, obj, false, false, NULL);
  }
  return obj;
}

LIR_Opr ShenandoahBarrierSetC1::resolve_address(LIRGenerator* gen, LIR_Address* addr, BasicType type, CodeEmitInfo* patch_emit_info) {
  LIR_Opr addr_opr = LIR_OprFact::address(addr);

  LIR_Opr resolved_addr = gen->new_pointer_register();
  if (patch_emit_info != NULL) {
    __ leal(addr_opr, resolved_addr, lir_patch_normal, new CodeEmitInfo(patch_emit_info));
  } else {
    __ leal(addr_opr, resolved_addr);
  }
  return LIR_OprFact::address(new LIR_Address(resolved_addr, type));
}
