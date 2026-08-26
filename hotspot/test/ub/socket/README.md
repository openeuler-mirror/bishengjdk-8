# UBSocket jtreg Test Plan

This directory contains UBSocket jtreg coverage. The current UBSocket data path
is receiver-owned ring based: attach exchanges each peer's local ring slot,
payload bytes are written into the receiver's ring, and one JVM polling thread
observes a sender-written process generation and compact Ring write-offset
table. Native per-selector ready queues and eventfds convert that shared-memory
progress into selector readiness. TCP remains the attach, close, and failure
control path; it does not carry
payload-readiness frames. Tests should not assert the old descriptor,
unread-list, heartbeat, or DATA_FALLBACK behavior.

## Directory Layout

- `ring/`: current ring data-path and polling/eventfd notification coverage.
- `basic/`: successful attach, core Java SocketChannel API, topology, option,
  and file-transfer coverage. The obsolete descriptor/heartbeat aggregate test
  has been removed; current coverage lives in the ring and API-specific tests.
- `attach/`: attach timing, timeout, restart, and early-request behavior.
- `error/`: attach fallback and peer-failure behavior. Runtime DATA_FALLBACK
  tests have been removed because that path no longer exists in the ring data
  path.
- `test-classes/`: helper programs launched by jtreg entry tests.

## Current Ring Semantics

- Each JVM owns a shared memory region controlled by `-XX:UBSocketMemorySize`
  and split into `-XX:UBSocketRingCount` inbound ring slots. The default Ring
  budget is 268431360 bytes (`256 MiB - 4 KiB`), so its 64-byte JVM process
  header uses 268431424 bytes. The actual UB allocation and mmap length are
  rounded up to a 4 MiB boundary, producing a 256 MiB default allocation with
  4032 bytes of tail padding. The flag describes Ring budget, not aligned
  allocation size.
- `-XX:UBSocketAttach=limit` is the default attach policy. A client data
  endpoint must match `remote.mapping`, while an accepted server endpoint must
  match `self.ip` plus a `self.port` local target. Unmatched connections stay
  on TCP before stack walking, Ring allocation, or control attach and increment
  `ub_attach_skip`. `-XX:UBSocketAttach=all` retains the original behavior:
  missing remote mappings use the peer data IP plus `UBSocketPort`.
- The per-Ring budget is `UBSocketMemorySize / UBSocketRingCount` and the total
  memory must divide evenly by the ring count. Each Ring contributes one 64-byte
  write state and one 64-byte peer-ack state to contiguous metadata tables; the
  remainder is payload capacity. The maximum Ring count is derived from the
  configured memory and minimum non-empty payload instead of a fixed limit.
  Moving the existing 128 bytes of state does not change the used mapping size.
- Each attached fd consumes one local inbound slot. If no slot is available,
  attach fails and the connection falls back to TCP.
- A sender copies payload bytes into the receiver-owned ring, publishes the
  compact write-offset table, and increments the process notification
  generation. The receiver only reads its UB mapping. Receive acknowledgements
  are written into the peer-owned compact peer-state table, preserving the same
  ownership rule. `-XX:UBSocketSpinNanos=0` requests one process publication per
  successful Ring write.
- A positive `UBSocketSpinNanos` value is an experimental specialization for
  one or a few connections and must be identical on both protocol-803 peers.
  It suppresses publication while read-ack shows the Ring is non-empty, forces
  progress at `min(256 KiB, Ring capacity)`, and spins each retained hot fd for
  the configured budget before clearing `ubReady`. Poller/eventfd remains the
  fallback after timeout; no TCP data WAKEUP is restored. Enabling this option
  for many connections has no aggregate spin/fairness guarantee.
- One busy polling thread per JVM first observes the process generation. On a
  change it scans the contiguous write-offset table, resolves only changed Ring
  slots, and sends them to native per-Selector ready queues. Poller enqueues exact
  connection generations and coalesces one outstanding eventfd wakeup; Java
  drains that queue in batches instead of scanning every UB read interest.
  `-XX:UBSocketPollerCpu=N` pins that thread to logical CPU `N`; the default `-1`
  leaves affinity under OS control.
- Protocol 803 attach exchanges and validates mapping size, Ring geometry,
  capabilities, SpinNanos and connection generations before mmap publication
  and commit. Any mismatch stays on TCP. A committed connection never resumes
  application I/O on its TCP control stream.
