/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE THIS COPYRIGHT NOTICES OR THIS FILE HEADER.
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
 * @summary Test UB socket behavior when peer closes, becomes unavailable, or crashes
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm PeerCloseTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class PeerCloseTest {
    private static final int DATA_SIZE = 64 * 1024;

    public static void main(String[] args) throws Exception {
        testEarlyCloseBeforeAttach();
        testServerUnavailable();
        testPeerCrashDuringAttach();
        testPeerCrashAfterAttach();

        System.out.println("=== All peer failure tests PASSED ===");
    }

    private static void testEarlyCloseBeforeAttach() throws Exception {
        String testName = "EarlyCloseBeforeAttach";
        System.out.println("Testing: " + testName);

        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort,
                "NIOScenarioServer", "earlyClose",
                String.valueOf(dataPort)
            );
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort,
                "NIOScenarioClient", "peerClose",
                "localhost",
                String.valueOf(dataPort)
            );

            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);

            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);

            if (!clientLog.contains("EXPECTED_PEER_CLOSE")) {
                throw new RuntimeException(testName + ": client did not handle early close\n" + clientLog);
            }

            if (clientLog.contains("UNEXPECTED_SUCCESS")) {
                throw new RuntimeException(testName + ": peer close path unexpectedly succeeded\n" + clientLog);
            }

            SocketTestSupport.assertNoVmCrash(
                clientLog, testName + ": client crashed on early close");

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);

            if (!serverLog.contains("Accepted client, closing immediately")) {
                throw new RuntimeException(testName + ": server should accept and close immediately\n" + serverLog);
            }

            System.out.println(testName + " PASSED");

        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void testServerUnavailable() throws Exception {
        String testName = "ServerUnavailable";
        System.out.println("Testing: " + testName);

        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort,
                "NIOScenarioServer", "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "1"
            );
            server = serverPb.start();
            Thread.sleep(500L);

            long serverPid = SocketTestSupport.getPid(server);
            Runtime.getRuntime().exec("kill " + serverPid);
            server.waitFor(5, java.util.concurrent.TimeUnit.SECONDS);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort,
                "NIOScenarioClient", "basic",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                testName + "-Client"
            );

            Process client = clientPb.start();
            boolean exited = client.waitFor(15, java.util.concurrent.TimeUnit.SECONDS);
            if (!exited) {
                client.destroy();
                throw new RuntimeException(testName + ": client hung when server not available");
            }

            OutputAnalyzer clientOutput = new OutputAnalyzer(client);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);

            SocketTestSupport.assertNoVmCrash(clientLog, testName + ": client crashed");

            System.out.println(testName + ": client handled server unavailable gracefully");

        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void testPeerCrashDuringAttach() throws Exception {
        String testName = "PeerCrashDuringAttach";
        System.out.println("Testing: " + testName);

        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort,
                "NIOScenarioServer", "delayedAccept",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "1",
                "5000"
            );
            server = serverPb.start();
            Thread.sleep(500L);

            long serverPid = SocketTestSupport.getPid(server);
            Runtime.getRuntime().exec("kill -9 " + serverPid);
            Thread.sleep(100L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort,
                "NIOScenarioClient", "basic",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                testName + "-Client"
            );

            Process client = clientPb.start();
            OutputAnalyzer clientOutput = new OutputAnalyzer(client);

            boolean clientExited = client.waitFor(10, java.util.concurrent.TimeUnit.SECONDS);
            if (!clientExited) {
                client.destroy();
                throw new RuntimeException(testName + ": client hung when server crashed during attach");
            }

            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            SocketTestSupport.assertNoVmCrash(
                clientLog, testName + ": client crashed instead of handling peer failure");

            System.out.println(testName + ": client handled peer crash gracefully");

        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void testPeerCrashAfterAttach() throws Exception {
        String testName = "PeerCrashAfterAttach";
        System.out.println("Testing: " + testName);

        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        Process server = null;
        Process client = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort,
                "NIOScenarioServer", "selector",
                String.valueOf(dataPort),
                String.valueOf(1024 * 1024),
                "1"
            );
            server = serverPb.start();
            Thread.sleep(1000L);

            long serverPid = SocketTestSupport.getPid(server);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort,
                "NIOScenarioClient", "basic",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(1024 * 1024),
                testName + "-Client"
            );
            client = clientPb.start();
            Thread.sleep(500L);

            Runtime.getRuntime().exec("kill -9 " + serverPid);

            OutputAnalyzer clientOutput = new OutputAnalyzer(client);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);

            boolean attachSucceeded = SocketTestSupport.containsBindSuccess(clientLog, false);
            SocketTestSupport.assertNoVmCrash(
                clientLog, testName + ": client crashed after peer died");

            System.out.println(testName + ": attach=" + attachSucceeded
                + ", client handled gracefully");

        } finally {
            SocketTestSupport.destroyIfAlive(server);
            SocketTestSupport.destroyIfAlive(client);
        }
    }
}
