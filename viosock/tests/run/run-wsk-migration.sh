#!/usr/bin/env bash
# run-wsk-migration.sh — viosock driver stability across a save/restore while
# the kernel-mode WSK path (viosockwsk-perf.sys on the guest) has transfers in
# flight: the kernel counterpart of run-migration.sh.
#
# Load: N kernel sender threads (guest) -> N Linux vsock_perf receivers on
# FWD_PORT_BASE+i, each pushing MIGR_BYTES_MB so the `virsh save` hits them
# mid-transfer.  The restore replays VIRTIO_VSOCK_EVENT_TRANSPORT_RESET (the
# destination side of a live migration): the senders' pending WskSend must
# fail, the threads must return and the service must stop cleanly - a thread
# stuck in the reset is what this run is after.  By default the restore also
# assigns the guest a different CID, as run-migration.sh does (see _migr.sh).
#
# Pass criterion (stability only):
#   * every load connection is established when the save hits,
#   * save and restore both succeed and the guest is reachable again,
#   * the VirtioSocket service is Running,
#   * the load service stops within MIGR_STOP_TIMEOUT (its unload joins the threads),
#   * a fresh kernel sender -> Linux receiver transfer completes,
#   * a fresh Linux sender -> kernel receiver transfer on the new CID completes.
#
# Must run on the libvirt host (needs virsh + the local Linux vsock_perf).
set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"
# shellcheck source=./_wsk.sh
. "$_here/_wsk.sh"
# shellcheck source=./_migr.sh
. "$_here/_migr.sh"

# --- tunables (env-overridable) ------------------------------------------
MIGR_N=${MIGR_N:-8}                       # kernel sender threads — the load knob
MIGR_BYTES_MB=${MIGR_BYTES_MB:-16384}     # MB per connection (large enough to still be in flight at save time)
MIGR_BUF=${MIGR_BUF:-65536}
MIGR_RAMP_SECS=${MIGR_RAMP_SECS:-3}       # extra transfer time once every load connection is up, before the save
MIGR_ESTAB_TIMEOUT=${MIGR_ESTAB_TIMEOUT:-60}
MIGR_LOAD_TIMEOUT=${MIGR_LOAD_TIMEOUT:-600}   # cap on the Linux receivers of the load
MIGR_STOP_TIMEOUT=${MIGR_STOP_TIMEOUT:-120}   # cap on stopping the load service after the restore
MIGR_PROBE_PORT=${MIGR_PROBE_PORT:-40000}
MIGR_PROBE_PORT2=${MIGR_PROBE_PORT2:-40001}
MIGR_PROBE_TIMEOUT=${MIGR_PROBE_TIMEOUT:-90}  # cap per post-restore probe
FWD_PORT_BASE=${FWD_PORT_BASE:-20000}
SERVER_GRACE_SECS=${SERVER_GRACE_SECS:-3}
LOCAL_BIN=${LOCAL_BIN:-/opt/vsock-test/vsock_perf}

# --- args ----------------------------------------------------------------
CFG=""; LOGDIR=""
while [ $# -gt 0 ]; do
    case "$1" in
        --config)       CFG="$2";             shift 2 ;;
        --logdir)       LOGDIR="$2";          shift 2 ;;
        --connections)  MIGR_N="$2";          shift 2 ;;
        --bytes-mb)     MIGR_BYTES_MB="$2";   shift 2 ;;
        --ramp)         MIGR_RAMP_SECS="$2";  shift 2 ;;
        --local-bin)    LOCAL_BIN="$2";       shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 [--config <cfg>] [--logdir <dir>] [--connections N]
          [--bytes-mb <mb>] [--ramp <secs>] [--local-bin <path>]

Driver stability across a virsh save/restore under N kernel-mode (WSK) vsock
connections of load (service $WSK_SERVICE on the guest, see install-wsk.sh).

Tunables (env-overridable, defaults in [brackets]):
  MIGR_N             [8]     Kernel sender connections for the load.
  MIGR_BYTES_MB      [16384] MB per connection for the load.
  MIGR_RAMP_SECS     [3]     Seconds of transfer after every load connection is up, before the save.
  MIGR_ESTAB_TIMEOUT [60]    Cap on waiting for every load connection to come up.
  MIGR_STOP_TIMEOUT  [120]   Cap on stopping the load service after the restore.
  MIGR_CHANGE_CID    [1]     Restore with a different guest CID (cross-host live-migration shape).
  MIGR_NEW_CID       []      Explicit new CID; empty = guest_cid+1 (skipping 0..3).
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

[ "$MIGR_N" -gt 0 ] 2>/dev/null || die "--connections must be a positive integer"