- `SHUTDOWN_OUTPUT` and `CLOSE` carry the final Ring write offset. EOF is ready
  only after Ring write and read offsets both reach that value. A timeout is a
  terminal UB I/O failure, not a normal EOF decision. Closing peers also publish
  a quiesced generation through the reverse Ring header. A slot that has not
  observed its matching generation is retired and cannot be reused; allocation
  reclaims it after the acknowledgement becomes visible.

## Notification Profiling

`publish_to_observe_ns` keeps its historical profile event ID but is not
recorded under protocol 803: Sender and Receiver `os::javaTimeNanos()` values
come from unrelated JVM/host monotonic clock domains. The reliable local stages
remain `observe_to_eventfd_complete_ns` and
`eventfd_complete_to_selector_inject_ns`. Their histograms use 1 us buckets,
16384 buckets, and cover `[0, 16.384 ms)`; larger samples increment overflow.
Profile counters and histogram updates are atomic. Formal performance runs must
still use `-XX:UBSocketProfile=0`.

## Known TODO

- `MultiServerTopologyTest` has exposed a rare fd reuse window in same-process
  multi-server selector scenarios. A closed channel may leave cancelled-key /
  UB selector state (`registered`, `ubReady`, or `ubClosePending`) pending until
  the selector drains its deregistration queue, while the OS can already reuse
  the same integer fd for a new accepted channel. The new server-side channel
  currently attempts UB attach during `ServerSocketChannelImpl.accept()`, before
  the application registers it with the selector, so a fix placed only in
  `EPollArrayWrapper.add(fd)` is too late for that connection. A future fix
  should keep native `has_registered(fd)` idempotent, add an identity guard so
  old `SelectionKey` deregistration cannot remove a newer fd mapping, and
  consider a one-shot "skip next UB attach for this fd" marker when old selector
  UB state has not fully drained. The fallback must be clean TCP for that
  connection, with no mixed TCP/UB state.

## Shared Helpers

- `SocketTestSupport.java` creates child JVMs, allocates free ports, configures
  `UBLog=path=...,socket=debug`, merges child output with UB logs, and provides
  common assertions.
- `SocketTestSupport.profileCount()`, `profileBytes()`, `profileMaxBytes()`,
  and the profile assertion helpers parse `UBSocketProfile` output across
  client and server logs.
- `SocketTestConfig.java` writes the socket allow-list used by
  `-XX:UBSocketConf`. Existing tests that intentionally exercise unmapped
  attach explicitly select `-XX:UBSocketAttach=all`; attach-policy tests use
  the default `limit`.
- `NIOScenarioServer` and `NIOScenarioClient` provide selector, non-blocking,
  and blocking scenarios. The `echoFrames` mode is the primary current ring
  regression: fixed-size request/response frames with sequence validation and
  configurable inflight.

## Runtime Configuration Reload

The complete stack/address configuration for future connections can be
replaced with:

```text
jcmd <pid> UBSocket.conf <filepath>
```

The VM parses a new configuration instance and publishes it only after every
entry is valid. A successful command prints the effective current `#stack` and
`#address` entries in normalized, reusable form. If the file cannot be read or
contains an invalid non-comment line, the command prints `ERROR` and `current
configuration unchanged` and keeps the previous instance.

Existing attached and TCP connections are not re-evaluated. New registrations
that arrive while reload is in progress immediately use normal TCP fallback
and increment `ub_attach_fallback`; they do not wait for file parsing. Once the
reload completes, stable `limit` mapping misses use `ub_attach_skip` instead.

## Ring Tests

### `ring/RingPollingNotifyDataPathTest.java`

This is the focused partial-read polling regression. It uses 20-byte requests
and 8 KiB responses with one outstanding request, verifies ring integrity,
polling/eventfd profiles, and retained level readiness across deliberately
partial reads.

### `ring/RingPollingDataPathTest.java`

This verifies Poller/eventfd readiness with burst, high-inflight echo traffic.

### `ring/RingSpinNotificationCoalescingTest.java`

This compares disabled per-write publication with enabled read-ack coalescing,
including non-empty skips, empty-ring requests, the fixed 256 KiB progress
threshold, retained readiness, and checked-JNI execution.

### `ring/RingSpinRaceTest.java`

This performs 20,000 single-inflight empty/drain/write transitions under the
10 us bounded spin and fails on any lost wakeup or process timeout.

### `ring/RingBoundaryPolicyTest.java`

