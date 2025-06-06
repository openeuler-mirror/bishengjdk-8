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

#ifndef SHARE_VM_JPROFILECACHE_JITPROFILECACHELOG_HPP
#define SHARE_VM_JPROFILECACHE_JITPROFILECACHELOG_HPP

/*
 * We use this file to be compatible with log framework in JDK11
 * To avoid conflict with jfr log, it's only used by jitProfileCache.cpp
 */

#include "runtime/os.hpp"
#include "utilities/debug.hpp"

#ifdef jprofilecache_log_error
#undef jprofilecache_log_error
#endif
#ifdef jprofilecache_log_warning
#undef jprofilecache_log_warning
#endif
#ifdef jprofilecache_log_info
#undef jprofilecache_log_info
#endif
#ifdef jprofilecache_log_debug
#undef jprofilecache_log_debug
#endif
#ifdef jprofilecache_log_trace
#undef jprofilecache_log_trace
#endif
#ifdef jprofilecache_log_enabled
#undef jprofilecache_log_enabled
#endif

#define jprofilecache_log_error(...)     (!log_is_enabled(Error, __VA_ARGS__))    ? (void)0 : tty->print_cr
#define jprofilecache_log_warning(...)   (!log_is_enabled(Warning, __VA_ARGS__))  ? (void)0 : tty->print_cr
#define jprofilecache_log_info(...)      (!log_is_enabled(Info, __VA_ARGS__))     ? (void)0 : tty->print_cr
#define jprofilecache_log_debug(...)     (!log_is_enabled(Debug, __VA_ARGS__))    ? (void)0 : tty->print_cr
#define jprofilecache_log_trace(...)     (!log_is_enabled(Trace, __VA_ARGS__))    ? (void)0 : tty->print_cr


#define log_is_enabled(level, ...) (JitProfileCacheLog::is_level(LogLevel::level))

class LogLevel : public AllStatic {
public:
    enum type {
        Trace,
        Debug,
        Info,
        Warning,
        Error,
        Off
    };
    static int LogLevelNum;
};
typedef LogLevel::type LogLevelType;

class JitProfileCacheLog {
public:
    static bool is_level(LogLevelType level) {
        if (level >= LogLevel::LogLevelNum) {
            return true;
        }

        return false;
    }
};

#endif