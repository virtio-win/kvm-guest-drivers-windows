#!/usr/bin/env bash
# setup-env.sh — inter-run cleanup on the guest:
#   * Kill any leftover vsock_test.exe / vsock_test_x86.exe.
#   * Remove stale scheduled tasks left by --as-system runs.
#   * Verify the viosock PnP device is present and OK.
#
# This step is called between sweeps to give each fresh sweep a clean
# guest.  It talks to the guest exclusively through _guest_ssh / _guest_ps
# — no reliance on the (now-removed) guest-exec.sh legacy wrapper.

set -euo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

CFG=""
while [ $# -gt 0 ]; do
    case "$1" in
        --config) CFG="$2"; shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 [--config <path>]

Kills stale vsock_test processes on the guest, removes the vsock_rev /
vsock_loopback scheduled tasks left over by --as-system runs, and
verifies that the viosock PnP device is loaded and started.
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

CFG=$(discover_config "$CFG")
guest_load "$CFG"

info "== setup-env: cleaning up guest =="
# Best-effort cleanup; schtasks /delete of a missing task returns
# non-zero, so wrap everything and force exit 0.
_guest_ps '
$ProgressPreference = "SilentlyContinue"
Get-Process vsock_test     -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process vsock_test_x86 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
foreach ($t in "vsock_rev","vsock_loopback") {
    & schtasks /end    /tn $t /f 2>$null | Out-Null
    & schtasks /delete /tn $t /f 2>$null | Out-Null
}
exit 0
'

info "== setup-env: verifying viosock driver =="
_guest_ps '
$ProgressPreference = "SilentlyContinue"
$d = Get-PnpDevice | Where-Object { $_.HardwareID -match "VEN_1AF4.*DEV_1053" }
if (-not $d)            { Write-Error "viosock PnP device not found"; exit 1 }
if ($d.Status -ne "OK") { Write-Error "viosock device status = $($d.Status)"; exit 1 }
"viosock device: $($d.FriendlyName) [$($d.Status)]"
exit 0
' || die "guest driver check failed"

info "== setup-env: done =="
