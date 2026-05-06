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
 * @summary Test unilateral UB enablement falls back to pure TCP
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm UnilateralFallbackTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;

public class UnilateralFallbackTest {
    private static final int DATA_SIZE = 262144;
    private static final String PUBLIC_HOST = "example.com";
    private static final int PUBLIC_PORT = 80;
    private static final String PUBLIC_PATH = "/";
    private static final String PUBLIC_EXPECTED_TOKEN = "Example Domain";

    public static void main(String[] args) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        runCase(configPath, "server-ub-client-plain", true, false);
        runCase(configPath, "server-plain-client-ub", false, true);
        testPublicEndpointClientFallback(configPath);
    }

    private static void runCase(String configPath, String caseName,
                                boolean enableServerUb, boolean enableClientUb) throws Exception {
        int controlPort = SocketTestSupport.findFreePort();
        int dataPort = SocketTestSupport.findFreePort();

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createProcessBuilder(
                enableServerUb,
                configPath,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "1"
            );
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = SocketTestSupport.createProcessBuilder(
                enableClientUb,
                configPath,
                controlPort,
                "NIOScenarioClient",
                "basic",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                caseName + "-client"
            );
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            SocketTestSupport.assertDataTransferSuccess(
                clientLog, "Unilateral fallback client did not complete");

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("clients completed successfully")) {
                throw new RuntimeException("Unilateral fallback server did not complete\n" + serverLog);
            }

            if (enableServerUb) {
                SocketTestSupport.assertNoBindSuccess(
                    serverLog, true, "Server should not bind UB in unilateral plain-client case");
            }
            if (enableClientUb) {
                SocketTestSupport.assertFallback(
                    clientLog, "UB-enabled client should fallback to TCP");
            }
            System.out.println("=== " + caseName + " client output ===");
            System.out.println(clientLog);
            System.out.println("=== " + caseName + " server output ===");
            System.out.println(serverLog);
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void testPublicEndpointClientFallback(String configPath) throws Exception {
        int controlPort = SocketTestSupport.findFreePort();
        ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
            configPath,
            controlPort,
            "PublicEndpointClient",
            PUBLIC_HOST,
            String.valueOf(PUBLIC_PORT),
            PUBLIC_PATH,
            PUBLIC_EXPECTED_TOKEN
        );
        OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
        clientOutput.shouldHaveExitValue(0);
        String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
        System.out.println("=== public endpoint client output ===");
        System.out.println(clientLog);
        if (clientLog.contains("PUBLIC_ENDPOINT_SKIPPED")) {
            return;
        }
        if (!clientLog.contains("PUBLIC_ENDPOINT_OK")) {
            throw new RuntimeException("UB-enabled public endpoint client did not complete\n"
                + clientLog);
        }
        SocketTestSupport.assertFallback(
            clientLog, "UB-enabled public endpoint client should fallback to TCP");
        SocketTestSupport.assertNoBindSuccess(
            clientLog, false, "Public endpoint client should not bind UB");
    }
}

class PublicEndpointClient {
    private static final long CONNECT_TIMEOUT_MS = 5000L;
    private static final long IO_TIMEOUT_MS = 5000L;

    public static void main(String[] args) throws Exception {
        if (args.length < 4) {
            throw new IllegalArgumentException(
                "Usage: PublicEndpointClient <host> <port> <path> <expectedToken>");
        }
        String host = args[0];
        int port = Integer.parseInt(args[1]);
        String path = args[2];
        String expectedToken = args[3];

        try {
            String response = httpGet(host, port, path);
            if (!response.startsWith("HTTP/")) {
                throw new RuntimeException("Missing HTTP status line\n" + response);
            }
            if (!response.contains(expectedToken)) {
                throw new RuntimeException("Missing expected token: " + expectedToken
                    + "\n" + response);
            }
            System.out.println("PUBLIC_ENDPOINT_OK");
        } catch (IOException e) {
            System.out.println("PUBLIC_ENDPOINT_SKIPPED: " + e.getClass().getName()
                + ": " + e.getMessage());
        }
    }

    private static String httpGet(String host, int port, String path) throws IOException {
        SocketChannel channel = SocketChannel.open();
        try {
            channel.configureBlocking(false);
            channel.connect(new InetSocketAddress(host, port));
            long connectDeadline = System.currentTimeMillis() + CONNECT_TIMEOUT_MS;
            while (!channel.finishConnect()) {
                if (System.currentTimeMillis() >= connectDeadline) {
                    throw new IOException("connect timeout to " + host + ":" + port);
                }
                sleepBriefly();
            }

            String request = "GET " + path + " HTTP/1.1\r\n"
                + "Host: " + host + "\r\n"
                + "Connection: close\r\n"
                + "User-Agent: UnilateralFallbackTest\r\n"
                + "\r\n";
            ByteBuffer out = ByteBuffer.wrap(request.getBytes(StandardCharsets.US_ASCII));
            long writeDeadline = System.currentTimeMillis() + IO_TIMEOUT_MS;
            while (out.hasRemaining()) {
                int n = channel.write(out);
                if (n == 0) {
                    if (System.currentTimeMillis() >= writeDeadline) {
                        throw new IOException("write timeout to " + host + ":" + port);
                    }
                    sleepBriefly();
                }
            }

            ByteArrayOutputStream response = new ByteArrayOutputStream();
            ByteBuffer in = ByteBuffer.allocate(4096);
            long readDeadline = System.currentTimeMillis() + IO_TIMEOUT_MS;
            while (true) {
                in.clear();
                int n = channel.read(in);
                if (n < 0) {
                    break;
                }
                if (n == 0) {
                    if (System.currentTimeMillis() >= readDeadline) {
                        throw new IOException("read timeout from " + host + ":" + port);
                    }
                    sleepBriefly();
                    continue;
                }
                readDeadline = System.currentTimeMillis() + IO_TIMEOUT_MS;
                response.write(in.array(), 0, n);
            }
            return new String(response.toByteArray(), StandardCharsets.ISO_8859_1);
        } finally {
            channel.close();
        }
    }

    private static void sleepBriefly() {
        try {
            Thread.sleep(10L);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }
}
