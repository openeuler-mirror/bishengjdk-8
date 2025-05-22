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
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_VM_GC_IMPLEMENTATION_SHARED_DYNAMIC_MAX_HEAP_OPERATION_HPP
#define SHARE_VM_GC_IMPLEMENTATION_SHARED_DYNAMIC_MAX_HEAP_OPERATION_HPP

#include "utilities/defaultStream.hpp"
#include "gc_implementation/shared/vmGCOperations.hpp"

#define DMH_LOG(fmt, ...)                           \
  if (TraceDynamicMaxHeap)  {                       \
    ResourceMark rm;                                \
    tty->print_cr(fmt, ##__VA_ARGS__);              \
  }

class VM_ChangeMaxHeapOp : public VM_GC_Operation {
public:
  VM_ChangeMaxHeapOp(size_t new_max_heap);
  VMOp_Type type() const {
    return VMOp_DynamicMaxHeap;
  }
  bool resize_success() const {
    return _resize_success;
  }
protected:
  size_t _new_max_heap;
  bool   _resize_success;
private:
  bool skip_operation() const;
};

class DynamicMaxHeapChecker : AllStatic {
public:
  static void common_check();
  static bool check_dynamic_max_heap_size_limit();
  static void warning_and_disable(const char *reason);
private:
  static const int _default_dynamic_max_heap_size_limit = 96;
};

class DynamicMaxHeapConfig : AllStatic {
public:
  static size_t initial_max_heap_size() { return _initial_max_heap_size; }
  static void set_initial_max_heap_size(size_t new_size) {
    _initial_max_heap_size = new_size;
  }
private:
  static size_t _initial_max_heap_size;
};
#endif // SHARE_VM_GC_IMPLEMENTATION_SHARED_DYNAMIC_MAX_HEAP_OPERATION_HPP
