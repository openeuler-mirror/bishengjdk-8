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
 * @summary Test UB socket VM options and dedicated socket allow-list flags
 * @library /testlibrary
 * @compile ../SocketTestConfig.java
 * @run main/othervm SocketOptionsTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

public class SocketOptionsTest {
    public static void main(String[] args) throws Exception {
        String[] appClass = new String[] { NoOpApp.class.getName() };

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UseUBSocket",
            appClass[0]
        );
        OutputAnalyzer output = new OutputAnalyzer(pb.start());
        output.shouldContain("must be enabled via -XX:+UnlockExperimentalVMOptions");
        output.shouldHaveExitValue(1);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        output.shouldHaveExitValue(0);

        Path missingConfLog = Files.createTempFile("ubsocket-options-missing-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConfPath=no_exist_file",
            "-XX:UBLog=path=" + missingConfLog + ",socket=warning",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String missingConfOutput = output.getOutput() + readText(missingConfLog);
        if (!missingConfOutput.contains("[socket][WARNING] Load allow-list failed or empty")) {
            throw new RuntimeException("Expected missing socket config warning\n" + missingConfOutput);
        }
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:UBLog=all=debug",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:UBLog=path=ub.log",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketControlPort=28772",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        output.shouldHaveExitValue(0);

        String configPath = SocketTestConfig.ensureSharedConfig();
        Path validConfLog = Files.createTempFile("ubsocket-options-valid-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConfPath=" + configPath,
            "-XX:UBLog=path=" + validConfLog + ",socket=debug",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String validConfOutput = output.getOutput() + readText(validConfLog);
        mustContain(validConfOutput, "[socket][DEBUG] Load conf file");
        mustContain(validConfOutput, "Load allow method: sun/nio/ch/SocketChannelImpl.connect");
        mustContain(validConfOutput, "Load allow method: sun/nio/ch/SocketChannelImpl.checkConnect");
        mustContain(validConfOutput, "Load allow method: sun/nio/ch/ServerSocketChannelImpl.accept");
        output.shouldHaveExitValue(0);

        String invalidConfig = SocketTestConfig.writeConfig(
            "UBSocketInvalid.conf",
            "sun/nio/ch/SocketChannelImpl.connect\n" +
            "sun/nio/ch/SocketChannelImpl.invalid-method!\n"
        );
        Path invalidConfLog = Files.createTempFile("ubsocket-options-invalid-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConfPath=" + invalidConfig,
            "-XX:UBLog=path=" + invalidConfLog + ",socket=debug",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String invalidConfOutput = output.getOutput() + readText(invalidConfLog);
        mustContain(invalidConfOutput, "[socket][DEBUG] Load conf file");
        mustContain(invalidConfOutput, "Load allow method: sun/nio/ch/SocketChannelImpl.connect");
        mustContain(invalidConfOutput, "Ignore invalid allow-list entry");
        mustContain(invalidConfOutput, "sun/nio/ch/SocketChannelImpl.invalid-method!");
        output.shouldHaveExitValue(0);

        Path socketOnlyLog = Files.createTempFile("ubsocket-options-socketonly-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBLog=socket_path=" + socketOnlyLog + ",socket=debug",
            "-XX:UBSocketConfPath=" + configPath,
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String socketOnlyOutput = output.getOutput() + readText(socketOnlyLog);
        mustContain(socketOnlyOutput, "[socket][DEBUG]");
        mustContain(socketOnlyOutput, "Load conf file");
        mustContain(socketOnlyOutput, "Load allow method: sun/nio/ch/SocketChannelImpl.connect");
        output.shouldHaveExitValue(0);

        Path fileOnlyLog = Files.createTempFile("ubsocket-options-fileonly-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBLog=file_path=" + fileOnlyLog + ",file=error",
            "-XX:UBSocketConfPath=" + configPath,
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String fileOnlyOutput = output.getOutput() + readText(fileOnlyLog);
        mustNotContain(fileOnlyOutput, "[socket]");
        mustNotContain(fileOnlyOutput, "Load allow method: sun/nio/ch/SocketChannelImpl.connect");
        output.shouldHaveExitValue(0);

        Path splitSocketLog = Files.createTempFile("ubsocket-options-split-socket-", ".log");
        Path splitFileLog = Files.createTempFile("ubsocket-options-split-file-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBLog=socket_path=" + splitSocketLog + ",file_path=" + splitFileLog +
                ",socket=debug,file=debug",
            "-XX:UBSocketConfPath=" + configPath,
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String splitSocketOutput = output.getOutput() + readText(splitSocketLog);
        String splitFileOutput = readText(splitFileLog);
        mustContain(splitSocketOutput, "[socket][DEBUG]");
        mustNotContain(splitFileOutput, "[socket]");
        output.shouldHaveExitValue(0);
    }

    private static String readText(Path path) throws Exception {
        return new String(Files.readAllBytes(path), StandardCharsets.UTF_8);
    }

    private static void mustContain(String text, String token) {
        if (!text.contains(token)) {
            throw new RuntimeException("'" + token + "' missing from combined output\n" + text);
        }
    }

    private static void mustNotContain(String text, String token) {
        if (text.contains(token)) {
            throw new RuntimeException("'" + token + "' unexpectedly present in combined output\n" + text);
        }
    }

    public static class NoOpApp {
        public static void main(String[] args) {
        }
    }
}
