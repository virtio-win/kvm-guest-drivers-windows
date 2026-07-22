#!/usr/bin/env bash
# prepare-perf.sh — one-shot vsock_perf setup: host + guest in a single
# call.  Thin orchestrator over:
#
#   prepare-perf-host.sh   (Linux build / install)
#   install-perf.sh        (Windows scp to guest)
#
# When it's useful:
#   * First-time dev-bench setup.
#   * CI pipeline: one command instead of two, both halves invoked with
#     the same package parameters.
#
# When it's *not* useful:
#   * Ordinary dev redeploy after a Windows-side rebuild — call
#     install-perf.sh directly (much cheaper: no `make`).
#   * Host-only refresh after bumping the pinned kernel version — call
#     prepare-perf-host.sh directly.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

CFG=""; VER=""; OUT_DIR=""; FROM=""; LINUX_BIN=""; SKIP_HOST=0; SKIP_GUEST=0

while [ $# -gt 0 ]; do
    case "$1" in
        --config)      CFG="$2";       shift 2 ;;
        --linux-ver)   VER="$2";       shift 2 ;;
        --out-dir)     OUT_DIR="$2";   shift 2 ;;
        --from)        FROM="$2";      shift 2 ;;
        --linux-bin)   LINUX_BIN="$2"; shift 2 ;;
        --skip-host)   SKIP_HOST=1;    shift ;;
        --skip-guest)  SKIP_GUEST=1;   shift ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 [--config <path>] [--linux-ver <ver>] [--out-dir <dir>]
          [--from <dir>] [--linux-bin <file>]
          [--skip-host] [--skip-guest]

Orchestrates prepare-perf-host.sh + install-perf.sh in one call.

Host-side flags forwarded to prepare-perf-host.sh:
  --linux-bin, --linux-ver, --out-dir

Guest-side flags forwarded to install-perf.sh:
  --from, --config

Skips:
  --skip-host    Do not touch the Linux binary.
  --skip-guest   Do not push the Windows binary to the guest.

For iterative dev work (redeploy after a Windows rebuild), invoke
install-perf.sh directly — no need to go through this orchestrator.
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

# --- 1. Host --------------------------------------------------------------
if [ "$SKIP_HOST" -eq 0 ]; then
    host_args=()
    [ -n "$LINUX_BIN" ] && host_args+=(--linux-bin "$LINUX_BIN")
    [ -n "$VER" ]       && host_args+=(--linux-ver "$VER")
    [ -n "$OUT_DIR" ]   && host_args+=(--out-dir  "$OUT_DIR")
    "$_here/prepare-perf-host.sh" "${host_args[@]}" || die "prepare-perf-host failed"
fi

# --- 2. Guest -------------------------------------------------------------
if [ "$SKIP_GUEST" -eq 0 ]; then
    [ -n "$FROM" ] || die "--from <dir> is required (or pass --skip-guest)"
    guest_args=(--from "$FROM")
    [ -n "$CFG" ] && guest_args+=(--config "$CFG")
    "$_here/install-perf.sh" "${guest_args[@]}" || die "install-perf failed"
fi

info "prepare-perf: done"
