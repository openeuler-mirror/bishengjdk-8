/*
 * Copyright (c) 1999, 2016, Oracle and/or its affiliates. All rights reserved.
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
#include "ci/ciCallProfile.hpp"
#include "ci/ciExceptionHandler.hpp"
#include "ci/ciInstanceKlass.hpp"
#include "ci/ciMethod.hpp"
#include "ci/ciMethodBlocks.hpp"
#include "ci/ciMethodData.hpp"
#include "ci/ciStreams.hpp"
#include "ci/ciSymbol.hpp"
#include "ci/ciReplay.hpp"
#include "ci/ciUtilities.hpp"
#include "classfile/systemDictionary.hpp"
#include "compiler/abstractCompiler.hpp"
#include "compiler/compilerOracle.hpp"
#include "compiler/methodLiveness.hpp"
#include "compiler/compileBroker.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/linkResolver.hpp"
#include "interpreter/oopMapCache.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/resourceArea.hpp"
#include "oops/generateOopMap.hpp"
#include "oops/oop.inline.hpp"
#include "prims/nativeLookup.hpp"
#include "runtime/deoptimization.hpp"
#include "utilities/bitMap.inline.hpp"
#include "utilities/xmlstream.hpp"
#ifdef COMPILER2
#include "ci/bcEscapeAnalyzer.hpp"
#include "ci/ciTypeFlow.hpp"
#include "oops/method.hpp"
#endif
#ifdef SHARK
#include "ci/ciTypeFlow.hpp"
#include "oops/method.hpp"
#endif

// ciMethod
//
// This class represents a Method* in the HotSpot virtual
// machine.


// ------------------------------------------------------------------
// ciMethod::ciMethod
//
// Loaded method.
ciMethod::ciMethod(methodHandle h_m, ciInstanceKlass* holder) :
  ciMetadata(h_m()),
  _holder(holder)
{
  assert(h_m() != NULL, "no null method");

  if (LogTouchedMethods) {
    h_m()->log_touched(Thread::current());
  }
  // These fields are always filled in in loaded methods.
  _flags = ciFlags(h_m()->access_flags());

  // Easy to compute, so fill them in now.
  _max_stack          = h_m()->max_stack();
  _max_locals         = h_m()->max_locals();
  _code_size          = h_m()->code_size();
  _intrinsic_id       = h_m()->intrinsic_id();
  _handler_count      = h_m()->exception_table_length();
  _size_of_parameters = h_m()->size_of_parameters();
  _uses_monitors      = h_m()->access_flags().has_monitor_bytecodes();
  _balanced_monitors  = !_uses_monitors || h_m()->access_flags().is_monitor_matching();
  _is_c1_compilable   = !h_m()->is_not_c1_compilable();
  _is_c2_compilable   = !h_m()->is_not_c2_compilable();
  // Lazy fields, filled in on demand.  Require allocation.
  _code               = NULL;
  _exception_handlers = NULL;
  _liveness           = NULL;
  _method_blocks = NULL;
#if defined(COMPILER2) || defined(SHARK)
  _flow               = NULL;
  _bcea               = NULL;
#endif // COMPILER2 || SHARK

  ciEnv *env = CURRENT_ENV;
  if (env->jvmti_can_hotswap_or_post_breakpoint() && can_be_compiled()) {
    // 6328518 check hotswap conditions under the right lock.
    MutexLocker locker(Compile_lock);
    if (Dependencies::check_evol_method(h_m()) != NULL) {
      _is_c1_compilable = false;
      _is_c2_compilable = false;
    }
  } else {
    CHECK_UNHANDLED_OOPS_ONLY(Thread::current()->clear_unhandled_oops());
  }

  if (h_m()->method_holder()->is_linked()) {
    _can_be_statically_bound = h_m()->can_be_statically_bound();
  } else {
    // Have to use a conservative value in this case.
    _can_be_statically_bound = false;
  }

  // Adjust the definition of this condition to be more useful:
  // %%% take these conditions into account in vtable generation
  if (!_can_be_statically_bound && h_m()->is_private())
    _can_be_statically_bound = true;
  if (_can_be_statically_bound && h_m()->is_abstract())
    _can_be_statically_bound = false;

  // generating _signature may allow GC and therefore move m.
  // These fields are always filled in.
  _name = env->get_symbol(h_m()->name());
  ciSymbol* sig_symbol = env->get_symbol(h_m()->signature());
  constantPoolHandle cpool = h_m()->constants();
  _signature = new (env->arena()) ciSignature(_holder, cpool, sig_symbol);
  _method_data = NULL;
  // Take a snapshot of these values, so they will be commensurate with the MDO.
  if (ProfileInterpreter || TieredCompilation) {
    int invcnt = h_m()->interpreter_invocation_count();
    // if the value overflowed report it as max int
    _interpreter_invocation_count = invcnt < 0 ? max_jint : invcnt ;
    _interpreter_throwout_count   = h_m()->interpreter_throwout_count();
  } else {
    _interpreter_invocation_count = 0;
    _interpreter_throwout_count = 0;
  }
  if (_interpreter_invocation_count == 0)
    _interpreter_invocation_count = 1;
  _instructions_size = -1;
#ifdef ASSERT
  if (ReplayCompiles) {
    ciReplay::initialize(this);
  }
#endif
}


// ------------------------------------------------------------------
// ciMethod::ciMethod
//
// Unloaded method.
ciMethod::ciMethod(ciInstanceKlass* holder,
                   ciSymbol*        name,
                   ciSymbol*        signature,
                   ciInstanceKlass* accessor) :
  ciMetadata((Metadata*)NULL),
  _name(                   name),
  _holder(                 holder),
  _intrinsic_id(           vmIntrinsics::_none),
  _liveness(               NULL),
  _can_be_statically_bound(false),
  _method_blocks(          NULL),
  _method_data(            NULL)
#if defined(COMPILER2) || defined(SHARK)
  ,
  _flow(                   NULL),
  _bcea(                   NULL),
  _instructions_size(-1)
#endif // COMPILER2 || SHARK
{
  // Usually holder and accessor are the same type but in some cases
  // the holder has the wrong class loader (e.g. invokedynamic call
  // sites) so we pass the accessor.
  _signature = new (CURRENT_ENV->arena()) ciSignature(accessor, constantPoolHandle(), signature);
}


// ------------------------------------------------------------------
// ciMethod::load_code
//
// Load the bytecodes and exception handler table for this method.
void ciMethod::load_code() {
  VM_ENTRY_MARK;
  assert(is_loaded(), "only loaded methods have code");

  Method* me = get_Method();
  Arena* arena = CURRENT_THREAD_ENV->arena();

  // Load the bytecodes.
  _code = (address)arena->Amalloc(code_size());
  memcpy(_code, me->code_base(), code_size());

  // Revert any breakpoint bytecodes in ci's copy
  if (me->number_of_breakpoints() > 0) {
    BreakpointInfo* bp = me->method_holder()->breakpoints();
    for (; bp != NULL; bp = bp->next()) {
      if (bp->match(me)) {
        code_at_put(bp->bci(), bp->orig_bytecode());
      }
    }
  }

  // And load the exception table.
  ExceptionTable exc_table(me);

  // Allocate one extra spot in our list of exceptions.  This
  // last entry will be used to represent the possibility that
  // an exception escapes the method.  See ciExceptionHandlerStream
  // for details.
  _exception_handlers =
    (ciExceptionHandler**)arena->Amalloc(sizeof(ciExceptionHandler*)
                                         * (_handler_count + 1));
  if (_handler_count > 0) {
    for (int i=0; i<_handler_count; i++) {
      _exception_handlers[i] = new (arena) ciExceptionHandler(
                                holder(),
            /* start    */      exc_table.start_pc(i),
            /* limit    */      exc_table.end_pc(i),
            /* goto pc  */      exc_table.handler_pc(i),
            /* cp index */      exc_table.catch_type_index(i));
    }
  }

  // Put an entry at the end of our list to represent the possibility
  // of exceptional exit.
  _exception_handlers[_handler_count] =
    new (arena) ciExceptionHandler(holder(), 0, code_size(), -1, 0);

  if (CIPrintMethodCodes) {
    print_codes();
  }
}


