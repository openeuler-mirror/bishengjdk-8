/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
*
*/

#include "precompiled.hpp"
#include "classfile/javaClasses.hpp"
#include "classfile/vmSymbols.hpp"
#include "jprofilecache/jitProfileCache.hpp"
#include "jprofilecache/jitProfileCacheThread.hpp"
#include "jprofilecache/jitProfileCacheDcmds.hpp"
#include "memory/resourceArea.hpp"
#include "oops/oop.inline.hpp"
#include "oops/symbol.hpp"
#include "runtime/handles.inline.hpp"
#include "services/diagnosticArgument.hpp"
#include "services/diagnosticFramework.hpp"
#include "utilities/globalDefinitions.hpp"


JitProfileCacheDCmds::JitProfileCacheDCmds(outputStream* output, bool heap_allocated) : DCmdWithParser(output, heap_allocated),
                                                                      _notify_precompile("-notify", "Notify JVM can start precompile", "BOOLEAN", false, "false"),
                                                                      _check_compile_finished("-check", "Check if the last precompilation submitted by JProfileCache is complete", "BOOLEAN", false, "false"),
                                                                      _deoptimize_compilation("-deopt", "Notify JVM to de-optimize precompile methods", "BOOLEAN", false, "false"),
                                                                      _help("-help", "Print this help information", "BOOLEAN", false, "false")
{
  _dcmdparser.add_dcmd_option(&_notify_precompile);
  _dcmdparser.add_dcmd_option(&_check_compile_finished);
  _dcmdparser.add_dcmd_option(&_deoptimize_compilation);
  _dcmdparser.add_dcmd_option(&_help);
}

int JitProfileCacheDCmds::num_arguments() {
  ResourceMark rm;
  JitProfileCacheDCmds* dcmd = new JitProfileCacheDCmds(NULL, false);
  if (dcmd != NULL) {
    DCmdMark mark(dcmd);
    return dcmd->_dcmdparser.num_arguments();
  } else {
    return 0;
  }
}

void JitProfileCacheDCmds::execute(DCmdSource source, TRAPS) {
  assert(is_init_completed(), "JVM is not fully initialized. Please try it later.");

  Klass* profilecache_class = SystemDictionary::resolve_or_fail(vmSymbols::com_huawei_jprofilecache_JProfileCache(), true, CHECK);
  instanceKlassHandle profilecacheClass (THREAD, profilecache_class);
  if (profilecacheClass->should_be_initialized()) {
    profilecacheClass->initialize(THREAD);
  }

  if (checkAndHandlePendingExceptions(output(), THREAD)) {
    return;
  }

  if (_notify_precompile.value()) {
    execute_trigger_precompilation(profilecacheClass, output(), THREAD);
  } else if (_check_compile_finished.value()) {
    execute_checkCompilation_finished(profilecacheClass, output(), THREAD);
  } else if (_deoptimize_compilation.value()) {
    execute_notifyDeopt_profileCache(profilecacheClass, output(), THREAD);
  } else {
    print_help_info();
  }
}

void JitProfileCacheDCmds::execute_trigger_precompilation(instanceKlassHandle profilecacheClass, outputStream* output, Thread* THREAD) {
  if (!JProfilingCacheCompileAdvance) {
    output->print_cr("JProfilingCacheCompileAdvance is off, triggerPrecompilation is invalid");
    return;
  }

  JavaValue result(T_VOID);
  JavaCalls::call_static(&result, profilecacheClass, vmSymbols::jprofilecache_trigger_precompilation_name(), vmSymbols::void_method_signature(), THREAD);
  if (checkAndHandlePendingExceptions(output, THREAD)) {
    return;
  }
}

void JitProfileCacheDCmds::execute_checkCompilation_finished(instanceKlassHandle profilecacheClass, outputStream* output, Thread* THREAD) {
  if (!JProfilingCacheCompileAdvance) {
    output->print_cr("JProfilingCacheCompileAdvance is off, checkIfCompilationIsComplete is invalid");
    return;
  }

  JavaValue result(T_BOOLEAN);
  JavaCalls::call_static(&result, profilecacheClass, vmSymbols::jprofilecache_check_if_compilation_is_complete_name(), vmSymbols::void_boolean_signature(), THREAD);
  if (checkAndHandlePendingExceptions(output, THREAD)) {
    return;
  }

  if (result.get_jboolean()) {
    output->print_cr("Last compilation task has compile finished.");
  } else {
    output->print_cr("Last compilation task not compile finish.");
  }
}

void JitProfileCacheDCmds::execute_notifyDeopt_profileCache(instanceKlassHandle profilecacheClass, outputStream* output, Thread* THREAD) {
  if (!(JProfilingCacheCompileAdvance && CompilationProfileCacheExplicitDeopt)) {
    output->print_cr("JProfilingCacheCompileAdvance or CompilationProfileCacheExplicitDeopt is off, notifyJVMDeoptProfileCacheMethods is invalid");
    return;
  }

  JavaValue result(T_VOID);
  JavaCalls::call_static(&result, profilecacheClass, vmSymbols::jprofilecache_notify_jvm_deopt_profilecache_methods_name(), vmSymbols::void_method_signature(), THREAD);
  if (checkAndHandlePendingExceptions(output, THREAD)) {
    return;
  }
}

bool JitProfileCacheDCmds::checkAndHandlePendingExceptions(outputStream* out, Thread* THREAD) {
  if (HAS_PENDING_EXCEPTION) {
    java_lang_Throwable::print(PENDING_EXCEPTION, out);
    CLEAR_PENDING_EXCEPTION;
    return true;
  }
  return false;
}
void JitProfileCacheDCmds::print_help_info() {
    output()->print_cr("The following commands are available:\n"
                       "-notify: %s\n"
                       "-check: %s\n"
                       "-deopt: %s\n"
                       "-help: %s\n",
                       _notify_precompile.description(), _check_compile_finished.description(), _deoptimize_compilation.description(), _help.description());
}
