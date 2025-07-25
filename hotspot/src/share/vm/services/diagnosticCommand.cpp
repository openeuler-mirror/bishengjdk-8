/*
 * Copyright (c) 2011, 2017, Oracle and/or its affiliates. All rights reserved.
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

#include "jprofilecache/jitProfileCacheDcmds.hpp"
#include "precompiled.hpp"
#include "cds/dynamicArchive.hpp"
#include "classfile/classLoaderHierarchyDCmd.hpp"
#include "classfile/classLoaderStats.hpp"
#include "gc_implementation/shared/vmGCOperations.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/os.hpp"
#include "services/diagnosticArgument.hpp"
#include "services/diagnosticCommand.hpp"
#include "services/diagnosticFramework.hpp"
#include "services/heapDumper.hpp"
#include "services/management.hpp"
#include "utilities/macros.hpp"
#include "oops/objArrayOop.hpp"
#include "code/codeCache.hpp"

#ifdef LINUX
#include "trimCHeapDCmd.hpp"
#include "mallocInfoDcmd.hpp"
#endif

PRAGMA_FORMAT_MUTE_WARNINGS_FOR_GCC

void DCmdRegistrant::register_dcmds(){
  // Registration of the diagnostic commands
  // First argument specifies which interfaces will export the command
  // Second argument specifies if the command is enabled
  // Third  argument specifies if the command is hidden
  uint32_t full_export = DCmd_Source_Internal | DCmd_Source_AttachAPI
                         | DCmd_Source_MBean;
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<HelpDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<VersionDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<CommandLineDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<PrintSystemPropertiesDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<PrintVMFlagsDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<VMDynamicLibrariesDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<VMUptimeDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<SystemGCDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<RunFinalizationDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<HeapInfoDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<FinalizerInfoDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<ChangeMaxHeapDCmd>(full_export, true, false));
#if INCLUDE_SERVICES // Heap dumping/inspection supported
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<HeapDumpDCmd>(DCmd_Source_Internal | DCmd_Source_AttachAPI, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<DynamicCDSDumpDCmd>(DCmd_Source_Internal | DCmd_Source_AttachAPI, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<ClassHistogramDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<ClassesDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<ClassStatsDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<MetaspaceDCmd>(full_export, true, false));
#endif // INCLUDE_SERVICES
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<ThreadDumpDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<RotateGCLogDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<ClassLoaderStatsDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<ClassLoaderHierarchyDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<CompileQueueDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<CodeListDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<CodeCacheDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<TouchedMethodsDCmd>(full_export, true, false));
#ifdef LINUX
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<TrimCLibcHeapDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<MallocInfoDcmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<PerfMapDCmd>(full_export, true, false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<JitProfileCacheDCmds>(full_export, true, false));
#endif // LINUX

  // Enhanced JMX Agent Support
  // These commands won't be exported via the DiagnosticCommandMBean until an
  // appropriate permission is created for them
  uint32_t jmx_agent_export_flags = DCmd_Source_Internal | DCmd_Source_AttachAPI;
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<JMXStartRemoteDCmd>(jmx_agent_export_flags, true,false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<JMXStartLocalDCmd>(jmx_agent_export_flags, true,false));
  DCmdFactory::register_DCmdFactory(new DCmdFactoryImpl<JMXStopRemoteDCmd>(jmx_agent_export_flags, true,false));

}

#ifndef HAVE_EXTRA_DCMD
void DCmdRegistrant::register_dcmds_ext(){
   // Do nothing here
}
#endif


HelpDCmd::HelpDCmd(outputStream* output, bool heap) : DCmdWithParser(output, heap),
  _all("-all", "Show help for all commands", "BOOLEAN", false, "false"),
  _cmd("command name", "The name of the command for which we want help",
        "STRING", false) {
  _dcmdparser.add_dcmd_option(&_all);
  _dcmdparser.add_dcmd_argument(&_cmd);
};

static int compare_strings(const char** s1, const char** s2) {
  return ::strcmp(*s1, *s2);
}

void HelpDCmd::execute(DCmdSource source, TRAPS) {
  if (_all.value()) {
    GrowableArray<const char*>* cmd_list = DCmdFactory::DCmd_list(source);
    cmd_list->sort(compare_strings);
    for (int i = 0; i < cmd_list->length(); i++) {
      DCmdFactory* factory = DCmdFactory::factory(source, cmd_list->at(i),
                                                  strlen(cmd_list->at(i)));
      output()->print_cr("%s%s", factory->name(),
                         factory->is_enabled() ? "" : " [disabled]");
      output()->print_cr("\t%s", factory->description());
      output()->cr();
      factory = factory->next();
    }
  } else if (_cmd.has_value()) {
    DCmd* cmd = NULL;
    DCmdFactory* factory = DCmdFactory::factory(source, _cmd.value(),
                                                strlen(_cmd.value()));
    if (factory != NULL) {
      output()->print_cr("%s%s", factory->name(),
                         factory->is_enabled() ? "" : " [disabled]");
      output()->print_cr("%s", factory->description());
      output()->print_cr("\nImpact: %s", factory->impact());
      JavaPermission p = factory->permission();
      if(p._class != NULL) {
        if(p._action != NULL) {
          output()->print_cr("\nPermission: %s(%s, %s)",
                  p._class, p._name == NULL ? "null" : p._name, p._action);
        } else {
          output()->print_cr("\nPermission: %s(%s)",
                  p._class, p._name == NULL ? "null" : p._name);
        }
      }
      output()->cr();
      cmd = factory->create_resource_instance(output());
      if (cmd != NULL) {
        DCmdMark mark(cmd);
        cmd->print_help(factory->name());
      }
    } else {
      output()->print_cr("Help unavailable : '%s' : No such command", _cmd.value());
    }
  } else {
    output()->print_cr("The following commands are available:");
    GrowableArray<const char *>* cmd_list = DCmdFactory::DCmd_list(source);
    cmd_list->sort(compare_strings);
    for (int i = 0; i < cmd_list->length(); i++) {
      DCmdFactory* factory = DCmdFactory::factory(source, cmd_list->at(i),
                                                  strlen(cmd_list->at(i)));
      output()->print_cr("%s%s", factory->name(),
                         factory->is_enabled() ? "" : " [disabled]");
      factory = factory->_next;
    }
    output()->print_cr("\nFor more information about a specific command use 'help <command>'.");
  }
}

int HelpDCmd::num_arguments() {
  ResourceMark rm;
  HelpDCmd* dcmd = new HelpDCmd(NULL, false);
  if (dcmd != NULL) {
    DCmdMark mark(dcmd);
    return dcmd->_dcmdparser.num_arguments();
  } else {
    return 0;
  }
}

void VersionDCmd::execute(DCmdSource source, TRAPS) {
  output()->print_cr("%s version %s", Abstract_VM_Version::vm_name(),
          Abstract_VM_Version::vm_release());
  JDK_Version jdk_version = JDK_Version::current();
  if (jdk_version.update_version() > 0) {
    output()->print_cr("JDK %d.%d_%02d", jdk_version.major_version(),
            jdk_version.minor_version(), jdk_version.update_version());
  } else {
    output()->print_cr("JDK %d.%d", jdk_version.major_version(),
            jdk_version.minor_version());
  }
}

PrintVMFlagsDCmd::PrintVMFlagsDCmd(outputStream* output, bool heap) :
                                   DCmdWithParser(output, heap),
  _all("-all", "Print all flags supported by the VM", "BOOLEAN", false, "false") {
  _dcmdparser.add_dcmd_option(&_all);
}

void PrintVMFlagsDCmd::execute(DCmdSource source, TRAPS) {
  if (_all.value()) {
    CommandLineFlags::printFlags(output(), true);
  } else {
    CommandLineFlags::printSetFlags(output());
  }
}

int PrintVMFlagsDCmd::num_arguments() {
    ResourceMark rm;
    PrintVMFlagsDCmd* dcmd = new PrintVMFlagsDCmd(NULL, false);
    if (dcmd != NULL) {
      DCmdMark mark(dcmd);
      return dcmd->_dcmdparser.num_arguments();
    } else {
      return 0;
    }
}

void PrintSystemPropertiesDCmd::execute(DCmdSource source, TRAPS) {
  // load sun.misc.VMSupport
  Symbol* klass = vmSymbols::sun_misc_VMSupport();
  Klass* k = SystemDictionary::resolve_or_fail(klass, true, CHECK);
  instanceKlassHandle ik (THREAD, k);
  if (ik->should_be_initialized()) {
    ik->initialize(THREAD);
  }
  if (HAS_PENDING_EXCEPTION) {
    java_lang_Throwable::print(PENDING_EXCEPTION, output());
    output()->cr();
    CLEAR_PENDING_EXCEPTION;
    return;
  }

  // invoke the serializePropertiesToByteArray method
  JavaValue result(T_OBJECT);
  JavaCallArguments args;

  Symbol* signature = vmSymbols::serializePropertiesToByteArray_signature();
  JavaCalls::call_static(&result,
                         ik,
                         vmSymbols::serializePropertiesToByteArray_name(),
                         signature,
                         &args,
                         THREAD);
  if (HAS_PENDING_EXCEPTION) {
    java_lang_Throwable::print(PENDING_EXCEPTION, output());
    output()->cr();
    CLEAR_PENDING_EXCEPTION;
    return;
  }

  // The result should be a [B
  oop res = (oop)result.get_jobject();
  assert(res->is_typeArray(), "just checking");
  assert(TypeArrayKlass::cast(res->klass())->element_type() == T_BYTE, "just checking");

  // copy the bytes to the output stream
  typeArrayOop ba = typeArrayOop(res);
  jbyte* addr = typeArrayOop(res)->byte_at_addr(0);
  output()->print_raw((const char*)addr, ba->length());
}

VMUptimeDCmd::VMUptimeDCmd(outputStream* output, bool heap) :
                           DCmdWithParser(output, heap),
  _date("-date", "Add a prefix with current date", "BOOLEAN", false, "false") {
  _dcmdparser.add_dcmd_option(&_date);
}

void VMUptimeDCmd::execute(DCmdSource source, TRAPS) {
  if (_date.value()) {
    output()->date_stamp(true, "", ": ");
  }
  output()->time_stamp().update_to(tty->time_stamp().ticks());
  output()->stamp();
  output()->print_cr(" s");
}

int VMUptimeDCmd::num_arguments() {
  ResourceMark rm;
  VMUptimeDCmd* dcmd = new VMUptimeDCmd(NULL, false);
  if (dcmd != NULL) {
    DCmdMark mark(dcmd);
    return dcmd->_dcmdparser.num_arguments();
  } else {
    return 0;
  }
}

void SystemGCDCmd::execute(DCmdSource source, TRAPS) {
  if (!DisableExplicitGC) {
    Universe::heap()->collect(GCCause::_java_lang_system_gc);
  } else {
    output()->print_cr("Explicit GC is disabled, no GC has been performed.");
  }
}

void RunFinalizationDCmd::execute(DCmdSource source, TRAPS) {
  Klass* k = SystemDictionary::resolve_or_fail(vmSymbols::java_lang_System(),
                                                 true, CHECK);
  instanceKlassHandle klass(THREAD, k);
  JavaValue result(T_VOID);
  JavaCalls::call_static(&result, klass,
                         vmSymbols::run_finalization_name(),
                         vmSymbols::void_method_signature(), CHECK);
}

void HeapInfoDCmd::execute(DCmdSource source, TRAPS) {
  Universe::heap()->print_on(output());
}

void FinalizerInfoDCmd::execute(DCmdSource source, TRAPS) {
  ResourceMark rm;


  Klass* k = SystemDictionary::resolve_or_null(
    vmSymbols::finalizer_histogram_klass(), THREAD);
  assert(k != NULL, "FinalizerHistogram class is not accessible");

  instanceKlassHandle klass(THREAD, k);
  JavaValue result(T_ARRAY);

  // We are calling lang.ref.FinalizerHistogram.getFinalizerHistogram() method
  // and expect it to return array of FinalizerHistogramEntry as Object[]

  JavaCalls::call_static(&result, klass,
                         vmSymbols::get_finalizer_histogram_name(),
                         vmSymbols::void_finalizer_histogram_entry_array_signature(), CHECK);

  objArrayOop result_oop = (objArrayOop) result.get_jobject();
  if (result_oop->length() == 0) {
    output()->print_cr("No instances waiting for finalization found");
    return;
  }

  oop foop = result_oop->obj_at(0);
  InstanceKlass* ik = InstanceKlass::cast(foop->klass());

  fieldDescriptor count_fd, name_fd;

  Klass* count_res = ik->find_field(
    vmSymbols::finalizer_histogram_entry_count_field(), vmSymbols::int_signature(), &count_fd);

  Klass* name_res = ik->find_field(
    vmSymbols::finalizer_histogram_entry_name_field(), vmSymbols::string_signature(), &name_fd);

  assert(count_res != NULL && name_res != NULL, "Unexpected layout of FinalizerHistogramEntry");

  output()->print_cr("Unreachable instances waiting for finalization");
  output()->print_cr("#instances  class name");
  output()->print_cr("-----------------------");

  for (int i = 0; i < result_oop->length(); ++i) {
    oop element_oop = result_oop->obj_at(i);
    oop str_oop = element_oop->obj_field(name_fd.offset());
    char *name = java_lang_String::as_utf8_string(str_oop);
    int count = element_oop->int_field(count_fd.offset());
    output()->print_cr("%10d  %s", count, name);
  }
}

ChangeMaxHeapDCmd::ChangeMaxHeapDCmd(outputStream* output, bool heap) :
  DCmdWithParser(output, heap),
  _new_max_heap_size("change_max_heap", "New max size of heap", "MEMORY SIZE", true) {
  _dcmdparser.add_dcmd_argument(&_new_max_heap_size);
}

int ChangeMaxHeapDCmd::num_arguments() {
  ResourceMark rm;
  ChangeMaxHeapDCmd* dcmd = new ChangeMaxHeapDCmd(NULL, false);
  if (dcmd != NULL) {
    DCmdMark mark(dcmd);
    return dcmd->_dcmdparser.num_arguments();
  } else {
    return 0;
  }
}

void ChangeMaxHeapDCmd::execute(DCmdSource source, TRAPS) {
  if (!Universe::is_dynamic_max_heap_enable()) {
    output()->print_cr("not supported because -XX:DynamicMaxHeapSizeLimit was not specified");
    return;
  }
  jlong input_max_heap_size = _new_max_heap_size.value()._size;
  size_t heap_alignment = Universe::heap()->collector_policy()->heap_alignment();
  jlong new_max_heap_size = align_size_up((size_t)input_max_heap_size, heap_alignment);
  output()->print_cr("align the given value " SIZE_FORMAT " up to "SIZE_FORMAT "K for heap alignment "SIZE_FORMAT "K",
                    input_max_heap_size,
                    (new_max_heap_size / K),
                    (heap_alignment / K));
  bool is_validate = Universe::heap()->check_new_max_heap_validity(new_max_heap_size, output());
  if (!is_validate) {
    output()->print_cr("GC.change_max_heap fail");
    return;
  }
  output()->print_cr("GC.change_max_heap (" SIZE_FORMAT "K" "->" SIZE_FORMAT "K)(" SIZE_FORMAT "K)",
                    (Universe::heap()->current_max_heap_size() / K),
                    (new_max_heap_size / K),
                    (Universe::heap()->collector_policy()->max_heap_byte_size_limit() / K));
  bool success = Universe::heap()->change_max_heap(new_max_heap_size);
  if (success) {
    output()->print_cr("GC.change_max_heap success");
  } else {
    output()->print_cr("GC.change_max_heap fail");
  }
}

#if INCLUDE_SERVICES // Heap dumping/inspection supported
HeapDumpDCmd::HeapDumpDCmd(outputStream* output, bool heap) :
                           DCmdWithParser(output, heap),
  _filename("filename","Name of the dump file", "STRING",true),
  _all("-all", "Dump all objects, including unreachable objects",
       "BOOLEAN", false, "false") {
  _dcmdparser.add_dcmd_option(&_all);
  _dcmdparser.add_dcmd_argument(&_filename);
}

void HeapDumpDCmd::execute(DCmdSource source, TRAPS) {
  // Request a full GC before heap dump if _all is false
  // This helps reduces the amount of unreachable objects in the dump
  // and makes it easier to browse.
  HeapDumper dumper(!_all.value() /* request GC if _all is false*/);
  int res = dumper.dump(_filename.value());
  if (res == 0) {
    output()->print_cr("Heap dump file created");
  } else {
    // heap dump failed
    ResourceMark rm;
    char* error = dumper.error_as_C_string();
    if (error == NULL) {
      output()->print_cr("Dump failed - reason unknown");
    } else {
      output()->print_cr("%s", error);
    }
  }
}

