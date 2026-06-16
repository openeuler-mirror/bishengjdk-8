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
 * @test DynamicMaxHeapDCmdTest
 * @summary Test dynamic max heap diagnostic commands with PS and CMS.
 * @library /testlibrary
 * @requires os.family=="linux"
 * @build DynamicMaxHeapDCmdTest
 * @run main DynamicMaxHeapDCmdTest
 */

import com.oracle.java.testlibrary.JDKToolFinder;
import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.lang.management.ManagementFactory;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;

public class DynamicMaxHeapDCmdTest {
    private static final String TARGET = "target";
    private static final String READY_PREFIX = "DynamicMaxHeapDCmdTest.pid=";

    public static void main(String[] args) throws Exception {
        if (args.length > 0 && TARGET.equals(args[0])) {
            runTarget();
            return;
        }

        if (!isDynamicMaxHeapSupported()) {
            System.out.println("Dynamic max heap is not supported on this machine, skip test.");
            return;
        }

        testDynamicMaxHeap("-XX:+UseParallelGC");
        testDynamicMaxHeap("-XX:+UseConcMarkSweepGC");
        testElasticMaxHeap("-XX:+UseParallelGC");
        testElasticMaxHeap("-XX:+UseConcMarkSweepGC");
    }

    private static boolean isDynamicMaxHeapSupported() throws Exception {
        OutputAnalyzer output = ProcessTools.executeProcess(
                ProcessTools.createJavaProcessBuilder("-XX:+UseParallelGC",
                                                      "-Xmx512m",
                                                      "-XX:DynamicMaxHeapSizeLimit=1g",
                                                      "-version"));
        output.shouldHaveExitValue(0);

        String text = output.getOutput();
        return !text.contains("DynamicMaxHeap feature are not available") &&
               !text.contains("DynamicMaxHeap feature is not available");
    }

    private static void testDynamicMaxHeap(String gc) throws Exception {
        runJcmdTest(gc,
                    "GC.change_max_heap",
                    "768m",
                    false,
                    "GC.change_max_heap success");
    }

    private static void testElasticMaxHeap(String gc) throws Exception {
        runJcmdTest(gc,
                    "GC.elastic_max_heap",
                    "768m",
                    true,
                    "GC.elastic_max_heap success");
    }

    private static void runJcmdTest(String gc,
                                    String dcmd,
                                    String size,
                                    boolean elastic,
                                    String expected) throws Exception {
        List<String> args = new ArrayList<String>();
        args.add(gc);
        args.add("-XX:+UsePerfData");
        args.add("-Xms128m");
        args.add("-Xmx512m");
        if (elastic) {
            args.add("-XX:+ElasticMaxHeap");
            args.add("-XX:ElasticMaxHeapSize=1g");
            args.add("-XX:+TraceElasticMaxHeap");
        } else {
            args.add("-XX:DynamicMaxHeapSizeLimit=1g");
            args.add("-XX:+TraceDynamicMaxHeap");
        }
        args.add(DynamicMaxHeapDCmdTest.class.getName());
        args.add(TARGET);

        ProcessBuilder targetBuilder =
                ProcessTools.createJavaProcessBuilder(args.toArray(new String[args.size()]));
        targetBuilder.redirectErrorStream(true);

        Process target = targetBuilder.start();
        StringBuilder targetOutput = new StringBuilder();
        BufferedReader reader = new BufferedReader(new InputStreamReader(target.getInputStream()));

        try {
            String pid = waitForTargetPid(reader, targetOutput);
            OutputAnalyzer output = runJcmd(pid, dcmd, size);
            System.out.println(output.getOutput());
            output.shouldHaveExitValue(0);
            output.shouldContain(expected);
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

    private static OutputAnalyzer runJcmd(String pid, String dcmd, String size) throws Exception {
        ProcessBuilder jcmd = new ProcessBuilder(JDKToolFinder.getJDKTool("jcmd"),
                                                 pid,
                                                 dcmd,
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

        List<byte[]> keepAlive = new ArrayList<byte[]>();
        for (int i = 0; i < 64; i++) {
            keepAlive.add(new byte[1024 * 1024]);
        }

        Thread.sleep(60000);
    }
}
