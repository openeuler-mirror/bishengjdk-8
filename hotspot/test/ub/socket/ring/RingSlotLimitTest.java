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
 * @summary Test configurable UBSocket ring slot count and slot-exhaustion fallback.
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=120 RingSlotLimitTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class RingSlotLimitTest {
    private static final int RING_COUNT = 2;
    private static final int CLIENTS = RING_COUNT + 1;
    private static final int HOLD_MS = 1000;

    public static void main(String[] args) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String[] vmOptions = new String[] {
            "-XX:UBSocketProfile=2",
            "-XX:UBSocketMemorySize=8M",
            "-XX:UBSocketRingCount=" + RING_COUNT
        };

        Process server = null;
        try {
            ProcessBuilder serverPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath,
                    controlPort,
                    vmOptions,
                    "NIOScenarioServer",
                    "acceptHold",
                    String.valueOf(dataPort),
                    String.valueOf(CLIENTS),
                    String.valueOf(HOLD_MS));
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath,
                    controlPort,
                    vmOptions,
                    "NIOScenarioClient",
                    "holdConnections",
                    "localhost",
                    String.valueOf(dataPort),
                    String.valueOf(CLIENTS),
                    String.valueOf(HOLD_MS),
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
            if (!serverLog.contains("ACCEPT_HOLD_OK clients=" + CLIENTS)) {
                throw new RuntimeException("Ring-slot fallback server did not complete\n"
                    + serverLog);
            }

            String combinedLog = clientLog + "\n" + serverLog;
            SocketTestSupport.assertProfileCountAtLeast(
                combinedLog, "ub_attach_success", RING_COUNT,
                "Connections should attach before configured ring slots are exhausted");
            SocketTestSupport.assertProfileCountAtLeast(
                combinedLog, "ring_attach_no_slot", 1,
                "The extra concurrent connection should exhaust configured ring slots");
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
}