int HeapDumpDCmd::num_arguments() {
  ResourceMark rm;
  HeapDumpDCmd* dcmd = new HeapDumpDCmd(NULL, false);
  if (dcmd != NULL) {
    DCmdMark mark(dcmd);
    return dcmd->_dcmdparser.num_arguments();
  } else {
    return 0;
  }
}

void DynamicCDSDumpDCmd::execute(DCmdSource source, TRAPS) {
#if INCLUDE_CDS
  if (DynamicDumpSharedSpaces) {
    DynamicArchive::dump();
    ShouldNotReachHere();
  } else {
    warning("Dynamic CDS is not enabled");
  }
#endif
}

ClassHistogramDCmd::ClassHistogramDCmd(outputStream* output, bool heap) :
                                       DCmdWithParser(output, heap),
  _all("-all", "Inspect all objects, including unreachable objects",
       "BOOLEAN", false, "false") {
  _dcmdparser.add_dcmd_option(&_all);
}

void ClassHistogramDCmd::execute(DCmdSource source, TRAPS) {
  VM_GC_HeapInspection heapop(output(),
                              !_all.value() /* request full gc if false */);
  VMThread::execute(&heapop);
}

int ClassHistogramDCmd::num_arguments() {
  ResourceMark rm;
  ClassHistogramDCmd* dcmd = new ClassHistogramDCmd(NULL, false);
  if (dcmd != NULL) {
    DCmdMark mark(dcmd);
    return dcmd->_dcmdparser.num_arguments();
  } else {
    return 0;
  }
}

