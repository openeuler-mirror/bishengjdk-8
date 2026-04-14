/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
 */

#include "matrix/matrixManager.hpp"

#include <unistd.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixAllowList.hpp"
#include "matrix/ubHeapMemory.hpp"
#include "matrix/ubSocket.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/thread.hpp"
#include "runtime/vframe.hpp"

const char* OPTION_BLANK_VALUE = "";

AllowListTable* MatrixGlobal::_allow_list_table = NULL;
outputStream* MatrixGlobal::_log_file = NULL;
bool MatrixGlobal::_early_initialized = false;
bool MatrixGlobal::_initialized = false;

bool check_runtime_flags() {
  bool io_features_enable = false;
  bool heap_features_enable = false;

  heap_features_enable = UseBorrowedMemory;

  if (UseUBFile || UseUBSocket) {
    io_features_enable = true;
    if (UBConfPath == NULL || strcmp(UBConfPath, OPTION_BLANK_VALUE) == 0) {
      tty->print_cr("UB conf path is NULL, UB-io-related features are disabled.");
      io_features_enable = false;
    }
    if (UBLogPath == NULL) {  // optional
      tty->print_cr("UB log path is NULL, check UBLogPath please.");
    }
  }

  if (io_features_enable == false) {
    if (PrintUBLog != false) {
      tty->print_cr(
          "Invalid flag PrintUBLog, no UB-io-related features are enabled.");
    }
    if (strcmp(UBConfPath, OPTION_BLANK_VALUE) != 0) {
      tty->print_cr(
          "Invalid flag UBConfPath, no UB-io-related features are enabled.");
    }
    if (strcmp(UBLogPath, OPTION_BLANK_VALUE) != 0) {
      tty->print_cr(
          "Invalid flag UBLogPath, no UB-io-related features are enabled.");
    }
  }

  return (io_features_enable || heap_features_enable);
}

void MatrixGlobal::early_init() {
  if (check_runtime_flags() == false) {
    return;
  }

  if (!os::Linux::ub_libs_ready()) {
    tty->print_cr("Load UB libraries failed, check please.");
    return;
  }

  // init ub env
  if (os::Linux::ub_prepare_env(PrintUBLog, UBLogPath) != 0) {
    tty->print_cr("Init UB env failed, check please.");
    return;
  }

  // init log file
  _log_file = tty;
  if (UBLogPath != NULL && strcmp(UBLogPath, OPTION_BLANK_VALUE) != 0) {
    fileStream* ub_log_file =
        new (ResourceObj::C_HEAP, mtInternal) fileStream(UBLogPath);
    if (ub_log_file != NULL) {
      if (ub_log_file->is_open()) {
        _log_file = ub_log_file;
      } else {
        delete ub_log_file;
      }
    }
  }

  _early_initialized = true;

  if (UseBorrowedMemory && MemTracker::tracking_level() != NMT_off) {
    float busy_ratio = BorrowedMemoryAllocationThreshold / 100.0f;
    size_t max_size = MaxOffheapBorrowedMemory >= 0 ? static_cast<size_t>(MaxOffheapBorrowedMemory) : SIZE_MAX;
    UBHeapMemory::dynamic_mem_init(busy_ratio, max_size);
  }
}

void MatrixGlobal::init() {
  ResourceMark rm;

  if (_early_initialized == false) {
    return;
  }

  // load conf file
  _allow_list_table = new AllowListTable();
  if (UBConfPath != NULL && strcmp(UBConfPath, OPTION_BLANK_VALUE) != 0) {
    FILE* ub_conf_file = fopen(UBConfPath, "r");
    int allow_method_count = 0;
    if (ub_conf_file != NULL) {
      UB_LOG("DEBUG", "Load UB conf file: %s\n", UBConfPath);
      char line[256];
      while (fgets(line, sizeof(line), ub_conf_file)) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0 || line[0] == '#') continue;
        char* dot = strrchr(line, '.');
        if (!dot) continue;
        *dot = '\0';
        Symbol* class_name =
            SymbolTable::lookup(line, (int)strlen(line), Thread::current());
        Symbol* method_name = SymbolTable::lookup(dot + 1, (int)strlen(dot + 1),
                                                  Thread::current());
        UB_LOG("DEBUG", "Load allow method: %s.%s\n", class_name->as_C_string(),
               method_name->as_C_string());
        allow_method_count++;
        _allow_list_table->add(class_name, method_name);
      }
      fclose(ub_conf_file);
    }
    if (allow_method_count == 0) {
      tty->print_cr("Load UB conf blank,check %s please.", UBConfPath);
    }
  }

  _initialized = true;

  // init ub socket
  if (UseUBSocket) {
    UBSocketManager::init();
  }

  // init ub mem file
  if (UseUBFile) {
    UBFileGlobal::init();
  }
}

void MatrixGlobal::log(const char* level, const char* format, ...) {
  char buf[32];
  _log_file->print("{%s}", os::iso8601_time(buf, sizeof(buf)));
  _log_file->print("{%" PRId64 "}[%p][%s] ", os::javaTimeNanos(),
                   JavaThread::current(), level);
  va_list ap;
  va_start(ap, format);
PRAGMA_DIAG_PUSH
PRAGMA_FORMAT_NONLITERAL_IGNORED_INTERNAL
  _log_file->vprint(format, ap);
PRAGMA_DIAG_POP
  va_end(ap);
}

bool MatrixGlobal::check_stack() {
  if (!_initialized) return false;

  JavaThread* jt = JavaThread::current();
  if (!jt->has_last_Java_frame()) return false;  // no Java frames

  ResourceMark rm;
  RegisterMap reg_map(jt);
  javaVFrame* jvf = jt->last_java_vframe(&reg_map);
  Method* last_method = jvf->method();
  int n = 0;
  while (jvf != NULL) {
    Method* method = jvf->method();
    if (_allow_list_table->contains(method->klass_name(), method->name())) {
      UB_LOG("DEBUG", "method match: %s.%s(loc %d),stack top %s.%s\n",
             method->klass_name()->as_C_string(), method->name()->as_C_string(),
             jvf->bci(), last_method->klass_name()->as_C_string(),
             last_method->name()->as_C_string());
      return true;
    }
    jvf = jvf->java_sender();
    n++;
    if (MaxJavaStackTraceDepth == n) break;
  }
  return false;
}

bool MatrixGlobal::print_stack() {
  ResourceMark rm;
  JavaThread* jt = JavaThread::current();
  if (!jt->has_last_Java_frame()) return false;  // no Java frames

  RegisterMap reg_map(jt);
  javaVFrame* jvf = jt->last_java_vframe(&reg_map);
  Method* last_method = jvf->method();
  int n = 0;
  tty->print_cr("[%p] Print Stack:", JavaThread::current());
  while (jvf != NULL) {
    Method* method = jvf->method();
    tty->print_cr("[%p] %s.%s(loc %d)", JavaThread::current(),
                  method->klass_name()->as_C_string(),
                  method->name_and_sig_as_C_string(), jvf->bci());
    jvf = jvf->java_sender();
    n++;
    if (MaxJavaStackTraceDepth == n) break;
  }
  return true;
}

void MatrixGlobal::before_exit() {
  if (!_initialized) return;
  if (UseUBSocket) {
    UBSocketManager::before_exit();
  }
  if (UseUBFile) {
    UBFileGlobal::before_exit();
  }

  if (UBHeapMemory::ub_dynamic_mem_enabled()) {
    UBHeapMemory::dynamic_mem_cleanup();
  }

  os::Linux::ub_finalize_env();
  if (_log_file != tty && _log_file != NULL) {
    _log_file->flush();
  }
}
