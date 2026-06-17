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
 * @test DynamicMaxHeapMemoryPoolTest
 * @summary Test memory pool max size after dynamic max heap changes.
 * @library /testlibrary
 * @requires os.family=="linux"
 * @build DynamicMaxHeapMemoryPoolTest
 * @run main DynamicMaxHeapMemoryPoolTest
 */

import com.oracle.java.testlibrary.JDKToolFinder;
import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.lang.management.ManagementFactory;
import java.lang.management.MemoryPoolMXBean;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;

public class DynamicMaxHeapMemoryPoolTest {
    private static final String TARGET = "target";
    private static final String READY_PREFIX = "DynamicMaxHeapMemoryPoolTest.pid=";
    private static final String SUCCESS = "DynamicMaxHeapMemoryPoolTest.success";
    private static final String UNSUPPORTED = "DynamicMaxHeap feature are not available";
    private static final long TARGET_MAX = 768L * 1024L * 1024L;

    public static void main(String[] args) throws Exception {
        if (args.length > 0 && TARGET.equals(args[0])) {
            runTarget(args[1], args[2]);
            return;
        }

        if (!isDynamicMaxHeapSupported()) {
            System.out.println("Dynamic max heap is not supported on this machine, skip test.");
            return;
        }

        testMemoryPoolMax("-XX:+UseParallelGC", "PS Old Gen", false);
        testMemoryPoolMax("-XX:+UseConcMarkSweepGC", "CMS Old Gen", false);
        testMemoryPoolMax("-XX:+UseParallelGC", "PS Old Gen", true);
        testMemoryPoolMax("-XX:+UseConcMarkSweepGC", "CMS Old Gen", true);
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

    private static void testMemoryPoolMax(String gc, String oldPoolName, boolean elastic) throws Exception {
        File signal = File.createTempFile("dynamic-max-heap", ".signal");
        if (!signal.delete()) {
            throw new RuntimeException("Could not delete signal file " + signal);
        }

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
        args.add(DynamicMaxHeapMemoryPoolTest.class.getName());
        args.add(TARGET);
        args.add(oldPoolName);
        args.add(signal.getAbsolutePath());

        ProcessBuilder targetBuilder =
                ProcessTools.createJavaProcessBuilder(args.toArray(new String[args.size()]));
        targetBuilder.redirectErrorStream(true);

        Process target = targetBuilder.start();
        StringBuilder targetOutput = new StringBuilder();
        BufferedReader reader = new BufferedReader(new InputStreamReader(target.getInputStream()));

        try {
            String pid = waitForTargetPid(reader, targetOutput);
            OutputAnalyzer output = runJcmd(pid,
                                           elastic ? "GC.elastic_max_heap" : "GC.change_max_heap",
                                           "768m");
            System.out.println(output.getOutput());
            output.shouldHaveExitValue(0);
            output.shouldContain(elastic ? "GC.elastic_max_heap success" : "GC.change_max_heap success");

            touch(signal);
            waitForSuccess(reader, targetOutput);
        } finally {
            target.destroy();
            if (!target.waitFor(10, TimeUnit.SECONDS)) {
                target.destroyForcibly();
                target.waitFor();
            }
            drain(reader, targetOutput);
            System.out.println(targetOutput.toString());
            signal.delete();
        }
    }

    private static OutputAnalyzer runJcmd(String pid, String dcmd, String size) throws Exception {
        ProcessBuilder jcmd = new ProcessBuilder(JDKToolFinder.getJDKTool("jcmd"), pid, dcmd, size);
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

    private static void waitForSuccess(BufferedReader reader, StringBuilder targetOutput) throws Exception {
        long deadline = System.currentTimeMillis() + 30000;
        String line;
        while (System.currentTimeMillis() < deadline && (line = reader.readLine()) != null) {
            targetOutput.append(line).append('\n');
            if (line.contains(SUCCESS)) {
                return;
            }
        }
        throw new RuntimeException("Target VM did not observe memory pool update. Output:\n" + targetOutput);
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

    private static void touch(File file) throws Exception {
        FileWriter writer = new FileWriter(file);
        try {
            writer.write("done");
        } finally {
            writer.close();
        }
    }

    private static void runTarget(String poolName, String signalPath) throws Exception {
        MemoryPoolMXBean pool = findPool(poolName);
        long before = pool.getUsage().getMax();
        String name = ManagementFactory.getRuntimeMXBean().getName();
        String pid = name.substring(0, name.indexOf('@'));

        System.out.println("pool=" + poolName + ", before=" + before);
        System.out.println(READY_PREFIX + pid);
        System.out.flush();

        File signal = new File(signalPath);
        long deadline = System.currentTimeMillis() + 30000;
        while (System.currentTimeMillis() < deadline && !signal.exists()) {
            Thread.sleep(100);
        }
        if (!signal.exists()) {
            throw new RuntimeException("Timed out waiting for signal file " + signalPath);
        }

        deadline = System.currentTimeMillis() + 30000;
        while (System.currentTimeMillis() < deadline) {
            long after = pool.getUsage().getMax();
            System.out.println("pool=" + poolName + ", after=" + after);
            if (after != before && after > 0 && after <= TARGET_MAX) {
                System.out.println(SUCCESS);
                return;
            }
            Thread.sleep(100);
        }
        throw new RuntimeException("Memory pool max did not change to dynamic limit: before=" + before);
    }

    private static MemoryPoolMXBean findPool(String name) {
        for (MemoryPoolMXBean pool : ManagementFactory.getMemoryPoolMXBeans()) {
            if (name.equals(pool.getName())) {
                return pool;
            }
        }
        throw new RuntimeException("Could not find memory pool " + name);
    }
}
