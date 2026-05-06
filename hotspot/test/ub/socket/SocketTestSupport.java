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

import com.oracle.java.testlibrary.ProcessTools;
import com.oracle.java.testlibrary.OutputAnalyzer;
import java.io.IOException;
import java.lang.reflect.Field;
import java.net.ServerSocket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Stream;

public final class SocketTestSupport {
    private static final AtomicInteger UB_LOG_FILE_ID = new AtomicInteger();
    private static final int FREE_PORT_ATTEMPTS = 100;
    private static final Set<Integer> RESERVED_PORTS = new HashSet<Integer>();

    public static final class ScenarioLogs {
        public final String clientLog;
        public final String serverLog;

        ScenarioLogs(String clientLog, String serverLog) {
            this.clientLog = clientLog;
            this.serverLog = serverLog;
        }
    }

    private SocketTestSupport() {
    }

    public static synchronized int findFreePort() throws IOException {
        for (int i = 0; i < FREE_PORT_ATTEMPTS; i++) {
            try (ServerSocket socket = new ServerSocket(0)) {
                int port = socket.getLocalPort();
                if (!RESERVED_PORTS.contains(port)) {
                    RESERVED_PORTS.add(port);
                    return port;
                }
            }
        }
        throw new IOException("Could not find an unused unique port");
    }

    public static ProcessBuilder createPlainProcessBuilder(String mainClass, String ... args)
        throws Exception {
        return ProcessTools.createJavaProcessBuilder(true, prependMainClass(mainClass, args));
    }

    public static ProcessBuilder createUbProcessBuilder(String configPath, int controlPort,
                                                        String mainClass, String ... args)
        throws Exception {
        return createProcessBuilder(true, configPath, controlPort, mainClass, args);
    }

    public static ProcessBuilder createUbProcessBuilderWithTimeout(String configPath,
                                                                   int controlPort,
                                                                   long timeoutMs,
                                                                   String mainClass,
                                                                   String ... args)
        throws Exception {
        return createUbProcessBuilderWithTimeoutAndVmOptions(
            configPath, controlPort, timeoutMs, new String[0], mainClass, args);
    }

    public static ProcessBuilder createUbProcessBuilderWithTimeoutAndVmOptions(
                                                                   String configPath,
                                                                   int controlPort,
                                                                   long timeoutMs,
                                                                   String[] vmOptions,
                                                                   String mainClass,
                                                                   String ... args)
        throws Exception {
        List<String> command = createUbOptions(configPath, controlPort, mainClass);
        if (timeoutMs >= 0L) {
            command.add("-XX:UBSocketTimeout=" + timeoutMs);
        }
        for (String vmOption : vmOptions) {
            command.add(vmOption);
        }
        command.add(mainClass);
        for (String arg : args) {
            command.add(arg);
        }

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(
            true, command.toArray(new String[command.size()]));
        pb.environment().put("UBSOCKET_LOG_PATH", extractUbLogPath(command.get(4)));
        return pb;
    }

    public static ScenarioLogs runUbScenario(String configPath, int controlPort,
                                             long startupDelayMs,
                                             String clientSuccessToken,
                                             String serverSuccessToken,
                                             String[] serverCommand,
                                             String[] clientCommand) throws Exception {
        Process server = null;
        try {
            ProcessBuilder serverPb = createUbProcessBuilder(configPath, controlPort,
                serverCommand[0], tail(serverCommand));
            String serverUbLogPath = getUbLogPath(serverPb);
            server = serverPb.start();
            Thread.sleep(startupDelayMs);

            ProcessBuilder clientPb = createUbProcessBuilder(configPath, controlPort,
                clientCommand[0], tail(clientCommand));
            String clientUbLogPath = getUbLogPath(clientPb);
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = combineOutputWithUbLog(clientOutput.getOutput(), clientUbLogPath);
            requireContains(clientLog, clientSuccessToken, "client");

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = combineOutputWithUbLog(serverOutput.getOutput(), serverUbLogPath);
            requireContains(serverLog, serverSuccessToken, "server");
            server = null;
            return new ScenarioLogs(clientLog, serverLog);
        } finally {
            destroyIfAlive(server);
        }
    }

    public static ProcessBuilder createProcessBuilder(boolean enableUb, String configPath,
                                                      int controlPort, String mainClass,
                                                      String ... args)
        throws Exception {
        List<String> command = new ArrayList<String>();
        String ubLogPath = null;
        if (enableUb) {
            command.addAll(createUbOptions(configPath, controlPort, mainClass));
            ubLogPath = extractUbLogPath(command.get(4));
        }

        command.add(mainClass);
        for (String arg : args) {
            command.add(arg);
        }

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(
            true, command.toArray(new String[command.size()]));
        if (enableUb) {
            pb.environment().put("UBSOCKET_LOG_PATH", ubLogPath);
        }
        return pb;
    }

