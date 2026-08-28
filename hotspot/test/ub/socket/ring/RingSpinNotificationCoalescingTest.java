/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 */

/*
 * @test
 * @summary Verify opt-in read-ack coalescing and 256 KiB progress publication
 * @library /testlibrary
 * @compile ../SocketTestSupport.java ../SocketTestConfig.java ../test-classes/SocketTestData.java ../test-classes/NIOScenarioServer.java ../test-classes/NIOScenarioClient.java
 * @run main/othervm/timeout=300 RingSpinNotificationCoalescingTest
 */

public class RingSpinNotificationCoalescingTest {
    private static final int REQUEST_SIZE = 64;
    private static final int RESPONSE_SIZE = 2048;
    private static final int INFLIGHT = 512;
    private static final int FRAMES = 2048;

    public static void main(String[] args) throws Exception {
        String disabled = runScenario("SpinDisabled", new String[] {
            "-XX:UBSocketProfile=2",
            "-XX:UBSocketSpinNanos=0"
        });
        assertCommon(disabled, "Spin disabled");
        long disabledWrites = SocketTestSupport.profileCount(
            disabled, "ring_write_total");
        long disabledPublishes = SocketTestSupport.profileCount(
            disabled, "poll_notify_publish");
        if (disabledWrites <= 0L || disabledPublishes < disabledWrites) {
            throw new RuntimeException(
                "Spin=0 must publish at least once per successful write: writes="
                + disabledWrites + ", publishes=" + disabledPublishes
                + "\n" + disabled);
        }

        String enabled = runScenario("SpinEnabled", new String[] {
            "-Xcheck:jni",
            "-XX:UBSocketProfile=2",
            "-XX:UBSocketSpinNanos=10000"
        });
        assertCommon(enabled, "Spin enabled");
        assertPositive(enabled, "poll_notify_request_empty");
        assertPositive(enabled, "poll_notify_request_threshold");
        assertPositive(enabled, "poll_notify_skip_nonempty");
        assertPositive(enabled, "selector_spin_attempt");
        assertPositive(enabled, "selector_ready_inject");

        long enabledWrites = SocketTestSupport.profileCount(
            enabled, "ring_write_total");
        long enabledPublishes = SocketTestSupport.profileCount(
            enabled, "poll_notify_publish");
        if (enabledWrites <= 0L || enabledPublishes <= 0L ||
            enabledPublishes >= enabledWrites) {
            throw new RuntimeException(
                "Enabled mode must publish less often than it writes: writes="
                + enabledWrites + ", publishes=" + enabledPublishes
                + "\n" + enabled);
        }
    }

    private static String runScenario(String id, String[] vmOptions)
        throws Exception {
        String configPath = SocketTestConfig.ensureSharedConfig();
        int dataPort = SocketTestSupport.findFreePort();
        int controlPort = SocketTestSupport.findFreePort();
        SocketTestSupport.ScenarioLogs logs =
            SocketTestSupport.runUbScenarioWithVmOptions(
                configPath, controlPort, vmOptions, 1000L,
                "ECHO_CLIENT_OK", "ECHO_SERVER_OK frames=" + FRAMES,
                new String[] {
                    "NIOScenarioServer", "echoFrames",
                    String.valueOf(dataPort), String.valueOf(REQUEST_SIZE),
                    String.valueOf(RESPONSE_SIZE), String.valueOf(FRAMES)
                },
                new String[] {
                    "NIOScenarioClient", "echoFrames", "localhost",
                    String.valueOf(dataPort), String.valueOf(REQUEST_SIZE),
                    String.valueOf(RESPONSE_SIZE), String.valueOf(INFLIGHT),
                    String.valueOf(FRAMES), id
                });
        return logs.clientLog + "\n" + logs.serverLog;
    }

    private static void assertCommon(String combined, String message) {
        SocketTestSupport.assertBindSuccesses(
            combined, false, 1, message + ": client attach");
        SocketTestSupport.assertBindSuccesses(
            combined, true, 1, message + ": server attach");
        SocketTestSupport.assertNoFallback(combined, message + ": no fallback");
        SocketTestSupport.assertNoLegacyControlFrames(
            combined, message + ": no TCP data wakeup");
        SocketTestSupport.assertNoVmCrash(combined, message + ": no crash");
    }

    private static void assertPositive(String text, String event) {
        long count = SocketTestSupport.profileCount(text, event);
        if (count <= 0L) {
            throw new RuntimeException("Missing positive profile event " + event
                + "\n" + text);
        }
    }
}
