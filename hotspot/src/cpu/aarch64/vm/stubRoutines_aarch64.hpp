/*
 * Copyright (c) 2013, Red Hat Inc.
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates.
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

#ifndef CPU_AARCH64_VM_STUBROUTINES_AARCH64_HPP
#define CPU_AARCH64_VM_STUBROUTINES_AARCH64_HPP

// This file holds the platform specific parts of the StubRoutines
// definition. See stubRoutines.hpp for a description on how to
// extend it.

static bool    returns_to_call_stub(address return_pc)   {
  return return_pc == _call_stub_return_address;
}

enum platform_dependent_constants {
  code_size1 = 19000,          // simply increase if too small (assembler will crash if too small)
  code_size2 = 45000           // simply increase if too small (assembler will crash if too small)
};

class aarch64 {
 friend class StubGenerator;

 private:
  static address _get_previous_fp_entry;
  static address _get_previous_sp_entry;

  static address _f2i_fixup;
  static address _f2l_fixup;
  static address _d2i_fixup;
  static address _d2l_fixup;

  static address _float_sign_mask;
  static address _float_sign_flip;
  static address _double_sign_mask;
  static address _double_sign_flip;

  static address _zero_longs;

  static address _large_arrays_hashcode_boolean;
  static address _large_arrays_hashcode_byte;
  static address _large_arrays_hashcode_char;
  static address _large_arrays_hashcode_int;
  static address _large_arrays_hashcode_short;

 public:
  static address _convert_masked_utf8_to_utf16;
  static address _scalar_convert_utf8_to_utf16;

  static address get_previous_fp_entry()
  {
    return _get_previous_fp_entry;
  }

  static address get_previous_sp_entry()
  {
    return _get_previous_sp_entry;
  }

  static address f2i_fixup()
  {
    return _f2i_fixup;
  }

  static address f2l_fixup()
  {
    return _f2l_fixup;
  }

  static address d2i_fixup()
  {
    return _d2i_fixup;
  }

  static address d2l_fixup()
  {
    return _d2l_fixup;
  }

  static address float_sign_mask()
  {
    return _float_sign_mask;
  }

  static address float_sign_flip()
  {
    return _float_sign_flip;
  }

  static address double_sign_mask()
  {
    return _double_sign_mask;
  }

  static address double_sign_flip()
  {
    return _double_sign_flip;
  }

  static address get_zero_longs()
  {
    return _zero_longs;
  }

  static address large_arrays_hashcode(BasicType eltype) {
    switch (eltype) {
    case T_BOOLEAN:
      return _large_arrays_hashcode_boolean;
    case T_BYTE:
      return _large_arrays_hashcode_byte;
    case T_CHAR:
      return _large_arrays_hashcode_char;
    case T_SHORT:
      return _large_arrays_hashcode_short;
    case T_INT:
      return _large_arrays_hashcode_int;
    default:
      ShouldNotReachHere();
    }

    return NULL;
  }

  static address scalar_convert_utf8_to_utf16()
  {
    return _scalar_convert_utf8_to_utf16;
  }

  static address convert_masked_utf8_to_utf16()
  {
    return _convert_masked_utf8_to_utf16;
  }

private:
  static juint    _crc_table[];
  static jubyte _pack_1_2_3_utf8_bytes[256][17];
  static jubyte _pack_1_2_utf8_bytes[256][17];
  static jubyte _shufutf8[209][16];
  static jubyte _utf8bigindex[4096][2];

};

#endif // CPU_AARCH64_VM_STUBROUTINES_AARCH64_HPP
