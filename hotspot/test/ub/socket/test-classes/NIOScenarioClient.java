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
import java.nio.channels.FileChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
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
        } else if ("waitSendTimeout".equals(mode)) {
            runWaitSendTimeout(args);
        } else if ("multiWrite".equals(mode)) {
            runMultiWrite(args);
        } else if ("parallelMultiWrite".equals(mode)) {
            runParallelMultiWrite(args);
        } else if ("gatherScatter".equals(mode)) {
            runGatherScatter(args);
        } else if ("transferTo".equals(mode)) {
            runTransferTo(args);
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
        if (chunkSize <= 0 || chunkCount <= 0) {
            throw new IllegalArgumentException("chunkSize and chunkCount must be positive");
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
            }

            String expectedHash = toHex(expectedDigest.digest());
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

            ByteBuffer[] ackBuffers = new ByteBuffer[] {
                ByteBuffer.allocate(7),
                ByteBuffer.allocate(17),
                ByteBuffer.allocate(128)
            };
            long totalRead = 0L;
            String ack = "";
            while (!ack.contains("hash " + expectedHash)) {
                long n = channel.read(ackBuffers);
                if (n <= 0L) {
                    throw new RuntimeException("[" + clientId + "] did not receive complete ACK");
                }
                totalRead += n;
                ack = readBuffersAsString(ackBuffers);
                if (!hasRemaining(ackBuffers)) {
                    break;
                }
            }
            if (!ack.contains("hash " + expectedHash)) {
                throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected="
                    + expectedHash + " ack=" + ack);
            }
            System.out.println("[" + clientId + "] gather write sent " + totalWritten
                + " bytes, scatter read " + totalRead + " bytes, hash verified");
        }
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
                 SocketChannel channel = SocketChannel.open()) {
                channel.configureBlocking(true);
                channel.connect(new InetSocketAddress(host, port));

                long transferred = 0L;
                while (transferred < payload.length) {
                    long n = fileChannel.transferTo(transferred,
                                                    payload.length - transferred,
                                                    channel);
                    if (n <= 0L) {
                        throw new RuntimeException("[" + clientId + "] transferTo stalled at "
                            + transferred + "/" + payload.length);
                    }
                    transferred += n;
                }
                System.out.println("[" + clientId + "] transferTo sent " + transferred + " bytes");

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
                    throw new RuntimeException("[" + clientId + "] HASH_MISMATCH expected="
                        + expectedHash + " ack=" + ack);
                }
                System.out.println("[" + clientId + "] Data sent successfully, hash verified");
            }
        } finally {
            Files.deleteIfExists(file);
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
