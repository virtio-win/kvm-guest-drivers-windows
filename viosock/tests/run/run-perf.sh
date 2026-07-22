#!/usr/bin/env bash
# run-perf.sh — automated vsock_perf sweep (forward + reverse) × two
# buffer sizes (default and large). Loopback is intentionally omitted —
# hairpin traffic on the Windows guest does not represent real workload.
#
# Reads stdout of both sides (Linux vsock_perf and Windows vsock_perf.exe)
# and prints a single-line result per run.  The receiver's line is the
# authoritative RX Gbps; the sender's line is TX Gbps.  Both are logged
# to $LOGDIR/{fwd,rev}_<buf>.log.
#
# vsock_perf has NO TCP control channel of its own — the two peers talk
# only over vsock. So there is no wait_guest_port() to poll; we just
# sleep SERVER_GRACE_SECS after launching the receiver.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

# --- tunables (env-overridable) ------------------------------------------
PERF_BYTES=${PERF_BYTES:-1G}
PERF_BUF_DEFAULT=${PERF_BUF_DEFAULT:-64K}
PERF_BUF_LARGE=${PERF_BUF_LARGE:-1M}
PERF_VSK_SIZE=${PERF_VSK_SIZE:-}     # empty → don't pass --vsk-size
PERF_RCVLOWAT=${PERF_RCVLOWAT:-}     # empty → don't pass --rcvlowat
PERF_PORT=${PERF_PORT:-12347}
# When set (any non-empty value), pass --no-poll to the Windows-side
# receiver in reverse.  Needed on viosock builds without WSAPoll on
# accept()ed vsock sockets (upstream master today); harmless for our
# vhi-8.1 driver but reports "read() calls" instead of "POLLIN wakeups".
PERF_NO_POLL=${PERF_NO_POLL:-}
SERVER_GRACE_SECS=${SERVER_GRACE_SECS:-1}

# Guest-side paths used only by the --as-system code path.  Grouped
# here so a future move (e.g. to C:\ci-scratch\) is a one-line edit.
GUEST_SRV_BAT='C:\srv_perf.bat'
GUEST_SRV_LOG='C:\srv_perf.log'
SCHTASKS_NAME='vsock_perf_srv'

# --- args ----------------------------------------------------------------
CFG=""; LOGDIR=""; VARIANT="posix"; BITS="x64"; DIRS=""; AS_SYSTEM=0
LOCAL_BIN="/opt/vsock-test/vsock_perf"

while [ $# -gt 0 ]; do
    case "$1" in
        --config)      CFG="$2";       shift 2 ;;
        --logdir)      LOGDIR="$2";    shift 2 ;;
        --variant)     VARIANT="$2";   shift 2 ;;
        --bits)        BITS="$2";      shift 2 ;;
        --x86)         BITS="x86";     shift ;;
        --only)        DIRS="$2";      shift 2 ;;
        --as-system)   AS_SYSTEM=1;    shift ;;
        --local-bin)   LOCAL_BIN="$2"; shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 [--config <cfg>] [--logdir <dir>]
          [--variant posix|wsa|overlapped] [--bits x64|x86 | --x86]
          [--only forward,reverse] [--as-system] [--local-bin <path>]

Runs vsock_perf in every enabled direction × two buffer sizes.

Tunables (env-overridable, defaults in [brackets]):
  PERF_BYTES        [1G]      Total bytes to transfer per run.
  PERF_BUF_DEFAULT  [64K]     Sender/receiver buffer for the "default" run.
  PERF_BUF_LARGE    [1M]      Sender/receiver buffer for the "large"   run.
  PERF_VSK_SIZE     [unset]   Optional --vsk-size (SO_VM_SOCKETS_BUFFER_SIZE).
  PERF_RCVLOWAT     [unset]   Optional --rcvlowat.
  PERF_PORT         [12347]   vsock port for the throughput channel.
  SERVER_GRACE_SECS [1]       Sleep between receiver launch and sender start.

Flags:
  --variant       posix (default), wsa or overlapped. Only 'posix' works today;
                  the other two are planned for the native Winsock port.
  --bits          Guest binary bit-width (default: x64).
  --only          Comma-separated subset: forward, reverse.
  --as-system     For the reverse direction: launch the Windows receiver via
                  schtasks /ru SYSTEM instead of a background ssh session.
  --local-bin     Path to the Linux vsock_perf on this host (default:
                  $LOCAL_BIN — the one prepare-perf.sh installs).
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

