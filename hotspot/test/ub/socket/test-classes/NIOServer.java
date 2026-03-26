/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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

import java.io.*;
import java.net.*;
import java.nio.*;
import java.nio.channels.*;
import java.util.*;
import java.util.concurrent.*;
import java.io.ByteArrayOutputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

public class NIOServer {
    
    private static final int MAX_READ_SIZE = 65536;
    private static final int MAX_READ_COUNT = 16; // same as netty
    private static final int READ_ACCEPT_OPS = SelectionKey.OP_READ | SelectionKey.OP_ACCEPT;
    
    private static class ConnectionState {
        String clientId;
        long totalReceived = 0;
        long expectedSize;
        boolean dataComplete = false;
        boolean ackSent = false;
        long startTime = System.currentTimeMillis();

        ByteArrayOutputStream dataBuffer;
        MessageDigest digest;
        String hashValue;
        
        ConnectionState(String clientId, long expectedSize) {
            this.clientId = clientId;
            this.expectedSize = expectedSize;
            this.dataBuffer = new ByteArrayOutputStream();
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
                    String.format("Invalid pos=%d + length=%d > capacity=%d", 
                                pos, length, buffer.capacity()));
            }
            
            int originalPosition = buffer.position();
            int originalLimit = buffer.limit();
            
