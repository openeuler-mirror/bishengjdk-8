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

#include "matrix/matrixAllowList.hpp"
#include "matrix/matrixLog.hpp"
#include "matrix/ubHeapMemory.hpp"
#include "matrix/ubFile.hpp"
#include "matrix/ubSocket/ubSocket.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/thread.hpp"
#include "runtime/vframe.hpp"

bool MatrixGlobal::_initialized = false;

void MatrixGlobal::init() {
  bool enabled = UseUBFile || UseUBSocket || UseBorrowedMemory;
  if (!enabled) { return; }

  if (!os::Linux::ub_libs_ready()) {
    tty->print_cr("Load UB libraries failed, check please.");
    return;
  }

  // init log
  MatrixLog::init();

  // init ub env
  if (os::Linux::ub_prepare_env(MatrixLog::min_log_level(), MatrixLog::log_path()) != 0) {
    tty->print_cr("Init UB env failed, check please.");
    return;
  }

  _initialized = true;

  if (UseBorrowedMemory && MemTracker::tracking_level() != NMT_off) {
    float busy_ratio = BorrowedMemoryAllocationThreshold / 100.0f;
    size_t max_size = MaxOffheapBorrowedMemory >= 0 ? static_cast<size_t>(MaxOffheapBorrowedMemory) : SIZE_MAX;
    UBHeapMemory::dynamic_mem_init(busy_ratio, max_size);
  }
}

void MatrixGlobal::init_features() {
  ResourceMark rm;

  if (!_initialized) { return; }

  // init ub socket
  if (UseUBSocket) {
    UBSocketManager::init();
  }

  // init ub mem file
  if (UseUBFile) {
    UBFileGlobal::init();
  }
}

bool MatrixGlobal::check_stack(UBFeature feature) {
  if (!_initialized) return false;
  switch (feature)
  {
  case UB_FILE:
    return UBFileGlobal::check_stack();
  case UB_SOCKET:
    return UBSocketManager::check_stack();
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
  MatrixLog::flush();
}
