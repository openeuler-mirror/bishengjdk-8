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
 * @summary Test single UB socket attach, data path, and heartbeat mechanism.
 *          1. Basic attach with 1MB data.
 *          2. IPv6 loopback attach with dual-stack control listener.
 *          3. Large data (8MB) with heartbeat enabled (UBSocketTimeout default 500ms).
 *          4. Dense chunked writes exercise fixed-size binary data-frame handling.
 *          5. Gathering write and scattering read exercise readv/writev handling.
 *          6. Large data without heartbeat (UBSocketTimeout=0) should not complete.
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm UBSocketBasicTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.channels.ServerSocketChannel;

public class UBSocketBasicTest {
    private static final int CLIENT_COUNT = 1;
    private static final int LARGE_DATA_SIZE = 8 * 1024 * 1024;
    private static final int BASIC_DATA_SIZE = 1024 * 1024;
    private static final int CHUNKED_NORMAL_DATA_SIZE = 1024 * 1024;
    private static final int CHUNK_NORMAL_SIZE = 512;
    private static final int CHUNKED_DATA_SIZE = 888 * 1024;
    private static final int CHUNK_SIZE = 888;
    private static final int GATHER_SCATTER_DATA_SIZE = 768 * 1024;
    private static final int GATHER_SCATTER_SEGMENT_SIZE = 333;
    private static final long NO_HEARTBEAT_SERVER_TIMEOUT_MS = 12000L;
    private static final long NO_HEARTBEAT_CLIENT_SETTLE_MS = 3000L;

    public static void main(String[] args) throws Exception {
        testUBSocketBasic();
        testIPv6LoopbackAttach();
        testFixedDataFrameBatching();
        testGatherScatterReadWrite();
        testLargeDataWithHeartbeat();
        testLargeDataWithoutHeartbeat();
    }