This covers current ring edge policies that should not be asserted with legacy
DATA_FALLBACK behavior.

- `testRingPressurePartialWrite`: writes a payload larger than one inbound ring
  slot through a non-blocking channel while the receiver delays reads. It uses
  a small configured ring to verify successful delivery, `ring_write_partial`,
  and the `ring_write_max_used_bytes` / `ring_read_max_used_bytes` profile
  high-water marks instead of runtime DATA_FALLBACK.

### `ring/RingSlotLimitTest.java`

This covers configurable ring slot count behavior.

- `testRingSlotLimitFallback`: opens more concurrent connections than the
  configured ring count. It verifies enough attach successes to consume the
  configured slots, profiles `ring_attach_no_slot`, and keeps the excess
  connection on clean TCP without legacy DATA_FALLBACK frames.

## Basic API Tests

### `basic/RingApiDataPathTest.java`

This replaces the useful non-heartbeat parts of the removed legacy aggregate
basic test. These scenarios are explicitly non-blocking first: a Java
`read()`/`write()` returning 0 is treated as valid non-blocking progress state,
and the test requires bounded selector progress plus final SHA-256 ACK
verification. Server ACK-before-close behavior is covered here: ring ACK data
must be visible to the client before peer-close EOF is reported.

- `testNonBlockingReadWrite`: verifies non-blocking
  `SocketChannel.read/write` for a 2 KiB payload.
- `testNonBlockingDirectBufferReadWrite`: verifies non-blocking direct
  `ByteBuffer` writes route through the same ring path and preserve payload
  integrity.
- `testNonBlockingChunkedSmallWrites`: verifies many small non-blocking writes
  on one connection route through the ring data path and preserve payload
  integrity.
- `testNonBlockingGatherScatterReadWrite`: verifies non-blocking
  `SocketChannel.write(ByteBuffer[])` and `SocketChannel.read(ByteBuffer[])`
  route through the ring data path and preserve payload integrity.

## Assertion Strategy

- Prefer externally visible correctness first: process exit, success tokens,
  frame sequence integrity, byte counts, and SHA-256 hashes where applicable.
- Use UB logs/profile only for internal state that has no Java API surface:
  attach success/fallback, ring read/write use, polling observation, eventfd
  Selector injection, and ring slot exhaustion.
- Ring tests should assert absence of legacy control markers such as
  `DATA_FALLBACK`, `HEARTBEAT`, and `descriptor_sent=`.
- Negative-path tests must be bounded by process timeouts and should assert no
  VM crash or hang rather than requiring graceful application-level success.

## Scenario Modes

`NIOScenarioServer` modes relevant to current ring tests:

| Mode | Usage | Description |
|------|-------|-------------|
| `selector` | `selector <port> <size> <clients> [bindHost]` | Non-blocking selector server with bounded per-event reads and SHA-256 ACK |
| `delayedRead` | `delayedRead <port> <size> <readDelayMs> [clients]` | Accepts all clients, delays reads, then verifies payloads and replies |
| `echoFrames` | `echoFrames <port> <requestSize> <responseSize> <frames> [bindHost]` | Non-blocking fixed-frame echo server with sequence-preserving responses |
| `earlyClose` | `earlyClose <port>` | Accepts then immediately closes with SO_LINGER=0 |

`NIOScenarioClient` modes relevant to current ring tests:

| Mode | Usage | Description |
|------|-------|-------------|
| `parallel` | `parallel <host> <port> <size> <count> <prefix>` | Concurrent blocking clients with hash verification |
| `nonBlockingBasic` | `nonBlockingBasic <host> <port> <size> <id>` | Non-blocking single-buffer write and bounded ACK read |
| `nonBlockingDirect` | `nonBlockingDirect <host> <port> <size> <id>` | Non-blocking direct-buffer write and bounded ACK read |
| `nonBlockingChunked` | `nonBlockingChunked <host> <port> <size> <chunkSize> <id>` | Non-blocking chunked writes and bounded ACK read |
| `nonBlockingGatherScatter` | `nonBlockingGatherScatter <host> <port> <size> <segmentSize> <id>` | Non-blocking gathering write and scattering ACK read |
| `echoFrames` | `echoFrames <host> <port> <requestSize> <responseSize> <inflight> <frames> <id> [readChunkSize]` | Non-blocking single connection with fixed-frame sequence tracking and optional partial-read cap |
| `peerClose` | `peerClose <host> <port>` | Connects and expects peer close |
