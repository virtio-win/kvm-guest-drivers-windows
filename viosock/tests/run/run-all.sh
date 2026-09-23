#!/usr/bin/env bash
# Orchestrator for the sweep stage of the pipeline:
#   setup-env -> forward -> reverse -> loopback
#
# Deploy is prepare-ci.sh's job now (which itself calls install-driver
# and install-tests).  run-all is the sweep-only entry point — assumes
# driver + test binaries are already on the guest.  Individual sweeps
# can still be called directly; run-all just chains them and aggregates
# the exit code.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

CFG=""; SKIP_SETUP=0
ONLY=""; VARIANT=""; BITS="x64"; JUNIT_DIR=""; LOGDIR=""; AS_SYSTEM=0

while [ $# -gt 0 ]; do
    case "$1" in
        --config)       CFG="$2";  shift 2 ;;
        --skip-setup)   SKIP_SETUP=1;  shift ;;
        --only)         ONLY="$2"; shift 2 ;;
        --variant)      VARIANT="$2"; shift 2 ;;
        --bits)         BITS="$2"; shift 2 ;;
        --x86)          BITS="x86"; shift ;;
        --junit-dir)    JUNIT_DIR="$2"; shift 2 ;;
        --logdir)       LOGDIR="$2"; shift 2 ;;
        --as-system)    AS_SYSTEM=1; shift ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 [--config <cfg>] [--skip-setup]
          [--only forward,reverse,loopback]
          [--variant <name>]
          [--bits x64|x86 | --x86]
          [--junit-dir <dir>] [--logdir <dir>]
          [--as-system]

Runs the sweep pipeline in order:
    setup-env -> forward -> reverse -> loopback

--skip-setup       Do not touch schtasks / vsock_test.exe cleanup.
--only <list>      Comma-separated subset: forward, reverse, loopback.
                   (default: all three)
--variant <name>   Restrict to rows of that variant in every list file.
                   (default: run every variant declared in each list)
--bits x64|x86     CPU bit-width for the guest binary (default: x64).
--x86              Shorthand for --bits x86.
--junit-dir <dir>  Emit JUnit XML per sweep at
                     <dir>/{forward,reverse,loopback}.xml.
--logdir <dir>     Shared log dir for all sweeps
                     (default: /tmp/vsock-run-<pid>).  Only failed-test
                     logs are kept.
--as-system        Forward to run-reverse/run-loopback: launch the Windows
                   server via schtasks /ru SYSTEM instead of a background
                   ssh session as the configured guest user.

To install driver + tests first, use prepare-ci.sh (which chains
resolve-guest -> prepare-host -> prepare-guest -> install-driver ->
install-tests -> setup-env), then run-all here.
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

# Which sweeps to run.
run_forward=1; run_reverse=1; run_loopback=1
if [ -n "$ONLY" ]; then
    run_forward=0; run_reverse=0; run_loopback=0
    IFS=',' read -ra parts <<< "$ONLY"
    for p in "${parts[@]}"; do
        case "$p" in
            forward)  run_forward=1 ;;
            reverse)  run_reverse=1 ;;
            loopback) run_loopback=1 ;;
            *) die "unknown --only stage: $p" ;;
        esac
    done
fi

if [ -z "$LOGDIR" ]; then LOGDIR="/tmp/vsock-run-$$"; fi
mkdir -p "$LOGDIR"
[ -n "$JUNIT_DIR" ] && mkdir -p "$JUNIT_DIR"

rc=0

if [ "$SKIP_SETUP" -eq 0 ]; then
    "$_here/setup-env.sh" --config "$CFG" || die "setup-env failed"
fi

# Assemble common flags forwarded to every sweep script.
common=(--config "$CFG" --bits "$BITS" --logdir "$LOGDIR")
[ -n "$VARIANT" ]     && common+=(--variant "$VARIANT")
[ "$AS_SYSTEM" -eq 1 ] && common+=(--as-system)

_junit_arg() {
    local suite="$1"
    [ -n "$JUNIT_DIR" ] && printf -- '--junit\n%s\n' "$JUNIT_DIR/$suite.xml"
}

if [ "$run_forward" -eq 1 ]; then
    echo; info "==== FORWARD ===="
    mapfile -t junit_args < <(_junit_arg forward)
    "$_here/run-forward.sh" "${common[@]}" "${junit_args[@]}" || rc=1
fi

if [ "$run_reverse" -eq 1 ]; then
    echo; info "==== REVERSE ===="
    mapfile -t junit_args < <(_junit_arg reverse)
    "$_here/run-reverse.sh" "${common[@]}" "${junit_args[@]}" || rc=1
fi

if [ "$run_loopback" -eq 1 ]; then
    echo; info "==== LOOPBACK ===="
    mapfile -t junit_args < <(_junit_arg loopback)
    "$_here/run-loopback.sh" "${common[@]}" "${junit_args[@]}" || rc=1
fi

echo
if [ "$rc" -eq 0 ]; then
    info "== run-all: all sweeps green =="
else
    err "== run-all: at least one sweep failed =="
fi
exit "$rc"
