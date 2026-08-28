/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

/*
 * @test
 * @summary Verify jcmd UBSocket.conf replaces future-connection policy only
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/UBSocketConfigReloadMain.java
 * @run main/othervm ConfigReloadTest
 */

import com.oracle.java.testlibrary.JDKToolFinder;
import com.oracle.java.testlibrary.OutputAnalyzer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.concurrent.TimeUnit;

public class ConfigReloadTest {
    private static final long TIMEOUT_SECONDS = 30L;

    public static void main(String[] args) throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        Path controlDir = Files.createTempDirectory("ubsocket-conf-reload-control-");
        String mapped = mappedConfig("UBSocketReloadInitial.conf", dataPort, controlPort);
        String unmapped = unmappedConfig("UBSocketReloadUnmapped.conf");
        Path invalid = controlDir.resolve("invalid.conf");
        Files.write(invalid,
            ("#stack\n" +
             "sun/nio/ch/SocketChannelImpl.connect\n" +
             "#address\n" +
             "remote.mapping=broken\n").getBytes(StandardCharsets.UTF_8));

        Process target = null;
        try {
            ProcessBuilder pb = SocketTestSupport.createUbProcessBuilderWithVmOptions(
                mapped,
                controlPort,
                new String[] { "-XX:UBSocketAttach=limit", "-XX:UBSocketProfile=1" },
                "UBSocketConfigReloadMain",
                String.valueOf(dataPort), controlDir.toString());
            String ubLog = SocketTestSupport.getUbLogPath(pb);
            target = pb.start();
            waitFor(controlDir.resolve("persistent-ready"));
            long pid = SocketTestSupport.getPid(target);

            // Hold a reload inside file parsing. Registrations in this window
            // must use FALLBACK, not wait and not count as stable mapping skips.
            Path fifo = controlDir.resolve("blocked-reload.conf");
            OutputAnalyzer mkfifo = new OutputAnalyzer(
                new ProcessBuilder("mkfifo", fifo.toString()).start());
            mkfifo.shouldHaveExitValue(0);
            Process blockedReload = startJcmd(pid, fifo.toString());
            waitForLog(ubLog, "configuration reload begin path=" + fifo);
            command(controlDir, 1, "new");
            Files.write(fifo, Files.readAllBytes(java.nio.file.Paths.get(mapped)),
                        StandardOpenOption.WRITE);
            if (!blockedReload.waitFor(TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                blockedReload.destroyForcibly();
                throw new RuntimeException("blocked jcmd UBSocket.conf did not finish");
            }
            OutputAnalyzer blockedOutput = new OutputAnalyzer(blockedReload);
            blockedOutput.shouldHaveExitValue(0);
            blockedOutput.shouldContain("UBSocket configuration reloaded successfully");

            OutputAnalyzer success = runJcmd(pid, unmapped);
            success.shouldHaveExitValue(0);
            success.shouldContain("UBSocket configuration reloaded successfully");
            success.shouldContain("path=" + unmapped);
            success.shouldContain("#stack");
            success.shouldContain("sun/nio/ch/SocketChannelImpl.connect");
            success.shouldContain("sun/nio/ch/SocketChannelImpl.finishConnect");
            success.shouldContain("sun/nio/ch/ServerSocketChannelImpl.accept");
            success.shouldContain("#address");
            if (success.getOutput().contains("remote.mapping=") ||
                success.getOutput().contains("self.port=")) {
                throw new RuntimeException("Unmapped effective config printed stale entries\n"
                    + success.getOutput());
            }

            command(controlDir, 2, "existing");
            command(controlDir, 3, "new");

            OutputAnalyzer failure = runJcmd(pid, invalid.toString());
            failure.shouldHaveExitValue(0);
            failure.shouldContain("ERROR: UBSocket configuration reload failed");
            failure.shouldContain("current configuration unchanged");
            failure.shouldContain("invalid configuration entry");

            command(controlDir, 4, "new");
            Files.write(controlDir.resolve("stop"), new byte[] { 1 },
                        StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);

            OutputAnalyzer targetOutput = new OutputAnalyzer(target);
            targetOutput.shouldHaveExitValue(0);
            targetOutput.shouldContain("RELOAD_MAIN_OK");
            String combined = SocketTestSupport.combineOutputWithUbLog(
                targetOutput.getOutput(), ubLog);
            SocketTestSupport.assertBindSuccesses(
                combined, false, 1, "The original long connection must attach once");
            SocketTestSupport.assertBindSuccesses(
                combined, true, 1, "The original accepted connection must attach once");
            long fallback = SocketTestSupport.profileCount(combined, "ub_attach_fallback");
            if (fallback != 2L) {
                throw new RuntimeException("Expected reload-window fallback on both endpoints, actual="
                    + fallback + "\n" + combined);
            }
            long skips = SocketTestSupport.profileCount(combined, "ub_attach_skip");
            if (skips != 4L) {
                throw new RuntimeException("Expected two later TCP connections to skip on both sides, "
                    + "actual=" + skips + "\n" + combined);
            }
            if (!combined.contains("configuration reload success path=" + unmapped)) {
                throw new RuntimeException("Missing successful reload log\n" + combined);
            }
            if (!combined.contains("configuration reload failed path=" + invalid)) {
                throw new RuntimeException("Missing failed reload log\n" + combined);
            }
            target = null;
        } finally {
            SocketTestSupport.destroyIfAlive(target);
        }
    }

