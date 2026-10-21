#!/usr/bin/env bash
# install-wsk.sh — copy the kernel-mode WSK perf driver (viosockwsk-perf.sys)
# onto the Windows guest and register it as the demand-start kernel service
# $WSK_SERVICE (default wskperf) if it is not there yet.  Destination
# directory comes from the config's `guest_wsk_dir` (default: C:\wsk-test).
#
# The driver is test-signed by its build, so the guest must run with test
# signing on - the same requirement as the viosock driver deploy.
#
# Intended lifecycle: rerun after every rebuild of viosock-wsk-perf; the
# service is stopped first if it is running.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"
# shellcheck source=./_wsk.sh
. "$_here/_wsk.sh"

CFG=""; FROM=""

while [ $# -gt 0 ]; do
    case "$1" in
        --config)  CFG="$2";  shift 2 ;;
        --from)    FROM="$2"; shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 --from <dir> [--config <path>]

Copies viosockwsk-perf.sys from <dir> to <guest_wsk_dir>\\ on the guest
(default: C:\\wsk-test) and creates the kernel service $WSK_SERVICE for it
unless it already exists.

  --from <dir>   Directory holding viosockwsk-perf.sys.
  --config       config file (auto-discovered otherwise).
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

[ -n "$FROM" ] || die "--from <dir> is required"
[ -r "$FROM/viosockwsk-perf.sys" ] || die "$FROM/viosockwsk-perf.sys not found"

CFG=$(discover_config "$CFG") || exit $?
guest_load "$CFG"
wsk_dir=$(config_read "$CFG" guest_wsk_dir)
wsk_dir=${wsk_dir:-'C:\wsk-test'}
scp_dir="${wsk_dir//\\//}"

state=$(wsk_state)
case "$state" in
    ""|1*|*STOPPED*) ;;
    *) info "== $WSK_SERVICE is $state - stopping it =="; wsk_stop || die "cannot stop $WSK_SERVICE" ;;
esac

info "== copying viosockwsk-perf.sys -> $scp_dir/viosockwsk-perf.sys =="
_guest_ssh "if not exist $wsk_dir mkdir $wsk_dir" >/dev/null 2>&1 || true
_guest_scp_to "$FROM/viosockwsk-perf.sys" "$scp_dir/viosockwsk-perf.sys" || die "scp failed"

if [ -z "$state" ]; then
    info "== creating kernel service $WSK_SERVICE =="
    _guest_ssh "sc create $WSK_SERVICE type= kernel start= demand binPath= $wsk_dir\\viosockwsk-perf.sys" \
        | tr -d '\r' | grep -q SUCCESS || die "sc create $WSK_SERVICE failed"
fi

info "install-wsk: done ($WSK_SERVICE: $(wsk_state))"
