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
 * @summary UBSocket attaches accepted server-side channels at accept time.
 *          Blocking client channels may still stay on TCP, and attached
 *          channels may switch blocking mode without corrupting data.
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=120 BlockingAttachPolicyTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class BlockingAttachPolicyTest {
    private static final int DATA_SIZE = 64 * 1024;
    private static final int CLIENT_COUNT = 1;

    public static void main(String[] args) throws Exception {
        testInitialBlockingChannelDoesNotAttach();
        testAttachedChannelAllowsBlockingMode();
    }

    private static void testInitialBlockingChannelDoesNotAttach() throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(CLIENT_COUNT));
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioClient",
                "basic",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "BlockingPolicy");
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            SocketTestSupport.assertDataTransferSuccess(
                clientLog, "Blocking client should complete on the TCP path");

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("All " + CLIENT_COUNT + " clients completed successfully")) {
                throw new RuntimeException("Blocking policy server did not complete\n" + serverLog);
            }

            String combined = clientLog + "\n" + serverLog;
            SocketTestSupport.assertNoBindSuccess(
                combined, false, "Blocking client SocketChannel must not attach to UBSocket");
            SocketTestSupport.assertNoBindSuccess(
                combined, true, "Accepted blocking SocketChannel must not attach to UBSocket");
            if (combined.contains("Bad message") || combined.contains("HASH_MISMATCH")) {
                throw new RuntimeException("Blocking path exposed UB control/data corruption\n"
                    + combined);
            }
            server = null;
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void testAttachedChannelAllowsBlockingMode() throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "acceptHold",
                String.valueOf(dataPort),
                "1",
                "3000");
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioClient",
                "switchBlockingAfterAttach",
                "localhost",
                String.valueOf(dataPort),
                "SwitchBlockingAfterAttach",
                "250");
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains("BLOCKING_SWITCH_ALLOWED")) {
                throw new RuntimeException("Attached channel did not allow configureBlocking(true)\n"
                    + clientLog);
            }

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            String combined = clientLog + "\n" + serverLog;
            SocketTestSupport.assertBindSuccesses(
                combined, false, 1, "blocking-switch client should attach before mode switch");
            SocketTestSupport.assertBindSuccesses(
                combined, true, 1, "blocking-switch server should attach before mode switch");
            server = null;
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }
}
