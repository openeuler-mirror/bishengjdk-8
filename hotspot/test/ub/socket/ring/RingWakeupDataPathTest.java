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
 * @summary Test the receiver-owned ring data path, sparse wakeup selector
 *          integration, aggressive wakeup mode, and ring-slot fallback.
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=240 RingWakeupDataPathTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class RingWakeupDataPathTest {
    private static final int REQUEST_20B = 20;
    private static final int RESPONSE_2K = 2048;
    private static final int SPARSE_INFLIGHT = 32;
    private static final int SPARSE_FRAMES = 2000;
    private static final int AGGRESSIVE_INFLIGHT = 8;
    private static final int AGGRESSIVE_FRAMES = 512;
    private static final int SLOT_LIMIT_CLIENTS = 9;
    private static final int SLOT_LIMIT_HOLD_MS = 1000;

    public static void main(String[] args) throws Exception {
        testSparseWakeupFrameEcho();
        testAggressiveWakeupFrameEcho();
        testRingSlotLimitFallback();
    }

    private static void testSparseWakeupFrameEcho() throws Exception {
        String combinedLog = runEchoFrameScenario(
            "SparseWakeupEcho",
            new String[] { "-XX:UBSocketProfile=2" },
            SPARSE_INFLIGHT,
            SPARSE_FRAMES);

        assertRingDataPath(combinedLog, "Sparse wakeup echo should use ring data path");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "wakeup_skip_nonempty", 1,
            "Sparse wakeup should coalesce writes while peer ring is non-empty");
        SocketTestSupport.assertProfileCountLessThan(
            combinedLog, "wakeup_send_total", "ring_write_total",
            "Sparse wakeup should send fewer TCP wakeups than ring writes");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "selector_probe_ready", 1,
            "Selector should observe pending UB ring data");
    }

    private static void testAggressiveWakeupFrameEcho() throws Exception {
        String combinedLog = runEchoFrameScenario(
            "AggressiveWakeupEcho",
            new String[] { "-XX:UBSocketProfile=2", "-XX:+UBSocketAggressiveWakeup" },
            AGGRESSIVE_INFLIGHT,
            AGGRESSIVE_FRAMES);

        assertRingDataPath(combinedLog, "Aggressive wakeup echo should use ring data path");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "wakeup_request_total", 1,
            "Aggressive wakeup mode should request TCP wakeups");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "wakeup_send_total", 1,
            "Aggressive wakeup mode should send TCP wakeup frames");
    }

    private static void testRingSlotLimitFallback() throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String[] profileOptions = new String[] { "-XX:UBSocketProfile=2" };

        Process server = null;
        try {
            ProcessBuilder serverPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath,
                    controlPort,
                    profileOptions,
                    "NIOScenarioServer",
                    "acceptHold",
                    String.valueOf(dataPort),
                    String.valueOf(SLOT_LIMIT_CLIENTS),
                    String.valueOf(SLOT_LIMIT_HOLD_MS));
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath,
                    controlPort,
                    profileOptions,
                    "NIOScenarioClient",
                    "holdConnections",
                    "localhost",
                    String.valueOf(dataPort),
                    String.valueOf(SLOT_LIMIT_CLIENTS),
                    String.valueOf(SLOT_LIMIT_HOLD_MS),
                    "RingSlotLimitClient");

            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains("HOLD_CONNECTIONS_OK")) {
                throw new RuntimeException("Ring-slot fallback clients did not complete\n"
                    + clientLog);
            }

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("ACCEPT_HOLD_OK clients=" + SLOT_LIMIT_CLIENTS)) {
                throw new RuntimeException("Ring-slot fallback server did not complete\n"
                    + serverLog);
            }

            String combinedLog = clientLog + "\n" + serverLog;
            SocketTestSupport.assertProfileCountAtLeast(
                combinedLog, "ub_attach_success", 1,
                "At least one connection should attach before slot exhaustion");
            SocketTestSupport.assertProfileCountAtLeast(
                combinedLog, "ring_attach_no_slot", 1,
                "The ninth concurrent connection should exhaust fixed ring slots");
            SocketTestSupport.assertProfileCountAtLeast(
                combinedLog, "ub_attach_fallback", 1,
                "Slot exhaustion should fallback to TCP");
            SocketTestSupport.assertNoLegacyControlFrames(
                combinedLog, "Slot fallback should not use legacy data fallback frames");
            SocketTestSupport.assertNoVmCrash(
                combinedLog, "Ring-slot fallback should not crash VM");
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static String runEchoFrameScenario(String caseName, String[] vmOptions,
                                               int inflight, int frames)
        throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        Process server = null;
        try {
            ProcessBuilder serverPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath,
                    controlPort,
                    vmOptions,
                    "NIOScenarioServer",
                    "echoFrames",
                    String.valueOf(dataPort),
                    String.valueOf(REQUEST_20B),
                    String.valueOf(RESPONSE_2K),
                    String.valueOf(frames));
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath,
                    controlPort,
                    vmOptions,
                    "NIOScenarioClient",
                    "echoFrames",
                    "localhost",
                    String.valueOf(dataPort),
                    String.valueOf(REQUEST_20B),
                    String.valueOf(RESPONSE_2K),
                    String.valueOf(inflight),
                    String.valueOf(frames),
                    caseName);

            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains("ECHO_CLIENT_OK")) {
                throw new RuntimeException(caseName + " client did not complete\n" + clientLog);
            }

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("ECHO_SERVER_OK frames=" + frames)) {
                throw new RuntimeException(caseName + " server did not complete\n" + serverLog);
            }
            return clientLog + "\n" + serverLog;
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void assertRingDataPath(String combinedLog, String message) {
        SocketTestSupport.assertBindSuccesses(
            combinedLog, false, 1, message + ": client attach");
        SocketTestSupport.assertBindSuccesses(
            combinedLog, true, 1, message + ": server attach");
        SocketTestSupport.assertNoFallback(combinedLog, message + ": should not fallback");
        SocketTestSupport.assertNoLegacyControlFrames(
            combinedLog, message + ": should not use legacy control frames");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "ub_attach_success", 2, message + ": attach profile");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "ring_write_total", 1, message + ": ring write profile");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "ring_read_total", 1, message + ": ring read profile");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "wakeup_frame_count", 1, message + ": wakeup parse profile");
        SocketTestSupport.assertNoVmCrash(combinedLog, message + ": no VM crash");
    }
}