case "$BITS"    in x64|x86) ;;             *) die "--bits must be x64 or x86" ;; esac
case "$VARIANT" in posix|wsa|overlapped) ;; *) die "--variant must be posix|wsa|overlapped" ;; esac

CFG=$(discover_config "$CFG")
guest_load "$CFG"
host_cid=$(config_read  "$CFG" host_cid);  [ -n "$host_cid"  ] || die "config has no host_cid="
guest_cid=$(config_read "$CFG" guest_cid); [ -n "$guest_cid" ] || die "config has no guest_cid="

[ -x "$LOCAL_BIN" ] || die "no vsock_perf at $LOCAL_BIN — run prepare-perf.sh first"

[ -z "$LOGDIR" ] && LOGDIR="/tmp/vsock-perf-$$"
mkdir -p "$LOGDIR"

# What runs where — 'posix' is the only variant today; 'wsa'/'overlapped'
# will pick a different exe / flag set through the same case-branch.
guest_perf_cmd() {
    local variant="$1" bits="$2" bin_dir="$3"
    local exe flags=''
    case "$bits" in
        x64) exe='vsock_perf.exe' ;;
        x86) exe='vsock_perf_x86.exe' ;;
    esac
    case "$variant" in
        posix)      ;;
        wsa)        flags=' --variant wsa' ;;
        overlapped) flags=' --variant overlapped' ;;
    esac
    printf '%s\\%s%s\n' "$bin_dir" "$exe" "$flags"
}
GUEST_CMD=$(guest_perf_cmd "$VARIANT" "$BITS" "$guest_bin_dir")

# What directions to run
run_fwd=1; run_rev=1
if [ -n "$DIRS" ]; then
    run_fwd=0; run_rev=0
    IFS=',' read -ra parts <<< "$DIRS"
    for p in "${parts[@]}"; do
        case "$p" in
            forward) run_fwd=1 ;;
            reverse) run_rev=1 ;;
            *) die "unknown --only stage: $p" ;;
        esac
    done
fi

# --- one run -------------------------------------------------------------
# Args:
#   1. direction (forward|reverse)
#   2. buf label (default|large)
#   3. buf value passed to --buf-size on both sides
_extra_args=()
[ -n "$PERF_VSK_SIZE" ] && _extra_args+=(--vsk-size "$PERF_VSK_SIZE")
[ -n "$PERF_RCVLOWAT" ] && _extra_args+=(--rcvlowat "$PERF_RCVLOWAT")

RESULTS=()   # "direction  buf-label  rx-gbps  tx-gbps"

run_forward() {
    local buf_label="$1" buf_val="$2"
    local rx_log="$LOGDIR/fwd_${buf_label}_rx.log"
    local tx_log="$LOGDIR/fwd_${buf_label}_tx.log"
    info "[forward/$buf_label] bytes=$PERF_BYTES  buf-size=$buf_val"

    # Linux receiver (this host) in background.
    "$LOCAL_BIN" --port "$PERF_PORT" --buf-size "$buf_val" "${_extra_args[@]}" \
        > "$rx_log" 2>&1 &
    local rx_pid=$!
    sleep "$SERVER_GRACE_SECS"

    # Windows sender.  host_cid is what the guest sees as our vsock CID.
    ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
        "$GUEST_CMD --sender $host_cid --port $PERF_PORT --bytes $PERF_BYTES --buf-size $buf_val" \
        > "$tx_log" 2>&1
    local tx_rc=$?

    wait "$rx_pid" 2>/dev/null
    local rx_rc=$?

    # tr -d '\r' — the Windows side emits CRLF.
    tr -d '\r' < "$tx_log" > "$tx_log.tmp" && mv "$tx_log.tmp" "$tx_log"

    _report forward "$buf_label" "$rx_log" "$tx_log" "$rx_rc" "$tx_rc"
}

