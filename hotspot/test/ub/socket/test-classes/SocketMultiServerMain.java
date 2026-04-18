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

import java.io.ByteArrayOutputStream;
import java.io.EOFException;
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
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public class SocketMultiServerMain {
    private static final int DEFAULT_SERVER_COUNT = 2;
    private static final int DEFAULT_CONNECTIONS_PER_SERVER = 1;
    private static final int DATA_SIZE = 524288;
    private static final int CLIENT_WRITE_CHUNK = 8192;
    private static final int SERVER_READ_CHUNK = 65536;
    private static final int SERVER_READ_BUDGET = 16;
    private static final int SELECTOR_TIMEOUT_MS = 10000;

    public static void main(String[] args) throws Exception {
        int serverCount = args.length > 0 ? Integer.parseInt(args[0]) : DEFAULT_SERVER_COUNT;
        int connectionsPerServer = args.length > 1
            ? Integer.parseInt(args[1]) : DEFAULT_CONNECTIONS_PER_SERVER;
        boolean concurrentClients = args.length > 2 && Boolean.parseBoolean(args[2]);

        ServerContext[] serverContexts = new ServerContext[serverCount];
        byte[][][] payloads = new byte[serverCount][][];
        int[] ports = new int[serverCount];

        for (int i = 0; i < serverCount; i++) {
            payloads[i] = new byte[connectionsPerServer][];
            for (int j = 0; j < connectionsPerServer; j++) {
                payloads[i][j] = SocketTestData.scopedUpperAlphabetData(i, j, DATA_SIZE);
            }

            ServerSocketChannel server = ServerSocketChannel.open();
            server.configureBlocking(false);
            server.socket().setReuseAddress(true);
            server.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), 0));
            ports[i] = ((InetSocketAddress)server.getLocalAddress()).getPort();
            serverContexts[i] = new ServerContext(i + 1, server, payloads[i]);
        }

        AtomicReference<Throwable> serverFailure = new AtomicReference<Throwable>();
        CountDownLatch serverReady = new CountDownLatch(1);
        Thread serverThread = new Thread(
            new ServerSelectorTask(serverContexts, serverCount * connectionsPerServer,
                                   serverReady, serverFailure),
            "UBSocketMultiServerSelector");
        serverThread.start();
        serverReady.await();
        if (serverFailure.get() != null) {
            throw new RuntimeException(serverFailure.get());
        }

        AtomicReference<Throwable> clientFailure = new AtomicReference<Throwable>();
        if (concurrentClients) {
            CountDownLatch startGate = new CountDownLatch(1);
            Thread[] clientThreads = new Thread[serverCount * connectionsPerServer];
            int clientIndex = 0;
            for (int i = 0; i < serverCount; i++) {
                for (int j = 0; j < connectionsPerServer; j++) {
                    Thread clientThread = new Thread(
                        new ClientTask(i + 1, ports[i], j + 1, payloads[i][j],
                                       startGate, clientFailure),
                        "UBSocketMultiServerClient-" + (i + 1) + "-" + (j + 1));
                    clientThread.start();
                    clientThreads[clientIndex++] = clientThread;
                }
            }

            startGate.countDown();
            for (Thread clientThread : clientThreads) {
                clientThread.join(10000L);
                if (clientThread.isAlive()) {
                    throw new RuntimeException(
                        "Client thread did not finish in time: " + clientThread.getName());
                }
            }
        } else {
            for (int i = 0; i < serverCount; i++) {
                for (int j = 0; j < connectionsPerServer; j++) {
                    try {
                        talkToServer(i + 1, ports[i], j + 1, payloads[i][j]);
                    } catch (Throwable t) {
                        clientFailure.compareAndSet(null, t);
                        break;
                    }
                }
            }
        }

        if (clientFailure.get() != null) {
            throw new RuntimeException(clientFailure.get());
        }

        serverThread.join(10000L);
        if (serverThread.isAlive()) {
            throw new RuntimeException("Server selector thread did not finish in time");
        }
        if (serverFailure.get() != null) {
            throw new RuntimeException(serverFailure.get());
        }

        if (serverCount == 1 && connectionsPerServer == 1 && !concurrentClients) {
            System.out.println("Same process data sent successfully");
        } else {
            System.out.println("Multi server same process data sent successfully");
        }
    }

    private static void talkToServer(int serverId, int port, int connectionId, byte[] payload)
        throws Exception {
        try (SocketChannel channel = SocketChannel.open()) {
            channel.configureBlocking(true);
            channel.connect(new InetSocketAddress("127.0.0.1", port));

            for (int offset = 0; offset < payload.length; ) {
                int chunkSize = Math.min(CLIENT_WRITE_CHUNK, payload.length - offset);
                ByteBuffer writeBuffer = ByteBuffer.allocateDirect(chunkSize);
                writeBuffer.put(payload, offset, chunkSize);
                writeBuffer.flip();
                while (writeBuffer.hasRemaining()) {
                    channel.write(writeBuffer);
                }
                offset += chunkSize;
            }

            byte[] expectedAck = ackText(serverId, connectionId).getBytes(StandardCharsets.UTF_8);
            ByteBuffer ackBuffer = ByteBuffer.allocate(expectedAck.length);
            while (ackBuffer.hasRemaining()) {
                int n = channel.read(ackBuffer);
                if (n < 0) {
                    throw new EOFException("Unexpected EOF while reading ACK for server "
                        + serverId + " connection " + connectionId);
                }
            }
            ackBuffer.flip();
            String ack = StandardCharsets.UTF_8.decode(ackBuffer).toString();
            if (!ackText(serverId, connectionId).equals(ack)) {
                throw new RuntimeException("Unexpected ACK from server " + serverId
                    + " connection " + connectionId + ": " + ack);
            }
        }
    }

    private static String ackText(int serverId, int connectionId) {
        return "ACK-" + serverId + "-" + connectionId;
    }

    private static final class ClientTask implements Runnable {
        private final int serverId;
        private final int port;
        private final int connectionId;
        private final byte[] payload;
        private final CountDownLatch startGate;
        private final AtomicReference<Throwable> failure;

        private ClientTask(int serverId, int port, int connectionId, byte[] payload,
                           CountDownLatch startGate, AtomicReference<Throwable> failure) {
            this.serverId = serverId;
            this.port = port;
            this.connectionId = connectionId;
            this.payload = payload;
            this.startGate = startGate;
            this.failure = failure;
        }

        @Override
        public void run() {
            try {
                if (startGate != null) {
                    startGate.await();
                }
                talkToServer(serverId, port, connectionId, payload);
            } catch (Throwable t) {
                failure.compareAndSet(null, t);
            }
        }
    }

    private static final class ServerContext {
        private final int serverId;
        private final ServerSocketChannel server;
        private final byte[][] expectedPayloads;
        private final boolean[] claimedConnections;

        private ServerContext(int serverId, ServerSocketChannel server, byte[][] expectedPayloads) {
            this.serverId = serverId;
            this.server = server;
            this.expectedPayloads = expectedPayloads;
            this.claimedConnections = new boolean[expectedPayloads.length];
        }
    }

    private static final class ConnectionState {
        private final ServerContext context;
        private final ByteArrayOutputStream buffer;
        private final ByteBuffer readBuffer = ByteBuffer.allocate(SERVER_READ_CHUNK);
        private ByteBuffer ackBuffer;

        private ConnectionState(ServerContext context) {
            this.context = context;
            this.buffer = new ByteArrayOutputStream(context.expectedPayloads[0].length);
        }
    }

    private static final class ServerSelectorTask implements Runnable {
        private final ServerContext[] contexts;
        private final int expectedConnections;
        private final CountDownLatch ready;
        private final AtomicReference<Throwable> failure;

        private ServerSelectorTask(ServerContext[] contexts, int expectedConnections,
                                   CountDownLatch ready, AtomicReference<Throwable> failure) {
            this.contexts = contexts;
            this.expectedConnections = expectedConnections;
            this.ready = ready;
            this.failure = failure;
        }

        @Override
        public void run() {
            Selector selector = null;
            try {
                selector = Selector.open();
                for (ServerContext context : contexts) {
                    context.server.register(selector, SelectionKey.OP_ACCEPT, context);
                }
                ready.countDown();

                int completedConnections = 0;
                while (completedConnections < expectedConnections) {
                    int readyChannels = selector.select(SELECTOR_TIMEOUT_MS);
                    if (readyChannels == 0) {
                        throw new RuntimeException("same-process selector timeout, completed="
                            + completedConnections + "/" + expectedConnections);
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
                                acceptConnections(key);
                            }
                            if (key.isReadable()) {
                                readConnection(key);
                            }
                            if (key.isWritable()) {
                                completedConnections += writeAck(key);
                            }
                        } catch (CancelledKeyException e) {
                            closeKey(key);
                        }
                    }
                }
            } catch (Throwable t) {
                failure.compareAndSet(null, t);
            } finally {
                ready.countDown();
                if (selector != null) {
                    try {
                        selector.close();
                    } catch (Exception ignore) {
                    }
                }
                for (ServerContext context : contexts) {
                    try {
                        context.server.close();
                    } catch (Exception ignore) {
                    }
                }
            }
        }

        private void acceptConnections(SelectionKey key) throws Exception {
            ServerContext context = (ServerContext) key.attachment();
            SocketChannel channel;
            while ((channel = context.server.accept()) != null) {
                channel.configureBlocking(false);
                channel.register(key.selector(), SelectionKey.OP_READ, new ConnectionState(context));
            }
        }

        private void readConnection(SelectionKey key) throws Exception {
            SocketChannel channel = (SocketChannel) key.channel();
            ConnectionState state = (ConnectionState) key.attachment();
            int readCount = 0;
            int eventReceived = 0;
            System.out.println("same-process selector OP_READ triggered for server "
                + state.context.serverId + ", reading up to " + SERVER_READ_BUDGET + " times");

            while (readCount < SERVER_READ_BUDGET) {
                state.readBuffer.clear();
                int n = channel.read(state.readBuffer);
                readCount++;
                if (n < 0) {
                    throw new EOFException("Unexpected EOF while reading payload for server "
                        + state.context.serverId);
                }
                if (n == 0) {
                    break;
                }
                state.readBuffer.flip();
                byte[] chunk = new byte[n];
                state.readBuffer.get(chunk);
                state.buffer.write(chunk, 0, chunk.length);
                eventReceived += n;
                if (state.buffer.size() >= state.context.expectedPayloads[0].length) {
                    break;
                }
            }
            System.out.println("same-process selector completed " + readCount
                + " reads, received " + eventReceived + " bytes");

            if (state.buffer.size() >= state.context.expectedPayloads[0].length) {
                int connectionId = matchConnectionId(state.context, state.buffer.toByteArray());
                if (connectionId < 0) {
                    throw new RuntimeException("Payload mismatch for server "
                        + state.context.serverId);
                }
                state.ackBuffer = ByteBuffer.wrap(
                    ackText(state.context.serverId, connectionId).getBytes(StandardCharsets.UTF_8));
                key.interestOps((key.interestOps() & ~SelectionKey.OP_READ) | SelectionKey.OP_WRITE);
            }
        }

        private int writeAck(SelectionKey key) throws Exception {
            SocketChannel channel = (SocketChannel) key.channel();
            ConnectionState state = (ConnectionState) key.attachment();
            if (state.ackBuffer == null) {
                return 0;
            }
            channel.write(state.ackBuffer);
            if (!state.ackBuffer.hasRemaining()) {
                closeKey(key);
                return 1;
            }
            return 0;
        }

        private int matchConnectionId(ServerContext context, byte[] received) {
            for (int i = 0; i < context.expectedPayloads.length; i++) {
                if (!context.claimedConnections[i] && Arrays.equals(context.expectedPayloads[i], received)) {
                    context.claimedConnections[i] = true;
                    return i + 1;
                }
            }
            return -1;
        }

        private void closeKey(SelectionKey key) {
            try {
                if (key != null) {
                    key.cancel();
                    Channel channel = key.channel();
                    if (channel != null) {
                        channel.close();
                    }
                }
            } catch (Exception ignore) {
            }
        }
    }
}
