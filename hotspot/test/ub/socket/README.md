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
  or `heap_path=...`; features without a path write to `tty`.
  It also centralizes UB log assertions for attach bind success, TCP fallback,
  heartbeat descriptors, data-transfer success tokens, and remote-memory refcount
  markers, plus resource cleanup checks used by scenario tests.
- `SocketTestConfig.java` writes the socket allow-list used by `-XX:UBSocketConfPath`.
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

## Server Modes

`NIOScenarioServer` supports the following modes:

| Mode | Usage | Description |
|------|-------|-------------|
| `selector` | `selector <port> <size> <clients>` | Non-blocking selector server with Netty-style read budget: each `OP_READ` event reads at most 16 times and 64 KiB per read, SHA-256 hash in ACK |
| `delayedAccept` | `delayedAccept <port> <size> <clients> <delayMs> [holdMs]` | Delays accept to simulate slow server startup, SHA-256 hash in ACK |
| `earlyClose` | `earlyClose <port>` | Accepts then immediately closes with SO_LINGER=0 (RST) |

## Client Modes

`NIOScenarioClient` supports the following modes:

| Mode | Usage | Description |
|------|-------|-------------|
| `basic` | `basic <host> <port> <size> <id>` | Single connection, send data, verify hash in ACK |
| `chunked` | `chunked <host> <port> <size> <chunkSize> <id>` | Single connection, writes payload in many small chunks to exercise dense descriptor parsing and buffer growth, verify hash |
| `parallel` | `parallel <host> <port> <size> <count> <prefix>` | Concurrent connections from threads, verify hash |
| `sequential` | `sequential <host> <port> <size> <rounds> <id>` | Serial reconnect across rounds, verify hash each round |
| `restartAware` | `restartAware <host> <port> <size> <rounds> <id>` | Like sequential but retries connect on failure |
| `refCount` | `refCount <host> <port> <size> <id>` | Opens two channels to same server, verify hash on both |
| `peerClose` | `peerClose <host> <port>` | Connects and expects peer to close immediately |

## Test Strategy

- Prefer behavior assertions first: process exit, client/server success tokens,
  byte counts, hashes, and expected fallback.
- Use UB logs only for UBSocket-internal state that has no public result surface,
  such as attach bind/fallback, remote-memory refcount, and cleanup warnings.
- Always read UB logs through `SocketTestSupport.combinedOutput(...)`; direct
  `OutputAnalyzer` output is not a stable source for VM-side UB logs.
- Keep negative-path tests bounded. If a child process is expected to fail or be
  killed, assert that it exits and does not crash the VM, rather than requiring
  a normal zero exit.
- For control-port conflict tests, always use `createUbProcessBuilder` for both
  client and server so that the UB fallback path is actually exercised.
- For tests that depend on UBSocket-internal log text, prefer the named helpers
  in `SocketTestSupport` over open-coded string checks. This keeps terms such as
  `bind success`, `fallback to TCP`, `Heartbeat:0:0;`, and `ref=0` in one place.
- UBLog levels should use the full `warning` spelling in options and docs; the
  shortened spelling is intentionally rejected by `SocketOptionsTest`.

## Category Coverage

### basic/

- `SocketSingleTest.java`: single attach, normal data path, descriptor-buffer growth under dense chunked writes, no abnormal cleanup or mmap/munmap failures, large selector-mode payload that requires heartbeat progress, and the negative no-heartbeat case that must not complete normally.
- `SocketMultiServerTest.java`: complex successful topologies: same-process single connection, same-process multi-listener sequential and concurrent clients, multi-JVM clients sharing one server, plus remote-memory refcount lifecycle (`ref=2` to `ref=0`).
- `SocketOptionsTest.java`: VM option validation and socket config loading.

### attach/

- `SocketSequentialReconnectTest.java`: repeated reconnect attach with hash verification and cleanup checks.
- `SocketServerRestartTest.java`: client reconnect across server restart with hash verification.
- `SocketAttachTimeoutTest.java`: attach timeout fallback to TCP, early request expiry, and timeout fallback socket cleanup.
- `SocketEarlyRequestCacheTest.java`: early control request cached until server appears.

### error/

- `SocketPeerCloseTest.java`: server closes connection before attach completes, server unavailable before client connects, and server killed during/after attach; client should not crash or hang.
- `SocketControlPortConflictTest.java`: control port occupied by non-UB process or raced by two UB servers; fallback to TCP expected.
- `SocketUnilateralFallbackTest.java`: only one side enables UB, falls back to TCP.
