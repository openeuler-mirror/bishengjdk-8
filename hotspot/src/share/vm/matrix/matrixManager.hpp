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

#ifndef SHARE_VM_MATRIX_MATRIXMANAGER_HPP
#define SHARE_VM_MATRIX_MATRIXMANAGER_HPP

#include <sys/resource.h>

#include "matrix/matrixAllowList.hpp"
#include "matrix/matrixUtils.hpp"

#define UB_LOG(level, fmt, ...)                    \
  if (strcmp(level, "ERROR") == 0 || PrintUBLog) { \
    ResourceMark rm;                               \
    MatrixGlobal::log(level, fmt, ##__VA_ARGS__);  \
  }

class MatrixGlobal : public AllStatic {
 public:
  static bool initialized() { return _initialized; }
  static void early_init();
  static void init();
  static void before_exit();
  static bool check_stack();
  static bool print_stack();
  static void log(const char* level, const char* format, ...);

 private:
  static bool _enabled;
  static bool _early_initialized;
  static bool _initialized;
  static outputStream* _log_file;
  static AllowListTable* _allow_list_table;
};

#endif  // SHARE_VM_MATRIX_MATRIXMANAGER_HPP