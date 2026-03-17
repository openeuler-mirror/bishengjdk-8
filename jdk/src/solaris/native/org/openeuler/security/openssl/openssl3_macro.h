/*
 * Copyright (c) 2024, Huawei Technologies Co., Ltd. All rights reserved.
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

#ifndef OPENSSL3_MACRO_H
#define OPENSSL3_MACRO_H

#define SSL3_ERR_LIB_SYS 2
#define SSL3_ERR_LIB_OFFSET 23L
#define SSL3_ERR_LIB_MASK 0xFF
#define SSL3_INT_MAX __INT_MAX__
#define SSL3_ERR_SYSTEM_FLAG ((unsigned int)SSL3_INT_MAX + 1)
#define SSL3_ERR_SYSTEM_MASK ((unsigned int)SSL3_INT_MAX)
#define SSL3_ERR_REASON_MASK 0x7FFFFF

#define SSL3_ERR_SYSTEM_ERROR(errcode) (((errcode)&SSL3_ERR_SYSTEM_FLAG) != 0)

#endif // OPENSSL3_MACRO_H