CFG=$(discover_config "$CFG") || exit $?
guest_load "$CFG"
guest_cid=$(config_read  "$CFG" guest_cid);  [ -n "$guest_cid"  ] || die "config has no guest_cid="
host_cid=$(config_read   "$CFG" host_cid);   [ -n "$host_cid"   ] || die "config has no host_cid="
guest_name=$(config_read "$CFG" guest_name); [ -n "$guest_name" ] || die "config has no guest_name= (libvirt domain)"

command -v virsh >/dev/null 2>&1 || die "virsh not found — run-wsk-migration.sh must run on the libvirt host"
[ -x "$LOCAL_BIN" ] || die "no vsock_perf at $LOCAL_BIN — run prepare-perf.sh first (or pass --local-bin)"

if [ -z "$LOGDIR" ]; then
    LOGDIR=$(mktemp -d /tmp/wsk-migration-XXXXXX) || die "mktemp failed"
else
    mkdir -p "$LOGDIR"
fi

info "== wsk migration stability: N=$MIGR_N, ${MIGR_BYTES_MB} MB/conn, domain=$guest_name, ramp=${MIGR_RAMP_SECS}s =="
info "   logs in $LOGDIR"
case "$(wsk_state)" in
    1*|*STOPPED*) ;;
    "") die "service $WSK_SERVICE not found on the guest - run install-wsk.sh first" ;;
    *) warn "  $WSK_SERVICE is not stopped ($(wsk_state)) - stopping it first"; wsk_stop || die "cannot stop $WSK_SERVICE" ;;
esac

# --- phase 1: the load ---------------------------------------------------
pids=()
for i in $(seq 0 $((MIGR_N - 1))); do
    timeout "$MIGR_LOAD_TIMEOUT" "$LOCAL_BIN" --port $((FWD_PORT_BASE + i)) --buf-size "$MIGR_BUF" \
        > "$LOGDIR/load_${i}_rx.log" 2>&1 &
    pids+=($!)
done
wsk_set_params 1 "$host_cid" "$FWD_PORT_BASE" "$MIGR_BYTES_MB" "$MIGR_BUF" "$MIGR_N" \
    || die "cannot write $WSK_SERVICE parameters on the guest"
wsk_start || die "sc start $WSK_SERVICE failed"
info "  load started: $MIGR_N kernel senders -> linux receivers on $FWD_PORT_BASE+i"

# The save must hit the whole load in flight: N established vsock sockets on
# the host peered with the guest.
load_wire_up() {
    ss -A vsock -H state established 2>/dev/null | awk -v c="$guest_cid" '$NF ~ "^"c":" { n++ } END { print n + 0 }'
}
estab_deadline=$(( $(date +%s) + MIGR_ESTAB_TIMEOUT ))
while [ "$(date +%s)" -lt "$estab_deadline" ]; do
    wire_up=$(load_wire_up)
    [ "$wire_up" -eq "$MIGR_N" ] && break
    sleep 1
done
info "  load up: wire $wire_up/$MIGR_N; ramping ${MIGR_RAMP_SECS}s"
sleep "$MIGR_RAMP_SECS"

# --- phase 2: save/restore mid-transfer (transport reset) ----------------
migr_plan_cid "$guest_name" "$guest_cid" "$LOGDIR"
wire_up=$(load_wire_up)
inflight_ok=0; [ "$wire_up" -eq "$MIGR_N" ] && inflight_ok=1
info "  in flight at save: wire $wire_up/$MIGR_N"
migr_save_restore "$guest_name" "$guest_cid" "$LOGDIR"
# From now on the guest lives at the (possibly new) CID; the probes target it.
guest_cid="$new_cid"

# The Linux receivers' connections are gone with the reset; reap them.
kill "${pids[@]}" 2>/dev/null || true
for p in "${pids[@]}"; do wait "$p" 2>/dev/null || true; done

# --- phase 3: stability assertions ---------------------------------------
migr_wait_guest
migr_driver_state

# 3c. the load service stops: the reset failed its sends and the unload joins the threads.
stop_ok=0; load_ok=0; load_failed=0; load_status=0x0
if [ "$guest_up" -eq 1 ]; then
    if wsk_stop "$MIGR_STOP_TIMEOUT"; then stop_ok=1; else err "  $WSK_SERVICE did not stop within ${MIGR_STOP_TIMEOUT}s"; fi
    read -r load_ok load_failed _ _ _ _ _ load_status <<< "$(wsk_results)"
    info "  load after restore: stop $([ "$stop_ok" -eq 1 ] && echo ok || echo FAIL), kernel ok=$load_ok failed=$load_failed (first error $load_status)"
fi

