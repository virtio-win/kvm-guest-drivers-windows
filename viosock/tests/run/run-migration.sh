#!/usr/bin/env bash
# run-migration.sh — viosock driver stability across a save/restore (the
# guest-visible part of a live migration) under vsock load.
#
# Starts N vsock connections per direction (via run-stress.sh as a load
# generator), injects a `virsh save` + `virsh restore` of the guest mid-transfer
# — QEMU replays this as a VIRTIO_VSOCK_EVENT_TRANSPORT_RESET, exactly like the
# destination side of a live migration — and then checks the driver is still
# healthy: the service is Running and a fresh vsock connection still completes.
#
# The load's wire transfers are expected to be reset by the save/restore and are
# NOT part of the pass criterion; they are only there to exercise the reset path
# with real in-flight traffic (parked recv/poll, per-socket teardown). The load's
# loopback transfers (VMADDR_CID_LOCAL) never touch the transport and must all
# complete across the reset.
#
# The connection count is the tunable load knob — MIGR_N env or --connections.
# A full high-count run may still expose issues, so the default is modest;
# raise it to stress the reset harder.
#
# Pass criterion (stability only — no address/CID assertions):
#   * every load connection is still transferring when the save hits,
#   * save and restore both succeed,
#   * the guest is reachable again after restore,
#   * the VirtioSocket service is Running,
#   * a fresh post-restore vsock_perf transfer completes,
#   * every loopback transfer of the load completes.
#
# Must run on the libvirt host (needs virsh + the local Linux vsock_perf).

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"
# shellcheck source=./_migr.sh
. "$_here/_migr.sh"

# --- tunables (env-overridable) ------------------------------------------
MIGR_N=${MIGR_N:-8}                   # connections per direction — the load knob
MIGR_BYTES=${MIGR_BYTES:-16G}         # bytes/conn (large enough to still be in flight at save time)
MIGR_RAMP_SECS=${MIGR_RAMP_SECS:-3}   # extra transfer time once every load connection is up, before the save
MIGR_ESTAB_TIMEOUT=${MIGR_ESTAB_TIMEOUT:-60}  # cap on waiting for every load connection to come up
MIGR_PROBE_PORT=${MIGR_PROBE_PORT:-40000}
MIGR_PROBE_PORT2=${MIGR_PROBE_PORT2:-40001}
MIGR_PROBE_TIMEOUT=${MIGR_PROBE_TIMEOUT:-90}  # cap per post-restore probe; a receiver whose sender never connected must not hang the run
MIGR_LOAD_TIMEOUT=${MIGR_LOAD_TIMEOUT:-600}   # cap on the load's own transfers; a stuck loopback transfer becomes a FAIL
MIGR_CHANGE_CID=${MIGR_CHANGE_CID:-1} # 0 = restore with unchanged CID
MIGR_NEW_CID=${MIGR_NEW_CID:-}        # explicit new CID; empty = guest_cid+1 (skipping 0..3)
MIGR_ADDRLIST=${MIGR_ADDRLIST:-1}     # 1 = monitor guest-side SIO_ADDRESS_LIST CID (soft, print-only)
LOCAL_BIN=${LOCAL_BIN:-/opt/vsock-test/vsock_perf}

# --- args ----------------------------------------------------------------
CFG=""; LOGDIR=""
while [ $# -gt 0 ]; do
    case "$1" in
        --config)       CFG="$2";             shift 2 ;;
        --logdir)       LOGDIR="$2";          shift 2 ;;
        --connections)  MIGR_N="$2";          shift 2 ;;
        --bytes)        MIGR_BYTES="$2";       shift 2 ;;
        --ramp)         MIGR_RAMP_SECS="$2";   shift 2 ;;
        --local-bin)    LOCAL_BIN="$2";        shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 [--config <cfg>] [--logdir <dir>] [--connections N]
          [--bytes <sz>] [--ramp <secs>] [--local-bin <path>]

Driver stability across a virsh save/restore under N vsock connections of load.
The connection count is the load knob (env MIGR_N or --connections); the default
is intentionally modest — raise it to stress the reset harder.

Tunables (env-overridable, defaults in [brackets]):
  MIGR_N           [8]    Connections per direction (3*N total: forward, reverse, loopback).
  MIGR_BYTES       [16G]  Bytes per connection for the load.
  MIGR_RAMP_SECS   [3]    Seconds of transfer after every load connection is up, before the save.
  MIGR_ESTAB_TIMEOUT [60] Cap on waiting for every load connection to come up.
  MIGR_LOAD_TIMEOUT [600] Cap on the load's transfers; loopback ones must finish within it.
  MIGR_CHANGE_CID  [1]    Restore with a different guest CID (cross-host live-migration shape).
  MIGR_NEW_CID     []     Explicit new CID; empty = guest_cid+1 (skipping 0..3).
  MIGR_ADDRLIST    [1]    Guest-side SIO_ADDRESS_LIST CID monitor (soft, print-only).
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

