/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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
 * @test
 * @summary UB elastic heap test via ProcessTools (single spawned Java process)
 * @library /testlibrary
 * @compile test-classes/ElasticHeapWorker.java test-classes/TestBase.java
 * @run main/othervm ElasticHeapTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.util.Scanner;
import java.util.List;
import java.util.ArrayList;

public class ElasticHeapTest extends TestBase {

    public static void main(String[] args) throws Exception {
        // Case 1: Borrow enabled, -Xmx 1G, alloc 512M.
        String[] contains = new String[] {
                "Allocation Success!",
                "Verification Success!",
                "[UB HEAP][INFO] Initializing pool"
        };
        String[] notContains = new String[] {
                "[UB HEAP][INFO] Acquiring"
        };
        runTest("Case1_Base", true, "1g", "2g", null, "512",
                contains, notContains, 0);

        // Case 2: Borrow enabled, -Xmx 100M, expand to 10G, alloc 5G.
        contains = new String[] {
                "Allocation Success!",
                "Verification Success!",
                "[UB HEAP][INFO] Initializing pool",
                "[UB HEAP][INFO] Acquiring"
        };
        notContains = null;
        runTest("Case2_expand", true, "100M", "15g", "10g", "5120",
                contains, notContains, 0);

        // Case 3: Borrow enabled, -Xmx 100M, alloc over max heap(200M).
        contains = new String[] {
                "[UB HEAP][INFO] Initializing pool"
        };
        notContains = new String[] {
                "Allocation Success!",
        };
        runTest("Case3_OOM", true, "100m", "1g", null, "200",
                contains, notContains, 1);
    }

    private static void runTest(String name, boolean enable, String xmx, String dynamicLimit,
                                String jcmdSize, String allocMb, String[] contains,
                                String[] notContains, int expectedExitValue) throws Exception {

        System.out.println("\n>>> Starting " + name);
        String borrowFlag = enable ? "-XX:+UseBorrowedMemory" : "-XX:-UseBorrowedMemory";

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(
                "-XX:+UnlockExperimentalVMOptions",
                borrowFlag,
                "-XX:+UseG1GC",
                "-Xmx" + xmx,
                "-XX:DynamicMaxHeapSizeLimit=" + dynamicLimit,
                "ElasticHeapWorker",
                allocMb
        );

        Process p = pb.start();
        long pid = getPidOfProcess(p);

        if (jcmdSize != null) {
            // Wait 1s for worker starting.
            Thread.sleep(1000);
            System.out.println("[Driver] Executing jcmd resize to " + jcmdSize);
            resizeAndCheck(pid, jcmdSize, new String[]{"success"}, null, "GC.change_max_heap");
        }

        System.out.println("[Driver] Waiting for Worker to finish...");
        p.waitFor();
        OutputAnalyzer output = new OutputAnalyzer(p);
        checkOutput(output, contains, notContains, expectedExitValue);

        System.out.println(">>> " + name + " PASSED");
    }

    private static void checkOutput(OutputAnalyzer output, String[] contains, String[] notContains, int expectedExitValue) {
        if (contains != null) {
            for (String s : contains) output.shouldContain(s);
        }
        if (notContains != null) {
            for (String s : notContains) output.shouldNotContain(s);
        }

        output.shouldHaveExitValue(expectedExitValue);
        System.out.println(output.getOutput());
    }
}