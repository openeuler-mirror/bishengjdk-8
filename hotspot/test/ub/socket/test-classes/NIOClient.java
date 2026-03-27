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

public class NIOClient {
    
    private static final int MAX_WRITE_SIZE = 4 * 1024 * 1024;
    
    public static void main(String[] args) throws Exception {
        if (args.length < 4) {
            System.err.println("Usage: NIOClient <host> <port> <dataSize> <clientId>");
            System.exit(1);
        }
        
        String host = args[0];
        int port = Integer.parseInt(args[1]);
        int dataSize = Integer.parseInt(args[2]);
        String clientId = args[3];
        
        System.out.println("[" + clientId + "] Connecting to " + host + ":" + port);
        System.out.println("[" + clientId + "] Preparing " + dataSize + " bytes data...");
        
        byte[] testData = generateTestData(dataSize);
        
        SocketChannel channel = SocketChannel.open();
        channel.configureBlocking(true);
        
        long startTime = System.currentTimeMillis();
        
        try {
            channel.connect(new InetSocketAddress(host, port));
            System.out.println("[" + clientId + "] Connected to server");
            
            ByteBuffer buffer = ByteBuffer.wrap(testData);
            long totalWritten = 0;
            int writeCount = 0;
            
            while (buffer.hasRemaining()) {
                int remaining = buffer.remaining();
                int toWrite = Math.min(remaining, MAX_WRITE_SIZE);
                
                ByteBuffer writeBuffer = ByteBuffer.allocate(toWrite);
                byte[] chunk = new byte[toWrite];
                buffer.get(chunk);
                writeBuffer.put(chunk);
                writeBuffer.flip();
                
                int written = channel.write(writeBuffer);
                if (written > 0) {
                    totalWritten += written;
                    writeCount++;
                }
            }
            
            long sendTime = System.currentTimeMillis() - startTime;
            System.out.println("[" + clientId + "] Sent " + totalWritten + " bytes in " + sendTime + " ms");
            
            ByteBuffer responseBuffer = ByteBuffer.allocate(1024);
            int bytesRead = channel.read(responseBuffer);
            
            if (bytesRead > 0) {
                responseBuffer.flip();
                byte[] responseData = new byte[bytesRead];
                responseBuffer.get(responseData);
                String response = new String(responseData, "UTF-8");
                System.out.println("[" + clientId + "] Server response: " + response);
                System.out.println("[" + clientId + "] Data sent successfully");
            }
            
        } finally {
            channel.close();
            System.out.println("[" + clientId + "] Connection closed");
        }
    }
    
    private static byte[] generateTestData(int size) {
        byte[] data = new byte[size];
        for (int i = 0; i < size; i++) {
            data[i] = (byte) ('A' + (i % 26));
        }
        return data;
    }
}