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
 * @summary Verify default mapping-limited attach, skip profiling, and all compatibility
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm AttachLimitTest
 */

public class AttachLimitTest {
    private static final int DATA_SIZE = 65536;

    public static void main(String[] args) throws Exception {
        testMappedDefaultLimit();
        testUnmappedDefaultLimit();
        testUnmappedAllCompatibility();
    }

    private static void testMappedDefaultLimit() throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String config = mappedConfig("UBSocketAttachLimitMapped.conf", dataPort, controlPort);
        SocketTestSupport.ScenarioLogs logs = run(config, dataPort, controlPort, "limit");
        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, 1, "Mapped limit client must attach");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, 1, "Mapped limit server must attach");
        SocketTestSupport.assertNoFallback(
            logs.clientLog + "\n" + logs.serverLog,
            "Mapped limit connection must not fall back");
        if (SocketTestSupport.profileCount(logs.clientLog + "\n" + logs.serverLog,
                                           "ub_attach_skip") != 0L) {
            throw new RuntimeException("Mapped connection must not increment ub_attach_skip\n"
                + logs.clientLog + "\n" + logs.serverLog);
        }
    }

    private static void testUnmappedDefaultLimit() throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String config = stackOnlyConfig("UBSocketAttachLimitUnmapped.conf");
        SocketTestSupport.ScenarioLogs logs = run(config, dataPort, controlPort, "limit");
        String combined = logs.clientLog + "\n" + logs.serverLog;
        SocketTestSupport.assertNoBindSuccess(
            logs.clientLog, false, "Unmapped limit client must stay on TCP");
        SocketTestSupport.assertNoBindSuccess(
            logs.serverLog, true, "Unmapped limit server must stay on TCP");
        long skipped = SocketTestSupport.profileCount(combined, "ub_attach_skip");
        if (skipped != 2L) {
            throw new RuntimeException("Expected one client and one server mapping skip, actual="
                + skipped + "\n" + combined);
        }
        long fallback = SocketTestSupport.profileCount(combined, "ub_attach_fallback");
        if (fallback != 0L) {
            throw new RuntimeException("Stable mapping misses must not count as fallback, actual="
                + fallback + "\n" + combined);
        }
        if (!combined.contains("policy=limit mapping=unmatched use=tcp")) {
            throw new RuntimeException("Missing limit skip log\n" + combined);
        }
    }

    private static void testUnmappedAllCompatibility() throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String config = stackOnlyConfig("UBSocketAttachAllUnmapped.conf");
        SocketTestSupport.ScenarioLogs logs = run(config, dataPort, controlPort, "all");
        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, 1, "All policy client must preserve unmapped attach");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, 1, "All policy server must preserve unmapped attach");
    }

    private static SocketTestSupport.ScenarioLogs run(String config, int dataPort,
                                                       int controlPort, String policy)
        throws Exception {
        return SocketTestSupport.runUbScenarioWithVmOptions(
            config,
            controlPort,
            new String[] { "-XX:UBSocketAttach=" + policy, "-XX:UBSocketProfile=1" },
            1000L,
            "Data sent successfully",
            "All 1 clients completed successfully",
            new String[] {
                "NIOScenarioServer", "selector",
                String.valueOf(dataPort), String.valueOf(DATA_SIZE), "1", "127.0.0.1"
            },
            new String[] {
                "NIOScenarioClient", "nonBlockingBasic", "127.0.0.1",
                String.valueOf(dataPort), String.valueOf(DATA_SIZE), "AttachLimitClient"
            });
    }

    private static String mappedConfig(String name, int dataPort, int controlPort)
        throws Exception {
        return SocketTestConfig.writeConfig(name,
            stackText() +
            "#address\n" +
            "self.ip=127.0.0.1 -> *\n" +
            "self.port=" + dataPort + " -> " + dataPort + "\n" +
            "remote.mapping=127.0.0.1:" + dataPort + " -> 127.0.0.1:"
                + controlPort + "\n");
    }

    private static String stackOnlyConfig(String name) throws Exception {
        return SocketTestConfig.writeConfig(name, stackText());
    }

    private static String stackText() {
        return "#stack\n" +
               "sun/nio/ch/SocketChannelImpl.connect\n" +
               "sun/nio/ch/SocketChannelImpl.finishConnect\n" +
               "sun/nio/ch/ServerSocketChannelImpl.accept\n";
    }
}
