/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
 * @summary Test whether ub socket options work
 * @library /testlibrary
 * @compile test-classes/Dummy.java
 * @run main/othervm SocketOptionsTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;
import java.io.*;
import java.nio.file.*;

public class SocketOptionsTest {
    public static void main(String[] args) throws Exception {

        // build The app
        String[] appClass = new String[] {"Dummy"};

        // test Experimental VMOptions
        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UseUBSocket",
            appClass[0]);

        OutputAnalyzer output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        output.shouldContain("must be enabled via -XX:+UnlockExperimentalVMOptions");
        output.shouldHaveExitValue(1);

        // test no conf file
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            appClass[0]);

        output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        output.shouldContain("UB conf path is NULL");
        output.shouldHaveExitValue(0);

        // test not exist conf file
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBConfPath=no_exist_file",
            appClass[0]);

        output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        output.shouldContain("Load UB conf blank");
        output.shouldHaveExitValue(0);

        // test useless option PrintUBLog
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+PrintUBLog",
            appClass[0]);

        output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        output.shouldContain("Invalid flag PrintUBLog");
        output.shouldHaveExitValue(0);

        // test useless option UBLogPath
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:UBLogPath=ub.log",
            appClass[0]);

        output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        output.shouldContain("Invalid flag UBLogPath");
        output.shouldHaveExitValue(0);

        // test useless option UBConfPath
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:UBConfPath=ub.conf",
            appClass[0]);

        output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        output.shouldContain("Invalid flag UBConfPath");
        output.shouldHaveExitValue(0);

        // test conf file
        String testDir = System.getProperty("test.classes", ".");
        File allowlistFile = new File(testDir, "UBSocket.conf");
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(allowlistFile))) {
            writer.write("Dummy.main");
            writer.newLine();
        }
        String allowlistPath = allowlistFile.getAbsolutePath();

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:+PrintUBLog",
            "-XX:UBConfPath=" + allowlistPath,
            appClass[0]);

        output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        output.shouldContain("Load UB conf file");
        output.shouldContain("Load allow method: Dummy.main");
        output.shouldHaveExitValue(0);

        // test blank UBLogPath
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBLogPath=",
            "-XX:UBConfPath=" + allowlistPath,
            appClass[0]);

        output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        output.shouldContain("UB log path is NULL");
        output.shouldHaveExitValue(0);

        // delete conf file
        allowlistFile.delete();
    }
}