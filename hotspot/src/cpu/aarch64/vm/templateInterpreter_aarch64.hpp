/*
 * Copyright (c) 2013, Red Hat Inc.
 * Copyright (c) 1997, 2010, Oracle and/or its affiliates.
 * All rights reserved.
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

#ifndef CPU_AARCH64_VM_TEMPLATEINTERPRETER_AARCH64_HPP
#define CPU_AARCH64_VM_TEMPLATEINTERPRETER_AARCH64_HPP


  public:
  static void patch_method(AbstractInterpreter::MethodKind kind) {
    unsigned value_offset = java_lang_String::value_offset_in_bytes();
    unsigned offset_offset = 0;
    address entry = entry_for_kind(kind);

    if (entry == NULL) {
      return;
    }

    switch (kind) {
    case AbstractInterpreter::org_netlib_blas_Dgemm_dgemm:
      if (StubRoutines::_dgemmDgemm == NULL) {
        break;
      }
      // 0 : ldr x0, [x20, #136]
      // 1 : ldr x1, [x20, #128]
      // 2 : mov x2, #0x0  ==================>  ldr w2, [x0, <java_lang_String::value_offset_in_bytes()>]
      // 3 : mov x3, #0x0  ==================>  ldr w3, [x1, <java_lang_String::value_offset_in_bytes()>]
      // 4 : orr x4, xzr, #0xc  =============>  orr x4, xzr, <java_lang_String::value_offset_in_bytes()>
      if (java_lang_String::has_offset_field()) {
        guarantee(Instruction_aarch64::extract(((unsigned*)entry)[2], 31, 23) == 0b110100101 &&
               Instruction_aarch64::extract(((unsigned*)entry)[3], 31, 23) == 0b110100101,
               "wrong insns in patch");
        offset_offset = java_lang_String::offset_offset_in_bytes();
        // ldr w2, [x0, <java_lang_String::value_offset_in_bytes()>]
        address tmp = entry + 4 * 2;
        Instruction_aarch64::patch(tmp, 31, 22, 0b1011100101);         // opc
        Instruction_aarch64::patch(tmp, 21, 10, offset_offset >> 2);   // imm12
        Instruction_aarch64::patch(tmp, 9, 5, 0);                      // Rn
        Instruction_aarch64::patch(tmp, 4, 0, 2);                      // Rt
        // ldr w3, [x1, <java_lang_String::value_offset_in_bytes()>]
        tmp = entry + 4 * 3;
        Instruction_aarch64::patch(tmp, 31, 22, 0b1011100101);         // opc
        Instruction_aarch64::patch(tmp, 21, 10, offset_offset >> 2);   // imm12
        Instruction_aarch64::patch(tmp, 9, 5, 1);                      // Rn
        Instruction_aarch64::patch(tmp, 4, 0, 3);                      // Rt
      }
      guarantee(Instruction_aarch64::extract(((unsigned*)entry)[4], 31, 23) == 0b101100100 &&
             Instruction_aarch64::extract(((unsigned*)entry)[4], 9, 0) == 0b1111100100, "wrong insns in patch");
      Instruction_aarch64::patch(entry + 4 * 4, 22, 10,
                                 (uint64_t)encode_logical_immediate(false, (uint64_t)value_offset));   // imm16
      ICache::invalidate_range(entry, 4 * 5);
      break;
    case AbstractInterpreter::org_netlib_blas_Dgemv_dgemv:
      if (StubRoutines::_dgemvDgemv == NULL) {
        break;
      }
      // 0 : ldr x0, [x20, #120]
      // 1 : mov x1, #0x0  ==================>  ldr w1, [r0, <java_lang_String::offset_offset_in_bytes()>]
      // 2 : orr x2, xzr, #0xc  =============>  orr x2, xzr, <java_lang_String::value_offset_in_bytes()>
      if (java_lang_String::has_offset_field()) {
        guarantee(Instruction_aarch64::extract(((unsigned*)entry)[1], 31, 23) == 0b110100101, "wrong insns in patch");
        offset_offset = java_lang_String::offset_offset_in_bytes();
        // ldr w1, [x0, <java_lang_String::value_offset_in_bytes()>]
        address tmp = entry + 4 * 1;
        Instruction_aarch64::patch(tmp, 31, 22, 0b1011100101);         // opc
        Instruction_aarch64::patch(tmp, 21, 10, offset_offset >> 2);   // imm12
        Instruction_aarch64::patch(tmp, 9, 5, 0);                      // Rn
        Instruction_aarch64::patch(tmp, 4, 0, 1);                      // Rt
      }
      guarantee(Instruction_aarch64::extract(((unsigned*)entry)[2], 31, 23) == 0b101100100 &&
             Instruction_aarch64::extract(((unsigned*)entry)[2], 9, 0) == 0b1111100010, "wrong insns in patch");
      Instruction_aarch64::patch(entry + 4 * 2, 22, 10,
                                 (uint64_t)encode_logical_immediate(false, (uint64_t)value_offset));   // imm16
      ICache::invalidate_range(entry, 4 * 3);
      break;
    default:
      break;
    }
  }

  protected:

  // Size of interpreter code.  Increase if too small.  Interpreter will
  // fail with a guarantee ("not enough space for interpreter generation");
  // if too small.
  // Run with +PrintInterpreter to get the VM to print out the size.
  // Max size with JVMTI
  const static int InterpreterCodeSize = 200 * 1024;

#endif // CPU_AARCH64_VM_TEMPLATEINTERPRETER_AARCH64_HPP
