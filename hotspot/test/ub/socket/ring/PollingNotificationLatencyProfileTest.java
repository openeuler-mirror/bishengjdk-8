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
 */

/*
 * @test
 * @summary Verify profile timing from process notification publication through
 *          polling observation and Selector ready injection.
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=240 PollingNotificationLatencyProfileTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;

public class PollingNotificationLatencyProfileTest {
    private static final int FRAME_SIZE = 64;
    private static final int INFLIGHT = 1;
    private static final int FRAMES = 128;

    private static final String[] LATENCY_EVENTS = {
        "observe_to_eventfd_complete_ns",
        "eventfd_complete_to_selector_inject_ns"
    };

    public static void main(String[] args) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String[] vmOptions = { "-XX:UBSocketProfile=2" };

        Process server = null;
        try {
            ProcessBuilder serverPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath, controlPort, vmOptions,
                    "NIOScenarioServer", "echoFrames",
                    String.valueOf(dataPort), String.valueOf(FRAME_SIZE),
                    String.valueOf(FRAME_SIZE), String.valueOf(FRAMES));
            server = serverPb.start();
            Thread.sleep(1000L);

            ProcessBuilder clientPb =
                SocketTestSupport.createUbProcessBuilderWithVmOptions(
                    configPath, controlPort, vmOptions,
                    "NIOScenarioClient", "echoFrames", "localhost",
                    String.valueOf(dataPort), String.valueOf(FRAME_SIZE),
                    String.valueOf(FRAME_SIZE), String.valueOf(INFLIGHT),
                    String.valueOf(FRAMES), "PollingLatencyProfile");

            OutputAnalyzer clientOutput = new OutputAnalyzer(clientPb.start());
            clientOutput.shouldHaveExitValue(0);
            String clientLog = SocketTestSupport.combinedOutput(clientOutput, clientPb);
            if (!clientLog.contains("ECHO_CLIENT_OK")) {
                throw new RuntimeException("Polling client did not complete\n" + clientLog);
            }

            OutputAnalyzer serverOutput = new OutputAnalyzer(server);
            serverOutput.shouldHaveExitValue(0);
            String serverLog = SocketTestSupport.combinedOutput(serverOutput, serverPb);
            if (!serverLog.contains("ECHO_SERVER_OK frames=" + FRAMES)) {
                throw new RuntimeException("Polling server did not complete\n" + serverLog);
            }

            String combined = clientLog + "\n" + serverLog;
            SocketTestSupport.assertBindSuccesses(
                combined, false, 1, "Polling client should attach");
            SocketTestSupport.assertBindSuccesses(
                combined, true, 1, "Polling server should attach");
            SocketTestSupport.assertNoFallback(
                combined, "Polling latency profile must not fallback");

            if (combined.contains("UBSocketProfile publish_to_observe_ns") ||
                combined.contains("UBSocketProfileLatency publish_to_observe_ns")) {
                throw new RuntimeException(
                    "Cross-JVM monotonic publication latency must be disabled\n" + combined);
            }

            long expectedCount = -1L;
            for (String event : LATENCY_EVENTS) {
                long count = SocketTestSupport.profileCount(combined, event);
                if (count <= 0L) {
                    throw new RuntimeException(
                        "Missing positive latency profile event " + event + "\n" + combined);
                }
                if (expectedCount < 0L) {
                    expectedCount = count;
                } else if (count != expectedCount) {
                    throw new RuntimeException(
                        "Latency profile event counts differ: expected=" + expectedCount
                        + ", event=" + event + ", actual=" + count + "\n" + combined);
                }
                assertOneMicrosecondHistogram(combined, event);
            }

            if (SocketTestSupport.profileCount(combined, "poll_profile_invalid") != 0L) {
                throw new RuntimeException(
                    "Polling latency profile recorded invalid samples\n" + combined);
            }
            assertReadProfileEvents(combined);
            SocketTestSupport.assertNoVmCrash(combined, "Latency profile must not crash");
            server = null;
        } finally {
            SocketTestSupport.destroyIfAlive(server);
        }
    }

    private static void assertOneMicrosecondHistogram(String text, String event) {
        String prefix = "UBSocketProfileLatency " + event + " ";
        for (String line : text.split("\\R")) {
            if (!line.startsWith(prefix)) {
                continue;
            }
            assertBucketValue(line, "p50_ns");
            assertBucketValue(line, "p90_ns");
            assertBucketValue(line, "p99_ns");
            return;
        }
        throw new RuntimeException("Missing latency histogram row " + prefix + "\n" + text);
    }

    private static void assertReadProfileEvents(String text) {
        assertPositiveProfileCount(text, "nio_read_total");
        assertPositiveProfileCount(text, "ring_read_hit");
    }

    private static void assertPositiveProfileCount(String text, String event) {
        if (SocketTestSupport.profileCount(text, event) <= 0L) {
            throw new RuntimeException(
                "Missing positive read profile event " + event + "\n" + text);
        }
    }

    private static void assertBucketValue(String line, String field) {
        String prefix = field + "=";
        for (String token : line.split("\\s+")) {
            if (!token.startsWith(prefix)) {
                continue;
            }
            String value = token.substring(prefix.length());
            if (value.startsWith(">")) {
                if (!">16383000".equals(value)) {
                    throw new RuntimeException("Unexpected histogram overflow boundary: "
                        + token + " in " + line);
                }
                return;
            }
            long nanos = Long.parseLong(value);
            if (nanos % 1000L != 0L) {
                throw new RuntimeException("Histogram value is not aligned to 1 us: "
                    + token + " in " + line);
            }
            return;
        }
        throw new RuntimeException("Missing " + field + " in " + line);
    }
}
