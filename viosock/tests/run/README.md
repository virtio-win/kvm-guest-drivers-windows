# viosock/tests/run — manual + CI test harness

Bash scripts and per-direction test lists that drive the `vsock_test`
interop suite (upstream Linux `tools/testing/vsock`) against a Windows
guest running the viosock driver.

Two flows are supported from the same set of scripts:

* **CI drop** — start from a libvirt domain name + an artifact package
  and let `prepare-ci.sh` build everything (Linux `vsock_test`, guest
  cert/testsigning, driver install, test binaries) end-to-end.
* **Dev bed** — the driver/tests are already deployed by the
  `viosock-deploy` skill; you just want to re-run the sweeps. Point
  `run-all.sh` (or an individual `run-*.sh`) at a config file and go.

## Layout

| File               | Purpose |
|--------------------|---------|
| `_lib.sh`          | Sourced helpers: config parse/discover, list parse, SSH/SCP/PowerShell wrappers, `variant_to_cmd`, JUnit XML builders, port-wait. |
| `resolve-guest.sh` | Query a libvirt domain via `virsh`, emit a config file (`guest_name`, `guest_cid`, `guest_ip`, `guest_user`, `ssh_key`, `host_cid=2`). |
| `prepare-host.sh`  | Build the pinned upstream Linux `vsock_test` into `/opt/vsock-test/vsock_test`; punch `firewalld` for the control ports. Idempotent. |
| `prepare-guest.sh` | Import publisher cert, enable `bcdedit testsigning`, add the Windows firewall rule for TCP 12345/12346, reboot if needed. |
| `install-driver.sh`| `scp` the package to `C:\viosock-pkg`, run `pnputil /add-driver /install` (tolerates "up-to-date"). |
| `install-tests.sh` | `scp` `vsock_test.exe` (+ `vsock_test_x86.exe` if present) into `guest_bin_dir` (default `C:`). |
| `prepare-ci.sh`    | Orchestrator: `resolve-guest → prepare-host → prepare-guest → install-driver → install-tests`. |
| `setup-env.sh`     | Inter-run cleanup on the guest (kill stray `vsock_test.exe`, wipe stale schtasks, verify the viosock PnP device). |
| `run-forward.sh`   | Forward sweep: Linux = vsock acceptor, Windows = connector. |
| `run-reverse.sh`   | Reverse sweep: Windows = vsock acceptor, Linux = connector. |
| `run-loopback.sh`  | Loopback sweep: both roles on the Windows guest. |
| `run-all.sh`       | Orchestrates `setup-env → forward → reverse → loopback`; aggregates exit code, optionally emits JUnit per suite. |
| `forward.list` / `reverse.list` / `loopback.list` | Test IDs to run per direction. |

Removed (historical): `guest-exec.sh` (virsh-agent fallback), `update-driver.sh`.
All guest access now goes through the `_guest_ssh` / `_guest_scp_*` /
`_guest_ps` helpers in `_lib.sh` — SSH-only, no virtio-serial fallback.

## Config file

The config is a `key = value` file. `#` starts a comment; whitespace
around `=` is ignored. Fields consumed by the runners:

| Key             | Meaning                                                            |
|-----------------|--------------------------------------------------------------------|
| `guest_name`    | libvirt domain name (used by `prepare-host` / `resolve-guest`).   |
| `guest_cid`     | vsock CID of the guest.                                            |
| `host_cid`      | vsock CID of the host (nearly always `2`).                         |
| `guest_ip`      | IPv4 the runners SSH into and use as `--control-host`.             |
| `guest_user`    | SSH login (default: `Administrator`).                              |
| `ssh_key`       | Path to the SSH private key; `~` is expanded.                      |
| `guest_bin_dir` | Where `vsock_test.exe` lives on the guest (default: `C:`).         |

`resolve-guest.sh` produces this file from a libvirt domain name and
an SSH key path. In dev flows you can also write it by hand — see
`../../dev008.config` for an example.

## Config discovery

Every runner accepts `--config <path>`. When omitted, `_lib.sh` looks in
this order:

1. `--config <path>` (explicit).
2. `$VSOCK_CI_CONFIG` env var.
3. Exactly one `*.config` under `/tmp/vsock-ci/` (the CI-standard drop
   location — this is where `prepare-ci.sh` writes by default).
4. Exactly one `*.config` next to the script (in-repo dev flow).
5. Exactly one `*.config` in `../..` (i.e. the `viosock/` dir).

Multiple candidates → the script prints them and exits 2 so you can
disambiguate. None → same, with an error explaining what to do.

## Contract

**Enabled = must pass.** No "expected failures". A failing enabled row
is a regression, and the runner exits non-zero. To exclude a row (bug,
Linux-only feature, WIP), comment its line in the corresponding `*.list`
file **and include a `disabled: <reason>` note** on the same line — the
next reader has to know why. Runtime feature detection is not "skipped"
— if the guest fails a feature probe, that's a failure, not a skip.