ClassesDCmd::ClassesDCmd(outputStream* output, bool heap) :
                                     DCmdWithParser(output, heap),
  _verbose("-verbose",
           "Dump the detailed content of a Java class. "
           "Some classes are annotated with flags: "
           "F = has, or inherits, a non-empty finalize method, "
           "f = has final method, "
           "W = methods rewritten, "
           "C = marked with @Contended annotation, "
           "R = has been redefined, "
           "S = is shared class",
           "BOOLEAN", false, "false") {
  _dcmdparser.add_dcmd_option(&_verbose);
}

class VM_PrintClasses : public VM_Operation {
private:
  outputStream* _out;
  bool _verbose;
public:
  VM_PrintClasses(outputStream* out, bool verbose) : _out(out), _verbose(verbose) {}

  virtual VMOp_Type type() const { return VMOp_PrintClasses; }

  virtual void doit() {
    PrintClassClosure closure(_out, _verbose);
    ClassLoaderDataGraph::classes_do(&closure);
  }
};

void ClassesDCmd::execute(DCmdSource source, TRAPS) {
  VM_PrintClasses vmop(output(), _verbose.is_set());
  VMThread::execute(&vmop);
}

int ClassesDCmd::num_arguments() {
  ResourceMark rm;
  ClassesDCmd* dcmd = new ClassesDCmd(NULL, false);
  if (dcmd != NULL) {
    DCmdMark mark(dcmd);
    return dcmd->_dcmdparser.num_arguments();
  } else {
    return 0;
  }
}

