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
 * @summary File read/write test via ProcessTools (single spawned Java process)
 * @library /testlibrary
 * @compile test-classes/FileIOHelper.java test-classes/FileIOChild.java
 * @run main/othervm FileIOTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;
import java.io.*;
import java.nio.file.*;

public class FileIOTest {

    public static void main(String[] args) throws Exception {

        String content = "Hello File IO Test!";
        Path file = Files.createTempFile("application_fileio-", ".txt");

        String testDir = System.getProperty("test.classes", ".");
        File allowlistFile = new File(testDir, "UBSocket.conf");
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(allowlistFile))) {
            writer.write("FileIOHelper.writeString");
            writer.newLine();
            writer.write("FileIOHelper.readString");
            writer.newLine();
        }
        String allowlistPath = allowlistFile.getAbsolutePath();

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(
                "-cp", testDir,
                "-XX:+UnlockExperimentalVMOptions",
                "-XX:+UseUBFile",
                "-XX:+PrintUBLog",
                "-XX:UBConfPath=" + allowlistPath,
                "FileIOChild",
                file.toString(),
                content
        );

        OutputAnalyzer out = new OutputAnalyzer(pb.start());
        out.shouldHaveExitValue(0);
        // APP log
        out.shouldContain("File IO Test PASSED");
        // UB debug log
        out.shouldContain("open file");
        out.shouldContain("cur blk");

        System.out.println(out.getOutput());

        // normal case
        pb = ProcessTools.createJavaProcessBuilder(
                "-cp", testDir,
                "FileIOChild",
                file.toString(),
                content
        );
        out = new OutputAnalyzer(pb.start());
        out.shouldHaveExitValue(0);
        // APP log
        out.shouldContain("File IO Test PASSED");
        // UB debug log
        out.shouldNotContain("open file");
        out.shouldNotContain("cur blk");

        System.out.println(out.getOutput());

        allowlistFile.delete();
    }
}