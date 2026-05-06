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
 * @summary Test FileChannel.transferTo from a plain file to an attached UBSocket
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm TransferToTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;
import java.util.ArrayList;
import java.util.List;

public class TransferToTest {
    private static final int CLIENT_COUNT = 1;
    private static final int DATA_SIZE = 8 * 1024 * 1024;

    public static void main(String[] args) throws Exception {
        String socketConfigPath = SocketTestConfig.ensureSharedConfig();
        SocketTestSupport.ScenarioLogs logs = runTransferToScenario(socketConfigPath);

        String combinedLog = logs.clientLog + "\n" + logs.serverLog;
        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, CLIENT_COUNT, "transferTo client should bind UBSocket");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, CLIENT_COUNT, "transferTo server should bind UBSocket");
        SocketTestSupport.assertNoFallback(combinedLog, "transferTo should not fallback");
        SocketTestSupport.assertNoVmCrash(combinedLog, "transferTo should not crash VM");
        if (!logs.clientLog.contains("transferTo sent " + DATA_SIZE + " bytes")) {
            throw new RuntimeException("transferTo should log success size="
                + DATA_SIZE + "\n" + logs.clientLog);
        }
        SocketTestSupport.assertHeartbeat(
            combinedLog, DATA_SIZE + " transferTo should require heartbeat progress");
        SocketTestSupport.assertTransferInfoLogs(
            combinedLog, "transferTo should log UBSocket data transfer summary");
        if (!combinedLog.contains("transfer_from_file")) {
            throw new RuntimeException("transferTo should use UBSocket transfer_from_file path\n"
                + combinedLog);
        }
    }

    private static SocketTestSupport.ScenarioLogs runTransferToScenario(String socketConfigPath)
        throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        Process server = null;
        try {
            ProcessBuilder serverPb = createTransferToProcessBuilder(
                socketConfigPath,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(CLIENT_COUNT)
            );
            String serverUbLogPath = SocketTestSupport.getUbLogPath(serverPb);
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = createTransferToProcessBuilder(
                socketConfigPath,
                controlPort,
                "NIOScenarioClient",
                "transferTo",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "PlainTransferToClient"
            );
            String clientUbLogPath = SocketTestSupport.getUbLogPath(clientPb);
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combineOutputWithUbLog(
                clientOutput.getOutput(), clientUbLogPath);
            if (!SocketTestSupport.containsDataTransferSuccess(clientLog)) {
                throw new RuntimeException("transferTo client did not verify hash\n" + clientLog);
            }

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combineOutputWithUbLog(
                serverOutput.getOutput(), serverUbLogPath);
            if (!serverLog.contains("All " + CLIENT_COUNT + " clients completed successfully")) {
                throw new RuntimeException("transferTo server did not complete\n" + serverLog);
            }
            server = null;
            return new SocketTestSupport.ScenarioLogs(clientLog, serverLog);
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static ProcessBuilder createTransferToProcessBuilder(String socketConfigPath,
                                                                 int controlPort,
                                                                 String mainClass,
                                                                 String ... args)
        throws Exception {
        String ubLogPath = SocketTestSupport.nextUbLogPath(mainClass);
        List<String> command = new ArrayList<String>();
        command.add("-XX:+UnlockExperimentalVMOptions");
        command.add("-XX:+UseUBSocket");
        command.add("-XX:UBSocketConf=" + socketConfigPath);
        command.add("-XX:UBSocketPort=" + controlPort);
        command.add("-XX:UBLog=path=" + ubLogPath + ",socket=debug");
        command.add(mainClass);
        for (String arg : args) {
            command.add(arg);
        }

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(
            true, command.toArray(new String[command.size()]));
        pb.environment().put("UBSOCKET_LOG_PATH", ubLogPath);
        return pb;
    }
}
