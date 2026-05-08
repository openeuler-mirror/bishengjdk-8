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
 * @summary Test sender-side data-path fallback and SEND-timeout fallback to TCP
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=300 DataFallbackTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class DataFallbackTest {
    // Equal to the configured 16MiB UB pool payload capacity, so per-frame
    // metadata makes the allocation exceed capacity and forces TCP fallback
    // without turning the scenario into a large-memory stress test.
    private static final int OVERSIZED_POOL_SIZE = 16 * 1024 * 1024;
    private static final int OVERSIZED_DATA_SIZE = OVERSIZED_POOL_SIZE;
    private static final int SEND_TIMEOUT_FIRST_SIZE = 1024 * 1024;
    private static final int SEND_TIMEOUT_SECOND_SIZE = 1024 * 1024;
    private static final int SEND_TIMEOUT_TOTAL_SIZE =
        SEND_TIMEOUT_FIRST_SIZE + SEND_TIMEOUT_SECOND_SIZE;
    private static final int ALLOC_FAILED_POOL_SIZE = 4 * 1024 * 1024;
    private static final int ALLOC_FAILED_CHUNK_SIZE = 3 * 1024 * 1024;
    private static final int ALLOC_FAILED_CHUNK_COUNT = 2;
    private static final int ALLOC_FAILED_TOTAL_SIZE =
        ALLOC_FAILED_CHUNK_SIZE * ALLOC_FAILED_CHUNK_COUNT;
    private static final int MULTI_WRITE_CHUNK_SIZE = 60 * 1024 * 1024;
    private static final int MULTI_WRITE_CHUNK_COUNT = 5;
    private static final int MULTI_WRITE_TOTAL_SIZE =
        MULTI_WRITE_CHUNK_SIZE * MULTI_WRITE_CHUNK_COUNT;
    private static final long MULTI_WRITE_READ_DELAY_MS = 5000L;
    private static final int CONCURRENT_CLIENT_COUNT = 2;
    private static final int CONCURRENT_ALLOC_POOL_SIZE = 8 * 1024 * 1024;
    private static final int POST_FALLBACK_CHUNK_SIZE = 16 * 1024 * 1024;
    private static final int POST_FALLBACK_CHUNK_COUNT = 20;
    private static final int POST_FALLBACK_TOTAL_SIZE =
        POST_FALLBACK_CHUNK_SIZE * POST_FALLBACK_CHUNK_COUNT;
    private static final long POST_FALLBACK_READ_DELAY_MS = 5000L;
    private static final long SOCKET_TIMEOUT_MS = 200L;
    private static final long READ_DELAY_MS = 800L;

    public static void main(String[] args) throws Exception {
        testOversizedWriteDataFallback();
        testSendTimeoutDataFallback();
        testAllocFailedDataFallback();
        testConcurrentMemoryPressureFallback();
        testPostFallbackContinuedWrites();
    }

    private static void testOversizedWriteDataFallback() throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String[] vmOptions = new String[] {
            "-Xmx256m",
            "-XX:UBSocketMemorySize=" + OVERSIZED_POOL_SIZE
        };

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilderWithTimeoutAndVmOptions(
                configPath,
                controlPort,
                -1,
                vmOptions,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(OVERSIZED_DATA_SIZE),
                "1"
            );
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilderWithTimeoutAndVmOptions(
                configPath,
                controlPort,
                -1,
                vmOptions,
                "NIOScenarioClient",
                "basic",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(OVERSIZED_DATA_SIZE),
                "DataFallbackClient"
            );

            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            SocketTestSupport.assertDataTransferSuccess(
                clientLog, "Data fallback client should still complete over TCP");

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("All 1 clients completed successfully")) {
                throw new RuntimeException("Data fallback server did not complete\n" + serverLog);
            }

            String combinedLog = clientLog + "\n" + serverLog;
            SocketTestSupport.assertDataFallback(
                combinedLog, "Expected sender-side data fallback markers");
            SocketTestSupport.assertBindSuccesses(
                combinedLog, false, 1, "Attach should succeed before data fallback");
            SocketTestSupport.assertBindSuccesses(
                combinedLog, true, 1, "Server attach should succeed before data fallback");
            SocketTestSupport.assertNoPayloadPrefixLog(
                combinedLog, "Data fallback should not log raw TCP payload data");
            SocketTestSupport.assertNoVmCrash(
                combinedLog, "Data fallback should not crash VM");
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void testAllocFailedDataFallback() throws Exception {
        runSingleMultiWriteFallbackScenario(
            "AllocFailedFallbackClient",
            ALLOC_FAILED_CHUNK_SIZE,
            ALLOC_FAILED_CHUNK_COUNT,
            ALLOC_FAILED_TOTAL_SIZE,
            MULTI_WRITE_READ_DELAY_MS,
            "Expected shared-memory allocation failure to trigger data fallback",
            new String[] {
                "-Xmx256m",
                "-XX:UBSocketMemorySize=" + ALLOC_FAILED_POOL_SIZE
            },
            0,
            "reason=alloc_failed",
            "reason=recv_timeout");
    }

    private static void testConcurrentMemoryPressureFallback() throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String[] vmOptions = new String[] {
            "-Xmx256m",
            "-XX:UBSocketMemorySize=" + CONCURRENT_ALLOC_POOL_SIZE
        };

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilderWithTimeoutAndVmOptions(
                configPath,
                controlPort,
                0,
                vmOptions,
                "NIOScenarioServer",
                "delayedRead",
                String.valueOf(dataPort),
                String.valueOf(ALLOC_FAILED_TOTAL_SIZE),
                String.valueOf(MULTI_WRITE_READ_DELAY_MS),
                String.valueOf(CONCURRENT_CLIENT_COUNT)
            );
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilderWithTimeoutAndVmOptions(
                configPath,
                controlPort,
                0,
                vmOptions,
                "NIOScenarioClient",
                "parallelMultiWrite",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(ALLOC_FAILED_CHUNK_SIZE),
                String.valueOf(ALLOC_FAILED_CHUNK_COUNT),
                String.valueOf(CONCURRENT_CLIENT_COUNT),
                "ConcurrentFallbackClient"
            );

            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains("PARALLEL_MULTI_WRITE_OK")) {
                throw new RuntimeException("Concurrent fallback clients did not complete\n"
                    + clientLog);
            }

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("DELAYED_READ_OK clients=" + CONCURRENT_CLIENT_COUNT)) {
                throw new RuntimeException("Concurrent fallback server did not complete\n"
                    + serverLog);
            }

            String combinedLog = clientLog + "\n" + serverLog;
            SocketTestSupport.assertDataFallback(
                combinedLog, "Expected concurrent memory pressure to trigger data fallback");
            if (!combinedLog.contains("reason=alloc_failed")) {
                throw new RuntimeException("Expected concurrent fallback to be caused by"
                    + " shared-memory allocation pressure\n" + combinedLog);
            }
            SocketTestSupport.assertBindSuccesses(
                combinedLog, false, CONCURRENT_CLIENT_COUNT,
                "All concurrent clients should attach before fallback");
            SocketTestSupport.assertBindSuccesses(
                combinedLog, true, CONCURRENT_CLIENT_COUNT,
                "Server should attach all concurrent clients before fallback");
            SocketTestSupport.assertNoPayloadPrefixLog(
                combinedLog, "Concurrent fallback should not log raw TCP payload data");
            SocketTestSupport.assertNoVmCrash(
                combinedLog, "Concurrent fallback should not crash VM");
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void testPostFallbackContinuedWrites() throws Exception {
        runSingleMultiWriteFallbackScenario(
            "PostFallbackContinuedWriteClient",
            POST_FALLBACK_CHUNK_SIZE,
            POST_FALLBACK_CHUNK_COUNT,
            POST_FALLBACK_TOTAL_SIZE,
            POST_FALLBACK_READ_DELAY_MS,
            "Expected post-fallback continued writes to stay on TCP",
            new String[] { "-Xmx768m" },
            SOCKET_TIMEOUT_MS,
            "reason=recv_timeout",
            null);
    }

    private static void runSingleMultiWriteFallbackScenario(String clientId,
                                                           int chunkSize,
                                                           int chunkCount,
                                                           int totalSize,
                                                           long readDelayMs,
                                                           String fallbackMessage,
                                                           String[] vmOptions,
                                                           long timeoutMs,
                                                           String expectedReason,
                                                           String unexpectedReason)
        throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilderWithTimeoutAndVmOptions(
                configPath,
                controlPort,
                timeoutMs,
                vmOptions,
                "NIOScenarioServer",
                "delayedRead",
                String.valueOf(dataPort),
                String.valueOf(totalSize),
                String.valueOf(readDelayMs)
            );
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilderWithTimeoutAndVmOptions(
                configPath,
                controlPort,
                timeoutMs,
                vmOptions,
                "NIOScenarioClient",
                "multiWrite",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(chunkSize),
                String.valueOf(chunkCount),
                clientId
            );

            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            SocketTestSupport.assertDataTransferSuccess(
                clientLog, clientId + " should still complete");

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("DELAYED_READ_OK")) {
                throw new RuntimeException(clientId + " server did not complete\n" + serverLog);
            }

            String combinedLog = clientLog + "\n" + serverLog;
            SocketTestSupport.assertDataFallback(combinedLog, fallbackMessage);
            if (expectedReason != null && !combinedLog.contains(expectedReason)) {
                throw new RuntimeException("Expected " + clientId + " fallback to include "
                    + expectedReason + "\n" + combinedLog);
            }
            if (unexpectedReason != null && combinedLog.contains(unexpectedReason)) {
                throw new RuntimeException("Expected " + clientId + " fallback not to include "
                    + unexpectedReason + "\n" + combinedLog);
            }
            SocketTestSupport.assertBindSuccesses(
                combinedLog, false, 1, "Attach should succeed before " + clientId + " fallback");
            SocketTestSupport.assertBindSuccesses(
                combinedLog, true, 1, "Server attach should succeed before " + clientId + " fallback");
            SocketTestSupport.assertNoPayloadPrefixLog(
                combinedLog, clientId + " should not log raw TCP payload data");
            SocketTestSupport.assertNoVmCrash(
                combinedLog, clientId + " fallback should not crash VM");
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void testSendTimeoutDataFallback() throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilderWithTimeout(
                configPath,
                controlPort,
                SOCKET_TIMEOUT_MS,
                "NIOScenarioServer",
                "delayedRead",
                String.valueOf(dataPort),
                String.valueOf(SEND_TIMEOUT_TOTAL_SIZE),
                String.valueOf(READ_DELAY_MS)
            );
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilderWithTimeout(
                configPath,
                controlPort,
                SOCKET_TIMEOUT_MS,
                "NIOScenarioClient",
                "waitSendTimeout",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(SEND_TIMEOUT_FIRST_SIZE),
                String.valueOf(SEND_TIMEOUT_SECOND_SIZE),
                String.valueOf(READ_DELAY_MS),
                "SendTimeoutFallbackClient"
            );

            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            SocketTestSupport.assertDataTransferSuccess(
                clientLog, "Timeout fallback client should still complete");

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("DELAYED_READ_OK")) {
                throw new RuntimeException("Timeout fallback server did not complete\n" + serverLog);
            }

            String combinedLog = clientLog + "\n" + serverLog;
            SocketTestSupport.assertDataFallback(
                combinedLog, "Expected SEND timeout to trigger data fallback");
            SocketTestSupport.assertNoVmCrash(
                combinedLog, "Timeout fallback should not crash VM");
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }
}
