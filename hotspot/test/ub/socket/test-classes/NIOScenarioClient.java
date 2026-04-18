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

import java.io.EOFException;
import java.io.IOException;
import java.net.ConnectException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;

public class NIOScenarioClient {
    private static final long CONNECT_DEADLINE_MS = 15000L;
    private static final long RETRY_INTERVAL_MS = 100L;

    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            throw new IllegalArgumentException("Usage: NIOScenarioClient <mode> ...");
        }

        String mode = args[0];
        if ("basic".equals(mode)) {
            runBasic(args);
        } else if ("chunked".equals(mode)) {
            runChunked(args);
        } else if ("parallel".equals(mode)) {
            runParallel(args);
        } else if ("peerClose".equals(mode)) {
            runPeerClose(args);
        } else if ("sequential".equals(mode)) {
            runSequential(args);
        } else if ("restartAware".equals(mode)) {
            runRestartAware(args);
        } else if ("refCount".equals(mode)) {
            runRefCount(args);
        } else {
            throw new IllegalArgumentException("Unknown mode: " + mode);
        }
    }

    private static void runBasic(String[] args) throws Exception {
        if (args.length < 5) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient basic <host> <port> <dataSize> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        String clientId = args[4];

        System.out.println("[" + clientId + "] Connecting to " + host + ":" + port);
        System.out.println("[" + clientId + "] Preparing " + dataSize + " bytes data...");

        byte[] payload = SocketTestData.upperAlphabetData(dataSize);
        String expectedHash = SocketTestData.sha256Hex(payload);
        long startTime = System.currentTimeMillis();
        long totalWritten = 0;

        try (SocketChannel channel = SocketChannel.open()) {
            channel.configureBlocking(true);
            channel.connect(new InetSocketAddress(host, port));
            System.out.println("[" + clientId + "] Connected to server");

            ByteBuffer writeBuffer = ByteBuffer.wrap(payload);
            while (writeBuffer.hasRemaining()) {
                totalWritten += channel.write(writeBuffer);
            }

            long duration = System.currentTimeMillis() - startTime;
            System.out.println("[" + clientId + "] Sent " + totalWritten + " bytes in " + duration + " ms");

            ByteBuffer responseBuffer = ByteBuffer.allocate(1024);
            int bytesRead = channel.read(responseBuffer);
            if (bytesRead > 0) {
                responseBuffer.flip();
                byte[] response = new byte[bytesRead];
                responseBuffer.get(response);
                String ack = new String(response, StandardCharsets.UTF_8);
                System.out.println("[" + clientId + "] Server response: " + ack);
                if (!ack.contains("hash " + expectedHash)) {
                    throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected=" + expectedHash + " ack=" + ack);
                }
                System.out.println("[" + clientId + "] Data sent successfully, hash verified");
            }
        }

        System.out.println("[" + clientId + "] Connection closed");
    }

    private static void runChunked(String[] args) throws Exception {
        if (args.length < 6) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient chunked <host> <port> <dataSize> <chunkSize> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        int chunkSize = Integer.parseInt(args[4]);
        String clientId = args[5];
        if (chunkSize <= 0) {
            throw new IllegalArgumentException("chunkSize must be positive");
        }

        byte[] payload = SocketTestData.upperAlphabetData(dataSize);
        String expectedHash = SocketTestData.sha256Hex(payload);
        long totalWritten = 0L;

        try (SocketChannel channel = SocketChannel.open()) {
            channel.configureBlocking(true);
            channel.connect(new InetSocketAddress(host, port));
            System.out.println("[" + clientId + "] Connected to server");

            for (int offset = 0; offset < payload.length; ) {
                int len = Math.min(chunkSize, payload.length - offset);
                ByteBuffer writeBuffer = ByteBuffer.wrap(payload, offset, len);
                while (writeBuffer.hasRemaining()) {
                    totalWritten += channel.write(writeBuffer);
                }
                offset += len;
            }

            ByteBuffer responseBuffer = ByteBuffer.allocate(1024);
            int bytesRead = channel.read(responseBuffer);
            if (bytesRead <= 0) {
                throw new RuntimeException("[" + clientId + "] did not receive ACK");
            }
            responseBuffer.flip();
            byte[] response = new byte[bytesRead];
            responseBuffer.get(response);
            String ack = new String(response, StandardCharsets.UTF_8);
            if (!ack.contains("hash " + expectedHash)) {
                throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected=" + expectedHash + " ack=" + ack);
            }
            System.out.println("[" + clientId + "] Sent " + totalWritten
                + " bytes in chunks of " + chunkSize + ", hash verified");
        }
    }

    private static void runParallel(String[] args) throws Exception {
        if (args.length < 6) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient parallel <host> <port> <dataSize> <clientCount> <clientIdPrefix>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        int clientCount = Integer.parseInt(args[4]);
        String clientIdPrefix = args[5];

        byte[] payload = SocketTestData.upperAlphabetData(dataSize);
        CountDownLatch startGate = new CountDownLatch(1);
        CountDownLatch doneGate = new CountDownLatch(clientCount);
        List<Throwable> errors = Collections.synchronizedList(new ArrayList<Throwable>());

        for (int i = 0; i < clientCount; i++) {
            Thread thread = new Thread(
                new ParallelClientTask(host, port, payload, clientIdPrefix + "-" + i,
                                       startGate, doneGate, errors),
                "parallel-client-" + i);
            thread.start();
        }

        startGate.countDown();
        doneGate.await();

        if (!errors.isEmpty()) {
            throw new RuntimeException("Parallel client failed, total errors=" + errors.size(),
                                       errors.get(0));
        }

        System.out.println("PARALLEL_CLIENT_OK");
    }

    private static void runPeerClose(String[] args) throws Exception {
        if (args.length < 3) {
            throw new IllegalArgumentException("Usage: NIOScenarioClient peerClose <host> <port>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);

        try (SocketChannel channel = SocketChannel.open()) {
            channel.configureBlocking(true);
            channel.connect(new InetSocketAddress(host, port));

            ByteBuffer writeBuffer = ByteBuffer.wrap("ping".getBytes("UTF-8"));
            while (writeBuffer.hasRemaining()) {
                channel.write(writeBuffer);
            }

            ByteBuffer responseBuffer = ByteBuffer.allocate(1);
            int bytesRead = channel.read(responseBuffer);
            if (bytesRead >= 0) {
                throw new RuntimeException("UNEXPECTED_SUCCESS nread=" + bytesRead);
            }
            throw new EOFException("Peer closed the connection");
        } catch (IOException expected) {
            System.out.println("EXPECTED_PEER_CLOSE: " + expected.getClass().getSimpleName());
        }
    }

    private static void runSequential(String[] args) throws Exception {
        if (args.length < 6) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient sequential <host> <port> <dataSize> <rounds> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        int rounds = Integer.parseInt(args[4]);
        String clientId = args[5];

        byte[] payload = SocketTestData.lowerAlphabetData(dataSize);
        for (int round = 0; round < rounds; round++) {
            String ack = connectSendAndReadAck(host, port, payload, false, round);
            System.out.println("[" + clientId + "] round " + round + " ack: " + ack);
        }
        System.out.println("SEQUENTIAL_RECONNECT_CLIENT_OK");
    }

    private static void runRestartAware(String[] args) throws Exception {
        if (args.length < 6) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient restartAware <host> <port> <dataSize> <rounds> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        int rounds = Integer.parseInt(args[4]);
        String clientId = args[5];

        byte[] payload = SocketTestData.lowerAlphabetData(dataSize);
        for (int round = 0; round < rounds; round++) {
            String ack = connectSendAndReadAck(host, port, payload, true, round);
            System.out.println("[" + clientId + "] round " + round + " ack: " + ack);
        }
        System.out.println("RESTART_AWARE_CLIENT_OK");
    }

    private static void runRefCount(String[] args) throws Exception {
        if (args.length < 5) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient refCount <host> <port> <dataSize> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        String clientId = args[4];

        byte[] payload = SocketTestData.lowerAlphabetData(dataSize);
        SocketChannel first = null;
        SocketChannel second = null;
        try {
            first = openAndSend(host, port, payload, clientId + "-1");
            second = openAndSend(host, port, payload, clientId + "-2");
            first.close();
            first = null;
            System.out.println("[" + clientId + "] first channel closed");
            Thread.sleep(300L);
            second.close();
            second = null;
            System.out.println("[" + clientId + "] second channel closed");
            System.out.println("REF_COUNT_CLIENT_OK");
        } finally {
            if (first != null) {
                first.close();
            }
            if (second != null) {
                second.close();
            }
        }
    }

    private static String connectSendAndReadAck(String host, int port, byte[] payload,
                                                boolean retryConnect, int round) throws Exception {
        String expectedHash = SocketTestData.sha256Hex(payload);
        long deadline = System.currentTimeMillis() + CONNECT_DEADLINE_MS;
        while (true) {
            try (SocketChannel channel = SocketChannel.open()) {
                channel.configureBlocking(true);
                channel.connect(new InetSocketAddress(host, port));

                ByteBuffer writeBuffer = ByteBuffer.wrap(payload);
                while (writeBuffer.hasRemaining()) {
                    channel.write(writeBuffer);
                }

                ByteBuffer responseBuffer = ByteBuffer.allocate(1024);
                int bytesRead = channel.read(responseBuffer);
                if (bytesRead <= 0) {
                    throw new RuntimeException("Round " + round + " did not receive ACK");
                }

                responseBuffer.flip();
                byte[] response = new byte[bytesRead];
                responseBuffer.get(response);
                String ack = new String(response, StandardCharsets.UTF_8);
                if (!ack.contains("hash " + expectedHash)) {
                    throw new RuntimeException("Round " + round + " HASH_MISMATCH expected=" + expectedHash + " ack=" + ack);
                }
                return ack;
            } catch (ConnectException e) {
                if (!retryConnect || System.currentTimeMillis() >= deadline) {
                    throw new RuntimeException("Round " + round + " connect failed", e);
                }
                Thread.sleep(RETRY_INTERVAL_MS);
            } catch (IOException e) {
                if (!retryConnect || System.currentTimeMillis() >= deadline) {
                    throw new RuntimeException("Round " + round + " connection failed", e);
                }
                Thread.sleep(RETRY_INTERVAL_MS);
            }
        }
    }

    private static SocketChannel openAndSend(String host, int port, byte[] payload,
                                             String clientId) throws Exception {
        String expectedHash = SocketTestData.sha256Hex(payload);
        SocketChannel channel = SocketChannel.open();
        channel.configureBlocking(true);
        channel.connect(new InetSocketAddress(host, port));

        ByteBuffer writeBuffer = ByteBuffer.wrap(payload);
        long totalWritten = 0L;
        while (writeBuffer.hasRemaining()) {
            totalWritten += channel.write(writeBuffer);
        }

        ByteBuffer responseBuffer = ByteBuffer.allocate(1024);
        int bytesRead = channel.read(responseBuffer);
        if (bytesRead <= 0) {
            channel.close();
            throw new RuntimeException("[" + clientId + "] did not receive ACK");
        }

        responseBuffer.flip();
        byte[] response = new byte[bytesRead];
        responseBuffer.get(response);
        String ack = new String(response, StandardCharsets.UTF_8);
        if (!ack.contains("hash " + expectedHash)) {
            channel.close();
            throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected=" + expectedHash + " ack=" + ack);
        }
        System.out.println("[" + clientId + "] ack: " + ack);
        System.out.println("[" + clientId + "] sent " + totalWritten + " bytes, hash verified");
        return channel;
    }

    private static final class ParallelClientTask implements Runnable {
        private final String host;
        private final int port;
        private final byte[] payload;
        private final String clientId;
        private final CountDownLatch startGate;
        private final CountDownLatch doneGate;
        private final List<Throwable> errors;

        private ParallelClientTask(String host, int port, byte[] payload, String clientId,
                                   CountDownLatch startGate, CountDownLatch doneGate,
                                   List<Throwable> errors) {
            this.host = host;
            this.port = port;
            this.payload = payload;
            this.clientId = clientId;
            this.startGate = startGate;
            this.doneGate = doneGate;
            this.errors = errors;
        }

        @Override
        public void run() {
            try {
                startGate.await();
                String ack = connectSendAndReadAck(host, port, payload, false, 0);
                System.out.println("[" + clientId + "] ack: " + ack);
                System.out.println("[" + clientId + "] Data sent successfully");
            } catch (Throwable t) {
                synchronized (errors) {
                    errors.add(t);
                }
            } finally {
                doneGate.countDown();
            }
        }
    }
}