command -v virsh >/dev/null 2>&1 || die "virsh not found — run-migration.sh must run on the libvirt host"
[ -x "$LOCAL_BIN" ] || die "no vsock_perf at $LOCAL_BIN — run prepare-perf.sh first (or pass --local-bin)"

if [ -z "$LOGDIR" ]; then
    LOGDIR=$(mktemp -d /tmp/vsock-migration-XXXXXX) || die "mktemp failed"
else
    mkdir -p "$LOGDIR"
fi

info "== migration stability: N=$MIGR_N/dir, bytes=$MIGR_BYTES, domain=$guest_name, ramp=${MIGR_RAMP_SECS}s =="
info "   logs in $LOGDIR"

# Guest-side address-list monitor (soft): sample the CID now and arm
# a SYSTEM-session watcher (survives save/restore) that times the CID change.
# Armed before the load so its ssh round trips don't eat into the transfers.
addrlist_start=""; addrlist_exit=""; addrlist_log="$LOGDIR/addrlist.log"
if [ "$MIGR_ADDRLIST" -eq 1 ]; then
    addrlist_start=$(timeout 30 ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
        "${guest_bin_dir}\\vsock_perf.exe --addrlist-query" 2>/dev/null | tr -d '\r' | grep -oE 'CID = [0-9]+' | grep -oE '[0-9]+')
    _guest_ssh "(echo ${guest_bin_dir}\\vsock_perf.exe --addrlist-watch ^> C:\\addrlist-watch.log 2^>^&1) > C:\\addrlist-watch.bat" >/dev/null 2>&1 || true
    _guest_ssh "schtasks /create /tn viosock_addrlist /tr C:\\addrlist-watch.bat /sc once /st 00:00 /ru SYSTEM /f" >/dev/null 2>&1 || true
    _guest_ssh "schtasks /run /tn viosock_addrlist" >/dev/null 2>&1 || true
    info "  addrlist: start CID=${addrlist_start:-?}, watcher armed (SYSTEM)"
fi

# --- phase 1: start the load (run-stress.sh) in the background -----------
stress_log="$LOGDIR/stress.log"
STRESS_BYTES="$MIGR_BYTES" STRESS_TIMEOUT_SECS="$MIGR_LOAD_TIMEOUT" "$_here/run-stress.sh" \
    --config "$CFG" --connections "$MIGR_N" --logdir "$LOGDIR/stress" \
    > "$stress_log" 2>&1 &
stress_pid=$!
info "  load started via run-stress.sh (pid=$stress_pid); waiting for transfers to run..."

for _ in $(seq 1 90); do
    grep -q 'waiting for completion' "$stress_log" 2>/dev/null && break
    kill -0 "$stress_pid" 2>/dev/null || { warn "  run-stress exited early — see $stress_log"; break; }
    sleep 1
done

# The save must hit the whole load in flight. Wire connections show up on the host as
# established vsock sockets peered with the guest (forward + reverse = 2*N); every guest-side
# vsock_perf with --port (forward tx, reverse rx, loopback tx, loopback rx) exits once its
# transfer ends or its connect fails, so a live one is still transferring. In the default
# loopback server mode one receiver serves all N loopback connections; its log tells how many
# it accepted and how many already finished.
lb_mode=${STRESS_LB_MODE:-server}
lb_srv_log="${GUEST_STRESS_DIR:-C:\\stress}\\lb_srv_rx.log"
load_wire_up() {
    ss -A vsock -H state established 2>/dev/null | awk -v c="$guest_cid" '$NF ~ "^"c":" { n++ } END { print n + 0 }'
}
# prints "<guest vsock_perf procs> <lb accepted> <lb finished>"
load_guest_state() {
    _guest_ps "\$n = @(Get-CimInstance Win32_Process -Filter \"Name='vsock_perf.exe'\" | Where-Object { \$_.CommandLine -match '--port' }).Count
\$l = Get-Content '$lb_srv_log' -ErrorAction SilentlyContinue
\$a = @(\$l | Select-String -Pattern '^conn \d+: accepted').Count
\$d = @(\$l | Select-String -Pattern '^conn \d+: rx performance').Count
\"STATE \$n \$a \$d\"" 2>/dev/null | tr -d '\r' | sed -n 's/^STATE //p' | tail -1
}
want_wire=$((2 * MIGR_N))
if [ "$lb_mode" = server ]; then
    want_procs=$((3 * MIGR_N + 1)); want_lb=$MIGR_N
