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
 * accompanied this work); if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * @test
 * @summary Test UBSocket selector probe does not make select(timeout) return before the user timeout
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm SelectorTimeoutProbeTest
 */

public class SelectorTimeoutProbeTest {
    private static final int DATA_SIZE = 2048;
    private static final int SELECT_TIMEOUT_MS = 200;
    private static final int MIN_ELAPSED_MS = SELECT_TIMEOUT_MS - 20;
    private static final int CLIENT_HOLD_MS = 1000;

    public static void main(String[] args) throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        SocketTestSupport.ScenarioLogs logs = SocketTestSupport.runUbScenario(
            configPath,
            controlPort,
            500L,
            "SEND_AND_HOLD_OK",
            "SELECT_TIMEOUT_AFTER_DRAIN_OK",
            new String[] {
                "NIOScenarioServer", "selectTimeoutAfterDrain",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(SELECT_TIMEOUT_MS),
                String.valueOf(MIN_ELAPSED_MS)
            },
            new String[] {
                "NIOScenarioClient", "sendAndHold",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(CLIENT_HOLD_MS),
                "SelectorTimeoutProbe-Client"
            }
        );

        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, 1, "SelectorTimeoutProbeTest: client should attach");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, 1, "SelectorTimeoutProbeTest: server should attach");
        SocketTestSupport.assertNoFallback(
            logs.clientLog + "\n" + logs.serverLog,
            "SelectorTimeoutProbeTest: should not fallback");
    }
}