`vsock_test` binary is a **faithful mirror of upstream**: `test_cases[]`
IDs must match, or `--pick N` desyncs between guest and host. Unimplemented
Windows-side callbacks are wired to `NULL`; `run_tests()` prints
`not implemented on this platform` and exits non-zero when it hits one —
which is exactly the "failure" the list is supposed to filter out.

## Variants (id ↔ Windows command)

Every list row carries a `variant` column. The `<id>` matches upstream
`vsock_test` (both sides pick the same numeric ID), and `variant` picks
which **Windows-side** invocation to run:

| variant       | Guest command                                          | Notes                                                                 |
|---------------|--------------------------------------------------------|-----------------------------------------------------------------------|
| `posix`       | `<guest_bin_dir>\vsock_test.exe`                       | The current Linux-source port through `compat.h` (Winsock2 shim).     |
| `wsa`         | `<guest_bin_dir>\vsock_test.exe --variant wsa`         | (Future.) A native Winsock port with no `compat.h`.                   |
| `overlapped`  | `<guest_bin_dir>\vsock_test.exe --variant overlapped`  | (Example.) Same exe, overlapped IO mode.                              |

Central mapping: `variant_to_cmd()` in `_lib.sh` — a new variant is one
`case`-branch. `variant_to_image()` returns the exe basename so
`taskkill /IM` covers every variant that shares an exe.

A single test ID may appear multiple times with different variants; each
row is an independent enabled/disabled unit, so we can (for example)
keep `posix` green while `wsa` is still being fixed.

Restrict a run to one variant with `--variant`:

```sh
./run-all.sh    --variant posix
./run-forward.sh --variant wsa
```

Omit `--variant` to run every declared variant in every list.

## Bit-width (`--bits` / `--x86`)

Runners take the guest bit-width as a sweep-wide flag:

```sh
./run-all.sh --bits x64   # default
./run-all.sh --bits x86   # same list, but pull vsock_test_x86.exe
./run-all.sh --x86        # shorthand for --bits x86
```

The list files don't need duplicate rows per bit-width; the same
`(id, variant)` re-runs against either binary.

## Single-test iteration (`--pick`)

While iterating on a fix, the whole list is overkill. `--pick` bypasses
the list and drives one (or several) IDs directly:

```sh
./run-reverse.sh --pick 4                # single test
./run-reverse.sh --pick 4,5,10           # comma-separated
./run-reverse.sh --pick 4 --pick 10      # repeatable
./run-reverse.sh --pick 4 --variant wsa  # variant defaults to posix
```

IDs given via `--pick` do not have to be enabled in the list — the point
is that you can drive an under-development or currently-disabled test
without editing the list.

## Windows-server launch mode

`run-reverse.sh` and `run-loopback.sh` need the Windows-side server to
outlive the SSH channel that started it. Two modes are supported:

* **Default (bg-ssh, Administrator).** The server is launched via a
  background SSH session as the configured `guest_user`. Killing the
  local SSH PID after the client finishes closes sshd's job object on
  the guest — a clean Windows-native tear-down.
* **`--as-system`.** The server is scheduled via `schtasks /ru SYSTEM`
  (Session 0). Useful when an Administrator-context bug prevents the
  server from binding vsock (tests 4/31 currently exhibit an
  `accept: Unknown error` flake under bg-ssh that clears under SYSTEM;
  root-cause TBD).

Whichever mode is used, `setup-env.sh` clears stale
`vsock_rev` / `vsock_loopback` scheduled tasks and stray `vsock_test.exe`
processes before every sweep.

### Server-ready grace period (`SERVER_GRACE_SECS`)

Between `wait_guest_port` (which detects the TCP control-port listener) and
the client connect, `run-reverse.sh` sleeps `SERVER_GRACE_SECS` seconds.
The Windows-side vsock listen socket may become ready shortly after the
control TCP port, so connecting too early can trip a `vsock connect
timeout` on the accept-race-heavy tests (test 4 is the usual canary).

Default: `1s`. Overridable from the environment without touching the
script:

```sh
SERVER_GRACE_SECS=2 ./run-reverse.sh --junit /tmp/reverse.xml
SERVER_GRACE_SECS=3 ./run-all.sh    --only reverse --junit-dir /tmp/junit
```

Bump it if a reverse test starts intermittently failing on the very
first `connect()` — that's the signal the grace is too short for the
current hardware / driver revision.

## JUnit XML output

Per-sweep JUnit XML for CI ingestion:

```sh
./run-forward.sh  --junit /tmp/forward.xml
./run-all.sh      --junit-dir /tmp/junit   # /tmp/junit/{forward,reverse,loopback}.xml
```

The XML uses `<testsuites>/<testsuite>/<testcase>`, `<failure>` embeds
client + server log content via CDATA. `time=` per case is measured with
`date +%s.%N`. CRLF is stripped from captured output before it lands in
the XML.

## Failed-only log retention