// ------------------------------------------------------------------
// ciMethod::has_linenumber_table
//
// length unknown until decompression
bool    ciMethod::has_linenumber_table() const {
  check_is_loaded();
  VM_ENTRY_MARK;
  return get_Method()->has_linenumber_table();
}


// ------------------------------------------------------------------
// ciMethod::compressed_linenumber_table
u_char* ciMethod::compressed_linenumber_table() const {
  check_is_loaded();
  VM_ENTRY_MARK;
  return get_Method()->compressed_linenumber_table();
}


// ------------------------------------------------------------------
// ciMethod::line_number_from_bci
int ciMethod::line_number_from_bci(int bci) const {
  check_is_loaded();
  VM_ENTRY_MARK;
  return get_Method()->line_number_from_bci(bci);
}


// ------------------------------------------------------------------
// ciMethod::vtable_index
//
// Get the position of this method's entry in the vtable, if any.
int ciMethod::vtable_index() {
  check_is_loaded();
  assert(holder()->is_linked(), "must be linked");
  VM_ENTRY_MARK;
  return get_Method()->vtable_index();
}


#ifdef SHARK
// ------------------------------------------------------------------
// ciMethod::itable_index
//
// Get the position of this method's entry in the itable, if any.
int ciMethod::itable_index() {
  check_is_loaded();
  assert(holder()->is_linked(), "must be linked");
  VM_ENTRY_MARK;
  Method* m = get_Method();
  if (!m->has_itable_index())
    return Method::nonvirtual_vtable_index;
  return m->itable_index();
}
#endif // SHARK


// ------------------------------------------------------------------
// ciMethod::native_entry
//
// Get the address of this method's native code, if any.
address ciMethod::native_entry() {
  check_is_loaded();
  assert(flags().is_native(), "must be native method");
  VM_ENTRY_MARK;
  Method* method = get_Method();
  address entry = method->native_function();
  assert(entry != NULL, "must be valid entry point");
  return entry;
}


// ------------------------------------------------------------------
// ciMethod::interpreter_entry
//
// Get the entry point for running this method in the interpreter.
address ciMethod::interpreter_entry() {
  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_Method());
  return Interpreter::entry_for_method(mh);
}


// ------------------------------------------------------------------
// ciMethod::uses_balanced_monitors
//
// Does this method use monitors in a strict stack-disciplined manner?
bool ciMethod::has_balanced_monitors() {
  check_is_loaded();
  if (_balanced_monitors) return true;

  // Analyze the method to see if monitors are used properly.
  VM_ENTRY_MARK;
  methodHandle method(THREAD, get_Method());
  assert(method->has_monitor_bytecodes(), "should have checked this");

  // Check to see if a previous compilation computed the
  // monitor-matching analysis.
  if (method->guaranteed_monitor_matching()) {
    _balanced_monitors = true;
    return true;
  }

  {
    EXCEPTION_MARK;
    ResourceMark rm(THREAD);
    GeneratePairingInfo gpi(method);
    gpi.compute_map(CATCH);
    if (!gpi.monitor_safe()) {
      return false;
    }
    method->set_guaranteed_monitor_matching();
    _balanced_monitors = true;
  }
  return true;
}


// ------------------------------------------------------------------
// ciMethod::get_flow_analysis
ciTypeFlow* ciMethod::get_flow_analysis() {
#if defined(COMPILER2) || defined(SHARK)
  if (_flow == NULL) {
    ciEnv* env = CURRENT_ENV;
    _flow = new (env->arena()) ciTypeFlow(env, this);
    _flow->do_flow();
  }
  return _flow;
#else // COMPILER2 || SHARK
  ShouldNotReachHere();
  return NULL;
#endif // COMPILER2 || SHARK
}


// ------------------------------------------------------------------
// ciMethod::get_osr_flow_analysis
ciTypeFlow* ciMethod::get_osr_flow_analysis(int osr_bci) {
#if defined(COMPILER2) || defined(SHARK)
  // OSR entry points are always place after a call bytecode of some sort
  assert(osr_bci >= 0, "must supply valid OSR entry point");
  ciEnv* env = CURRENT_ENV;
  ciTypeFlow* flow = new (env->arena()) ciTypeFlow(env, this, osr_bci);
  flow->do_flow();
  return flow;
#else // COMPILER2 || SHARK
  ShouldNotReachHere();
  return NULL;
#endif // COMPILER2 || SHARK
}

