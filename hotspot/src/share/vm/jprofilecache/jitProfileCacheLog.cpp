/*
 * Copyright (c) 2025, Huawei Technologies Co., Ltd. All rights reserved.
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

#include "jprofilecache/jitProfileCacheLog.hpp"

#include <stdarg.h>

// Match unified logging defaults: warning/error are visible without explicit log configuration.
int LogLevel::LogLevelNum = LogLevel::Warning;

static const char* jprofilecache_level_name(LogLevelType level) {
  switch (level) {
    case LogLevel::Trace:
      return "trace";
    case LogLevel::Debug:
      return "debug";
    case LogLevel::Info:
      return "info";
    case LogLevel::Warning:
      return "warning";
    case LogLevel::Error:
      return "error";
    default:
      return "off";
  }
}

void JitProfileCacheLog::print(LogLevelType level, const char* format, ...) {
  tty->print("[%.3fs][%s][jprofilecache] ", os::elapsedTime(), jprofilecache_level_name(level));

  va_list ap;
  va_start(ap, format);
  tty->vprint_cr(format, ap);
  va_end(ap);
}
