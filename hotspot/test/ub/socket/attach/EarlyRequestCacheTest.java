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
 * @summary Test early attach request cache success and expiry fallback
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm EarlyRequestCacheTest
 */

public class EarlyRequestCacheTest {
    private static final int DATA_SIZE = 131072;
    private static final int CLIENT_COUNT = 1;
    private static final int CACHE_ACCEPT_DELAY_MS = 300;
    private static final int EXPIRY_ACCEPT_DELAY_MS = 3000;

    public static void main(String[] args) throws Exception {
        testEarlyRequestCacheSuccess();
        testEarlyRequestExpiryFallback();
    }

    private static void testEarlyRequestCacheSuccess() throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        SocketTestSupport.ScenarioLogs logs = SocketTestSupport.runUbScenario(
            configPath,
            controlPort,
            100L,
            "Data sent successfully",
            "All 1 clients completed successfully",
            new String[] {
                "NIOScenarioServer", "delayedAccept",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(CLIENT_COUNT),
                String.valueOf(CACHE_ACCEPT_DELAY_MS)
            },
            new String[] {
                "NIOScenarioClient", "basic", "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "EarlyCacheClient"
            }
        );
        SocketTestSupport.assertBindSuccesses(
            logs.clientLog, false, CLIENT_COUNT, "Early request cache should allow client attach");
        SocketTestSupport.assertNoFallback(
            logs.clientLog, "Early request cache should not fallback");
        SocketTestSupport.assertBindSuccesses(
            logs.serverLog, true, CLIENT_COUNT, "Early request cache should allow server attach");
        SocketTestSupport.assertNoFallback(
            logs.serverLog, "Early request cache should not fallback");
    }

    private static void testEarlyRequestExpiryFallback() throws Exception {
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        String configPath = SocketTestConfig.ensureSharedConfig();

        SocketTestSupport.ScenarioLogs logs = SocketTestSupport.runUbScenario(
            configPath,
            controlPort,
            100L,
            "Data sent successfully",
            "All 1 clients completed successfully",
            new String[] {
                "NIOScenarioServer", "delayedAccept",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                String.valueOf(CLIENT_COUNT),
                String.valueOf(EXPIRY_ACCEPT_DELAY_MS)
            },
            new String[] {
                "NIOScenarioClient", "basic", "localhost",
                String.valueOf(dataPort),
                String.valueOf(DATA_SIZE),
                "EarlyExpiryClient"
            }
        );
        SocketTestSupport.assertFallback(
            logs.clientLog, "Early request expiry should fallback");
        SocketTestSupport.assertNoBindSuccess(
            logs.clientLog, false, "Expired early request should not attach successfully");
        SocketTestSupport.assertNoBindSuccess(
            logs.serverLog, true, "Expired early request server should not bind UB");
    }
}
