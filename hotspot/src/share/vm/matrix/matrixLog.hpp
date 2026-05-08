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

#ifndef SHARE_VM_MATRIX_MATRIXLOG_HPP
#define SHARE_VM_MATRIX_MATRIXLOG_HPP

#include "matrix/matrixManager.hpp"

enum UBLogLevel {
  UB_LOG_DEBUG = 0,
  UB_LOG_INFO = 1,
  UB_LOG_WARNING = 2,
  UB_LOG_ERROR = 3
};

class MatrixLog : public AllStatic {
 public:
  static bool initialized() { return _initialized; }
  static void init();
  static bool should_log(UBFeature feature, UBLogLevel level);
  static void log(UBFeature feature, UBLogLevel level, const char* format, ...);
  static int min_log_level() { return _min_log_level; }
  static const char* log_path() { return _default_log_path; }
  static bool enabled(UBFeature feature);
  static outputStream* stream(UBFeature feature);
  static void flush();

 private:
  static bool _initialized;
  static int _log_levels[UB_FEATURE_COUNT];
  static int _min_log_level;
  static outputStream* _default_log_file;
  static outputStream* _log_files[UB_FEATURE_COUNT];
  static char* _default_log_path;
  static char* _log_paths[UB_FEATURE_COUNT];

  static void apply_selector(const char* start, const char* end);
  static void set_default_log_path(const char* start, const char* end);
  static void set_log_path(UBFeature feature, const char* start, const char* end);
  static outputStream* log_file(UBFeature feature);
};

#define UB_LOG(feature, level, fmt, ...)                    \
  do {                                                      \
    if (MatrixLog::should_log(feature, level)) {            \
      ResourceMark rm;                                      \
      MatrixLog::log(feature, level, fmt, ##__VA_ARGS__);   \
    }                                                       \
  } while (0)

#endif  // SHARE_VM_MATRIX_MATRIXLOG_HPP
