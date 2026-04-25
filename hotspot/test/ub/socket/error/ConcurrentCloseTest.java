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

/*
 * @test
 * @summary Test closing SocketChannel while UBSocket attach or data fallback is in progress
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java
 * @run main/othervm ConcurrentCloseTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import java.nio.ByteBuffer;
import java.nio.channels.SocketChannel;
import java.net.InetSocketAddress;

public class ConcurrentCloseTest {
    private static final int DATA_SIZE = 64 * 1024;
    private static final int FALLBACK_CHUNK_SIZE = 60 * 1024 * 1024;
    private static final int FALLBACK_CHUNK_COUNT = 4;
    private static final int FALLBACK_TOTAL_SIZE = FALLBACK_CHUNK_SIZE * FALLBACK_CHUNK_COUNT;

    public static void main(String[] args) throws Exception {
        testCloseDuringAttach();
        testCloseDuringFallbackWrite();
    }

    private static void testCloseDuringAttach() throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "delayedAccept",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "1",
                "3000"
            );
            server = serverPb.start();
            Thread.sleep(500L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "CloseDuringAttachClient",
                "localhost",
                String.valueOf(dataPort),
                "100"
            );
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains("CLOSE_DURING_ATTACH_OK")) {
                throw new RuntimeException("close during attach client did not complete\n"
                    + clientLog);
            }
            SocketTestSupport.assertNoVmCrash(
                clientLog, "close during attach should not crash VM");
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void testCloseDuringFallbackWrite() throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();
        String[] vmOptions = new String[] { "-Xmx768m" };

        Process server = null;
        try {
            ProcessBuilder serverPb =
                SocketTestSupport.createUbProcessBuilderWithTimeoutAndVmOptions(
                    configPath,
                    controlPort,
                    0,
                    vmOptions,
                    "NIOScenarioServer",
                    "delayedRead",
                    String.valueOf(dataPort),
                    String.valueOf(FALLBACK_TOTAL_SIZE),
                    "5000"
                );
            server = serverPb.start();
            Thread.sleep(500L);

            ProcessBuilder clientPb =
                SocketTestSupport.createUbProcessBuilderWithTimeoutAndVmOptions(
                    configPath,
                    controlPort,
                    0,
                    vmOptions,
                    "CloseDuringFallbackClient",
                    "localhost",
                    String.valueOf(dataPort),
                    String.valueOf(FALLBACK_CHUNK_SIZE),
                    String.valueOf(FALLBACK_CHUNK_COUNT),
                    "1000"
                );
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains("CLOSE_DURING_FALLBACK_OK")) {
                throw new RuntimeException("close during fallback client did not complete\n"
                    + clientLog);
            }
            SocketTestSupport.assertNoVmCrash(
                clientLog, "close during fallback should not crash VM");
            SocketTestSupport.assertNoMemoryOperationFailure(
                clientLog, "close during fallback should not report mmap/munmap failures");
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }
}

class CloseDuringAttachClient {
    public static void main(String[] args) throws Exception {
        if (args.length < 3) {
            throw new IllegalArgumentException(
                "Usage: CloseDuringAttachClient <host> <port> <closeDelayMs>");
        }
        final String host = args[0];
        final int port = Integer.parseInt(args[1]);
        long closeDelayMs = Long.parseLong(args[2]);
        final SocketChannel channel = SocketChannel.open();

        Thread worker = new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    channel.configureBlocking(true);
                    channel.connect(new InetSocketAddress(host, port));
                    ByteBuffer buffer = ByteBuffer.allocate(64 * 1024);
                    while (buffer.hasRemaining()) {
                        channel.write(buffer);
                    }
                    channel.read(ByteBuffer.allocate(16));
                } catch (Throwable expected) {
                    System.out.println("close-during-attach worker observed "
                        + expected.getClass().getName());
                }
            }
        }, "close-during-attach-worker");

        worker.start();
        Thread.sleep(closeDelayMs);
        channel.close();
        worker.join(10000L);
        if (worker.isAlive()) {
            throw new RuntimeException("close during attach worker did not finish");
        }
        System.out.println("CLOSE_DURING_ATTACH_OK");
    }
}

class CloseDuringFallbackClient {
    public static void main(String[] args) throws Exception {
        if (args.length < 5) {
            throw new IllegalArgumentException(
                "Usage: CloseDuringFallbackClient <host> <port> <chunkSize> <chunkCount> <closeDelayMs>");
        }
        final String host = args[0];
        final int port = Integer.parseInt(args[1]);
        final int chunkSize = Integer.parseInt(args[2]);
        final int chunkCount = Integer.parseInt(args[3]);
        long closeDelayMs = Long.parseLong(args[4]);
        final SocketChannel channel = SocketChannel.open();

        Thread worker = new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    channel.configureBlocking(true);
                    channel.connect(new InetSocketAddress(host, port));
                    for (int chunk = 0; chunk < chunkCount; chunk++) {
                        byte[] payload = SocketTestData.scopedUpperAlphabetData(chunk, 0, chunkSize);
                        ByteBuffer buffer = ByteBuffer.wrap(payload);
                        while (buffer.hasRemaining()) {
                            channel.write(buffer);
                        }
                    }
                    channel.read(ByteBuffer.allocate(16));
                } catch (Throwable expected) {
                    System.out.println("close-during-fallback worker observed "
                        + expected.getClass().getName());
                }
            }
        }, "close-during-fallback-worker");

        worker.start();
        Thread.sleep(closeDelayMs);
        channel.close();
        worker.join(10000L);
        if (worker.isAlive()) {
            throw new RuntimeException("close during fallback worker did not finish");
        }
        System.out.println("CLOSE_DURING_FALLBACK_OK");
    }
}
