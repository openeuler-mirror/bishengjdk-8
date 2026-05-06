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
 * @summary Test repeated reconnects on the same server and after server restart
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm ReconnectTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import java.io.OutputStream;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

public class ReconnectTest {
    private static final int DATA_SIZE = 65536;
    private static final int SEQUENTIAL_ROUNDS = 3;
    private static final int RESTART_ROUNDS = 2;
    private static final int LIFECYCLE_EARLY_REQUESTS = 8;
    private static final int LIFECYCLE_HOLD_MS = 1500;
    private static final int ATTACH_FRAME_SIZE = 88;
    private static final int ENDPOINT_SIZE = 20;
    private static final int MEM_NAME_SIZE = 32;
    private static final int CHECKSUM_OFFSET = 8;
    private static final int AF_INET = 2;
    private static final int UB_SOCKET_PROTOCOL_VERSION = 800;
    private static final int UB_SOCKET_ATTACH_REQ = 1;
    private static final int UB_SOCKET_ATTACH_OK = 0;

    public static void main(String[] args) throws Exception {
        testSequentialReconnect();
        testServerRestartReconnect();
        testControlPortLifecycleWithPendingEarlyRequests();
    }

    private static void testSequentialReconnect() throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        SocketTestSupport.ScenarioLogs logs = SocketTestSupport.runUbScenario(
            configPath,
            controlPort,
            1000L,
            "SEQUENTIAL_RECONNECT_CLIENT_OK",
            "All " + SEQUENTIAL_ROUNDS + " clients completed successfully",
            new String[] {
                "NIOScenarioServer", "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(SEQUENTIAL_ROUNDS)
            },
            new String[] {
                "NIOScenarioClient", "sequential", "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(SEQUENTIAL_ROUNDS),
                "SequentialClient"
            }
        );
        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, SEQUENTIAL_ROUNDS,
            "Expected all reconnect attaches to succeed");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, SEQUENTIAL_ROUNDS,
            "Expected server to bind each reconnect");
        SocketTestSupport.assertNoFallback(
            logs.clientLog, "Sequential reconnect should not fallback");
        SocketTestSupport.assertNoFallback(
            logs.serverLog, "Sequential reconnect should not fallback");
        SocketTestSupport.assertNoAbnormalFdCleanup(
            logs.serverLog, "Sequential reconnect should not report abnormal fd cleanup");
        SocketTestSupport.assertNoMemoryOperationFailure(
            logs.clientLog + "\n" + logs.serverLog,
            "Sequential reconnect should not report mmap/munmap failures");
    }

    private static void testServerRestartReconnect() throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
            configPath,
            controlPort,
            "NIOScenarioClient",
            "restartAware",
            "localhost",
            String.valueOf(dataPort),
            String.valueOf(DATA_SIZE),
            String.valueOf(RESTART_ROUNDS),
            "RestartAwareClient"
        );

        ProcessBuilder serverPb1 = null;
        ProcessBuilder serverPb2 = null;
        Process serverRound1 = null;
        Process serverRound2 = null;
        try {
            serverPb1 = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "1"
            );
            serverRound1 = serverPb1.start();

            Thread.sleep(500L);

            Process client = clientPb.start();
            OutputAnalyzer server1Output = new OutputAnalyzer(serverRound1);
            server1Output.shouldHaveExitValue(0);

            serverPb2 = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "1"
            );
            serverRound2 = serverPb2.start();

            OutputAnalyzer clientOutput = new OutputAnalyzer(client);
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains("RESTART_AWARE_CLIENT_OK")) {
                throw new RuntimeException("Restart-aware client did not complete\n" + clientLog);
            }
            SocketTestSupport.assertBindSuccesses(
                clientLog, false, RESTART_ROUNDS,
                "Restart-aware client should attach on both rounds");
            SocketTestSupport.assertNoFallback(
                clientLog, "Restart-aware client should not fallback");

            OutputAnalyzer server2Output = new OutputAnalyzer(serverRound2);
            server2Output.shouldHaveExitValue(0);
            String serverLog =
                SocketTestSupport.combinedOutput(server1Output, serverPb1) + "\n" +
                SocketTestSupport.combinedOutput(server2Output, serverPb2);
            SocketTestSupport.assertBindSuccesses(
                serverLog, true, RESTART_ROUNDS,
                "Restart-aware server should bind on both rounds");
            SocketTestSupport.assertNoFallback(
                serverLog, "Restart-aware server should not fallback");
        } finally {
            if (serverRound1 != null && serverRound1.isAlive()) {
                serverRound1.destroy();
                serverRound1.waitFor();
            }
            if (serverRound2 != null && serverRound2.isAlive()) {
                serverRound2.destroy();
                serverRound2.waitFor();
            }
        }
    }

    private static void testControlPortLifecycleWithPendingEarlyRequests() throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int restartDataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        Process server = null;
        List<Socket> pendingRequests = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "delayedAccept",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "1",
                "0",
                String.valueOf(LIFECYCLE_HOLD_MS)
            );
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
                "LifecycleWarmupClient"
            );
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            SocketTestSupport.assertDataTransferSuccess(
                clientLog, "Lifecycle warmup client should complete");
            SocketTestSupport.assertBindSuccesses(
                clientLog, false, 1, "Lifecycle warmup client should attach");

            pendingRequests = openEarlyRequests(controlPort);

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("All 1 clients completed successfully")) {
                throw new RuntimeException("Lifecycle warmup server did not complete\n" + serverLog);
            }
            SocketTestSupport.assertBindSuccesses(
                serverLog, true, 1, "Lifecycle warmup server should attach");
            server = null;

            closeSockets(pendingRequests);
            pendingRequests = null;

            SocketTestSupport.ScenarioLogs restartLogs = SocketTestSupport.runUbScenario(
                configPath,
                controlPort,
                500L,
                "Data sent successfully",
                "All 1 clients completed successfully",
                new String[] {
                    "NIOScenarioServer", "selector",
                    String.valueOf(restartDataPort),
                    String.valueOf(DATA_SIZE),
                    "1"
                },
                new String[] {
                    "NIOScenarioClient", "basic", "localhost",
                    String.valueOf(restartDataPort),
                    String.valueOf(DATA_SIZE),
                    "LifecycleRestartClient"
                }
            );
            SocketTestSupport.assertBindSuccesses(
                restartLogs.clientLog, false, 1,
                "Restart client should attach after pending early-request cleanup");
            SocketTestSupport.assertBindSuccesses(
                restartLogs.serverLog, true, 1,
                "Restart server should attach after pending early-request cleanup");
            SocketTestSupport.assertNoFallback(
                restartLogs.clientLog + "\n" + restartLogs.serverLog,
                "Restart after pending early-request cleanup should not fallback");
        } finally {
            closeSockets(pendingRequests);
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static List<Socket> openEarlyRequests(int controlPort) throws Exception {
        List<Socket> sockets = new ArrayList<Socket>();
        try {
            for (int i = 0; i < LIFECYCLE_EARLY_REQUESTS; i++) {
                Socket socket = new Socket("127.0.0.1", controlPort);
                sockets.add(socket);
                OutputStream out = socket.getOutputStream();
                out.write(attachRequestFrame(50000 + i));
                out.flush();
            }
            Thread.sleep(200L);
            return sockets;
        } catch (Exception e) {
            closeSockets(sockets);
            throw e;
        }
    }

    private static void closeSockets(List<Socket> sockets) {
        if (sockets == null) {
            return;
        }
        for (Socket socket : sockets) {
            try {
                socket.close();
            } catch (Exception ignore) {
            }
        }
    }

    private static byte[] attachRequestFrame(int requestId) {
        ByteBuffer frame = ByteBuffer.allocate(ATTACH_FRAME_SIZE);
        frame.order(ByteOrder.LITTLE_ENDIAN);
        frame.putShort((short)UB_SOCKET_PROTOCOL_VERSION);
        frame.putShort((short)UB_SOCKET_ATTACH_REQ);
        frame.putInt(requestId);
        frame.putInt(0);
        frame.putInt(UB_SOCKET_ATTACH_OK);
        putEndpoint(frame, 30000 + requestId);
        putEndpoint(frame, 40000 + requestId);
        byte[] memName = ("SOCK-LIFE-" + requestId).getBytes();
        frame.put(memName, 0, Math.min(memName.length, MEM_NAME_SIZE - 1));
        while (frame.position() < ATTACH_FRAME_SIZE) {
            frame.put((byte)0);
        }
        byte[] bytes = frame.array();
        int checksum = fnv1a(bytes);
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(CHECKSUM_OFFSET, checksum);
        return bytes;
    }

    private static void putEndpoint(ByteBuffer frame, int port) {
        frame.putShort((short)AF_INET);
        frame.putShort((short)port);
        frame.put((byte)127);
        frame.put((byte)0);
        frame.put((byte)0);
        frame.put((byte)1);
        for (int i = 4; i < ENDPOINT_SIZE - 4; i++) {
            frame.put((byte)0);
        }
    }

    private static int fnv1a(byte[] bytes) {
        int hash = 0x811c9dc5;
        for (byte value : bytes) {
            hash ^= (value & 0xff);
            hash *= 0x01000193;
        }
        return hash;
    }
}