// ------------------------------------------------------------------
// ciMethod::raw_liveness_at_bci
//
// Which local variables are live at a specific bci?
MethodLivenessResult ciMethod::raw_liveness_at_bci(int bci) {
  check_is_loaded();
  if (_liveness == NULL) {
    // Create the liveness analyzer.
    Arena* arena = CURRENT_ENV->arena();
    _liveness = new (arena) MethodLiveness(arena, this);
    _liveness->compute_liveness();
  }
  return _liveness->get_liveness_at(bci);
}

// ------------------------------------------------------------------
// ciMethod::liveness_at_bci
//
// Which local variables are live at a specific bci?  When debugging
// will return true for all locals in some cases to improve debug
// information.
MethodLivenessResult ciMethod::liveness_at_bci(int bci) {
  MethodLivenessResult result = raw_liveness_at_bci(bci);
  if (CURRENT_ENV->should_retain_local_variables() || DeoptimizeALot || CompileTheWorld) {
    // Keep all locals live for the user's edification and amusement.
    result.at_put_range(0, result.size(), true);
  }
  return result;
}

// ciMethod::live_local_oops_at_bci
//
// find all the live oops in the locals array for a particular bci
// Compute what the interpreter believes by using the interpreter
// oopmap generator. This is used as a double check during osr to
// guard against conservative result from MethodLiveness making us
// think a dead oop is live.  MethodLiveness is conservative in the
// sense that it may consider locals to be live which cannot be live,
// like in the case where a local could contain an oop or  a primitive
// along different paths.  In that case the local must be dead when
// those paths merge. Since the interpreter's viewpoint is used when
// gc'ing an interpreter frame we need to use its viewpoint  during
// OSR when loading the locals.

BitMap ciMethod::live_local_oops_at_bci(int bci) {
  VM_ENTRY_MARK;
  InterpreterOopMap mask;
  OopMapCache::compute_one_oop_map(get_Method(), bci, &mask);
  int mask_size = max_locals();
  BitMap result(mask_size);
  result.clear();
  int i;
  for (i = 0; i < mask_size ; i++ ) {
    if (mask.is_oop(i)) result.set_bit(i);
  }
  return result;
}


#ifdef COMPILER1
// ------------------------------------------------------------------
// ciMethod::bci_block_start
//
// Marks all bcis where a new basic block starts
const BitMap ciMethod::bci_block_start() {
  check_is_loaded();
  if (_liveness == NULL) {
    // Create the liveness analyzer.
    Arena* arena = CURRENT_ENV->arena();
    _liveness = new (arena) MethodLiveness(arena, this);
    _liveness->compute_liveness();
  }

  return _liveness->get_bci_block_start();
}
#endif // COMPILER1


// ------------------------------------------------------------------
// ciMethod::call_profile_at_bci
//
// Get the ciCallProfile for the invocation of this method.
// Also reports receiver types for non-call type checks (if TypeProfileCasts).
ciCallProfile ciMethod::call_profile_at_bci(int bci) {
  ResourceMark rm;
  ciCallProfile result;
  if (method_data() != NULL && method_data()->is_mature()) {
    ciProfileData* data = method_data()->bci_to_data(bci);
    if (data != NULL && data->is_CounterData()) {
      // Every profiled call site has a counter.
      int count = data->as_CounterData()->count();

      if (!data->is_ReceiverTypeData()) {
        result._receiver_count[0] = 0;  // that's a definite zero
      } else { // ReceiverTypeData is a subclass of CounterData
        ciReceiverTypeData* call = (ciReceiverTypeData*)data->as_ReceiverTypeData();
        // In addition, virtual call sites have receiver type information
        int receivers_count_total = 0;
        int morphism = 0;
        // Precompute morphism for the possible fixup
        for (uint i = 0; i < call->row_limit(); i++) {
          ciKlass* receiver = call->receiver(i);
          if (receiver == NULL)  continue;
          morphism++;
        }
        int epsilon = 0;
        if (TieredCompilation && ProfileInterpreter) {
          // Interpreter and C1 treat final and special invokes differently.
          // C1 will record a type, whereas the interpreter will just
          // increment the count. Detect this case.
          if (morphism == 1 && count > 0) {
            epsilon = count;
            count = 0;
          }
        }
        for (uint i = 0; i < call->row_limit(); i++) {
          ciKlass* receiver = call->receiver(i);
          if (receiver == NULL)  continue;
          int rcount = call->receiver_count(i) + epsilon;
          if (rcount == 0) rcount = 1; // Should be valid value
          receivers_count_total += rcount;
          // Add the receiver to result data.
          result.add_receiver(receiver, rcount);
          // If we extend profiling to record methods,
          // we will set result._method also.
        }
        // Determine call site's morphism.
        // The call site count is 0 with known morphism (onlt 1 or 2 receivers)
        // or < 0 in the case of a type check failured for checkcast, aastore, instanceof.
        // The call site count is > 0 in the case of a polymorphic virtual call.
        if (morphism > 0 && morphism == result._limit) {
           // The morphism <= MorphismLimit.
           if ((morphism <  ciCallProfile::MorphismLimit) ||
               (morphism == ciCallProfile::MorphismLimit && count == 0)) {
#ifdef ASSERT
             if (count > 0) {
               this->print_short_name(tty);
               tty->print_cr(" @ bci:%d", bci);
               this->print_codes();
               assert(false, "this call site should not be polymorphic");
             }
#endif
             result._morphism = morphism;
           }
        }
        // Make the count consistent if this is a call profile. If count is
        // zero or less, presume that this is a typecheck profile and
        // do nothing.  Otherwise, increase count to be the sum of all
        // receiver's counts.
        if (count >= 0) {
          count += receivers_count_total;
        }
      }
      result._count = count;
    }
  }
  return result;
}

