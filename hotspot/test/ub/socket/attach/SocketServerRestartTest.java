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
 * @summary Test client reconnects and attaches again after server restart
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm SocketServerRestartTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class SocketServerRestartTest {
    private static final int DATA_SIZE = 65536;
    private static final int ROUNDS = 2;

    public static void main(String[] args) throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        ProcessBuilder clientPb = SocketTestSupport.createUbProcessBuilder(
            configPath,
            controlPort,
            "NIOScenarioClient",
            "restartAware",
            "localhost",
            String.valueOf(dataPort),
            String.valueOf(DATA_SIZE),
            String.valueOf(ROUNDS),
            "RestartAwareClient"
        );

        ProcessBuilder serverPb1 = null;
        ProcessBuilder serverPb2 = null;
        Process serverRound1 = null;
        Process serverRound2 = null;
        try {
            serverPb1 = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "1"
            );
            serverRound1 = serverPb1.start();

            Thread.sleep(500L);

            Process client = clientPb.start();
            OutputAnalyzer server1Output = new OutputAnalyzer(serverRound1);
            server1Output.shouldHaveExitValue(0);

            serverPb2 = SocketTestSupport.createUbProcessBuilder(
                configPath,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "1"
            );
            serverRound2 = serverPb2.start();

            OutputAnalyzer clientOutput = new OutputAnalyzer(client);
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains("RESTART_AWARE_CLIENT_OK")) {
                throw new RuntimeException("Restart-aware client did not complete\n" + clientLog);
            }
            SocketTestSupport.assertBindSuccesses(
                clientLog, false, ROUNDS, "Restart-aware client should attach on both rounds");
            SocketTestSupport.assertNoFallback(
                clientLog, "Restart-aware client should not fallback");

            OutputAnalyzer server2Output = new OutputAnalyzer(serverRound2);
            server2Output.shouldHaveExitValue(0);
            String serverLog =
                SocketTestSupport.combinedOutput(server1Output, serverPb1) + "\n" +
                SocketTestSupport.combinedOutput(server2Output, serverPb2);
            SocketTestSupport.assertBindSuccesses(
                serverLog, true, ROUNDS, "Restart-aware server should bind on both rounds");
            SocketTestSupport.assertNoFallback(
                serverLog, "Restart-aware server should not fallback");
        } finally {
            if (serverRound1 != null && serverRound1.isAlive()) {
                serverRound1.destroy();
                serverRound1.waitFor();
            }
            if (serverRound2 != null && serverRound2.isAlive()) {
                serverRound2.destroy();
                serverRound2.waitFor();
            }
        }
    }
}