else
    want_procs=$((4 * MIGR_N)); want_lb=0
fi
load_all_up() {
    wire_up=$(load_wire_up)
    read -r guest_procs lb_accepted lb_done <<<"$(load_guest_state)"
    [ "$wire_up" -eq "$want_wire" ] && [ "${guest_procs:-0}" -eq "$want_procs" ] &&
        [ "${lb_accepted:-0}" -eq "$want_lb" ] && [ "${lb_done:-1}" -eq 0 ]
}
estab_deadline=$(( $(date +%s) + MIGR_ESTAB_TIMEOUT ))
while [ "$(date +%s)" -lt "$estab_deadline" ]; do
    load_all_up && break
    sleep 1
done
info "  load up: wire $wire_up/$want_wire, guest vsock_perf ${guest_procs:-?}/$want_procs, loopback accepted ${lb_accepted:-?}/$want_lb (finished ${lb_done:-?}); ramping ${MIGR_RAMP_SECS}s"
sleep "$MIGR_RAMP_SECS"

# --- phase 2: save/restore mid-transfer (transport reset) ----------------
migr_plan_cid "$guest_name" "$guest_cid" "$LOGDIR"

inflight_ok=0
load_all_up && inflight_ok=1
inflight="wire $wire_up/$want_wire, guest vsock_perf ${guest_procs:-?}/$want_procs, loopback accepted ${lb_accepted:-?}/$want_lb (finished ${lb_done:-?})"
info "  in flight at save: $inflight"

migr_save_restore "$guest_name" "$guest_cid" "$LOGDIR"

# From now on the guest lives at the (possibly new) CID; subsequent probes target it.
guest_cid="$new_cid"

# --- phase 3: stability assertions ---------------------------------------
# 3a. guest reachable again
migr_wait_guest

# 3b. driver still Running
migr_driver_state

# 3c. a fresh vsock connection still completes (forward: local receiver, guest sender)
probe_ok=0
probe_rx="$LOGDIR/probe_rx.log"
if [ "$guest_up" -eq 1 ]; then
    "$LOCAL_BIN" --port "$MIGR_PROBE_PORT" --buf-size 64K > "$probe_rx" 2>&1 &
    probe_pid=$!
    sleep 2
    timeout "$MIGR_PROBE_TIMEOUT" ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
        "${guest_bin_dir}\\vsock_perf.exe --sender $host_cid --port $MIGR_PROBE_PORT --bytes 8M --buf-size 64K" \
        > "$LOGDIR/probe_tx.log" 2>&1 || true
    # If the sender failed to connect, the receiver is parked in accept(); bound the wait.
    for _ in $(seq 1 $((MIGR_PROBE_TIMEOUT * 2))); do kill -0 "$probe_pid" 2>/dev/null || break; sleep 0.5; done
    kill "$probe_pid" 2>/dev/null || true
    wait "$probe_pid" 2>/dev/null || true
    tr -d '\r' < "$probe_rx" > "$probe_rx.tmp" && mv "$probe_rx.tmp" "$probe_rx"
    grep -q 'rx performance:' "$probe_rx" && probe_ok=1
fi

# 3d. reverse probe (host sender -> guest receiver on the new CID) proves the guest driver
# picked up the CID we asked for on restore. Guest receiver runs foreground under SSH.
probe2_ok=0
probe2_rx="$LOGDIR/probe2_rx.log"
probe2_tx="$LOGDIR/probe2_tx.log"
if [ "$guest_up" -eq 1 ]; then
    _guest_ssh "${guest_bin_dir}\\vsock_perf.exe --port $MIGR_PROBE_PORT2 --buf-size 64K" \
        > "$probe2_rx" 2>&1 &
    probe2_pid=$!
    sleep 2
    timeout "$MIGR_PROBE_TIMEOUT" "$LOCAL_BIN" --sender "$guest_cid" --port "$MIGR_PROBE_PORT2" --bytes 8M --buf-size 64K \
        > "$probe2_tx" 2>&1 || true
    # Bound the guest receiver's ssh session too; the trailing taskkill reaps a
    # guest-side vsock_perf still parked in accept().
    for _ in $(seq 1 $((MIGR_PROBE_TIMEOUT * 2))); do kill -0 "$probe2_pid" 2>/dev/null || break; sleep 0.5; done
    kill "$probe2_pid" 2>/dev/null || true
    wait "$probe2_pid" 2>/dev/null || true
    tr -d '\r' < "$probe2_rx" > "$probe2_rx.tmp" && mv "$probe2_rx.tmp" "$probe2_rx"
    grep -q 'rx performance:' "$probe2_rx" && probe2_ok=1