// ------------------------------------------------------------------
// Add new receiver and sort data by receiver's profile count.
void ciCallProfile::add_receiver(ciKlass* receiver, int receiver_count) {
  // Add new receiver and sort data by receiver's counts when we have space
  // for it otherwise replace the less called receiver (less called receiver
  // is placed to the last array element which is not used).
  // First array's element contains most called receiver.
  int i = _limit;
  for (; i > 0 && receiver_count > _receiver_count[i-1]; i--) {
    _receiver[i] = _receiver[i-1];
    _receiver_count[i] = _receiver_count[i-1];
  }
  _receiver[i] = receiver;
  _receiver_count[i] = receiver_count;
  if (_limit < MorphismLimit) _limit++;
}


void ciMethod::assert_virtual_call_type_ok(int bci) {
  assert(java_code_at_bci(bci) == Bytecodes::_invokevirtual ||
         java_code_at_bci(bci) == Bytecodes::_invokeinterface, err_msg("unexpected bytecode %s", Bytecodes::name(java_code_at_bci(bci))));
}

void ciMethod::assert_call_type_ok(int bci) {
  assert(java_code_at_bci(bci) == Bytecodes::_invokestatic ||
         java_code_at_bci(bci) == Bytecodes::_invokespecial ||
         java_code_at_bci(bci) == Bytecodes::_invokedynamic, err_msg("unexpected bytecode %s", Bytecodes::name(java_code_at_bci(bci))));
}

/**
 * Check whether profiling provides a type for the argument i to the
 * call at bci bci
 *
 * @param bci  bci of the call
 * @param i    argument number
 * @return     profiled type
 *
 * If the profile reports that the argument may be null, return false
 * at least for now.
 */
ciKlass* ciMethod::argument_profiled_type(int bci, int i) {
  if (MethodData::profile_parameters() && method_data() != NULL && method_data()->is_mature()) {
    ciProfileData* data = method_data()->bci_to_data(bci);
    if (data != NULL) {
      if (data->is_VirtualCallTypeData()) {
        assert_virtual_call_type_ok(bci);
        ciVirtualCallTypeData* call = (ciVirtualCallTypeData*)data->as_VirtualCallTypeData();
        if (i >= call->number_of_arguments()) {
          return NULL;
        }
        ciKlass* type = call->valid_argument_type(i);
        if (type != NULL && !call->argument_maybe_null(i)) {
          return type;
        }
      } else if (data->is_CallTypeData()) {
        assert_call_type_ok(bci);
        ciCallTypeData* call = (ciCallTypeData*)data->as_CallTypeData();
        if (i >= call->number_of_arguments()) {
          return NULL;
        }
        ciKlass* type = call->valid_argument_type(i);
        if (type != NULL && !call->argument_maybe_null(i)) {
          return type;
        }
      }
    }
  }
  return NULL;
}

/**
 * Check whether profiling provides a type for the return value from
 * the call at bci bci
 *
 * @param bci  bci of the call
 * @return     profiled type
 *
 * If the profile reports that the argument may be null, return false
 * at least for now.
 */
ciKlass* ciMethod::return_profiled_type(int bci) {
  if (MethodData::profile_return() && method_data() != NULL && method_data()->is_mature()) {
    ciProfileData* data = method_data()->bci_to_data(bci);
    if (data != NULL) {
      if (data->is_VirtualCallTypeData()) {
        assert_virtual_call_type_ok(bci);
        ciVirtualCallTypeData* call = (ciVirtualCallTypeData*)data->as_VirtualCallTypeData();
        ciKlass* type = call->valid_return_type();
        if (type != NULL && !call->return_maybe_null()) {
          return type;
        }
      } else if (data->is_CallTypeData()) {
        assert_call_type_ok(bci);
        ciCallTypeData* call = (ciCallTypeData*)data->as_CallTypeData();
        ciKlass* type = call->valid_return_type();
        if (type != NULL && !call->return_maybe_null()) {
          return type;
        }
      }
    }
  }
  return NULL;
}

/**
 * Check whether profiling provides a type for the parameter i
 *
 * @param i    parameter number
 * @return     profiled type
 *
 * If the profile reports that the argument may be null, return false
 * at least for now.
 */
ciKlass* ciMethod::parameter_profiled_type(int i) {
  if (MethodData::profile_parameters() && method_data() != NULL && method_data()->is_mature()) {
    ciParametersTypeData* parameters = method_data()->parameters_type_data();
    if (parameters != NULL && i < parameters->number_of_parameters()) {
      ciKlass* type = parameters->valid_parameter_type(i);
      if (type != NULL && !parameters->parameter_maybe_null(i)) {
        return type;
      }
    }
  }
  return NULL;
}


// ------------------------------------------------------------------
// ciMethod::find_monomorphic_target
//
// Given a certain calling environment, find the monomorphic target
// for the call.  Return NULL if the call is not monomorphic in
// its calling environment, or if there are only abstract methods.
// The returned method is never abstract.
// Note: If caller uses a non-null result, it must inform dependencies
// via assert_unique_concrete_method or assert_leaf_type.
ciMethod* ciMethod::find_monomorphic_target(ciInstanceKlass* caller,
                                            ciInstanceKlass* callee_holder,
                                            ciInstanceKlass* actual_recv,
                                            bool check_access) {
  check_is_loaded();

  if (actual_recv->is_interface()) {
    // %%% We cannot trust interface types, yet.  See bug 6312651.
    return NULL;
  }

  ciMethod* root_m = resolve_invoke(caller, actual_recv, check_access);
  if (root_m == NULL) {
    // Something went wrong looking up the actual receiver method.
    return NULL;
  }
  assert(!root_m->is_abstract(), "resolve_invoke promise");

  // Make certain quick checks even if UseCHA is false.

  // Is it private or final?
  if (root_m->can_be_statically_bound()) {
    return root_m;
  }

  if (actual_recv->is_leaf_type() && actual_recv == root_m->holder()) {
    // Easy case.  There is no other place to put a method, so don't bother
    // to go through the VM_ENTRY_MARK and all the rest.
    return root_m;
  }

  // Array methods (clone, hashCode, etc.) are always statically bound.
  // If we were to see an array type here, we'd return root_m.
  // However, this method processes only ciInstanceKlasses.  (See 4962591.)
  // The inline_native_clone intrinsic narrows Object to T[] properly,
  // so there is no need to do the same job here.

  if (!UseCHA)  return NULL;

  VM_ENTRY_MARK;

  // Disable CHA for default methods for now
  if (root_m->get_Method()->is_default_method()) {
    return NULL;
  }

  methodHandle target;
  {
    MutexLocker locker(Compile_lock);
    Klass* context = actual_recv->get_Klass();
    target = Dependencies::find_unique_concrete_method(context,
                                                       root_m->get_Method());
    // %%% Should upgrade this ciMethod API to look for 1 or 2 concrete methods.
  }

#ifndef PRODUCT
  if (TraceDependencies && target() != NULL && target() != root_m->get_Method()) {
    tty->print("found a non-root unique target method");
    tty->print_cr("  context = %s", InstanceKlass::cast(actual_recv->get_Klass())->external_name());
    tty->print("  method  = ");
    target->print_short_name(tty);
    tty->cr();
  }
#endif //PRODUCT

  if (target() == NULL) {
    return NULL;
  }
  if (target() == root_m->get_Method()) {
    return root_m;
  }
  if (!root_m->is_public() &&
      !root_m->is_protected()) {
    // If we are going to reason about inheritance, it's easiest
    // if the method in question is public, protected, or private.
    // If the answer is not root_m, it is conservatively correct
    // to return NULL, even if the CHA encountered irrelevant
    // methods in other packages.
    // %%% TO DO: Work out logic for package-private methods
    // with the same name but different vtable indexes.
    return NULL;
  }
  return CURRENT_THREAD_ENV->get_method(target());
}

