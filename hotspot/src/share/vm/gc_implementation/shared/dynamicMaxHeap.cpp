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
#include "precompiled.hpp"
#include "dynamicMaxHeap.hpp"

size_t DynamicMaxHeapConfig::_initial_max_heap_size = 0;

VM_ChangeMaxHeapOp::VM_ChangeMaxHeapOp(size_t new_max_heap) :
  VM_GC_Operation(0, GCCause::_change_max_heap, 0, true) {
  _new_max_heap = new_max_heap;
  _resize_success = false;
}

bool VM_ChangeMaxHeapOp::skip_operation() const {
  return false;
}

/*
 * validity check
 * new current max heap must be:
 * 1. >= min_heap_byte_size
 * 2. <= max_heap_byte_size
 * 3. not equal with current_max_heap_size
 */
bool CollectedHeap::check_new_max_heap_validity(size_t new_size, outputStream* st) {
  if (new_size > collector_policy()->max_heap_byte_size_limit()) {
    st->print_cr(/* GC.change_max_heap/GC.elastic_max_heap */"%s " SIZE_FORMAT "K exceeds maximum limit " SIZE_FORMAT "K",
                 Universe::dynamic_max_heap_dcmd_name(),
                 (new_size / K),
                 (collector_policy()->max_heap_byte_size_limit() / K));
    return false;
  }
  if (new_size < collector_policy()->min_heap_byte_size()) {
    st->print_cr(/* GC.change_max_heap/GC.elastic_max_heap */"%s " SIZE_FORMAT "K below minimum limit " SIZE_FORMAT "K",
                 Universe::dynamic_max_heap_dcmd_name(),
                 (new_size / K),
                 (collector_policy()->min_heap_byte_size() / K));
    return false;
  }
  // don`t print log if it is init shrink triggered by DynamicMaxHeapSizeLimit
  if (new_size == current_max_heap_size()) {
    st->print_cr(/* GC.change_max_heap/GC.elastic_max_heap */"%s " SIZE_FORMAT "K same with current max heap size " SIZE_FORMAT "K",
                 Universe::dynamic_max_heap_dcmd_name(),
                 (new_size / K),
                 (current_max_heap_size() / K));
    return false;
  }
  return true;
}

/*
  common check for Dynamic Max Heap
  1. only linux aarch hisi
  2. can not fix new/old size, but need to fix ratio of new/size(UseAdaptiveGCBoundary must be false)
  3. must support UseAdaptiveSizePolicy, otherwise all size fixed
  4. only G1/PS/CMS GC implemented now
 */
