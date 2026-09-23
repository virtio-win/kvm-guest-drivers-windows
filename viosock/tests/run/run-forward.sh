#!/usr/bin/env bash
# Forward vsock_test sweep (Linux = vsock acceptor / --mode=server,
# Windows = vsock connector / --mode=client).
#
# Runs every ID listed in forward.list, one --pick per ID (fresh control
# channel + fresh server per test). Exits non-zero if any enabled test
# fails or hangs — enabled tests MUST pass, that's the whole contract.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

CFG=""; LIST="$_here/forward.list"; PER_TEST_TIMEOUT=40
CONTROL_PORT=12345
LOCAL_BIN="/ssd/vsock_test"       # vsock_test on the Linux host (this box)
LOGDIR=""; VARIANT=""; BITS="x64"; JUNIT=""; PICK_IDS=()

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
        -h|--help)
            cat >&2 <<EOF
Usage: $0 [--config <cfg>] [--list <path>] [--timeout <s>] [--port <p>]
          [--logdir <dir>] [--variant <name>]
          [--bits x64|x86 | --x86] [--junit <path>]

  --list       Which forward.list to read (default: forward.list next to script).
  --timeout    Per-test hard timeout in seconds (default: 40).
  --logdir     Directory for per-test log files (default: /tmp/vsock-run-<pid>).
               Only failed-test logs are kept; passed-test logs are removed.
  --variant    Run only rows with this variant (default: all).
  --bits       CPU bit-width to run the guest binary at (default: x64).
  --x86        Shorthand for --bits x86.
  --junit      Emit a JUnit XML report at <path>.
  --pick <id>  Run only these IDs, bypassing the list.  Accepts a single
               number, a comma-separated set, or may be given repeatedly.
               Variant defaults to posix unless --variant is set.
               Examples: --pick 22
                         --pick 18,20,27,28,37
                         --pick 18 --pick 20 --pick 27
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

case "$BITS" in
    x64|x86) ;;
    *) die "--bits must be x64 or x86, got: $BITS" ;;
esac

CFG=$(discover_config "$CFG")
guest_load "$CFG"
host_cid=$(config_read "$CFG" host_cid);   [ -n "$host_cid" ]    || die "config has no host_cid="
guest_cid=$(config_read "$CFG" guest_cid); [ -n "$guest_cid" ]   || die "config has no guest_cid="

# Address the guest control-client uses to reach us. Priority:
#   1. host_ip= from config (explicit override).
#   2. `ip route get $guest_ip` — picks the interface actually facing
#      the guest.  This correctly handles multi-homed hosts (e.g. dev008
#      talking to a host-only-network guest via a different NIC).
#   3. `hostname -I | awk '{print $1}'` — last-resort fallback.
host_ip=$(config_read "$CFG" host_ip)
[ -z "$host_ip" ] && host_ip=$(ip -o -4 route get "$guest_ip" 2>/dev/null \
    | awk '{for (i=1; i<=NF; i++) if ($i=="src") { print $(i+1); exit }}')
[ -z "$host_ip" ] && host_ip=$(hostname -I 2>/dev/null | awk '{print $1}')
[ -n "$host_ip" ] || die "cannot determine host_ip; set it in $CFG"

if [ -z "$LOGDIR" ]; then LOGDIR="/tmp/vsock-run-$$"; fi
mkdir -p "$LOGDIR"

if [ "${#PICK_IDS[@]}" -gt 0 ]; then
    # --pick(s) bypass the list entirely — useful for debug iteration
    # and feature-focused runs (e.g. all MSG_ZEROCOPY IDs).
    ROWS=()
    for id in "${PICK_IDS[@]}"; do
        [[ "$id" =~ ^[0-9]+$ ]] || die "--pick must be a numeric ID, got: $id"
        ROWS+=("$id"$'\t'"${VARIANT:-posix}")
    done
else
    mapfile -t ROWS < <(read_list "$LIST" "$VARIANT")
    [ "${#ROWS[@]}" -gt 0 ] || die "no enabled tests in $LIST${VARIANT:+ (variant=$VARIANT)}"
fi

info "== forward sweep: ${#ROWS[@]} test(s), bits=$BITS, timeout ${PER_TEST_TIMEOUT}s, logs in $LOGDIR =="

[ -n "$JUNIT" ] && junit_begin "$JUNIT" "forward"

_cleanup() {
    pkill -f "$LOCAL_BIN" 2>/dev/null || true
    local kill=""
    while IFS= read -r img; do kill+="taskkill /F /IM $img 2>nul & "; done < <(variant_all_images)
    _guest_ssh "$kill exit 0" >/dev/null 2>&1 || true
}
trap _cleanup EXIT
_cleanup

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
    cli_log="$LOGDIR/cli_${N}_${V}.log"
    log "[$tag] server: $LOCAL_BIN --mode=server --pick $N"
    "$LOCAL_BIN" --mode=server --control-port="$CONTROL_PORT" --peer-cid="$guest_cid" --pick "$N" \
        >"$srv_log" 2>&1 &
    srv_pid=$!
    sleep 1

    log "[$tag] client: ssh $_guest_ssh_host $guest_cmd --mode=client --control-host=$host_ip --pick $N"
    t_start=$(date +%s.%N)
    # Strip Windows CRLF up front so $CLIENT / $LINE never carry \r into
    # log files or JUnit attributes.  Pipefail preserves ssh/timeout's rc.
    CLIENT=$(timeout "$PER_TEST_TIMEOUT" ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
        "$guest_cmd --mode=client --control-host=$host_ip --control-port=$CONTROL_PORT --peer-cid=$host_cid --pick $N" 2>&1 \
        | tr -d '\r')
    RC=$?
    t_end=$(date +%s.%N)
    elapsed=$(awk -v a="$t_start" -v b="$t_end" 'BEGIN{ printf "%.3f", b-a }')
    printf '%s\n' "$CLIENT" > "$cli_log"
    log "[$tag] client returned rc=$RC in ${elapsed}s"

    # Reap the server.  A well-behaved client leaves the server to exit
    # cleanly after the COMPLETED barrier; if the client hung or died the
    # server may still be blocked in accept()/read() — kill it so `wait`
    # doesn't hang the whole sweep.
    if kill -0 "$srv_pid" 2>/dev/null; then
        log "[$tag] server still alive after client; sending SIGTERM"
        kill -TERM "$srv_pid" 2>/dev/null || true
        sleep 1
        kill -0 "$srv_pid" 2>/dev/null && kill -KILL "$srv_pid" 2>/dev/null || true
    fi
    wait "$srv_pid" 2>/dev/null || true

    LINE=$(printf '%s' "$CLIENT" | grep -E "^$N - " | head -1)
    [ -z "$LINE" ] && LINE="$N - (no client output; server log: $srv_log)"

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
info "=== forward: pass=$PASS fail=$FAIL hung=$HUNG (of ${#ROWS[@]} enabled, bits=$BITS) ==="
if [ "${#FAILED_ENTRIES[@]}" -gt 0 ]; then
    err "failed: ${FAILED_ENTRIES[*]}"
    err "logs:"
    for entry in "${FAILED_LOGS[@]}"; do
        IFS='|' read -r t cl sl <<< "$entry"
        printf '  [%s] client=%s  server=%s\n' "$t" "$cl" "$sl" >&2
    done
    exit 1
fi