// ------------------------------------------------------------------
// ciMethod::resolve_invoke
//
// Given a known receiver klass, find the target for the call.
// Return NULL if the call has no target or the target is abstract.
ciMethod* ciMethod::resolve_invoke(ciKlass* caller, ciKlass* exact_receiver, bool check_access) {
   check_is_loaded();
   VM_ENTRY_MARK;

   KlassHandle caller_klass (THREAD, caller->get_Klass());
   KlassHandle h_recv       (THREAD, exact_receiver->get_Klass());
   KlassHandle h_resolved   (THREAD, holder()->get_Klass());
   Symbol* h_name      = name()->get_symbol();
   Symbol* h_signature = signature()->get_symbol();

   methodHandle m;
   // Only do exact lookup if receiver klass has been linked.  Otherwise,
   // the vtable has not been setup, and the LinkResolver will fail.
   if (h_recv->oop_is_array()
        ||
       InstanceKlass::cast(h_recv())->is_linked() && !exact_receiver->is_interface()) {
     if (holder()->is_interface()) {
       m = LinkResolver::resolve_interface_call_or_null(h_recv, h_resolved, h_name, h_signature, caller_klass, check_access);
     } else {
       m = LinkResolver::resolve_virtual_call_or_null(h_recv, h_resolved, h_name, h_signature, caller_klass, check_access);
     }
   }

   if (m.is_null()) {
     // Return NULL only if there was a problem with lookup (uninitialized class, etc.)
     return NULL;
   }

   ciMethod* result = this;
   if (m() != get_Method()) {
     result = CURRENT_THREAD_ENV->get_method(m());
   }

   // Don't return abstract methods because they aren't
   // optimizable or interesting.
   if (result->is_abstract()) {
     return NULL;
   } else {
     return result;
   }
}

// ------------------------------------------------------------------
// ciMethod::resolve_vtable_index
//
// Given a known receiver klass, find the vtable index for the call.
// Return Method::invalid_vtable_index if the vtable_index is unknown.
int ciMethod::resolve_vtable_index(ciKlass* caller, ciKlass* receiver) {
   check_is_loaded();

   int vtable_index = Method::invalid_vtable_index;
   // Only do lookup if receiver klass has been linked.  Otherwise,
   // the vtable has not been setup, and the LinkResolver will fail.
   if (!receiver->is_interface()
       && (!receiver->is_instance_klass() ||
           receiver->as_instance_klass()->is_linked())) {
     VM_ENTRY_MARK;

     KlassHandle caller_klass (THREAD, caller->get_Klass());
     KlassHandle h_recv       (THREAD, receiver->get_Klass());
     Symbol* h_name = name()->get_symbol();
     Symbol* h_signature = signature()->get_symbol();

     vtable_index = LinkResolver::resolve_virtual_vtable_index(h_recv, h_recv, h_name, h_signature, caller_klass);
     if (vtable_index == Method::nonvirtual_vtable_index) {
       // A statically bound method.  Return "no such index".
       vtable_index = Method::invalid_vtable_index;
     }
   }

   return vtable_index;
}

// ------------------------------------------------------------------
// ciMethod::interpreter_call_site_count
int ciMethod::interpreter_call_site_count(int bci) {
  if (method_data() != NULL) {
    ResourceMark rm;
    ciProfileData* data = method_data()->bci_to_data(bci);
    if (data != NULL && data->is_CounterData()) {
      return scale_count(data->as_CounterData()->count());
    }
  }
  return -1;  // unknown
}

// ------------------------------------------------------------------
// ciMethod::get_field_at_bci
ciField* ciMethod::get_field_at_bci(int bci, bool &will_link) {
  ciBytecodeStream iter(this);
  iter.reset_to_bci(bci);
  iter.next();
  return iter.get_field(will_link);
}

// ------------------------------------------------------------------
// ciMethod::get_method_at_bci
ciMethod* ciMethod::get_method_at_bci(int bci, bool &will_link, ciSignature* *declared_signature) {
  ciBytecodeStream iter(this);
  iter.reset_to_bci(bci);
  iter.next();
  return iter.get_method(will_link, declared_signature);
}

// ------------------------------------------------------------------
// Adjust a CounterData count to be commensurate with
// interpreter_invocation_count.  If the MDO exists for
// only 25% of the time the method exists, then the
// counts in the MDO should be scaled by 4X, so that
// they can be usefully and stably compared against the
// invocation counts in methods.
int ciMethod::scale_count(int count, float prof_factor) {
  if (count > 0 && method_data() != NULL) {
    int counter_life;
    int method_life = interpreter_invocation_count();
    if (TieredCompilation) {
      // In tiered the MDO's life is measured directly, so just use the snapshotted counters
      counter_life = MAX2(method_data()->invocation_count(), method_data()->backedge_count());
    } else {
      int current_mileage = method_data()->current_mileage();
      int creation_mileage = method_data()->creation_mileage();
      counter_life = current_mileage - creation_mileage;
    }

    // counter_life due to backedge_counter could be > method_life
    if (counter_life > method_life)
      counter_life = method_life;
    if (0 < counter_life && counter_life <= method_life) {
      count = (int)((double)count * prof_factor * method_life / counter_life + 0.5);
      count = (count > 0) ? count : 1;
    }
  }
  return count;
}


