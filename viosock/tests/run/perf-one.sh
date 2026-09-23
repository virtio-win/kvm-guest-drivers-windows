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
CFG=""; VARIANT="posix"
NO_POLL=${PERF_NO_POLL:-}
ZEROCOPY=${PERF_ZEROCOPY:-}
LOCAL_BIN="/opt/vsock-test/vsock_perf"

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
  --no-poll                 reverse only: pass --no-poll to Windows
                            receiver (blocking read() instead of WSAPoll;
                            needed on viosock builds lacking WSAPoll on
                            accept()ed sockets). Also settable via env
                            PERF_NO_POLL=1.
  --zerocopy                pass --zerocopy (MSG_ZEROCOPY on every
                            send()) to whichever side is the sender:
                            Windows in forward, Linux in reverse. Also
                            settable via env PERF_ZEROCOPY=1.
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
        --no-poll)     NO_POLL=1;      shift ;;
        --zerocopy)    ZEROCOPY=1;     shift ;;
        --local-bin)   LOCAL_BIN="$2"; shift 2 ;;
        -h|--help)     usage ;;
        *) die "unknown arg: $1" ;;
    esac
done

case "$DIRECTION" in forward|reverse) ;; *) usage ;; esac
case "$VARIANT"   in posix|wsa|overlapped) ;; *) die "--variant must be posix|wsa|overlapped" ;; esac

CFG=$(discover_config "$CFG") || exit $?
guest_load "$CFG"
host_cid=$(config_read  "$CFG" host_cid);  [ -n "$host_cid"  ] || die "config has no host_cid="
guest_cid=$(config_read "$CFG" guest_cid); [ -n "$guest_cid" ] || die "config has no guest_cid="
[ -x "$LOCAL_BIN" ] || die "no vsock_perf at $LOCAL_BIN — run prepare-perf.sh first"

# Windows-side command
guest_flags=''
case "$VARIANT" in
    wsa)        guest_flags=' --variant wsa' ;;
    overlapped) guest_flags=' --variant overlapped' ;;
esac
GUEST_CMD="${guest_bin_dir}\\vsock_perf.exe${guest_flags}"

# Shared receiver / sender argument tails
recv_args=(--port "$PORT" --buf-size "$BUF")
send_args=(--port "$PORT" --bytes "$BYTES" --buf-size "$BUF")
[ -n "$VSK_SIZE" ] && recv_args+=(--vsk-size "$VSK_SIZE") && send_args+=(--vsk-size "$VSK_SIZE")
[ -n "$RCVLOWAT" ] && recv_args+=(--rcvlowat "$RCVLOWAT")
# --no-poll is Windows-only; upstream Linux vsock_perf doesn't know it.
# So only extend recv_args in reverse (Windows receiver).
[ -n "$NO_POLL" ] && [ "$DIRECTION" = reverse ] && recv_args+=(--no-poll)
# --zerocopy is sender-side (pin user pages, skip the copy into the
# transport buffer).  The receiver is unaware - vhost delivers the
# same packets into the guest queue either way - so it wires the
# same into send_args regardless of direction.
[ -n "$ZEROCOPY" ] && send_args+=(--zerocopy)

LOGDIR="/tmp/vsock-perf-one-$$"; mkdir -p "$LOGDIR"
rx_log="$LOGDIR/rx.log"
tx_log="$LOGDIR/tx.log"

info "== perf-one: $DIRECTION  bytes=$BYTES  buf=$BUF${VSK_SIZE:+  vsk=$VSK_SIZE}${RCVLOWAT:+  rcvlowat=$RCVLOWAT}${NO_POLL:+  no-poll}${ZEROCOPY:+  zerocopy}  variant=$VARIANT =="

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
    ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
        "$GUEST_CMD $(printf '%s ' "${recv_args[@]}")" > "$rx_log" 2>&1 &
    rx_ssh_pid=$!
    sleep "$GRACE"
    "$LOCAL_BIN" --sender "$guest_cid" "${send_args[@]}" > "$tx_log" 2>&1
    tx_rc=$?
    wait "$rx_ssh_pid" 2>/dev/null; rx_rc=$?
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
