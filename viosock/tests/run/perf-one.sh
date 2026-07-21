#!/usr/bin/env bash
# perf-one.sh — one-shot manual vsock_perf run for interactive tuning.
#
# One direction, one buffer size, one --bytes budget.  Prints the raw
# vsock_perf stdout of both sides (receiver first, then sender) plus a
# single-line RX/TX Gbps summary.  Same tunables env-vars as run-perf.sh,
# but everything is also settable via CLI.
#
# Typical uses:
#   ./perf-one.sh --direction forward --buf-size 256K
#   ./perf-one.sh --direction reverse --bytes 4G --buf-size 1M --vsk-size 4M
#   ./perf-one.sh --direction forward --rcvlowat 65536

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

# --- defaults ------------------------------------------------------------
DIRECTION=""
BYTES=${PERF_BYTES:-1G}
BUF=${PERF_BUF:-64K}
VSK_SIZE=${PERF_VSK_SIZE:-}
RCVLOWAT=${PERF_RCVLOWAT:-}
PORT=${PERF_PORT:-12347}
GRACE=${SERVER_GRACE_SECS:-1}
CFG=""; VARIANT="posix"; BITS="x64"; AS_SYSTEM=0
NO_POLL=${PERF_NO_POLL:-}
LOCAL_BIN="/opt/vsock-test/vsock_perf"

# Guest-side paths used only by the --as-system code path.  Grouped
# here so a future move (e.g. to C:\ci-scratch\) is a one-line edit.
GUEST_SRV_BAT='C:\srv_perf_one.bat'
GUEST_SRV_LOG='C:\srv_perf_one.log'
SCHTASKS_NAME='vsock_perf_one'

usage() {
    cat >&2 <<EOF
Usage: $0 --direction forward|reverse [options]

Required:
  --direction {forward|reverse}
      forward — Linux receiver / Windows sender
      reverse — Windows receiver / Linux sender

Perf knobs (defaults show current env or hard-coded fallback):
  --bytes    <bytes>K|M|G   Total bytes to transfer                    [$BYTES]
  --buf-size <bytes>K|M|G   Per-send / per-recv buffer                 [$BUF]
  --vsk-size <bytes>K|M|G   SO_VM_SOCKETS_BUFFER_SIZE (both sides)     [${VSK_SIZE:-unset}]
  --rcvlowat <bytes>K|M|G   SO_RCVLOWAT (receiver side)                [${RCVLOWAT:-unset}]
  --port     <port>         vsock port                                 [$PORT]
  --grace    <secs>         Sleep between receiver start and sender    [$GRACE]

Other:
  --config <path>           config file (auto-discovered otherwise)
  --variant posix|wsa|overlapped   Windows-side binary variant         [$VARIANT]
  --bits    x64|x86         Guest binary bit-width                     [$BITS]
  --x86                     shorthand for --bits x86
  --no-poll                 reverse only: pass --no-poll to Windows
                            receiver (blocking read() instead of WSAPoll;
                            needed on viosock builds lacking WSAPoll on
                            accept()ed sockets). Also settable via env
                            PERF_NO_POLL=1.
  --as-system               reverse only: launch Windows receiver via
                            schtasks /ru SYSTEM instead of bg-ssh
  --local-bin <path>        Linux vsock_perf path                      [$LOCAL_BIN]
EOF
    exit 1
}

while [ $# -gt 0 ]; do
    case "$1" in
        --direction)   DIRECTION="$2"; shift 2 ;;
        --bytes)       BYTES="$2";     shift 2 ;;
        --buf-size)    BUF="$2";       shift 2 ;;
        --vsk-size)    VSK_SIZE="$2";  shift 2 ;;
        --rcvlowat)    RCVLOWAT="$2";  shift 2 ;;
        --port)        PORT="$2";      shift 2 ;;
        --grace)       GRACE="$2";     shift 2 ;;
        --config)      CFG="$2";       shift 2 ;;
        --variant)     VARIANT="$2";   shift 2 ;;
        --bits)        BITS="$2";      shift 2 ;;
        --x86)         BITS="x86";     shift ;;
        --no-poll)     NO_POLL=1;      shift ;;
        --as-system)   AS_SYSTEM=1;    shift ;;
        --local-bin)   LOCAL_BIN="$2"; shift 2 ;;
        -h|--help)     usage ;;
        *) die "unknown arg: $1" ;;
    esac
done

case "$DIRECTION" in forward|reverse) ;; *) usage ;; esac
case "$BITS"      in x64|x86) ;;             *) die "--bits must be x64 or x86" ;; esac
case "$VARIANT"   in posix|wsa|overlapped) ;; *) die "--variant must be posix|wsa|overlapped" ;; esac