// ------------------------------------------------------------------
// ciMethod::is_special_get_caller_class_method
//
bool ciMethod::is_ignored_by_security_stack_walk() const {
  check_is_loaded();
  VM_ENTRY_MARK;
  return get_Method()->is_ignored_by_security_stack_walk();
}


// ------------------------------------------------------------------
// invokedynamic support

// ------------------------------------------------------------------
// ciMethod::is_method_handle_intrinsic
//
// Return true if the method is an instance of the JVM-generated
// signature-polymorphic MethodHandle methods, _invokeBasic, _linkToVirtual, etc.
bool ciMethod::is_method_handle_intrinsic() const {
  vmIntrinsics::ID iid = _intrinsic_id;  // do not check if loaded
  return (MethodHandles::is_signature_polymorphic(iid) &&
          MethodHandles::is_signature_polymorphic_intrinsic(iid));
}

// ------------------------------------------------------------------
// ciMethod::is_compiled_lambda_form
//
// Return true if the method is a generated MethodHandle adapter.
// These are built by Java code.
bool ciMethod::is_compiled_lambda_form() const {
  vmIntrinsics::ID iid = _intrinsic_id;  // do not check if loaded
  return iid == vmIntrinsics::_compiledLambdaForm;
}

// ------------------------------------------------------------------
// ciMethod::is_object_initializer
//
bool ciMethod::is_object_initializer() const {
   return name() == ciSymbol::object_initializer_name();
}

// ------------------------------------------------------------------
// ciMethod::has_member_arg
//
// Return true if the method is a linker intrinsic like _linkToVirtual.
// These are built by the JVM.
bool ciMethod::has_member_arg() const {
  vmIntrinsics::ID iid = _intrinsic_id;  // do not check if loaded
  return (MethodHandles::is_signature_polymorphic(iid) &&
          MethodHandles::has_member_arg(iid));
}

// ------------------------------------------------------------------
// ciMethod::ensure_method_data
//
// Generate new MethodData* objects at compile time.
// Return true if allocation was successful or no MDO is required.
bool ciMethod::ensure_method_data(methodHandle h_m) {
  EXCEPTION_CONTEXT;
  if (is_native() || is_abstract() || h_m()->is_accessor()) {
    return true;
  }
  if (JProfilingCacheCompileAdvance && CURRENT_ENV->task()->is_jprofilecache_compilation()) {
    _method_data = CURRENT_ENV->get_empty_methodData();
    return false;
  }
  if (h_m()->method_data() == NULL) {
    Method::build_interpreter_method_data(h_m, THREAD);
    if (HAS_PENDING_EXCEPTION) {
      CLEAR_PENDING_EXCEPTION;
    }
  }
  if (h_m()->method_data() != NULL) {
    _method_data = CURRENT_ENV->get_method_data(h_m()->method_data());
    _method_data->load_data();
    return true;
  } else {
    _method_data = CURRENT_ENV->get_empty_methodData();
    return false;
  }
}

// public, retroactive version
bool ciMethod::ensure_method_data() {
  bool result = true;
  if (_method_data == NULL || _method_data->is_empty()) {
    GUARDED_VM_ENTRY({
      result = ensure_method_data(get_Method());
    });
  }
  return result;
}


// ------------------------------------------------------------------
// ciMethod::method_data
//
ciMethodData* ciMethod::method_data() {
  if (_method_data != NULL) {
    return _method_data;
  }
  VM_ENTRY_MARK;
  ciEnv* env = CURRENT_ENV;
  Thread* my_thread = JavaThread::current();
  methodHandle h_m(my_thread, get_Method());

  if (JProfilingCacheCompileAdvance && CURRENT_ENV->task()->is_jprofilecache_compilation()) {
    _method_data = CURRENT_ENV->get_empty_methodData();
  } else if (h_m()->method_data() != NULL) {
    _method_data = CURRENT_ENV->get_method_data(h_m()->method_data());
    _method_data->load_data();
  } else {
    _method_data = CURRENT_ENV->get_empty_methodData();
  }
  return _method_data;

}

// ------------------------------------------------------------------
// ciMethod::method_data_or_null
// Returns a pointer to ciMethodData if MDO exists on the VM side,
// NULL otherwise.
ciMethodData* ciMethod::method_data_or_null() {
  ciMethodData *md = method_data();
  if (md->is_empty()) {
    return NULL;
  }
  return md;
}

// ------------------------------------------------------------------
// ciMethod::ensure_method_counters
//
MethodCounters* ciMethod::ensure_method_counters() {
  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_Method());
  MethodCounters* method_counters = mh->get_method_counters(CHECK_NULL);
  return method_counters;
}

// ------------------------------------------------------------------
// ciMethod::should_exclude
//
// Should this method be excluded from compilation?
bool ciMethod::should_exclude() {
  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_Method());
  bool ignore;
  return CompilerOracle::should_exclude(mh, ignore);
}

// ------------------------------------------------------------------
// ciMethod::should_inline
//
// Should this method be inlined during compilation?
bool ciMethod::should_inline() {
  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_Method());
  return CompilerOracle::should_inline(mh);
}

// ------------------------------------------------------------------
// ciMethod::should_not_inline
//
// Should this method be disallowed from inlining during compilation?
bool ciMethod::should_not_inline() {
  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_Method());
  return CompilerOracle::should_not_inline(mh);
}

// ------------------------------------------------------------------
// ciMethod::should_print_assembly
//
// Should the compiler print the generated code for this method?
bool ciMethod::should_print_assembly() {
  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_Method());
  return CompilerOracle::should_print(mh);
}

