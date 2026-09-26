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

CFG=""; PKG=""; DEST='C:/viosock-pkg'; REINSTALL=0

while [ $# -gt 0 ]; do
    case "$1" in
        --config)     CFG="$2";  shift 2 ;;
        --package)    PKG="$2";  shift 2 ;;
        --dest)       DEST="$2"; shift 2 ;;
        --reinstall)  REINSTALL=1; shift ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 --config <path> --package <dir> [--dest <win-path>] [--reinstall]

Copies pre-signed viosock package files to <win-path> on the guest,
places viosocklib DLLs into System32/SysWOW64, then runs
pnputil /add-driver /install.

  --package <dir>    Directory with viosock.inf/sys/dll/cat.
                     Must be pre-signed.
  --dest <win-path>  Staging path on the guest (default: C:/viosock-pkg).
  --reinstall        Before /add-driver, run pnputil /delete-driver
                     /uninstall /force on every DriverStore entry whose
                     Original Name is viosock.inf.  Off by default; only
                     needed when the .sys bytes changed but viosock.inf's
                     DriverVer did NOT, since pnputil /add-driver dedupes
                     by INF metadata and would otherwise report the new
                     package as "already up-to-date" and leave the old
                     DriverStore copy bound to the device.
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

[ -n "$PKG" ] || die "--package <dir> is required"
[ -d "$PKG" ] || die "not a directory: $PKG"
[ -r "$PKG/viosock.inf" ] || die "$PKG/viosock.inf not found"
[ -r "$PKG/viosock.sys" ] || die "$PKG/viosock.sys not found"

CFG=$(discover_config "$CFG") || exit $?
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

# 3a) Optional pre-purge: opt-in via --reinstall.  See the flag's help
#     text above for why this exists.
if [ "$REINSTALL" -eq 1 ]; then
    info "== --reinstall: enumerate existing viosock oem*.inf =="
    oem_list=$(_guest_ssh "pnputil /enum-drivers" 2>/dev/null | tr -d '\r' | awk '
        /^Published Name:/ { pub = $3 }
        /^Original Name:[[:space:]]+viosock\.inf/ { print pub }
    ')
    if [ -z "$oem_list" ]; then
        info "no existing viosock package in DriverStore"
    else
        while IFS= read -r oem; do
            [ -n "$oem" ] || continue
            info "== pnputil /delete-driver $oem /uninstall /force =="
            _guest_ssh "pnputil /delete-driver $oem /uninstall /force" \
                || warn "pnputil /delete-driver $oem returned non-zero (continuing)"
        done <<<"$oem_list"
    fi
fi

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
