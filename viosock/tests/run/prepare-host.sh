#!/usr/bin/env bash
# prepare-host.sh — build the Linux vsock_test binary from a pinned
# kernel tarball on this (libvirt) host.
#
# Idempotent: skips download + build when <out-dir>/vsock_test already
# exists and its recorded version matches <linux-ver>.
#
# Fetch order:
#   1. --tarball-url override, if given.
#   2. Otherwise https://cdn.kernel.org/pub/linux/kernel/v<major>.x/linux-<ver>.tar.xz.
#   3. On curl failure, the value of `linux_tarball_mirror` from the config
#      (a directory URL — the tarball name is appended).

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

DEFAULT_VER=6.19.8
DEFAULT_OUT=/opt/vsock-test

CFG=""; VER=""; OUT_DIR=""; TARBALL_URL=""

while [ $# -gt 0 ]; do
    case "$1" in
        --config)       CFG="$2";         shift 2 ;;
        --linux-ver)    VER="$2";         shift 2 ;;
        --out-dir)      OUT_DIR="$2";     shift 2 ;;
        --tarball-url)  TARBALL_URL="$2"; shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 [--config <path>] [--linux-ver <ver>] [--out-dir <dir>] [--tarball-url <url>]

Builds vsock_test from tools/testing/vsock of a pinned Linux kernel release.
Idempotent — skips when <out-dir>/vsock_test already exists at <ver>.

  --linux-ver     Kernel version to build (default: $DEFAULT_VER).
  --out-dir       Install prefix (default: $DEFAULT_OUT).
                  Final binary lives at <out-dir>/vsock_test.
  --tarball-url   Full URL override for the tarball.
  --config        Optional; only consulted for 'linux_tarball_mirror'.
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

[ -n "$VER" ]     || VER="$DEFAULT_VER"
[ -n "$OUT_DIR" ] || OUT_DIR="$DEFAULT_OUT"

# Prereq check
for cmd in curl tar make gcc; do
    command -v "$cmd" >/dev/null || die "missing prereq: $cmd"
done

# Open the control-channel ports on the libvirt firewall zone so guests
# on an isolated / host-only network can reach us on TCP.  Idempotent —
# firewall-cmd silently keeps duplicate ports.  Runs *before* the
# already-built short-circuit so re-invocations still fix the firewall.
CTRL_PORTS=(12345/tcp 12346/tcp)
if command -v firewall-cmd >/dev/null 2>&1 && firewall-cmd --state >/dev/null 2>&1; then
    zone=$(firewall-cmd --get-zone-of-interface=virbr1 2>/dev/null || true)
    [ -z "$zone" ] && zone=libvirt
    info "opening control ports ${CTRL_PORTS[*]} in firewalld zone '$zone'"
    for p in "${CTRL_PORTS[@]}"; do
        firewall-cmd --zone="$zone" --add-port="$p" >/dev/null 2>&1 || true
        firewall-cmd --zone="$zone" --add-port="$p" --permanent >/dev/null 2>&1 || true
    done
fi

# Idempotent short-circuit
STAMP="$OUT_DIR/.linux-ver"
if [ -f "$OUT_DIR/vsock_test" ] && [ "$(cat "$STAMP" 2>/dev/null)" = "$VER" ]; then
    info "prepare-host: $OUT_DIR/vsock_test already built for Linux $VER"
    exit 0
fi

mirror=""
if [ -n "$CFG" ]; then
    [ -r "$CFG" ] || die "config not readable: $CFG"
    mirror=$(config_read "$CFG" linux_tarball_mirror)
fi

VER_MAJOR=$(printf '%s' "$VER" | cut -d. -f1)
TARBALL="linux-${VER}.tar.xz"
DEFAULT_URL="https://cdn.kernel.org/pub/linux/kernel/v${VER_MAJOR}.x/${TARBALL}"
URL="${TARBALL_URL:-$DEFAULT_URL}"

mkdir -p "$OUT_DIR/src"
cd "$OUT_DIR/src"

if [ ! -f "$TARBALL" ]; then
    info "downloading $URL"
    if ! curl -fL --retry 3 -o "$TARBALL" "$URL"; then
        rm -f "$TARBALL"
        if [ -n "$mirror" ]; then
            warn "kernel.org fetch failed, trying mirror: $mirror/$TARBALL"
            curl -fL --retry 3 -o "$TARBALL" "$mirror/$TARBALL" || die "tarball download failed"
        else
            die "tarball download failed (no mirror configured; set linux_tarball_mirror in $CFG or pass --tarball-url)"
        fi
    fi
fi

# Extract only the tools/ subtree — that's what tools/testing/vsock needs
# (tools/include and tools/build among others).
info "extracting linux-$VER/tools/"
rm -rf "linux-$VER"
tar -xJf "$TARBALL" "linux-$VER/tools" || die "tarball extract failed"

info "building"
make -C "linux-$VER/tools/testing/vsock" -s || die "vsock_test build failed"

install -Dm755 "linux-$VER/tools/testing/vsock/vsock_test" "$OUT_DIR/vsock_test"
printf '%s\n' "$VER" > "$STAMP"

info "prepare-host: built $OUT_DIR/vsock_test for Linux $VER"
"$OUT_DIR/vsock_test" --help 2>&1 | head -3 || true
