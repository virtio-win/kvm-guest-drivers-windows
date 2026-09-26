#!/usr/bin/env bash
# Reverse vsock_test sweep (Windows = vsock acceptor / --mode=server,
# Linux  = vsock connector / --mode=client).
#
# The Windows server is launched over a background SSH session and stays
# in sshd's job object as the configured guest user; killing the local
# ssh PID after the client finishes tears the server down cleanly.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

# Grace period between "TCP control port listening on guest" (detected by
# wait_guest_port) and the client connect: the Windows-side vsock listen
# socket may become ready shortly AFTER the control TCP port.  Overridable
# from the environment (`SERVER_GRACE_SECS=<n> ./run-reverse.sh ...`) if a
# particular test starts failing "vsock connect timeout".  Default 1s.
SERVER_GRACE_SECS=${SERVER_GRACE_SECS:-1}

CFG=""; LIST="$_here/reverse.list"; PER_TEST_TIMEOUT=40
CONTROL_PORT=12345
LOCAL_BIN="/opt/vsock-test/vsock_test"
LOGDIR=""; VARIANT=""; BITS="x64"; JUNIT=""; PICK_IDS=(); MAX_ID=""

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
        --max-id) MAX_ID="$2"; shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 [--config <cfg>] [--list <path>] [--timeout <s>] [--port <p>]
          [--logdir <dir>] [--variant <name>]
          [--bits x64|x86 | --x86] [--junit <path>]
          [--pick <id[,id,...]>] [--max-id <n>]

  --max-id <n> Skip every row whose id > n. Use when the Linux-side host
               vsock_test is older than our 6.19-based guest binary: cap n
               to the last id that stock host binary knows (26 for v6.12).
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

case "$BITS" in
    x64|x86) ;;
    *) die "--bits must be x64 or x86, got: $BITS" ;;
esac

CFG=$(discover_config "$CFG") || exit $?
guest_load "$CFG"
host_cid=$(config_read "$CFG" host_cid);   [ -n "$host_cid" ]    || die "config has no host_cid="
guest_cid=$(config_read "$CFG" guest_cid); [ -n "$guest_cid" ]   || die "config has no guest_cid="
# guest_ip is loaded by guest_load; nothing extra to do here.

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

# --max-id fallback: CLI > MAX_TEST_ID env > config 'max_test_id' > unlimited.
[ -z "$MAX_ID" ] && MAX_ID=${MAX_TEST_ID:-}
[ -z "$MAX_ID" ] && MAX_ID=$(config_read "$CFG" max_test_id)
if [ -n "$MAX_ID" ]; then
    [[ "$MAX_ID" =~ ^[0-9]+$ ]] || die "--max-id must be a positive integer, got: $MAX_ID"
    FILTERED=()
    for row in "${ROWS[@]}"; do
        IFS=$'\t' read -r _id _ <<< "$row"
        [ "$_id" -le "$MAX_ID" ] && FILTERED+=("$row")
    done
    ROWS=("${FILTERED[@]}")
    [ "${#ROWS[@]}" -gt 0 ] || die "no rows with id <= $MAX_ID"
fi

info "== reverse sweep: ${#ROWS[@]} test(s), bits=$BITS, timeout ${PER_TEST_TIMEOUT}s, logs in $LOGDIR =="

[ -n "$JUNIT" ] && junit_begin "$JUNIT" "reverse"

_cleanup() {
    local kill=""
    while IFS= read -r img; do kill+="taskkill /F /IM $img 2>nul & "; done < <(variant_all_images)
    _guest_ssh "$kill exit 0" >/dev/null 2>&1 || true
    [ -n "${srv_pid:-}" ] && kill -0 "$srv_pid" 2>/dev/null && kill "$srv_pid" 2>/dev/null || true
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
    log "[$tag] server: ssh $_guest_ssh_host $guest_cmd --mode=server --pick $N"
    # Keep the ssh session alive in the background so the guest-side
    # vsock_test.exe stays in sshd's job object as Administrator.  Killing
    # the ssh PID after the client finishes closes the job on the guest
    # and terminates the server.
    ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" \
        "$guest_cmd --mode=server --control-port=$CONTROL_PORT --peer-cid=$host_cid --pick $N" \
        >"$srv_log" 2>&1 &
    srv_ssh_pid=$!
    log "[$tag] waiting for server to bind :$CONTROL_PORT on guest..."
    if ! wait_guest_port "$CONTROL_PORT" 20; then
        kill "$srv_ssh_pid" 2>/dev/null; wait "$srv_ssh_pid" 2>/dev/null || true
        LINE="$N -  (server never bound port $CONTROL_PORT)"
        printf '[%s] %s\n' "$tag" "$LINE"
        FAIL=$((FAIL+1)); FAILED_ENTRIES+=("$tag")
        [ -n "$JUNIT" ] && junit_case_failed "$JUNIT" "$name — bind timeout" "0.000" "server never bound port $CONTROL_PORT"
        continue
    fi
    sleep "$SERVER_GRACE_SECS"

    cli_log="$LOGDIR/cli_${N}_${V}.log"
    log "[$tag] client: $LOCAL_BIN --mode=client --control-host=$guest_ip --pick $N"
    t_start=$(date +%s.%N)
    # Linux client — no CRLF to strip, but keep the pipeline shape symmetric
    # with the other runners.
    CLIENT=$(timeout "$PER_TEST_TIMEOUT" "$LOCAL_BIN" \
        --mode=client --control-host="$guest_ip" --control-port="$CONTROL_PORT" \
        --peer-cid="$guest_cid" --pick "$N" 2>&1 \
        | tr -d '\r')
    RC=$?
    t_end=$(date +%s.%N)
    elapsed=$(awk -v a="$t_start" -v b="$t_end" 'BEGIN{ printf "%.3f", b-a }')
    printf '%s\n' "$CLIENT" > "$cli_log"
    log "[$tag] client returned rc=$RC in ${elapsed}s"

    # Tear down the server: killing local ssh closes sshd's job object → server dies.
    kill "$srv_ssh_pid" 2>/dev/null || true
    wait "$srv_ssh_pid" 2>/dev/null || true
    # Strip Windows CRLF from srv_log (server output came through Windows).
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
info "=== reverse: pass=$PASS fail=$FAIL hung=$HUNG (of ${#ROWS[@]} enabled, bits=$BITS) ==="
if [ "${#FAILED_ENTRIES[@]}" -gt 0 ]; then
    err "failed: ${FAILED_ENTRIES[*]}"
    err "logs:"
    for entry in "${FAILED_LOGS[@]}"; do
        IFS='|' read -r t cl sl <<< "$entry"
        printf '  [%s] client=%s  server=%s\n' "$t" "$cl" "$sl" >&2
    done
    exit 1
fi
