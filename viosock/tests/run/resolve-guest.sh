#!/usr/bin/env bash
# resolve-guest.sh — probe a running libvirt guest and emit a minimal
# config file for the rest of the harness. Runs on the libvirt host.
#
# Inputs:
#   --guest <domain>       (required)  libvirt domain name.
#   --ssh-key <path>       Path to the SSH private key used to reach the
#                          guest. Written into the config verbatim.
#                          Optional here — but downstream scripts require it.
#   --ssh-user <name>      SSH login user on the guest (default: Administrator).
#   --out <path>           write the config to a file (default: stdout).
#
# Output config (in key=value format that config_read understands):
#   guest_name  = <libvirt domain>
#   guest_cid   = <parsed from `virsh dumpxml`, the <vsock> element>
#   host_cid    = 2                (fixed for Linux vhost-vsock)
#   guest_ip    = <first IPv4 from `virsh domifaddr`, if available>
#   guest_user  = <ssh user>
#   ssh_key     = <path to private key on this host>
#
# There is intentionally no `guest = <alias>` — downstream scripts talk
# directly to <guest_user>@<guest_ip> with <ssh_key>, avoiding any
# mutation of ~/.ssh/config on the runner.
#
# The script exits non-zero if the guest is not running or if the vsock
# CID cannot be found in the domain XML.

set -uo pipefail
_here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=./_lib.sh
. "$_here/_lib.sh"

GUEST=""; SSH_KEY=""; SSH_USER=""; OUT=""

while [ $# -gt 0 ]; do
    case "$1" in
        --guest)      GUEST="$2";     shift 2 ;;
        --ssh-key)    SSH_KEY="$2";   shift 2 ;;
        --ssh-user)   SSH_USER="$2";  shift 2 ;;
        --out)        OUT="$2";       shift 2 ;;
        -h|--help)
            cat >&2 <<EOF
Usage: $0 --guest <domain> [--ssh-key <path>] [--ssh-user <name>] [--out <path>]

Probes a running libvirt guest via virsh and emits a harness config.
The domain must be running and have a <vsock> device with a static CID.

  --ssh-key <path>   Written into the config verbatim so downstream
                     scripts can talk to the guest.
  --ssh-user <name>  SSH login (default: Administrator).
EOF
            exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

[ -n "$GUEST" ]    || die "--guest <domain> is required"
: "${SSH_USER:=Administrator}"

command -v virsh >/dev/null || die "virsh not found (must run on the libvirt host)"

# 1) Domain must be running.
state=$(virsh domstate "$GUEST" 2>/dev/null | tr -d '\r' | head -1)
[ -n "$state" ] || die "libvirt domain '$GUEST' not found"
[ "$state" = "running" ] || die "libvirt domain '$GUEST' is not running (state: $state)"

# 2) Guest CID from <vsock ... address='N'/> in the domain XML.
xml=$(virsh dumpxml "$GUEST" 2>/dev/null) || die "virsh dumpxml '$GUEST' failed"
guest_cid=$(printf '%s' "$xml" | awk '
    /<vsock/       { in_vsock = 1 }
    /<\/vsock>/    { in_vsock = 0 }
    in_vsock {
        if (match($0, /address=['"'"'"][0-9]+['"'"'"]/)) {
            s = substr($0, RSTART, RLENGTH)
            gsub(/[^0-9]/, "", s)
            print s
            exit
        }
    }
')
[ -n "$guest_cid" ] || die "cannot find vsock CID in domain '$GUEST' XML"

# 3) Guest IP: first IPv4 from `virsh domifaddr` (best-effort; may be empty
#    if the guest has not published a lease yet or QEMU-agent is off).
guest_ip=$(virsh domifaddr "$GUEST" 2>/dev/null \
    | awk '$4 ~ /^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/ { split($4, a, "/"); print a[1]; exit }')

# 4) Emit config.
emit() {
    cat <<EOF
# resolved by resolve-guest.sh
guest_name  = $GUEST
guest_cid   = $guest_cid
host_cid    = 2
EOF
    [ -n "$guest_ip" ] && printf 'guest_ip    = %s\n' "$guest_ip"
    printf 'guest_user  = %s\n' "$SSH_USER"
    [ -n "$SSH_KEY" ]  && printf 'ssh_key     = %s\n' "$SSH_KEY"
}

if [ -n "$OUT" ]; then
    mkdir -p "$(dirname "$OUT")" || die "cannot create parent dir for $OUT"
    emit > "$OUT" || die "cannot write $OUT"
    info "resolved config written to $OUT (guest_cid=$guest_cid${guest_ip:+, guest_ip=$guest_ip})"
else
    emit
fi