#define DEFAULT_COLUMNS "InstBytes,KlassBytes,CpAll,annotations,MethodCount,Bytecodes,MethodAll,ROAll,RWAll,Total"
ClassStatsDCmd::ClassStatsDCmd(outputStream* output, bool heap) :
                                       DCmdWithParser(output, heap),
  _csv("-csv", "Print in CSV (comma-separated values) format for spreadsheets",
       "BOOLEAN", false, "false"),
  _all("-all", "Show all columns",
       "BOOLEAN", false, "false"),
  _help("-help", "Show meaning of all the columns",
       "BOOLEAN", false, "false"),
  _columns("columns", "Comma-separated list of all the columns to show. "
           "If not specified, the following columns are shown: " DEFAULT_COLUMNS,
           "STRING", false) {
  _dcmdparser.add_dcmd_option(&_all);
  _dcmdparser.add_dcmd_option(&_csv);
  _dcmdparser.add_dcmd_option(&_help);
  _dcmdparser.add_dcmd_argument(&_columns);
}

void ClassStatsDCmd::execute(DCmdSource source, TRAPS) {
  if (!UnlockDiagnosticVMOptions) {
    output()->print_cr("GC.class_stats command requires -XX:+UnlockDiagnosticVMOptions");
    return;
  }

  VM_GC_HeapInspection heapop(output(),
                              true /* request_full_gc */);
  heapop.set_csv_format(_csv.value());
  heapop.set_print_help(_help.value());
  heapop.set_print_class_stats(true);
  if (_all.value()) {
    if (_columns.has_value()) {
      output()->print_cr("Cannot specify -all and individual columns at the same time");
      return;
    } else {
      heapop.set_columns(NULL);
    }
  } else {
    if (_columns.has_value()) {
      heapop.set_columns(_columns.value());
    } else {
      heapop.set_columns(DEFAULT_COLUMNS);
    }
  }
  VMThread::execute(&heapop);
}

