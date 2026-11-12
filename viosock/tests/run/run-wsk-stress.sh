#!/usr/bin/env bash
# run-wsk-stress.sh — multi-connection vsock stress through the kernel-mode
# WSK library (viosockwsk-perf.sys on the guest), the kernel counterpart of
# run-stress.sh.
#
# One service instance drives N connections of one role at a time, so the
# directions run back to back:
#   forward : N kernel sender threads (guest) -> N Linux vsock_perf receivers
#             on FWD_PORT_BASE+i;
#   reverse : N Linux vsock_perf senders -> N kernel receiver threads (guest)
#             on REV_PORT_BASE+i.
# The guest side is one kernel service: parameters go into its registry key,
# `sc start` kicks the transfers off, the Results key reports when all
# threads are done (see _wsk.sh).
#
# Pass criterion per direction: the driver reports Ok=N Failed=0, every
# Linux vsock_perf exited 0, the service stops cleanly (its unload joins
# the threads), and the VirtioSocket service is still Running.
#
# Must run on the libvirt host (needs the local Linux vsock_perf).
set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"
# shellcheck source=./_wsk.sh
. "$_here/_wsk.sh"

# --- tunables (env-overridable) ------------------------------------------
WSK_N=${WSK_N:-32}
WSK_BYTES_MB=${WSK_BYTES_MB:-1024}          # per connection
WSK_BUF=${WSK_BUF:-65536}                   # bytes, both sides
WSK_TIMEOUT_SECS=${WSK_TIMEOUT_SECS:-900}   # cap on a direction (driver Done + Linux children)
FWD_PORT_BASE=${FWD_PORT_BASE:-20000}
REV_PORT_BASE=${REV_PORT_BASE:-30000}
SERVER_GRACE_SECS=${SERVER_GRACE_SECS:-3}

# --- args ----------------------------------------------------------------
CFG=""; LOGDIR=""; DIRS=""
LOCAL_BIN="/opt/vsock-test/vsock_perf"

while [ $# -gt 0 ]; do
    case "$1" in
        --config)       CFG="$2";           shift 2 ;;
        --logdir)       LOGDIR="$2";        shift 2 ;;
        --connections)  WSK_N="$2";         shift 2 ;;
        --bytes-mb)     WSK_BYTES_MB="$2";  shift 2 ;;
        --buf-size)     WSK_BUF="$2";       shift 2 ;;
        --timeout)      WSK_TIMEOUT_SECS="$2"; shift 2 ;;
        --only)         DIRS="$2";          shift 2 ;;
        --local-bin)    LOCAL_BIN="$2";     shift 2 ;;
        -h|--help)
            cat >&2 <<USAGE
Usage: $0 [--config <cfg>] [--logdir <dir>] [--connections <n>]
          [--bytes-mb <mb>] [--buf-size <bytes>] [--timeout <secs>]
          [--only forward,reverse] [--local-bin <path>]

N connections per direction through the kernel WSK perf driver (service
$WSK_SERVICE on the guest), forward then reverse.  Ports FWD_PORT_BASE+i /
REV_PORT_BASE+i.  Logs land in --logdir (default: /tmp/wsk-stress-\$\$).

Env: WSK_N [$WSK_N]  WSK_BYTES_MB [$WSK_BYTES_MB]  WSK_BUF [$WSK_BUF]
     WSK_TIMEOUT_SECS [$WSK_TIMEOUT_SECS]  WSK_SERVICE [$WSK_SERVICE]
     FWD_PORT_BASE [$FWD_PORT_BASE]  REV_PORT_BASE [$REV_PORT_BASE]
USAGE
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done
[ "$WSK_N" -gt 0 ] 2>/dev/null || die "--connections must be a positive integer"

CFG=$(discover_config "$CFG") || exit $?
guest_load "$CFG"
guest_cid=$(config_read "$CFG" guest_cid); [ -n "$guest_cid" ] || die "config has no guest_cid="
host_cid=$(config_read  "$CFG" host_cid);  [ -n "$host_cid"  ] || die "config has no host_cid="
[ -x "$LOCAL_BIN" ] || die "no vsock_perf at $LOCAL_BIN - run prepare-perf.sh first"

