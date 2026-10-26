#!/usr/bin/env bash
# install-driver.sh — copy a pre-signed viosock package to the Windows
# guest and install it via pnputil /add-driver /install. When pnputil asks
# for a reboot (the device was running another viosock, e.g. the one from
# the guest tools), the guest is rebooted and the script waits for it; the
# installed driver version is then checked against the INF.
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

# guest_boot_time  - the guest's last boot time as Windows reports it (empty while unreachable)
guest_boot_time() {
    ssh "${_guest_ssh_opts[@]}" -o ConnectTimeout=10 "$_guest_ssh_host" \
        'powershell -NoProfile -Command "(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString(\"o\")"' \
        2>/dev/null | tr -d '\r' | tail -1
}

# reboot_guest  - reboots the guest and waits (up to 10 minutes) until it is back. "Back" means
#   Windows reports a new boot time: sshd keeps answering for a while after `shutdown /r`, so a
#   working ssh connection alone proves nothing.
reboot_guest() {
    local before after i
    before=$(guest_boot_time)
    info "rebooting the guest (booted $before)"
    _guest_ssh 'shutdown /r /t 3 /f' >/dev/null 2>&1 || true
    for i in $(seq 1 60); do
        sleep 10
        after=$(guest_boot_time)
        if [ -n "$after" ] && [ "$after" != "$before" ]; then
            info "guest is back (booted $after)"
            return 0
        fi
    done
    return 1
}

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
# pnputil can print a failure and still exit 0 (seen with an unsigned package: "Failed to add
# driver package: No signature was present in the subject."), so the text is checked as well.
if printf '%s' "$pnputil_out" | grep -q 'Failed to add driver package'; then
    die "pnputil could not add the driver package (see above)"
fi
case "$pnputil_rc" in
    0) ;;
    3010|194)
        # ERROR_SUCCESS_REBOOT_REQUIRED (194 once ssh has cut it to 8 bits): the package is in
        # the DriverStore, but the device keeps the old driver until a reboot, the case when the
        # image already had a viosock (the guest tools ship one).
        info "pnputil: package installed, the guest must reboot for the device to pick it up"
        reboot_guest || die "the guest did not come back from the reboot in 10 minutes"
        ;;
    *)
        if printf '%s' "$pnputil_out" | grep -qE 'up-to-date|Already exists in the system'; then
            info "pnputil: driver already present and up-to-date"
        else
            die "pnputil failed (rc=$pnputil_rc)"
        fi
        ;;
esac

# 4) The device must now run the driver from this package, not whatever the image had (the
#    guest tools ship a viosock too): compare the INF's DriverVer with the installed one.
inf_ver=$(tr -d '\r' < "$PKG/viosock.inf" | sed -n 's/^[[:space:]]*DriverVer[[:space:]]*=[[:space:]]*[^,]*,[[:space:]]*\([0-9.]*\).*/\1/p' | head -1)
if [ -n "$inf_ver" ]; then
    dev_ver=$(_guest_ps '
$d = Get-PnpDevice | Where-Object { $_.HardwareID -match "VEN_1AF4.*DEV_1053" } | Select-Object -First 1
if ($d) { (Get-PnpDeviceProperty -InstanceId $d.InstanceId -KeyName DEVPKEY_Device_DriverVersion).Data }
' 2>/dev/null | tr -d '\r' | tail -1)
    if [ "$dev_ver" = "$inf_ver" ]; then
        info "viosock device runs driver $dev_ver from the package"
    else
        die "viosock device runs driver '${dev_ver:-none}', the package has $inf_ver: the installation did not take"
    fi
else
    warn "cannot read DriverVer from $PKG/viosock.inf; the installed version is not verified"
fi

info "install-driver: done"
