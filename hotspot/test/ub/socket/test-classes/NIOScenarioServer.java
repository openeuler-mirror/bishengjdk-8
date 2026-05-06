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
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.CancelledKeyException;
import java.nio.channels.Channel;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

public class NIOScenarioServer {
    private static final int SELECTOR_MAX_READ_SIZE = 65536;
    private static final int SELECTOR_MAX_READ_COUNT = 16;
    private static final int SELECTOR_TIMEOUT_MS = 10000;

    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            throw new IllegalArgumentException("Usage: NIOScenarioServer <mode> ...");
        }

        String mode = args[0];
        if ("selector".equals(mode)) {
            runSelector(args);
        } else if ("delayedAccept".equals(mode)) {
            runDelayedAccept(args);
        } else if ("delayedRead".equals(mode)) {
            runDelayedRead(args);
        } else if ("earlyClose".equals(mode)) {
            runEarlyClose(args);
        } else {
            throw new IllegalArgumentException("Unknown mode: " + mode);
        }
    }

    private static void runSelector(String[] args) throws Exception {
        if (args.length < 4) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioServer selector <port> <expectedSize> <clientCount>");
        }

        int port = Integer.parseInt(args[1]);
        final long expectedSize = Long.parseLong(args[2]);
        int clientCount = Integer.parseInt(args[3]);

        ServerSocketChannel server = ServerSocketChannel.open();
        Selector selector = Selector.open();
        long startTime = System.currentTimeMillis();
        try {
            server.configureBlocking(false);
            server.socket().setReuseAddress(true);
            InetSocketAddress bindAddress = args.length >= 5
                ? new InetSocketAddress(InetAddress.getByName(args[4]), port)
                : new InetSocketAddress(port);
            server.bind(bindAddress);
            server.register(selector, SelectionKey.OP_ACCEPT);
            System.out.println("Server listening on " + bindAddress);

            int acceptedClients = 0;
            int completedClients = 0;
            while (completedClients < clientCount) {
                int readyChannels = selector.select(SELECTOR_TIMEOUT_MS);
                if (readyChannels == 0) {
                    throw new RuntimeException("Server timeout waiting for selector event, completed="
                        + completedClients + "/" + clientCount);
                }

                Set<SelectionKey> selectedKeys = selector.selectedKeys();
                Iterator<SelectionKey> keyIterator = selectedKeys.iterator();
                while (keyIterator.hasNext()) {
                    SelectionKey key = keyIterator.next();
                    keyIterator.remove();
                    if (!key.isValid()) {
                        closeKey(key);
                        continue;
                    }

                    try {
                        if (key.isAcceptable()) {
                            acceptedClients += acceptSelectorClients(
                                key, server, expectedSize, acceptedClients, clientCount);
                        }
                        if (key.isReadable()) {
                            readSelectorClient(key);
                        }
                        if (key.isWritable()) {
                            completedClients += writeSelectorAck(key);
                        }
                    } catch (CancelledKeyException e) {
                        closeKey(key);
                    } catch (IOException e) {
                        System.err.println("IO error: " + e.getMessage());
                        closeKey(key);
                    }
                }
            }

            long duration = System.currentTimeMillis() - startTime;
            System.out.println("All " + clientCount + " clients completed successfully!");
            System.out.println("Total time: " + duration + " ms");
        } finally {
            selector.close();
            server.close();
        }
    }

    private static int acceptSelectorClients(SelectionKey key, ServerSocketChannel server,
                                             long expectedSize, int acceptedClients,
                                             int clientCount) throws IOException {
        int accepted = 0;
        SocketChannel channel;
        while (acceptedClients + accepted < clientCount && (channel = server.accept()) != null) {
            channel.configureBlocking(false);
            String clientId = "Client-" + (acceptedClients + accepted + 1);
            ConnectionState state = new ConnectionState(clientId, expectedSize);
            channel.register(key.selector(), SelectionKey.OP_READ, state);
            System.out.println("New connection: " + clientId + " from " + channel.getRemoteAddress());
            accepted++;
        }
        return accepted;
    }

    private static void readSelectorClient(SelectionKey key) throws IOException {
        SocketChannel channel = (SocketChannel)key.channel();
        ConnectionState state = (ConnectionState)key.attachment();
        if (state == null || state.dataComplete) {
            return;
        }

        ByteBuffer readBuffer = ByteBuffer.allocate(SELECTOR_MAX_READ_SIZE);
        int readCount = 0;
        long eventReceived = 0L;
        System.out.println(state.clientId + " selector OP_READ triggered, reading up to "
            + SELECTOR_MAX_READ_COUNT + " times");

        while (readCount < SELECTOR_MAX_READ_COUNT) {
            readBuffer.clear();
            int n = channel.read(readBuffer);
            readCount++;
            if (n < 0) {
                throw new EOFException(state.clientId + " closed early at "
                    + state.totalReceived + "/" + state.expectedSize);
            }
            if (n == 0) {
                break;
            }
            state.addData(readBuffer, 0, n);
            eventReceived += n;
            if (state.totalReceived >= state.expectedSize) {
                break;
            }
        }
        System.out.println(state.clientId + " completed " + readCount
            + " reads, received " + eventReceived + " bytes");

        if (state.totalReceived >= state.expectedSize) {
            state.markComplete();
            System.out.println(state.clientId + " data complete: "
                + state.totalReceived + " / " + state.expectedSize);
            state.prepareAck();
            key.interestOps((key.interestOps() & ~SelectionKey.OP_READ) | SelectionKey.OP_WRITE);
            System.out.println(state.clientId + " registered OP_WRITE for ACK");
        }
    }

    private static int writeSelectorAck(SelectionKey key) throws IOException {
        SocketChannel channel = (SocketChannel)key.channel();
        ConnectionState state = (ConnectionState)key.attachment();
        if (state == null || state.ackBuffer == null) {
            closeKey(key);
            return 0;
        }

        channel.write(state.ackBuffer);
        if (!state.ackBuffer.hasRemaining()) {
            System.out.println("ACK sent to " + state.clientId + ": " + state.totalReceived + " bytes");
            System.out.println("Client " + state.clientId + " disconnected.");
            closeKey(key);
            return 1;
        }
        return 0;
    }

    private static void closeKey(SelectionKey key) {
        try {
            if (key != null) {
                key.cancel();
                Channel channel = key.channel();
                if (channel != null) {
                    channel.close();
                }
            }
        } catch (IOException e) {
            System.err.println("Error closing channel: " + e.getMessage());
        }
    }

    private static void runDelayedAccept(String[] args) throws Exception {
        if (args.length < 5 || args.length > 6) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioServer delayedAccept <port> <expectedSize> <clientCount> <acceptDelayMs> [holdAfterMs]");
        }

        int port = Integer.parseInt(args[1]);
        final long expectedSize = Long.parseLong(args[2]);
        int clientCount = Integer.parseInt(args[3]);
        long acceptDelayMs = Long.parseLong(args[4]);
        long holdAfterMs = args.length == 6 ? Long.parseLong(args[5]) : 0L;

        ServerSocketChannel server = ServerSocketChannel.open();
        server.configureBlocking(true);
        server.socket().setReuseAddress(true);
        server.bind(new InetSocketAddress(port));
        System.out.println("Delayed server listening on port " + port);
        System.out.println("Delaying accept for " + acceptDelayMs + " ms");
        Thread.sleep(acceptDelayMs);

        ExecutorService executor = Executors.newFixedThreadPool(Math.min(clientCount, 8));
        ArrayList<Future<?>> tasks = new ArrayList<Future<?>>();
        try {
            for (int i = 0; i < clientCount; i++) {
                final int clientIndex = i;
                final SocketChannel channel = server.accept();
                channel.configureBlocking(true);
                tasks.add(executor.submit(new Runnable() {
                    @Override
                    public void run() {
                        try {
                            handleDelayedAcceptClient(channel, expectedSize, clientIndex);
                        } catch (Exception e) {
                            throw new RuntimeException(e);
                        }
                    }
                }));
            }

            for (Future<?> task : tasks) {
                task.get();
            }
            System.out.println("All " + clientCount + " clients completed successfully");
            if (holdAfterMs > 0L) {
                System.out.println("Holding delayedAccept server for " + holdAfterMs + " ms");
                Thread.sleep(holdAfterMs);
            }
        } finally {
            executor.shutdownNow();
            server.close();
        }
    }

    private static void handleDelayedAcceptClient(SocketChannel channel, long expectedSize,
                                                  int clientIndex) throws Exception {
        ByteBuffer readBuffer = ByteBuffer.allocate(SELECTOR_MAX_READ_SIZE);
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            long totalRead = 0L;
            while (totalRead < expectedSize) {
                readBuffer.clear();
                int n = channel.read(readBuffer);
                if (n < 0) {
                    throw new RuntimeException("Client-" + clientIndex
                        + " closed early at " + totalRead + "/" + expectedSize);
                }
                if (n > 0) {
                    readBuffer.flip();
                    byte[] chunk = new byte[n];
                    readBuffer.get(chunk);
                    digest.update(chunk, 0, n);
                    totalRead += n;
                }
            }

            byte[] hashBytes = digest.digest();
            StringBuilder hashStr = new StringBuilder(hashBytes.length * 2);
            for (byte b : hashBytes) {
                hashStr.append(String.format("%02x", b));
            }

            ByteBuffer ack = ByteBuffer.wrap(
                ("ACK " + totalRead + " bytes received, hash " + hashStr.toString()).getBytes(StandardCharsets.UTF_8));
            while (ack.hasRemaining()) {
                channel.write(ack);
            }
        } finally {
            channel.close();
        }
    }

    private static void runEarlyClose(String[] args) throws Exception {
        if (args.length < 2) {
            throw new IllegalArgumentException("Usage: NIOScenarioServer earlyClose <port>");
        }

        int port = Integer.parseInt(args[1]);
        ServerSocketChannel server = ServerSocketChannel.open();
        server.configureBlocking(true);
        server.socket().setReuseAddress(true);
        server.bind(new InetSocketAddress(port));

        SocketChannel channel = null;
        try {
            channel = server.accept();
            channel.socket().setSoLinger(true, 0);
            System.out.println("Accepted client, closing immediately");
        } finally {
            if (channel != null) {
                channel.close();
            }
            server.close();
        }
        System.out.println("Server closed connection intentionally");
    }

    private static void runDelayedRead(String[] args) throws Exception {
        if (args.length < 4) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioServer delayedRead <port> <expectedSize> <readDelayMs> [clientCount]");
        }

        int port = Integer.parseInt(args[1]);
        final long expectedSize = Long.parseLong(args[2]);
        long readDelayMs = Long.parseLong(args[3]);
        int clientCount = args.length >= 5 ? Integer.parseInt(args[4]) : 1;

        ServerSocketChannel server = ServerSocketChannel.open();
        server.configureBlocking(true);
        server.socket().setReuseAddress(true);
        server.bind(new InetSocketAddress(port));
        System.out.println("Delayed-read server listening on port " + port);

        ArrayList<SocketChannel> channels = new ArrayList<SocketChannel>();
        ExecutorService executor = Executors.newFixedThreadPool(Math.min(clientCount, 8));
        try {
            for (int i = 0; i < clientCount; i++) {
                SocketChannel channel = server.accept();
                channel.configureBlocking(true);
                channels.add(channel);
            }
            System.out.println("Delayed-read accepted " + clientCount
                + " clients, sleeping " + readDelayMs + " ms");
            Thread.sleep(readDelayMs);

            ArrayList<Future<?>> tasks = new ArrayList<Future<?>>();
            for (final SocketChannel channel : channels) {
                tasks.add(executor.submit(new Runnable() {
                    @Override
                    public void run() {
                        try {
                            handleDelayedReadClient(channel, expectedSize);
                        } catch (Exception e) {
                            throw new RuntimeException(e);
                        }
                    }
                }));
            }
            for (Future<?> task : tasks) {
                task.get();
            }
            System.out.println("DELAYED_READ_OK clients=" + clientCount);
        } finally {
            executor.shutdownNow();
            for (SocketChannel channel : channels) {
                try {
                    channel.close();
                } catch (IOException ignore) {
                }
            }
            server.close();
        }
    }

    private static void handleDelayedReadClient(SocketChannel channel, long expectedSize)
        throws Exception {
        ByteBuffer readBuffer = ByteBuffer.allocate(SELECTOR_MAX_READ_SIZE);
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        long totalRead = 0L;
        try {
            while (totalRead < expectedSize) {
                readBuffer.clear();
                int n = channel.read(readBuffer);
                if (n < 0) {
                    throw new RuntimeException("Delayed-read client closed early at "
                        + totalRead + "/" + expectedSize);
                }
                if (n > 0) {
                    readBuffer.flip();
                    byte[] chunk = new byte[n];
                    readBuffer.get(chunk);
                    digest.update(chunk, 0, n);
                    totalRead += n;
                }
            }

            byte[] hashBytes = digest.digest();
            StringBuilder hashStr = new StringBuilder(hashBytes.length * 2);
            for (byte b : hashBytes) {
                hashStr.append(String.format("%02x", b));
            }

            ByteBuffer ack = ByteBuffer.wrap(
                ("ACK " + totalRead + " bytes received, hash " + hashStr.toString())
                    .getBytes(StandardCharsets.UTF_8));
            while (ack.hasRemaining()) {
                channel.write(ack);
            }
        } finally {
            channel.close();
        }
    }

    private static final class ConnectionState {
        final String clientId;
        final long expectedSize;
        final MessageDigest digest;
        long totalReceived = 0L;
        boolean dataComplete = false;
        ByteBuffer ackBuffer;
        String hashValue;

        ConnectionState(String clientId, long expectedSize) {
            this.clientId = clientId;
            this.expectedSize = expectedSize;
            try {
                this.digest = MessageDigest.getInstance("SHA-256");
            } catch (NoSuchAlgorithmException e) {
                throw new RuntimeException("SHA-256 not available", e);
            }
        }

        void addData(ByteBuffer buffer, int pos, int length) {
            if (buffer == null || length <= 0) {
                return;
            }
            if (pos + length > buffer.capacity()) {
                throw new IllegalArgumentException(
                    "Invalid pos=" + pos + " + length=" + length + " > capacity=" + buffer.capacity());
            }

            int originalPosition = buffer.position();
            int originalLimit = buffer.limit();
            try {
                buffer.position(pos);
                buffer.limit(pos + length);
                byte[] data = new byte[length];
                buffer.get(data);
                digest.update(data, 0, length);
                totalReceived += length;
            } finally {
                buffer.position(originalPosition);
                buffer.limit(originalLimit);
            }
        }

        void markComplete() {
            dataComplete = true;
            byte[] hashBytes = digest.digest();
            StringBuilder sb = new StringBuilder(hashBytes.length * 2);
            for (byte b : hashBytes) {
                sb.append(String.format("%02x", b));
            }
            hashValue = sb.toString();
            System.out.println("[" + clientId + "] Data complete, Hash: " + hashValue);
        }

        void prepareAck() {
            ackBuffer = ByteBuffer.wrap(
                ("ACK " + totalReceived + " bytes received, hash " + hashValue)
                    .getBytes(StandardCharsets.UTF_8));
        }
    }
}
