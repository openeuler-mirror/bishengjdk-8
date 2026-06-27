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
 * @summary Test UBSocket preserves TCP half-close semantics with selector read EOF and server ACK write
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm HalfCloseAckTest
 */

public class HalfCloseAckTest {
    private static final int DATA_SIZE = 4096;
    private static final String[] PROFILE_OPTIONS = new String[] {
        "-XX:UBSocketProfile=2"
    };

    public static void main(String[] args) throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        SocketTestSupport.ScenarioLogs logs = SocketTestSupport.runUbScenarioWithVmOptions(
            configPath,
            controlPort,
            PROFILE_OPTIONS,
            500L,
            "HALF_CLOSE_CLIENT_EOF_OK",
            "HALF_CLOSE_SERVER_ACK_OK",
            new String[] {
                "NIOScenarioServer", "halfCloseAck",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE)
            },
            new String[] {
                "NIOScenarioClient", "halfCloseThenReadAck",
                "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "HalfCloseAck-Client"
            }
        );

        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, 1, "HalfCloseAckTest: client should attach");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, 1, "HalfCloseAckTest: server should attach");
        String combinedLog = logs.clientLog + "\n" + logs.serverLog;
        SocketTestSupport.assertNoFallback(
            combinedLog,
            "HalfCloseAckTest: should not fallback");
        SocketTestSupport.assertNoLegacyControlFrames(
            combinedLog, "HalfCloseAckTest: should not use legacy control frames");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "ub_attach_success", 2,
            "HalfCloseAckTest: attach profile");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "ring_write_total", 1,
            "HalfCloseAckTest: ring write profile");
        SocketTestSupport.assertProfileCountAtLeast(
            combinedLog, "ring_read_total", 1,
            "HalfCloseAckTest: ring read profile");
    }
}
