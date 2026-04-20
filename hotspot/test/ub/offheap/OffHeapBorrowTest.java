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
 * @summary UB offheap memory borrow test via ProcessTools (single spawned Java process)
 * @library /testlibrary
 * @compile test-classes/UnsafeWorker.java
 * @run main/othervm OffHeapBorrowTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;

public class OffHeapBorrowTest {
    public static void main(String[] args) throws Exception {
        String[] notContains;

        // Case 1: Memory borrow disabled test.
        String[] contains = new String[] {
                "UB offheap allocator Test PASSED!",
                "Allocation Success!",
                "Verification Success!"
        };
        runTest(false, "128M", "256", contains, null, 0);

        // Case 2: Borrow enabled, size < MaxOffheapLocalMemory
        contains = new String[] {
                "UB offheap allocator Test PASSED!",
                "Allocation Success!",
                "Verification Success!"
        };
        notContains = null;
        runTest(true, "256M", "128", contains, notContains, 0);

        // Case 3: Borrow enabled, size > MaxOffheapLocalMemory
        contains = new String[] {
                "UB offheap allocator Test PASSED!",
                "Allocation Success!",
                "Verification Success!"
        };
        notContains = null;
        runTest(true, "128M", "256", contains, notContains, 0);
    }

    private static void runTest(boolean enable, String localLimit, String allocMb,
                                String[] contains, String[] notContains, int expectedExitValue) throws Exception {

        String useFlag = enable ? "-XX:+UseBorrowedMemory" : "-XX:-UseBorrowedMemory";

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(
                "-XX:+UnlockExperimentalVMOptions",
                useFlag,
                "-XX:MaxOffheapLocalMemory=" + localLimit,
                "-XX:NativeMemoryTracking=detail",
                "UnsafeWorker",
                allocMb
        );

        System.out.println("\n[UB OFFHEAP TEST CASE] " + useFlag + " | LocalLimit: " + localLimit + " | Request: " + allocMb + "MB");

        OutputAnalyzer output = new OutputAnalyzer(pb.start());

        checkOutput(output, contains, notContains, expectedExitValue);

        System.out.println("Result: PASSED");
    }

    private static void checkOutput(OutputAnalyzer output, String[] contains, String[] notContains,
                                    int expectedExitValue) throws Exception {

        if (contains != null) {
            for (String expected : contains) {
                output.shouldContain(expected);
            }
        }

        if (notContains != null) {
            for (String forbidden : notContains) {
                output.shouldNotContain(forbidden);
            }
        }

        output.shouldHaveExitValue(expectedExitValue);
        System.out.println(output.getOutput());
    }
}
