# vsock_test notes

Per-test special conditions, known failure fingerprints, and links to tickets.
Short reasons for a test being in the default skip list go inline in
`test-ignore.list`; this file holds the longer story.

Sections below match test IDs (run `vsock_test --list` to enumerate).

---

## Reverse-direction sweep findings (Windows = vsock acceptor, Linux = vsock connector)

The default `viosock/run-vsock-test-isolated.ps1` flow runs Linux as
`--mode=server` (vsock acceptor) and Windows as `--mode=client` (vsock
connector). That exercises the **connector path** of our driver
(`connect()`, `recv()`, `WSAPoll(POLLIN | POLLHUP)`, etc.) but never the
**acceptor path** (`bind`, `listen`, `accept`, peer-close-on-listening-fd).

Running with `-Reverse` flips the roles. Tested on `vhi-8.0` (commit 507b0b8b
= WSAPoll landed). For the control channel, Windows-side TCP needs an
inbound rule on the chosen port:

```bat
netsh advfirewall firewall add rule name="vsock_test_control_12345" ^
    dir=in action=allow protocol=TCP localport=12345
```

Full sweep result (39 tests): **pass=10, fail=29**.

| Result | Tests | Interpretation |
|---|---|---|
| **ok** (10) | 0, 2, 3, 4, 10, 12, 24, 29, 31, 34 | SOCK_STREAM happy-path acceptor works: connection reset, client-close, server-close, multiple connections, poll+SO_RCVLOWAT, invalid buffer, SIOCOUTQ, transport-release UAF guard, SO_LINGER null-ptr guard, SIOCINQ |
| **acceptor RST** (11) | 5, 8, 9, 11, 13, 14, 15, 21, 25, 35, 38 | Linux-connector sees `recv: Connection reset by peer`. Windows-acceptor RSTs the peer when it should close cleanly (or shouldn't close at all). Same symptom family as VSTOR-130675 ("FD_CLOSE on SHUTDOWN, cleanup RST as non-error") but on the **acceptor side** — separate fix. Test 21 (`double bind connect`) initially wedged Windows-acceptor; the hang-guard in the runner now force-kills the Windows process after the Linux client returns, and the symptom degrades to the same RST family. |
| **unexpected EOF** (2) | 22, 23 | `virtio credit update + SO_RCVLOWAT` / `+ low rx_bytes`: Linux-connector hits `unexpected EOF while receiving bytes`. Probably another acceptor-side premature close, surfaced in the credit-update path. |
| **`connect() #2 RST`** (1) | 30 | `retry failed connect()`: Linux-connector's second `connect()` hits `Connection reset by peer`. Windows-side fd state machine doesn't allow re-connect on a fresh socket cleanly. |
| **Unexpected lingering** (1) | 32 | `SO_LINGER close() on unread`: Windows-acceptor doesn't honor SO_LINGER timeout — close lingers past the configured period. |
| **`bind only` ETIMEDOUT** (1) | 1 | Linux-connector's `connect(2)` to a Windows-bound-but-not-listening fd hits ETIMEDOUT (110) instead of ECONNREFUSED. Symptom difference, may be Linux-bind-only test design assuming Linux acceptor semantics. |
| **skip-frame mismatch** (13) | 6, 7, 16-20, 26-28, 33, 36, 37 | Windows-side has `.skip = true` (SEQPACKET / SHUT_* / MSG_ZEROCOPY / leak-tests — Linux-only features). Windows vsock_test prints `SKIP` on the control channel; Linux-client doesn't handle that frame and bails with `expected "LISTENING" ... got "SKIP"`. Test-infra mismatch, **not a driver bug**. |

**Action items** (separate tickets when picked up):
- "vsock: sys: acceptor sends RST instead of clean close" — fingerprint:
  Linux-connector observes `recv: Connection reset by peer` on tests
  5/8/9/11/13/14/15/21/25/35/38. Probably the symmetric of VSTOR-130675's
  fix in Rx.c / Loopback.c, applied to the acceptor side. Tests 22/23
  ("unexpected EOF") and 30 ("connect #2 RST") are likely the same family
  with different recv-path manifestations.
- "vsock: sys: SO_LINGER not honored on close" — fingerprint: test 32.
  Acceptor closes immediately regardless of SO_LINGER timeout.
- "vsock_test: handle SKIP frame on Linux client side" — make the Linux
  vsock_test (upstream) tolerate Windows-side compile-time skips, so a
  reverse sweep doesn't false-fail 13 tests on SEQPACKET/SHUT_*/MSG_ZEROCOPY/leak.
  This is upstream territory but a local patch is possible.

---

## Loopback sweep findings (Windows = both sides, peer-cid = guest_cid)

Both `vsock_test --mode=server` and `--mode=client` on the same Windows
guest, control over `127.0.0.1`, vsock peer-cid = own cid (4). Driver
detects this in `VIOSockConnect` (`dst_cid == guest_cid`) and routes
through `viosock/sys/Loopback.c` (SOCK_LOOPBACK flag). Server launched
via `schtasks /ru SYSTEM` so it survives ssh disconnect; client invoked
from a separate ssh session. Per-test 20s timeout.

Runner: `viosock/run-vsock-test-loopback.sh` (per-pick isolated, fresh
control channel and server per test).

Full sweep (39 tests) on `vhi-8.0` after VSTOR-130675 driver fixes:
**pass=21, fail=12, hung=6**.

| Result | Tests | Interpretation |
|---|---|---|
| **ok** (21) | 0, 1, 4, 5, 6, 7, 16-20, 24, 26-31, 33, 36, 37 | Connect/accept/recv/poll path works in loopback. SEQPACKET (6, 7) and SHUT_*/MSG_ZEROCOPY/leak tests (16-20, 27, 28) show `ok` because Windows port marks them `.skip = true` and both sides agree to skip. |
| **hung / WSAPoll never wakes** (6) | 2, 3, 12, 14, 32, 34 | All use `vsock_wait_remote_close` → `WSAPoll(POLLIN \| POLLHUP)`. Loopback Tx blocks: see "Loopback Tx ownership transfer" below. |
| **`recv: connection reset`** (7) | 8, 9, 11, 13, 15, 25, 35 | All SOCK_SEQPACKET; driver doesn't implement, loopback path doesn't gate on socket type, test progresses far enough to hit recv RST. |
| **SO_RCVLOWAT / `unexpected EOF`** (3) | 10, 22, 23 | `SO_RCVLOWAT` not supported. |
| **`recv: connection reset`** (1) | 21 | `double bind connect` — separate failure mode, same as cross-host. |
| **`size mismatch: set 8 got 4`** (1) | 38 | `TX credit bounds` — known: `SO_VM_SOCKETS_BUFFER_MAX_SIZE` is ULONG in driver, test expects ULL. Listed in VSTOR-130675 description. |

### Loopback Tx ownership transfer (root cause of the 6 hangs)

In cross-host Tx, `VIOSockTxEnqueue` copies the send buffer into a virtio
ring entry and completes the `WDFREQUEST` of the sender as soon as the
data is queued. In loopback, the same path routes through
`viosock/sys/Loopback.c::VIOSockLoopbackTxEnqueue` → `Loopback.c:291`
`VIOSockRxRequestEnqueueCb(pDestSocket, Request, Length)` which **takes
the sender's `WDFREQUEST` and inserts it directly into the peer's
`RxCbList`** (`Rx.c:770` `InsertTailList(&pSocket->RxCbList, ...)`). The
sender's `NtWriteFile` therefore returns `STATUS_PENDING` and the
user-mode `viosocklib.dll` blocks in
`viosock/lib/native.c:367` `WaitForSingleObject(pContext->hEvent, INFINITE)`
until the peer calls `recv()`.

For any test whose pattern is `send_byte → close` without a matching
peer `recv()` (tests 2, 3, 12, 14, 32, 34), the sender's `send_byte`
hangs forever, the test deadlocks, and the test framework's control
channel gets stuck waiting for a sync from the dead peer. In cross-host
this isn't visible because the virtio Tx ring decouples completion from
peer consumption.

A separate `vsock_wait_remote_close` quirk in the Windows port —
`WSAPoll(POLLIN | POLLHUP)` returns on POLLIN as soon as data arrives,
not on peer close (Linux uses `EPOLLRDHUP` which fires only on shutdown)
— additionally causes test 2's server to exit `vsock_wait_remote_close`
prematurely on FD_READ. The race is masked in cross-host by virtio
latency placing SHUTDOWN before the FD_READ check. A POLLHUP-only loop
in `util.c::vsock_wait_remote_close` is the correct test-side fix, but
on its own it does not unblock loopback because the loopback Tx hang
sits above it.

**Tickets**: VSTOR-130675 (cross-host RST/FD_CLOSE fixes), VSTOR-130045
(test port), new ticket "viosock: sys: loopback Tx must not transfer
WDFREQUEST ownership to peer Rx queue" (to be filed).

---

<!--
Template for new entries:

## test N - <name>

**Vsock roles**: (which side is acceptor / connector)
**What it exercises**: (the actual driver path being checked)
**Known failure fingerprint**: (trace snippet + interpretation)
**Tickets**: (links)
-->
