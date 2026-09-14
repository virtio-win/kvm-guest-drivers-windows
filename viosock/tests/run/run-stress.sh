#!/usr/bin/env bash
# run-stress.sh — bidirectional multi-connection vsock stress test.
#
# Launches N vsock_perf sender/receiver pairs in each direction at once
# (2·N connections total) between the Linux host and the Windows guest,
# and checks that every connection completes and the driver survives.
#
# Each connection uses its own vsock port so the Windows acceptor pending
# queue and the guest driver's per-socket state machine are exercised in
# parallel, not serialized on a single listen socket.
#
# Guest-side fan-out is done inside ONE PowerShell script per direction
# (receivers on the guest, then senders on the guest) via Start-Process +
# Wait-Process, so we open two SSH sessions total — not 2·N — regardless
# of how many connections the sweep is running.
#
# Pass criterion:
#   * every 2·N vsock_perf child (local and guest-side) returned exit 0,
#   * VirtioSocket service is still Running on the guest after the run.
#
# Aggregate RX/TX Gbps is emitted for reference; best-case per-connection
# throughput belongs in run-perf.sh, not here.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

# --- tunables (env-overridable) ------------------------------------------
STRESS_N=${STRESS_N:-64}
STRESS_BYTES=${STRESS_BYTES:-1G}
STRESS_BUF=${STRESS_BUF:-64K}
FWD_PORT_BASE=${FWD_PORT_BASE:-20000}
REV_PORT_BASE=${REV_PORT_BASE:-30000}
SERVER_GRACE_SECS=${SERVER_GRACE_SECS:-3}
GUEST_STRESS_DIR=${GUEST_STRESS_DIR:-'C:\stress'}

# --- args ----------------------------------------------------------------
CFG=""; LOGDIR=""; VARIANT="posix"; BITS="x64"; DIRS=""
LOCAL_BIN="/opt/vsock-test/vsock_perf"

while [ $# -gt 0 ]; do
    case "$1" in
        --config)       CFG="$2";       shift 2 ;;
        --logdir)       LOGDIR="$2";    shift 2 ;;
        --variant)      VARIANT="$2";   shift 2 ;;
        --bits)         BITS="$2";      shift 2 ;;
        --x86)          BITS="x86";     shift ;;
        --connections)  STRESS_N="$2";  shift 2 ;;
        --bytes)        STRESS_BYTES="$2"; shift 2 ;;
        --buf-size)     STRESS_BUF="$2";   shift 2 ;;
        --only)         DIRS="$2";      shift 2 ;;
        --local-bin)    LOCAL_BIN="$2"; shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 [--config <cfg>] [--logdir <dir>]
          [--variant posix|wsa|overlapped] [--bits x64|x86 | --x86]
          [--connections N] [--bytes <sz>] [--buf-size <sz>]
          [--only forward,reverse] [--local-bin <path>]

Runs N vsock_perf pairs in each direction in parallel (2·N connections
total). Guest-side receivers (reverse) and senders (forward) each fan
out inside a single PowerShell script; only 2 SSH sessions to the guest
are opened per run. Ports are FWD_PORT_BASE+i for forward, REV_PORT_BASE+i
for reverse. All logs land in --logdir (default: /tmp/vsock-stress-\$\$).

Tunables (env-overridable, defaults in [brackets]):
  STRESS_N          [64]      Connections per direction.
  STRESS_BYTES      [1G]      Bytes per connection.
  STRESS_BUF        [64K]     Buffer size (both sides).
  FWD_PORT_BASE     [20000]   Forward-connection port base (Linux receivers).
  REV_PORT_BASE     [30000]   Reverse-connection port base (Windows receivers).
  SERVER_GRACE_SECS [3]       Sleep between receiver launches and sender fan-out.
  GUEST_STRESS_DIR  [C:\stress] Guest-side scratch dir for per-connection logs.
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

case "$BITS"    in x64|x86) ;;             *) die "--bits must be x64 or x86" ;; esac
case "$VARIANT" in posix|wsa|overlapped) ;; *) die "--variant must be posix|wsa|overlapped" ;; esac
[ "$STRESS_N" -gt 0 ] 2>/dev/null || die "--connections must be a positive integer"

