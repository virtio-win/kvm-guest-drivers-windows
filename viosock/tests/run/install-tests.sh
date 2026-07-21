#!/usr/bin/env bash
# install-tests.sh — copy vsock_test.exe (and its x86 sibling, if
# present) onto the Windows guest.  Destination directory comes from
# the config's `guest_bin_dir` field (default: C:).

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

CFG=""; FROM=""

while [ $# -gt 0 ]; do
    case "$1" in
        --config)   CFG="$2";  shift 2 ;;
        --from)     FROM="$2"; shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 --config <path> --from <dir>

Copies vsock_test.exe (and vsock_test_x86.exe if present) from <dir>
to <guest_bin_dir>/ on the guest (default: C:/).  vsock_test.exe must
be signed (or the guest must permit unsigned .exe launch — the standard
user-mode default).
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

[ -n "$FROM" ] || die "--from <dir> is required"
[ -d "$FROM" ] || die "not a directory: $FROM"
[ -r "$FROM/vsock_test.exe" ] || die "$FROM/vsock_test.exe not found"

CFG=$(discover_config "$CFG")
guest_load "$CFG"

# scp/sftp on Windows accepts forward slashes; normalize the config's
# possibly-backslash-separated bin dir into a form scp is happy with.
scp_dir="${guest_bin_dir//\\//}"

info "== copying vsock_test.exe -> $scp_dir/vsock_test.exe =="
_guest_scp_to "$FROM/vsock_test.exe" "$scp_dir/vsock_test.exe"

# x86 sibling: prefer a file already in $FROM (CI-package layout where
# both binaries are pre-staged into one directory), otherwise fall back
# to the dev-tree layout produced by buildAll.bat — same tree with
# `/x64/` swapped for `/x86/`, and TargetName-changed exe name.
X86_SRC=""
if   [ -f "$FROM/vsock_test_x86.exe" ]; then
    X86_SRC="$FROM/vsock_test_x86.exe"
elif [[ "$FROM" == */x64/* ]] && [ -f "${FROM/\/x64\//\/x86\/}/vsock_test_x86.exe" ]; then
    X86_SRC="${FROM/\/x64\//\/x86\/}/vsock_test_x86.exe"
fi

if [ -n "$X86_SRC" ]; then
    info "== copying $X86_SRC -> $scp_dir/vsock_test_x86.exe =="
    _guest_scp_to "$X86_SRC" "$scp_dir/vsock_test_x86.exe"
else
    warn "vsock_test_x86.exe not found near $FROM (--bits x86 will fail)"
fi

info "install-tests: done"