// ------------------------------------------------------------------
// ciMethod::break_at_execute
//
// Should the compiler insert a breakpoint into the generated code
// method?
bool ciMethod::break_at_execute() {
  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_Method());
  return CompilerOracle::should_break_at(mh);
}

// ------------------------------------------------------------------
// ciMethod::has_option
//
bool ciMethod::has_option(const char* option) {
  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_Method());
  return CompilerOracle::has_option_string(mh, option);
}

// ------------------------------------------------------------------
// ciMethod::has_option_value
//
template<typename T>
bool ciMethod::has_option_value(const char* option, T& value) {
  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_Method());
  return CompilerOracle::has_option_value(mh, option, value);
}
// Explicit instantiation for all OptionTypes supported.
template bool ciMethod::has_option_value<intx>(const char* option, intx& value);
template bool ciMethod::has_option_value<uintx>(const char* option, uintx& value);
template bool ciMethod::has_option_value<bool>(const char* option, bool& value);
template bool ciMethod::has_option_value<ccstr>(const char* option, ccstr& value);

// ------------------------------------------------------------------
// ciMethod::can_be_compiled
//
// Have previous compilations of this method succeeded?
bool ciMethod::can_be_compiled() {
  check_is_loaded();
  ciEnv* env = CURRENT_ENV;
  if (is_c1_compile(env->comp_level())) {
    return _is_c1_compilable;
  }
  return _is_c2_compilable;
}

// ------------------------------------------------------------------
// ciMethod::set_not_compilable
//
// Tell the VM that this method cannot be compiled at all.
void ciMethod::set_not_compilable(const char* reason) {
  check_is_loaded();
  VM_ENTRY_MARK;
  ciEnv* env = CURRENT_ENV;
  if (is_c1_compile(env->comp_level())) {
    _is_c1_compilable = false;
  } else {
    _is_c2_compilable = false;
  }
  get_Method()->set_not_compilable(env->comp_level(), true, reason);
}

// ------------------------------------------------------------------
// ciMethod::can_be_osr_compiled
//
// Have previous compilations of this method succeeded?
//
// Implementation note: the VM does not currently keep track
// of failed OSR compilations per bci.  The entry_bci parameter
// is currently unused.
bool ciMethod::can_be_osr_compiled(int entry_bci) {
  check_is_loaded();
  VM_ENTRY_MARK;
  ciEnv* env = CURRENT_ENV;
  return !get_Method()->is_not_osr_compilable(env->comp_level());
}

// ------------------------------------------------------------------
// ciMethod::has_compiled_code
bool ciMethod::has_compiled_code() {
  return instructions_size() > 0;
}

int ciMethod::comp_level() {
  check_is_loaded();
  VM_ENTRY_MARK;
  nmethod* nm = get_Method()->code();
  if (nm != NULL) return nm->comp_level();
  return 0;
}

int ciMethod::highest_osr_comp_level() {
  check_is_loaded();
  VM_ENTRY_MARK;
  return get_Method()->highest_osr_comp_level();
}

// ------------------------------------------------------------------
// ciMethod::code_size_for_inlining
//
// Code size for inlining decisions.  This method returns a code
// size of 1 for methods which has the ForceInline annotation.
int ciMethod::code_size_for_inlining() {
  check_is_loaded();
  if (get_Method()->force_inline()) {
    return 1;
  }
  return code_size();
}

// ------------------------------------------------------------------
// ciMethod::instructions_size
//
// This is a rough metric for "fat" methods, compared before inlining
// with InlineSmallCode.  The CodeBlob::code_size accessor includes
// junk like exception handler, stubs, and constant table, which are
// not highly relevant to an inlined method.  So we use the more
// specific accessor nmethod::insts_size.
int ciMethod::instructions_size() {
  if (_instructions_size == -1) {
    GUARDED_VM_ENTRY(
                     nmethod* code = get_Method()->code();
                     if (code != NULL && (code->comp_level() == CompLevel_full_optimization)) {
                       _instructions_size = code->insts_end() - code->verified_entry_point();
                     } else {
                       _instructions_size = 0;
                     }
                     );
  }
  return _instructions_size;
}

// ------------------------------------------------------------------
// ciMethod::log_nmethod_identity
void ciMethod::log_nmethod_identity(xmlStream* log) {
  GUARDED_VM_ENTRY(
    nmethod* code = get_Method()->code();
    if (code != NULL) {
      code->log_identity(log);
    }
  )
}

// ------------------------------------------------------------------
// ciMethod::is_not_reached
bool ciMethod::is_not_reached(int bci) {
  check_is_loaded();
  VM_ENTRY_MARK;
  return Interpreter::is_not_reached(
               methodHandle(THREAD, get_Method()), bci);
}

// ------------------------------------------------------------------
// ciMethod::was_never_executed
bool ciMethod::was_executed_more_than(int times) {
  VM_ENTRY_MARK;
  return get_Method()->was_executed_more_than(times);
}

// ------------------------------------------------------------------
// ciMethod::has_unloaded_classes_in_signature
bool ciMethod::has_unloaded_classes_in_signature() {
  VM_ENTRY_MARK;
  {
    EXCEPTION_MARK;
    methodHandle m(THREAD, get_Method());
    bool has_unloaded = Method::has_unloaded_classes_in_signature(m, (JavaThread *)THREAD);
    if( HAS_PENDING_EXCEPTION ) {
      CLEAR_PENDING_EXCEPTION;
      return true;     // Declare that we may have unloaded classes
    }
    return has_unloaded;
  }
}

// ------------------------------------------------------------------
// ciMethod::is_klass_loaded
bool ciMethod::is_klass_loaded(int refinfo_index, bool must_be_resolved) const {
  VM_ENTRY_MARK;
  return get_Method()->is_klass_loaded(refinfo_index, must_be_resolved);
}

