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
 */

#ifndef SHARE_VM_JPROFILECACHE_JITPROFILECACHEUTILS_HPP
#define SHARE_VM_JPROFILECACHE_JITPROFILECACHEUTILS_HPP

#include "classfile/classLoaderData.hpp"

class JitProfileCacheUtils : public AllStatic {
public:
    static Symbol* get_class_loader_name(ClassLoaderData* cld);
    static Symbol* remove_meaningless_suffix(Symbol* s);

    static bool commit_compilation(methodHandle m, int comp_level, int bci, TRAPS);

    static bool is_in_unpreloadable_classes_black_list(Symbol* s);
    static bool is_in_unpreloadable_classes_black_list(const char* str);
};

#endif //SHARE_VM_JPROFILECACHE_JITPROFILECACHEUTILS_HPP