    public static String getUbLogPath(ProcessBuilder pb) {
        return pb.environment().get("UBSOCKET_LOG_PATH");
    }

    public static String combineOutputWithUbLog(String output, String ubLogPath)
        throws IOException {
        // VM tty output is not always captured consistently by OutputAnalyzer,
        // so socket tests read the redirected UB log file and merge it here.
        String ubLog = readText(ubLogPath);
        if (ubLog.isEmpty()) {
            return output;
        }
        if (output == null || output.isEmpty()) {
            return ubLog;
        }
        return output + "\n" + ubLog;
    }

    public static String combinedOutput(OutputAnalyzer output, ProcessBuilder pb)
        throws IOException {
        return combineOutputWithUbLog(output.getOutput(), getUbLogPath(pb));
    }

    public static void destroyIfAlive(Process process) throws InterruptedException {
        if (process != null && process.isAlive()) {
            process.destroy();
            if (!process.waitFor(5, TimeUnit.SECONDS)) {
                process.destroyForcibly();
                process.waitFor();
            }
        }
    }

    public static long getPid(Process process) throws Exception {
        Field pidField = process.getClass().getDeclaredField("pid");
        pidField.setAccessible(true);
        Object value = pidField.get(process);
        if (value instanceof Number) {
            return ((Number)value).longValue();
        }
        throw new IllegalStateException("Unexpected pid field: " + value);
    }

    public static long countOpenFileDescriptors(long pid) throws IOException {
        Path fdDir = Paths.get("/proc", Long.toString(pid), "fd");
        try (Stream<Path> paths = Files.list(fdDir)) {
            return paths.count();
        }
    }

    public static long countOpenTcpSockets(long pid) throws IOException {
        Set<String> tcpInodes = new HashSet<String>();
        loadTcpSocketInodes(Paths.get("/proc", Long.toString(pid), "net", "tcp"), tcpInodes);
        loadTcpSocketInodes(Paths.get("/proc", Long.toString(pid), "net", "tcp6"), tcpInodes);

        long count = 0;
        Path fdDir = Paths.get("/proc", Long.toString(pid), "fd");
        try (Stream<Path> paths = Files.list(fdDir)) {
            for (Path path : (Iterable<Path>)paths::iterator) {
                String inode = readSocketInode(path);
                if (inode != null && tcpInodes.contains(inode)) {
                    count++;
                }
            }
        }
        return count;
    }

    public static boolean containsLineWithAllTokens(String text, String ... tokens) {
        return countLinesWithAllTokens(text, tokens) > 0;
    }

    public static int countLinesWithAllTokens(String text, String ... tokens) {
        int count = 0;
        for (String line : text.split("\\R")) {
            boolean matched = true;
            for (String token : tokens) {
                if (!line.contains(token)) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                count++;
            }
        }
        return count;
    }

    public static int countBindSuccesses(String text, boolean serverSide) {
        return countLinesWithAllTokens(
            text,
            serverSide ? "attach server finished" : "attach client finished",
            "bind success");
    }

    public static boolean containsBindSuccess(String text, boolean serverSide) {
        return countBindSuccesses(text, serverSide) > 0;
    }

    public static boolean containsAttachFinished(String text, boolean serverSide) {
        return containsLineWithAllTokens(
            text,
            serverSide ? "attach server finished" : "attach client finished");
    }

    public static void assertBindSuccesses(String text, boolean serverSide, int expected,
                                           String message) {
        int actual = countBindSuccesses(text, serverSide);
        if (actual != expected) {
            throw new RuntimeException(message + "\nexpected=" + expected + ", actual=" + actual
                + "\n" + text);
        }
    }