run_reverse() {
    local buf_label="$1" buf_val="$2"
    local rx_log="$LOGDIR/rev_${buf_label}_rx.log"
    local tx_log="$LOGDIR/rev_${buf_label}_tx.log"
    info "[reverse/$buf_label] bytes=$PERF_BYTES  buf-size=$buf_val"

    # --no-poll is a Windows-only flag; only add it to the guest-side
    # receiver command, never to the Linux upstream vsock_perf.
    local no_poll=''
    [ -n "$PERF_NO_POLL" ] && no_poll=' --no-poll'

    # Windows receiver: same launch pattern as run-reverse.sh — background
    # ssh session so its job object owns the guest-side process; killing
    # the local ssh pid tears the process down cleanly.
    local rx_rc=0
    if [ "$AS_SYSTEM" -eq 1 ]; then
        _guest_ssh "(echo @echo off & echo $GUEST_CMD --port $PERF_PORT --buf-size $buf_val$no_poll ^> $GUEST_SRV_LOG 2^>^&1) > $GUEST_SRV_BAT"
        _guest_ssh "schtasks /create /tn $SCHTASKS_NAME /tr $GUEST_SRV_BAT /sc once /st 00:00 /ru SYSTEM /f" >/dev/null 2>&1
        _guest_ssh "schtasks /run /tn $SCHTASKS_NAME" >/dev/null 2>&1
        local rx_ssh_pid=""
    else
        ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
            "$GUEST_CMD --port $PERF_PORT --buf-size $buf_val$no_poll" \
            > "$rx_log" 2>&1 &
        local rx_ssh_pid=$!
    fi
    sleep "$SERVER_GRACE_SECS"

    # Linux sender.  guest_cid is what this host sees as the guest.
    "$LOCAL_BIN" --sender "$guest_cid" --port "$PERF_PORT" \
        --bytes "$PERF_BYTES" --buf-size "$buf_val" "${_extra_args[@]}" \
        > "$tx_log" 2>&1
    local tx_rc=$?

    if [ -n "$rx_ssh_pid" ]; then
        wait "$rx_ssh_pid" 2>/dev/null
        rx_rc=$?
    else
        _guest_scp_from "$GUEST_SRV_LOG" "$rx_log" 2>/dev/null || true
        _guest_ssh "del $GUEST_SRV_LOG 2>nul & schtasks /end /tn $SCHTASKS_NAME /f 2>nul & schtasks /delete /tn $SCHTASKS_NAME /f 2>nul & exit 0" >/dev/null 2>&1
    fi

    [ -f "$rx_log" ] && { tr -d '\r' < "$rx_log" > "$rx_log.tmp" && mv "$rx_log.tmp" "$rx_log"; }

    _report reverse "$buf_label" "$rx_log" "$tx_log" "$rx_rc" "$tx_rc"
}

_report() {
    local direction="$1" buf_label="$2" rx_log="$3" tx_log="$4" rx_rc="$5" tx_rc="$6"
    local rx_gbps tx_gbps
    rx_gbps=$(grep -oE 'rx performance: [0-9.]+' "$rx_log" 2>/dev/null | tail -1 | awk '{print $NF}')
    tx_gbps=$(grep -oE 'tx performance: [0-9.]+' "$tx_log" 2>/dev/null | tail -1 | awk '{print $NF}')
    [ -z "$rx_gbps" ] && rx_gbps='-'
    [ -z "$tx_gbps" ] && tx_gbps='-'
    printf '[%s/%s] RX=%s Gbps  TX=%s Gbps  rx-rc=%s  tx-rc=%s\n' \
        "$direction" "$buf_label" "$rx_gbps" "$tx_gbps" "$rx_rc" "$tx_rc"
    RESULTS+=("$direction $buf_label $rx_gbps $tx_gbps")
}

# --- go ------------------------------------------------------------------
info "== vsock_perf sweep: bytes=$PERF_BYTES  buffers={$PERF_BUF_DEFAULT, $PERF_BUF_LARGE}  logs in $LOGDIR =="

if [ "$run_fwd" -eq 1 ]; then
    run_forward default "$PERF_BUF_DEFAULT"
    run_forward large   "$PERF_BUF_LARGE"
fi
if [ "$run_rev" -eq 1 ]; then
    run_reverse default "$PERF_BUF_DEFAULT"
    run_reverse large   "$PERF_BUF_LARGE"
fi

echo
info "=== summary ==="
printf '  %-10s %-8s %10s %10s\n' direction buf 'RX Gbps' 'TX Gbps'
for r in "${RESULTS[@]}"; do
    read -r dir buf rx tx <<< "$r"
    printf '  %-10s %-8s %10s %10s\n' "$dir" "$buf" "$rx" "$tx"
done