# wsk_probe <mode> <port> <sets ok var> — one 8 MB kernel transfer; mode 1 = kernel
# sender -> local receiver, mode 0 = local sender -> kernel receiver on the new CID.
wsk_probe() {
    local mode="$1" port="$2" var="$3" ok=0 p_ok p_failed p_status peer_pid="" i
    if [ "$mode" = 1 ]; then
        "$LOCAL_BIN" --port "$port" --buf-size 64K > "$LOGDIR/probe_${port}_rx.log" 2>&1 &
        peer_pid=$!
        sleep 1
    fi
    if wsk_set_params "$mode" "$host_cid" "$port" 8 65536 1 && wsk_start; then
        if [ "$mode" = 0 ]; then
            sleep "$SERVER_GRACE_SECS"
            timeout "$MIGR_PROBE_TIMEOUT" "$LOCAL_BIN" --sender "$guest_cid" --port "$port" --bytes 8M --buf-size 64K \
                > "$LOGDIR/probe_${port}_tx.log" 2>&1 || true
        fi
        if wsk_wait_done "$MIGR_PROBE_TIMEOUT"; then
            read -r p_ok p_failed _ _ _ _ _ p_status <<< "$(wsk_results)"
            [ "$p_ok" -eq 1 ] && ok=1
            [ "$p_failed" -eq 0 ] || warn "  probe port $port: kernel side failed with $p_status"
        else
            warn "  probe port $port: no Done within ${MIGR_PROBE_TIMEOUT}s"
        fi
        wsk_stop || { warn "  probe port $port: $WSK_SERVICE did not stop"; ok=0; }
    else
        warn "  probe port $port: cannot start $WSK_SERVICE"
    fi
    if [ -n "$peer_pid" ]; then
        # A receiver whose sender never connected is parked in accept(); bound the wait.
        for i in $(seq 1 $((MIGR_PROBE_TIMEOUT * 2))); do kill -0 "$peer_pid" 2>/dev/null || break; sleep 0.5; done
        kill "$peer_pid" 2>/dev/null || true
        wait "$peer_pid" 2>/dev/null || true
    fi
    eval "$var=$ok"
}

# 3d/3e. fresh kernel transfers both ways; the reverse one proves the guest
# driver adopted the CID we asked for on restore.
probe_ok=0; probe2_ok=0
if [ "$guest_up" -eq 1 ] && [ "$stop_ok" -eq 1 ]; then
    wsk_probe 1 "$MIGR_PROBE_PORT"  probe_ok
    wsk_probe 0 "$MIGR_PROBE_PORT2" probe2_ok
fi

# --- report --------------------------------------------------------------
echo
info "=== wsk migration stability summary ==="
printf '  connections     : %d\n' "$MIGR_N"
printf '  CID             : %s\n' \
    "$([ "$new_cid" = "$(config_read "$CFG" guest_cid)" ] && echo "$guest_cid (unchanged)" || echo "$(config_read "$CFG" guest_cid) -> $new_cid")"
printf '  in flight @save : wire %s/%d%s\n' "$wire_up" "$MIGR_N" \
    "$([ "$inflight_ok" -eq 1 ] || echo '  — FAIL: raise MIGR_BYTES_MB / MIGR_ESTAB_TIMEOUT')"
printf '  save / restore  : rc %d / %d\n' "$save_rc" "$restore_rc"
printf '  guest reachable : %s\n' "$([ "$guest_up" -eq 1 ] && echo yes || echo NO)"
printf '  driver state    : %s\n' "$srv_state"
printf '  load stop       : %s (kernel ok=%d failed=%d, first error %s)\n' \
    "$([ "$stop_ok" -eq 1 ] && echo ok || echo FAIL)" "$load_ok" "$load_failed" "$load_status"
printf '  kernel -> host  : %s\n' "$([ "$probe_ok"  -eq 1 ] && echo ok || echo FAIL)"
printf '  host  -> kernel : %s\n' "$([ "$probe2_ok" -eq 1 ] && echo ok || echo FAIL)"

if [ "$inflight_ok" -eq 1 ] && [ "$save_rc" -eq 0 ] && [ "$restore_rc" -eq 0 ] \
   && [ "$guest_up" -eq 1 ] && [ "$driver_ok" -eq 1 ] && [ "$stop_ok" -eq 1 ] \
   && [ "$probe_ok" -eq 1 ] && [ "$probe2_ok" -eq 1 ]; then
    info "=== PASS ==="
    exit 0
else
    err "=== FAIL (inflight=$inflight_ok save=$save_rc restore=$restore_rc up=$guest_up driver=$srv_state stop=$stop_ok probe=$probe_ok probe2=$probe2_ok) ==="
    exit 1
fi
