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
 * @summary Verify exact Selector ready delivery across the native queue batch boundary
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=300 ReadyQueueBatchTest
 */

public class ReadyQueueBatchTest {
    private static final int CONNECTIONS = 33;
    private static final int DATA_SIZE = 64;

    public static void main(String[] args) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String[] vmOptions = {
            "-XX:UBSocketAttach=all",
            "-XX:UBSocketMemorySize=8M",
            "-XX:UBSocketRingCount=64",
            "-XX:UBSocketProfile=2"
        };

        SocketTestSupport.ScenarioLogs logs =
            SocketTestSupport.runUbScenarioWithVmOptions(
                configPath, controlPort, vmOptions, 1000L,
                "PARALLEL_CLIENT_OK",
                "All " + CONNECTIONS + " clients completed successfully",
                new String[] {
                    "NIOScenarioServer", "selector", String.valueOf(dataPort),
                    String.valueOf(DATA_SIZE), String.valueOf(CONNECTIONS)
                },
                new String[] {
                    "NIOScenarioClient", "parallel", "localhost",
                    String.valueOf(dataPort), String.valueOf(DATA_SIZE),
                    String.valueOf(CONNECTIONS), "ReadyQueueBatch"
                });

        String combined = logs.clientLog + "\n" + logs.serverLog;
        SocketTestSupport.assertBindSuccesses(
            combined, false, CONNECTIONS, "Every batch client must attach");
        SocketTestSupport.assertBindSuccesses(
            combined, true, CONNECTIONS, "Every batch server channel must attach");
        SocketTestSupport.assertNoFallback(
            combined, "Ready queue batch path must not fallback");
        SocketTestSupport.assertProfileCountAtLeast(
            combined, "selector_ready_queued", CONNECTIONS,
            "Ready queue must enqueue active subscriptions");
        SocketTestSupport.assertProfileCountAtLeast(
            combined, "selector_eventfd_coalesced", 1,
            "Ready queue must coalesce one Selector wakeup across connections");
        if (SocketTestSupport.profileCount(
                combined, "close_failure_timeout") != 0L) {
            throw new RuntimeException(
                "Ready queue batch close timed out\n" + combined);
        }
        SocketTestSupport.assertNoVmCrash(
            combined, "Ready queue batch path must not crash");
    }
}
