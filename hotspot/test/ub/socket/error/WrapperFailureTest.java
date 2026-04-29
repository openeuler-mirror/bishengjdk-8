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
 * @summary Test UB wrapper backing file failures disable or fallback cleanly
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm WrapperFailureTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

public class WrapperFailureTest {
    private static final int DATA_SIZE = 128 * 1024;
    private static final Path UB_BACKING_DIR = Paths.get("/tmp/ubwrapper");

    public static void main(String[] args) throws Exception {
        testInitMallocFailureDisablesUBSocket();
        testRemoteMmapFailureFallsBackToTcp();
    }

    private static void testInitMallocFailureDisablesUBSocket() throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        Path backup = backupBackingDir();
        Process server = null;
        try {
            Files.write(UB_BACKING_DIR, new byte[] { 'x' });

            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "1"
            );
            server = serverPb.start();
            Thread.sleep(1000L);

            restoreBackingDir(backup);
            backup = null;

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioClient",
                "basic",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "InitMallocFailureClient"
            );
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            SocketTestSupport.assertDataTransferSuccess(
                clientLog, "init malloc failure client should complete over TCP");
            SocketTestSupport.assertFallback(
                clientLog, "client should fallback when server failed UBSocket init");
            SocketTestSupport.assertNoBindSuccess(
                clientLog, false, "client should not bind UB when server init malloc failed");

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("All 1 clients completed successfully")) {
                throw new RuntimeException("init malloc failure server did not complete\n"
                    + serverLog);
            }
            mustContain(serverLog, "open failed", "backing-file malloc failure log");
            mustContain(serverLog, "init malloc failed", "UBSocket init malloc failure log");
            SocketTestSupport.assertNoBindSuccess(
                serverLog, true, "server should not bind UB after init malloc failure");
            SocketTestSupport.assertNoVmCrash(
                clientLog + "\n" + serverLog,
                "init malloc failure fallback should not crash VM");
            server = null;
        } finally {
            SocketTestSupport.destroyIfAlive(server);
            if (backup != null) {
                restoreBackingDir(backup);
            }
        }
    }

    private static void testRemoteMmapFailureFallsBackToTcp() throws Exception {
        Path backup = backupBackingDir();
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        Process server = null;
        try {
            Files.createDirectories(UB_BACKING_DIR);
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "1"
            );
            server = serverPb.start();
            Thread.sleep(1000L);
            List<Path> serverBackingFiles = listBackingFiles();
            if (serverBackingFiles.size() != 1) {
                throw new RuntimeException("Expected one server backing file, found "
                    + serverBackingFiles);
            }
            Files.delete(serverBackingFiles.get(0));

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioClient",
                "basic",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "RemoteMmapFailureClient"
            );
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            SocketTestSupport.assertDataTransferSuccess(
                clientLog, "remote mmap failure client should complete over TCP");
            SocketTestSupport.assertFallback(
                clientLog, "remote mmap failure client should fallback to TCP");
            SocketTestSupport.assertNoBindSuccess(
                clientLog, false, "remote mmap failure client should not bind UB");

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("All 1 clients completed successfully")) {
                throw new RuntimeException("remote mmap failure server did not complete\n"
                    + serverLog);
            }
            String combinedLog = clientLog + "\n" + serverLog;
            mustContain(combinedLog, "mmap failed", "remote mmap failure log");
            SocketTestSupport.assertNoBindSuccess(
                serverLog, true, "remote mmap failure server should not bind UB");
            SocketTestSupport.assertNoVmCrash(
                combinedLog, "remote mmap failure should not crash VM");
            server = null;
        } finally {
            SocketTestSupport.destroyIfAlive(server);
            restoreBackingDir(backup);
        }
    }

    private static Path backupBackingDir() throws Exception {
        Path backup = Paths.get("/tmp/ubwrapper-WrapperFailureTest-" + System.nanoTime());
        if (Files.exists(UB_BACKING_DIR)) {
            Files.move(UB_BACKING_DIR, backup);
        }
        return backup;
    }

    private static void restoreBackingDir(Path backup) throws Exception {
        deleteRecursively(UB_BACKING_DIR);
        if (Files.exists(backup)) {
            Files.move(backup, UB_BACKING_DIR);
        }
    }

    private static List<Path> listBackingFiles() throws Exception {
        List<Path> files = new ArrayList<Path>();
        for (Path file : Files.newDirectoryStream(UB_BACKING_DIR)) {
            if (Files.isRegularFile(file)) {
                files.add(file);
            }
        }
        return files;
    }

    private static void deleteRecursively(Path path) throws Exception {
        if (!Files.exists(path)) {
            return;
        }
        if (Files.isDirectory(path)) {
            for (Path child : Files.newDirectoryStream(path)) {
                deleteRecursively(child);
            }
        }
        Files.delete(path);
    }

    private static void mustContain(String text, String token, String message) {
        if (!text.contains(token)) {
            throw new RuntimeException("Missing " + message + ": " + token + "\n" + text);
        }
    }
}
