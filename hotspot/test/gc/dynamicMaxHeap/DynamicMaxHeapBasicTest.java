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
 * @test DynamicMaxHeapBasicTest
 * @summary Test basic elastic max heap resizing cases.
 * @library /testlibrary
 * @requires os.family=="linux"
 * @build DynamicMaxHeapBasicTest
 * @run main DynamicMaxHeapBasicTest
 */

import com.oracle.java.testlibrary.JDKToolFinder;
import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.lang.management.ManagementFactory;
import java.util.concurrent.TimeUnit;

public class DynamicMaxHeapBasicTest {
    private static final String TARGET = "target";
    private static final String READY_PREFIX = "DynamicMaxHeapBasicTest.pid=";
    private static final String UNSUPPORTED = "DynamicMaxHeap feature are not available";

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
        ProcessBuilder targetBuilder =
                ProcessTools.createJavaProcessBuilder(gc,
                                                      "-XX:+UsePerfData",
                                                      "-XX:+ElasticMaxHeap",
                                                      "-Xms104857599",
                                                      "-Xmx629145599",
                                                      "-XX:ElasticMaxHeapSize=1073741823",
                                                      DynamicMaxHeapBasicTest.class.getName(),
                                                      TARGET);
        targetBuilder.redirectErrorStream(true);

        Process target = targetBuilder.start();
        StringBuilder targetOutput = new StringBuilder();
        BufferedReader reader = new BufferedReader(new InputStreamReader(target.getInputStream()));

        try {
            String pid = waitForTargetPid(reader, targetOutput);

            resizeAndCheck(pid, "500M", "GC.elastic_max_heap success", null);
            resizeAndCheck(pid, "800M", "GC.elastic_max_heap success", null);
            resizeAndCheck(pid, "2G", "GC.elastic_max_heap fail", "exceeds maximum limit");
            resizeAndCheck(pid, "1G", "GC.elastic_max_heap success", null);
            resizeAndCheck(pid, "314572799", "GC.elastic_max_heap success", null);
        } finally {
            target.destroy();
            if (!target.waitFor(10, TimeUnit.SECONDS)) {
                target.destroyForcibly();
                target.waitFor();
            }
            drain(reader, targetOutput);
            System.out.println(targetOutput.toString());
        }
    }

    private static void resizeAndCheck(String pid,
                                       String size,
                                       String expected,
                                       String optionalExpected) throws Exception {
        OutputAnalyzer output = runJcmd(pid, size);
        System.out.println(output.getOutput());
        output.shouldHaveExitValue(0);
        output.shouldContain(expected);
        output.shouldContain("GC.elastic_max_heap");
        if (optionalExpected != null) {
            output.shouldContain(optionalExpected);
        }
    }

    private static OutputAnalyzer runJcmd(String pid, String size) throws Exception {
        ProcessBuilder jcmd = new ProcessBuilder(JDKToolFinder.getJDKTool("jcmd"),
                                                 pid,
                                                 "GC.elastic_max_heap",
                                                 size);
        return new OutputAnalyzer(jcmd.start());
    }

    private static String waitForTargetPid(BufferedReader reader,
                                           StringBuilder targetOutput) throws Exception {
        long deadline = System.currentTimeMillis() + 30000;
        String line;
        while (System.currentTimeMillis() < deadline && (line = reader.readLine()) != null) {
            targetOutput.append(line).append('\n');
            if (line.startsWith(READY_PREFIX)) {
                return line.substring(READY_PREFIX.length());
            }
        }
        throw new RuntimeException("Target VM did not report its pid. Output:\n" + targetOutput);
    }

    private static void drain(BufferedReader reader, StringBuilder targetOutput) throws Exception {
        try {
            String line;
            while ((line = reader.readLine()) != null) {
                targetOutput.append(line).append('\n');
            }
        } catch (IOException e) {
            // The target process may close stdout while it is being destroyed.
        }
    }

    private static void runTarget() throws Exception {
        String name = ManagementFactory.getRuntimeMXBean().getName();
        String pid = name.substring(0, name.indexOf('@'));

        System.out.println(READY_PREFIX + pid);
        System.out.flush();
        Thread.sleep(60000);
    }
}
