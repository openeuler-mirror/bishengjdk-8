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
 * @summary Test attach early-request queue rejects requests after its hard limit
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm EarlyRequestQueueLimitTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;

public class EarlyRequestQueueLimitTest {
    private static final int DATA_SIZE = 131072;
    private static final int FLOOD_REQUESTS = 140;
    private static final int SECOND_ACCEPT_DELAY_MS = 3000;
    private static final int HOLD_AFTER_COMPLETE_MS = 5000;
    private static final int ATTACH_FRAME_SIZE = 88;
    private static final int ENDPOINT_SIZE = 20;
    private static final int MEM_NAME_SIZE = 32;
    private static final int CHECKSUM_OFFSET = 8;
    private static final int AF_INET = 2;
    private static final int UB_SOCKET_PROTOCOL_VERSION = 800;
    private static final int UB_SOCKET_ATTACH_REQ = 1;
    private static final int UB_SOCKET_ATTACH_OK = 0;

    public static void main(String[] args) throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        Process server = null;
        List<Socket> floodSockets = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "EarlyRequestQueueLimitServer",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(SECOND_ACCEPT_DELAY_MS),
                String.valueOf(HOLD_AFTER_COMPLETE_MS)
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
                "QueueLimitWarmupClient"
            );
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            SocketTestSupport.assertDataTransferSuccess(
                clientLog, "warmup client should complete before queue flood");
            SocketTestSupport.assertBindSuccesses(
                clientLog, false, 1, "warmup client should attach before queue flood");
            SocketTestSupport.assertNoFallback(
                clientLog, "warmup client should not fallback before queue flood");

            floodSockets = floodControlPort(controlPort);

            ProcessBuilder fallbackClientPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioClient",
                "basic",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "QueueLimitFallbackClient"
            );
            OutputAnalyzer fallbackClientOutput = new OutputAnalyzer(fallbackClientPb.start());
            fallbackClientOutput.shouldHaveExitValue(0);
            String fallbackClientLog = SocketTestSupport.combinedOutput(
                fallbackClientOutput, fallbackClientPb);
            SocketTestSupport.assertDataTransferSuccess(
                fallbackClientLog, "queue-full fallback client should complete over TCP");
            SocketTestSupport.assertFallback(
                fallbackClientLog, "queue-full client should fallback to TCP");
            SocketTestSupport.assertNoBindSuccess(
                fallbackClientLog, false, "queue-full client should not bind UB");

            closeSockets(floodSockets);
            floodSockets = null;

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("early request queue full limit=128")) {
                throw new RuntimeException("Expected early request queue limit warning\n" + serverLog);
            }
            if (!serverLog.contains("attach agent started") ||
                    !serverLog.contains("port=" + controlPort)) {
                throw new RuntimeException("Expected attach-agent bind log\n" + serverLog);
            }
            if (!serverLog.contains("Queue limit fallback server completed")) {
                throw new RuntimeException("Expected fallback server completion\n" + serverLog);
            }
            SocketTestSupport.assertBindSuccesses(
                serverLog, true, 1, "only warmup server attach should bind UB");
            if (SocketTestSupport.countFallbacks(serverLog, true) < 1) {
                throw new RuntimeException("Expected queue-full server side to fallback\n" + serverLog);
            }
            server = null;
        } finally {
            closeSockets(floodSockets);
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static List<Socket> floodControlPort(int controlPort) throws Exception {
        List<Socket> sockets = new ArrayList<Socket>();
        try {
            for (int i = 0; i < FLOOD_REQUESTS; i++) {
                Socket socket = new Socket("127.0.0.1", controlPort);
                sockets.add(socket);
                OutputStream out = socket.getOutputStream();
                out.write(attachRequestFrame(10000 + i));
                out.flush();
            }
            Thread.sleep(500L);
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
        byte[] memName = ("SOCK-QUEUE-" + requestId).getBytes();
        frame.put(memName, 0, Math.min(memName.length, MEM_NAME_SIZE - 1));
        while (frame.position() < ATTACH_FRAME_SIZE) {
            frame.put((byte)0);
        }
        // Server disables checksum (returns 0), so test client must also use 0
        ByteBuffer.wrap(frame.array()).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(CHECKSUM_OFFSET, 0);
        return frame.array();
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

class EarlyRequestQueueLimitServer {
    public static void main(String[] args) throws Exception {
        if (args.length < 4) {
            throw new IllegalArgumentException(
                "Usage: EarlyRequestQueueLimitServer <port> <dataSize> <secondAcceptDelayMs> <holdAfterMs>");
        }

        int port = Integer.parseInt(args[0]);
        int dataSize = Integer.parseInt(args[1]);
        long secondAcceptDelayMs = Long.parseLong(args[2]);
        long holdAfterMs = Long.parseLong(args[3]);

        ServerSocketChannel server = ServerSocketChannel.open();
        try {
            server.configureBlocking(true);
            server.socket().setReuseAddress(true);
            server.bind(new InetSocketAddress(port));

            handleClient(server.accept(), dataSize);
            System.out.println("Queue limit warmup server completed");

            Thread.sleep(secondAcceptDelayMs);
            handleClient(server.accept(), dataSize);
            System.out.println("Queue limit fallback server completed");

            if (holdAfterMs > 0L) {
                Thread.sleep(holdAfterMs);
            }
        } finally {
            server.close();
        }
    }

    private static void handleClient(SocketChannel channel, int expectedSize) throws Exception {
        ByteBuffer readBuffer = ByteBuffer.allocate(65536);
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        long totalRead = 0L;
        try {
            channel.configureBlocking(true);
            while (totalRead < expectedSize) {
                readBuffer.clear();
                int n = channel.read(readBuffer);
                if (n < 0) {
                    throw new IOException("Client closed early at " + totalRead + "/" + expectedSize);
                }
                if (n == 0) {
                    continue;
                }
                readBuffer.flip();
                byte[] chunk = new byte[n];
                readBuffer.get(chunk);
                digest.update(chunk, 0, n);
                totalRead += n;
            }

            byte[] hashBytes = digest.digest();
            StringBuilder hash = new StringBuilder(hashBytes.length * 2);
            for (byte b : hashBytes) {
                hash.append(String.format("%02x", b));
            }

            ByteBuffer ack = ByteBuffer.wrap(
                ("ACK " + totalRead + " bytes received, hash " + hash.toString())
                    .getBytes(StandardCharsets.UTF_8));
            while (ack.hasRemaining()) {
                channel.write(ack);
            }
        } finally {
            channel.close();
        }
    }
}
