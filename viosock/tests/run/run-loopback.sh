#!/usr/bin/env bash
# Loopback vsock_test sweep (both --mode=server and --mode=client
# on the same Windows guest, control channel over 127.0.0.1, vsock
# peer-cid = own guest_cid).
#
# The server is detached via schtasks /ru SYSTEM /sc once; the client
# is invoked from a separate SSH session.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

# Guest-side paths used only by the --as-system code path.  Kept as
# top-of-script constants so a future move (e.g. to C:\ci-scratch\)
# is one line.
GUEST_SRV_BAT='C:\srv_lb.bat'
GUEST_SRV_LOG_FMT='C:\srv_lb_%s_%s.log'   # printf format: N, V
SCHTASKS_NAME='vsock_loopback'

CFG=""; LIST="$_here/loopback.list"; PER_TEST_TIMEOUT=40
CONTROL_PORT=12346
LOGDIR=""; VARIANT=""; BITS="x64"; JUNIT=""; PICK_IDS=(); AS_SYSTEM=0

while [ $# -gt 0 ]; do
    case "$1" in
        --config)  CFG="$2";  shift 2 ;;
        --list)    LIST="$2"; shift 2 ;;
        --timeout) PER_TEST_TIMEOUT="$2"; shift 2 ;;
        --port)    CONTROL_PORT="$2"; shift 2 ;;
        --logdir)  LOGDIR="$2"; shift 2 ;;
        --variant) VARIANT="$2"; shift 2 ;;
        --bits)    BITS="$2"; shift 2 ;;
        --x86)     BITS="x86"; shift ;;
        --junit)   JUNIT="$2"; shift 2 ;;
        --pick)
            IFS=',' read -ra _picks <<< "$2"
            for p in "${_picks[@]}"; do PICK_IDS+=("$p"); done
            shift 2 ;;
        --as-system) AS_SYSTEM=1; shift ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

case "$BITS" in
    x64|x86) ;;
    *) die "--bits must be x64 or x86, got: $BITS" ;;
esac

CFG=$(discover_config "$CFG")
guest_load "$CFG"
guest_cid=$(config_read "$CFG" guest_cid); [ -n "$guest_cid" ]   || die "config has no guest_cid="

if [ -z "$LOGDIR" ]; then LOGDIR="/tmp/vsock-run-$$"; fi
mkdir -p "$LOGDIR"

if [ "${#PICK_IDS[@]}" -gt 0 ]; then
    ROWS=()
    for id in "${PICK_IDS[@]}"; do
        [[ "$id" =~ ^[0-9]+$ ]] || die "--pick must be a numeric ID, got: $id"
        ROWS+=("$id"$'\t'"${VARIANT:-posix}")
    done
else
    mapfile -t ROWS < <(read_list "$LIST" "$VARIANT")
    [ "${#ROWS[@]}" -gt 0 ] || die "no enabled tests in $LIST${VARIANT:+ (variant=$VARIANT)}"
fi

info "== loopback sweep: ${#ROWS[@]} test(s), bits=$BITS, timeout ${PER_TEST_TIMEOUT}s =="

[ -n "$JUNIT" ] && junit_begin "$JUNIT" "loopback"

_cleanup() {
    local kill=""
    while IFS= read -r img; do kill+="taskkill /F /IM $img 2>nul & "; done < <(variant_all_images)
    if [ "$AS_SYSTEM" -eq 1 ]; then
        _guest_ssh "$kill schtasks /end /tn $SCHTASKS_NAME /f 2>nul & schtasks /delete /tn $SCHTASKS_NAME /f 2>nul & exit 0" >/dev/null 2>&1 || true
    else
        _guest_ssh "$kill exit 0" >/dev/null 2>&1 || true
    fi
}
trap _cleanup EXIT
_cleanup

_pull_guest_log() {
    local remote="$1" local_path="$2"
    _guest_scp_from "$remote" "$local_path" 2>/dev/null || true
    _guest_ssh "del $remote 2>nul & exit 0" >/dev/null 2>&1 || true
}

PASS=0; FAIL=0; HUNG=0
FAILED_ENTRIES=()
FAILED_LOGS=()