bool DynamicMaxHeapChecker::common_check() {
  if (!FLAG_IS_CMDLINE(DynamicMaxHeapSizeLimit) &&
      !FLAG_IS_CMDLINE(ElasticMaxHeapSize) &&
      !ElasticMaxHeap) {
    return false;
  }
  if ((FLAG_IS_CMDLINE(DynamicMaxHeapSizeLimit) || FLAG_IS_CMDLINE(ElasticMaxHeapSize)) &&
      !FLAG_IS_CMDLINE(MaxHeapSize)) {
    warning_and_disable(/* -XX:DynamicMaxHeapSizeLimit/-XX:ElasticMaxHeapSize */"should be used together with -Xmx/-XX:MaxHeapSize");
    return false;
  }
#if !defined(LINUX) || !defined(AARCH64)
  warning_and_disable(/* -XX:DynamicMaxHeapSizeLimit/-XX:ElasticMaxHeapSize */"can only be assigned on Linux aarch64");
  return false;
#endif
#ifdef AARCH64
  if (!VM_Version::is_hisi_enabled()) {
    warning_and_disable(/* -XX:DynamicMaxHeapSizeLimit/-XX:ElasticMaxHeapSize */"can only be assigned on HISI now");
    return false;
  }
#endif
  bool is_valid = false;
  size_t dummy_param = 0;
  os::Linux::dmh_g1_get_region_limit(dummy_param, dummy_param, is_valid, true);
  if (!is_valid) {
    warning_and_disable(/* -XX:DynamicMaxHeapSizeLimit/-XX:ElasticMaxHeapSize */"can only used with ACC installed");
    return false;
  }
  os::Linux::dmh_g1_can_shrink(dummy_param, dummy_param, dummy_param, dummy_param, is_valid, true);
  if (!is_valid) {
    warning_and_disable(/* -XX:DynamicMaxHeapSizeLimit/-XX:ElasticMaxHeapSize */"can only used with ACC installed");
    return false;
  }
  if (FLAG_IS_CMDLINE(OldSize) || FLAG_IS_CMDLINE(NewSize) || FLAG_IS_CMDLINE(MaxNewSize)) {
    warning_and_disable(/* -XX:DynamicMaxHeapSizeLimit/-XX:ElasticMaxHeapSize */"can not be used with -XX:OldSize/-XX:NewSize/-XX:MaxNewSize");
    return false;
  }
  if (UseAdaptiveGCBoundary) {
    warning_and_disable(/* -XX:DynamicMaxHeapSizeLimit/-XX:ElasticMaxHeapSize */"can not be used with -XX:+UseAdaptiveGCBoundary");
    return false;
  }
  if (!UseAdaptiveSizePolicy) {
    warning_and_disable(/* -XX:DynamicMaxHeapSizeLimit/-XX:ElasticMaxHeapSize */"should be used with -XX:+UseAdaptiveSizePolicy");
    return false;
  }
  if (!UseG1GC && !UseConcMarkSweepGC && !UseParallelGC) {
    warning_and_disable(/* -XX:DynamicMaxHeapSizeLimit/-XX:ElasticMaxHeapSize */"should be used with -XX:+UseG1GC/-XX:+UseConcMarkSweepGC/-XX:+UseParallelGC now");
    return false;
  }
  if (G1Uncommit) {
    warning_and_disable(/* -XX:DynamicMaxHeapSizeLimit/-XX:ElasticMaxHeapSize */"can not be used with -XX:+G1Uncommit");
    return false;
  }
  if ((FLAG_IS_CMDLINE(DynamicMaxHeapSizeLimit) || FLAG_IS_CMDLINE(ElasticMaxHeapSize)) &&
      DynamicMaxHeapSizeLimit <= MaxHeapSize) {
    warning_and_disable(/* -XX:DynamicMaxHeapSizeLimit/-XX:ElasticMaxHeapSize */"should be larger than -Xmx/-XX:MaxHeapSize");
    return false;
  }
  return true;
}

// DynamicMaxHeapSizeLimit should be used together with Xmx and larger than Xmx
bool DynamicMaxHeapChecker::check_dynamic_max_heap_size_limit() {
  Universe::set_dynamic_max_heap_size_limit_set_on_cmdline(FLAG_IS_CMDLINE(DynamicMaxHeapSizeLimit));
  Universe::set_elastic_max_heap_size_set_on_cmdline(FLAG_IS_CMDLINE(ElasticMaxHeapSize));
  if (TraceElasticMaxHeap) {
    FLAG_SET_ERGO(bool, TraceDynamicMaxHeap, true);
  }
  if (FLAG_IS_CMDLINE(ElasticMaxHeapSize)) {
    FLAG_SET_ERGO(uintx, DynamicMaxHeapSizeLimit, ElasticMaxHeapSize);
  }
  if (FLAG_IS_CMDLINE(ElasticMaxHeapShrinkMinFreeRatio)) {
    FLAG_SET_ERGO(uintx, DynamicMaxHeapShrinkMinFreeRatio, ElasticMaxHeapShrinkMinFreeRatio);
  }
  return common_check();
}

void DynamicMaxHeapChecker::warning_and_disable(const char *reason) {
  warning("%s feature are not available for reason -XX:%s %s, automatically disabled",
          Universe::dynamic_max_heap_option_name(),
          Universe::dynamic_max_heap_size_limit_option_name(),
          reason);
  FLAG_SET_DEFAULT(DynamicMaxHeapSizeLimit, ScaleForWordSize(DynamicMaxHeapChecker::_default_dynamic_max_heap_size_limit * M));
  Universe::set_dynamic_max_heap_enable(false);
}