int ClassStatsDCmd::num_arguments() {
  ResourceMark rm;
  ClassStatsDCmd* dcmd = new ClassStatsDCmd(NULL, false);
  if (dcmd != NULL) {
    DCmdMark mark(dcmd);
    return dcmd->_dcmdparser.num_arguments();
  } else {
    return 0;
  }
}
#endif // INCLUDE_SERVICES

ThreadDumpDCmd::ThreadDumpDCmd(outputStream* output, bool heap) :
                               DCmdWithParser(output, heap),
  _locks("-l", "print java.util.concurrent locks", "BOOLEAN", false, "false"),
  _extended("-e", "print extended thread information", "BOOLEAN", false, "false") {
  _dcmdparser.add_dcmd_option(&_locks);
  _dcmdparser.add_dcmd_option(&_extended);
}

void ThreadDumpDCmd::execute(DCmdSource source, TRAPS) {
  // thread stacks
  VM_PrintThreads op1(output(), _locks.value(), _extended.value());
  VMThread::execute(&op1);

  // JNI global handles
  VM_PrintJNI op2(output());
  VMThread::execute(&op2);

  // Deadlock detection
  VM_FindDeadlocks op3(output());
  VMThread::execute(&op3);
}

int ThreadDumpDCmd::num_arguments() {
  ResourceMark rm;
  ThreadDumpDCmd* dcmd = new ThreadDumpDCmd(NULL, false);
  if (dcmd != NULL) {
    DCmdMark mark(dcmd);
    return dcmd->_dcmdparser.num_arguments();
  } else {
    return 0;
  }
}