for row in "${ROWS[@]}"; do
    IFS=$'\t' read -r N V <<< "$row"
    guest_cmd=$(variant_to_cmd "$V" "$BITS" "$guest_bin_dir") || { FAIL=$((FAIL+1)); FAILED_ENTRIES+=("$N/$V"); continue; }
    tag="$N/$V"
    name="$N/$V/$BITS"

    _cleanup

    srv_log="$LOGDIR/srv_${N}_${V}.log"
    if [ "$AS_SYSTEM" -eq 1 ]; then
        srv_log_guest=$(printf "$GUEST_SRV_LOG_FMT" "$N" "$V")
        log "[$tag] server: schtasks $SCHTASKS_NAME ($guest_cmd --pick $N as SYSTEM)"
        _guest_ssh "(echo @echo off & echo $guest_cmd --mode=server --control-port=$CONTROL_PORT --peer-cid=$guest_cid --pick $N ^> $srv_log_guest 2^>^&1) > $GUEST_SRV_BAT"
        _guest_ssh "schtasks /create /tn $SCHTASKS_NAME /tr $GUEST_SRV_BAT /sc once /st 00:00 /ru SYSTEM /f" >/dev/null 2>&1
        _guest_ssh "schtasks /run /tn $SCHTASKS_NAME" >/dev/null 2>&1
        srv_ssh_pid=""
    else
        log "[$tag] server: ssh $_guest_ssh_host $guest_cmd --mode=server --pick $N"
        ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
            "$guest_cmd --mode=server --control-port=$CONTROL_PORT --peer-cid=$guest_cid --pick $N" \
            >"$srv_log" 2>&1 &
        srv_ssh_pid=$!
    fi
    log "[$tag] waiting for server to bind :$CONTROL_PORT..."
    if ! wait_guest_port "$CONTROL_PORT" 20; then
        [ -n "$srv_ssh_pid" ] && { kill "$srv_ssh_pid" 2>/dev/null; wait "$srv_ssh_pid" 2>/dev/null || true; }
        LINE="$N -  (server never bound port $CONTROL_PORT)"
        printf '[%s] %s\n' "$tag" "$LINE"
        FAIL=$((FAIL+1)); FAILED_ENTRIES+=("$tag")
        [ -n "$JUNIT" ] && junit_case_failed "$JUNIT" "$name — bind timeout" "0.000" "server never bound port $CONTROL_PORT"
        continue
    fi
    # Grace for Administrator server to bind vsock listen after
    # TCP:12345 is up (see comment in run-reverse.sh).
    sleep 5

    cli_log="$LOGDIR/cli_${N}_${V}.log"
    log "[$tag] client: ssh $_guest_ssh_host $guest_cmd --mode=client (loopback --peer-cid=$guest_cid) --pick $N"
    t_start=$(date +%s.%N)
    # Strip Windows CRLF from client output up front.
    CLIENT=$(timeout "$PER_TEST_TIMEOUT" ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
        "$guest_cmd --mode=client --control-host=127.0.0.1 --control-port=$CONTROL_PORT --peer-cid=$guest_cid --pick $N" 2>&1 \
        | tr -d '\r')
    RC=$?
    t_end=$(date +%s.%N)
    elapsed=$(awk -v a="$t_start" -v b="$t_end" 'BEGIN{ printf "%.3f", b-a }')
    printf '%s\n' "$CLIENT" > "$cli_log"
    log "[$tag] client returned rc=$RC in ${elapsed}s"

    # Tear down the server.
    if [ -n "$srv_ssh_pid" ]; then
        kill "$srv_ssh_pid" 2>/dev/null || true
        wait "$srv_ssh_pid" 2>/dev/null || true
    else
        _pull_guest_log "$srv_log_guest" "$srv_log"
        _guest_ssh "schtasks /end /tn $SCHTASKS_NAME /f 2>nul & schtasks /delete /tn $SCHTASKS_NAME /f 2>nul & exit 0" >/dev/null 2>&1
    fi
    # Strip Windows CRLF from srv_log (both sides are Windows in loopback).
    [ -f "$srv_log" ] && { tr -d '\r' < "$srv_log" > "$srv_log.tmp" && mv "$srv_log.tmp" "$srv_log"; }

    LINE=$(printf '%s' "$CLIENT" | grep -E "^$N - " | head -1)
    [ -z "$LINE" ] && LINE="$N - (no client output)"

    if [ "$RC" = "124" ]; then
        printf '[%s] %s [HUNG-timeout]\n' "$tag" "$LINE"
        HUNG=$((HUNG+1)); FAILED_ENTRIES+=("$tag"); FAILED_LOGS+=("$tag|$cli_log|$srv_log")
        [ -n "$JUNIT" ] && junit_case_failed "$JUNIT" "$name — ${LINE#$N - }" "$elapsed" "hung after ${PER_TEST_TIMEOUT}s" "$cli_log"
    elif printf '%s' "$LINE" | grep -q '\.\.\.ok' && [ "$RC" -eq 0 ]; then
        printf '[%s] %s\n' "$tag" "$LINE"
        PASS=$((PASS+1))
        rm -f "$srv_log" "$cli_log"
        [ -n "$JUNIT" ] && junit_case_ok "$JUNIT" "$name — ${LINE#$N - }" "$elapsed"
    else
        printf '[%s] %s\n' "$tag" "$LINE"
        FAIL=$((FAIL+1)); FAILED_ENTRIES+=("$tag"); FAILED_LOGS+=("$tag|$cli_log|$srv_log")
        [ -n "$JUNIT" ] && junit_case_failed "$JUNIT" "$name — ${LINE#$N - }" "$elapsed" "${LINE#*...}" "$cli_log"
    fi
done

[ -n "$JUNIT" ] && junit_finalize "$JUNIT"

echo
info "=== loopback: pass=$PASS fail=$FAIL hung=$HUNG (of ${#ROWS[@]} enabled, bits=$BITS) ==="
if [ "${#FAILED_ENTRIES[@]}" -gt 0 ]; then
    err "failed: ${FAILED_ENTRIES[*]}"
    err "logs:"
    for entry in "${FAILED_LOGS[@]}"; do
        IFS='|' read -r t cl sl <<< "$entry"
        printf '  [%s] client=%s  server=%s\n' "$t" "$cl" "$sl" >&2
    done
    exit 1
fi
