#!/usr/bin/env bash
# prepare-guest.sh — install the publisher cert and enable test-signing
# on the Windows guest.  Reboots the guest if test-signing was just
# enabled (skippable via --skip-reboot).
#
# Flags:
#   --config <path>         Required.  Resolved config with `guest=` etc.
#   --cert-cer <path>       If given, import this .cer into the guest's
#                           LocalMachine\Root and LocalMachine\TrustedPublisher
#                           stores.  If omitted, the cert step is skipped —
#                           appropriate for WHQL-signed drivers.
#   --skip-test-signing     Do not touch `bcdedit /set testsigning`.
#   --skip-reboot           Do not reboot even if test-signing was just
#                           enabled.  The guest still needs a reboot for
#                           the change to take effect — you'll have to do
#                           it externally.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

# Where the guest stages the transient artifacts we push (publisher cert
# + generated PS script).  A single top-of-script constant so future
# migration to a different scratch path is a one-line change.
GUEST_TEMP='C:/Windows/Temp'
GUEST_CER_PATH="$GUEST_TEMP/publisher.cer"
GUEST_PS_PATH="$GUEST_TEMP/prepare-guest.ps1"

CFG=""; CERT_CER=""; SKIP_TS=0; SKIP_REBOOT=0

while [ $# -gt 0 ]; do
    case "$1" in
        --config)             CFG="$2"; shift 2 ;;
        --cert-cer)           CERT_CER="$2"; shift 2 ;;
        --skip-test-signing)  SKIP_TS=1; shift ;;
        --skip-reboot)        SKIP_REBOOT=1; shift ;;
        -h|--help)
            sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

CFG=$(discover_config "$CFG")
guest_load "$CFG"

if [ -n "$CERT_CER" ]; then
    [ -r "$CERT_CER" ] || die "cert not readable: $CERT_CER"
fi

# Verify SSH up front so we fail fast.
ssh "${_guest_ssh_opts[@]}" -o ConnectTimeout=10 "$_guest_ssh_host" 'exit' >/dev/null 2>&1 \
    || die "SSH to $_guest_ssh_host failed — verify ssh_key/guest_ip/guest_user in $CFG and that sshd is running on the guest"

# Compose a one-shot PS script tailored to the requested flags.
PS=$(mktemp)
{
    # Preamble with bash-interpolated variables (double-quoted heredoc).
    cat <<PREAMBLE
\$rebootNeeded = \$false
\$certPath     = '$GUEST_CER_PATH'
PREAMBLE
    if [ -n "$CERT_CER" ]; then
        cat <<'PSCERT'
if (-not (Test-Path $certPath)) { throw "cert not found: $certPath" }
$cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 $certPath
$thumb = $cert.Thumbprint
foreach ($storeName in 'Root','TrustedPublisher') {
    $s = New-Object System.Security.Cryptography.X509Certificates.X509Store($storeName, 'LocalMachine')
    $s.Open('ReadWrite')
    if (-not ($s.Certificates | Where-Object { $_.Thumbprint -eq $thumb })) {
        $s.Add($cert)
        Write-Host ("cert imported into LocalMachine\" + $storeName)
    } else {
        Write-Host ("cert already in LocalMachine\" + $storeName)
    }
    $s.Close()
}
PSCERT
    fi
    # Open inbound TCP for the control-channel ports used by reverse and
    # loopback sweeps.  Idempotent (New-NetFirewallRule -ErrorAction ignored
    # if the rule already exists — we key by -Name).
    cat <<'PSFW'
$fwRule = 'vsock-test-control'
if (-not (Get-NetFirewallRule -Name $fwRule -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name $fwRule -DisplayName 'vsock_test control-channel ports' `
        -Direction Inbound -Protocol TCP -LocalPort 12345,12346 -Action Allow -Profile Any | Out-Null
    Write-Host 'firewall: vsock-test-control rule added (TCP :12345,12346)'
} else {
    Write-Host 'firewall: vsock-test-control rule already present'
}
PSFW
    if [ "$SKIP_TS" -eq 0 ]; then
        cat <<'PSTS'
$out = bcdedit /enum '{current}' 2>&1 | Out-String
if ($out -match 'testsigning\s+Yes') {
    Write-Host 'testsigning: already on'
} else {
    bcdedit /set testsigning on | Out-Null
    Write-Host 'testsigning: enabled (reboot required)'
    $rebootNeeded = $true
}
PSTS
    fi
    echo 'if ($rebootNeeded) { exit 100 } else { exit 0 }'
} > "$PS"

if [ -n "$CERT_CER" ]; then
    _guest_scp_to "$CERT_CER" "$GUEST_CER_PATH" || die "cert upload failed"
fi

_guest_scp_to "$PS" "$GUEST_PS_PATH" || die "ps1 upload failed"
set +e
_guest_ssh "powershell -NoProfile -ExecutionPolicy Bypass -File $GUEST_PS_PATH"
rc=$?
set -e
rm -f "$PS"

case "$rc" in
    0)
        info "prepare-guest: done, no reboot needed"
        ;;
    100)
        info "prepare-guest: test-signing was just enabled, reboot required"
        if [ "$SKIP_REBOOT" -eq 1 ]; then
            warn "--skip-reboot set; test-signing will only take effect after an external reboot"
            exit 0
        fi
        info "rebooting guest..."
        _guest_ssh 'shutdown /r /t 0' >/dev/null 2>&1 || true
        # give the guest a moment to actually shut down before probing
        sleep 15
        info "waiting for guest SSH to come back..."
        for i in $(seq 1 60); do
            if ssh "${_guest_ssh_opts[@]}" -o ConnectTimeout=5 "$_guest_ssh_host" 'exit' >/dev/null 2>&1; then
                info "guest back after ~$((15 + i*5))s"
                exit 0
            fi
            sleep 5
        done
        die "guest did not come back after ~5 minutes"
        ;;
    *)
        die "prepare-guest.ps1 exited with $rc"
        ;;
esac
