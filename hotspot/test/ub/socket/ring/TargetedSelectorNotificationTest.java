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
 * @summary Verify Ring notifications target only subscribed ready Selectors
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SelectorNotificationIsolationMain.java
 * @run main/othervm/timeout=240 TargetedSelectorNotificationTest
 */

public class TargetedSelectorNotificationTest {
    public static void main(String[] args) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        SocketTestSupport.ScenarioLogs logs =
            SocketTestSupport.runUbScenarioWithVmOptions(
                configPath, controlPort,
                new String[] { "-XX:UBSocketProfile=2" }, 1000L,
                "SELECTOR_ISOLATION_CLIENT_OK operations=128 idleSelectors=8",
                "SELECTOR_ISOLATION_SERVER_OK operations=128",
                new String[] {
                    "SelectorNotificationIsolationMain", "server",
                    String.valueOf(dataPort)
                },
                new String[] {
                    "SelectorNotificationIsolationMain", "client",
                    String.valueOf(dataPort)
                });
        String combined = logs.clientLog + "\n" + logs.serverLog;
        SocketTestSupport.assertBindSuccesses(
            combined, false, 2, "Both client channels must attach");
        SocketTestSupport.assertBindSuccesses(
            combined, true, 2, "Both server channels must attach");
        SocketTestSupport.assertNoFallback(
            combined, "Targeted notification scenario must not fallback");

        long invalid = SocketTestSupport.profileCount(
            combined, "poll_profile_invalid");
        // Each process may finish with one eventfd sample that races the
        // final close/readiness drain. Broadcast fanout produces roughly
        // idleSelectors * operations invalid samples and must remain far
        // above this per-process boundary allowance.
        if (invalid > 4L) {
            throw new RuntimeException(
                "Unrelated Selector eventfd was notified invalid=" + invalid
                + "\n" + combined);
        }
        SocketTestSupport.assertNoVmCrash(
            combined, "Targeted notification scenario must not crash");
    }
}