class VM_DumpTouchedMethods : public VM_Operation {
private:
  outputStream* _out;
public:
  VM_DumpTouchedMethods(outputStream* out) {
    _out = out;
  }

  virtual VMOp_Type type() const { return VMOp_DumpTouchedMethods; }

  virtual void doit() {
    Method::print_touched_methods(_out);
  }
};

TouchedMethodsDCmd::TouchedMethodsDCmd(outputStream* output, bool heap) :
                                       DCmdWithParser(output, heap)
{}

void TouchedMethodsDCmd::execute(DCmdSource source, TRAPS) {
  if (!UnlockDiagnosticVMOptions) {
    output()->print_cr("VM.touched_methods command requires -XX:+UnlockDiagnosticVMOptions");
    return;
  }
  VM_DumpTouchedMethods dumper(output());
  VMThread::execute(&dumper);
}

int TouchedMethodsDCmd::num_arguments() {
  return 0;
}

// Enhanced JMX Agent support

JMXStartRemoteDCmd::JMXStartRemoteDCmd(outputStream *output, bool heap_allocated) :

  DCmdWithParser(output, heap_allocated),

  _config_file
  ("config.file",
   "set com.sun.management.config.file", "STRING", false),

  _jmxremote_host
  ("jmxremote.host",
   "set com.sun.management.jmxremote.host", "STRING", false),

  _jmxremote_port
  ("jmxremote.port",
   "set com.sun.management.jmxremote.port", "STRING", false),

  _jmxremote_rmi_port
  ("jmxremote.rmi.port",
   "set com.sun.management.jmxremote.rmi.port", "STRING", false),

  _jmxremote_ssl
  ("jmxremote.ssl",
   "set com.sun.management.jmxremote.ssl", "STRING", false),

  _jmxremote_registry_ssl
  ("jmxremote.registry.ssl",
   "set com.sun.management.jmxremote.registry.ssl", "STRING", false),

  _jmxremote_authenticate
  ("jmxremote.authenticate",
   "set com.sun.management.jmxremote.authenticate", "STRING", false),

  _jmxremote_password_file
  ("jmxremote.password.file",
   "set com.sun.management.jmxremote.password.file", "STRING", false),

  _jmxremote_access_file
  ("jmxremote.access.file",
   "set com.sun.management.jmxremote.access.file", "STRING", false),

  _jmxremote_login_config
  ("jmxremote.login.config",
   "set com.sun.management.jmxremote.login.config", "STRING", false),

  _jmxremote_ssl_enabled_cipher_suites
  ("jmxremote.ssl.enabled.cipher.suites",
   "set com.sun.management.jmxremote.ssl.enabled.cipher.suite", "STRING", false),

  _jmxremote_ssl_enabled_protocols
  ("jmxremote.ssl.enabled.protocols",
   "set com.sun.management.jmxremote.ssl.enabled.protocols", "STRING", false),

  _jmxremote_ssl_need_client_auth
  ("jmxremote.ssl.need.client.auth",
   "set com.sun.management.jmxremote.need.client.auth", "STRING", false),

  _jmxremote_ssl_config_file
  ("jmxremote.ssl.config.file",
   "set com.sun.management.jmxremote.ssl_config_file", "STRING", false),