    public static void assertNoBindSuccess(String text, boolean serverSide, String message) {
        if (containsBindSuccess(text, serverSide)) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    public static boolean containsFallback(String text) {
        return text.contains("fallback to TCP");
    }

    public static int countFallbacks(String text, boolean serverSide) {
        return countLinesWithAllTokens(
            text,
            serverSide ? "attach server finished" : "attach client finished",
            "fallback to TCP");
    }

    public static void assertNoFallback(String text, String message) {
        if (containsFallback(text)) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    public static void assertFallback(String text, String message) {
        if (!containsFallback(text)) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    public static boolean containsDataFallback(String text) {
        return text.contains("send DATA_FALLBACK frame")
            || text.contains("recv DATA_FALLBACK frame");
    }

    public static boolean containsHeartbeat(String text) {
        return text.contains("send HEARTBEAT frame")
            || text.contains("recv HEARTBEAT frame");
    }

    public static void assertDataFallback(String text, String message) {
        if (!containsDataFallback(text)) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    public static void assertHeartbeat(String text, String message) {
        if (!containsHeartbeat(text)) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    public static void assertNoHeartbeat(String text, String message) {
        if (containsHeartbeat(text)) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    public static void assertTransferInfoLogs(String text, String message) {
        if (!text.contains("write_data success")) {
            throw new RuntimeException(message + ": missing write_data info log\n" + text);
        }
        if (!text.contains("read_data requested=")) {
            throw new RuntimeException(message + ": missing read_data info log\n" + text);
        }
    }

    public static void assertNoPayloadPrefixLog(String text, String message) {
        String token = "payload_prefix=";
        if (text.contains(token)) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    public static boolean containsRefCount(String text, int refCount) {
        return text.contains("ref=" + refCount) || text.contains("ref " + refCount);
    }

    public static void assertRefCount(String text, int refCount, String message) {
        if (!containsRefCount(text, refCount)) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    public static boolean containsDataTransferSuccess(String text) {
        return text.contains("Data sent successfully") || text.contains("hash verified");
    }

    public static void assertDataTransferSuccess(String text, String message) {
        if (!containsDataTransferSuccess(text)) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    public static boolean containsControlBindFailure(String text) {
        return text.contains("Address already in use") || text.contains("start agent failed");
    }

    public static void assertNoVmCrash(String text, String message) {
        if (text.contains("hs_err_pid") || text.contains("SIGSEGV") ||
                text.contains("Internal Error") || text.contains("guarantee(")) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    public static void assertNoAbnormalFdCleanup(String text, String message) {
        if (text.contains("abnormal fds")) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    public static void assertNoMemoryOperationFailure(String text, String message) {
        if (text.contains("munmap failed") || text.contains("mmap failed")) {
            throw new RuntimeException(message + "\n" + text);
        }
    }

    private static void loadTcpSocketInodes(Path path, Set<String> inodes) throws IOException {
        for (String line : Files.readAllLines(path)) {
            String trimmed = line.trim();
            if (trimmed.isEmpty() || trimmed.startsWith("sl")) {
                continue;
            }
            String[] fields = trimmed.split("\\s+");
            if (fields.length > 9) {
                inodes.add(fields[9]);
            }
        }
    }

    private static String readSocketInode(Path fdPath) {
        try {
            String target = Files.readSymbolicLink(fdPath).toString();
            if (target.startsWith("socket:[") && target.endsWith("]")) {
                return target.substring(8, target.length() - 1);
            }
            return null;
        } catch (IOException e) {
            return null;
        }
    }

    private static String[] prependMainClass(String mainClass, String ... args) {
        String[] command = new String[args.length + 1];
        command[0] = mainClass;
        System.arraycopy(args, 0, command, 1, args.length);
        return command;
    }

    private static List<String> createUbOptions(String configPath, int controlPort,
                                                String mainClass) {
        String ubLogPath = nextUbLogPath(mainClass);
        List<String> command = new ArrayList<String>();
        command.add("-XX:+UnlockExperimentalVMOptions");
        command.add("-XX:+UseUBSocket");
        command.add("-XX:UBSocketConf=" + configPath);
        command.add("-XX:UBSocketPort=" + controlPort);
        command.add("-XX:UBLog=path=" + ubLogPath + ",socket=debug");
        return command;
    }

    private static String[] tail(String[] command) {
        String[] tail = new String[command.length - 1];
        System.arraycopy(command, 1, tail, 0, tail.length);
        return tail;
    }

    private static String extractUbLogPath(String ubLogOption) {
        String prefix = "-XX:UBLog=path=";
        if (!ubLogOption.startsWith(prefix)) {
            return null;
        }
        String value = ubLogOption.substring(prefix.length());
        int commaIdx = value.indexOf(',');
        return commaIdx >= 0 ? value.substring(0, commaIdx) : value;
    }

    private static void requireContains(String text, String token, String side) {
        if (token != null && !token.isEmpty() && !text.contains(token)) {
            throw new RuntimeException("Missing " + side + " token: " + token + "\n" + text);
        }
    }

    private static String readText(String path) throws IOException {
        if (path == null) {
            return "";
        }
        Path file = Paths.get(path);
        if (!Files.exists(file, new LinkOption[0])) {
            return "";
        }
        return new String(Files.readAllBytes(file), StandardCharsets.UTF_8);
    }

    public static String nextUbLogPath(String mainClass) {
        return Paths.get(System.getProperty("test.classes", "."),
                         "ublog-" + mainClass + "-" + System.currentTimeMillis()
                         + "-" + System.nanoTime() + "-"
                         + UB_LOG_FILE_ID.incrementAndGet() + ".log")
                    .toString();
    }
}
