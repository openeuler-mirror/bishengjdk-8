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
 * @run main/othervm OptionsTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;
import java.net.ServerSocket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

public class OptionsTest {
    public static void main(String[] args) throws Exception {
        String[] appClass = new String[] { NoOpApp.class.getName() };

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+PrintFlagsFinal",
            "-version"
        );
        OutputAnalyzer output = new OutputAnalyzer(pb.start());
        output.shouldMatch("(?m)^\\s*uintx UBSocketMemorySize\\s+[:]?= 268431360\\s+\\{experimental\\}$");
        output.shouldMatch("(?m)^\\s*uintx UBSocketSpinNanos\\s+[:]?= 0\\s+\\{experimental\\}$");
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:UBSocketSpinNanos=10000",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        mustContain(output.getOutput(), "UBSocket is disabled, but spin nanos is set.");
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:UBSocketSpinNanos=-1",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        mustContain(output.getOutput(),
                    "Improperly specified VM option 'UBSocketSpinNanos=-1'");
        output.shouldHaveExitValue(1);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UseUBSocket",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        output.shouldContain("must be enabled via -XX:+UnlockExperimentalVMOptions");
        output.shouldHaveExitValue(1);

        // UBSocket Conf
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        output.shouldContain("UBSocketConf path is NULL, UBSocket is disabled.");
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        output.shouldContain("UBSocketConf path is NULL, UBSocket is disabled.");
        output.shouldHaveExitValue(0);

        Path missingConfLog = Files.createTempFile("ubsocket-options-missing-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=no_exist_file",
            "-XX:UBLog=path=" + missingConfLog + ",socket=warning",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String missingConfOutput = output.getOutput() + readText(missingConfLog);
        if (!missingConfOutput.contains("UBSocket load configuration failed or empty: no_exist_file")) {
            throw new RuntimeException("Expected missing socket config warning\n" + missingConfOutput);
        }
        output.shouldHaveExitValue(0);

        String configPath = SocketTestConfig.ensureSharedConfig();
        Path validConfLog = Files.createTempFile("ubsocket-options-valid-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=" + configPath,
            "-XX:UBLog=path=" + validConfLog + ",socket=debug",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String validConfOutput = output.getOutput() + readText(validConfLog);
        mustContain(validConfOutput, "[socket][INFO] Load conf file");
        mustContain(validConfOutput, "Load allow method: sun/nio/ch/SocketChannelImpl.connect");
        mustContain(validConfOutput, "Load allow method: sun/nio/ch/SocketChannelImpl.finishConnect");
        mustContain(validConfOutput, "Load allow method: sun/nio/ch/ServerSocketChannelImpl.accept");
        output.shouldHaveExitValue(0);

        // Attach policy accepts the supported values and rejects unknown values.
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketAttach=all",
            "-XX:UBSocketConf=" + configPath,
            "-XX:UBSocketPort=" + freePort(),
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketAttach=invalid",
            "-XX:UBSocketConf=" + configPath,
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        mustContain(output.getOutput(), "UBSocket attach policy(invalid) invalid");
        output.shouldHaveExitValue(0);

        String invalidConfig = SocketTestConfig.writeConfig(
            "UBSocketInvalid.conf",
            "#stack\n" +
            "sun/nio/ch/SocketChannelImpl.connect\n" +
            "sun/nio/ch/SocketChannelImpl.invalid-method!\n"
        );
        Path invalidConfLog = Files.createTempFile("ubsocket-options-invalid-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=" + invalidConfig,
            "-XX:UBLog=path=" + invalidConfLog + ",socket=debug",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String invalidConfOutput = output.getOutput() + readText(invalidConfLog);
        mustContain(invalidConfOutput, "[socket][INFO] Load conf file");
        mustContain(invalidConfOutput, "Load allow method: sun/nio/ch/SocketChannelImpl.connect");
        mustContain(invalidConfOutput, "Ignore invalid configuration");
        mustContain(invalidConfOutput, "sun/nio/ch/SocketChannelImpl.invalid-method!");
        output.shouldHaveExitValue(0);

        String mappedConfig = SocketTestConfig.writeConfig(
            "UBSocketMapped.conf",
            "#stack\n" +
            "sun/nio/ch/SocketChannelImpl.connect\n" +
            "sun/nio/ch/SocketChannelImpl.finishConnect\n" +
            "sun/nio/ch/ServerSocketChannelImpl.accept\n" +
            "#address\n" +
            "self.ip=192.168.231.44 -> *\n" +
            "self.port=23081 -> 20880\n" +
            "self.port=24081 -> 33081\n" +
            "remote.mapping=192.168.231.44:23091 -> 192.168.231.44:24091\n"
        );
        Path mappedConfLog = Files.createTempFile("ubsocket-options-mapped-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=" + mappedConfig,
            "-XX:UBLog=path=" + mappedConfLog + ",socket=debug",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String mappedConfOutput = output.getOutput() + readText(mappedConfLog);
        mustContain(mappedConfOutput, "Load allow method: sun/nio/ch/SocketChannelImpl.connect");
        mustContain(mappedConfOutput, "Load self ip map");
        mustContain(mappedConfOutput, "Load self port map public_port=23081 local_port=20880");
        mustContain(mappedConfOutput, "Load self port map public_port=24081 local_port=33081");
        mustContain(mappedConfOutput, "Load remote endpoint map data_port=23091 control_port=24091");
        mustNotContain(mappedConfOutput, "Ignore invalid configuration");
        output.shouldHaveExitValue(0);

        // UBSocket Port
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=" + configPath,
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        mustContain(output.getOutput(), "UBSocket port(0) invalid, UBSocket is disabled.");
        output.shouldHaveExitValue(0);

        // UBSocket ring count
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:UBSocketRingCount=2",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        mustContain(output.getOutput(), "UBSocket is disabled, but ring count is set.");
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=" + configPath,
            "-XX:UBSocketPort=" + freePort(),
            "-XX:UBSocketRingCount=0",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        mustContain(output.getOutput(), "UBSocket ring count(0) invalid");
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=" + configPath,
            "-XX:UBSocketPort=" + freePort(),
            "-XX:UBSocketMemorySize=526336",
            "-XX:UBSocketRingCount=257",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=" + configPath,
            "-XX:UBSocketPort=" + freePort(),
            "-XX:UBSocketMemorySize=524288",
            "-XX:UBSocketRingCount=4096",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        mustContain(output.getOutput(), "UBSocket ring count(4096) invalid");
        output.shouldHaveExitValue(0);

        // UBSocket poller CPU
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:UBSocketPollerCpu=0",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        mustContain(output.getOutput(), "UBSocket is disabled, but poller CPU is set.");
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=" + configPath,
            "-XX:UBSocketPort=" + freePort(),
            "-XX:UBSocketPollerCpu=-2",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        mustContain(output.getOutput(), "UBSocket poller CPU(-2) invalid, UBSocket is disabled.");
        output.shouldHaveExitValue(0);

        int pollerCpu = firstAllowedCpu();
        Path pollerCpuLog = Files.createTempFile("ubsocket-options-poller-cpu-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=" + configPath,
            "-XX:UBSocketPort=" + freePort(),
            "-XX:UBSocketMemorySize=1048576",
            "-XX:UBSocketPollerCpu=" + pollerCpu,
            "-XX:UBLog=path=" + pollerCpuLog + ",socket=debug",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String pollerCpuOutput = output.getOutput() + readText(pollerCpuLog);
        mustContain(pollerCpuOutput, "process poller bound cpu=" + pollerCpu);
        mustContain(pollerCpuOutput, "size=4194304 ring_count=8");
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=" + configPath,
            "-XX:UBSocketPort=-1",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        mustContain(output.getOutput(), "Improperly specified VM option 'UBSocketPort=-1'");
        output.shouldHaveExitValue(1);

        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBSocketConf=" + configPath,
            "-XX:UBSocketPort=65536",
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        mustContain(output.getOutput(), "UBSocket port(65536) invalid, UBSocket is disabled.");
        output.shouldHaveExitValue(0);

        // UBSocket Log
        Path socketOnlyLog = Files.createTempFile("ubsocket-options-socketonly-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBLog=socket_path=" + socketOnlyLog + ",socket=debug",
            "-XX:UBSocketConf=" + configPath,
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String socketOnlyOutput = output.getOutput() + readText(socketOnlyLog);
        mustContain(socketOnlyOutput, "[socket][INFO]");
        mustContain(socketOnlyOutput, "Load conf file");
        mustContain(socketOnlyOutput, "Load allow method: sun/nio/ch/SocketChannelImpl.connect");
        output.shouldHaveExitValue(0);

        Path pidLogDir = Files.createTempDirectory("ubsocket-options-pidlog-");
        Path pidLogPattern = pidLogDir.resolve("ubsocket-options-%p.log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBLog=path=" + pidLogPattern + ",socket=debug",
            "-XX:UBSocketConf=" + configPath,
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        output.shouldHaveExitValue(0);
        if (Files.exists(pidLogPattern)) {
            throw new RuntimeException("UBLog should expand %p instead of creating literal path: "
                + pidLogPattern);
        }
        Path expandedPidLog = null;
        try (java.nio.file.DirectoryStream<Path> paths =
                Files.newDirectoryStream(pidLogDir, "ubsocket-options-*.log")) {
            for (Path path : paths) {
                if (expandedPidLog != null) {
                    throw new RuntimeException("Expected one expanded pid log, found at least two: "
                        + expandedPidLog + " and " + path);
                }
                expandedPidLog = path;
            }
        }
        if (expandedPidLog == null) {
            throw new RuntimeException("UBLog %p expansion did not create a log file in "
                + pidLogDir);
        }
        String pidLogName = expandedPidLog.getFileName().toString();
        if (!pidLogName.matches("ubsocket-options-[0-9]+\\.log")) {
            throw new RuntimeException("UBLog %p expansion produced unexpected file name: "
                + pidLogName);
        }
        String pidLogOutput = output.getOutput() + readText(expandedPidLog);
        mustContain(pidLogOutput, "[socket][INFO]");

        Path fileOnlyLog = Files.createTempFile("ubsocket-options-fileonly-", ".log");
        pb = ProcessTools.createJavaProcessBuilder(
            true,
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:+UseUBSocket",
            "-XX:UBLog=file_path=" + fileOnlyLog + ",file=error",
            "-XX:UBSocketConf=" + configPath,
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
            "-XX:UBSocketConf=" + configPath,
            appClass[0]
        );
        output = new OutputAnalyzer(pb.start());
        String splitSocketOutput = output.getOutput() + readText(splitSocketLog);
        String splitFileOutput = readText(splitFileLog);
        mustContain(splitSocketOutput, "[socket][INFO]");
        mustNotContain(splitFileOutput, "[socket]");
        output.shouldHaveExitValue(0);

    }

    private static String readText(Path path) throws Exception {
        return new String(Files.readAllBytes(path), StandardCharsets.UTF_8);
    }

    private static int freePort() throws Exception {
        try (ServerSocket socket = new ServerSocket(0)) {
            return socket.getLocalPort();
        }
    }

    private static int firstAllowedCpu() throws Exception {
        for (String line : Files.readAllLines(java.nio.file.Paths.get("/proc/self/status"),
                                              StandardCharsets.UTF_8)) {
            if (!line.startsWith("Cpus_allowed_list:")) {
                continue;
            }
            String firstRange = line.substring(line.indexOf(':') + 1).trim().split(",")[0];
            String firstCpu = firstRange.split("-")[0];
            return Integer.parseInt(firstCpu);
        }
        throw new RuntimeException("Cpus_allowed_list missing from /proc/self/status");
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