run_fwd=1; run_rev=1
if [ -n "$DIRS" ]; then
    run_fwd=0; run_rev=0
    IFS=',' read -ra parts <<< "$DIRS"
    for p in "${parts[@]}"; do
        case "$p" in
            forward) run_fwd=1 ;;
            reverse) run_rev=1 ;;
            *) die "unknown --only direction: $p" ;;
        esac
    done
fi

if [ -z "$LOGDIR" ]; then
    LOGDIR=$(mktemp -d /tmp/wsk-stress-XXXXXX) || die "mktemp failed"
else
    mkdir -p "$LOGDIR"
fi

info "== wsk stress: N=$WSK_N per direction, ${WSK_BYTES_MB} MB/conn, buf=$WSK_BUF, service=$WSK_SERVICE =="
case "$(wsk_state)" in
    1*|*STOPPED*) ;;
    "") die "service $WSK_SERVICE not found on the guest - run install-wsk.sh first" ;;
    *) warn "  $WSK_SERVICE is not stopped ($(wsk_state)) - stopping it first"; wsk_stop || die "cannot stop $WSK_SERVICE" ;;
esac

# --- one direction -------------------------------------------------------
# run_direction <fwd|rev>: sets <dir>_ok <dir>_failed <dir>_mbit <dir>_linux_fail <dir>_stop_ok <dir>_done
run_direction() {
    local dir="$1" mode port_base peer i t0 t1
    local -a pids=()
    if [ "$dir" = fwd ]; then mode=1; port_base=$FWD_PORT_BASE; else mode=0; port_base=$REV_PORT_BASE; fi
    peer=$host_cid

    local ok=0 failed=0 bytes=0 elapsed=0 mbit=0 stalls=0 conns=0 status=0x0 linux_fail=0 stop_ok=0 done_ok=0
    t0=$(date +%s)

    if [ "$dir" = fwd ]; then
        # Linux receivers first, so the kernel senders find their peers.
        for i in $(seq 0 $((WSK_N - 1))); do
            timeout "$WSK_TIMEOUT_SECS" "$LOCAL_BIN" --port $((port_base + i)) --buf-size "$WSK_BUF" \
                > "$LOGDIR/fwd_${i}_rx.log" 2>&1 &
            pids+=($!)
        done
        info "  forward: $WSK_N linux receivers spawned on $port_base+i"
    fi

    wsk_set_params "$mode" "$peer" "$port_base" "$WSK_BYTES_MB" "$WSK_BUF" "$WSK_N" \
        || die "  cannot write $WSK_SERVICE parameters on the guest"
    if ! wsk_start; then
        err "  sc start $WSK_SERVICE failed"
        [ "${#pids[@]}" -gt 0 ] && kill "${pids[@]}" 2>/dev/null
        eval "${dir}_ok=0 ${dir}_failed=$WSK_N ${dir}_mbit=0 ${dir}_linux_fail=$WSK_N ${dir}_stop_ok=1 ${dir}_done=0"
        return 1
    fi
    info "  $dir: kernel $( [ "$mode" = 1 ] && echo senders || echo receivers ) started (N=$WSK_N, port $port_base+i)"

    if [ "$dir" = rev ]; then
        sleep "$SERVER_GRACE_SECS"
        for i in $(seq 0 $((WSK_N - 1))); do
            timeout "$WSK_TIMEOUT_SECS" "$LOCAL_BIN" --sender "$guest_cid" --port $((port_base + i)) \
                --bytes "${WSK_BYTES_MB}M" --buf-size "$WSK_BUF" > "$LOGDIR/rev_${i}_tx.log" 2>&1 &
            pids+=($!)
        done
        info "  reverse: $WSK_N linux senders spawned to $guest_cid:$port_base+i"
    fi

    if wsk_wait_done "$WSK_TIMEOUT_SECS"; then
        done_ok=1
        read -r ok failed bytes elapsed mbit stalls conns status <<< "$(wsk_results)"
    else
        err "  $dir: driver did not report Done within ${WSK_TIMEOUT_SECS}s"
    fi

    # The unload joins the threads; a thread that never returns is a finding.
    if wsk_stop; then stop_ok=1; else err "  $dir: $WSK_SERVICE did not stop"; fi

    for i in "${pids[@]}"; do
        wait "$i" || linux_fail=$((linux_fail + 1))
    done
    t1=$(date +%s)
    local first_err=""; [ "$failed" -gt 0 ] && first_err=" (first error $status)"
    info "  $dir: done in $((t1 - t0)) s - kernel ok=$ok failed=$failed$first_err ($(wsk_mbit_to_gbps "$mbit") Gbps aggregate, $stalls stalls), linux failures $linux_fail/$WSK_N"
    eval "${dir}_ok=$ok ${dir}_failed=$failed ${dir}_mbit=$mbit ${dir}_linux_fail=$linux_fail ${dir}_stop_ok=$stop_ok ${dir}_done=$done_ok"
}

