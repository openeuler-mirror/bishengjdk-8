/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

import com.oracle.java.testlibrary.JDKToolFinder;
import com.oracle.java.testlibrary.OutputAnalyzer;

import java.io.BufferedReader;
import java.io.IOException;

public class TestBase {
    public static void resizeAndCheck(String pid, String newSize,
                                      String[] contains,
                                      String[] notContains) throws Exception {
        resizeAndCheck(pid, newSize, contains, notContains, "GC.elastic_max_heap");
    }

    public static void resizeAndCheck(String pid, String newSize,
                                      String[] contains,
                                      String[] notContains,
                                      String type) throws Exception {
        ProcessBuilder pb = new ProcessBuilder(JDKToolFinder.getJDKTool("jcmd"),
                                               pid,
                                               type,
                                               newSize);
        OutputAnalyzer output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        output.shouldHaveExitValue(0);
        checkOutput(output, contains, notContains);
    }

    public static void checkOutput(OutputAnalyzer output,
                                   String[] contains,
                                   String[] notContains) throws Exception {
        if (contains != null) {
            for (String s : contains) {
                output.shouldContain(s);
            }
        }
        if (notContains != null) {
            for (String s : notContains) {
                output.shouldNotContain(s);
            }
        }
    }

    public static String waitForPid(BufferedReader reader,
                                    StringBuilder targetOutput,
                                    String readyPrefix) throws Exception {
        long deadline = System.currentTimeMillis() + 30000;
        String line;
        while (System.currentTimeMillis() < deadline && (line = reader.readLine()) != null) {
            targetOutput.append(line).append('\n');
            if (line.startsWith(readyPrefix)) {
                return line.substring(readyPrefix.length());
            }
        }
        throw new RuntimeException("Target VM did not report its pid. Output:\n" + targetOutput);
    }

    public static void drain(BufferedReader reader, StringBuilder targetOutput) throws Exception {
        try {
            String line;
            while ((line = reader.readLine()) != null) {
                targetOutput.append(line).append('\n');
            }
        } catch (IOException e) {
            // The target process may close stdout while it is being destroyed.
        }
    }
}
