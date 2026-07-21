#!/usr/bin/env bash
# prepare-ci.sh — orchestrator for the CI-side preparation pipeline.
#
# Runs, in order:
#   1. resolve-guest.sh    (if --guest given; skipped if --config given)
#   2. prepare-host.sh     (Linux vsock_test build)
#   3. prepare-guest.sh    (cert + testsigning, with reboot)
#   4. install-driver.sh   (scp package + pnputil /add-driver /install)
#   5. install-tests.sh    (scp vsock_test.exe)
#
# Every stage can be skipped individually.  Fails fast on the first
# non-zero exit.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

GUEST=""; CFG=""; CFG_OUT=""
SSH_KEY=""; SSH_USER=""
PKG=""
CERT_CER=""
SKIP_CERT=0; SKIP_TS=0; SKIP_REBOOT=0
SKIP_HOST=0; SKIP_GUEST=0; SKIP_DRIVER=0; SKIP_TESTS=0
WHQL=0
LINUX_VER=""

while [ $# -gt 0 ]; do
    case "$1" in
        --guest)             GUEST="$2";     shift 2 ;;
        --config)            CFG="$2";       shift 2 ;;
        --config-out)        CFG_OUT="$2";   shift 2 ;;
        --ssh-key)           SSH_KEY="$2";   shift 2 ;;
        --ssh-user)          SSH_USER="$2";  shift 2 ;;
        --package)           PKG="$2";       shift 2 ;;
        --cert-cer)          CERT_CER="$2";  shift 2 ;;
        --skip-cert)         SKIP_CERT=1;    shift ;;
        --skip-test-signing) SKIP_TS=1;      shift ;;
        --skip-reboot)       SKIP_REBOOT=1;  shift ;;
        --skip-host-prep)    SKIP_HOST=1;    shift ;;
        --skip-guest-prep)   SKIP_GUEST=1;   shift ;;
        --skip-driver)       SKIP_DRIVER=1;  shift ;;
        --skip-tests)        SKIP_TESTS=1;   shift ;;
        --whql)              WHQL=1;         shift ;;
        --linux-ver)         LINUX_VER="$2"; shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 (--guest <domain> | --config <path>) --package <dir> [options]

Runs the CI preparation pipeline: resolve-guest -> prepare-host -> prepare-guest
-> install-driver -> install-tests.

Guest identity (exactly one of):
  --guest <domain>       libvirt domain name; will be resolved via virsh.
                         Requires --ssh-key.
  --config <path>        pre-resolved config file.

Artifact drop:
  --package <dir>        Directory with viosock.inf/sys/cat, viosocklib_*.dll,
                         viosockwspsvc.exe, vsock_test.exe (and optionally its
                         x86 sibling), VirtIOTestCert.cer.  Required unless
                         --skip-driver AND --skip-tests are both set.

Config bookkeeping (only when --guest is used):
  --config-out <path>    Where to save the resolved config. Defaults to
                         /tmp/vsock-ci/<guest>.config.
  --ssh-key <path>       Written into the emitted config; downstream scripts
                         use it to reach the guest. Required with --guest.
  --ssh-user <name>      SSH login (default: Administrator).

Prepare-guest tuning:
  --cert-cer <path>      Publisher cert to import into the guest.  Defaults
                         to <package>/VirtIOTestCert.cer when --package is
                         given and --skip-cert is not.
  --skip-cert            Do not install any cert on the guest.
  --skip-test-signing    Do not enable bcdedit testsigning.
  --skip-reboot          Do not reboot the guest even if testsigning was
                         just enabled.
  --whql                 Shorthand for --skip-cert + --skip-test-signing.
                         Appropriate when the driver is WHQL-signed.

Per-stage skips:
  --skip-host-prep       Skip prepare-host (Linux vsock_test build).
  --skip-guest-prep      Skip prepare-guest (cert + testsigning).
  --skip-driver          Skip install-driver.
  --skip-tests           Skip install-tests.

