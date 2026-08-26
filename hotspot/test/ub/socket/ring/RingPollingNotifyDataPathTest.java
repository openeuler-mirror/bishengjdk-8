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
 * @summary Verify that small UBSocket payloads use process polling and selector
 *          eventfd notification, including level readiness after partial reads.
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=240 RingPollingNotifyDataPathTest
 */

public class RingPollingNotifyDataPathTest {
    private static final int REQUEST_SIZE = 20;
    private static final int INFLIGHT = 1;
    private static final int PARTIAL_RESPONSE_SIZE = 8192;
    private static final int PARTIAL_READ_SIZE = 64;
    private static final int PARTIAL_FRAMES = 32;

    public static void main(String[] args) throws Exception {
        String partialLog = runEchoScenario(
            "PollingPartialRead", PARTIAL_RESPONSE_SIZE, PARTIAL_FRAMES,
            PARTIAL_READ_SIZE);
        assertPollingPath(partialLog, "Partial-read polling path");
        long readCalls = metric(partialLog, "readCalls");
        long minimumReadCalls = (long)PARTIAL_FRAMES
            * (PARTIAL_RESPONSE_SIZE / PARTIAL_READ_SIZE);
        if (readCalls < minimumReadCalls) {
            throw new RuntimeException("Partial reads did not require retained level readiness: "
                + "readCalls=" + readCalls + ", expected>=" + minimumReadCalls
                + "\n" + partialLog);
        }
    }

    private static String runEchoScenario(String id, int responseSize, int frames,
                                          int readChunkSize) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String[] vmOptions = { "-XX:UBSocketProfile=2" };

        SocketTestSupport.ScenarioLogs logs =
            SocketTestSupport.runUbScenarioWithVmOptions(
                configPath, controlPort, vmOptions, 1000L,
                "ECHO_CLIENT_OK", "ECHO_SERVER_OK frames=" + frames,
                new String[] {
                    "NIOScenarioServer", "echoFrames",
                    String.valueOf(dataPort), String.valueOf(REQUEST_SIZE),
                    String.valueOf(responseSize), String.valueOf(frames)
                },
                new String[] {
                    "NIOScenarioClient", "echoFrames", "localhost",
                    String.valueOf(dataPort), String.valueOf(REQUEST_SIZE),
                    String.valueOf(responseSize), String.valueOf(INFLIGHT),
                    String.valueOf(frames), id, String.valueOf(readChunkSize)
                });
        return logs.clientLog + "\n" + logs.serverLog;
    }

    private static void assertPollingPath(String combined, String message) {
        SocketTestSupport.assertBindSuccesses(combined, false, 1,
                                               message + ": client attach");
        SocketTestSupport.assertBindSuccesses(combined, true, 1,
                                               message + ": server attach");
        SocketTestSupport.assertNoFallback(combined, message + ": no fallback");
        SocketTestSupport.assertProfileCountAtLeast(
            combined, "ring_write_total", 1, message + ": Ring writes");
        SocketTestSupport.assertProfileCountAtLeast(
            combined, "poll_notify_publish", 1, message + ": publications");
        SocketTestSupport.assertProfileCountAtLeast(
            combined, "poll_notify_observed", 1, message + ": observations");
        SocketTestSupport.assertProfileCountAtLeast(
            combined, "selector_poll_ready_inject", 1, message + ": eventfd injection");
        SocketTestSupport.assertNoVmCrash(combined, message + ": no VM crash");
    }

    private static long metric(String text, String name) {
        String prefix = name + "=";
        for (String token : text.split("\\s+")) {
            if (token.startsWith(prefix)) {
                return Long.parseLong(token.substring(prefix.length()));
            }
        }
        throw new RuntimeException("Missing metric " + prefix + " in output\n" + text);
    }
}
