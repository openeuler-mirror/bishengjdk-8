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
 * @summary Test UBSocket non-blocking ring boundary behavior under
 *          receiver-backpressure partial ring writes.
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=300 RingBoundaryPolicyTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class RingBoundaryPolicyTest {
    private static final int CLIENT_COUNT = 1;
    private static final int PRESSURE_DATA_SIZE = 34 * 1024 * 1024;
    private static final long PRESSURE_READ_DELAY_MS = 1000L;

    public static void main(String[] args) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();

        testRingPressurePartialWrite(configPath);
    }

    private static void testRingPressurePartialWrite(String configPath) throws Exception {
        SocketTestSupport.ScenarioLogs logs = runDelayedReadScenario(
            configPath,
            PRESSURE_DATA_SIZE,
            PRESSURE_READ_DELAY_MS,
            new String[] {
                "NIOScenarioClient", "nonBlockingBasic", "localhost",
                "$DATA_PORT",
                String.valueOf(PRESSURE_DATA_SIZE),
                "RingPressureClient"
            },
            "NONBLOCKING_IO_OK");

        String combinedLog = logs.clientLog + "\n" + logs.serverLog;
        assertNonBlockingApi(logs.clientLog, "basic", PRESSURE_DATA_SIZE,
                             "ring pressure");
        assertSuccessfulRingTransfer(combinedLog, "ring pressure");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "ring_write_partial", 1,
            "Payload larger than one inbound ring slot should produce partial ring writes");
    }

    private static SocketTestSupport.ScenarioLogs runDelayedReadScenario(
            String configPath,
            int expectedSize,
            long readDelayMs,
            String[] clientCommand,
            String clientSuccessToken) throws Exception {
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
                    "delayedRead",
                    String.valueOf(dataPort),
                    String.valueOf(expectedSize),
                    String.valueOf(readDelayMs),
                    String.valueOf(CLIENT_COUNT));
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath,
                    controlPort,
                    profileOptions,
                    clientCommand[0],
                    substituteDataPort(clientCommand, dataPort));

            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains(clientSuccessToken)) {
                throw new RuntimeException("Missing client token: " + clientSuccessToken
                    + "\n" + clientLog);
            }

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("DELAYED_READ_OK clients=" + CLIENT_COUNT)) {
                throw new RuntimeException("Delayed-read server did not complete\n" + serverLog);
            }
            server = null;
            return new SocketTestSupport.ScenarioLogs(clientLog, serverLog);
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static String[] substituteDataPort(String[] command, int dataPort) {
        String[] result = new String[command.length - 1];
        for (int i = 1; i < command.length; i++) {
            result[i - 1] = "$DATA_PORT".equals(command[i])
                ? String.valueOf(dataPort)
                : command[i];
        }
        return result;
    }

    private static void assertNonBlockingApi(String clientLog, String api,
                                             int expectedBytesWritten,
                                             String message) {
        if (!clientLog.contains("NONBLOCKING_IO_OK")
                || !clientLog.contains("api=" + api)
                || !clientLog.contains("bytesWritten=" + expectedBytesWritten)) {
            throw new RuntimeException(message
                + ": missing non-blocking client API marker api=" + api
                + ", bytesWritten=" + expectedBytesWritten
                + "\n" + clientLog);
        }
    }

    private static void assertSuccessfulRingTransfer(String combinedLog, String message) {
        SocketTestSupport.assertBindSuccesses(
            combinedLog, false, CLIENT_COUNT, message + ": client attach");
        SocketTestSupport.assertBindSuccesses(
            combinedLog, true, CLIENT_COUNT, message + ": server attach");
        SocketTestSupport.assertNoFallback(combinedLog, message + ": should not fallback");
        SocketTestSupport.assertNoLegacyControlFrames(
            combinedLog, message + ": should not use obsolete control frames");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "ub_attach_success", CLIENT_COUNT * 2,
            message + ": attach profile");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "ring_write_total", 1,
            message + ": ring write profile");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "ring_read_total", 1,
            message + ": ring read profile");
        SocketTestSupport.assertNoVmCrash(combinedLog, message + ": no VM crash");
        SocketTestSupport.assertNoMemoryOperationFailure(
            combinedLog, message + ": no shared-memory map failure");
    }
}
