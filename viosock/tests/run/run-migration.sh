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
# The load's own transfers are expected to be reset by the save/restore and are
# NOT part of the pass criterion; the load is only there to exercise the reset
# path with real in-flight traffic (parked recv/poll, per-socket teardown).
#
# The connection count is the tunable load knob — MIGR_N env or --connections.
# A full high-count run may still expose issues, so the default is modest;
# raise it to stress the reset harder.
#
# Pass criterion (stability only — no address/CID assertions):
#   * save and restore both succeed,
#   * the guest is reachable again after restore,
#   * the VirtioSocket service is Running,
#   * a fresh post-restore vsock_perf transfer completes.
#
# Must run on the libvirt host (needs virsh + the local Linux vsock_perf).

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

# --- tunables (env-overridable) ------------------------------------------
MIGR_N=${MIGR_N:-8}                   # connections per direction — the load knob
MIGR_BYTES=${MIGR_BYTES:-4G}          # bytes/conn (large enough to still be in flight at save time)
MIGR_RAMP_SECS=${MIGR_RAMP_SECS:-12}  # let transfers run this long before the save
MIGR_PROBE_PORT=${MIGR_PROBE_PORT:-40000}
MIGR_PROBE_PORT2=${MIGR_PROBE_PORT2:-40001}
MIGR_CHANGE_CID=${MIGR_CHANGE_CID:-1} # 0 = restore with unchanged CID
MIGR_NEW_CID=${MIGR_NEW_CID:-}        # explicit new CID; empty = guest_cid+1 (skipping 0..3)
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
  MIGR_N           [8]    Connections per direction (2*N total).
  MIGR_BYTES       [4G]   Bytes per connection for the load.
  MIGR_RAMP_SECS   [12]   Seconds of transfer before the save is injected.
  MIGR_CHANGE_CID  [1]    Restore with a different guest CID (cross-host live-migration shape).
  MIGR_NEW_CID     []     Explicit new CID; empty = guest_cid+1 (skipping 0..3).
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

[ -z "$LOGDIR" ] && LOGDIR="/tmp/vsock-migration-$$"
mkdir -p "$LOGDIR"

info "== migration stability: N=$MIGR_N/dir, bytes=$MIGR_BYTES, domain=$guest_name, ramp=${MIGR_RAMP_SECS}s =="
info "   logs in $LOGDIR"

# --- phase 1: start the load (run-stress.sh) in the background -----------
stress_log="$LOGDIR/stress.log"
STRESS_BYTES="$MIGR_BYTES" "$_here/run-stress.sh" \
    --config "$CFG" --connections "$MIGR_N" --logdir "$LOGDIR/stress" \
    > "$stress_log" 2>&1 &
stress_pid=$!
info "  load started via run-stress.sh (pid=$stress_pid); waiting for transfers to run..."

for _ in $(seq 1 90); do
    grep -q 'waiting for completion' "$stress_log" 2>/dev/null && break
    kill -0 "$stress_pid" 2>/dev/null || { warn "  run-stress exited early — see $stress_log"; break; }
    sleep 1
done
info "  letting transfers ramp ${MIGR_RAMP_SECS}s"
sleep "$MIGR_RAMP_SECS"

# --- phase 2: save/restore mid-transfer (transport reset) ----------------
sr="$LOGDIR/$guest_name.migr.sr"
newxml=""
new_cid="$guest_cid"
if [ "$MIGR_CHANGE_CID" -eq 1 ]; then
    if [ -n "$MIGR_NEW_CID" ]; then
        new_cid="$MIGR_NEW_CID"
    else
        new_cid=$((guest_cid + 1))
        case "$new_cid" in 0|1|2|3) new_cid=4 ;; esac
    fi
    [ "$new_cid" != "$guest_cid" ] || die "MIGR_NEW_CID=$new_cid equals current guest_cid"
    newxml="$LOGDIR/$guest_name.new.xml"
    virsh dumpxml "$guest_name" > "$newxml" || die "virsh dumpxml $guest_name failed"
    grep -qE "<cid[^/]*/>" "$newxml" || die "domain XML has no <cid .../> to swap"
    # Normalize to auto='no' address='new' — with auto='yes' libvirt may re-pick a CID on restore.
    sed -i -E "s|<cid[^/]*/>|<cid auto='no' address='$new_cid'/>|" "$newxml"
    info "  CID plan: $guest_cid -> $new_cid  (XML: $newxml)"
fi