CFG=$(discover_config "$CFG")
guest_load "$CFG"
host_cid=$(config_read  "$CFG" host_cid);  [ -n "$host_cid"  ] || die "config has no host_cid="
guest_cid=$(config_read "$CFG" guest_cid); [ -n "$guest_cid" ] || die "config has no guest_cid="

[ -x "$LOCAL_BIN" ] || die "no vsock_perf at $LOCAL_BIN — run prepare-perf.sh first"

[ -z "$LOGDIR" ] && LOGDIR="/tmp/vsock-stress-$$"
mkdir -p "$LOGDIR"

# Guest binary path + variant flag. Same dispatch as run-perf.sh.
case "$BITS" in
    x64) guest_exe='vsock_perf.exe' ;;
    x86) guest_exe='vsock_perf_x86.exe' ;;
esac
guest_variant_flag=''
case "$VARIANT" in
    wsa)        guest_variant_flag='--variant wsa' ;;
    overlapped) guest_variant_flag='--variant overlapped' ;;
esac
guest_exe_path="${guest_bin_dir}\\${guest_exe}"

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

# --- guest-side fan-out via one PowerShell per direction ----------------
# Each script spawns N vsock_perf.exe processes with Start-Process, waits
# for all of them, and echoes one EXIT:<pid>:<code> line per child so
# we can count failures without re-parsing every log.  The per-child log
# files are then scp'd back after the ssh returns.
#
# The PowerShell here-doc uses single-quoted @'...'@ so $, `, and `` are
# NOT expanded on the shell side; parameters ($N, $Base, $Buf, ...) get
# interpolated ourselves by generating the script text with printf.