    private static void testUBSocketBasic() throws Exception {
        System.out.println("=== Testing basic attach with " + BASIC_DATA_SIZE + " bytes ===");

        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        SocketTestSupport.ScenarioLogs logs = SocketTestSupport.runUbScenario(
            configPath,
            controlPort,
            1000L,
            "Data sent successfully",
            "All " + CLIENT_COUNT + " clients completed successfully",
            new String[] {
                "NIOScenarioServer", "selector",
                String.valueOf(dataPort),
                String.valueOf(BASIC_DATA_SIZE),
                String.valueOf(CLIENT_COUNT)
            },
            new String[] {
                "NIOScenarioClient", "basic", "localhost",
                String.valueOf(dataPort),
                String.valueOf(BASIC_DATA_SIZE),
                "BasicClient"
            }
        );
        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, CLIENT_COUNT, "Basic attach should bind successfully");
        SocketTestSupport.assertNoFallback(
            logs.clientLog, "Basic attach should not fallback");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, CLIENT_COUNT, "Basic attach should bind successfully");
        SocketTestSupport.assertNoFallback(
            logs.serverLog, "Basic attach should not fallback");
        SocketTestSupport.assertNoAbnormalFdCleanup(
            logs.serverLog, "Basic attach should not report abnormal fd cleanup");
        SocketTestSupport.assertNoMemoryOperationFailure(
            logs.clientLog + "\n" + logs.serverLog,
            "Basic attach should not report mmap/munmap failures");
        SocketTestSupport.assertTransferInfoLogs(
            logs.clientLog + "\n" + logs.serverLog,
            "Basic attach should log UBSocket data transfer summary");

        System.out.println("=== Basic attach test PASSED ===");
    }

    private static void testIPv6LoopbackAttach() throws Exception {
        if (!isIPv6LoopbackAvailable()) {
            System.out.println("=== IPv6 loopback unavailable, skipping IPv6 test ===");
            return;
        }
        System.out.println("=== Testing IPv6 loopback attach and data path ===");

        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = findFreeIPv6LoopbackPort();
        int controlPort = SocketTestSupport.findFreePort();

        SocketTestSupport.ScenarioLogs logs = SocketTestSupport.runUbScenario(
            configPath,
            controlPort,
            1000L,
            "Data sent successfully",
            "All " + CLIENT_COUNT + " clients completed successfully",
            new String[] {
                "NIOScenarioServer", "selector",
                String.valueOf(dataPort),
                String.valueOf(BASIC_DATA_SIZE),
                String.valueOf(CLIENT_COUNT),
                "::1"
            },
            new String[] {
                "NIOScenarioClient", "basic", "::1",
                String.valueOf(dataPort),
                String.valueOf(BASIC_DATA_SIZE),
                "IPv6LoopbackClient"
            }
        );
        SocketTestSupport.assertDataTransferSuccess(
            logs.clientLog, "IPv6 loopback client should transfer data successfully");
        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, CLIENT_COUNT,
            "IPv6 loopback client should bind UB successfully");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, CLIENT_COUNT,
            "IPv6 loopback server should bind UB successfully");
        SocketTestSupport.assertNoFallback(
            logs.clientLog, "IPv6 loopback client should not fallback");
        SocketTestSupport.assertNoFallback(
            logs.serverLog, "IPv6 loopback server should not fallback");
        SocketTestSupport.assertNoVmCrash(
            logs.clientLog + "\n" + logs.serverLog,
            "IPv6 loopback attach should not crash VM");

        System.out.println("=== IPv6 loopback attach test PASSED ===");
    }

    private static boolean isIPv6LoopbackAvailable() {
        try (ServerSocketChannel channel = ServerSocketChannel.open()) {
            channel.bind(new InetSocketAddress(InetAddress.getByName("::1"), 0));
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private static int findFreeIPv6LoopbackPort() throws Exception {
        for (int i = 0; i < 100; i++) {
            int port = SocketTestSupport.findFreePort();
            try (ServerSocketChannel channel = ServerSocketChannel.open()) {
                channel.bind(new InetSocketAddress(InetAddress.getByName("::1"), port));
                return port;
            } catch (Exception e) {
                // Try another globally reserved test port.
            }
        }
        throw new Exception("Could not find an IPv6 loopback port");
    }

    private static void testFixedDataFrameBatching() throws Exception {
        System.out.println("=== Testing fixed-size binary data frames with chunked writes ===");

        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        SocketTestSupport.ScenarioLogs logs = SocketTestSupport.runUbScenario(
            configPath,
            controlPort,
            1000L,
            "hash verified",
            "All " + CLIENT_COUNT + " clients completed successfully",
            new String[] {
                "NIOScenarioServer", "selector",
                String.valueOf(dataPort),
                String.valueOf(CHUNKED_DATA_SIZE),
                String.valueOf(CLIENT_COUNT)
            },
            new String[] {
                "NIOScenarioClient", "chunked", "localhost",
                String.valueOf(dataPort),
                String.valueOf(CHUNKED_DATA_SIZE),
                String.valueOf(CHUNK_SIZE),
                "FixedDataFrameBatchingClient"
            }
        );

        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, CLIENT_COUNT, "Chunked writes should bind successfully");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, CLIENT_COUNT, "Chunked writes should bind successfully");
        SocketTestSupport.assertNoFallback(
            logs.clientLog + "\n" + logs.serverLog, "Chunked writes should not fallback");
        SocketTestSupport.assertDataTransferSuccess(
            logs.clientLog, "Chunked writes should preserve payload integrity");

        System.out.println("=== Fixed data-frame batching test PASSED ===");
    }

    private static void testGatherScatterReadWrite() throws Exception {
        System.out.println("=== Testing gathering write and scattering read ===");

        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        SocketTestSupport.ScenarioLogs logs = SocketTestSupport.runUbScenario(
            configPath,
            controlPort,
            1000L,
            "scatter read",
            "All " + CLIENT_COUNT + " clients completed successfully",
            new String[] {
                "NIOScenarioServer", "selector",
                String.valueOf(dataPort),
                String.valueOf(GATHER_SCATTER_DATA_SIZE),
                String.valueOf(CLIENT_COUNT)
            },
            new String[] {
                "NIOScenarioClient", "gatherScatter", "localhost",
                String.valueOf(dataPort),
                String.valueOf(GATHER_SCATTER_DATA_SIZE),
                String.valueOf(GATHER_SCATTER_SEGMENT_SIZE),
                "GatherScatterClient"
            }
        );

        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, CLIENT_COUNT, "Gather/scatter should bind successfully");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, CLIENT_COUNT, "Gather/scatter should bind successfully");
        SocketTestSupport.assertNoFallback(
            logs.clientLog + "\n" + logs.serverLog, "Gather/scatter should not fallback");
        SocketTestSupport.assertDataTransferSuccess(
            logs.clientLog, "Gather/scatter should preserve payload integrity");

        System.out.println("=== Gathering write and scattering read test PASSED ===");
    }

    private static void testLargeDataWithHeartbeat() throws Exception {
        System.out.println("=== Testing large data with heartbeat: " + LARGE_DATA_SIZE + " bytes ===");

        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        // Use default UBSocketTimeout (500ms) - heartbeat enabled
        Process server = null;
        Process client = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilderWithTimeout(
                configPath, controlPort, -1,  // -1 means use default timeout
                "NIOScenarioServer", "selector",
                String.valueOf(dataPort),
                String.valueOf(LARGE_DATA_SIZE),
                "1");
            String serverUbLogPath = SocketTestSupport.getUbLogPath(serverPb);
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilderWithTimeout(
                configPath, controlPort, -1,
                "NIOScenarioClient", "basic", "localhost",
                String.valueOf(dataPort),
                String.valueOf(LARGE_DATA_SIZE),
                "HeartbeatClient");
            String clientUbLogPath = SocketTestSupport.getUbLogPath(clientPb);
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combineOutputWithUbLog(
                clientOutput.getOutput(), clientUbLogPath);

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combineOutputWithUbLog(
                serverOutput.getOutput(), serverUbLogPath);

            SocketTestSupport.assertDataTransferSuccess(clientLog, "Large data transfer failed");

            // Verify heartbeat logs after the protocol switched to binary data frames.
            SocketTestSupport.assertHeartbeat(
                clientLog + "\n" + serverLog,
                "Large data transfer should trigger heartbeat writes");

            System.out.println("=== Large data with heartbeat test PASSED ===");
            System.out.println("- Data transfer completed: OK");
            System.out.println("- Heartbeat detected in logs: OK");

        } finally {
            SocketTestSupport.destroyIfAlive(server);
            SocketTestSupport.destroyIfAlive(client);
        }
    }

    private static void testLargeDataWithoutHeartbeat() throws Exception {
        System.out.println("=== Testing large data WITHOUT heartbeat (UBSocketTimeout=0) ===");
        System.out.println("Expected: large selector-mode transfer cannot complete normally");

        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        Process server = null;
        Process client = null;
        try {
            // Set UBSocketTimeout=0 to disable heartbeat
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilderWithTimeout(
                configPath, controlPort, 0,
                "NIOScenarioServer", "selector",
                String.valueOf(dataPort),
                String.valueOf(LARGE_DATA_SIZE),
                "1");
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilderWithTimeout(
                configPath, controlPort, 0,
                "NIOScenarioClient", "basic", "localhost",
                String.valueOf(dataPort),
                String.valueOf(LARGE_DATA_SIZE),
                "NoHeartbeatClient");
            client = clientPb.start();

            boolean serverFinished = server.waitFor(
                NO_HEARTBEAT_SERVER_TIMEOUT_MS, java.util.concurrent.TimeUnit.MILLISECONDS);
            boolean clientFinished = client.waitFor(
                NO_HEARTBEAT_CLIENT_SETTLE_MS, java.util.concurrent.TimeUnit.MILLISECONDS);
            if (!serverFinished) {
                SocketTestSupport.destroyIfAlive(server);
            }
            if (!clientFinished) {
                SocketTestSupport.destroyIfAlive(client);
            }

            OutputAnalyzer clientOutput = new OutputAnalyzer(client);
            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            String clientLog = SocketTestSupport.combineOutputWithUbLog(
                clientOutput.getOutput(), SocketTestSupport.getUbLogPath(clientPb));
            String serverLog = SocketTestSupport.combineOutputWithUbLog(
                serverOutput.getOutput(), SocketTestSupport.getUbLogPath(serverPb));

            SocketTestSupport.assertNoHeartbeat(
                clientLog + "\n" + serverLog,
                "UBSocketTimeout=0 should disable heartbeat");
            if (clientLog.contains("hs_err_pid") || clientLog.contains("SIGSEGV") ||
                    serverLog.contains("hs_err_pid") || serverLog.contains("SIGSEGV")) {
                throw new RuntimeException("Large transfer without heartbeat should not crash VM\n"
                    + "=== client log ===\n" + clientLog + "\n=== server log ===\n" + serverLog);
            }
            boolean normalCompleted = serverFinished && clientFinished &&
                                      serverOutput.getExitValue() == 0 &&
                                      clientOutput.getExitValue() == 0 &&
                                      SocketTestSupport.containsDataTransferSuccess(clientLog) &&
                                      serverLog.contains("All " + CLIENT_COUNT
                                          + " clients completed successfully");
            if (normalCompleted) {
                throw new RuntimeException("Large transfer without heartbeat completed normally\n"
                    + "=== client log ===\n" + clientLog + "\n=== server log ===\n" + serverLog);
            }

            System.out.println("=== Large data without heartbeat test PASSED ===");
            System.out.println("- Normal completion blocked as expected: OK");
            System.out.println("- Heartbeat disabled: OK");

        } finally {
            SocketTestSupport.destroyIfAlive(server);
            SocketTestSupport.destroyIfAlive(client);
        }
    }

}
