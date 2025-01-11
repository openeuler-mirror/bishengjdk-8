/*
 * Copyright (c) 1999, 2015, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "opto/c2compiler.hpp"
#include "opto/runtime.hpp"
#if defined AD_MD_HPP
# include AD_MD_HPP
#elif defined TARGET_ARCH_MODEL_x86_32
# include "adfiles/ad_x86_32.hpp"
#elif defined TARGET_ARCH_MODEL_x86_64
# include "adfiles/ad_x86_64.hpp"
#elif defined TARGET_ARCH_MODEL_aarch64
# include "adfiles/ad_aarch64.hpp"
#elif defined TARGET_ARCH_MODEL_sparc
# include "adfiles/ad_sparc.hpp"
#elif defined TARGET_ARCH_MODEL_zero
# include "adfiles/ad_zero.hpp"
#elif defined TARGET_ARCH_MODEL_ppc_64
# include "adfiles/ad_ppc_64.hpp"
#endif

// register information defined by ADLC
extern const char register_save_policy[];
extern const int  register_save_type[];

const char* C2Compiler::retry_no_subsuming_loads() {
  return "retry without subsuming loads";
}
const char* C2Compiler::retry_no_escape_analysis() {
  return "retry without escape analysis";
}
const char* C2Compiler::retry_class_loading_during_parsing() {
  return "retry class loading during parsing";
}
bool C2Compiler::init_c2_runtime() {

  // Check assumptions used while running ADLC
  Compile::adlc_verification();
  assert(REG_COUNT <= ConcreteRegisterImpl::number_of_registers, "incompatible register counts");

  for (int i = 0; i < ConcreteRegisterImpl::number_of_registers ; i++ ) {
      OptoReg::vm2opto[i] = OptoReg::Bad;
  }

  for( OptoReg::Name i=OptoReg::Name(0); i<OptoReg::Name(REG_COUNT); i = OptoReg::add(i,1) ) {
    VMReg r = OptoReg::as_VMReg(i);
    if (r->is_valid()) {
      OptoReg::vm2opto[r->value()] = i;
    }
  }

  // Check that runtime and architecture description agree on callee-saved-floats
  bool callee_saved_floats = false;
  for( OptoReg::Name i=OptoReg::Name(0); i<OptoReg::Name(_last_Mach_Reg); i = OptoReg::add(i,1) ) {
    // Is there a callee-saved float or double?
    if( register_save_policy[i] == 'E' /* callee-saved */ &&
       (register_save_type[i] == Op_RegF || register_save_type[i] == Op_RegD) ) {
      callee_saved_floats = true;
    }
  }

  DEBUG_ONLY( Node::init_NodeProperty(); )

  Compile::pd_compiler2_init();

  CompilerThread* thread = CompilerThread::current();

  HandleMark handle_mark(thread);
  return OptoRuntime::generate(thread->env());
}


void C2Compiler::initialize() {
  // The first compiler thread that gets here will initialize the
  // small amount of global state (and runtime stubs) that C2 needs.

  // There is a race possible once at startup and then we're fine

  // Note that this is being called from a compiler thread not the
  // main startup thread.
  if (should_perform_init()) {
    bool successful = C2Compiler::init_c2_runtime();
    int new_state = (successful) ? initialized : failed;
    set_state(new_state);
  }
}

void C2Compiler::compile_method(ciEnv* env, ciMethod* target, int entry_bci) {
  assert(is_initialized(), "Compiler thread must be initialized");

  bool subsume_loads = SubsumeLoads;
  bool do_escape_analysis = DoEscapeAnalysis && !env->should_retain_local_variables();
  bool eliminate_boxing = EliminateAutoBox;
  while (!env->failing()) {
    // Attempt to compile while subsuming loads into machine instructions.
    Compile C(env, this, target, entry_bci, subsume_loads, do_escape_analysis, eliminate_boxing);


    // Check result and retry if appropriate.
    if (C.failure_reason() != NULL) {
      if (C.failure_reason_is(retry_class_loading_during_parsing())) {
        env->record_failure(C.failure_reason());
        continue;  // retry
      }
      if (C.failure_reason_is(retry_no_subsuming_loads())) {
        assert(subsume_loads, "must make progress");
        subsume_loads = false;
        continue;  // retry
      }
      if (C.failure_reason_is(retry_no_escape_analysis())) {
        assert(do_escape_analysis, "must make progress");
        do_escape_analysis = false;
        continue;  // retry
      }
      if (C.has_boxed_value()) {
        // Recompile without boxing elimination regardless failure reason.
        assert(eliminate_boxing, "must make progress");
        eliminate_boxing = false;
        continue;  // retry
      }
      // Pass any other failure reason up to the ciEnv.
      // Note that serious, irreversible failures are already logged
      // on the ciEnv via env->record_method_not_compilable().
      env->record_failure(C.failure_reason());
    }
    if (StressRecompilation) {
      if (subsume_loads) {
        subsume_loads = false;
        continue;  // retry
      }
      if (do_escape_analysis) {
        do_escape_analysis = false;
        continue;  // retry
      }
    }

    // No retry; just break the loop.
    break;
  }
}