fi

# Guest-side address-list monitor (soft): sample the now-current CID and pull the
# watcher's change log, then retire the task. Never gates the run.
if [ "$MIGR_ADDRLIST" -eq 1 ] && [ "$guest_up" -eq 1 ]; then
    sleep 2
    addrlist_exit=$(timeout 30 ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
        "${guest_bin_dir}\\vsock_perf.exe --addrlist-query" 2>/dev/null | tr -d '\r' | grep -oE 'CID = [0-9]+' | grep -oE '[0-9]+')
    timeout 30 ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" 'type C:\addrlist-watch.log' 2>/dev/null | tr -d '\r' > "$addrlist_log" || true
    _guest_ssh 'schtasks /end /tn viosock_addrlist & schtasks /delete /tn viosock_addrlist /f' >/dev/null 2>&1 || true
fi

# --- 3e. the load finishes on its own: wire transfers were reset, loopback ones must complete ---
# run-stress bounds itself by MIGR_LOAD_TIMEOUT (+60 s for the guest sessions); the extra
# margin covers its log pull and summary.
for _ in $(seq 1 $((MIGR_LOAD_TIMEOUT + 180))); do kill -0 "$stress_pid" 2>/dev/null || break; sleep 1; done
if kill -0 "$stress_pid" 2>/dev/null; then
    warn "  run-stress still running after ${MIGR_LOAD_TIMEOUT}s — killing it"
    kill "$stress_pid" 2>/dev/null || true
fi
wait "$stress_pid" 2>/dev/null || true
timeout 60 ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" 'taskkill /F /IM vsock_perf.exe' >/dev/null 2>&1 || true

lb_line=$(grep -E '^ *loopback fails *:' "$stress_log" 2>/dev/null | tail -1)
lb_ok=0
[[ "$lb_line" =~ rx=0\ tx=0\  ]] && lb_ok=1

# --- report --------------------------------------------------------------
echo
info "=== migration stability summary ==="
printf '  connections/dir : %d\n' "$MIGR_N"
printf '  CID             : %s\n' \
    "$([ "$new_cid" = "$(config_read "$CFG" guest_cid)" ] && echo "$guest_cid (unchanged)" || echo "$(config_read "$CFG" guest_cid) -> $new_cid")"
if [ "$MIGR_ADDRLIST" -eq 1 ]; then
    printf '  guest CID query : start=%s exit=%s\n' "${addrlist_start:-?}" "${addrlist_exit:-?}"
    if [ -s "$addrlist_log" ]; then
        printf '  guest ADDR_LIST_CHANGE:\n'
        sed 's/^/    /' "$addrlist_log"
    else
        printf '  guest ADDR_LIST_CHANGE: (no change captured)\n'
    fi
fi
printf '  in flight @save : %s%s\n' "$inflight" \
    "$([ "$inflight_ok" -eq 1 ] || echo '  — FAIL: raise MIGR_BYTES / MIGR_ESTAB_TIMEOUT')"
printf '  save / restore  : rc %d / %d\n' "$save_rc" "$restore_rc"
printf '  guest reachable : %s\n' "$([ "$guest_up" -eq 1 ] && echo yes || echo NO)"
printf '  driver state    : %s\n' "$srv_state"
printf '  guest -> host   : %s\n' "$([ "$probe_ok"  -eq 1 ] && echo ok || echo FAIL)"
printf '  host  -> guest  : %s\n' "$([ "$probe2_ok" -eq 1 ] && echo ok || echo FAIL)"
printf '  loopback load   : %s\n' "$([ "$lb_ok" -eq 1 ] && echo ok || echo "FAIL (${lb_line:-no summary in $stress_log})")"

if [ "$inflight_ok" -eq 1 ] && [ "$save_rc" -eq 0 ] && [ "$restore_rc" -eq 0 ] \
   && [ "$guest_up" -eq 1 ] && [ "$driver_ok" -eq 1 ] \
   && [ "$probe_ok" -eq 1 ] && [ "$probe2_ok" -eq 1 ] && [ "$lb_ok" -eq 1 ]; then
    info "=== PASS ==="
    exit 0
else
    err "=== FAIL (inflight=$inflight_ok save=$save_rc restore=$restore_rc up=$guest_up driver=$srv_state probe=$probe_ok probe2=$probe2_ok loopback=$lb_ok) ==="
    exit 1
fi
