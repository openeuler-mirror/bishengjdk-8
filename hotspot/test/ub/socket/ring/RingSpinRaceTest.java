/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 */

/*
 * @test
 * @summary Stress protocol-803 empty/drain/write races with bounded UB readiness spin
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=300 RingSpinRaceTest
 */

public class RingSpinRaceTest {
    private static final int FRAME_SIZE = 64;
    private static final int INFLIGHT = 1;
    private static final int FRAMES = 20000;
    private static final String[] VM_OPTIONS = {
        "-XX:UBSocketProfile=2",
        "-XX:UBSocketSpinNanos=10000"
    };

    public static void main(String[] args) throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        SocketTestSupport.ScenarioLogs logs =
            SocketTestSupport.runUbScenarioWithVmOptions(
                configPath, controlPort, VM_OPTIONS, 1000L,
                "ECHO_CLIENT_OK", "ECHO_SERVER_OK frames=" + FRAMES,
                new String[] {
                    "NIOScenarioServer", "echoFrames",
                    String.valueOf(dataPort), String.valueOf(FRAME_SIZE),
                    String.valueOf(FRAME_SIZE), String.valueOf(FRAMES)
                },
                new String[] {
                    "NIOScenarioClient", "echoFrames", "localhost",
                    String.valueOf(dataPort), String.valueOf(FRAME_SIZE),
                    String.valueOf(FRAME_SIZE), String.valueOf(INFLIGHT),
                    String.valueOf(FRAMES), "SpinRace"
                });
        String combined = logs.clientLog + "\n" + logs.serverLog;

        SocketTestSupport.assertBindSuccesses(
            combined, false, 1, "Race client should attach once");
        SocketTestSupport.assertBindSuccesses(
            combined, true, 1, "Race server should attach once");
        SocketTestSupport.assertNoFallback(combined, "Race stress must not fallback");
        SocketTestSupport.assertNoLegacyControlFrames(
            combined, "Race stress must not restore TCP data wakeup");
        SocketTestSupport.assertNoVmCrash(combined, "Race stress must not crash");
        assertPositive(combined, "poll_notify_request_empty");
        assertPositive(combined, "selector_spin_attempt");

        long ready = SocketTestSupport.profileCount(combined, "selector_spin_ready");
        long timeout = SocketTestSupport.profileCount(combined, "selector_spin_timeout");
        if (ready + timeout <= 0L) {
            throw new RuntimeException("No bounded-spin outcome recorded\n" + combined);
        }
    }

    private static void assertPositive(String text, String event) {
        long count = SocketTestSupport.profileCount(text, event);
        if (count <= 0L) {
            throw new RuntimeException("Missing positive profile event " + event
                + "\n" + text);
        }
    }
}
