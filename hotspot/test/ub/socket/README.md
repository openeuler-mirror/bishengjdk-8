# UBSocket jtreg Test Plan

This directory contains UBSocket jtreg coverage grouped by test intent. Keep
shared helpers in this directory and put only jtreg entry tests in the typed
subdirectories.

## Directory Layout

- `basic/`: successful attach, data-path, complex topology, and VM option validation.
- `attach/`: attach timing, timeout, restart, and early-request behavior.
- `error/`: fallback and failure handling, including peer close and control-port conflicts.
- `test-classes/`: helper programs launched by the jtreg entry tests.

## Shared Helpers

- `SocketTestSupport.java` creates Java child processes, allocates free ports,
  configures `UBLog=path=...,socket=debug`, merges child stdout/stderr with
  the UBLog file, and provides common assertions.
  Tests that need per-feature files can use `socket_path=...`, `file_path=...`,
  or `heap_path=...`; `%p` in UBLog paths expands to the JVM process id.
  Features without a path write to `tty`.
  It also centralizes UB log assertions for attach bind success, TCP fallback,
  heartbeat frames, `DATA_FALLBACK` markers, data-transfer success tokens, and remote-memory refcount
  markers, plus resource cleanup checks used by scenario tests.
- `SocketTestConfig.java` writes the socket allow-list used by `-XX:UBSocketConf`.
- `test-classes/NIOScenarioServer.java` and `test-classes/NIOScenarioClient.java`
  provide the runtime server/client scenarios used by the tests.
- `test-classes/SocketTestData.java` generates deterministic test payloads and
  provides `sha256Hex()` for data integrity verification.
- `test-classes/SocketMultiServerMain.java` runs multiple servers and clients
  inside a single JVM for same-process attach testing.

## Data Integrity Verification

All data-transfer tests use SHA-256 hash verification to ensure end-to-end
data integrity. The protocol works as follows:

1. Client computes `SHA-256(payload)` before sending.
2. Server computes `SHA-256(received)` while reading.
3. Server includes the hash in its ACK response: `ACK <n> bytes received, hash <hex>`.
4. Client compares the server's hash against its own; throws `HASH_MISMATCH`
   on discrepancy.

The only exception is `SocketMultiServerMain`, which uses `Arrays.equals()`
for full byte-by-byte comparison (strictest possible verification).

## Data-Path Memory Reuse

UBSocket data frames point to sender-side shared-memory ranges.
`UBSocketBlkBitmap` prevents `get_free_memory()` from reusing blocks that have
not been consumed yet. `UnreadMsgTable` runs a background reclaim loop for all
UBSocket configurations: it always detects consumed ranges and releases bitmap
bits. `UBSocketBlkMeta` now records `state/fd/send_nanos/recv_nanos/read_nanos`:
`SEND` means the sender published the allocation but the receiver has not yet
parsed the data frame, `RECV` means the receiver accepted the frame but has not
finished draining the payload, and `READ` means the payload is fully consumed
and ready for sender-side reclaim. The sender-side `SEND -> RECV` timeout is
always enabled; if the peer does not accept a descriptor in time, the fd switches
to `DATA_FALLBACK` so the remaining payload is carried by normal TCP. When
`UBSocketTimeout > 0`, `RECV -> READ` timeout sends heartbeat frames while the
peer drains the payload.

## Server Modes

`NIOScenarioServer` supports the following modes:

| Mode | Usage | Description |
|------|-------|-------------|
| `selector` | `selector <port> <size> <clients> [bindHost]` | Non-blocking selector server with Netty-style read budget: each `OP_READ` event reads at most 16 times and 64 KiB per read, SHA-256 hash in ACK |
| `delayedAccept` | `delayedAccept <port> <size> <clients> <delayMs> [holdMs]` | Delays accept to simulate slow server startup, SHA-256 hash in ACK |
| `delayedRead` | `delayedRead <port> <size> <readDelayMs> [clients]` | Accepts immediately but delays the first read to force sender-side `SEND -> RECV` recv-timeout fallback, SHA-256 hash in ACK |
| `earlyClose` | `earlyClose <port>` | Accepts then immediately closes with SO_LINGER=0 (RST) |

## Client Modes

`NIOScenarioClient` supports the following modes:

