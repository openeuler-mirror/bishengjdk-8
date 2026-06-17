/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Copyright (C) 2023, 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

/*
 * @test RuntimeMemoryTest
 * @summary Test java.lang.Runtime max memory and total memory
 * @library /testlibrary
 * @requires os.family=="linux"
 * @requires os.arch == "aarch64"
 * @build TestBase
 * @run main/othervm -Xms50M -Xmx2G -XX:+ElasticMaxHeap -XX:+UseParallelGC RuntimeMemoryTest
 * @run main/othervm -Xms50M -Xmx2G -XX:+ElasticMaxHeap -XX:+UseG1GC RuntimeMemoryTest
 */

import com.oracle.java.testlibrary.Asserts;
import com.oracle.java.testlibrary.ProcessTools;

public class RuntimeMemoryTest extends TestBase {
    static Object[] rootArray;
    static final long M = 1024L * 1024L;

    public static void main(String[] args) throws Exception {
        long pid = ProcessTools.getProcessId();
        Runtime runtime = Runtime.getRuntime();
        long max = runtime.maxMemory() / M;
        long total = runtime.totalMemory() / M;
        long free = runtime.freeMemory() / M;
        System.out.println("Before alloc -- Max: " + max + "M, Total: " + total + "M, Free: " + free + "M");

        allocAndFree(1024L * 1024L * 1024L);
        max = runtime.maxMemory() / M;
        total = runtime.totalMemory() / M;
        free = runtime.freeMemory() / M;
        rootArray = null;
        System.out.println("After alloc -- Max: " + max + "M, Total: " + total + "M, Free: " + free + "M");
        Asserts.assertGT(max, 1024L);
        Asserts.assertGTE(max, total);
        Asserts.assertGT(max, free);

        resizeAndCheck(Long.toString(pid),
                       "100M",
                       new String[] {"GC.elastic_max_heap (", "GC.elastic_max_heap success"},
                       null);
        max = runtime.maxMemory() / M;
        total = runtime.totalMemory() / M;
        free = runtime.freeMemory() / M;
        System.out.println("After resize -- Max: " + max + "M, Total: " + total + "M, Free: " + free + "M");
        Asserts.assertLT(max, 129L);
        Asserts.assertGTE(max, total);
        Asserts.assertGT(max, free);
    }

    static void allocAndFree(long size) {
        int rootLen = (int)(size / 1024L);
        rootArray = new Object[rootLen];
        for (int i = 0; i < rootLen; i++) {
            rootArray[i] = new int[254];
        }
    }
}