fwd_ok=0; fwd_failed=0; fwd_mbit=0; fwd_linux_fail=0; fwd_stop_ok=1; fwd_done=1
rev_ok=0; rev_failed=0; rev_mbit=0; rev_linux_fail=0; rev_stop_ok=1; rev_done=1
[ "$run_fwd" -eq 1 ] && run_direction fwd
[ "$run_rev" -eq 1 ] && run_direction rev

srv_state=$(_guest_ssh 'sc query VirtioSocket' 2>/dev/null \
    | tr -d '\r' | awk -F: '/STATE/ {gsub(/^[ \t]+|[ \t]+$/, "", $2); print $2; exit}')
[ -n "$srv_state" ] || srv_state='<unknown>'

echo
info "=== wsk stress summary ==="
printf '  connections/dir : %d\n' "$WSK_N"
printf '  MB/conn         : %d\n' "$WSK_BYTES_MB"
printf '  buf-size        : %d\n' "$WSK_BUF"
[ "$run_fwd" -eq 1 ] && printf '  forward (kernel tx): ok=%d failed=%d  %s Gbps  linux-rx fails %d  stop %s\n' \
    "$fwd_ok" "$fwd_failed" "$(wsk_mbit_to_gbps "$fwd_mbit")" "$fwd_linux_fail" "$([ "$fwd_stop_ok" -eq 1 ] && echo ok || echo FAIL)"
[ "$run_rev" -eq 1 ] && printf '  reverse (kernel rx): ok=%d failed=%d  %s Gbps  linux-tx fails %d  stop %s\n' \
    "$rev_ok" "$rev_failed" "$(wsk_mbit_to_gbps "$rev_mbit")" "$rev_linux_fail" "$([ "$rev_stop_ok" -eq 1 ] && echo ok || echo FAIL)"
printf '  driver state    : %s\n' "$srv_state"

driver_ok=0
case "$srv_state" in *RUNNING*|4*) driver_ok=1 ;; esac
dir_ok() {  # dir_ok <dir>
    local d="$1" ok failed lf st dn
    eval "ok=\$${d}_ok failed=\$${d}_failed lf=\$${d}_linux_fail st=\$${d}_stop_ok dn=\$${d}_done"
    [ "$dn" -eq 1 ] && [ "$ok" -eq "$WSK_N" ] && [ "$failed" -eq 0 ] && [ "$lf" -eq 0 ] && [ "$st" -eq 1 ]
}
fwd_pass=1; rev_pass=1
[ "$run_fwd" -eq 1 ] && { dir_ok fwd || fwd_pass=0; }
[ "$run_rev" -eq 1 ] && { dir_ok rev || rev_pass=0; }
if [ "$fwd_pass" -eq 1 ] && [ "$rev_pass" -eq 1 ] && [ "$driver_ok" -eq 1 ]; then
    info "=== PASS ==="
    exit 0
fi
err "=== FAIL (forward=$fwd_pass reverse=$rev_pass driver=$srv_state) ==="
exit 1
