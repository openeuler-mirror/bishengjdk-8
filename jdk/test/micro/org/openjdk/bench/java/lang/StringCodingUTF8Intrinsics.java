/*
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
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

package org.openjdk.bench.java.lang;

import org.openjdk.jmh.annotations.Benchmark;
import org.openjdk.jmh.annotations.BenchmarkMode;
import org.openjdk.jmh.annotations.Measurement;
import org.openjdk.jmh.annotations.Mode;
import org.openjdk.jmh.annotations.Threads;
import org.openjdk.jmh.annotations.Warmup;

@BenchmarkMode(Mode.AverageTime)
@Warmup(iterations = 3)
@Measurement(iterations = 5, time = 5)
@Threads(1)
public class StringCodingUTF8Intrinsics {

    private static int count = 100000;

    private static byte[][] ss = {
            "dqwqdwqwqdw".getBytes(),
            "\u4e86\u8fd9\u7bc7\u6587\u7ae0\uff1a".getBytes(),
            "[  ]hzzone:\u4e5f\u5c31\u5784\u65ad\u5e73\u53f0\u603b\u90e8\u5de5\u8d44\u8ddf\u8001\u4eba\u9000\u4f11\u91d1\u6da8\u5176\u4ed6\u6536\u5165\u5927\u591a\u4e0b\u8dcc\u6216\u5931\u4e1a\u5f52\u96f6(2026-02-06 07:20)\n".getBytes(),
            "[-5]voTvo:\u524d\u63d0\u5c31\u8bf4\u9519\u4e86\uff0c\u6536\u5165\u8d8a\u6765\u8d8a\u4f4e\u597d\u5417(2026-02-06 10:16)\n".getBytes(),
            "[-5]shocker:\u8d22\u5bcc\u96c6\u4e2d\u548c\u5b8f\u5927\u53d9\u4e8b\uff0c\u548c\u4e07\u5343\u666e\u901a\u5bb6\u5ead\u611f\u53d7\u76f8\u77db\u76fe\u3002(2026-02-06 10:22)\n".getBytes(),
            "[-5]yourcarin0:\u9884\u5236\u83dc\u4e8b\u4ef6\u5fd8\u4e86\uff1f(2026-02-06 11:14)\n".getBytes(),
            "[+5]whatswrong:\u5355\u6b21\u5355\u4eba\u8d39\u7528\u6da8\u4e86\uff0c\u4e0b\u9986\u5b50\u9891\u7387\u5927\u5e45\u964d\u4f4e(2026-02-06 14:13)".getBytes()
    };

    @Benchmark
    public static void TT() {
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < ss.length; j++) {
                String s = new String(ss[j]);
                byte[] df = s.getBytes();
                df[df.length - 1] = df[df.length - 2];
            }
        }
    }

    public static void main(String[] args) {
        count = 1;
        TT();
    }
}