info "== virsh save $guest_name (mid-transfer) =="
save_rc=0;    virsh save    "$guest_name" "$sr" --running || save_rc=$?
info "  save rc=$save_rc state=$(virsh domstate "$guest_name" 2>/dev/null)"
sleep 2
if [ -n "$newxml" ]; then
    info "== virsh restore --xml (CID $guest_cid -> $new_cid) =="
    restore_rc=0; virsh restore "$sr" --xml "$newxml" --running || restore_rc=$?
else
    info "== virsh restore =="
    restore_rc=0; virsh restore "$sr" --running || restore_rc=$?
fi
info "  restore rc=$restore_rc state=$(virsh domstate "$guest_name" 2>/dev/null)"
rm -f "$sr"

# From now on the guest lives at the (possibly new) CID; subsequent probes target it.
guest_cid="$new_cid"

# --- phase 3: stability assertions ---------------------------------------
# 3a. guest reachable again
guest_up=0
for _ in $(seq 1 60); do
    if _guest_ssh 'echo up' >/dev/null 2>&1; then guest_up=1; break; fi
    sleep 2
done

# 3b. driver still Running
srv_state=$(_guest_ssh 'sc query VirtioSocket' 2>/dev/null \
    | tr -d '\r' | awk -F: '/STATE/ {gsub(/^[ \t]+|[ \t]+$/, "", $2); print $2; exit}')
[ -n "$srv_state" ] || srv_state='<unknown>'
driver_ok=0; case "$srv_state" in *RUNNING*|*[[:space:]]4*|4*) driver_ok=1 ;; esac

# 3c. a fresh vsock connection still completes (forward: local receiver, guest sender)
probe_ok=0
probe_rx="$LOGDIR/probe_rx.log"
if [ "$guest_up" -eq 1 ]; then
    "$LOCAL_BIN" --port "$MIGR_PROBE_PORT" --buf-size 64K > "$probe_rx" 2>&1 &
    probe_pid=$!
    sleep 2
    _guest_ssh "${guest_bin_dir}\\vsock_perf.exe --sender $host_cid --port $MIGR_PROBE_PORT --bytes 8M --buf-size 64K" \
        > "$LOGDIR/probe_tx.log" 2>&1 || true
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
    "$LOCAL_BIN" --sender "$guest_cid" --port "$MIGR_PROBE_PORT2" --bytes 8M --buf-size 64K \
        > "$probe2_tx" 2>&1 || true
    wait "$probe2_pid" 2>/dev/null || true
    tr -d '\r' < "$probe2_rx" > "$probe2_rx.tmp" && mv "$probe2_rx.tmp" "$probe2_rx"
    grep -q 'rx performance:' "$probe2_rx" && probe2_ok=1
fi

# --- cleanup: stop the load and any stuck guest vsock_perf ---------------
kill "$stress_pid" 2>/dev/null || true
wait "$stress_pid" 2>/dev/null || true
_guest_ssh 'taskkill /F /IM vsock_perf.exe' >/dev/null 2>&1 || true

# --- report --------------------------------------------------------------
echo
info "=== migration stability summary ==="
printf '  connections/dir : %d\n' "$MIGR_N"
printf '  CID             : %s\n' \
    "$([ "$new_cid" = "$(config_read "$CFG" guest_cid)" ] && echo "$guest_cid (unchanged)" || echo "$(config_read "$CFG" guest_cid) -> $new_cid")"
printf '  save / restore  : rc %d / %d\n' "$save_rc" "$restore_rc"
printf '  guest reachable : %s\n' "$([ "$guest_up" -eq 1 ] && echo yes || echo NO)"
printf '  driver state    : %s\n' "$srv_state"
printf '  guest -> host   : %s\n' "$([ "$probe_ok"  -eq 1 ] && echo ok || echo FAIL)"
printf '  host  -> guest  : %s\n' "$([ "$probe2_ok" -eq 1 ] && echo ok || echo FAIL)"

if [ "$save_rc" -eq 0 ] && [ "$restore_rc" -eq 0 ] \
   && [ "$guest_up" -eq 1 ] && [ "$driver_ok" -eq 1 ] \
   && [ "$probe_ok" -eq 1 ] && [ "$probe2_ok" -eq 1 ]; then
    info "=== PASS ==="
    exit 0
else
    err "=== FAIL (save=$save_rc restore=$restore_rc up=$guest_up driver=$srv_state probe=$probe_ok probe2=$probe2_ok) ==="
    exit 1
fi
