/*
 * Copyright (c) 2016, 2017, Red Hat, Inc. All rights reserved.
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

#ifndef SHARE_VM_GC_SHENANDOAH_SHENANDOAHLOGGING_HPP
#define SHARE_VM_GC_SHENANDOAH_SHENANDOAHLOGGING_HPP

#include <stdio.h> // for va_list and friends
#include <stdarg.h>

class ShenandoahLogger {
public:
  static void handle_trace(const char* format, ...);
  static void handle_debug(const char* format, ...);
  static void handle_info(const char* format, ...);
  static void handle_warning(const char* format, ...);
private:
  static void handle_generic(const char* format, va_list ap);
};

#define log_trace(...)   if (ShenandoahLogTrace)   ShenandoahLogger::handle_trace
#define log_debug(...)   if (ShenandoahLogDebug)   ShenandoahLogger::handle_debug
#define log_warning(...) if (ShenandoahLogWarning) ShenandoahLogger::handle_warning

// With ShenandoahLogInfo, only print out the single-"gc"-tag messages.
#define log_info(...)    if (((strcmp(#__VA_ARGS__, "gc") == 0) && (ShenandoahLogInfo  || PrintGC || PrintGCDetails)) || \
                             ((strcmp(#__VA_ARGS__, "gc") > 0)  && (ShenandoahLogInfo  || PrintGCDetails)) || \
                             ShenandoahLogDebug)  \
                                ShenandoahLogger::handle_info

#ifndef PRODUCT
#define log_develop_trace(...) if (ShenandoahLogTrace) ShenandoahLogger::handle_trace
#define log_develop_debug(...) if (ShenandoahLogDebug) ShenandoahLogger::handle_debug
#else
#define DUMMY_ARGUMENT_CONSUMER(...)
#define log_develop_trace(...) DUMMY_ARGUMENT_CONSUMER
#define log_develop_debug(...) DUMMY_ARGUMENT_CONSUMER
#endif

#endif
