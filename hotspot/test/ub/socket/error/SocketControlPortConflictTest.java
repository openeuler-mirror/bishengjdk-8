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
 * @summary Test UB socket behavior when control port is occupied.
 *          Scenario 1: Non-UB process occupies control port first, then UB server/client start.
 *                      Expected: UB server bind fails, client fallback to TCP.
 *          Scenario 2: Two UB servers race for same control port.
 *                      Expected: One succeeds, the other fails, client of failed server fallback to TCP.
 *          IMPORTANT: UBSocketAttachAgent is lazy-started - control port binding is triggered
 *          by the first client connection, not at JVM startup.
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm SocketControlPortConflictTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import java.net.ServerSocket;

public class SocketControlPortConflictTest {
    private static final int DATA_SIZE = 64 * 1024;

    public static void main(String[] args) throws Exception {
        // Scenario 1: Non-UB process occupies control port first
        testNonUBProcessOccupiesPort();

        // Scenario 2: Two UB servers race for same control port
        testUBServerPortRace();
    }

    /**
     * Scenario 1: A non-UB process (plain TCP socket) occupies the control port first.
     * Then UB server and client start with same control port.
     * Expected: UB server bind fails, client fallback to TCP and completes data transfer.
     */
    private static void testNonUBProcessOccupiesPort() throws Exception {
        System.out.println("\n=== Scenario 1: Non-UB process occupies control port ===\n");

        int controlPort = SocketTestSupport.findFreePort();
        int dataPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        // Non-UB process occupies control port (plain ServerSocket)
        ServerSocket portHogger = new ServerSocket(controlPort);
        System.out.println("Non-UB process bound control port " + controlPort);

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort, "NIOScenarioServer", "selector",
                String.valueOf(dataPort), String.valueOf(DATA_SIZE), "1");
            server = serverPb.start();
            Thread.sleep(500L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort, "NIOScenarioClient", "basic", "localhost",
                String.valueOf(dataPort), String.valueOf(DATA_SIZE), "Client");
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);

            System.out.println("=== Server log ===");
            System.out.println(serverLog);
            System.out.println("=== Client log ===");
            System.out.println(clientLog);

            SocketTestSupport.assertDataTransferSuccess(
                clientLog, "Scenario1: Client data transfer failed");

            SocketTestSupport.assertNoBindSuccess(
                serverLog, true, "Scenario1: Server should NOT bind control port");

            // Server should have bind failure
            boolean serverFailed = SocketTestSupport.containsControlBindFailure(serverLog)
                || !SocketTestSupport.containsAttachFinished(serverLog, true);
            if (!serverFailed) {
                throw new RuntimeException("Scenario1: Server should fail to bind\n" + serverLog);
            }

            SocketTestSupport.assertFallback(clientLog, "Scenario1: Client should fallback to TCP");

            System.out.println("=== Scenario 1 PASSED ===");
            System.out.println("- Non-UB process held control port: OK");
            System.out.println("- UB server bind failed: OK");
            System.out.println("- Client fallback to TCP: OK");
            System.out.println("- Data transfer completed: OK");

        } finally {
            portHogger.close();
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    /**
     * Scenario 2: Two UB servers race for same control port via concurrent client connections.
     * Expected: One server wins, the other fails with EADDRINUSE.
     */
    private static void testUBServerPortRace() throws Exception {
        System.out.println("\n=== Scenario 2: Two UB servers race for same control port ===\n");

        int controlPort = SocketTestSupport.findFreePort();
        int dataPortA = SocketTestSupport.findFreePort();
        int dataPortB = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        Process serverA = null;
        Process serverB = null;
        try {
            ProcessBuilder serverAPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort, "NIOScenarioServer", "selector",
                String.valueOf(dataPortA), String.valueOf(DATA_SIZE), "1");
            ProcessBuilder serverBPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort, "NIOScenarioServer", "selector",
                String.valueOf(dataPortB), String.valueOf(DATA_SIZE), "1");

            serverA = serverAPb.start();
            serverB = serverBPb.start();
            Thread.sleep(500L);

            ProcessBuilder clientAPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort, "NIOScenarioClient", "basic", "localhost",
                String.valueOf(dataPortA), String.valueOf(DATA_SIZE), "ClientA");
            ProcessBuilder clientBPb = SocketTestSupport.createUbProcessBuilder(
                configPath, controlPort, "NIOScenarioClient", "basic", "localhost",
                String.valueOf(dataPortB), String.valueOf(DATA_SIZE), "ClientB");

            // Start both clients concurrently (NOT sequential via OutputAnalyzer)
            Process clientAProc = clientAPb.start();
            Process clientBProc = clientBPb.start();

            // Now wait for both clients to complete
            OutputAnalyzer clientAOutput = new OutputAnalyzer(clientAProc);
            clientAOutput.shouldHaveExitValue(0);
            String clientALog = SocketTestSupport.combinedOutput(clientAOutput, clientAPb);

            OutputAnalyzer clientBOutput = new OutputAnalyzer(clientBProc);
            clientBOutput.shouldHaveExitValue(0);
            String clientBLog = SocketTestSupport.combinedOutput(clientBOutput, clientBPb);

            OutputAnalyzer serverAOutput = new OutputAnalyzer(serverA);
            serverAOutput.shouldHaveExitValue(0);
            String serverALog = SocketTestSupport.combinedOutput(serverAOutput, serverAPb);

            OutputAnalyzer serverBOutput = new OutputAnalyzer(serverB);
            serverBOutput.shouldHaveExitValue(0);
            String serverBLog = SocketTestSupport.combinedOutput(serverBOutput, serverBPb);

            System.out.println("=== ServerA log ===");
            System.out.println(serverALog);
            System.out.println("=== ServerB log ===");
            System.out.println(serverBLog);
            System.out.println("=== ClientA log ===");
            System.out.println(clientALog);
            System.out.println("=== ClientB log ===");
            System.out.println(clientBLog);

            SocketTestSupport.assertDataTransferSuccess(
                clientALog, "Scenario2 ClientA: data transfer failed");
            SocketTestSupport.assertDataTransferSuccess(
                clientBLog, "Scenario2 ClientB: data transfer failed");

            System.out.println("=== Both clients data transfer PASSED ===");

            boolean serverABindSuccess = SocketTestSupport.containsBindSuccess(serverALog, true);
            boolean serverBBindSuccess = SocketTestSupport.containsBindSuccess(serverBLog, true);
            boolean serverABindFailed = SocketTestSupport.containsControlBindFailure(serverALog)
                || !SocketTestSupport.containsAttachFinished(serverALog, true);
            boolean serverBBindFailed = SocketTestSupport.containsControlBindFailure(serverBLog)
                || !SocketTestSupport.containsAttachFinished(serverBLog, true);

            boolean clientAUB = SocketTestSupport.containsBindSuccess(clientALog, false)
                && !SocketTestSupport.containsFallback(clientALog);
            boolean clientBUB = SocketTestSupport.containsBindSuccess(clientBLog, false)
                && !SocketTestSupport.containsFallback(clientBLog);
            boolean clientATCP = SocketTestSupport.containsFallback(clientALog);
            boolean clientBTCP = SocketTestSupport.containsFallback(clientBLog);

            boolean scenario1 = serverABindSuccess && serverBBindFailed && clientAUB && clientBTCP;
            boolean scenario2 = serverBBindSuccess && serverABindFailed && clientBUB && clientATCP;
            boolean buggyScenario = serverABindSuccess && serverBBindSuccess;

            if (scenario1) {
                System.out.println("=== Scenario 2 PASSED (ServerA won) ===");
                System.out.println("- ServerA bound control port: OK");
                System.out.println("- ServerB failed to bind: OK");
                System.out.println("- ClientA used UB: OK");
                System.out.println("- ClientB fallback to TCP: OK");
            } else if (scenario2) {
                System.out.println("=== Scenario 2 PASSED (ServerB won) ===");
                System.out.println("- ServerB bound control port: OK");
                System.out.println("- ServerA failed to bind: OK");
                System.out.println("- ClientB used UB: OK");
                System.out.println("- ClientA fallback to TCP: OK");
            } else if (buggyScenario) {
                throw new RuntimeException("Scenario2 BUG: Both servers bound same control port!");
            } else {
                throw new RuntimeException("Scenario2 unexpected state");
            }

        } finally {
            SocketTestSupport.destroyIfAlive(serverA);
            SocketTestSupport.destroyIfAlive(serverB);
        }
    }
}
