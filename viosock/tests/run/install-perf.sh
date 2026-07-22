#!/usr/bin/env bash
# install-perf.sh — copy vsock_perf.exe (and its optional x86 sibling)
# onto the Windows guest. Destination directory comes from the config's
# `guest_bin_dir` (default: C:).
#
# Intended lifecycle:
#   * Dev bench: rerun after every Windows-side vsock_perf rebuild
#     (agents / manual). Cheap — just two scp calls.
#   * CI: called once per pipeline invocation with --from pointing at
#     the artifact directory that has vsock_perf.exe (and typically
#     vsock_perf_x86.exe too, pre-staged).

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

Copies vsock_perf.exe (and vsock_perf_x86.exe if present) from <dir>
to <guest_bin_dir>/ on the guest (default: C:/).

  --from <dir>   Directory holding vsock_perf.exe.  Two layouts are
                 supported:
                   * CI-package layout: both exes already in <dir>.
                   * Dev-tree  layout: <dir> ends in /x64/…/vsock_perf,
                     the x86 sibling is picked up from the
                     …/x86/…/vsock_perf_x86.exe sibling automatically.
  --config       config file (auto-discovered otherwise).
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

[ -n "$FROM" ] || die "--from <dir> is required"
[ -d "$FROM" ] || die "not a directory: $FROM"
[ -r "$FROM/vsock_perf.exe" ] || die "$FROM/vsock_perf.exe not found"

CFG=$(discover_config "$CFG")
guest_load "$CFG"

scp_dir="${guest_bin_dir//\\//}"

info "== copying vsock_perf.exe -> $scp_dir/vsock_perf.exe =="
_guest_scp_to "$FROM/vsock_perf.exe" "$scp_dir/vsock_perf.exe"

# x86 sibling: prefer a file already in $FROM (CI-package layout),
# otherwise fall back to the dev-tree layout produced by
# viosock/tests/vsock_perf/buildAll.bat — same tree with /x64/ swapped
# for /x86/, and TargetName-changed exe name.
X86_SRC=""
if   [ -f "$FROM/vsock_perf_x86.exe" ]; then
    X86_SRC="$FROM/vsock_perf_x86.exe"
elif [[ "$FROM" == */x64/* ]] && [ -f "${FROM/\/x64\//\/x86\/}/vsock_perf_x86.exe" ]; then
    X86_SRC="${FROM/\/x64\//\/x86\/}/vsock_perf_x86.exe"
fi

if [ -n "$X86_SRC" ]; then
    info "== copying $X86_SRC -> $scp_dir/vsock_perf_x86.exe =="
    _guest_scp_to "$X86_SRC" "$scp_dir/vsock_perf_x86.exe"
else
    warn "vsock_perf_x86.exe not found near $FROM (--bits x86 will fail)"
fi

info "install-perf: done"
