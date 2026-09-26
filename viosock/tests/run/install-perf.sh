#!/usr/bin/env bash
# install-perf.sh — copy vsock_perf.exe onto the Windows guest.
# Destination directory comes from the config's `guest_bin_dir`
# (default: C:).  Only the 64-bit build is deployed — the perf harness
# has no x86-specific coverage to justify shipping a second binary.
#
# Intended lifecycle:
#   * Dev bench: rerun after every Windows-side vsock_perf rebuild
#     (agents / manual).
#   * CI: called once per pipeline invocation with --from pointing at
#     the artifact directory that has vsock_perf.exe.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

CFG=""; FROM=""

while [ $# -gt 0 ]; do
    case "$1" in
        --config)  CFG="$2";  shift 2 ;;
        --from)    FROM="$2"; shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 --from <dir> [--config <path>]

Copies vsock_perf.exe from <dir> to <guest_bin_dir>/ on the guest
(default: C:/).

  --from <dir>   Directory holding vsock_perf.exe.
  --config       config file (auto-discovered otherwise).
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

[ -n "$FROM" ] || die "--from <dir> is required"
[ -d "$FROM" ] || die "not a directory: $FROM"
[ -r "$FROM/vsock_perf.exe" ] || die "$FROM/vsock_perf.exe not found"

CFG=$(discover_config "$CFG") || exit $?
guest_load "$CFG"

scp_dir="${guest_bin_dir//\\//}"

info "== copying vsock_perf.exe -> $scp_dir/vsock_perf.exe =="
_guest_scp_to "$FROM/vsock_perf.exe" "$scp_dir/vsock_perf.exe"

info "install-perf: done"