Prepare-host tuning:
  --linux-ver <ver>      Linux kernel version to build vsock_test from
                         (default: hard-coded in prepare-host.sh).
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

# WHQL is a preset — collapses to skip-cert + skip-test-signing.
if [ "$WHQL" -eq 1 ]; then
    SKIP_CERT=1
    SKIP_TS=1
fi

# --guest xor --config
if [ -n "$GUEST" ] && [ -n "$CFG" ]; then
    die "--guest and --config are mutually exclusive"
fi
if [ -z "$GUEST" ] && [ -z "$CFG" ]; then
    die "one of --guest or --config is required"
fi

# Package required unless both install stages are skipped.
if [ "$SKIP_DRIVER" -eq 0 ] || [ "$SKIP_TESTS" -eq 0 ]; then
    [ -n "$PKG" ] || die "--package <dir> is required (or pass --skip-driver + --skip-tests)"
    [ -d "$PKG" ] || die "not a directory: $PKG"
fi

# Cert defaults: when --skip-cert not set and --cert-cer not given,
# fall back to $PKG/VirtIOTestCert.cer if it's there.
if [ "$SKIP_CERT" -eq 0 ] && [ -z "$CERT_CER" ] && [ -n "$PKG" ] && [ -r "$PKG/VirtIOTestCert.cer" ]; then
    CERT_CER="$PKG/VirtIOTestCert.cer"
fi

# --- 1. resolve-guest (if --guest) ----------------------------------------
if [ -n "$GUEST" ]; then
    [ -n "$SSH_KEY" ] || die "--ssh-key is required together with --guest"
    if [ -z "$CFG_OUT" ]; then
        CFG_OUT="/tmp/vsock-ci/${GUEST}.config"
    fi
    info "==== resolve-guest ($GUEST → $CFG_OUT) ===="
    resolve_args=(--guest "$GUEST" --ssh-key "$SSH_KEY" --out "$CFG_OUT")
    [ -n "$SSH_USER" ] && resolve_args+=(--ssh-user "$SSH_USER")
    "$_here/resolve-guest.sh" "${resolve_args[@]}" || die "resolve-guest failed"
    CFG="$CFG_OUT"
fi

# From here on everything reads from a resolved config file.
[ -r "$CFG" ] || die "config not readable: $CFG"

# --- 2. prepare-host -----------------------------------------------------
if [ "$SKIP_HOST" -eq 0 ]; then
    echo; info "==== prepare-host ===="
    args=(--config "$CFG")
    [ -n "$LINUX_VER" ] && args+=(--linux-ver "$LINUX_VER")
    "$_here/prepare-host.sh" "${args[@]}" || die "prepare-host failed"
fi

# --- 3. prepare-guest ----------------------------------------------------
if [ "$SKIP_GUEST" -eq 0 ]; then
    echo; info "==== prepare-guest ===="
    args=(--config "$CFG")
    [ "$SKIP_CERT" -eq 0 ] && [ -n "$CERT_CER" ] && args+=(--cert-cer "$CERT_CER")
    [ "$SKIP_TS" -eq 1 ]   && args+=(--skip-test-signing)
    [ "$SKIP_REBOOT" -eq 1 ] && args+=(--skip-reboot)
    "$_here/prepare-guest.sh" "${args[@]}" || die "prepare-guest failed"
fi

# --- 4. install-driver ---------------------------------------------------
if [ "$SKIP_DRIVER" -eq 0 ]; then
    echo; info "==== install-driver ===="
    "$_here/install-driver.sh" --config "$CFG" --package "$PKG" || die "install-driver failed"
fi

# --- 5. install-tests ----------------------------------------------------
if [ "$SKIP_TESTS" -eq 0 ]; then
    echo; info "==== install-tests ===="
    "$_here/install-tests.sh" --config "$CFG" --from "$PKG" || die "install-tests failed"
fi

echo
info "==== prepare-ci: all stages green ===="
info "config: $CFG"
