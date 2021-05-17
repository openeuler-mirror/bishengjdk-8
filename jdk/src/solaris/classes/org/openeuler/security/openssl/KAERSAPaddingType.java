/*
 * Copyright (c) 2021, Huawei Technologies Co., Ltd. All rights reserved.
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

package org.openeuler.security.openssl;

import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

enum KAERSAPaddingType {
    // raw RSA
    PKCS1Padding(1, "PKCS1Padding"),

    // PKCS#1 v1.5 RSA
    NoPadding(3, "NoPadding"),

    // PKCS#2 v2.2 OAEP with MGF1
    OAEP(4, "OAEP", new HashSet<>(
            Arrays.asList(
                    "OAEPPADDING",
                    "OAEPWITHMD5ANDMGF1PADDING",
                    "OAEPWITHSHA1ANDMGF1PADDING",
                    "OAEPWITHMD5ANDMGF1PADDING",
                    "OAEPWITHSHA1ANDMGF1PADDING",
                    "OAEPWITHSHA-1ANDMGF1PADDING",
                    "OAEPWITHSHA-224ANDMGF1PADDING",
                    "OAEPWITHSHA-256ANDMGF1PADDING",
                    "OAEPWITHSHA-384ANDMGF1PADDING",
                    "OAEPWITHSHA-512ANDMGF1PADDING",
                    "OAEPWITHSHA-512/224ANDMGF1PADDING",
                    "OAEPWITHSHA-512/256ANDMGF1PADDING"))
    ),

    // PSS
    PKCS1PssPadding(6, "RSA_PKCS1_PSS_PADDING");

    private final int id;
    private final String name;
    private final Set<String> supportPaddings;

    public int getId() {
        return id;
    }

    public String getName() {
        return name;
    }

    KAERSAPaddingType(int id, String name) {
        this(id, name, Collections.singleton(name));
    }

    KAERSAPaddingType(int id, String name, Set<String> supportPaddings) {
        this.id = id;
        this.name = name;
        this.supportPaddings = supportPaddings;
    }

    public Set<String> getSupportPaddings() {
        return supportPaddings;
    }
}
