/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.Iterator;
import java.util.Set;
import java.util.concurrent.atomic.AtomicReference;

public class UBSocketConfigReloadMain {
    private static final int FRAME_BYTES = 8;
    private static final long TIMEOUT_MS = 15000L;

    public static void main(String[] args) throws Exception {
        final int dataPort = Integer.parseInt(args[0]);
        final Path controlDir = Paths.get(args[1]);
        Files.createDirectories(controlDir);
        final AtomicReference<Throwable> serverFailure = new AtomicReference<Throwable>();
        final Path stop = controlDir.resolve("stop");
        Thread server = new Thread(new Runnable() {
            public void run() {
                try {
                    runServer(dataPort, controlDir, stop);
                } catch (Throwable t) {
                    serverFailure.compareAndSet(null, t);
                }
            }
        }, "UBSocketConfigReloadServer");
        server.start();
        waitFor(controlDir.resolve("ready"));

        Client persistent = Client.connect("127.0.0.1", dataPort);
        persistent.exchange("old-one");
        signal(controlDir, "persistent-ready", "READY");

        int command = 1;
        while (!Files.exists(stop)) {
            Path request = controlDir.resolve("command-" + command);
            if (!Files.exists(request)) {
                if (serverFailure.get() != null) {
                    throw new RuntimeException(serverFailure.get());
                }
                Thread.sleep(10L);
                continue;
            }
            String operation = new String(Files.readAllBytes(request),
                                          StandardCharsets.UTF_8).trim();
            if ("existing".equals(operation)) {
                persistent.exchange("old-two");
            } else if ("new".equals(operation)) {
                Client oneShot = Client.connect("127.0.0.1", dataPort);
                try {
                    oneShot.exchange("new-" + command);
                } finally {
                    oneShot.close();
                }
            } else {
                throw new IllegalArgumentException("unknown command: " + operation);
            }
            signal(controlDir, "done-" + command, "DONE");
            command++;
        }

        persistent.close();
        server.join(TIMEOUT_MS);
        if (server.isAlive()) {
            throw new RuntimeException("reload server did not stop");
        }
        if (serverFailure.get() != null) {
            throw new RuntimeException(serverFailure.get());
        }
        System.out.println("RELOAD_MAIN_OK");
    }

    private static void runServer(int port, Path controlDir, Path stop) throws Exception {
        try (ServerSocketChannel server = ServerSocketChannel.open();
             Selector selector = Selector.open()) {
            server.configureBlocking(false);
            server.bind(new InetSocketAddress("127.0.0.1", port));
            server.register(selector, SelectionKey.OP_ACCEPT);
            signal(controlDir, "ready", "READY");
            while (!Files.exists(stop)) {
                if (selector.select(100L) == 0) { continue; }
                Set<SelectionKey> selected = selector.selectedKeys();
                Iterator<SelectionKey> keys = selected.iterator();
                while (keys.hasNext()) {
                    SelectionKey key = keys.next();
                    keys.remove();
                    if (!key.isValid()) { continue; }
                    if (key.isAcceptable()) {
                        SocketChannel channel = server.accept();
                        if (channel != null) {
                            channel.configureBlocking(false);
                            channel.register(selector, SelectionKey.OP_READ,
                                             ByteBuffer.allocate(FRAME_BYTES));
                        }
                    } else if (key.isReadable()) {
                        SocketChannel channel = (SocketChannel)key.channel();
                        ByteBuffer frame = (ByteBuffer)key.attachment();
                        int n = channel.read(frame);
                        if (n < 0) {
                            key.cancel();
                            channel.close();
                        } else if (!frame.hasRemaining()) {
                            frame.flip();
                            ByteBuffer response = ByteBuffer.allocate(FRAME_BYTES);
                            response.put(frame).flip();
                            key.attach(response);
                            key.interestOps(SelectionKey.OP_WRITE);
                        }
                    } else if (key.isWritable()) {
                        SocketChannel channel = (SocketChannel)key.channel();
                        ByteBuffer response = (ByteBuffer)key.attachment();
                        channel.write(response);
                        if (!response.hasRemaining()) {
                            key.attach(ByteBuffer.allocate(FRAME_BYTES));
                            key.interestOps(SelectionKey.OP_READ);
                        }
                    }
                }
            }
            for (SelectionKey key : selector.keys()) {
                try { key.channel().close(); } catch (Exception ignore) {}
            }
        }
    }