Every sweep writes `cli_<N>_<V>.log` / `srv_<N>_<V>.log` under
`--logdir` (default: `/tmp/vsock-run-<pid>`). Passing tests remove their
own logs. Failing/hung tests leave logs behind and the sweep summary
prints their absolute paths, so you can go straight from a red summary
to the actual output. `run-all.sh --logdir` shares one directory across
all three sweeps.

## Running

### CI drop (from scratch)

Package `<pkg>` must contain, at minimum:

```
viosock.inf, viosock.sys, viosock.cat, viosocklib_x64.dll,
viosockwspsvc.exe, vsock_test.exe   (+ vsock_test_x86.exe if built)
VirtIOTestCert.cer                  (unless --whql or --skip-cert)
```

Run the CI preparation pipeline:

```sh
./prepare-ci.sh \
    --guest <libvirt-domain> \
    --ssh-key ~/.ssh/id_rsa    \
    --package /path/to/package
```

Then the sweeps:

```sh
./run-all.sh --junit-dir /tmp/junit
```

Useful `prepare-ci.sh` flags:

| Flag                      | Effect |
|---------------------------|--------|
| `--config <path>`         | Skip `resolve-guest`; use a pre-resolved config. |
| `--config-out <path>`     | Where `resolve-guest` writes the config (default `/tmp/vsock-ci/<guest>.config`). |
| `--ssh-user <name>`       | SSH login (default: `Administrator`). |
| `--cert-cer <path>`       | Publisher cert to import into the guest (defaults to `<pkg>/VirtIOTestCert.cer`). |
| `--skip-cert`             | Do not import a publisher cert. |
| `--skip-test-signing`     | Do not enable `bcdedit testsigning`. |
| `--skip-reboot`           | Do not reboot after testsigning was just enabled. |
| `--whql`                  | Shorthand for `--skip-cert + --skip-test-signing` (WHQL-signed driver). |
| `--skip-host-prep`        | Skip building the Linux `vsock_test`. |
| `--skip-guest-prep`       | Skip cert/testsigning. |
| `--skip-driver`           | Skip `install-driver.sh`. |
| `--skip-tests`            | Skip `install-tests.sh`. |
| `--linux-ver <ver>`       | Kernel version to build `vsock_test` from (default hard-coded in `prepare-host.sh`, currently 6.19.8). |

### Dev bed (`viosock-deploy` already ran)

If the driver and `vsock_test.exe` are already on the guest, skip
`prepare-ci.sh` entirely and drive the sweeps against your dev config:

```sh
./run-all.sh --config viosock/dev008.config
./run-all.sh --config viosock/dev008.config --only forward,reverse
./run-all.sh --config viosock/dev008.config --as-system
./run-forward.sh --pick 21
```

### Single-stage invocations

Every stage can be run standalone; they all read the same config:

```sh
./resolve-guest.sh  --guest <domain> --ssh-key <key> --out /tmp/vsock-ci/x.config
./prepare-host.sh   --config /tmp/vsock-ci/x.config
./prepare-guest.sh  --config /tmp/vsock-ci/x.config --cert-cer <pkg>/VirtIOTestCert.cer
./install-driver.sh --config /tmp/vsock-ci/x.config --package <pkg>
./install-tests.sh  --config /tmp/vsock-ci/x.config --from <pkg>
./setup-env.sh      --config /tmp/vsock-ci/x.config
./run-forward.sh    --config /tmp/vsock-ci/x.config
```

## Editing the lists

Contract for edits:

* Row format: `<id>  <variant>  <free-text description>`.
* Disabled = leading `#`, MUST include `disabled: <reason>` on the same
  line (grep-able).
* Comment blocks with `#`-only lines are fine for section headers.
* When a fix lands: uncomment the row, drop the `disabled:` note.
* IDs are stable — they map to positions in `test_cases[]` inside
  upstream `vsock_test.c` and are the same `--pick N` on both sides.
* Adding a new `(id, variant)` pair for an existing ID is fine: two rows
  with the same id and different variants coexist and are treated
  independently.
* Do **not** add tests unique to this fork. Adding a row shifts every
  subsequent index in `test_cases[]` and desyncs `--pick N` against a
  stock upstream host binary. Validate new behavior through `vsock_perf`
  or an existing upstream test instead.

Sanity check what the runner sees for a list, all variants:

```sh
awk '/^[[:space:]]*#/||/^[[:space:]]*$/{next} {if($1~/^[0-9]+$/&&$2!=""&&$2!~/^[0-9]/) print $1"\t"$2}' forward.list
```

That's the same parse as `read_list` in `_lib.sh`; append the variant
name as an extra `awk` guard to see only one variant.

## PowerShell helper (`_guest_ps`)

`_guest_ps <script>` in `_lib.sh` runs a PowerShell snippet on the guest
via `-EncodedCommand` (base64-UTF16LE). It sidesteps every cmd/ssh
quoting layer, so the snippet can contain literal quotes, `$` sigils,
and heredocs without escaping. Prefer it over hand-quoting cmd strings
for any PowerShell work on the guest.
