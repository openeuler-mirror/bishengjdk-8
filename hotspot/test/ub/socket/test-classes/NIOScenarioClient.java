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
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.ConnectException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.FileChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CountDownLatch;

public class NIOScenarioClient {
    private static final long CONNECT_DEADLINE_MS = 15000L;
    private static final long RETRY_INTERVAL_MS = 100L;
    private static final long ACK_DEADLINE_MS = 15000L;
    private static final int ECHO_FRAME_HEADER_SIZE = 8;
    private static final int ECHO_SELECT_TIMEOUT_MS = 15000;

    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            throw new IllegalArgumentException("Usage: NIOScenarioClient <mode> ...");
        }

        String mode = args[0];
        if ("basic".equals(mode)) {
            runBasic(args);
        } else if ("nonBlockingBasic".equals(mode)) {
            runNonBlockingBasic(args);
        } else if ("nonBlockingDirect".equals(mode)) {
            runNonBlockingDirect(args);
        } else if ("chunked".equals(mode)) {
            runChunked(args);
        } else if ("nonBlockingChunked".equals(mode)) {
            runNonBlockingChunked(args);
        } else if ("parallel".equals(mode)) {
            runParallel(args);
        } else if ("peerClose".equals(mode)) {
            runPeerClose(args);
        } else if ("writeAfterPeerClose".equals(mode)) {
            runWriteAfterPeerClose(args);
        } else if ("sequential".equals(mode)) {
            runSequential(args);
        } else if ("restartAware".equals(mode)) {
            runRestartAware(args);
        } else if ("refCount".equals(mode)) {
            runRefCount(args);
        } else if ("waitSendTimeout".equals(mode)) {
            runWaitSendTimeout(args);
        } else if ("multiWrite".equals(mode)) {
            runMultiWrite(args);
        } else if ("pacedMultiWrite".equals(mode)) {
            runPacedMultiWrite(args);
        } else if ("nonBlockingPacedMultiWrite".equals(mode)) {
            runNonBlockingPacedMultiWrite(args);
        } else if ("parallelMultiWrite".equals(mode)) {
            runParallelMultiWrite(args);
        } else if ("gatherScatter".equals(mode)) {
            runGatherScatter(args);
        } else if ("nonBlockingGatherScatter".equals(mode)) {
            runNonBlockingGatherScatter(args);
        } else if ("transferTo".equals(mode)) {
            runTransferTo(args);
        } else if ("echoFrames".equals(mode)) {
            runEchoFrames(args);
        } else if ("switchBlockingAfterAttach".equals(mode)) {
            runSwitchBlockingAfterAttach(args);
        } else if ("holdConnections".equals(mode)) {
            runHoldConnections(args);
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

            String ack = readAck(channel, expectedHash, clientId);
            System.out.println("[" + clientId + "] Server response: " + ack);
            if (!ack.contains("hash " + expectedHash)) {
                throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected=" + expectedHash + " ack=" + ack);
            }
            System.out.println("[" + clientId + "] Data sent successfully, hash verified");
        }

        System.out.println("[" + clientId + "] Connection closed");
    }

    private static void runNonBlockingBasic(String[] args) throws Exception {
        if (args.length < 5) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient nonBlockingBasic <host> <port> <dataSize> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        String clientId = args[4];

        byte[] payload = SocketTestData.upperAlphabetData(dataSize);
        String expectedHash = SocketTestData.sha256Hex(payload);
        NonBlockingIoResult result = runNonBlockingBuffers(
            host,
            port,
            new ByteBuffer[] { ByteBuffer.wrap(payload) },
            expectedHash,
            clientId,
            false,
            false,
            0L);
        printNonBlockingOk(clientId, "basic", result);
    }

    private static void runNonBlockingDirect(String[] args) throws Exception {
        if (args.length < 5) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient nonBlockingDirect <host> <port> <dataSize> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        String clientId = args[4];

        byte[] payload = SocketTestData.upperAlphabetData(dataSize);
        String expectedHash = SocketTestData.sha256Hex(payload);
        ByteBuffer directBuffer = ByteBuffer.allocateDirect(payload.length);
        directBuffer.put(payload);
        directBuffer.flip();
        NonBlockingIoResult result = runNonBlockingBuffers(
            host,
            port,
            new ByteBuffer[] { directBuffer },
            expectedHash,
            clientId,
            false,
            false,
            0L);
        printNonBlockingOk(clientId, "direct", result);
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

            String ack = readAck(channel, expectedHash, clientId);
            if (!ack.contains("hash " + expectedHash)) {
                throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected=" + expectedHash + " ack=" + ack);
            }
            System.out.println("[" + clientId + "] Sent " + totalWritten
                + " bytes in chunks of " + chunkSize + ", hash verified");
        }
    }

    private static void runNonBlockingChunked(String[] args) throws Exception {
        if (args.length < 6) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient nonBlockingChunked <host> <port> <dataSize> <chunkSize> <clientId>");
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
        NonBlockingIoResult result = runNonBlockingBuffers(
            host,
            port,
            splitBuffers(payload, chunkSize),
            expectedHash,
            clientId,
            false,
            false,
            0L);
        printNonBlockingOk(clientId, "chunked", result);
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
            long deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
            while (true) {
                int bytesRead = channel.read(responseBuffer);
                if (bytesRead < 0) {
                    throw new EOFException("Peer closed the connection");
                }
                if (bytesRead > 0) {
                    throw new RuntimeException("UNEXPECTED_SUCCESS nread=" + bytesRead);
                }
                if (System.currentTimeMillis() > deadline) {
                    throw new RuntimeException("timeout waiting for peer close");
                }
                Thread.sleep(10L);
            }
        } catch (IOException expected) {
            System.out.println("EXPECTED_PEER_CLOSE: " + expected.getClass().getSimpleName());
        }
    }

    private static void runWriteAfterPeerClose(String[] args) throws Exception {
        if (args.length < 5) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient writeAfterPeerClose <host> <port> <dataSize> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        String clientId = args[4];

        byte[] payload = SocketTestData.upperAlphabetData(dataSize);
        String expectedHash = SocketTestData.sha256Hex(payload);
        ByteBuffer writeBuffer = ByteBuffer.wrap(payload);
        ByteBuffer responseBuffer = ByteBuffer.allocate(1024);
        String ack = "";
        boolean connected = false;
        boolean ackComplete = false;
        boolean eofSeen = false;
        long deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;

        try (SocketChannel channel = SocketChannel.open();
             Selector selector = Selector.open()) {
            channel.configureBlocking(false);
            channel.connect(new InetSocketAddress(host, port));
            channel.register(selector, SelectionKey.OP_CONNECT);

            while (!eofSeen) {
                int ready = selector.select(1000L);
                if (ready == 0) {
                    if (System.currentTimeMillis() > deadline) {
                        throw new RuntimeException("[" + clientId
                            + "] timeout waiting peer close ack=" + ack
                            + " written=" + (payload.length - writeBuffer.remaining()));
                    }
                    continue;
                }

                Set<SelectionKey> selectedKeys = selector.selectedKeys();
                Iterator<SelectionKey> iterator = selectedKeys.iterator();
                while (iterator.hasNext()) {
                    SelectionKey key = iterator.next();
                    iterator.remove();
                    if (!key.isValid()) {
                        continue;
                    }
                    if (key.isConnectable()) {
                        SocketChannel selected = (SocketChannel)key.channel();
                        if (selected.finishConnect()) {
                            connected = true;
                            key.interestOps(SelectionKey.OP_READ | SelectionKey.OP_WRITE);
                            deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                        }
                    }
                    if (key.isWritable() && writeBuffer.hasRemaining()) {
                        int n = channel.write(writeBuffer);
                        if (n < 0) {
                            throw new EOFException("[" + clientId
                                + "] peer closed during initial write");
                        }
                        if (n > 0) {
                            deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                        }
                    }
                    if (key.isReadable()) {
                        int n = channel.read(responseBuffer);
                        if (n < 0) {
                            eofSeen = true;
                            break;
                        }
                        if (n > 0) {
                            responseBuffer.flip();
                            byte[] response = new byte[responseBuffer.remaining()];
                            responseBuffer.get(response);
                            ack += new String(response, StandardCharsets.UTF_8);
                            responseBuffer.clear();
                            ackComplete = ack.contains("hash " + expectedHash);
                            deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                        }
                    }
                    if (key.isValid() && connected) {
                        int ops = SelectionKey.OP_READ;
                        if (writeBuffer.hasRemaining()) {
                            ops |= SelectionKey.OP_WRITE;
                        }
                        key.interestOps(ops);
                    }
                }
            }

            if (!ackComplete) {
                throw new RuntimeException("[" + clientId
                    + "] peer closed before ACK hash, ack=" + ack);
            }

            ByteBuffer lateWrite = ByteBuffer.wrap("late-write".getBytes(StandardCharsets.UTF_8));
            try {
                int n = channel.write(lateWrite);
                if (n > 0) {
                    throw new RuntimeException("[" + clientId
                        + "] write after peer close succeeded n=" + n);
                }
            } catch (IOException expected) {
                System.out.println("[" + clientId + "] write after close rejected: "
                    + expected.getClass().getSimpleName());
            }
        }

        System.out.println("WRITE_AFTER_PEER_CLOSE_OK");
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

    private static void runWaitSendTimeout(String[] args) throws Exception {
        if (args.length < 7) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient waitSendTimeout <host> <port> <firstSize> <secondSize> <pauseMs> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int firstSize = Integer.parseInt(args[3]);
        int secondSize = Integer.parseInt(args[4]);
        long pauseMs = Long.parseLong(args[5]);
        String clientId = args[6];

        byte[] firstPayload = SocketTestData.upperAlphabetData(firstSize);
        byte[] secondPayload = SocketTestData.lowerAlphabetData(secondSize);
        byte[] expectedPayload = new byte[firstSize + secondSize];
        System.arraycopy(firstPayload, 0, expectedPayload, 0, firstSize);
        System.arraycopy(secondPayload, 0, expectedPayload, firstSize, secondSize);
        String expectedHash = SocketTestData.sha256Hex(expectedPayload);

        try (SocketChannel channel = SocketChannel.open()) {
            channel.configureBlocking(true);
            channel.connect(new InetSocketAddress(host, port));
            System.out.println("[" + clientId + "] Connected to server");

            ByteBuffer firstBuffer = ByteBuffer.wrap(firstPayload);
            while (firstBuffer.hasRemaining()) {
                channel.write(firstBuffer);
            }
            System.out.println("[" + clientId + "] First payload sent: " + firstSize);

            Thread.sleep(pauseMs);

            ByteBuffer secondBuffer = ByteBuffer.wrap(secondPayload);
            while (secondBuffer.hasRemaining()) {
                channel.write(secondBuffer);
            }
            System.out.println("[" + clientId + "] Second payload sent: " + secondSize);

            String ack = readAck(channel, expectedHash, clientId);
            if (!ack.contains("hash " + expectedHash)) {
                throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected="
                    + expectedHash + " ack=" + ack);
            }
            System.out.println("[" + clientId + "] Data sent successfully, hash verified");
        }
    }

    private static void runMultiWrite(String[] args) throws Exception {
        if (args.length < 6) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient multiWrite <host> <port> <chunkSize> <chunkCount> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int chunkSize = Integer.parseInt(args[3]);
        int chunkCount = Integer.parseInt(args[4]);
        String clientId = args[5];
        runMultiWrite0(host, port, chunkSize, chunkCount, 0L, clientId);
    }

    private static void runPacedMultiWrite(String[] args) throws Exception {
        if (args.length < 7) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient pacedMultiWrite <host> <port> <chunkSize> <chunkCount> <pauseMillis> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int chunkSize = Integer.parseInt(args[3]);
        int chunkCount = Integer.parseInt(args[4]);
        long pauseMillis = Long.parseLong(args[5]);
        String clientId = args[6];
        runMultiWrite0(host, port, chunkSize, chunkCount, pauseMillis, clientId);
    }

    private static void runNonBlockingPacedMultiWrite(String[] args) throws Exception {
        if (args.length < 7) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient nonBlockingPacedMultiWrite <host> <port> <chunkSize> <chunkCount> <pauseMillis> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int chunkSize = Integer.parseInt(args[3]);
        int chunkCount = Integer.parseInt(args[4]);
        long pauseMillis = Long.parseLong(args[5]);
        String clientId = args[6];
        if (chunkSize <= 0 || chunkCount <= 0) {
            throw new IllegalArgumentException("chunkSize and chunkCount must be positive");
        }
        if (pauseMillis < 0L) {
            throw new IllegalArgumentException("pauseMillis must not be negative");
        }

        MessageDigest expectedDigest = MessageDigest.getInstance("SHA-256");
        ByteBuffer[] buffers = new ByteBuffer[chunkCount];
        for (int chunk = 0; chunk < chunkCount; chunk++) {
            byte[] payload = SocketTestData.scopedUpperAlphabetData(chunk, 0, chunkSize);
            expectedDigest.update(payload);
            buffers[chunk] = ByteBuffer.wrap(payload);
        }

        String expectedHash = toHex(expectedDigest.digest());
        NonBlockingIoResult result = runNonBlockingBuffers(
            host,
            port,
            buffers,
            expectedHash,
            clientId,
            false,
            false,
            pauseMillis);
        printNonBlockingOk(clientId, "pacedMultiWrite", result);
    }

    private static void runMultiWrite0(String host, int port, int chunkSize,
                                       int chunkCount, long pauseMillis,
                                       String clientId) throws Exception {
        if (chunkSize <= 0 || chunkCount <= 0) {
            throw new IllegalArgumentException("chunkSize and chunkCount must be positive");
        }
        if (pauseMillis < 0L) {
            throw new IllegalArgumentException("pauseMillis must not be negative");
        }

        MessageDigest expectedDigest = MessageDigest.getInstance("SHA-256");
        long totalWritten = 0L;

        try (SocketChannel channel = SocketChannel.open()) {
            channel.configureBlocking(true);
            channel.connect(new InetSocketAddress(host, port));
            System.out.println("[" + clientId + "] Connected to server");

            for (int chunk = 0; chunk < chunkCount; chunk++) {
                byte[] payload = SocketTestData.scopedUpperAlphabetData(chunk, 0, chunkSize);
                expectedDigest.update(payload);
                ByteBuffer writeBuffer = ByteBuffer.wrap(payload);
                long chunkWritten = 0L;
                while (writeBuffer.hasRemaining()) {
                    int n = channel.write(writeBuffer);
                    totalWritten += n;
                    chunkWritten += n;
                }
                System.out.println("[" + clientId + "] chunk " + chunk
                    + " sent: " + chunkWritten + " bytes");
                if (pauseMillis > 0L && chunk + 1 < chunkCount) {
                    Thread.sleep(pauseMillis);
                }
            }

            String expectedHash = toHex(expectedDigest.digest());
            String ack = readAck(channel, expectedHash, clientId);
            if (!ack.contains("hash " + expectedHash)) {
                throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected="
                    + expectedHash + " ack=" + ack);
            }
            System.out.println("[" + clientId + "] Sent " + totalWritten
                + " bytes in " + chunkCount + " chunks, hash verified");
        }
    }

    private static void runParallelMultiWrite(String[] args) throws Exception {
        if (args.length < 7) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient parallelMultiWrite <host> <port> <chunkSize> <chunkCount> <clientCount> <clientIdPrefix>");
        }

        final String host = args[1];
        final int port = Integer.parseInt(args[2]);
        final int chunkSize = Integer.parseInt(args[3]);
        final int chunkCount = Integer.parseInt(args[4]);
        int clientCount = Integer.parseInt(args[5]);
        final String clientIdPrefix = args[6];
        if (clientCount <= 0) {
            throw new IllegalArgumentException("clientCount must be positive");
        }

        final CountDownLatch startGate = new CountDownLatch(1);
        CountDownLatch doneGate = new CountDownLatch(clientCount);
        List<Throwable> errors = Collections.synchronizedList(new ArrayList<Throwable>());
        for (int i = 0; i < clientCount; i++) {
            final int clientIndex = i;
            Thread thread = new Thread(new Runnable() {
                @Override
                public void run() {
                    try {
                        startGate.await();
                        runMultiWrite(new String[] {
                            "multiWrite",
                            host,
                            String.valueOf(port),
                            String.valueOf(chunkSize),
                            String.valueOf(chunkCount),
                            clientIdPrefix + "-" + clientIndex
                        });
                    } catch (Throwable t) {
                        errors.add(t);
                    } finally {
                        doneGate.countDown();
                    }
                }
            }, "parallel-multi-write-client-" + i);
            thread.start();
        }

        startGate.countDown();
        doneGate.await();
        if (!errors.isEmpty()) {
            throw new RuntimeException("Parallel multi-write client failed, total errors="
                + errors.size(), errors.get(0));
        }
        System.out.println("PARALLEL_MULTI_WRITE_OK");
    }

    private static void runGatherScatter(String[] args) throws Exception {
        if (args.length < 6) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient gatherScatter <host> <port> <dataSize> <segmentSize> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        int segmentSize = Integer.parseInt(args[4]);
        String clientId = args[5];
        if (segmentSize <= 0) {
            throw new IllegalArgumentException("segmentSize must be positive");
        }

        byte[] payload = SocketTestData.upperAlphabetData(dataSize);
        String expectedHash = SocketTestData.sha256Hex(payload);

        try (SocketChannel channel = SocketChannel.open()) {
            channel.configureBlocking(true);
            channel.connect(new InetSocketAddress(host, port));
            System.out.println("[" + clientId + "] Connected to server");

            ByteBuffer[] writeBuffers = splitBuffers(payload, segmentSize);
            long totalWritten = 0L;
            while (totalWritten < payload.length) {
                long n = channel.write(writeBuffers);
                if (n <= 0L) {
                    throw new RuntimeException("[" + clientId + "] gather write stalled at "
                        + totalWritten + "/" + payload.length);
                }
                totalWritten += n;
            }

            ByteBuffer[] ackBuffers = newAckBuffers();
            String ack = readScatterAck(channel, ackBuffers, expectedHash, clientId);
            long totalRead = totalPosition(ackBuffers);
            if (!ack.contains("hash " + expectedHash)) {
                throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected="
                    + expectedHash + " ack=" + ack);
            }
            System.out.println("[" + clientId + "] gather write sent " + totalWritten
                + " bytes, scatter read " + totalRead + " bytes, hash verified");
        }
    }

    private static void runNonBlockingGatherScatter(String[] args) throws Exception {
        if (args.length < 6) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient nonBlockingGatherScatter <host> <port> <dataSize> <segmentSize> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        int segmentSize = Integer.parseInt(args[4]);
        String clientId = args[5];
        if (segmentSize <= 0) {
            throw new IllegalArgumentException("segmentSize must be positive");
        }

        byte[] payload = SocketTestData.upperAlphabetData(dataSize);
        String expectedHash = SocketTestData.sha256Hex(payload);
        NonBlockingIoResult result = runNonBlockingBuffers(
            host,
            port,
            splitBuffers(payload, segmentSize),
            expectedHash,
            clientId,
            true,
            true,
            0L);
        printNonBlockingOk(clientId, "gatherScatter", result);
    }

    private static String readAck(SocketChannel channel, String expectedHash,
                                  String clientId) throws Exception {
        ByteBuffer responseBuffer = ByteBuffer.allocate(1024);
        long deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
        String ack = "";
        while (!ack.contains("hash " + expectedHash)) {
            int bytesRead = channel.read(responseBuffer);
            if (bytesRead < 0) {
                throw new RuntimeException("[" + clientId
                    + "] peer closed before complete ACK: " + ack);
            }
            if (bytesRead == 0) {
                if (System.currentTimeMillis() > deadline) {
                    throw new RuntimeException("[" + clientId
                        + "] timeout waiting for ACK: " + ack);
                }
                Thread.sleep(10L);
                continue;
            }
            responseBuffer.flip();
            byte[] response = new byte[responseBuffer.remaining()];
            responseBuffer.get(response);
            ack += new String(response, StandardCharsets.UTF_8);
            responseBuffer.clear();
            deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
        }
        return ack;
    }

    private static ByteBuffer[] newAckBuffers() {
        return new ByteBuffer[] {
            ByteBuffer.allocate(7),
            ByteBuffer.allocate(17),
            ByteBuffer.allocate(128)
        };
    }

    private static String readScatterAck(SocketChannel channel, ByteBuffer[] ackBuffers,
                                         String expectedHash, String clientId)
        throws Exception {
        String ack = "";
        long deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
        while (!ack.contains("hash " + expectedHash)) {
            long n = channel.read(ackBuffers);
            if (n < 0L) {
                throw new RuntimeException("[" + clientId
                    + "] peer closed before complete ACK: " + ack);
            }
            if (n == 0L) {
                if (System.currentTimeMillis() > deadline) {
                    throw new RuntimeException("[" + clientId
                        + "] timeout waiting for scatter ACK: " + ack);
                }
                Thread.sleep(10L);
                continue;
            }
            ack = readBuffersAsString(ackBuffers);
            deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
            if (!hasRemaining(ackBuffers) && !ack.contains("hash " + expectedHash)) {
                throw new RuntimeException("[" + clientId
                    + "] ACK exceeded scatter buffers: " + ack);
            }
        }
        return ack;
    }

    private static NonBlockingIoResult runNonBlockingBuffers(
            String host,
            int port,
            ByteBuffer[] writeBuffers,
            String expectedHash,
            String clientId,
            boolean gatheringWrite,
            boolean scatterAck,
            long pauseMillis) throws Exception {
        NonBlockingIoResult result = new NonBlockingIoResult();
        ByteBuffer responseBuffer = scatterAck ? null : ByteBuffer.allocate(1024);
        ByteBuffer[] ackBuffers = scatterAck ? newAckBuffers() : null;
        int writeIndex = 0;
        int gatherIndex = 0;
        boolean writeDone = !hasRemaining(writeBuffers);
        String ack = "";
        long deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;

        SocketChannel channel = SocketChannel.open();
        Selector selector = Selector.open();
        try {
            channel.configureBlocking(false);
            channel.connect(new InetSocketAddress(host, port));
            channel.register(selector, SelectionKey.OP_CONNECT);

            while (!ack.contains("hash " + expectedHash)) {
                int ready = selector.select(1000L);
                if (ready == 0) {
                    if (System.currentTimeMillis() > deadline) {
                        throw new RuntimeException("[" + clientId
                            + "] timeout in non-blocking IO"
                            + " bytesWritten=" + result.bytesWritten
                            + " bytesRead=" + result.bytesRead
                            + " ack=" + ack);
                    }
                    continue;
                }

                Set<SelectionKey> selectedKeys = selector.selectedKeys();
                Iterator<SelectionKey> iterator = selectedKeys.iterator();
                while (iterator.hasNext()) {
                    SelectionKey selected = iterator.next();
                    iterator.remove();
                    if (!selected.isValid()) {
                        continue;
                    }
                    if (selected.isConnectable()) {
                        SocketChannel sc = (SocketChannel)selected.channel();
                        if (sc.finishConnect()) {
                            selected.interestOps(SelectionKey.OP_READ
                                | (writeDone ? 0 : SelectionKey.OP_WRITE));
                            deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                        }
                    }
                    if (selected.isWritable() && !writeDone) {
                        if (gatheringWrite) {
                            while (gatherIndex < writeBuffers.length
                                    && !writeBuffers[gatherIndex].hasRemaining()) {
                                gatherIndex++;
                            }
                            if (gatherIndex >= writeBuffers.length) {
                                writeDone = true;
                            } else {
                                int batchCount = Math.min(writeBuffers.length - gatherIndex, 64);
                                long remainingBefore =
                                    totalRemaining(writeBuffers, gatherIndex, batchCount);
                                long n = channel.write(writeBuffers, gatherIndex, batchCount);
                                if (n < 0L) {
                                    throw new RuntimeException("[" + clientId
                                        + "] channel closed during non-blocking gather write");
                                }
                                if (n == 0L) {
                                    result.zeroWrites++;
                                } else {
                                    result.bytesWritten += n;
                                    if (n < remainingBefore) {
                                        result.partialWrites++;
                                    }
                                    deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                                }
                                while (gatherIndex < writeBuffers.length
                                        && !writeBuffers[gatherIndex].hasRemaining()) {
                                    gatherIndex++;
                                }
                                writeDone = gatherIndex >= writeBuffers.length;
                            }
                        } else {
                            while (writeIndex < writeBuffers.length) {
                                ByteBuffer current = writeBuffers[writeIndex];
                                int remainingBefore = current.remaining();
                                int n = channel.write(current);
                                if (n < 0) {
                                    throw new RuntimeException("[" + clientId
                                        + "] channel closed during non-blocking write");
                                }
                                if (n == 0) {
                                    result.zeroWrites++;
                                    break;
                                }
                                result.bytesWritten += n;
                                if (n < remainingBefore) {
                                    result.partialWrites++;
                                }
                                deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                                if (current.hasRemaining()) {
                                    break;
                                }
                                writeIndex++;
                                if (pauseMillis > 0L && writeIndex < writeBuffers.length) {
                                    Thread.sleep(pauseMillis);
                                    break;
                                }
                            }
                            writeDone = writeIndex >= writeBuffers.length;
                        }
                    }
                    if (selected.isReadable()) {
                        if (scatterAck) {
                            long n = channel.read(ackBuffers);
                            if (n < 0L) {
                                throw new RuntimeException("[" + clientId
                                    + "] peer closed before complete non-blocking ACK: " + ack);
                            }
                            if (n == 0L) {
                                result.zeroReads++;
                            } else {
                                result.bytesRead += n;
                                ack = readBuffersAsString(ackBuffers);
                                deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                                if (!hasRemaining(ackBuffers)
                                        && !ack.contains("hash " + expectedHash)) {
                                    throw new RuntimeException("[" + clientId
                                        + "] ACK exceeded scatter buffers: " + ack);
                                }
                            }
                        } else {
                            int n = channel.read(responseBuffer);
                            if (n < 0) {
                                throw new RuntimeException("[" + clientId
                                    + "] peer closed before complete non-blocking ACK: " + ack);
                            }
                            if (n == 0) {
                                result.zeroReads++;
                            } else {
                                result.bytesRead += n;
                                responseBuffer.flip();
                                byte[] response = new byte[responseBuffer.remaining()];
                                responseBuffer.get(response);
                                ack += new String(response, StandardCharsets.UTF_8);
                                responseBuffer.clear();
                                deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                            }
                        }
                    }
                    if (selected.isValid() && channel.isConnected()) {
                        int ops = SelectionKey.OP_READ;
                        if (!writeDone) {
                            ops |= SelectionKey.OP_WRITE;
                        }
                        selected.interestOps(ops);
                    }
                }
            }
        } finally {
            selector.close();
            channel.close();
        }

        if (!ack.contains("hash " + expectedHash)) {
            throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected="
                + expectedHash + " ack=" + ack);
        }
        return result;
    }

    private static void printNonBlockingOk(String clientId, String api,
                                           NonBlockingIoResult result) {
        System.out.println("NONBLOCKING_IO_OK id=" + clientId
            + " api=" + api
            + " bytesWritten=" + result.bytesWritten
            + " bytesRead=" + result.bytesRead
            + " zeroReads=" + result.zeroReads
            + " zeroWrites=" + result.zeroWrites
            + " partialWrites=" + result.partialWrites);
        System.out.println("[" + clientId + "] Data sent successfully, hash verified");
    }

    private static long totalPosition(ByteBuffer[] buffers) {
        long total = 0L;
        for (ByteBuffer buffer : buffers) {
            total += buffer.position();
        }
        return total;
    }

    private static long totalRemaining(ByteBuffer[] buffers) {
        return totalRemaining(buffers, 0, buffers.length);
    }

    private static long totalRemaining(ByteBuffer[] buffers, int offset, int length) {
        long total = 0L;
        int end = offset + length;
        for (int i = offset; i < end; i++) {
            total += buffers[i].remaining();
        }
        return total;
    }

    private static ByteBuffer[] splitBuffers(byte[] payload, int segmentSize) {
        int count = (payload.length + segmentSize - 1) / segmentSize;
        ByteBuffer[] buffers = new ByteBuffer[count];
        for (int i = 0; i < count; i++) {
            int offset = i * segmentSize;
            int len = Math.min(segmentSize, payload.length - offset);
            buffers[i] = ByteBuffer.wrap(payload, offset, len);
        }
        return buffers;
    }

    private static boolean hasRemaining(ByteBuffer[] buffers) {
        for (ByteBuffer buffer : buffers) {
            if (buffer.hasRemaining()) {
                return true;
            }
        }
        return false;
    }

    private static String readBuffersAsString(ByteBuffer[] buffers) {
        int total = 0;
        for (ByteBuffer buffer : buffers) {
            total += buffer.position();
        }
        byte[] data = new byte[total];
        int offset = 0;
        for (ByteBuffer buffer : buffers) {
            int position = buffer.position();
            ByteBuffer duplicate = buffer.duplicate();
            duplicate.flip();
            duplicate.get(data, offset, position);
            offset += position;
        }
        return new String(data, StandardCharsets.UTF_8);
    }

    private static String toHex(byte[] bytes) {
        StringBuilder sb = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) {
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }

    private static final class NonBlockingIoResult {
        private long bytesWritten;
        private long bytesRead;
        private long zeroReads;
        private long zeroWrites;
        private long partialWrites;
    }

    private static void runTransferTo(String[] args) throws Exception {
        if (args.length < 5) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient transferTo <host> <port> <dataSize> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int dataSize = Integer.parseInt(args[3]);
        String clientId = args[4];

        byte[] payload = SocketTestData.upperAlphabetData(dataSize);
        String expectedHash = SocketTestData.sha256Hex(payload);
        Path file = Files.createTempFile("application_ubsocket-transferTo-", ".dat");
        try {
            try (FileOutputStream out = new FileOutputStream(file.toFile())) {
                out.write(payload);
            }

            try (FileChannel fileChannel = FileChannel.open(file);
                 SocketChannel channel = SocketChannel.open();
                 Selector selector = Selector.open()) {
                channel.configureBlocking(false);
                channel.connect(new InetSocketAddress(host, port));
                channel.register(selector, SelectionKey.OP_CONNECT);

                long transferred = 0L;
                ByteBuffer responseBuffer = ByteBuffer.allocate(1024);
                long deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                String ack = "";
                while (transferred < payload.length || !ack.contains("hash " + expectedHash)) {
                    int ready = selector.select(1000L);
                    if (ready == 0) {
                        if (System.currentTimeMillis() > deadline) {
                            throw new RuntimeException("[" + clientId
                                + "] transferTo timeout transferred=" + transferred
                                + "/" + payload.length + " ack=" + ack);
                        }
                        continue;
                    }

                    Set<SelectionKey> selectedKeys = selector.selectedKeys();
                    Iterator<SelectionKey> iterator = selectedKeys.iterator();
                    while (iterator.hasNext()) {
                        SelectionKey key = iterator.next();
                        iterator.remove();
                        if (!key.isValid()) {
                            continue;
                        }
                        if (key.isConnectable()) {
                            SocketChannel selected = (SocketChannel)key.channel();
                            if (selected.finishConnect()) {
                                key.interestOps(SelectionKey.OP_READ | SelectionKey.OP_WRITE);
                                deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                            }
                        }
                        if (key.isWritable() && transferred < payload.length) {
                            long n = fileChannel.transferTo(transferred,
                                                            payload.length - transferred,
                                                            channel);
                            if (n < 0L) {
                                throw new RuntimeException("[" + clientId
                                    + "] transferTo failed at " + transferred
                                    + "/" + payload.length);
                            }
                            if (n > 0L) {
                                transferred += n;
                                deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                            }
                        }
                        if (key.isReadable()) {
                            int bytesRead = channel.read(responseBuffer);
                            if (bytesRead < 0) {
                                throw new RuntimeException("[" + clientId
                                    + "] peer closed before complete ACK: " + ack);
                            }
                            if (bytesRead > 0) {
                                responseBuffer.flip();
                                byte[] response = new byte[responseBuffer.remaining()];
                                responseBuffer.get(response);
                                ack += new String(response, StandardCharsets.UTF_8);
                                responseBuffer.clear();
                                deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                            }
                        }
                        if (key.isValid()) {
                            int ops = SelectionKey.OP_READ;
                            if (transferred < payload.length) {
                                ops |= SelectionKey.OP_WRITE;
                            }
                            key.interestOps(ops);
                        }
                    }
                }
                System.out.println("[" + clientId + "] transferTo sent " + transferred + " bytes");
                if (!ack.contains("hash " + expectedHash)) {
                    throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected="
                        + expectedHash + " ack=" + ack);
                }
                System.out.println("[" + clientId + "] Data sent successfully, hash verified");
            }
        } finally {
            Files.deleteIfExists(file);
        }
    }

    private static void runEchoFrames(String[] args) throws Exception {
        if (args.length < 8) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient echoFrames <host> <port> <requestSize> <responseSize> <inflight> <frames> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int requestSize = Integer.parseInt(args[3]);
        int responseSize = Integer.parseInt(args[4]);
        int inflight = Integer.parseInt(args[5]);
        int frames = Integer.parseInt(args[6]);
        String clientId = args[7];
        if (requestSize < ECHO_FRAME_HEADER_SIZE || responseSize < ECHO_FRAME_HEADER_SIZE) {
            throw new IllegalArgumentException("frame sizes must be at least "
                + ECHO_FRAME_HEADER_SIZE);
        }
        if (inflight <= 0 || frames <= 0) {
            throw new IllegalArgumentException("inflight and frames must be positive");
        }

        boolean[] active = new boolean[frames];
        ArrayDeque<ByteBuffer> pendingWrites = new ArrayDeque<ByteBuffer>();
        ByteBuffer readBuffer = ByteBuffer.allocate(Math.max(responseSize * Math.min(inflight, 16),
                                                            responseSize * 2));

        int nextSequence = 0;
        int outstanding = 0;
        int completed = 0;
        long bytesWritten = 0L;
        long bytesRead = 0L;
        long deadline = System.currentTimeMillis() + ECHO_SELECT_TIMEOUT_MS;

        SocketChannel channel = SocketChannel.open();
        Selector selector = Selector.open();
        try {
            channel.configureBlocking(false);
            channel.connect(new InetSocketAddress(host, port));
            SelectionKey key = channel.register(selector, SelectionKey.OP_CONNECT);

            while (completed < frames) {
                int ready = selector.select(1000L);
                if (ready == 0) {
                    if (System.currentTimeMillis() > deadline) {
                        throw new RuntimeException("[" + clientId
                            + "] timeout completed=" + completed + "/" + frames
                            + ", outstanding=" + outstanding);
                    }
                    continue;
                }
                deadline = System.currentTimeMillis() + ECHO_SELECT_TIMEOUT_MS;

                Set<SelectionKey> selectedKeys = selector.selectedKeys();
                Iterator<SelectionKey> iterator = selectedKeys.iterator();
                while (iterator.hasNext()) {
                    SelectionKey selected = iterator.next();
                    iterator.remove();
                    if (!selected.isValid()) {
                        continue;
                    }
                    if (selected.isConnectable()) {
                        SocketChannel sc = (SocketChannel)selected.channel();
                        while (!sc.finishConnect()) {
                            // finishConnect is non-blocking; retry on the next selected event.
                        }
                        selected.interestOps(SelectionKey.OP_READ | SelectionKey.OP_WRITE);
                    }
                    if (selected.isWritable()) {
                        while (outstanding < inflight && nextSequence < frames) {
                            pendingWrites.add(makeEchoRequest(nextSequence, requestSize));
                            active[nextSequence] = true;
                            nextSequence++;
                            outstanding++;
                        }
                        while (!pendingWrites.isEmpty()) {
                            ByteBuffer head = pendingWrites.peek();
                            int n = channel.write(head);
                            if (n < 0) {
                                throw new RuntimeException("[" + clientId
                                    + "] channel closed during write");
                            }
                            if (n == 0) {
                                break;
                            }
                            bytesWritten += n;
                            if (!head.hasRemaining()) {
                                pendingWrites.remove();
                            }
                        }
                    }
                    if (selected.isReadable()) {
                        int n = channel.read(readBuffer);
                        if (n < 0) {
                            throw new RuntimeException("[" + clientId
                                + "] channel closed during read completed="
                                + completed + "/" + frames);
                        }
                        if (n > 0) {
                            bytesRead += n;
                            readBuffer.flip();
                            while (readBuffer.remaining() >= responseSize) {
                                int frameStart = readBuffer.position();
                                long sequence = readBuffer.getLong();
                                if (sequence < 0L || sequence >= frames) {
                                    throw new RuntimeException("[" + clientId
                                        + "] invalid response sequence=" + sequence);
                                }
                                int index = (int)sequence;
                                if (!active[index]) {
                                    throw new RuntimeException("[" + clientId
                                        + "] duplicate or stale response sequence=" + sequence);
                                }
                                verifyEchoPayload(readBuffer, frameStart, responseSize,
                                                  index, false);
                                active[index] = false;
                                completed++;
                                outstanding--;
                                readBuffer.position(frameStart + responseSize);
                            }
                            readBuffer.compact();
                        }
                    }
                    int ops = SelectionKey.OP_READ;
                    if (!pendingWrites.isEmpty()
                            || (outstanding < inflight && nextSequence < frames)) {
                        ops |= SelectionKey.OP_WRITE;
                    }
                    if (selected.isValid()) {
                        selected.interestOps(ops);
                    }
                }
            }
        } finally {
            selector.close();
            channel.close();
        }

        System.out.println("ECHO_CLIENT_OK id=" + clientId
            + " frames=" + frames
            + " bytesWritten=" + bytesWritten
            + " bytesRead=" + bytesRead
            + " inflight=" + inflight);
    }

    private static void runHoldConnections(String[] args) throws Exception {
        if (args.length < 6) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient holdConnections <host> <port> <clientCount> <holdMillis> <clientId>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        int clientCount = Integer.parseInt(args[3]);
        long holdMillis = Long.parseLong(args[4]);
        String clientId = args[5];
        ArrayList<SocketChannel> channels = new ArrayList<SocketChannel>();
        try {
            for (int i = 0; i < clientCount; i++) {
                SocketChannel channel = SocketChannel.open();
                channel.configureBlocking(false);
                channel.connect(new InetSocketAddress(host, port));
                long deadline = System.currentTimeMillis() + CONNECT_DEADLINE_MS;
                while (!channel.finishConnect()) {
                    if (System.currentTimeMillis() >= deadline) {
                        throw new ConnectException("connect timeout to " + host + ":" + port);
                    }
                    Thread.sleep(RETRY_INTERVAL_MS);
                }
                channels.add(channel);
            }
            Thread.sleep(holdMillis);
        } finally {
            for (SocketChannel channel : channels) {
                try {
                    channel.close();
                } catch (IOException ignore) {
                }
            }
        }

        System.out.println("HOLD_CONNECTIONS_OK id=" + clientId
            + " clients=" + clientCount);
    }

    private static void runSwitchBlockingAfterAttach(String[] args) throws Exception {
        if (args.length < 5) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioClient switchBlockingAfterAttach <host> <port> <clientId> <holdMillis>");
        }

        String host = args[1];
        int port = Integer.parseInt(args[2]);
        String clientId = args[3];
        long holdMillis = Long.parseLong(args[4]);

        SocketChannel channel = SocketChannel.open();
        try {
            channel.configureBlocking(false);
            channel.connect(new InetSocketAddress(host, port));
            long deadline = System.currentTimeMillis() + CONNECT_DEADLINE_MS;
            while (!channel.finishConnect()) {
                if (System.currentTimeMillis() >= deadline) {
                    throw new ConnectException("connect timeout to " + host + ":" + port);
                }
                Thread.sleep(1L);
            }

            channel.configureBlocking(true);
            System.out.println("[" + clientId + "] BLOCKING_SWITCH_ALLOWED");
            if (holdMillis > 0L) {
                Thread.sleep(holdMillis);
            }
        } finally {
            channel.close();
        }
    }

    static ByteBuffer makeEchoRequest(int sequence, int size) {
        ByteBuffer buffer = ByteBuffer.allocate(size);
        buffer.putLong((long)sequence);
        for (int i = ECHO_FRAME_HEADER_SIZE; i < size; i++) {
            buffer.put((byte)((sequence + i) & 0xff));
        }
        buffer.flip();
        return buffer;
    }

    static ByteBuffer makeEchoResponse(int sequence, int size) {
        ByteBuffer buffer = ByteBuffer.allocate(size);
        buffer.putLong((long)sequence);
        for (int i = ECHO_FRAME_HEADER_SIZE; i < size; i++) {
            buffer.put((byte)((sequence * 31 + i) & 0xff));
        }
        buffer.flip();
        return buffer;
    }

    static void verifyEchoPayload(ByteBuffer buffer, int frameStart, int frameSize,
                                  int sequence, boolean request) {
        for (int i = ECHO_FRAME_HEADER_SIZE; i < frameSize; i++) {
            byte actual = buffer.get(frameStart + i);
            byte expected = request
                ? (byte)((sequence + i) & 0xff)
                : (byte)((sequence * 31 + i) & 0xff);
            if (actual != expected) {
                throw new RuntimeException("frame payload mismatch sequence=" + sequence
                    + " offset=" + i + " expected=" + (expected & 0xff)
                    + " actual=" + (actual & 0xff));
            }
        }
    }

    private static String connectSendAndReadAck(String host, int port, byte[] payload,
                                                boolean retryConnect, int round) throws Exception {
        long deadline = System.currentTimeMillis() + CONNECT_DEADLINE_MS;
        while (true) {
            OpenSendResult result = null;
            try {
                result = openAndSendNonBlocking(host, port, payload, "Round " + round);
                return result.ack;
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
            } finally {
                if (result != null) {
                    result.channel.close();
                }
            }
        }
    }

    private static SocketChannel openAndSend(String host, int port, byte[] payload,
                                             String clientId) throws Exception {
        OpenSendResult result = openAndSendNonBlocking(host, port, payload, clientId);
        System.out.println("[" + clientId + "] ack: " + result.ack);
        System.out.println("[" + clientId + "] sent " + result.bytesWritten
            + " bytes, hash verified");
        return result.channel;
    }

    private static OpenSendResult openAndSendNonBlocking(String host, int port,
                                                         byte[] payload,
                                                         String clientId) throws Exception {
        String expectedHash = SocketTestData.sha256Hex(payload);
        SocketChannel channel = SocketChannel.open();
        boolean success = false;
        ByteBuffer writeBuffer = ByteBuffer.wrap(payload);
        ByteBuffer responseBuffer = ByteBuffer.allocate(1024);
        long totalWritten = 0L;
        long deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
        String ack = "";

        try (Selector selector = Selector.open()) {
            channel.configureBlocking(false);
            boolean connected = channel.connect(new InetSocketAddress(host, port));
            channel.register(selector, connected
                ? SelectionKey.OP_READ | SelectionKey.OP_WRITE
                : SelectionKey.OP_CONNECT);

            while (!ack.contains("hash " + expectedHash)) {
                int ready = selector.select(1000L);
                if (ready == 0) {
                    if (System.currentTimeMillis() > deadline) {
                        throw new RuntimeException("[" + clientId
                            + "] timeout waiting for ACK: " + ack);
                    }
                    continue;
                }

                Set<SelectionKey> selectedKeys = selector.selectedKeys();
                Iterator<SelectionKey> iterator = selectedKeys.iterator();
                while (iterator.hasNext()) {
                    SelectionKey key = iterator.next();
                    iterator.remove();
                    if (!key.isValid()) {
                        continue;
                    }
                    if (key.isConnectable()) {
                        SocketChannel selected = (SocketChannel)key.channel();
                        if (selected.finishConnect()) {
                            key.interestOps(SelectionKey.OP_READ | SelectionKey.OP_WRITE);
                            deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                        }
                    }
                    if (key.isWritable() && writeBuffer.hasRemaining()) {
                        int n = channel.write(writeBuffer);
                        if (n < 0) {
                            throw new EOFException("[" + clientId
                                + "] channel closed during non-blocking write");
                        }
                        if (n > 0) {
                            totalWritten += n;
                            deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                        }
                    }
                    if (key.isReadable()) {
                        int n = channel.read(responseBuffer);
                        if (n < 0) {
                            throw new EOFException("[" + clientId
                                + "] peer closed before complete ACK: " + ack);
                        }
                        if (n > 0) {
                            responseBuffer.flip();
                            byte[] response = new byte[responseBuffer.remaining()];
                            responseBuffer.get(response);
                            ack += new String(response, StandardCharsets.UTF_8);
                            responseBuffer.clear();
                            deadline = System.currentTimeMillis() + ACK_DEADLINE_MS;
                        }
                    }
                    if (key.isValid() && channel.isConnected()) {
                        int ops = SelectionKey.OP_READ;
                        if (writeBuffer.hasRemaining()) {
                            ops |= SelectionKey.OP_WRITE;
                        }
                        key.interestOps(ops);
                    }
                }
            }

            if (totalWritten != payload.length) {
                throw new RuntimeException("[" + clientId + "] incomplete write "
                    + totalWritten + "/" + payload.length);
            }
            success = true;
            return new OpenSendResult(channel, ack, totalWritten);
        } finally {
            if (!success) {
                channel.close();
            }
        }
    }

    private static final class OpenSendResult {
        private final SocketChannel channel;
        private final String ack;
        private final long bytesWritten;

        private OpenSendResult(SocketChannel channel, String ack, long bytesWritten) {
            this.channel = channel;
            this.ack = ack;
            this.bytesWritten = bytesWritten;
        }
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
