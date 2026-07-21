#!/usr/bin/env bash
# prepare-perf-host.sh — build (or install) the Linux vsock_perf binary.
#
# Two paths:
#   * Dev  — reuse the kernel sources prepare-host.sh already extracted
#            under $OUT_DIR/src/linux-<ver>/, run `make vsock_perf`,
#            install to $OUT_DIR/vsock_perf. Idempotent (skips when the
#            stamp matches). vsock_test/prepare-host is a prerequisite;
#            we deliberately don't re-fetch the ~140 MB tarball here.
#   * CI   — pass --linux-bin <file> with a pre-built binary and the
#            `make` step is skipped: the file is copied verbatim to
#            $OUT_DIR/vsock_perf.
#
# Intended lifecycle:
#   * Dev bench: run once when setting up the host, or after bumping
#     the pinned kernel version.
#   * CI: run every pipeline invocation with --linux-bin pointing at
#     the artifact staged by an earlier pipeline step.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

DEFAULT_VER=6.19.8
DEFAULT_OUT=/opt/vsock-test

VER=""; OUT_DIR=""; LINUX_BIN=""

while [ $# -gt 0 ]; do
    case "$1" in
        --linux-ver)  VER="$2";       shift 2 ;;
        --out-dir)    OUT_DIR="$2";   shift 2 ;;
        --linux-bin)  LINUX_BIN="$2"; shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 [--linux-ver <ver>] [--out-dir <dir>] [--linux-bin <file>]

Prepares the host side of vsock_perf.

  --linux-bin <f>   Pre-built Linux vsock_perf; skips 'make', copies <f>
                    to <out-dir>/vsock_perf. Meant for CI.
  --linux-ver       Kernel version to build (default: $DEFAULT_VER).
                    Only consulted when --linux-bin is not passed.
  --out-dir         Install prefix (default: $DEFAULT_OUT).
                    Final binary lives at <out-dir>/vsock_perf.

Depends on prepare-host.sh having already extracted the kernel sources
into <out-dir>/src/linux-<ver>/. If those are missing and --linux-bin
is not given, the script tells you to run prepare-host.sh first.
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

[ -n "$VER" ]     || VER="$DEFAULT_VER"
[ -n "$OUT_DIR" ] || OUT_DIR="$DEFAULT_OUT"

if [ -n "$LINUX_BIN" ]; then
    # CI path: just copy the pre-built binary into place.
    [ -r "$LINUX_BIN" ] || die "--linux-bin not readable: $LINUX_BIN"
    info "prepare-perf-host: installing $LINUX_BIN -> $OUT_DIR/vsock_perf"
    install -Dm755 "$LINUX_BIN" "$OUT_DIR/vsock_perf"
else
    # Dev path: build against sources prepare-host.sh already extracted.
    STAMP="$OUT_DIR/.linux-ver"
    if [ -f "$OUT_DIR/vsock_perf" ] && [ "$(cat "$STAMP" 2>/dev/null)" = "$VER" ]; then
        info "prepare-perf-host: $OUT_DIR/vsock_perf already built for Linux $VER"
        exit 0
    fi
    SRC="$OUT_DIR/src/linux-$VER/tools/testing/vsock"
    [ -d "$SRC" ] || die "kernel sources not found at $SRC — run prepare-host.sh first (or pass --linux-bin for the CI path)"
    for cmd in make gcc; do
        command -v "$cmd" >/dev/null || die "missing prereq: $cmd"
    done
    info "building vsock_perf in $SRC"
    make -C "$SRC" -s vsock_perf || die "vsock_perf build failed"
    install -Dm755 "$SRC/vsock_perf" "$OUT_DIR/vsock_perf"
    # Refresh the version stamp only if it wasn't already set
    # (prepare-host writes it too — same file — so we don't overwrite
    # something newer).
    [ -f "$STAMP" ] || printf '%s\n' "$VER" > "$STAMP"
    info "prepare-perf-host: installed $OUT_DIR/vsock_perf"
    "$OUT_DIR/vsock_perf" --help 2>&1 | head -2 || true
fi
