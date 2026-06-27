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
 *          integration, and configurable wakeup threshold.
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
    private static final int LOW_THRESHOLD_INFLIGHT = 8;
    private static final int LOW_THRESHOLD_FRAMES = 16;

    public static void main(String[] args) throws Exception {
        testSparseWakeupFrameEcho();
        testLowThresholdWakeupFrameEcho();
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
            combinedLog, "selector_ready_inject", 1,
            "Selector should inject pending UB ring readiness");
    }

    private static void testLowThresholdWakeupFrameEcho() throws Exception {
        String combinedLog = runEchoFrameScenario(
            "LowThresholdWakeupEcho",
            new String[] {
                "-XX:UBSocketProfile=2",
                "-XX:UBSocketWakeupThresholdBytes=1"
            },
            LOW_THRESHOLD_INFLIGHT,
            LOW_THRESHOLD_FRAMES);

        assertRingDataPath(combinedLog, "Low-threshold wakeup echo should use ring data path");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "wakeup_request_threshold", 1,
            "Low wakeup threshold should request threshold-driven TCP wakeups");
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