    private static Process startJcmd(long pid, String path) throws Exception {
        return new ProcessBuilder(
            JDKToolFinder.getJDKTool("jcmd"), Long.toString(pid), "UBSocket.conf", path)
            .start();
    }

    private static OutputAnalyzer runJcmd(long pid, String path) throws Exception {
        Process process = startJcmd(pid, path);
        if (!process.waitFor(TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
            process.destroyForcibly();
            throw new RuntimeException("jcmd UBSocket.conf timed out");
        }
        return new OutputAnalyzer(process);
    }

    private static void command(Path dir, int id, String value) throws Exception {
        Files.write(dir.resolve("command-" + id), value.getBytes(StandardCharsets.UTF_8),
                    StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);
        waitFor(dir.resolve("done-" + id));
    }

    private static void waitForLog(String path, String token) throws Exception {
        Path log = java.nio.file.Paths.get(path);
        long deadline = System.currentTimeMillis() + TimeUnit.SECONDS.toMillis(TIMEOUT_SECONDS);
        while (System.currentTimeMillis() < deadline) {
            if (Files.exists(log) &&
                new String(Files.readAllBytes(log), StandardCharsets.UTF_8).contains(token)) {
                return;
            }
            Thread.sleep(20L);
        }
        throw new RuntimeException("timeout waiting for log token: " + token);
    }

    private static void waitFor(Path path) throws Exception {
        long deadline = System.currentTimeMillis() + TimeUnit.SECONDS.toMillis(TIMEOUT_SECONDS);
        while (!Files.exists(path)) {
            if (System.currentTimeMillis() >= deadline) {
                throw new RuntimeException("timeout waiting for " + path);
            }
            Thread.sleep(20L);
        }
    }

    private static String mappedConfig(String name, int dataPort, int controlPort)
        throws Exception {
        return SocketTestConfig.writeConfig(name,
            stackText() +
            "#address\n" +
            "self.ip=127.0.0.1 -> *\n" +
            "self.port=" + dataPort + " -> " + dataPort + "\n" +
            "remote.mapping=127.0.0.1:" + dataPort + " -> 127.0.0.1:"
                + controlPort + "\n");
    }

    private static String unmappedConfig(String name) throws Exception {
        return SocketTestConfig.writeConfig(name, stackText() + "#address\n");
    }

    private static String stackText() {
        return "#stack\n" +
               "sun/nio/ch/SocketChannelImpl.connect\n" +
               "sun/nio/ch/SocketChannelImpl.finishConnect\n" +
               "sun/nio/ch/ServerSocketChannelImpl.accept\n";
    }
}
