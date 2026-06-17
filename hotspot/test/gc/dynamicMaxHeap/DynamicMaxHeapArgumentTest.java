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
 * @test DynamicMaxHeapArgumentTest
 * @summary Test dynamic max heap argument validation.
 * @library /testlibrary
 * @requires os.family=="linux"
 * @build DynamicMaxHeapArgumentTest
 * @run main DynamicMaxHeapArgumentTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;

public class DynamicMaxHeapArgumentTest {
    private static final String UNSUPPORTED = "DynamicMaxHeap feature are not available";

    public static void main(String[] args) throws Exception {
        testUnsupportedPlatformCheck();

        if (!isDynamicMaxHeapSupported()) {
            System.out.println("Dynamic max heap is not supported on this machine, skip supported-only checks.");
            return;
        }

        testMissingXmx();
        testLimitNotLargerThanXmx();
        testFixedNewSize();
        testFixedMaxNewSize();
        testFixedOldSize();
        testUseAdaptiveGCBoundary();
        testNoAdaptiveSizePolicy();
        testUnsupportedGC();
        testElasticOptionName();
    }

    private static boolean isDynamicMaxHeapSupported() throws Exception {
        OutputAnalyzer output = runJava("-XX:+UseParallelGC",
                                        "-Xmx512m",
                                        "-XX:DynamicMaxHeapSizeLimit=1g");
        output.shouldHaveExitValue(0);
        return !output.getOutput().contains(UNSUPPORTED);
    }

    private static void testUnsupportedPlatformCheck() throws Exception {
        OutputAnalyzer output = runJava("-XX:+UseParallelGC",
                                        "-Xmx512m",
                                        "-XX:DynamicMaxHeapSizeLimit=1g");
        output.shouldHaveExitValue(0);
        String text = output.getOutput();
        if (text.contains(UNSUPPORTED)) {
            output.shouldMatch("can only be assigned on (Linux aarch64|HISI now)|can only used with ACC installed");
        }
    }

    private static void testMissingXmx() throws Exception {
        shouldWarn("should be used together with -Xmx/-XX:MaxHeapSize",
                   "-XX:+UseParallelGC",
                   "-XX:DynamicMaxHeapSizeLimit=1g");
    }

    private static void testLimitNotLargerThanXmx() throws Exception {
        shouldWarn("should be larger than -Xmx/-XX:MaxHeapSize",
                   "-XX:+UseParallelGC",
                   "-Xmx512m",
                   "-XX:DynamicMaxHeapSizeLimit=512m");
    }

    private static void testFixedNewSize() throws Exception {
        shouldWarn("can not be used with -XX:OldSize/-XX:NewSize/-XX:MaxNewSize",
                   "-XX:+UseParallelGC",
                   "-Xmx512m",
                   "-XX:DynamicMaxHeapSizeLimit=1g",
                   "-Xmn64m");
    }

    private static void testFixedMaxNewSize() throws Exception {
        shouldWarn("can not be used with -XX:OldSize/-XX:NewSize/-XX:MaxNewSize",
                   "-XX:+UseParallelGC",
                   "-Xmx512m",
                   "-XX:DynamicMaxHeapSizeLimit=1g",
                   "-XX:MaxNewSize=64m");
    }

    private static void testFixedOldSize() throws Exception {
        shouldWarn("can not be used with -XX:OldSize/-XX:NewSize/-XX:MaxNewSize",
                   "-XX:+UseParallelGC",
                   "-Xmx512m",
                   "-XX:DynamicMaxHeapSizeLimit=1g",
                   "-XX:OldSize=128m");
    }

    private static void testUseAdaptiveGCBoundary() throws Exception {
        shouldWarn("can not be used with -XX:+UseAdaptiveGCBoundary",
                   "-XX:+UseParallelGC",
                   "-Xmx512m",
                   "-XX:DynamicMaxHeapSizeLimit=1g",
                   "-XX:+UseAdaptiveGCBoundary");
    }

    private static void testNoAdaptiveSizePolicy() throws Exception {
        shouldWarn("should be used with -XX:+UseAdaptiveSizePolicy",
                   "-XX:+UseParallelGC",
                   "-Xmx512m",
                   "-XX:DynamicMaxHeapSizeLimit=1g",
                   "-XX:-UseAdaptiveSizePolicy");
    }

    private static void testUnsupportedGC() throws Exception {
        shouldWarn("should be used with -XX:+UseG1GC/-XX:+UseConcMarkSweepGC/-XX:+UseParallelGC now",
                   "-XX:+UseSerialGC",
                   "-Xmx512m",
                   "-XX:DynamicMaxHeapSizeLimit=1g");
    }

    private static void testElasticOptionName() throws Exception {
        OutputAnalyzer output = runJava("-XX:+UseParallelGC",
                                        "-Xmx512m",
                                        "-XX:+ElasticMaxHeap",
                                        "-XX:ElasticMaxHeapSize=512m");
        output.shouldHaveExitValue(0);
        output.shouldContain("ElasticMaxHeap feature are not available");
        output.shouldContain("should be larger than -Xmx/-XX:MaxHeapSize");
    }

    private static void shouldWarn(String expected, String... vmArgs) throws Exception {
        OutputAnalyzer output = runJava(vmArgs);
        output.shouldHaveExitValue(0);
        output.shouldContain(UNSUPPORTED);
        output.shouldContain(expected);
    }

    private static OutputAnalyzer runJava(String... vmArgs) throws Exception {
        String[] args = new String[vmArgs.length + 1];
        System.arraycopy(vmArgs, 0, args, 0, vmArgs.length);
        args[vmArgs.length] = "-version";
        return ProcessTools.executeProcess(ProcessTools.createJavaProcessBuilder(args));
    }
}