// JDP Protocol support
  _jmxremote_autodiscovery
  ("jmxremote.autodiscovery",
   "set com.sun.management.jmxremote.autodiscovery", "STRING", false),

   _jdp_port
  ("jdp.port",
   "set com.sun.management.jdp.port", "INT", false),

   _jdp_address
  ("jdp.address",
   "set com.sun.management.jdp.address", "STRING", false),

   _jdp_source_addr
  ("jdp.source_addr",
   "set com.sun.management.jdp.source_addr", "STRING", false),

   _jdp_ttl
  ("jdp.ttl",
   "set com.sun.management.jdp.ttl", "INT", false),

   _jdp_pause
  ("jdp.pause",
   "set com.sun.management.jdp.pause", "INT", false),

   _jdp_name
  ("jdp.name",
   "set com.sun.management.jdp.name", "STRING", false)

  {
    _dcmdparser.add_dcmd_option(&_config_file);
    _dcmdparser.add_dcmd_option(&_jmxremote_host);
    _dcmdparser.add_dcmd_option(&_jmxremote_port);
    _dcmdparser.add_dcmd_option(&_jmxremote_rmi_port);
    _dcmdparser.add_dcmd_option(&_jmxremote_ssl);
    _dcmdparser.add_dcmd_option(&_jmxremote_registry_ssl);
    _dcmdparser.add_dcmd_option(&_jmxremote_authenticate);
    _dcmdparser.add_dcmd_option(&_jmxremote_password_file);
    _dcmdparser.add_dcmd_option(&_jmxremote_access_file);
    _dcmdparser.add_dcmd_option(&_jmxremote_login_config);
    _dcmdparser.add_dcmd_option(&_jmxremote_ssl_enabled_cipher_suites);
    _dcmdparser.add_dcmd_option(&_jmxremote_ssl_enabled_protocols);
    _dcmdparser.add_dcmd_option(&_jmxremote_ssl_need_client_auth);
    _dcmdparser.add_dcmd_option(&_jmxremote_ssl_config_file);
    _dcmdparser.add_dcmd_option(&_jmxremote_autodiscovery);
    _dcmdparser.add_dcmd_option(&_jdp_port);
    _dcmdparser.add_dcmd_option(&_jdp_address);
    _dcmdparser.add_dcmd_option(&_jdp_source_addr);
    _dcmdparser.add_dcmd_option(&_jdp_ttl);
    _dcmdparser.add_dcmd_option(&_jdp_pause);
    _dcmdparser.add_dcmd_option(&_jdp_name);
}


int JMXStartRemoteDCmd::num_arguments() {
  ResourceMark rm;
  JMXStartRemoteDCmd* dcmd = new JMXStartRemoteDCmd(NULL, false);
  if (dcmd != NULL) {
    DCmdMark mark(dcmd);
    return dcmd->_dcmdparser.num_arguments();
  } else {
    return 0;
  }
}


void JMXStartRemoteDCmd::execute(DCmdSource source, TRAPS) {
    ResourceMark rm(THREAD);
    HandleMark hm(THREAD);

    // Load and initialize the sun.management.Agent class
    // invoke startRemoteManagementAgent(string) method to start
    // the remote management server.
    // throw java.lang.NoSuchMethodError if the method doesn't exist

    Handle loader = Handle(THREAD, SystemDictionary::java_system_loader());
    Klass* k = SystemDictionary::resolve_or_fail(vmSymbols::sun_management_Agent(), loader, Handle(), true, CHECK);
    instanceKlassHandle ik (THREAD, k);

    JavaValue result(T_VOID);

    // Pass all command line arguments to java as key=value,...
    // All checks are done on java side

    int len = 0;
    stringStream options;
    char comma[2] = {0,0};

    // Leave default values on Agent.class side and pass only
    // agruments explicitly set by user. All arguments passed
    // to jcmd override properties with the same name set by
    // command line with -D or by managmenent.properties
    // file.
#define PUT_OPTION(a) \
    if ( (a).is_set() ){ \
        options.print(\
               ( *((a).type()) == 'I' ) ? "%scom.sun.management.%s=%d" : "%scom.sun.management.%s=%s",\
                comma, (a).name(), (a).value()); \
        comma[0] = ','; \
    }

    PUT_OPTION(_config_file);
    PUT_OPTION(_jmxremote_host);
    PUT_OPTION(_jmxremote_port);
    PUT_OPTION(_jmxremote_rmi_port);
    PUT_OPTION(_jmxremote_ssl);
    PUT_OPTION(_jmxremote_registry_ssl);
    PUT_OPTION(_jmxremote_authenticate);
    PUT_OPTION(_jmxremote_password_file);
    PUT_OPTION(_jmxremote_access_file);
    PUT_OPTION(_jmxremote_login_config);
    PUT_OPTION(_jmxremote_ssl_enabled_cipher_suites);
    PUT_OPTION(_jmxremote_ssl_enabled_protocols);
    PUT_OPTION(_jmxremote_ssl_need_client_auth);
    PUT_OPTION(_jmxremote_ssl_config_file);
    PUT_OPTION(_jmxremote_autodiscovery);
    PUT_OPTION(_jdp_port);
    PUT_OPTION(_jdp_address);
    PUT_OPTION(_jdp_source_addr);
    PUT_OPTION(_jdp_ttl);
    PUT_OPTION(_jdp_pause);
    PUT_OPTION(_jdp_name);

#undef PUT_OPTION

    Handle str = java_lang_String::create_from_str(options.as_string(), CHECK);
    JavaCalls::call_static(&result, ik, vmSymbols::startRemoteAgent_name(), vmSymbols::string_void_signature(), str, CHECK);
}

