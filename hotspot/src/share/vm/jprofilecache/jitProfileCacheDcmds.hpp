/*
 * Copyright (c) 2025, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2019 Alibaba Group Holding Limited. All Rights Reserved.
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

#ifndef SHARE_JPROFILECACHE_JITPROFILECACHEDCMDS_HPP
#define SHARE_JPROFILECACHE_JITPROFILECACHEDCMDS_HPP

#include "services/diagnosticCommand.hpp"

class JitProfileCacheDCmds : public DCmdWithParser  {
public:
    JitProfileCacheDCmds(outputStream* output, bool heap_allocated);
    static const char* name() {
      return "JProfilecache";
    }
    static const char* description() {
      return "JProfilecache command. ";
    }
    static int num_arguments();
    virtual void execute(DCmdSource source, TRAPS);
protected:
    DCmdArgument<bool> _notify_precompile;
    DCmdArgument<bool> _check_compile_finished;
    DCmdArgument<bool> _deoptimize_compilation;
    DCmdArgument<bool> _help;
    void print_help_info();
    void execute_trigger_precompilation(instanceKlassHandle profilecacheClass, outputStream* output, Thread* THREAD);
    void execute_checkCompilation_finished(instanceKlassHandle profilecacheClass, outputStream* output, Thread* THREAD);
    void execute_notifyDeopt_profileCache(instanceKlassHandle profilecacheClass, outputStream* output, Thread* THREAD);
    bool checkAndHandlePendingExceptions(outputStream* out, Thread* THREAD);
};

#endif // SHARE_JPROFILECACHE_JITPROFILECACHEDCMDS_HPP