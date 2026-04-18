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

#include "matrix/matrixUtils.hpp"

enum UBFeature {
  UB_FILE,
  UB_SOCKET,
  UB_HEAP,
  UB_FEATURE_COUNT
};

class MatrixGlobal : public AllStatic {
 public:
  static bool initialized() { return _initialized; }
  static void init();
  static void init_features();
  static void before_exit();
  static bool check_stack(UBFeature feature);
  static bool print_stack();

 private:
  static bool _initialized;
};

#endif  // SHARE_VM_MATRIX_MATRIXMANAGER_HPP