// ------------------------------------------------------------------
// ciMethod::check_call
bool ciMethod::check_call(int refinfo_index, bool is_static) const {
  // This method is used only in C2 from InlineTree::ok_to_inline,
  // and is only used under -Xcomp or -XX:CompileTheWorld.
  // It appears to fail when applied to an invokeinterface call site.
  // FIXME: Remove this method and resolve_method_statically; refactor to use the other LinkResolver entry points.
  VM_ENTRY_MARK;
  {
    EXCEPTION_MARK;
    HandleMark hm(THREAD);
    constantPoolHandle pool (THREAD, get_Method()->constants());
    methodHandle spec_method;
    KlassHandle  spec_klass;
    Bytecodes::Code code = (is_static ? Bytecodes::_invokestatic : Bytecodes::_invokevirtual);
    LinkResolver::resolve_method_statically(spec_method, spec_klass, code, pool, refinfo_index, THREAD);
    if (HAS_PENDING_EXCEPTION) {
      CLEAR_PENDING_EXCEPTION;
      return false;
    } else {
      return (spec_method->is_static() == is_static);
    }
  }
  return false;
}

// ------------------------------------------------------------------
// ciMethod::print_codes
//
// Print the bytecodes for this method.
void ciMethod::print_codes_on(outputStream* st) {
  check_is_loaded();
  GUARDED_VM_ENTRY(get_Method()->print_codes_on(st);)
}


#define FETCH_FLAG_FROM_VM(flag_accessor) { \
  check_is_loaded(); \
  VM_ENTRY_MARK; \
  return get_Method()->flag_accessor(); \
}

bool ciMethod::is_empty_method() const {         FETCH_FLAG_FROM_VM(is_empty_method); }
bool ciMethod::is_vanilla_constructor() const {  FETCH_FLAG_FROM_VM(is_vanilla_constructor); }
bool ciMethod::has_loops      () const {         FETCH_FLAG_FROM_VM(has_loops); }
bool ciMethod::has_jsrs       () const {         FETCH_FLAG_FROM_VM(has_jsrs);  }
bool ciMethod::is_accessor    () const {         FETCH_FLAG_FROM_VM(is_accessor); }
bool ciMethod::is_initializer () const {         FETCH_FLAG_FROM_VM(is_initializer); }

bool ciMethod::is_boxing_method() const {
  if (holder()->is_box_klass()) {
    switch (intrinsic_id()) {
      case vmIntrinsics::_Boolean_valueOf:
      case vmIntrinsics::_Byte_valueOf:
      case vmIntrinsics::_Character_valueOf:
      case vmIntrinsics::_Short_valueOf:
      case vmIntrinsics::_Integer_valueOf:
      case vmIntrinsics::_Long_valueOf:
      case vmIntrinsics::_Float_valueOf:
      case vmIntrinsics::_Double_valueOf:
        return true;
      default:
        return false;
    }
  }
  return false;
}

bool ciMethod::is_unboxing_method() const {
  if (holder()->is_box_klass()) {
    switch (intrinsic_id()) {
      case vmIntrinsics::_booleanValue:
      case vmIntrinsics::_byteValue:
      case vmIntrinsics::_charValue:
      case vmIntrinsics::_shortValue:
      case vmIntrinsics::_intValue:
      case vmIntrinsics::_longValue:
      case vmIntrinsics::_floatValue:
      case vmIntrinsics::_doubleValue:
        return true;
      default:
        return false;
    }
  }
  return false;
}

BCEscapeAnalyzer  *ciMethod::get_bcea() {
#ifdef COMPILER2
  if (_bcea == NULL) {
    _bcea = new (CURRENT_ENV->arena()) BCEscapeAnalyzer(this, NULL);
  }
  return _bcea;
#else // COMPILER2
  ShouldNotReachHere();
  return NULL;
#endif // COMPILER2
}

ciMethodBlocks  *ciMethod::get_method_blocks() {
  Arena *arena = CURRENT_ENV->arena();
  if (_method_blocks == NULL) {
    _method_blocks = new (arena) ciMethodBlocks(arena, this);
  }
  return _method_blocks;
}

#undef FETCH_FLAG_FROM_VM

void ciMethod::dump_name_as_ascii(outputStream* st) {
  Method* method = get_Method();
  st->print("%s %s %s",
            method->klass_name()->as_quoted_ascii(),
            method->name()->as_quoted_ascii(),
            method->signature()->as_quoted_ascii());
}

void ciMethod::dump_replay_data(outputStream* st) {
  ResourceMark rm;
  Method* method = get_Method();
  MethodCounters* mcs = method->method_counters();
  st->print("ciMethod ");
  dump_name_as_ascii(st);
  st->print_cr(" %d %d %d %d %d",
               mcs == NULL ? 0 : mcs->invocation_counter()->raw_counter(),
               mcs == NULL ? 0 : mcs->backedge_counter()->raw_counter(),
               interpreter_invocation_count(),
               interpreter_throwout_count(),
               _instructions_size);
}

// ------------------------------------------------------------------
// ciMethod::print_codes
//
// Print a range of the bytecodes for this method.
void ciMethod::print_codes_on(int from, int to, outputStream* st) {
  check_is_loaded();
  GUARDED_VM_ENTRY(get_Method()->print_codes_on(from, to, st);)
}

// ------------------------------------------------------------------
// ciMethod::print_name
//
// Print the name of this method, including signature and some flags.
void ciMethod::print_name(outputStream* st) {
  check_is_loaded();
  GUARDED_VM_ENTRY(get_Method()->print_name(st);)
}

// ------------------------------------------------------------------
// ciMethod::print_short_name
//
// Print the name of this method, without signature.
void ciMethod::print_short_name(outputStream* st) {
  if (is_loaded()) {
    GUARDED_VM_ENTRY(get_Method()->print_short_name(st););
  } else {
    // Fall back if method is not loaded.
    holder()->print_name_on(st);
    st->print("::");
    name()->print_symbol_on(st);
    if (WizardMode)
      signature()->as_symbol()->print_symbol_on(st);
  }
}

// ------------------------------------------------------------------
// ciMethod::print_impl
//
// Implementation of the print method.
void ciMethod::print_impl(outputStream* st) {
  ciMetadata::print_impl(st);
  st->print(" name=");
  name()->print_symbol_on(st);
  st->print(" holder=");
  holder()->print_name_on(st);
  st->print(" signature=");
  signature()->as_symbol()->print_symbol_on(st);
  if (is_loaded()) {
    st->print(" loaded=true");
    st->print(" arg_size=%d", arg_size());
    st->print(" flags=");
    flags().print_member_flags(st);
  } else {
    st->print(" loaded=false");
  }
}
