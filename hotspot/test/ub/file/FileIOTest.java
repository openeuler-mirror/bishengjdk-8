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
        Path ubLog = Files.createTempFile("ubfile-", ".log");

        String testDir = System.getProperty("test.classes", ".");
        File allowlistFile = new File(testDir, "UBFile.conf");
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
                "-XX:UBFileConf=" + allowlistPath,
                "-XX:UBLog=path=" + ubLog.toString() + ",file=debug",
                "FileIOChild",
                file.toString(),
                content
        );

        OutputAnalyzer out = new OutputAnalyzer(pb.start());
        out.shouldHaveExitValue(0);
        out.shouldContain("File IO Test PASSED");
        String combinedLog = out.getOutput()
            + new String(Files.readAllBytes(ubLog), java.nio.charset.StandardCharsets.UTF_8);
        if (!combinedLog.contains("open file")) {
            throw new RuntimeException("UB file path should emit open trace\n" + combinedLog);
        }
        if (!combinedLog.contains("cur blk")) {
            throw new RuntimeException("UB file path should emit block trace\n" + combinedLog);
        }

        System.out.println(combinedLog);

        // normal case
        pb = ProcessTools.createJavaProcessBuilder(
                "-cp", testDir,
                "FileIOChild",
                file.toString(),
                content
        );
        out = new OutputAnalyzer(pb.start());
        out.shouldHaveExitValue(0);
        out.shouldContain("File IO Test PASSED");
        out.shouldNotContain("open file");
        out.shouldNotContain("cur blk");

        System.out.println(out.getOutput());

        allowlistFile.delete();
        Files.deleteIfExists(ubLog);
    }
}