CFG=$(discover_config "$CFG")
guest_load "$CFG"
host_cid=$(config_read  "$CFG" host_cid);  [ -n "$host_cid"  ] || die "config has no host_cid="
guest_cid=$(config_read "$CFG" guest_cid); [ -n "$guest_cid" ] || die "config has no guest_cid="
[ -x "$LOCAL_BIN" ] || die "no vsock_perf at $LOCAL_BIN — run prepare-perf.sh first"

# Windows-side command
exe='vsock_perf.exe'
[ "$BITS" = x86 ] && exe='vsock_perf_x86.exe'
guest_flags=''
case "$VARIANT" in
    wsa)        guest_flags=' --variant wsa' ;;
    overlapped) guest_flags=' --variant overlapped' ;;
esac
GUEST_CMD="${guest_bin_dir}\\${exe}${guest_flags}"

# Shared receiver / sender argument tails
recv_args=(--port "$PORT" --buf-size "$BUF")
send_args=(--port "$PORT" --bytes "$BYTES" --buf-size "$BUF")
[ -n "$VSK_SIZE" ] && recv_args+=(--vsk-size "$VSK_SIZE") && send_args+=(--vsk-size "$VSK_SIZE")
[ -n "$RCVLOWAT" ] && recv_args+=(--rcvlowat "$RCVLOWAT")
# --no-poll is Windows-only; upstream Linux vsock_perf doesn't know it.
# So only extend recv_args in reverse (Windows receiver).
[ -n "$NO_POLL" ] && [ "$DIRECTION" = reverse ] && recv_args+=(--no-poll)

LOGDIR="/tmp/vsock-perf-one-$$"; mkdir -p "$LOGDIR"
rx_log="$LOGDIR/rx.log"
tx_log="$LOGDIR/tx.log"

info "== perf-one: $DIRECTION  bytes=$BYTES  buf=$BUF${VSK_SIZE:+  vsk=$VSK_SIZE}${RCVLOWAT:+  rcvlowat=$RCVLOWAT}${NO_POLL:+  no-poll}  variant=$VARIANT  bits=$BITS =="

if [ "$DIRECTION" = forward ]; then
    "$LOCAL_BIN" "${recv_args[@]}" > "$rx_log" 2>&1 &
    rx_pid=$!
    sleep "$GRACE"
    ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
        "$GUEST_CMD --sender $host_cid $(printf '%s ' "${send_args[@]}")" > "$tx_log" 2>&1
    tx_rc=$?
    wait "$rx_pid" 2>/dev/null; rx_rc=$?
    tr -d '\r' < "$tx_log" > "$tx_log.tmp" && mv "$tx_log.tmp" "$tx_log"
else
    if [ "$AS_SYSTEM" -eq 1 ]; then
        _guest_ssh "(echo @echo off & echo $GUEST_CMD $(printf '%s ' "${recv_args[@]}") ^> $GUEST_SRV_LOG 2^>^&1) > $GUEST_SRV_BAT"
        _guest_ssh "schtasks /create /tn $SCHTASKS_NAME /tr $GUEST_SRV_BAT /sc once /st 00:00 /ru SYSTEM /f" >/dev/null 2>&1
        _guest_ssh "schtasks /run /tn $SCHTASKS_NAME" >/dev/null 2>&1
        rx_ssh_pid=""
    else
        ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
            "$GUEST_CMD $(printf '%s ' "${recv_args[@]}")" > "$rx_log" 2>&1 &
        rx_ssh_pid=$!
    fi
    sleep "$GRACE"
    "$LOCAL_BIN" --sender "$guest_cid" "${send_args[@]}" > "$tx_log" 2>&1
    tx_rc=$?
    if [ -n "${rx_ssh_pid:-}" ]; then
        wait "$rx_ssh_pid" 2>/dev/null; rx_rc=$?
    else
        _guest_scp_from "$GUEST_SRV_LOG" "$rx_log" 2>/dev/null || true
        _guest_ssh "del $GUEST_SRV_LOG 2>nul & schtasks /end /tn $SCHTASKS_NAME /f 2>nul & schtasks /delete /tn $SCHTASKS_NAME /f 2>nul & exit 0" >/dev/null 2>&1
        rx_rc=0
    fi
    [ -f "$rx_log" ] && { tr -d '\r' < "$rx_log" > "$rx_log.tmp" && mv "$rx_log.tmp" "$rx_log"; }
fi

echo
info "----- receiver ($rx_log) -----"
cat "$rx_log"
echo
info "----- sender ($tx_log) -----"
cat "$tx_log"
echo
rx_gbps=$(grep -oE 'rx performance: [0-9.]+' "$rx_log" 2>/dev/null | tail -1 | awk '{print $NF}')
tx_gbps=$(grep -oE 'tx performance: [0-9.]+' "$tx_log" 2>/dev/null | tail -1 | awk '{print $NF}')
info "RX=${rx_gbps:-'-'} Gbps  TX=${tx_gbps:-'-'} Gbps  rx-rc=$rx_rc  tx-rc=$tx_rc"
