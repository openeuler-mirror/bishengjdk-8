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
 * @summary Test complex successful socket topologies and isolation
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/SocketMultiServerMain.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm SocketMultiServerTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import java.util.ArrayList;
import java.util.List;

public class SocketMultiServerTest {
    private static final int SAME_PROCESS_SERVER_COUNT = 2;
    private static final int SAME_PROCESS_CONNECTIONS_PER_SERVER = 2;
    private static final int SAME_PROCESS_TOTAL_CONNECTIONS =
        SAME_PROCESS_SERVER_COUNT * SAME_PROCESS_CONNECTIONS_PER_SERVER;

    private static final int MULTI_JVM_DATA_SIZE = 131072;
    private static final int MULTI_JVM_CLIENT_JVMS = 2;
    private static final int MULTI_JVM_CONNECTIONS_PER_CLIENT = 2;
    private static final int MULTI_JVM_TOTAL_CONNECTIONS =
        MULTI_JVM_CLIENT_JVMS * MULTI_JVM_CONNECTIONS_PER_CLIENT;

    public static void main(String[] args) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();

        testSameProcessSingleConnection(configPath);
        testSameProcessMultiServer(configPath, false);
        testSameProcessMultiServer(configPath, true);
        testMultiJvmClientsSingleServer(configPath);
        testRefCountCleanup(configPath);
    }

    private static void testSameProcessSingleConnection(String configPath) throws Exception {
        int controlPort = SocketTestSupport.findFreePort();

        Process process = null;
        try {
            ProcessBuilder pb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "SocketMultiServerMain",
                "1",
                "1",
                "false"
            );
            String ubLogPath = SocketTestSupport.getUbLogPath(pb);

            process = pb.start();
            OutputAnalyzer output = new OutputAnalyzer(process);
            output.shouldHaveExitValue(0);
            output.shouldContain("Same process data sent successfully");
            output.shouldContain("same-process selector OP_READ triggered");

            String log = SocketTestSupport.combineOutputWithUbLog(output.getOutput(), ubLogPath);
            SocketTestSupport.assertBindSuccesses(
                log, false, 1, "Same-process client attach should bind successfully");
            SocketTestSupport.assertBindSuccesses(
                log, true, 1, "Same-process server attach should bind successfully");
            SocketTestSupport.assertNoFallback(log, "Same-process attach should not fallback");
            System.out.println("=== Same-process single-connection test PASSED ===");
        } finally {
            SocketTestSupport.destroyIfAlive(process);
        }
    }

    private static void testSameProcessMultiServer(String configPath,
                                                   boolean concurrentClients) throws Exception {
        int controlPort = SocketTestSupport.findFreePort();

        Process process = null;
        try {
            ProcessBuilder pb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "SocketMultiServerMain",
                String.valueOf(SAME_PROCESS_SERVER_COUNT),
                String.valueOf(SAME_PROCESS_CONNECTIONS_PER_SERVER),
                String.valueOf(concurrentClients)
            );
            String ubLogPath = SocketTestSupport.getUbLogPath(pb);

            process = pb.start();
            OutputAnalyzer output = new OutputAnalyzer(process);
            output.shouldHaveExitValue(0);
            output.shouldContain("Multi server same process data sent successfully");
            output.shouldContain("same-process selector OP_READ triggered");

            String log = SocketTestSupport.combineOutputWithUbLog(output.getOutput(), ubLogPath);
            SocketTestSupport.assertBindSuccesses(
                log, false, SAME_PROCESS_TOTAL_CONNECTIONS,
                "Expected all same-process client attaches to succeed");
            SocketTestSupport.assertBindSuccesses(
                log, true, SAME_PROCESS_TOTAL_CONNECTIONS,
                "Expected all same-process server attaches to succeed");
            SocketTestSupport.assertNoFallback(log, "Multi-server topology should not fallback");
            System.out.println("=== Same-process multi-server "
                + (concurrentClients ? "concurrent" : "sequential") + " test PASSED ===");
        } finally {
            SocketTestSupport.destroyIfAlive(process);
        }
    }

    private static void testMultiJvmClientsSingleServer(String configPath) throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        Process server = null;
        List<Process> clients = new ArrayList<Process>();
        List<ProcessBuilder> clientBuilders = new ArrayList<ProcessBuilder>();
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(MULTI_JVM_DATA_SIZE),
                String.valueOf(MULTI_JVM_TOTAL_CONNECTIONS)
            );
            server = serverPb.start();
            Thread.sleep(1000L);

            for (int i = 0; i < MULTI_JVM_CLIENT_JVMS; i++) {
                ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                    configPath,
                    controlPort,
                    "NIOScenarioClient",
                    "parallel",
                    "localhost",
                    String.valueOf(dataPort),
                    String.valueOf(MULTI_JVM_DATA_SIZE),
                    String.valueOf(MULTI_JVM_CONNECTIONS_PER_CLIENT),
                    "ParallelJvm-" + i
                );
                clientBuilders.add(clientPb);
                clients.add(clientPb.start());
            }

            int clientBinds = 0;
            for (int i = 0; i < clients.size(); i++) {
                OutputAnalyzer clientOutput = new OutputAnalyzer(clients.get(i));
                clientOutput.shouldHaveExitValue(0);
                String clientLog = SocketTestSupport.combinedOutput(
                    clientOutput, clientBuilders.get(i));
                if (!clientLog.contains("PARALLEL_CLIENT_OK")) {
                    throw new RuntimeException("Multi-JVM client did not complete\n" + clientLog);
                }
                clientBinds += SocketTestSupport.countBindSuccesses(clientLog, false);
                SocketTestSupport.assertNoFallback(
                    clientLog, "Multi-JVM clients should not fallback");
            }

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("All " + MULTI_JVM_TOTAL_CONNECTIONS
                    + " clients completed successfully")) {
                throw new RuntimeException("Multi-JVM server did not complete\n" + serverLog);
            }

            if (clientBinds != MULTI_JVM_TOTAL_CONNECTIONS) {
                throw new RuntimeException("Multi-JVM clients: expected all client attaches"
                    + " to succeed, actual=" + clientBinds);
            }
            SocketTestSupport.assertBindSuccesses(
                serverLog, true, MULTI_JVM_TOTAL_CONNECTIONS,
                "Expected all multi-JVM server attaches to succeed");
            SocketTestSupport.assertNoFallback(
                serverLog, "Multi-JVM server should not fallback");
            System.out.println("=== Multi-JVM clients single-server test PASSED ===");
        } finally {
            for (Process client : clients) {
                SocketTestSupport.destroyIfAlive(client);
            }
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void testRefCountCleanup(String configPath) throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        Process server = null;
        try {
            ProcessBuilder serverPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(MULTI_JVM_DATA_SIZE),
                "2"
            );
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioClient",
                "refCount",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(MULTI_JVM_DATA_SIZE),
                "RefCountTopologyClient"
            );
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            String combinedLog = clientLog + "\n" + serverLog;

            SocketTestSupport.assertRefCount(
                combinedLog, 2, "Expected shared remote-memory ref count to reach 2");
            SocketTestSupport.assertRefCount(
                combinedLog, 0, "Expected shared remote-memory ref count to return to 0");
            SocketTestSupport.assertNoFallback(combinedLog, "Ref-count topology should not fallback");
            SocketTestSupport.assertNoMemoryOperationFailure(
                combinedLog, "Ref-count topology should not report mmap/munmap failures");
            System.out.println("=== Ref-count topology cleanup test PASSED ===");
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }
}