void C2Compiler::print_timers() {
  // do nothing
}

bool C2Compiler::is_intrinsic_available(methodHandle method, methodHandle compilation_context) {
  // Assume a non-virtual dispatch. A virtual dispatch is
  // possible for only a limited set of available intrinsics whereas
  // a non-virtual dispatch is possible for all available intrinsics.
  return is_intrinsic_supported(method, false) &&
         !is_intrinsic_disabled_by_flag(method, compilation_context);
}

bool C2Compiler::is_intrinsic_supported(methodHandle method, bool is_virtual) {
  vmIntrinsics::ID id = method->intrinsic_id();
  assert(id != vmIntrinsics::_none, "must be a VM intrinsic");

  if (id < vmIntrinsics::FIRST_ID || id >= vmIntrinsics::LAST_COMPILER_INLINE) {
    return false;
  }

  // Only Object.hashCode and Object.clone intrinsics implement also a virtual
  // dispatch because calling both methods is expensive but both methods are
  // frequently overridden. All other intrinsics implement only a non-virtual
  // dispatch.
  if (is_virtual) {
    switch (id) {
    case vmIntrinsics::_hashCode:
    case vmIntrinsics::_clone:
      break;
    default:
      return false;
    }
  }

  switch (id) {
  case vmIntrinsics::_compareTo:
    if (!Matcher::match_rule_supported(Op_StrComp)) return false;
    break;
  case vmIntrinsics::_equals:
    if (!Matcher::match_rule_supported(Op_StrEquals)) return false;
    break;
  case vmIntrinsics::_equalsC:
    if (!Matcher::match_rule_supported(Op_AryEq)) return false;
    break;
  case vmIntrinsics::_copyMemory:
    if (StubRoutines::unsafe_arraycopy() == NULL) return false;
    break;
  case vmIntrinsics::_encodeISOArray:
    if (!Matcher::match_rule_supported(Op_EncodeISOArray)) return false;
    break;
  case vmIntrinsics::_bitCount_i:
    if (!Matcher::match_rule_supported(Op_PopCountI)) return false;
    break;
  case vmIntrinsics::_bitCount_l:
    if (!Matcher::match_rule_supported(Op_PopCountL)) return false;
    break;
  case vmIntrinsics::_numberOfLeadingZeros_i:
    if (!Matcher::match_rule_supported(Op_CountLeadingZerosI)) return false;
    break;
  case vmIntrinsics::_numberOfLeadingZeros_l:
    if (!Matcher::match_rule_supported(Op_CountLeadingZerosL)) return false;
    break;
  case vmIntrinsics::_numberOfTrailingZeros_i:
    if (!Matcher::match_rule_supported(Op_CountTrailingZerosI)) return false;
    break;
  case vmIntrinsics::_numberOfTrailingZeros_l:
    if (!Matcher::match_rule_supported(Op_CountTrailingZerosL)) return false;
    break;
  case vmIntrinsics::_reverseBytes_c:
    if (!Matcher::match_rule_supported(Op_ReverseBytesUS)) return false;
    break;
  case vmIntrinsics::_reverseBytes_s:
    if (!Matcher::match_rule_supported(Op_ReverseBytesS)) return false;
    break;
  case vmIntrinsics::_reverseBytes_i:
    if (!Matcher::match_rule_supported(Op_ReverseBytesI)) return false;
    break;
  case vmIntrinsics::_reverseBytes_l:
    if (!Matcher::match_rule_supported(Op_ReverseBytesL)) return false;
    break;
  case vmIntrinsics::_compareAndSwapObject:
#ifdef _LP64
    if (!UseCompressedOops && !Matcher::match_rule_supported(Op_CompareAndSwapP)) return false;
#endif
    break;
  case vmIntrinsics::_compareAndSwapLong:
    if (!Matcher::match_rule_supported(Op_CompareAndSwapL)) return false;
    break;
  case vmIntrinsics::_getAndAddInt:
    if (!Matcher::match_rule_supported(Op_GetAndAddI)) return false;
    break;
  case vmIntrinsics::_getAndAddLong:
    if (!Matcher::match_rule_supported(Op_GetAndAddL)) return false;
    break;
  case vmIntrinsics::_getAndSetInt:
    if (!Matcher::match_rule_supported(Op_GetAndSetI)) return false;
    break;
  case vmIntrinsics::_getAndSetLong:
    if (!Matcher::match_rule_supported(Op_GetAndSetL)) return false;
    break;
  case vmIntrinsics::_getAndSetObject:
#ifdef _LP64
    if (!UseCompressedOops && !Matcher::match_rule_supported(Op_GetAndSetP)) return false;
    if (UseCompressedOops && !Matcher::match_rule_supported(Op_GetAndSetN)) return false;
    break;
#else
    if (!Matcher::match_rule_supported(Op_GetAndSetP)) return false;
    break;
#endif
  case vmIntrinsics::_incrementExactI:
  case vmIntrinsics::_addExactI:
    if (!Matcher::match_rule_supported(Op_OverflowAddI)) return false;
    break;
  case vmIntrinsics::_incrementExactL:
  case vmIntrinsics::_addExactL:
    if (!Matcher::match_rule_supported(Op_OverflowAddL)) return false;
    break;
  case vmIntrinsics::_decrementExactI:
  case vmIntrinsics::_subtractExactI:
    if (!Matcher::match_rule_supported(Op_OverflowSubI)) return false;
    break;
  case vmIntrinsics::_decrementExactL:
  case vmIntrinsics::_subtractExactL:
    if (!Matcher::match_rule_supported(Op_OverflowSubL)) return false;
    break;
  case vmIntrinsics::_negateExactI:
    if (!Matcher::match_rule_supported(Op_OverflowSubI)) return false;
    break;
  case vmIntrinsics::_negateExactL:
    if (!Matcher::match_rule_supported(Op_OverflowSubL)) return false;
    break;
  case vmIntrinsics::_multiplyExactI:
    if (!Matcher::match_rule_supported(Op_OverflowMulI)) return false;
    break;
  case vmIntrinsics::_multiplyExactL:
    if (!Matcher::match_rule_supported(Op_OverflowMulL)) return false;
    break;
  case vmIntrinsics::_getCallerClass:
    if (SystemDictionary::reflect_CallerSensitive_klass() == NULL) return false;
    break;
  case vmIntrinsics::_hashCode:
  case vmIntrinsics::_identityHashCode:
  case vmIntrinsics::_getClass:
  case vmIntrinsics::_dsin:
  case vmIntrinsics::_dcos:
  case vmIntrinsics::_dtan:
  case vmIntrinsics::_dabs:
  case vmIntrinsics::_datan2:
  case vmIntrinsics::_dsqrt:
  case vmIntrinsics::_dexp:
  case vmIntrinsics::_dlog:
  case vmIntrinsics::_dlog10:
  case vmIntrinsics::_dpow:
  case vmIntrinsics::_min:
  case vmIntrinsics::_max:
  case vmIntrinsics::_arraycopy:
  case vmIntrinsics::_indexOf:
  case vmIntrinsics::_getObject:
  case vmIntrinsics::_getBoolean:
  case vmIntrinsics::_getByte:
  case vmIntrinsics::_getShort:
  case vmIntrinsics::_getChar:
  case vmIntrinsics::_getInt:
  case vmIntrinsics::_getLong:
  case vmIntrinsics::_getFloat:
  case vmIntrinsics::_getDouble:
  case vmIntrinsics::_putObject:
  case vmIntrinsics::_putBoolean:
  case vmIntrinsics::_putByte:
  case vmIntrinsics::_putShort:
  case vmIntrinsics::_putChar:
  case vmIntrinsics::_putInt:
  case vmIntrinsics::_putLong:
  case vmIntrinsics::_putFloat:
  case vmIntrinsics::_putDouble:
  case vmIntrinsics::_getByte_raw:
  case vmIntrinsics::_getShort_raw:
  case vmIntrinsics::_getChar_raw:
  case vmIntrinsics::_getInt_raw:
  case vmIntrinsics::_getLong_raw:
  case vmIntrinsics::_getFloat_raw:
  case vmIntrinsics::_getDouble_raw:
  case vmIntrinsics::_getAddress_raw:
  case vmIntrinsics::_putByte_raw:
  case vmIntrinsics::_putShort_raw:
  case vmIntrinsics::_putChar_raw:
  case vmIntrinsics::_putInt_raw:
  case vmIntrinsics::_putLong_raw:
  case vmIntrinsics::_putFloat_raw:
  case vmIntrinsics::_putDouble_raw:
  case vmIntrinsics::_putAddress_raw:
  case vmIntrinsics::_prefetchRead:
  case vmIntrinsics::_prefetchWrite:
  case vmIntrinsics::_prefetchReadStatic:
  case vmIntrinsics::_prefetchWriteStatic:
  case vmIntrinsics::_getObjectVolatile:
  case vmIntrinsics::_getBooleanVolatile:
  case vmIntrinsics::_getByteVolatile:
  case vmIntrinsics::_getShortVolatile:
  case vmIntrinsics::_getCharVolatile:
  case vmIntrinsics::_getIntVolatile:
  case vmIntrinsics::_getLongVolatile:
  case vmIntrinsics::_getFloatVolatile:
  case vmIntrinsics::_getDoubleVolatile:
  case vmIntrinsics::_putObjectVolatile:
  case vmIntrinsics::_putBooleanVolatile:
  case vmIntrinsics::_putByteVolatile:
  case vmIntrinsics::_putShortVolatile:
  case vmIntrinsics::_putCharVolatile:
  case vmIntrinsics::_putIntVolatile:
  case vmIntrinsics::_putLongVolatile:
  case vmIntrinsics::_putFloatVolatile:
  case vmIntrinsics::_putDoubleVolatile:
  case vmIntrinsics::_compareAndSwapInt:
  case vmIntrinsics::_putOrderedObject:
  case vmIntrinsics::_putOrderedInt:
  case vmIntrinsics::_putOrderedLong:
  case vmIntrinsics::_loadFence:
  case vmIntrinsics::_storeFence:
  case vmIntrinsics::_fullFence:
  case vmIntrinsics::_currentThread:
  case vmIntrinsics::_isInterrupted:
#ifdef JFR_HAVE_INTRINSICS
  case vmIntrinsics::_getClassId:
  case vmIntrinsics::_getEventWriter:
  case vmIntrinsics::_counterTime:
#endif
  case vmIntrinsics::_currentTimeMillis:
  case vmIntrinsics::_nanoTime:
  case vmIntrinsics::_allocateInstance:
  case vmIntrinsics::_newArray:
  case vmIntrinsics::_getLength:
  case vmIntrinsics::_copyOf:
  case vmIntrinsics::_copyOfRange:
  case vmIntrinsics::_clone:
  case vmIntrinsics::_isAssignableFrom:
  case vmIntrinsics::_isInstance:
  case vmIntrinsics::_getModifiers:
  case vmIntrinsics::_isInterface:
  case vmIntrinsics::_isArray:
  case vmIntrinsics::_isPrimitive:
  case vmIntrinsics::_getSuperclass:
  case vmIntrinsics::_getClassAccessFlags:
  case vmIntrinsics::_floatToRawIntBits:
  case vmIntrinsics::_floatToIntBits:
  case vmIntrinsics::_intBitsToFloat:
  case vmIntrinsics::_doubleToRawLongBits:
  case vmIntrinsics::_doubleToLongBits:
  case vmIntrinsics::_longBitsToDouble:
  case vmIntrinsics::_Reference_get:
  case vmIntrinsics::_aescrypt_encryptBlock:
  case vmIntrinsics::_aescrypt_decryptBlock:
  case vmIntrinsics::_cipherBlockChaining_encryptAESCrypt:
  case vmIntrinsics::_cipherBlockChaining_decryptAESCrypt:
  case vmIntrinsics::_counterMode_AESCrypt:
  case vmIntrinsics::_md5_implCompress:
  case vmIntrinsics::_sha_implCompress:
  case vmIntrinsics::_sha2_implCompress:
  case vmIntrinsics::_sha5_implCompress:
  case vmIntrinsics::_digestBase_implCompressMB:
  case vmIntrinsics::_multiplyToLen:
  case vmIntrinsics::_squareToLen:
  case vmIntrinsics::_mulAdd:
  case vmIntrinsics::_montgomeryMultiply:
  case vmIntrinsics::_montgomerySquare:
  case vmIntrinsics::_ghash_processBlocks:
  case vmIntrinsics::_updateCRC32:
  case vmIntrinsics::_updateBytesCRC32:
  case vmIntrinsics::_updateByteBufferCRC32:
  case vmIntrinsics::_profileBoolean:
  case vmIntrinsics::_dgemm_dgemm:
  case vmIntrinsics::_dgemv_dgemv:
  case vmIntrinsics::_f2jblas_ddot:
  case vmIntrinsics::_getComponentType:
    break;
  default:
    return false;
  }
  return true;
}

