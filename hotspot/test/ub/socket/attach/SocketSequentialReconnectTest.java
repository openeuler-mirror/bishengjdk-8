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
 * @summary Test repeated reconnect and attach on the same server
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm SocketSequentialReconnectTest
 */

public class SocketSequentialReconnectTest {
    private static final int DATA_SIZE = 65536;
    private static final int ROUND_COUNT = 3;

    public static void main(String[] args) throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        SocketTestSupport.ScenarioLogs logs = SocketTestSupport.runUbScenario(
            configPath,
            controlPort,
            1000L,
            "SEQUENTIAL_RECONNECT_CLIENT_OK",
            "All " + ROUND_COUNT + " clients completed successfully",
            new String[] {
                "NIOScenarioServer", "selector",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(ROUND_COUNT)
            },
            new String[] {
                "NIOScenarioClient", "sequential", "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(ROUND_COUNT),
                "SequentialClient"
            }
        );
        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, ROUND_COUNT, "Expected all reconnect attaches to succeed");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, ROUND_COUNT, "Expected server to bind each reconnect");
        SocketTestSupport.assertNoFallback(
            logs.clientLog, "Sequential reconnect should not fallback");
        SocketTestSupport.assertNoFallback(
            logs.serverLog, "Sequential reconnect should not fallback");
        SocketTestSupport.assertNoAbnormalFdCleanup(
            logs.serverLog, "Sequential reconnect should not report abnormal fd cleanup");
        SocketTestSupport.assertNoMemoryOperationFailure(
            logs.clientLog + "\n" + logs.serverLog,
            "Sequential reconnect should not report mmap/munmap failures");
    }
}
