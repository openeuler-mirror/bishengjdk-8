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
 * @summary Test the receiver-owned ring data path with process polling and
 *          selector eventfd readiness injection.
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=240 RingPollingDataPathTest
 */

public class RingPollingDataPathTest {
    private static final int REQUEST_20B = 20;
    private static final int RESPONSE_2K = 2048;
    private static final int CONCURRENT_INFLIGHT = 32;
    private static final int CONCURRENT_FRAMES = 2000;

    public static void main(String[] args) throws Exception {
        testPollingFrameEcho();
    }

    private static void testPollingFrameEcho() throws Exception {
        String combinedLog = runEchoFrameScenario(
            "PollingEcho",
            new String[] { "-XX:UBSocketProfile=2" },
            CONCURRENT_INFLIGHT,
            CONCURRENT_FRAMES);

        assertRingDataPath(combinedLog, "Polling echo should use ring data path");
    }

    private static String runEchoFrameScenario(String caseName, String[] vmOptions,
                                               int inflight, int frames)
        throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        SocketTestSupport.ScenarioLogs logs =
            SocketTestSupport.runUbScenarioWithVmOptions(
                configPath, controlPort, vmOptions, 1000L,
                "ECHO_CLIENT_OK", "ECHO_SERVER_OK frames=" + frames,
                new String[] {
                    "NIOScenarioServer", "echoFrames",
                    String.valueOf(dataPort), String.valueOf(REQUEST_20B),
                    String.valueOf(RESPONSE_2K), String.valueOf(frames)
                },
                new String[] {
                    "NIOScenarioClient", "echoFrames", "localhost",
                    String.valueOf(dataPort), String.valueOf(REQUEST_20B),
                    String.valueOf(RESPONSE_2K), String.valueOf(inflight),
                    String.valueOf(frames), caseName
                });
        return logs.clientLog + "\n" + logs.serverLog;
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
            combinedLog, "poll_notify_publish", 1,
            message + ": publish process notification");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "poll_notify_observed", 1,
            message + ": poll process notification");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "poll_write_offset_scanned", 1,
            message + ": compact write-offset scan");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "poll_write_offset_changed", 1,
            message + ": changed Ring detection");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "selector_poll_ready_inject", 1,
            message + ": selector polling readiness injection");
        SocketTestSupport.assertNoVmCrash(combinedLog, message + ": no VM crash");
    }
}