bool C2Compiler::is_intrinsic_disabled_by_flag(methodHandle method, methodHandle compilation_context) {
  vmIntrinsics::ID id = method->intrinsic_id();
  assert(id != vmIntrinsics::_none, "must be a VM intrinsic");

  if (vmIntrinsics::is_disabled_by_flags(method->intrinsic_id())) {
    return true;
  }

  // Check if the intrinsic corresponding to 'method' has been disabled on
  // the command line by using the DisableIntrinsic flag (either globally
  // or on a per-method level, see src/share/vm/compiler/abstractCompiler.hpp
  // for details).
  // Usually, the compilation context is the caller of the method 'method'.
  // The only case when for a non-recursive method 'method' the compilation context
  // is not the caller of the 'method' (but it is the method itself) is
  // java.lang.ref.Referene::get.
  // For java.lang.ref.Reference::get, the intrinsic version is used
  // instead of the C2-compiled version so that the value in the referent
  // field can be registered by the G1 pre-barrier code. The intrinsified
  // version of Reference::get also adds a memory barrier to prevent
  // commoning reads from the referent field across safepoint since GC
  // can change the referent field's value. See Compile::Compile()
  // in src/share/vm/opto/compile.cpp for more details.
  ccstr disable_intr = NULL;
  if ((DisableIntrinsic[0] != '\0' && strstr(DisableIntrinsic, vmIntrinsics::name_at(id)) != NULL) ||
      (!compilation_context.is_null() &&
       CompilerOracle::has_option_value(compilation_context, "DisableIntrinsic", disable_intr) &&
       strstr(disable_intr, vmIntrinsics::name_at(id)) != NULL)
  ) {
    return true;
  }

  // -XX:-InlineNatives disables nearly all intrinsics except the ones listed in
  // the following switch statement.
  if (!InlineNatives) {
    switch (id) {
    case vmIntrinsics::_indexOf:
    case vmIntrinsics::_compareTo:
    case vmIntrinsics::_equals:
    case vmIntrinsics::_equalsC:
    case vmIntrinsics::_getAndAddInt:
    case vmIntrinsics::_getAndAddLong:
    case vmIntrinsics::_getAndSetInt:
    case vmIntrinsics::_getAndSetLong:
    case vmIntrinsics::_getAndSetObject:
    case vmIntrinsics::_loadFence:
    case vmIntrinsics::_storeFence:
    case vmIntrinsics::_fullFence:
    case vmIntrinsics::_Reference_get:
      break;
    default:
      return true;
    }
  }

  if (!InlineUnsafeOps) {
    switch (id) {
    case vmIntrinsics::_loadFence:
    case vmIntrinsics::_storeFence:
    case vmIntrinsics::_fullFence:
    case vmIntrinsics::_compareAndSwapObject:
    case vmIntrinsics::_compareAndSwapLong:
    case vmIntrinsics::_compareAndSwapInt:
      return true;
    default:
      return false;
    }
  }

  return false;
}
