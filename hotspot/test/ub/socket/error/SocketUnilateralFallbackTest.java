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
 * @run main/othervm SocketUnilateralFallbackTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class SocketUnilateralFallbackTest {
    private static final int DATA_SIZE = 262144;

    public static void main(String[] args) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        runCase(configPath, "server-ub-client-plain", true, false);
        runCase(configPath, "server-plain-client-ub", false, true);
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
}