JMXStartLocalDCmd::JMXStartLocalDCmd(outputStream *output, bool heap_allocated) :
  DCmd(output, heap_allocated) {
  // do nothing
}

void JMXStartLocalDCmd::execute(DCmdSource source, TRAPS) {
    ResourceMark rm(THREAD);
    HandleMark hm(THREAD);

    // Load and initialize the sun.management.Agent class
    // invoke startLocalManagementAgent(void) method to start
    // the local management server
    // throw java.lang.NoSuchMethodError if method doesn't exist

    Handle loader = Handle(THREAD, SystemDictionary::java_system_loader());
    Klass* k = SystemDictionary::resolve_or_fail(vmSymbols::sun_management_Agent(), loader, Handle(), true, CHECK);
    instanceKlassHandle ik (THREAD, k);

    JavaValue result(T_VOID);
    JavaCalls::call_static(&result, ik, vmSymbols::startLocalAgent_name(), vmSymbols::void_method_signature(), CHECK);
}

void JMXStopRemoteDCmd::execute(DCmdSource source, TRAPS) {
    ResourceMark rm(THREAD);
    HandleMark hm(THREAD);

    // Load and initialize the sun.management.Agent class
    // invoke stopRemoteManagementAgent method to stop the
    // management server
    // throw java.lang.NoSuchMethodError if method doesn't exist

    Handle loader = Handle(THREAD, SystemDictionary::java_system_loader());
    Klass* k = SystemDictionary::resolve_or_fail(vmSymbols::sun_management_Agent(), loader, Handle(), true, CHECK);
    instanceKlassHandle ik (THREAD, k);

    JavaValue result(T_VOID);
    JavaCalls::call_static(&result, ik, vmSymbols::stopRemoteAgent_name(), vmSymbols::void_method_signature(), CHECK);
}

VMDynamicLibrariesDCmd::VMDynamicLibrariesDCmd(outputStream *output, bool heap_allocated) :
  DCmd(output, heap_allocated) {
  // do nothing
}

void VMDynamicLibrariesDCmd::execute(DCmdSource source, TRAPS) {
  os::print_dll_info(output());
  output()->cr();
}

void RotateGCLogDCmd::execute(DCmdSource source, TRAPS) {
  if (UseGCLogFileRotation) {
    VM_RotateGCLog rotateop(output());
    VMThread::execute(&rotateop);
  } else {
    output()->print_cr("Target VM does not support GC log file rotation.");
  }
}

void CompileQueueDCmd::execute(DCmdSource source, TRAPS) {
  VM_PrintCompileQueue printCompileQueueOp(output());
  VMThread::execute(&printCompileQueueOp);
}

void CodeListDCmd::execute(DCmdSource source, TRAPS) {
  VM_PrintCodeList printCodeListOp(output());
  VMThread::execute(&printCodeListOp);
}

void CodeCacheDCmd::execute(DCmdSource source, TRAPS) {
  VM_PrintCodeCache printCodeCacheOp(output());
  VMThread::execute(&printCodeCacheOp);
}

#ifdef LINUX
void PerfMapDCmd::execute(DCmdSource source, TRAPS) {
  CodeCache::write_perf_map();
}
#endif // LINUX
