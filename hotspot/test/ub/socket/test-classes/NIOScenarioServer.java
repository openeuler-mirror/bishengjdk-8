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
import java.util.ArrayDeque;
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
    private static final int ECHO_FRAME_HEADER_SIZE = 8;

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
        } else if ("halfCloseAck".equals(mode)) {
            runHalfCloseAck(args);
        } else if ("selectTimeoutAfterDrain".equals(mode)) {
            runSelectTimeoutAfterDrain(args);
        } else if ("echoFrames".equals(mode)) {
            runEchoFrames(args);
        } else if ("acceptHold".equals(mode)) {
            runAcceptHold(args);
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
                channel.configureBlocking(false);
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
                if (n == 0) {
                    Thread.sleep(1L);
                    continue;
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
                if (channel.write(ack) == 0) {
                    Thread.sleep(1L);
                }
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

    private static void runHalfCloseAck(String[] args) throws Exception {
        if (args.length < 3) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioServer halfCloseAck <port> <expectedSize>");
        }

        int port = Integer.parseInt(args[1]);
        long expectedSize = Long.parseLong(args[2]);
        ServerSocketChannel server = ServerSocketChannel.open();
        Selector selector = Selector.open();
        ByteBuffer readBuffer = ByteBuffer.allocate(SELECTOR_MAX_READ_SIZE);
        boolean completed = false;
        long deadline = System.currentTimeMillis() + 30000L;
        try {
            server.configureBlocking(false);
            server.socket().setReuseAddress(true);
            server.bind(new InetSocketAddress(port));
            server.register(selector, SelectionKey.OP_ACCEPT);
            System.out.println("Half-close ACK server listening on port " + port);

            while (!completed) {
                int ready = selector.select(1000L);
                if (ready == 0) {
                    if (System.currentTimeMillis() > deadline) {
                        throw new RuntimeException("Half-close ACK server timeout");
                    }
                    continue;
                }
                deadline = System.currentTimeMillis() + 30000L;

                Iterator<SelectionKey> iterator = selector.selectedKeys().iterator();
                while (iterator.hasNext()) {
                    SelectionKey key = iterator.next();
                    iterator.remove();
                    if (!key.isValid()) {
                        continue;
                    }
                    if (key.isAcceptable()) {
                        SocketChannel channel = server.accept();
                        if (channel != null) {
                            channel.configureBlocking(false);
                            channel.register(selector, SelectionKey.OP_READ,
                                             new ConnectionState("HalfCloseClient", expectedSize));
                        }
                    } else {
                        if (key.isReadable()) {
                            readHalfCloseClient(key, readBuffer);
                        }
                        if (key.isValid() && key.isWritable()) {
                            completed = writeHalfCloseAck(key);
                        }
                    }
                }
            }
        } finally {
            selector.close();
            server.close();
        }
        System.out.println("HALF_CLOSE_SERVER_ACK_OK");
    }

    private static void readHalfCloseClient(SelectionKey key, ByteBuffer readBuffer)
        throws IOException {
        SocketChannel channel = (SocketChannel)key.channel();
        ConnectionState state = (ConnectionState)key.attachment();
        while (true) {
            readBuffer.clear();
            int n = channel.read(readBuffer);
            if (n < 0) {
                if (state.totalReceived != state.expectedSize) {
                    throw new EOFException("half-close before expected payload: "
                        + state.totalReceived + "/" + state.expectedSize);
                }
                state.markComplete();
                state.ackBuffer = ByteBuffer.wrap(
                    ("HALF_CLOSE_ACK " + state.totalReceived
                        + " bytes received, hash " + state.hashValue)
                        .getBytes(StandardCharsets.UTF_8));
                key.interestOps(SelectionKey.OP_WRITE);
                return;
            }
            if (n == 0) {
                return;
            }
            readBuffer.flip();
            state.addData(readBuffer, 0, n);
        }
    }

    private static boolean writeHalfCloseAck(SelectionKey key) throws IOException {
        SocketChannel channel = (SocketChannel)key.channel();
        ConnectionState state = (ConnectionState)key.attachment();
        while (state.ackBuffer.hasRemaining()) {
            int n = channel.write(state.ackBuffer);
            if (n < 0) {
                throw new EOFException("half-close ACK write saw closed channel");
            }
            if (n == 0) {
                return false;
            }
        }
        closeKey(key);
        return true;
    }

    private static void runSelectTimeoutAfterDrain(String[] args) throws Exception {
        if (args.length < 5) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioServer selectTimeoutAfterDrain <port> <expectedSize> <selectTimeoutMs> <minElapsedMs>");
        }

        int port = Integer.parseInt(args[1]);
        long expectedSize = Long.parseLong(args[2]);
        long selectTimeoutMs = Long.parseLong(args[3]);
        long minElapsedMs = Long.parseLong(args[4]);
        ServerSocketChannel server = ServerSocketChannel.open();
        Selector selector = Selector.open();
        ByteBuffer readBuffer = ByteBuffer.allocate(SELECTOR_MAX_READ_SIZE);
        ConnectionState state = new ConnectionState("SelectTimeoutClient", expectedSize);
        long deadline = System.currentTimeMillis() + 30000L;
        try {
            server.configureBlocking(false);
            server.socket().setReuseAddress(true);
            server.bind(new InetSocketAddress(port));
            server.register(selector, SelectionKey.OP_ACCEPT);
            System.out.println("Select-timeout server listening on port " + port);

            while (state.totalReceived < expectedSize) {
                int ready = selector.select(1000L);
                if (ready == 0) {
                    if (System.currentTimeMillis() > deadline) {
                        throw new RuntimeException("Select-timeout server read timeout at "
                            + state.totalReceived + "/" + expectedSize);
                    }
                    continue;
                }
                Iterator<SelectionKey> iterator = selector.selectedKeys().iterator();
                while (iterator.hasNext()) {
                    SelectionKey key = iterator.next();
                    iterator.remove();
                    if (!key.isValid()) {
                        continue;
                    }
                    if (key.isAcceptable()) {
                        SocketChannel channel = server.accept();
                        if (channel != null) {
                            channel.configureBlocking(false);
                            channel.register(selector, SelectionKey.OP_READ, state);
                        }
                    } else if (key.isReadable()) {
                        readSelectorTimeoutClient(key, readBuffer, state);
                        deadline = System.currentTimeMillis() + 30000L;
                    }
                }
            }

            state.markComplete();
            long startNs = System.nanoTime();
            int ready = selector.select(selectTimeoutMs);
            long elapsedMs = (System.nanoTime() - startNs) / 1000000L;
            if (ready != 0) {
                throw new RuntimeException("select(timeout) should have no new events after drain, ready="
                    + ready + " elapsedMs=" + elapsedMs);
            }
            if (elapsedMs < minElapsedMs) {
                throw new RuntimeException("select(timeout) returned before timeout after UB probe, elapsedMs="
                    + elapsedMs + " minElapsedMs=" + minElapsedMs);
            }
            System.out.println("SELECT_TIMEOUT_AFTER_DRAIN_OK elapsedMs=" + elapsedMs
                + " timeoutMs=" + selectTimeoutMs);
        } finally {
            selector.close();
            server.close();
        }
    }

    private static void readSelectorTimeoutClient(SelectionKey key, ByteBuffer readBuffer,
                                                  ConnectionState state)
        throws IOException {
        SocketChannel channel = (SocketChannel)key.channel();
        while (state.totalReceived < state.expectedSize) {
            readBuffer.clear();
            int n = channel.read(readBuffer);
            if (n < 0) {
                throw new EOFException("select-timeout client closed early at "
                    + state.totalReceived + "/" + state.expectedSize);
            }
            if (n == 0) {
                return;
            }
            readBuffer.flip();
            state.addData(readBuffer, 0, n);
        }
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
                channel.configureBlocking(false);
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

    private static void runEchoFrames(String[] args) throws Exception {
        if (args.length < 5) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioServer echoFrames <port> <requestSize> <responseSize> <frames> [bindHost]");
        }

        int port = Integer.parseInt(args[1]);
        int requestSize = Integer.parseInt(args[2]);
        int responseSize = Integer.parseInt(args[3]);
        int frames = Integer.parseInt(args[4]);
        if (requestSize < ECHO_FRAME_HEADER_SIZE || responseSize < ECHO_FRAME_HEADER_SIZE) {
            throw new IllegalArgumentException("frame sizes must be at least "
                + ECHO_FRAME_HEADER_SIZE);
        }

        ServerSocketChannel server = ServerSocketChannel.open();
        Selector selector = Selector.open();
        int completed = 0;
        int openChannels = 0;
        long start = System.currentTimeMillis();
        long deadline = start + SELECTOR_TIMEOUT_MS;
        try {
            server.configureBlocking(false);
            server.socket().setReuseAddress(true);
            InetSocketAddress bindAddress = args.length >= 6
                ? new InetSocketAddress(InetAddress.getByName(args[5]), port)
                : new InetSocketAddress(port);
            server.bind(bindAddress);
            server.register(selector, SelectionKey.OP_ACCEPT);
            System.out.println("Echo frame server listening on " + bindAddress);

            while (completed < frames || openChannels > 0) {
                int ready = selector.select(SELECTOR_TIMEOUT_MS);
                if (ready == 0) {
                    if (System.currentTimeMillis() > deadline) {
                        throw new RuntimeException("Echo frame server timeout completed="
                            + completed + "/" + frames);
                    }
                    continue;
                }
                deadline = System.currentTimeMillis() + SELECTOR_TIMEOUT_MS;

                Set<SelectionKey> selectedKeys = selector.selectedKeys();
                Iterator<SelectionKey> iterator = selectedKeys.iterator();
                while (iterator.hasNext()) {
                    SelectionKey key = iterator.next();
                    iterator.remove();
                    if (!key.isValid()) {
                        continue;
                    }
                    if (key.isAcceptable()) {
                        SocketChannel channel;
                        while ((channel = server.accept()) != null) {
                            channel.configureBlocking(false);
                            EchoFrameState state =
                                new EchoFrameState(requestSize, responseSize, frames);
                            channel.register(selector, SelectionKey.OP_READ, state);
                            openChannels++;
                            System.out.println("Echo frame client accepted: "
                                + channel.getRemoteAddress());
                        }
                    } else {
                        if (key.isReadable()) {
                            if (readEchoFrames(key)) {
                                openChannels--;
                                continue;
                            }
                        }
                        if (key.isValid() && key.isWritable()) {
                            completed += writeEchoFrames(key);
                            if (!key.isValid()) {
                                openChannels--;
                            }
                        }
                    }
                }
            }
            System.out.println("ECHO_SERVER_OK frames=" + completed
                + " elapsedMs=" + (System.currentTimeMillis() - start));
        } finally {
            selector.close();
            server.close();
        }
    }

    private static void runAcceptHold(String[] args) throws Exception {
        if (args.length < 4) {
            throw new IllegalArgumentException(
                "Usage: NIOScenarioServer acceptHold <port> <clientCount> <holdMillis>");
        }

        int port = Integer.parseInt(args[1]);
        int clientCount = Integer.parseInt(args[2]);
        long holdMillis = Long.parseLong(args[3]);
        ServerSocketChannel server = ServerSocketChannel.open();
        ArrayList<SocketChannel> channels = new ArrayList<SocketChannel>();
        try {
            server.configureBlocking(true);
            server.socket().setReuseAddress(true);
            server.bind(new InetSocketAddress(port));
            for (int i = 0; i < clientCount; i++) {
                SocketChannel channel = server.accept();
                channel.configureBlocking(false);
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
            server.close();
        }

        System.out.println("ACCEPT_HOLD_OK clients=" + clientCount);
    }

    private static boolean readEchoFrames(SelectionKey key) throws IOException {
        SocketChannel channel = (SocketChannel)key.channel();
        EchoFrameState state = (EchoFrameState)key.attachment();
        int n = channel.read(state.input);
        if (n < 0) {
            closeKey(key);
            return true;
        }
        if (n == 0) {
            return false;
        }

        state.input.flip();
        while (state.input.remaining() >= state.requestSize
                && state.framesRead < state.expectedFrames) {
            int frameStart = state.input.position();
            long sequence = state.input.getLong();
            if (sequence < 0L || sequence >= state.expectedFrames) {
                throw new RuntimeException("invalid request sequence=" + sequence);
            }
            NIOScenarioClient.verifyEchoPayload(state.input, frameStart,
                                                state.requestSize,
                                                (int)sequence, true);
            state.input.position(frameStart + state.requestSize);
            state.responses.add(
                NIOScenarioClient.makeEchoResponse((int)sequence, state.responseSize));
            state.framesRead++;
        }
        state.input.compact();
        key.interestOps(key.interestOps() | SelectionKey.OP_WRITE);
        return false;
    }

    private static int writeEchoFrames(SelectionKey key) throws IOException {
        SocketChannel channel = (SocketChannel)key.channel();
        EchoFrameState state = (EchoFrameState)key.attachment();
        int completed = 0;
        while (!state.responses.isEmpty()) {
            ByteBuffer head = state.responses.peek();
            int n = channel.write(head);
            if (n < 0) {
                closeKey(key);
                return completed;
            }
            if (n == 0) {
                break;
            }
            if (!head.hasRemaining()) {
                state.responses.remove();
                state.framesWritten++;
                completed++;
            }
        }
        if (state.responses.isEmpty()) {
            key.interestOps(key.interestOps() & ~SelectionKey.OP_WRITE);
        }
        return completed;
    }

    private static void handleDelayedReadClient(SocketChannel channel, long expectedSize)
        throws Exception {
        Selector selector = Selector.open();
        ByteBuffer readBuffer = ByteBuffer.allocate(SELECTOR_MAX_READ_SIZE);
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        long totalRead = 0L;
        ByteBuffer ack = null;
        long deadline = System.currentTimeMillis() + 120000L;
        try {
            channel.register(selector, SelectionKey.OP_READ);
            while (true) {
                int ready = selector.select(1000L);
                if (ready == 0) {
                    if (System.currentTimeMillis() > deadline) {
                        throw new RuntimeException("Delayed-read timeout at "
                            + totalRead + "/" + expectedSize);
                    }
                    continue;
                }
                Iterator<SelectionKey> iterator = selector.selectedKeys().iterator();
                while (iterator.hasNext()) {
                    SelectionKey key = iterator.next();
                    iterator.remove();
                    if (!key.isValid()) {
                        continue;
                    }
                    if (key.isReadable() && totalRead < expectedSize) {
                        while (totalRead < expectedSize) {
                            readBuffer.clear();
                            int n = channel.read(readBuffer);
                            if (n < 0) {
                                throw new RuntimeException("Delayed-read client closed early at "
                                    + totalRead + "/" + expectedSize);
                            }
                            if (n == 0) {
                                break;
                            }
                            readBuffer.flip();
                            byte[] chunk = new byte[n];
                            readBuffer.get(chunk);
                            digest.update(chunk, 0, n);
                            totalRead += n;
                            deadline = System.currentTimeMillis() + 120000L;
                        }
                        if (totalRead >= expectedSize) {
                            byte[] hashBytes = digest.digest();
                            StringBuilder hashStr = new StringBuilder(hashBytes.length * 2);
                            for (byte b : hashBytes) {
                                hashStr.append(String.format("%02x", b));
                            }
                            ack = ByteBuffer.wrap(
                                ("ACK " + totalRead + " bytes received, hash "
                                    + hashStr.toString()).getBytes(StandardCharsets.UTF_8));
                            key.interestOps(SelectionKey.OP_WRITE);
                        }
                    }
                    if (key.isWritable() && ack != null) {
                        while (ack.hasRemaining()) {
                            int n = channel.write(ack);
                            if (n < 0) {
                                throw new RuntimeException("Delayed-read ACK channel closed");
                            }
                            if (n == 0) {
                                break;
                            }
                        }
                        if (!ack.hasRemaining()) {
                            return;
                        }
                    }
                }
            }
        } finally {
            selector.close();
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

    private static final class EchoFrameState {
        final int requestSize;
        final int responseSize;
        final int expectedFrames;
        final ByteBuffer input;
        final ArrayDeque<ByteBuffer> responses = new ArrayDeque<ByteBuffer>();
        int framesRead;
        int framesWritten;

        EchoFrameState(int requestSize, int responseSize, int expectedFrames) {
            this.requestSize = requestSize;
            this.responseSize = responseSize;
            this.expectedFrames = expectedFrames;
            this.input = ByteBuffer.allocate(Math.max(requestSize * 16, 4096));
        }
    }
}
