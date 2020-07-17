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

#ifndef SHARE_GC_SHENANDOAH_C1_SHENANDOAHBARRIERSETC1_HPP
#define SHARE_GC_SHENANDOAH_C1_SHENANDOAHBARRIERSETC1_HPP

#include "c1/c1_CodeStubs.hpp"
#include "memory/allocation.hpp"

class LIRGenerator;

class ShenandoahLoadReferenceBarrierStub: public CodeStub {
  friend class ShenandoahBarrierSetC1;
private:
  LIR_Opr _obj;
  LIR_Opr _result;

public:
  ShenandoahLoadReferenceBarrierStub(LIR_Opr obj, LIR_Opr result) :
    _obj(obj), _result(result)
  {
    assert(_obj->is_register(), "should be register");
    assert(_result->is_register(), "should be register");
  }

  LIR_Opr obj() const { return _obj; }
  LIR_Opr result() const { return _result; }

  virtual void emit_code(LIR_Assembler* e);
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case();
    visitor->do_input(_obj);
    visitor->do_temp(_result);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("ShenandoahLoadReferenceBarrierStub"); }
#endif // PRODUCT
};

class ShenandoahBarrierSetC1  : public CHeapObj<mtGC>{
private:
  CodeBlob* _pre_barrier_c1_runtime_code_blob;
public:
  static ShenandoahBarrierSetC1* bsc1();

  LIR_Opr load_reference_barrier(LIRGenerator* gen, LIR_Opr obj);
  LIR_Opr storeval_barrier(LIRGenerator* gen, LIR_Opr obj, CodeEmitInfo* info, bool patch);
private:
  LIR_Opr load_reference_barrier_impl(LIRGenerator* gen, LIR_Opr obj);
  LIR_Opr ensure_in_register(LIRGenerator* gen, LIR_Opr obj);

};

#endif // SHARE_GC_SHENANDOAH_C1_SHENANDOAHBARRIERSETC1_HPP