| Mode | Usage | Description |
|------|-------|-------------|
| `basic` | `basic <host> <port> <size> <id>` | Single connection, send data, verify hash in ACK |
| `chunked` | `chunked <host> <port> <size> <chunkSize> <id>` | Single connection, writes payload in many small chunks to exercise fixed-size data-frame handling under dense writes, verify hash |
| `gatherScatter` | `gatherScatter <host> <port> <size> <segmentSize> <id>` | Single connection using gathering writes and scattering reads to exercise writev/readv adaptation, verify hash |
| `transferTo` | `transferTo <host> <port> <size> <id>` | Single connection using FileChannel.transferTo from a plain file to an attached UBSocket, verify hash |
| `parallel` | `parallel <host> <port> <size> <count> <prefix>` | Concurrent connections from threads, verify hash |
| `sequential` | `sequential <host> <port> <size> <rounds> <id>` | Serial reconnect across rounds, verify hash each round |
| `restartAware` | `restartAware <host> <port> <size> <rounds> <id>` | Like sequential but retries connect on failure |
| `refCount` | `refCount <host> <port> <size> <id>` | Opens two channels to same server, verify hash on both |
| `waitSendTimeout` | `waitSendTimeout <host> <port> <firstSize> <secondSize> <pauseMs> <id>` | Sends one UB-backed chunk, waits for sender-side `SEND -> RECV` recv timeout to trigger `DATA_FALLBACK`, then sends the remainder over TCP and verifies the combined hash |
| `multiWrite` | `multiWrite <host> <port> <chunkSize> <chunkCount> <id>` | Sends several large writes on one connection, verifies the combined hash, and is used by allocation-pressure and post-fallback continued-write scenarios |
| `parallelMultiWrite` | `parallelMultiWrite <host> <port> <chunkSize> <chunkCount> <clientCount> <prefix>` | Starts concurrent multi-write clients to verify simultaneous `DATA_FALLBACK` isolation and hash integrity |
| `peerClose` | `peerClose <host> <port>` | Connects and expects peer to close immediately |

## Test Strategy

- Prefer behavior assertions first: process exit, client/server success tokens,
  byte counts, hashes, and expected fallback.
- Use UB logs only for UBSocket-internal state that has no public result surface,
  such as attach bind/fallback, remote-memory refcount, absence of raw payload
  logging, queue-limit warnings, and cleanup warnings.
- Always read UB logs through `SocketTestSupport.combinedOutput(...)`; direct
  `OutputAnalyzer` output is not a stable source for VM-side UB logs.
- Keep negative-path tests bounded. If a child process is expected to fail or be
  killed, assert that it exits and does not crash the VM, rather than requiring
  a normal zero exit.
- For control-port conflict tests, always use `createUbProcessBuilder` for both
  client and server so that the UB fallback path is actually exercised.
- For tests that depend on UBSocket-internal log text, prefer the named helpers
  in `SocketTestSupport` over open-coded string checks. This keeps terms such as
  `bind success`, `fallback to TCP`, `send HEARTBEAT frame`, `write_data success`,
  `read_data requested=`, and `ref=0` in one place.
- UBLog levels should use the full `warning` spelling in options and docs; the
  shortened spelling is intentionally rejected by `OptionsTest`.

## Category Coverage

### basic/

- `UBSocketBasicTest.java`: single attach, IPv6 loopback attach, normal data path, fixed-size binary data-frame handling under dense chunked writes, gathering write and scattering read, no abnormal cleanup or mmap/munmap failures, large selector-mode payload that requires heartbeat progress, and the negative no-heartbeat case that must not complete normally.
- `TransferToTest.java`: plain file `FileChannel.transferTo` to an attached UBSocket, with SHA-256 verification and UBSocket data-transfer log assertions.
- `MultiServerTopologyTest.java`: complex successful topologies: same-process single connection, same-process multi-listener sequential and concurrent clients, multi-JVM clients sharing one server, plus remote-memory refcount lifecycle (`ref=2` to `ref=0`).
- `OptionsTest.java`: VM option validation and socket config loading.

### attach/

- `ReconnectTest.java`: repeated reconnect attach on one server, client reconnect across server restart, and same-control-port restart after pending early requests, with hash verification and cleanup checks.
- `AttachTimeoutTest.java`: attach timeout fallback to TCP and timeout fallback socket cleanup.
- `EarlyRequestCacheTest.java`: early control request cached until server appears, plus early-request expiry fallback.
- `EarlyRequestQueueLimitTest.java`: raw valid attach requests fill the early-request queue until the hard cap, then verify the limit warning, listener bind-address log, and a real client fallback with hash-verified TCP transfer.

### error/

- `PeerCloseTest.java`: server closes connection before attach completes, server unavailable before client connects, and server killed during/after attach; client should not crash or hang.
- `PortConflictTest.java`: control port occupied by non-UB process or raced by two UB servers; fallback to TCP expected.
- `UnilateralFallbackTest.java`: only one side enables UB, falls back to TCP; also verifies a UB-enabled client can fall back when connecting to a public HTTP endpoint.
- `DataFallbackTest.java`: attach succeeds first, then sender emits `DATA_FALLBACK` and switches remaining traffic to TCP for oversized single writes, small-pool `alloc_failed` multi-write fallback, concurrent small-pool allocation pressure, sender-side `recv_timeout` fallback, and long post-fallback write streams; verifies hash-correct transfer and no raw TCP payload logging.
- `ConcurrentCloseTest.java`: closes SocketChannel while attach or data fallback/write is in progress; verifies the JVM exits cleanly without VM crash or mapping-operation failures.
- `WrapperFailureTest.java`: manipulates `/tmp/ubwrapper` backing files to force server init malloc failure and attach-time remote mmap failure; verifies TCP fallback and hash-correct transfer.