    private static final class Client {
        private final SocketChannel channel;
        private final Selector selector;

        private Client(SocketChannel channel, Selector selector) {
            this.channel = channel;
            this.selector = selector;
        }

        static Client connect(String host, int port) throws Exception {
            SocketChannel channel = SocketChannel.open();
            Selector selector = Selector.open();
            channel.configureBlocking(false);
            channel.connect(new InetSocketAddress(host, port));
            channel.register(selector, SelectionKey.OP_CONNECT);
            long deadline = System.currentTimeMillis() + TIMEOUT_MS;
            while (!channel.isConnected()) {
                if (selector.select(100L) == 0) {
                    if (System.currentTimeMillis() >= deadline) {
                        throw new RuntimeException("connect timeout");
                    }
                    continue;
                }
                Iterator<SelectionKey> keys = selector.selectedKeys().iterator();
                while (keys.hasNext()) {
                    SelectionKey key = keys.next();
                    keys.remove();
                    if (key.isConnectable() && channel.finishConnect()) {
                        key.interestOps(0);
                    }
                }
            }
            return new Client(channel, selector);
        }

        void exchange(String text) throws Exception {
            byte[] expected = fixedFrame(text);
            ByteBuffer write = ByteBuffer.wrap(expected);
            ByteBuffer read = ByteBuffer.allocate(FRAME_BYTES);
            SelectionKey key = channel.keyFor(selector);
            key.interestOps(SelectionKey.OP_READ | SelectionKey.OP_WRITE);
            long deadline = System.currentTimeMillis() + TIMEOUT_MS;
            while (read.hasRemaining()) {
                if (selector.select(100L) == 0) {
                    if (System.currentTimeMillis() >= deadline) {
                        throw new RuntimeException("exchange timeout: " + text);
                    }
                    continue;
                }
                Iterator<SelectionKey> keys = selector.selectedKeys().iterator();
                while (keys.hasNext()) {
                    SelectionKey selected = keys.next();
                    keys.remove();
                    if (selected.isWritable() && write.hasRemaining()) {
                        channel.write(write);
                    }
                    if (selected.isReadable()) {
                        if (channel.read(read) < 0) { throw new EOFException(); }
                    }
                    selected.interestOps(SelectionKey.OP_READ |
                        (write.hasRemaining() ? SelectionKey.OP_WRITE : 0));
                }
            }
            if (!java.util.Arrays.equals(expected, read.array())) {
                throw new RuntimeException("echo mismatch: " + text);
            }
            key.interestOps(0);
        }

        void close() throws Exception {
            channel.close();
            selector.close();
        }
    }

    private static byte[] fixedFrame(String text) {
        byte[] raw = text.getBytes(StandardCharsets.US_ASCII);
        byte[] frame = new byte[FRAME_BYTES];
        System.arraycopy(raw, 0, frame, 0, Math.min(raw.length, frame.length));
        return frame;
    }

    private static void waitFor(Path path) throws Exception {
        long deadline = System.currentTimeMillis() + TIMEOUT_MS;
        while (!Files.exists(path)) {
            if (System.currentTimeMillis() >= deadline) {
                throw new RuntimeException("timeout waiting for " + path);
            }
            Thread.sleep(10L);
        }
    }

    private static void signal(Path dir, String name, String value) throws Exception {
        Files.write(dir.resolve(name), value.getBytes(StandardCharsets.UTF_8),
                    StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);
    }
}
