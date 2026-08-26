/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Set;

public final class SelectorNotificationIsolationMain {
    private static final int FRAME_SIZE = 64;
    private static final int OPERATIONS = 128;
    private static final int IDLE_SELECTOR_COUNT = 8;
    private static final long DEADLINE_MS = 20_000L;

    private static final class ServerConnection {
        final boolean foreground;
        final ByteBuffer readBuffer = ByteBuffer.allocateDirect(FRAME_SIZE);
        ByteBuffer writeBuffer;

        ServerConnection(boolean foreground) {
            this.foreground = foreground;
        }
    }

    private SelectorNotificationIsolationMain() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            throw new IllegalArgumentException(
                "Usage: SelectorNotificationIsolationMain <server|client> <port>");
        }
        int port = Integer.parseInt(args[1]);
        if ("server".equals(args[0])) {
            runServer(port);
        } else if ("client".equals(args[0])) {
            runClient(port);
        } else {
            throw new IllegalArgumentException("Unknown mode: " + args[0]);
        }
    }

    private static void runServer(int port) throws Exception {
        try (ServerSocketChannel listener = ServerSocketChannel.open();
             Selector selector = Selector.open()) {
            listener.configureBlocking(false);
            listener.bind(new InetSocketAddress("127.0.0.1", port));
            listener.register(selector, SelectionKey.OP_ACCEPT);

            int accepted = 0;
            int completed = 0;
            long deadline = System.currentTimeMillis() + DEADLINE_MS;
            while (completed < OPERATIONS) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0L) {
                    throw new IOException("Server deadline accepted=" + accepted
                        + " completed=" + completed);
                }
                if (selector.select(remaining) == 0) {
                    continue;
                }
                Set<SelectionKey> selected = selector.selectedKeys();
                Iterator<SelectionKey> iterator = selected.iterator();
                while (iterator.hasNext()) {
                    SelectionKey key = iterator.next();
                    iterator.remove();
                    if (!key.isValid()) {
                        continue;
                    }
                    if (key.isAcceptable()) {
                        SocketChannel channel;
                        while (accepted < 2 && (channel = listener.accept()) != null) {
                            channel.configureBlocking(false);
                            ServerConnection state = new ServerConnection(accepted == 0);
                            channel.register(selector, SelectionKey.OP_READ, state);
                            accepted++;
                        }
                        if (accepted == 2) {
                            key.cancel();
                            listener.close();
                        }
                        continue;
                    }

                    SocketChannel channel = (SocketChannel)key.channel();
                    ServerConnection state = (ServerConnection)key.attachment();
                    if (key.isReadable()) {
                        if (!state.foreground) {
                            throw new IOException("Auxiliary channel unexpectedly readable");
                        }
                        int n = channel.read(state.readBuffer);
                        if (n < 0) {
                            throw new IOException("Foreground channel closed before completion");
                        }
                        if (!state.readBuffer.hasRemaining()) {
                            state.readBuffer.flip();
                            state.writeBuffer = state.readBuffer;
                            key.interestOps(SelectionKey.OP_WRITE);
                        }
                    }
                    if (key.isValid() && key.isWritable()) {
                        channel.write(state.writeBuffer);
                        if (!state.writeBuffer.hasRemaining()) {
                            completed++;
                            state.writeBuffer = null;
                            state.readBuffer.clear();
                            key.interestOps(SelectionKey.OP_READ);
                        }
                    }
                }
            }
            if (accepted != 2) {
                throw new IOException("Expected two accepted channels, actual=" + accepted);
            }
        }
        System.out.println("SELECTOR_ISOLATION_SERVER_OK operations=" + OPERATIONS);
    }

    private static void runClient(int port) throws Exception {
        List<Selector> idleSelectors = new ArrayList<Selector>();
        try (Selector foregroundSelector = Selector.open();
             Selector auxiliarySelector = Selector.open();
             SocketChannel foreground = connect(port, foregroundSelector);
             SocketChannel auxiliary = connect(port, auxiliarySelector)) {
            foregroundSelector.selectNow();
            auxiliarySelector.selectNow();

            for (int i = 0; i < IDLE_SELECTOR_COUNT; i++) {
                idleSelectors.add(Selector.open());
            }

            ByteBuffer request = ByteBuffer.allocateDirect(FRAME_SIZE);
            ByteBuffer response = ByteBuffer.allocateDirect(FRAME_SIZE);
            for (int operation = 0; operation < OPERATIONS; operation++) {
                request.clear();
                request.putInt(operation);
                while (request.hasRemaining()) {
                    request.put((byte)(operation * 31 + request.position()));
                }
                request.flip();
                writeFully(foreground, request);

                response.clear();
                readFully(foreground, foregroundSelector, response);
                response.flip();
                if (response.getInt() != operation) {
                    throw new IOException("Response sequence mismatch operation=" + operation);
                }
            }
        } finally {
            IOException failure = null;
            for (Selector selector : idleSelectors) {
                try {
                    selector.close();
                } catch (IOException ioe) {
                    if (failure == null) {
                        failure = ioe;
                    } else {
                        failure.addSuppressed(ioe);
                    }
                }
            }
            if (failure != null) {
                throw failure;
            }
        }
        System.out.println("SELECTOR_ISOLATION_CLIENT_OK operations=" + OPERATIONS
            + " idleSelectors=" + IDLE_SELECTOR_COUNT);
    }

    private static SocketChannel connect(int port, Selector selector) throws Exception {
        SocketChannel channel = SocketChannel.open();
        boolean success = false;
        try {
            channel.configureBlocking(false);
            SelectionKey key = channel.register(selector, SelectionKey.OP_CONNECT);
            if (channel.connect(new InetSocketAddress("127.0.0.1", port))) {
                key.interestOps(SelectionKey.OP_READ);
                success = true;
                return channel;
            }

            long deadline = System.currentTimeMillis() + DEADLINE_MS;
            while (!channel.finishConnect()) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0L || selector.select(remaining) == 0) {
                    throw new IOException("Connect deadline to port " + port);
                }
                selector.selectedKeys().clear();
            }
            key.interestOps(SelectionKey.OP_READ);
            selector.selectNow();
            selector.selectedKeys().clear();
            success = true;
            return channel;
        } finally {
            if (!success) {
                channel.close();
            }
        }
    }

    private static void writeFully(SocketChannel channel, ByteBuffer buffer)
        throws Exception {
        long deadline = System.currentTimeMillis() + DEADLINE_MS;
        while (buffer.hasRemaining()) {
            int n = channel.write(buffer);
            if (n < 0) {
                throw new IOException("Write reached EOF");
            }
            if (n == 0) {
                if (System.currentTimeMillis() >= deadline) {
                    throw new IOException("Write deadline remaining=" + buffer.remaining());
                }
                Thread.yield();
            }
        }
    }

    private static void readFully(SocketChannel channel, Selector selector,
                                  ByteBuffer buffer) throws Exception {
        long deadline = System.currentTimeMillis() + DEADLINE_MS;
        while (buffer.hasRemaining()) {
            int n = channel.read(buffer);
            if (n < 0) {
                throw new IOException("Read reached EOF remaining=" + buffer.remaining());
            }
            if (n > 0) {
                continue;
            }
            long remaining = deadline - System.currentTimeMillis();
            if (remaining <= 0L || selector.select(remaining) == 0) {
                throw new IOException("Read deadline remaining=" + buffer.remaining());
            }
            selector.selectedKeys().clear();
        }
    }
}
