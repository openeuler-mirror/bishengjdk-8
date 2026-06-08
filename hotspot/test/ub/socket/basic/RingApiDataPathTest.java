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
 * @summary Test UBSocket receiver-owned ring coverage for core SocketChannel
 *          non-blocking read/write, direct buffers, chunked writes, and
 *          gather/scatter APIs.
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=240 RingApiDataPathTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class RingApiDataPathTest {
    private static final int CLIENT_COUNT = 1;
    private static final int BASIC_20B = 20;
    private static final int BASIC_2K = 2048;
    private static final int DIRECT_DATA_SIZE = 64 * 1024;
    private static final int CHUNKED_DATA_SIZE = 128 * 1024;
    private static final int CHUNK_SIZE = 20;
    private static final int GATHER_SCATTER_DATA_SIZE = 96 * 1024;
    private static final int GATHER_SCATTER_SEGMENT_SIZE = 31;

    public static void main(String[] args) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();

        testNonBlockingReadWrite(configPath, BASIC_20B, "RingBasic20B");
        testNonBlockingReadWrite(configPath, BASIC_2K, "RingBasic2K");
        testNonBlockingDirectBufferReadWrite(configPath);
        testNonBlockingChunkedSmallWrites(configPath);
        testNonBlockingGatherScatterReadWrite(configPath);
    }

    private static void testNonBlockingReadWrite(String configPath, int dataSize,
                                                 String clientId) throws Exception {
        SocketTestSupport.ScenarioLogs logs = runProfiledScenario(
            configPath,
            "NONBLOCKING_IO_OK",
            "All " + CLIENT_COUNT + " clients completed successfully",
            new String[] {
                "NIOScenarioServer", "selector",
                "$DATA_PORT",
                String.valueOf(dataSize),
                String.valueOf(CLIENT_COUNT)
            },
            new String[] {
                "NIOScenarioClient", "nonBlockingBasic", "localhost",
                "$DATA_PORT",
                String.valueOf(dataSize),
                clientId
            });

        assertCurrentRingDataPath(logs.clientLog + "\n" + logs.serverLog,
                                  CLIENT_COUNT,
                                  "non-blocking read/write " + dataSize + "B");
        assertNonBlockingApi(logs.clientLog, "basic",
                             dataSize,
                             "non-blocking read/write " + dataSize + "B");
        SocketTestSupport.assertDataTransferSuccess(
            logs.clientLog, "Non-blocking read/write should preserve payload integrity");
    }

    private static void testNonBlockingDirectBufferReadWrite(String configPath) throws Exception {
        SocketTestSupport.ScenarioLogs logs = runProfiledScenario(
            configPath,
            "NONBLOCKING_IO_OK",
            "All " + CLIENT_COUNT + " clients completed successfully",
            new String[] {
                "NIOScenarioServer", "selector",
                "$DATA_PORT",
                String.valueOf(DIRECT_DATA_SIZE),
                String.valueOf(CLIENT_COUNT)
            },
            new String[] {
                "NIOScenarioClient", "nonBlockingDirect", "localhost",
                "$DATA_PORT",
                String.valueOf(DIRECT_DATA_SIZE),
                "RingDirectBuffer"
            });

        assertCurrentRingDataPath(logs.clientLog + "\n" + logs.serverLog,
                                  CLIENT_COUNT,
                                  "non-blocking direct buffer read/write");
        assertNonBlockingApi(logs.clientLog, "direct",
                             DIRECT_DATA_SIZE,
                             "non-blocking direct buffer read/write");
        SocketTestSupport.assertDataTransferSuccess(
            logs.clientLog, "Non-blocking direct buffer should preserve payload integrity");
    }

    private static void testNonBlockingChunkedSmallWrites(String configPath) throws Exception {
        SocketTestSupport.ScenarioLogs logs = runProfiledScenario(
            configPath,
            "NONBLOCKING_IO_OK",
            "All " + CLIENT_COUNT + " clients completed successfully",
            new String[] {
                "NIOScenarioServer", "selector",
                "$DATA_PORT",
                String.valueOf(CHUNKED_DATA_SIZE),
                String.valueOf(CLIENT_COUNT)
            },
            new String[] {
                "NIOScenarioClient", "nonBlockingChunked", "localhost",
                "$DATA_PORT",
                String.valueOf(CHUNKED_DATA_SIZE),
                String.valueOf(CHUNK_SIZE),
                "RingChunkedSmallWrites"
            });

        assertCurrentRingDataPath(logs.clientLog + "\n" + logs.serverLog,
                                  CLIENT_COUNT,
                                  "non-blocking chunked small writes");
        assertNonBlockingApi(logs.clientLog, "chunked",
                             CHUNKED_DATA_SIZE,
                             "non-blocking chunked small writes");
        SocketTestSupport.assertDataTransferSuccess(
            logs.clientLog, "Non-blocking chunked writes should preserve payload integrity");
    }

    private static void testNonBlockingGatherScatterReadWrite(String configPath) throws Exception {
        SocketTestSupport.ScenarioLogs logs = runProfiledScenario(
            configPath,
            "NONBLOCKING_IO_OK",
            "All " + CLIENT_COUNT + " clients completed successfully",
            new String[] {
                "NIOScenarioServer", "selector",
                "$DATA_PORT",
                String.valueOf(GATHER_SCATTER_DATA_SIZE),
                String.valueOf(CLIENT_COUNT)
            },
            new String[] {
                "NIOScenarioClient", "nonBlockingGatherScatter", "localhost",
                "$DATA_PORT",
                String.valueOf(GATHER_SCATTER_DATA_SIZE),
                String.valueOf(GATHER_SCATTER_SEGMENT_SIZE),
                "RingGatherScatter"
            });

        assertCurrentRingDataPath(logs.clientLog + "\n" + logs.serverLog,
                                  CLIENT_COUNT,
                                  "non-blocking gather/scatter read/write");
        assertNonBlockingApi(logs.clientLog, "gatherScatter",
                             GATHER_SCATTER_DATA_SIZE,
                             "non-blocking gather/scatter read/write");
        SocketTestSupport.assertDataTransferSuccess(
            logs.clientLog, "Non-blocking gather/scatter should preserve payload integrity");
    }

    private static SocketTestSupport.ScenarioLogs runProfiledScenario(
            String configPath,
            String clientSuccessToken,
            String serverSuccessToken,
            String[] serverCommand,
            String[] clientCommand) throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String[] vmOptions = new String[] { "-XX:UBSocketProfile=2" };

        Process server = null;
        try {
            ProcessBuilder serverPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath,
                    controlPort,
                    vmOptions,
                    serverCommand[0],
                    substituteDataPort(serverCommand, dataPort));
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath,
                    controlPort,
                    vmOptions,
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
            if (!serverLog.contains(serverSuccessToken)) {
                throw new RuntimeException("Missing server token: " + serverSuccessToken
                    + "\n" + serverLog);
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
                || !clientLog.contains("bytesWritten=" + expectedBytesWritten)
                || !clientLog.contains("Data sent successfully, hash verified")
                || clientLog.contains("peer closed before complete non-blocking ACK")) {
            throw new RuntimeException(message
                + ": missing non-blocking API success marker api=" + api
                + ", bytesWritten=" + expectedBytesWritten
                + ", or ACK-before-EOF verification"
                + "\n" + clientLog);
        }
    }

    private static void assertCurrentRingDataPath(String combinedLog,
                                                  int connections,
                                                  String message) {
        SocketTestSupport.assertBindSuccesses(
            combinedLog, false, connections, message + ": client attach");
        SocketTestSupport.assertBindSuccesses(
            combinedLog, true, connections, message + ": server attach");
        SocketTestSupport.assertNoFallback(combinedLog, message + ": should not fallback");
        SocketTestSupport.assertNoLegacyControlFrames(
            combinedLog, message + ": should not use obsolete control frames");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "ub_attach_success", connections * 2,
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
