/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE THIS FILE HEADER.
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
 * @summary Test attach timeout fallback and cleanup
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm AttachTimeoutTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class AttachTimeoutTest {
    private static final int DATA_SIZE = 131072;
    private static final int CLIENT_COUNT = 1;
    private static final int ACCEPT_DELAY_MS = 3000;
    private static final int RESOURCE_CLIENT_COUNT = 3;
    private static final int HOLD_AFTER_COMPLETE_MS = 4000;
    private static final long FD_SETTLE_WAIT_MS = 800L;
    private static final int CONTROL_LISTENER_TCP_SOCKETS = 1;

    public static void main(String[] args) throws Exception {
        testAttachTimeout(300L, "AttachTimeoutClient",
            "Attach timeout should fallback");
        testTimeoutFallbackResourceCleanup();
    }

    private static void testAttachTimeout(long timeoutMs, String clientId,
                                          String message) throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        SocketTestSupport.ScenarioLogs logs = SocketTestSupport.runUbScenario(
            configPath,
            controlPort,
            timeoutMs,
            "Data sent successfully",
            "All " + CLIENT_COUNT + " clients completed successfully",
            new String[] {
                "NIOScenarioServer", "delayedAccept",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(CLIENT_COUNT),
                String.valueOf(ACCEPT_DELAY_MS)
            },
            new String[] {
                "NIOScenarioClient", "basic", "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                clientId
            }
        );
        SocketTestSupport.assertFallback(logs.clientLog, message);
        SocketTestSupport.assertNoBindSuccess(
            logs.clientLog, false, message + " should not attach successfully");
        SocketTestSupport.assertNoBindSuccess(
            logs.serverLog, true, message + " server should not bind UB");

        System.out.println("=== " + message + " PASSED ===");
    }

    private static void testTimeoutFallbackResourceCleanup() throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
            configPath,
            controlPort,
            "NIOScenarioServer",
            "delayedAccept",
            String.valueOf(dataPort),
            String.valueOf(DATA_SIZE),
            String.valueOf(RESOURCE_CLIENT_COUNT),
            String.valueOf(ACCEPT_DELAY_MS),
            String.valueOf(HOLD_AFTER_COMPLETE_MS)
        );
        Process server = serverPb.start();
        try {
            Thread.sleep(500L);

            long serverPid = SocketTestSupport.getPid(server);
            long baselineTcpSockets = SocketTestSupport.countOpenTcpSockets(serverPid);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioClient",
                "parallel",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(RESOURCE_CLIENT_COUNT),
                "TimeoutResourceClient"
            );
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains("PARALLEL_CLIENT_OK")) {
                throw new RuntimeException("Timeout resource client did not complete\n" + clientLog);
            }

            int fallbacks = SocketTestSupport.countFallbacks(clientLog, false);
            if (fallbacks != RESOURCE_CLIENT_COUNT) {
                throw new RuntimeException("Expected all clients to fallback after timeout, actual="
                    + fallbacks + "\n" + clientLog);
            }

            Thread.sleep(FD_SETTLE_WAIT_MS);
            if (!server.isAlive()) {
                throw new RuntimeException("Server exited before fd cleanup could be observed");
            }

            long currentTcpSockets = SocketTestSupport.countOpenTcpSockets(serverPid);
            long expectedTcpSockets = baselineTcpSockets + CONTROL_LISTENER_TCP_SOCKETS;
            if (currentTcpSockets != expectedTcpSockets) {
                throw new RuntimeException("Attach-timeout cleanup leaked server TCP sockets: baseline="
                    + baselineTcpSockets + ", expected=" + expectedTcpSockets
                    + ", after=" + currentTcpSockets);
            }

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("All " + RESOURCE_CLIENT_COUNT + " clients completed successfully")) {
                throw new RuntimeException("Timeout resource server did not complete\n" + serverLog);
            }
            SocketTestSupport.assertNoAbnormalFdCleanup(
                serverLog, "Server reported abnormal fd cleanup");
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }
}
