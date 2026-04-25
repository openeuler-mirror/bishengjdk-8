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
 * @summary Test FileChannel.transferTo over an attached UBSocket with and without UBFile
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
    private static final int PLAIN_DATA_SIZE = 8 * 1024 * 1024;
    private static final int UBFILE_DATA_SIZE = 16 * 1024 * 1024;

    public static void main(String[] args) throws Exception {
        String socketConfigPath = SocketTestConfig.ensureSharedConfig();
        String ubFileConfigPath = SocketTestConfig.writeConfig(
            "UBFileTransferTo.conf",
            "NIOScenarioClient.runTransferTo\n"
        );

        testPlainFileTransferTo(socketConfigPath);
        testUBFileTransferTo(socketConfigPath, ubFileConfigPath);
    }

    private static void testPlainFileTransferTo(String socketConfigPath) throws Exception {
        SocketTestSupport.ScenarioLogs logs = runTransferToScenario(
            socketConfigPath,
            null,
            PLAIN_DATA_SIZE,
            "PlainTransferToClient"
        );

        String combinedLog = logs.clientLog + "\n" + logs.serverLog;
        assertCommonTransferToBehavior(combinedLog, logs.clientLog, logs.serverLog,
            PLAIN_DATA_SIZE, "plain transferTo");
        if (combinedLog.contains("[file][DEBUG] open file") ||
                combinedLog.contains("[file][DEBUG] cur blk")) {
            throw new RuntimeException("plain transferTo should not enable UBFile\n" + combinedLog);
        }
    }

    private static void testUBFileTransferTo(String socketConfigPath,
                                             String ubFileConfigPath) throws Exception {
        SocketTestSupport.ScenarioLogs logs = runTransferToScenario(
            socketConfigPath,
            ubFileConfigPath,
            UBFILE_DATA_SIZE,
            "UBFileTransferToClient"
        );

        String combinedLog = logs.clientLog + "\n" + logs.serverLog;
        assertCommonTransferToBehavior(combinedLog, logs.clientLog, logs.serverLog,
            UBFILE_DATA_SIZE, "UBFile transferTo");
        if (!combinedLog.contains("[file][DEBUG] open file") ||
                !combinedLog.contains("application_ubsocket-transferTo-") ||
                !combinedLog.contains("[file][DEBUG] transfer") ||
                !combinedLog.contains("count " + UBFILE_DATA_SIZE)) {
            throw new RuntimeException("UBFile transferTo should use UBFile open/transfer path\n"
                + combinedLog);
        }
    }

    private static SocketTestSupport.ScenarioLogs runTransferToScenario(String socketConfigPath,
                                                                        String ubFileConfigPath,
                                                                        int dataSize,
                                                                        String clientId)
        throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();

        Process server = null;
        try {
            ProcessBuilder serverPb = createTransferToProcessBuilder(
                socketConfigPath,
                null,
                controlPort,
                "NIOScenarioServer",
                "selector",
                String.valueOf(dataPort),
                String.valueOf(dataSize),
                String.valueOf(CLIENT_COUNT)
            );
            String serverUbLogPath = SocketTestSupport.getUbLogPath(serverPb);
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb = createTransferToProcessBuilder(
                socketConfigPath,
                ubFileConfigPath,
                controlPort,
                "NIOScenarioClient",
                "transferTo",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(dataSize),
                clientId
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
            if (!serverLog.contains("attach agent started") ||
                    !serverLog.contains("port=" + controlPort)) {
                throw new RuntimeException("transferTo server should log attach-agent bind address\n"
                    + serverLog);
            }
            server = null;
            return new SocketTestSupport.ScenarioLogs(clientLog, serverLog);
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static ProcessBuilder createTransferToProcessBuilder(String socketConfigPath,
                                                                 String ubFileConfigPath,
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
        if (ubFileConfigPath != null) {
            command.add("-XX:+UseUBFile");
            command.add("-XX:UBFileConf=" + ubFileConfigPath);
        }
        command.add("-XX:UBLog=path=" + ubLogPath + ",socket=debug,file=debug");
        command.add(mainClass);
        for (String arg : args) {
            command.add(arg);
        }

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(
            true, command.toArray(new String[command.size()]));
        pb.environment().put("UBSOCKET_LOG_PATH", ubLogPath);
        return pb;
    }

    private static void assertCommonTransferToBehavior(String combinedLog,
                                                       String clientLog,
                                                       String serverLog,
                                                       int dataSize,
                                                       String label) {
        SocketTestSupport.assertBindSuccesses(
            clientLog, false, CLIENT_COUNT, label + " client should bind UBSocket");
        SocketTestSupport.assertBindSuccesses(
            serverLog, true, CLIENT_COUNT, label + " server should bind UBSocket");
        SocketTestSupport.assertNoFallback(combinedLog, label + " should not fallback");
        SocketTestSupport.assertNoVmCrash(combinedLog, label + " should not crash VM");
        if (!clientLog.contains("transferTo sent " + dataSize + " bytes")) {
            throw new RuntimeException(label + " should log transferTo success size="
                + dataSize + "\n" + clientLog);
        }
        SocketTestSupport.assertHeartbeat(
            combinedLog, dataSize + " transferTo should require heartbeat progress");
        SocketTestSupport.assertTransferInfoLogs(
            combinedLog, label + " should log UBSocket data transfer summary");
    }
}