            try {
                buffer.position(pos);
                buffer.limit(pos + length);
                
                byte[] data = new byte[length];
                buffer.get(data);
                
                dataBuffer.write(data, 0, length);
                digest.update(data, 0, length);
                
                totalReceived += length;
                
            } finally {
                buffer.position(originalPosition);
                buffer.limit(originalLimit);
            }
        }

        long getTotalReceived() {
            return totalReceived;
        }
        
        boolean isDataComplete() {
            return dataComplete;
        }
        
        void setDataComplete(boolean complete) {
            this.dataComplete = complete;
            byte[] hashBytes = digest.digest();
            this.hashValue = bytesToHex(hashBytes);
            System.out.println("[" + clientId + "] Data complete, Hash: " + hashValue);
        }

        String getHashValue() {
            return hashValue;
        }

        byte[] getReceivedData() {
            return dataBuffer.toByteArray();
        }
        
        boolean isAckSent() {
            return ackSent;
        }
        
        void setAckSent(boolean sent) {
            this.ackSent = sent;
        }

        private String bytesToHex(byte[] bytes) {
            StringBuilder sb = new StringBuilder(bytes.length * 2);
            for (byte b : bytes) {
                sb.append(String.format("%02x", b));
            }
            return sb.toString();
        }
        
        @Override
        public String toString() {
            return String.format("ConnectionState{clientId=%s, received=%d, expected=%d, complete=%s, hash=%s}",
                    clientId, totalReceived, expectedSize, dataComplete, 
                    hashValue != null ? hashValue.substring(0, 16) + "..." : "N/A");
        }
    }
    
    private static final Map<SocketChannel, ConnectionState> clientStates = new ConcurrentHashMap<>();
    private static int totalClients = 0;
    private static int completedClients = 0;
    
    public static void main(String[] args) throws Exception {
        if (args.length < 3) {
            System.err.println("Usage: NIOServer <port> <expectedSize> <clientCount>");
            System.exit(1);
        }
        
        int port = Integer.parseInt(args[0]);
        long expectedSize = Long.parseLong(args[1]);
        totalClients = Integer.parseInt(args[2]);
        
        ServerSocketChannel serverChannel = ServerSocketChannel.open();
        serverChannel.configureBlocking(false);
        serverChannel.socket().setReuseAddress(true);
        serverChannel.bind(new InetSocketAddress(port));
        
        System.out.println("Server listening on port " + port);
        
        Selector selector = Selector.open();
        serverChannel.register(selector, SelectionKey.OP_ACCEPT);
        
        long startTime = System.currentTimeMillis();
        try {
            while (completedClients < totalClients) {
                int readyChannels = selector.select(10000); // timeout 10s
                if (readyChannels == 0) {
                    System.err.println("Server timeout waiting for epoll event");
                    System.exit(1);
                }
                
                Set<SelectionKey> selectedKeys = selector.selectedKeys();
                Iterator<SelectionKey> keyIterator = selectedKeys.iterator();
                
                while (keyIterator.hasNext()) {
                    SelectionKey key = keyIterator.next();
                    keyIterator.remove();
                    processSelectedKey(key, serverChannel, expectedSize);
                }
            }
            long totalTime = System.currentTimeMillis() - startTime;
            System.out.println("All " + totalClients + " clients completed successfully!");
            System.out.println("Total time: " + totalTime + " ms");
        } finally {
            selector.close();
            serverChannel.close();
            for (SocketChannel channel : clientStates.keySet()) {
                try { channel.close(); } catch (IOException e) {}
            }
        }
    }
    
    private static void processSelectedKey(SelectionKey key, 
                                           ServerSocketChannel serverChannel,
                                           long expectedSize) {
        if (!key.isValid()) {
            closeKey(key);
            return;
        }
        
        try {
            int readyOps = key.readyOps();
            
            if ((readyOps & SelectionKey.OP_CONNECT) != 0) {
                int ops = key.interestOps();
                ops &= ~SelectionKey.OP_CONNECT;
                key.interestOps(ops);
                SocketChannel ch = (SocketChannel) key.channel();
                ch.finishConnect();
            }
            
            if ((readyOps & SelectionKey.OP_WRITE) != 0) {
                SocketChannel ch = (SocketChannel) key.channel();
                ConnectionState state = clientStates.get(ch);
                
                if (state != null && !state.isAckSent()) {
                    long totalReceived = state.getTotalReceived();
                    ByteBuffer responseBuffer = ByteBuffer.wrap(
                        ("ACK " + totalReceived + " bytes received, hash " + state.getHashValue()).getBytes("UTF-8"));
                    ch.write(responseBuffer);
                    state.setAckSent(true);
                    System.out.println("ACK sent to " + state.clientId + ": " + totalReceived + " bytes");
                }
                
                closeKey(key);
                clientStates.remove(ch);
                completedClients++;
                System.out.println("Client " + state.clientId + " disconnected. Completed: " + completedClients + "/" + totalClients);
                return;
            }
            
            if ((readyOps & READ_ACCEPT_OPS) != 0 || readyOps == 0) {
                if ((readyOps & SelectionKey.OP_ACCEPT) != 0) {
                    SocketChannel clientChannel = serverChannel.accept();
                    if (clientChannel != null) {
                        clientChannel.configureBlocking(false);
                        String clientId = "Client-" + (clientStates.size() + 1);
                        ConnectionState state = new ConnectionState(clientId, expectedSize);
                        clientStates.put(clientChannel, state);
                        
                        clientChannel.register(
                            key.selector(), 
                            SelectionKey.OP_READ,
                            state
                        );
                        System.out.println("New connection: " + clientId + " from " + clientChannel.getRemoteAddress());
                    }
                }
                
                if ((readyOps & SelectionKey.OP_READ) != 0) {
                    SocketChannel ch = (SocketChannel) key.channel();
                    ConnectionState state = clientStates.get(ch);
                    
                    if (state != null && !state.isDataComplete()) {
                        int actualReadCount = handleRead(key, state);
                        
                        if (state.getTotalReceived() >= state.expectedSize) {
                            state.setDataComplete(true);
                            System.out.println(state.clientId + " data complete: " + state.getTotalReceived() + " / " + state.expectedSize);
                            
                            int ops = key.interestOps();
                            ops |= SelectionKey.OP_WRITE;
                            key.interestOps(ops);
                            System.out.println(state.clientId + " registered OP_WRITE for ACK");
                        }
                    }
                }
            }
            
        } catch (CancelledKeyException e) {
            closeKey(key);
        } catch (IOException e) {
            System.err.println("IO error: " + e.getMessage());
            closeKey(key);
        }
    }
    
    private static int handleRead(SelectionKey key, ConnectionState state) {
        SocketChannel channel = (SocketChannel) key.channel();
        ByteBuffer buffer = ByteBuffer.allocate(MAX_READ_SIZE);
        int readCount = 0;
        long eventReceived = 0;
        
        System.out.println(state.clientId + " epoll OP_READ triggered, reading up to " + MAX_READ_COUNT + " times");
        
        while (readCount < MAX_READ_COUNT) {
            buffer.clear();
            int bytesRead;
            try {
                bytesRead = channel.read(buffer);
                readCount++;
            } catch (IOException e) {
                System.err.println(state.clientId + " read error: " + e.getMessage());
                break;
            }
            
            if (bytesRead > 0) {
                state.addData(buffer, 0, bytesRead);
                eventReceived += bytesRead;
            } else {
                System.out.println(state.clientId + " read " + bytesRead + " at #" + readCount);
                break;
            }
        }
        
        System.out.println(state.clientId + " completed " + readCount + " reads, received " + eventReceived + " bytes");
        return readCount;
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
}