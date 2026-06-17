/*
 * Copyright (c) 2026, Huawei Technologies Co. Ltd. All rights reserved.
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
 * @test DynamicMaxHeapRuntimeMemoryTest
 * @summary Test Runtime max memory after dynamic max heap changes.
 * @library /testlibrary
 * @requires os.family=="linux"
 * @build DynamicMaxHeapRuntimeMemoryTest
 * @run main DynamicMaxHeapRuntimeMemoryTest
 */

import com.oracle.java.testlibrary.JDKToolFinder;
import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;

public class DynamicMaxHeapRuntimeMemoryTest {
    private static final String TARGET = "target";
    private static final String UNSUPPORTED = "DynamicMaxHeap feature are not available";
    private static final long M = 1024L * 1024L;
    private static final long ALLOC_SIZE = 64L * M;
    private static final long AFTER_RESIZE_MAX_BOUND = 288L;
    private static Object[] rootArray;

    public static void main(String[] args) throws Exception {
        if (args.length > 0 && TARGET.equals(args[0])) {
            runTarget();
            return;
        }

        if (!isDynamicMaxHeapSupported()) {
            System.out.println("Dynamic max heap is not supported on this machine, skip test.");
            return;
        }

        test("-XX:+UseParallelGC");
        test("-XX:+UseConcMarkSweepGC");
    }

    private static boolean isDynamicMaxHeapSupported() throws Exception {
        OutputAnalyzer output = ProcessTools.executeProcess(
                ProcessTools.createJavaProcessBuilder("-XX:+UseParallelGC",
                                                      "-Xmx512m",
                                                      "-XX:DynamicMaxHeapSizeLimit=1g",
                                                      "-version"));
        output.shouldHaveExitValue(0);
        return !output.getOutput().contains(UNSUPPORTED);
    }

    private static void test(String gc) throws Exception {
        OutputAnalyzer output = ProcessTools.executeProcess(
                ProcessTools.createJavaProcessBuilder(gc,
                                                      "-Xms50M",
                                                      "-Xmx512M",
                                                      "-XX:+ElasticMaxHeap",
                                                      "-XX:ElasticMaxHeapSize=1G",
                                                      "-Dtest.jdk=" + System.getProperty("test.jdk"),
                                                      "-Dcompile.jdk=" + System.getProperty("compile.jdk"),
                                                      DynamicMaxHeapRuntimeMemoryTest.class.getName(),
                                                      TARGET));
        System.out.println(output.getOutput());
        output.shouldHaveExitValue(0);
        output.shouldContain("GC.elastic_max_heap success");
        output.shouldContain("Runtime max memory changed");
    }

    private static void runTarget() throws Exception {
        Runtime runtime = Runtime.getRuntime();
        long pid = ProcessTools.getProcessId();

        long beforeMax = runtime.maxMemory() / M;
        long beforeTotal = runtime.totalMemory() / M;
        long beforeFree = runtime.freeMemory() / M;
        System.out.println("Before alloc -- Max: " + beforeMax +
                           "M, Total: " + beforeTotal +
                           "M, Free: " + beforeFree + "M");

        allocAndFree(ALLOC_SIZE);

        long afterAllocMax = runtime.maxMemory() / M;
        long afterAllocTotal = runtime.totalMemory() / M;
        long afterAllocFree = runtime.freeMemory() / M;
        System.out.println("After alloc -- Max: " + afterAllocMax +
                           "M, Total: " + afterAllocTotal +
                           "M, Free: " + afterAllocFree + "M");
        if (afterAllocMax < (ALLOC_SIZE / M) || afterAllocMax < afterAllocTotal || afterAllocMax <= afterAllocFree) {
            throw new RuntimeException("Unexpected Runtime memory before resize");
        }

        rootArray = null;
        resizeAndCheck(Long.toString(pid), "256M");

        long afterResizeMax = runtime.maxMemory() / M;
        long afterResizeTotal = runtime.totalMemory() / M;
        long afterResizeFree = runtime.freeMemory() / M;
        System.out.println("After resize -- Max: " + afterResizeMax +
                           "M, Total: " + afterResizeTotal +
                           "M, Free: " + afterResizeFree + "M");
        if (afterResizeMax >= afterAllocMax ||
            afterResizeMax > AFTER_RESIZE_MAX_BOUND ||
            afterResizeMax < afterResizeTotal ||
            afterResizeMax <= afterResizeFree) {
            throw new RuntimeException("Runtime max memory did not follow dynamic heap resize");
        }
        System.out.println("Runtime max memory changed");
    }

    private static void allocAndFree(long size) {
        int rootLen = (int)(size / 1024L);
        rootArray = new Object[rootLen];
        for (int i = 0; i < rootLen; i++) {
            rootArray[i] = new int[254];
        }
    }

    private static void resizeAndCheck(String pid, String size) throws Exception {
        ProcessBuilder jcmd = new ProcessBuilder(JDKToolFinder.getJDKTool("jcmd"),
                                                 pid,
                                                 "GC.elastic_max_heap",
                                                 size);
        OutputAnalyzer output = new OutputAnalyzer(jcmd.start());
        System.out.println(output.getOutput());
        output.shouldHaveExitValue(0);
        output.shouldContain("GC.elastic_max_heap success");
    }
}