# Build a PS script that fans out N vsock_perf.exe children via
# Start-Process -RedirectStandardOutput/Error into per-connection log
# files, then WaitForExit()s them all.  We do NOT try to read
# ExitCode — Start-Process with -RedirectStandard* is documented to
# leave ExitCode $null even after WaitForExit — success/failure of
# each connection is judged post-hoc on the host by looking for the
# `rx performance:` / `tx performance:` line in the log (vsock_perf
# prints it only on a clean run).
gen_ps_rx() {
    local var_flag_prefix=""
    [ -n "$guest_variant_flag" ] && var_flag_prefix="$guest_variant_flag "
    cat <<EOF
\$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Path '$GUEST_STRESS_DIR' -Force | Out-Null
Get-ChildItem '$GUEST_STRESS_DIR\\rev_*.log','$GUEST_STRESS_DIR\\rev_*.err' -ErrorAction SilentlyContinue | Remove-Item
\$procs = @()
for (\$i = 0; \$i -lt $STRESS_N; \$i++) {
    \$port = $REV_PORT_BASE + \$i
    \$log  = "$GUEST_STRESS_DIR\\rev_\${i}_rx.log"
    \$err  = "$GUEST_STRESS_DIR\\rev_\${i}_rx.err"
    \$args = @('${var_flag_prefix}--port'.Split(' ') | Where-Object { \$_ -ne '' }) + @("\$port", '--buf-size', '$STRESS_BUF')
    \$procs += Start-Process -FilePath '$guest_exe_path' -ArgumentList \$args \`
        -RedirectStandardOutput \$log -RedirectStandardError \$err \`
        -NoNewWindow -PassThru
}
foreach (\$p in \$procs) { \$p.WaitForExit() }
[Console]::Out.WriteLine("DONE:$STRESS_N")
EOF
}

gen_ps_tx() {
    local var_flag_prefix=""
    [ -n "$guest_variant_flag" ] && var_flag_prefix="$guest_variant_flag "
    cat <<EOF
\$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Path '$GUEST_STRESS_DIR' -Force | Out-Null
Get-ChildItem '$GUEST_STRESS_DIR\\fwd_*.log','$GUEST_STRESS_DIR\\fwd_*.err' -ErrorAction SilentlyContinue | Remove-Item
\$procs = @()
for (\$i = 0; \$i -lt $STRESS_N; \$i++) {
    \$port = $FWD_PORT_BASE + \$i
    \$log  = "$GUEST_STRESS_DIR\\fwd_\${i}_tx.log"
    \$err  = "$GUEST_STRESS_DIR\\fwd_\${i}_tx.err"
    \$args = @('${var_flag_prefix}--sender'.Split(' ') | Where-Object { \$_ -ne '' }) + @('$host_cid', '--port', "\$port", '--bytes', '$STRESS_BYTES', '--buf-size', '$STRESS_BUF')
    \$procs += Start-Process -FilePath '$guest_exe_path' -ArgumentList \$args \`
        -RedirectStandardOutput \$log -RedirectStandardError \$err \`
        -NoNewWindow -PassThru
}
foreach (\$p in \$procs) { \$p.WaitForExit() }
[Console]::Out.WriteLine("DONE:$STRESS_N")
EOF
}

# --- local (Linux) fan-out ----------------------------------------------
declare -a LOCAL_PIDS=() LOCAL_NAMES=()

start_local_forward_receivers() {
    local i port name log
    for i in $(seq 0 $((STRESS_N - 1))); do
        port=$((FWD_PORT_BASE + i))
        name="fwd_$i"
        log="$LOGDIR/${name}_rx.log"
        "$LOCAL_BIN" --port "$port" --buf-size "$STRESS_BUF" \
            > "$log" 2>&1 &
        LOCAL_PIDS+=($!); LOCAL_NAMES+=("$name/rx")
    done
}

start_local_reverse_senders() {
    local i port name log
    for i in $(seq 0 $((STRESS_N - 1))); do
        port=$((REV_PORT_BASE + i))
        name="rev_$i"
        log="$LOGDIR/${name}_tx.log"
        "$LOCAL_BIN" --sender "$guest_cid" --port "$port" \
            --bytes "$STRESS_BYTES" --buf-size "$STRESS_BUF" \
            > "$log" 2>&1 &
        LOCAL_PIDS+=($!); LOCAL_NAMES+=("$name/tx")
    done
}

# --- go ------------------------------------------------------------------
info "== vsock stress: N=$STRESS_N per direction, bytes=$STRESS_BYTES, buf=$STRESS_BUF, variant=$VARIANT, bits=$BITS =="
info "   logs in $LOGDIR"

# Phase 1: fan-out receivers on both sides in parallel.
if [ "$run_fwd" -eq 1 ]; then
    start_local_forward_receivers
    info "  forward: $STRESS_N linux receivers spawned"
fi

guest_rx_pid=""
if [ "$run_rev" -eq 1 ]; then
    ps_rx=$(gen_ps_rx)
    _guest_ps "$ps_rx" > "$LOGDIR/guest_rev_rx.log" 2>&1 &
    guest_rx_pid=$!
    info "  reverse: $STRESS_N guest receivers spawned via single ssh (pid=$guest_rx_pid)"
fi

info "sleeping ${SERVER_GRACE_SECS}s for receivers to bind..."
sleep "$SERVER_GRACE_SECS"

# Phase 2: fan-out senders on both sides in parallel.
t0=$(date +%s)
if [ "$run_rev" -eq 1 ]; then
    start_local_reverse_senders
    info "  reverse: $STRESS_N linux senders spawned"
fi

guest_tx_pid=""
if [ "$run_fwd" -eq 1 ]; then
    ps_tx=$(gen_ps_tx)
    _guest_ps "$ps_tx" > "$LOGDIR/guest_fwd_tx.log" 2>&1 &
    guest_tx_pid=$!
    info "  forward: $STRESS_N guest senders spawned via single ssh (pid=$guest_tx_pid)"
fi

info "waiting for completion..."

# Wait for local children.
local_fail=0
for i in "${!LOCAL_PIDS[@]}"; do
    wait "${LOCAL_PIDS[$i]}"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        err "  ${LOCAL_NAMES[$i]} exit rc=$rc"
        local_fail=$((local_fail + 1))
    fi
done

# Wait for the two guest-side ssh sessions.
[ -n "$guest_rx_pid" ] && { wait "$guest_rx_pid" || err "  guest reverse-rx ssh exit rc=$?"; }
[ -n "$guest_tx_pid" ] && { wait "$guest_tx_pid" || err "  guest forward-tx ssh exit rc=$?"; }

t1=$(date +%s)
elapsed=$((t1 - t0))

# --- pull guest-side per-connection logs ---------------------------------
if [ "$run_rev" -eq 1 ]; then
    scp "${_guest_scp_opts[@]}" "${_guest_ssh_host}:${GUEST_STRESS_DIR//\\//}/rev_*.log" "$LOGDIR/" >/dev/null 2>&1 || true
fi
if [ "$run_fwd" -eq 1 ]; then
    scp "${_guest_scp_opts[@]}" "${_guest_ssh_host}:${GUEST_STRESS_DIR//\\//}/fwd_*.log" "$LOGDIR/" >/dev/null 2>&1 || true
fi

# Strip CRLF from every log so grep / awk see LF.
for f in "$LOGDIR"/*.log; do
    [ -f "$f" ] || continue
    tr -d '\r' < "$f" > "$f.tmp" && mv "$f.tmp" "$f"
done

# Per-connection pass/fail — vsock_perf prints "rx performance:" or
# "tx performance:" only on a clean run.  A missing log or a log
# without that line counts as a failure.
count_missing_perf() {
    local prefix="$1" side="$2" n="$3"
    local i log fail=0
    for i in $(seq 0 $((n - 1))); do
        log="$LOGDIR/${prefix}_${i}_${side}.log"
        if [ ! -s "$log" ] || ! grep -q "${side} performance:" "$log"; then
            fail=$((fail + 1))
        fi
    done
    echo "$fail"
}
guest_rx_fail=0
guest_tx_fail=0
[ "$run_rev" -eq 1 ] && guest_rx_fail=$(count_missing_perf rev rx "$STRESS_N")
[ "$run_fwd" -eq 1 ] && guest_tx_fail=$(count_missing_perf fwd tx "$STRESS_N")

# --- driver sanity on the guest -----------------------------------------
srv_state=$(_guest_ssh 'sc query VirtioSocket' 2>/dev/null \
    | tr -d '\r' | awk -F: '/STATE/ {gsub(/^[ \t]+|[ \t]+$/, "", $2); print $2; exit}')
[ -n "$srv_state" ] || srv_state='<unknown>'

# --- aggregate throughput ----------------------------------------------
sum_gbps() {
    local glob="$1"
    awk '
        /performance:/ {
            for (i = 1; i <= NF; i++) if ($(i-1) == "performance:") sum += $i + 0
        }
        END { printf "%.2f", sum }
    ' $glob 2>/dev/null
}
fwd_rx_sum=$(sum_gbps "$LOGDIR/fwd_*_rx.log")
fwd_tx_sum=$(sum_gbps "$LOGDIR/fwd_*_tx.log")
rev_rx_sum=$(sum_gbps "$LOGDIR/rev_*_rx.log")
rev_tx_sum=$(sum_gbps "$LOGDIR/rev_*_tx.log")

# --- report -------------------------------------------------------------
total_tx_fail=$((local_fail + guest_tx_fail))
total_rx_fail=$((guest_rx_fail))
# local_fail is really a mix (rx forward + tx reverse); already counted above.

echo
info "=== stress summary ==="
printf '  connections/dir : %d\n' "$STRESS_N"
printf '  bytes/conn      : %s\n' "$STRESS_BYTES"
printf '  buf-size        : %s\n' "$STRESS_BUF"
printf '  wall time       : %d s\n' "$elapsed"
printf '  local failures  : %d / %d\n' "$local_fail" "${#LOCAL_PIDS[@]}"
printf '  guest-rx fails  : %d / %d\n' "$guest_rx_fail" "$([ "$run_rev" -eq 1 ] && echo "$STRESS_N" || echo 0)"
printf '  guest-tx fails  : %d / %d\n' "$guest_tx_fail" "$([ "$run_fwd" -eq 1 ] && echo "$STRESS_N" || echo 0)"
printf '  driver state    : %s\n' "$srv_state"
if [ "$run_fwd" -eq 1 ]; then
    printf '  forward aggregate: RX=%s Gbps  TX=%s Gbps\n' "$fwd_rx_sum" "$fwd_tx_sum"
fi
if [ "$run_rev" -eq 1 ]; then
    printf '  reverse aggregate: RX=%s Gbps  TX=%s Gbps\n' "$rev_rx_sum" "$rev_tx_sum"
fi

# --- exit --------------------------------------------------------------
driver_ok=0
case "$srv_state" in *RUNNING*|*[[:space:]]4*|4*) driver_ok=1 ;; esac
if [ "$local_fail" -eq 0 ] && [ "$guest_rx_fail" -eq 0 ] && [ "$guest_tx_fail" -eq 0 ] && [ "$driver_ok" -eq 1 ]; then
    info "=== PASS ==="
    exit 0
else
    err "=== FAIL (local=$local_fail guest-rx=$guest_rx_fail guest-tx=$guest_tx_fail driver=$srv_state) ==="
    exit 1
fi
