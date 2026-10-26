# _migr.sh — the save/restore step shared by run-migration.sh and
# run-wsk-migration.sh: plan the restore CID, virsh save + restore, wait for
# the guest to answer again and read the VirtioSocket service state.
# Sourced after _lib.sh.  Honours MIGR_CHANGE_CID (1 = restore with a
# different CID, default) and MIGR_NEW_CID (explicit new CID; empty = the
# first free one above the reserved 0..3).

# migr_plan_cid <guest_name> <guest_cid> <logdir> — sets new_cid and newxml
#   (newxml empty = restore with the same CID).
migr_plan_cid() {
    local guest_name="$1" guest_cid="$2" logdir="$3" used_cids
    newxml=""
    new_cid="$guest_cid"
    [ "${MIGR_CHANGE_CID:-1}" -eq 1 ] || return 0
    if [ -n "${MIGR_NEW_CID:-}" ]; then
        new_cid="$MIGR_NEW_CID"
    else
        # The first CID above the reserved 0..3 that differs from ours and is not held by another
        # running domain on this host: a restore into a taken CID fails ("failed to set guest
        # cid: Address already in use") and the guest is gone with it.
        used_cids=$(for d in $(virsh list --name 2>/dev/null); do
                        virsh dumpxml "$d" 2>/dev/null | grep -oE "<cid[^/]*address='[0-9]+'" | grep -oE "[0-9]+'$" | tr -d "'"
                    done)
        new_cid=$((guest_cid + 1))
        while [ "$new_cid" -le 3 ] || [ "$new_cid" -eq "$guest_cid" ] || grep -qx "$new_cid" <<<"$used_cids"; do
            new_cid=$((new_cid + 1))
        done
    fi
    [ "$new_cid" != "$guest_cid" ] || die "MIGR_NEW_CID=$new_cid equals current guest_cid"
    newxml="$logdir/$guest_name.new.xml"
    virsh dumpxml "$guest_name" > "$newxml" || die "virsh dumpxml $guest_name failed"
    grep -qE "<cid[^/]*/>" "$newxml" || die "domain XML has no <cid .../> to swap"
    # Normalize to auto='no' address='new' — with auto='yes' libvirt may re-pick a CID on restore.
    sed -i -E "s|<cid[^/]*/>|<cid auto='no' address='$new_cid'/>|" "$newxml"
    info "  CID plan: $guest_cid -> $new_cid  (XML: $newxml)"
}

# migr_save_restore <guest_name> <guest_cid> <logdir> — virsh save, then restore
#   with the planned XML (if any); sets save_rc and restore_rc.  A failed
#   restore keeps the saved state on disk for manual recovery.
migr_save_restore() {
    local guest_name="$1" guest_cid="$2" logdir="$3" sr="$3/$1.migr.sr"
    info "== virsh save $guest_name (mid-transfer) =="
    save_rc=0;    virsh save    "$guest_name" "$sr" --running || save_rc=$?
    info "  save rc=$save_rc state=$(virsh domstate "$guest_name" 2>/dev/null)"
    sleep 2
    if [ -n "$newxml" ]; then
        info "== virsh restore --xml (CID $guest_cid -> $new_cid) =="
        restore_rc=0; virsh restore "$sr" --xml "$newxml" --running || restore_rc=$?
    else
        info "== virsh restore =="
        restore_rc=0; virsh restore "$sr" --running || restore_rc=$?
    fi
    info "  restore rc=$restore_rc state=$(virsh domstate "$guest_name" 2>/dev/null)"
    if [ "$restore_rc" -eq 0 ]; then
        rm -f "$sr"
    else
        warn "  restore FAILED (rc=$restore_rc) — keeping the saved state for manual recovery: $sr"
    fi
}

# migr_wait_guest [<polls>] — sets guest_up (1 once ssh answers again; 2 s per poll, 60 by default).
migr_wait_guest() {
    local i
    guest_up=0
    for i in $(seq 1 "${1:-60}"); do
        if _guest_ssh_bounded 15 'echo up' >/dev/null 2>&1; then guest_up=1; return 0; fi
        sleep 2
    done
    return 1
}

# migr_driver_state — sets srv_state (sc STATE text) and driver_ok (1 = RUNNING).
#   Bounded: after a restore an ssh session has been seen to stay open on a live
#   TCP connection after its command had finished, which hung the run.
migr_driver_state() {
    srv_state=$(_guest_ssh_bounded 60 'sc query VirtioSocket' 2>/dev/null \
        | tr -d '\r' | awk -F: '/STATE/ {gsub(/^[ \t]+|[ \t]+$/, "", $2); print $2; exit}')
    [ -n "$srv_state" ] || srv_state='<unknown>'
    driver_ok=0; case "$srv_state" in *RUNNING*|*[[:space:]]4*|4*) driver_ok=1 ;; esac
}
