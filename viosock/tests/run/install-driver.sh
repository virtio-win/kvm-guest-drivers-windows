#!/usr/bin/env bash
# install-driver.sh — copy a pre-signed viosock package to the Windows
# guest and install it via pnputil /add-driver /install.
#
# viosock.inf has CopyFiles directives that place viosocklib.dll into
# System32 (and its x86 sibling into SysWOW64), so pnputil handles the
# userland DLL placement too — we just stage the whole package alongside
# the .inf and let pnputil do the rest.
#
# CI signs .sys/.cat/.dll on the build host; we never ship signtool.exe
# to the guest.  The publisher cert must already be trusted on the guest
# (prepare-guest.sh puts VirtIOTestCert.cer into Root+TrustedPublisher).

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

CFG=""; PKG=""; DEST='C:/viosock-pkg'

while [ $# -gt 0 ]; do
    case "$1" in
        --config)   CFG="$2";  shift 2 ;;
        --package)  PKG="$2";  shift 2 ;;
        --dest)     DEST="$2"; shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 --config <path> --package <dir> [--dest <win-path>]

Copies pre-signed viosock package files to <win-path> on the guest,
places viosocklib DLLs into System32/SysWOW64, then runs
pnputil /add-driver /install.

  --package <dir>    Directory with viosock.inf/sys/dll/cat.
                     Must be pre-signed.
  --dest <win-path>  Staging path on the guest (default: C:/viosock-pkg).
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

[ -n "$PKG" ] || die "--package <dir> is required"
[ -d "$PKG" ] || die "not a directory: $PKG"
[ -r "$PKG/viosock.inf" ] || die "$PKG/viosock.inf not found"
[ -r "$PKG/viosock.sys" ] || die "$PKG/viosock.sys not found"

CFG=$(discover_config "$CFG")
guest_load "$CFG"

# 1) Stage directory on guest.
info "== staging package to ${_guest_ssh_host}:${DEST} =="
_guest_ssh "powershell -NoProfile -Command \"New-Item -ItemType Directory -Force -Path '$DEST' | Out-Null\"" \
    || die "cannot create $DEST on guest"

# 2) Copy every file in the package to $DEST.
for f in "$PKG"/*; do
    [ -f "$f" ] || continue
    _guest_scp_to "$f" "$DEST/$(basename "$f")" || die "scp failed: $f"
done

# 3) Install/update the driver package.  pnputil reads viosock.inf and
#    places both viosock.sys (DriverStore) and viosocklib.dll (System32/
#    SysWOW64) per the INF's CopyFiles/AddReg directives.
#
# pnputil returns a non-zero exit code when the driver is already present
# and up-to-date ("Added driver packages: 0") — an idempotent re-install
# is a benign no-op.  Treat that output as success.
info "== pnputil /add-driver $DEST/viosock.inf /install =="
set +e
pnputil_out=$(_guest_ssh "pnputil /add-driver \"$DEST\\viosock.inf\" /install" 2>&1)
pnputil_rc=$?
set -e
printf '%s\n' "$pnputil_out"
if [ "$pnputil_rc" -ne 0 ]; then
    if printf '%s' "$pnputil_out" | grep -qE 'up-to-date|Already exists in the system'; then
        info "pnputil: driver already present and up-to-date"
    else
        die "pnputil failed (rc=$pnputil_rc)"
    fi
fi

info "install-driver: done"
