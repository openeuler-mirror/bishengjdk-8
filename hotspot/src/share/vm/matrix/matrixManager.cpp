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

#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocket.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"

bool MatrixGlobal::_initialized = false;

void MatrixGlobal::init() {
  if (!UseUBSocket) {
    UBSocketManager::check_options();
    return;
  }

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
}

void MatrixGlobal::init_features() {
  if (!_initialized) { return; }

  if (UseUBSocket) {
    UBSocketManager::init();
  }
}

bool MatrixGlobal::check_stack(UBFeature feature) {
  if (!_initialized) return false;
  switch (feature)
  {
  case UB_SOCKET:
    return UBSocketManager::check_stack();
  default:
    return false;
  }
}

void MatrixGlobal::before_exit() {
  // now for performance analysis
  UBSocketProfiler::print_summary();
  
  if (!_initialized) { return; }
  if (UseUBSocket) {
    UBSocketManager::before_exit();
  }
  os::Linux::ub_finalize_env();
  MatrixLog::flush();
}
