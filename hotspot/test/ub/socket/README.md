# UBSocket jtreg Test Plan

This directory contains UBSocket jtreg coverage. The current UBSocket data path
is receiver-owned ring based: attach exchanges each peer's local ring slot,
payload bytes are written into the receiver's ring, and TCP carries only small
WAKEUP control frames for selector readiness. Tests should not assert the old
descriptor/unread-list/heartbeat/DATA_FALLBACK behavior.

## Directory Layout

- `ring/`: current ring data-path and wakeup policy coverage.
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
  and split into `-XX:UBSocketRingCount` inbound ring slots. The default is
  256 MiB total memory and eight 32 MiB slots.
- The ring slot size is `UBSocketMemorySize / UBSocketRingCount`; the total
  memory must divide evenly by the ring count and the slot size must be a
  multiple of the UB block size.
- Each attached fd consumes one local inbound slot. If no slot is available,
  attach fails and the connection falls back to TCP.
- A writer copies payload bytes into the peer's receiver-owned ring and then
  may send a 2-byte TCP WAKEUP frame.
- Sparse wakeup mode coalesces writes while the peer ring is non-empty, with
  empty-to-nonempty and threshold-driven wakeups.
- `-XX:UBSocketWakeupThresholdBytes=N` controls the non-empty-ring threshold
  for threshold-driven wakeups. The default is 64 KiB; small values can be used
  as a diagnostic/performance comparison mode.
- Selector readiness is driven by TCP wakeups plus `has_pending_data(fd)` /
  probe checks against ring state. Wakeup frames are not payload and must not
  be returned through Java reads.

## Known TODO

- `MultiServerTopologyTest` has exposed a rare fd reuse window in same-process
  multi-server selector scenarios. A closed channel may leave cancelled-key /
  UB selector state (`registered`, `ubProbe`, or `ubClosePending`) pending until
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
  `-XX:UBSocketConf`.
- `NIOScenarioServer` and `NIOScenarioClient` provide selector, non-blocking,
  and blocking scenarios. The `echoFrames` mode is the primary current ring
  regression: fixed-size request/response frames with sequence validation and
  configurable inflight.

## Ring Tests

### `ring/RingWakeupDataPathTest.java`

This is the primary jtreg coverage for the current implementation.

- `testSparseWakeupFrameEcho`: runs a non-blocking selector echo scenario with
  20-byte requests, 2 KiB responses, single connection, and 32 outstanding
  frames. It verifies sequence correctness, ring read/write profile events,
  wakeup parsing, sparse wakeup coalescing, and selector readiness injection.
- `testLowThresholdWakeupFrameEcho`: runs the same frame echo style with a
  small `-XX:UBSocketWakeupThresholdBytes` value and verifies the threshold
  wakeup profile path.
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
  `SocketChannel.read/write` for small 20-byte and 2 KiB payloads.
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
  attach success/fallback, ring read/write use, wakeup coalescing, selector
  injection, and ring slot exhaustion.
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
| `multiWrite` | `multiWrite <host> <port> <chunkSize> <chunkCount> <id>` | Multiple writes on one connection, useful for wakeup coalescing |
| `nonBlockingBasic` | `nonBlockingBasic <host> <port> <size> <id>` | Non-blocking single-buffer write and bounded ACK read |
| `nonBlockingDirect` | `nonBlockingDirect <host> <port> <size> <id>` | Non-blocking direct-buffer write and bounded ACK read |
| `nonBlockingChunked` | `nonBlockingChunked <host> <port> <size> <chunkSize> <id>` | Non-blocking chunked writes and bounded ACK read |
| `nonBlockingPacedMultiWrite` | `nonBlockingPacedMultiWrite <host> <port> <chunkSize> <chunkCount> <pauseMs> <id>` | Non-blocking paced writes for wakeup coalescing coverage |
| `nonBlockingGatherScatter` | `nonBlockingGatherScatter <host> <port> <size> <segmentSize> <id>` | Non-blocking gathering write and scattering ACK read |
| `echoFrames` | `echoFrames <host> <port> <requestSize> <responseSize> <inflight> <frames> <id>` | Non-blocking single connection with fixed-frame sequence tracking |
| `peerClose` | `peerClose <host> <port>` | Connects and expects peer close |
