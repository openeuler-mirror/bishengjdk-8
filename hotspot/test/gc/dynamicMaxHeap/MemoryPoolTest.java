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
 * @test MemoryPoolTest
 * @summary Test MemoryPool MemoryUsage returns correct max size
 * @library /testlibrary
 * @requires os.family=="linux"
 * @requires os.arch == "aarch64"
 * @build TestBase
 * @run main/othervm -Xms50M -Xmx2G -XX:+ElasticMaxHeap -XX:+UseParallelGC -XX:+TraceElasticMaxHeap MemoryPoolTest
 * @run main/othervm -Xms50M -Xmx2G -XX:+ElasticMaxHeap -XX:+UseG1GC -XX:+TraceElasticMaxHeap MemoryPoolTest
 */

import com.oracle.java.testlibrary.Asserts;
import com.oracle.java.testlibrary.ProcessTools;

import java.lang.management.ManagementFactory;
import java.lang.management.MemoryMXBean;
import java.lang.management.MemoryPoolMXBean;
import java.lang.management.MemoryUsage;
import java.util.List;

public class MemoryPoolTest extends TestBase {
    static Object[] rootArray;
    static final long M = 1024L * 1024L;
    static long edenMaxSize;
    static long survivorMaxSize;
    static long oldGenMaxSize;

    public static void main(String[] args) throws Exception {
        long pid = ProcessTools.getProcessId();

        allocAndFree(1024L * 1024L * 1024L);
        MemoryMXBean mem = ManagementFactory.getMemoryMXBean();
        MemoryUsage usage = mem.getHeapMemoryUsage();
        long max = usage.getMax() / M;
        long committed = usage.getCommitted() / M;
        long used = usage.getUsed() / M;
        System.out.println("After alloc -- Heap Max: " + max +
                           "M, Committed: " + committed +
                           "M, Used: " + used + "M");
        Asserts.assertGT(max, 1024L);
        Asserts.assertGTE(max, committed);
        Asserts.assertGTE(committed, used);

        getMemoryInfo();
        System.out.println("After alloc -- Eden Max: " + edenMaxSize +
                           "M, Survivor Max: " + survivorMaxSize +
                           "M, Old Max: " + oldGenMaxSize + "M");
        long origOld = oldGenMaxSize;
        long origEden = edenMaxSize;
        Asserts.assertGT(edenMaxSize + survivorMaxSize + oldGenMaxSize, 1024L);
        Asserts.assertGTE(max, oldGenMaxSize);
        Asserts.assertGTE(oldGenMaxSize, edenMaxSize);
        Asserts.assertGTE(oldGenMaxSize, survivorMaxSize);

        rootArray = null;
        resizeAndCheck(Long.toString(pid),
                       "100M",
                       new String[] {"GC.elastic_max_heap (", "GC.elastic_max_heap success"},
                       null);
        usage = mem.getHeapMemoryUsage();
        max = usage.getMax() / M;
        committed = usage.getCommitted() / M;
        used = usage.getUsed() / M;
        System.out.println("After resize -- Heap Max: " + max +
                           "M, Committed: " + committed +
                           "M, Used: " + used + "M");
        Asserts.assertLTE(max, 128L);
        Asserts.assertGTE(max, committed);
        Asserts.assertGTE(committed, used);

        getMemoryInfo();
        System.out.println("After resize -- Eden Max: " + edenMaxSize +
                           "M, Survivor Max: " + survivorMaxSize +
                           "M, Old Max: " + oldGenMaxSize + "M");
        Asserts.assertLT(oldGenMaxSize, origOld);
        Asserts.assertLTE(edenMaxSize, origEden);
    }

    static void allocAndFree(long size) {
        int rootLen = (int)(size / 1024L);
        rootArray = new Object[rootLen];
        for (int i = 0; i < rootLen; i++) {
            rootArray[i] = new int[254];
        }
    }

    static void getMemoryInfo() {
        List<MemoryPoolMXBean> pools = ManagementFactory.getMemoryPoolMXBeans();
        for (MemoryPoolMXBean pool : pools) {
            String name = pool.getName();
            MemoryUsage usage = pool.getUsage();
            long maxSize = usage.getMax() / M;
            if (name.contains("Eden")) {
                edenMaxSize = maxSize;
            } else if (name.contains("Survivor")) {
                survivorMaxSize = maxSize;
            } else if (name.contains("Old")) {
                oldGenMaxSize = maxSize;
            }
        }
    }
}
