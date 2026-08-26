/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

/*
 * @test
 * @summary Protocol 803 rejects asymmetric polling semantics before UB attach commit
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=120 ProtocolContractTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class ProtocolContractTest {
    private static final int DATA_SIZE = 4096;

    public static void main(String[] args) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        runCase(configPath, "SpinClientEnabled",
            new String[] { "-XX:UBSocketSpinNanos=0" },
            new String[] { "-XX:UBSocketSpinNanos=10000" }, false);
        runCase(configPath, "SpinServerEnabled",
            new String[] { "-XX:UBSocketSpinNanos=10000" },
            new String[] { "-XX:UBSocketSpinNanos=0" }, false);
        runCase(configPath, "MemoryMismatch",
            new String[] { "-XX:UBSocketMemorySize=1M" },
            new String[] { "-XX:UBSocketMemorySize=2M" }, false);
        runCase(configPath, "RingCountMismatch",
            new String[] { "-XX:UBSocketMemorySize=2M", "-XX:UBSocketRingCount=8" },
            new String[] { "-XX:UBSocketMemorySize=2M", "-XX:UBSocketRingCount=16" },
            false);
        runCase(configPath, "MatchedNonDefault",
            new String[] {
                "-XX:UBSocketMemorySize=2M", "-XX:UBSocketRingCount=16",
                "-XX:UBSocketSpinNanos=10000"
            },
            new String[] {
                "-XX:UBSocketMemorySize=2M", "-XX:UBSocketRingCount=16",
                "-XX:UBSocketSpinNanos=10000"
            }, true);
        runCase(configPath, "DynamicRingCount",
            new String[] {
                "-XX:UBSocketMemorySize=526336", "-XX:UBSocketRingCount=257"
            },
            new String[] {
                "-XX:UBSocketMemorySize=526336", "-XX:UBSocketRingCount=257"
            }, true);
    }

    private static void runCase(String configPath, String id,
                                String[] serverSpecificOptions,
                                String[] clientSpecificOptions,
                                boolean expectAttach) throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String[] serverOptions = options(serverSpecificOptions);
        String[] clientOptions = options(clientSpecificOptions);

        Process server = null;
        try {
            ProcessBuilder serverPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath, controlPort, serverOptions,
                    "NIOScenarioServer", "selector", String.valueOf(dataPort),
                    String.valueOf(DATA_SIZE), "1");
            server = serverPb.start();
            Thread.sleep(500L);

            ProcessBuilder clientPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath, controlPort, clientOptions,
                    "NIOScenarioClient", "nonBlockingBasic", "localhost",
                    String.valueOf(dataPort), String.valueOf(DATA_SIZE),
                    id);
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains("NONBLOCKING_IO_OK id=" + id + " api=basic")) {
                throw new RuntimeException(
                    "Asymmetric protocol client did not complete over TCP\n" + clientLog);
            }

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("All 1 clients completed successfully")) {
                throw new RuntimeException(
                    "Asymmetric protocol server did not complete over TCP\n" + serverLog);
            }
            server = null;

            String combined = clientLog + "\n" + serverLog;
            if (expectAttach) {
                SocketTestSupport.assertBindSuccesses(
                    combined, false, 1, id + " client attach");
                SocketTestSupport.assertBindSuccesses(
                    combined, true, 1, id + " server attach");
                SocketTestSupport.assertNoFallback(
                    combined, id + " matched protocol must not fallback");
            } else {
                SocketTestSupport.assertNoBindSuccess(
                    combined, false, id + " client must not commit UB attach");
                SocketTestSupport.assertNoBindSuccess(
                    combined, true, id + " server must not commit UB attach");
                SocketTestSupport.assertFallback(
                    combined, id + " must cleanly fall back to TCP");
            }
            SocketTestSupport.assertNoVmCrash(
                combined, id + " must not crash the VM");
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static String[] options(String[] specific) {
        String[] result = new String[specific.length + 1];
        result[0] = "-XX:UBSocketAttach=all";
        System.arraycopy(specific, 0, result, 1, specific.length);
        return result;
    }
}
