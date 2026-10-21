# _wsk.sh — drive the kernel-mode viosockwsk-perf driver on the guest.
#
# The driver runs as the kernel service $WSK_SERVICE.  It reads its load
# description from the service's Parameters registry key, starts one
# thread per connection in DriverEntry and, when the last thread is done,
# writes the aggregate into the Results key (Done=1 last).  So a run is:
# wsk_set_params -> wsk_start -> wsk_wait_done -> wsk_results -> wsk_stop.
# Sourced after _lib.sh (needs _guest_ssh).

WSK_SERVICE=${WSK_SERVICE:-wskperf}
WSK_KEY="HKLM\\SYSTEM\\CurrentControlSet\\Services\\$WSK_SERVICE"

# wsk_set_params <mode: 0 receiver|1 sender> <peer_cid> <port> <bytes_mb> <buf_size> <connections>
#   Connection i listens on / connects to <port>+i.
wsk_set_params() {
    local mode="$1" peer="$2" port="$3" mb="$4" buf="$5" n="$6"
    local cmd="" kv
    for kv in "Mode=$mode" "PeerCid=$peer" "Port=$port" "BytesMB=$mb" "BufSize=$buf" "Connections=$n" "Transport=0"; do
        cmd+="reg add $WSK_KEY\\Parameters /v ${kv%%=*} /t REG_DWORD /d ${kv#*=} /f & "
    done
    _guest_ssh "${cmd% & }" >/dev/null 2>&1
}

# wsk_start — sc start: DriverEntry has started the transfers when it returns.
wsk_start() {
    _guest_ssh "sc start $WSK_SERVICE" >/dev/null 2>&1
}

# wsk_state — the sc STATE text (e.g. "4  RUNNING", "1  STOPPED").
wsk_state() {
    _guest_ssh "sc query $WSK_SERVICE" 2>/dev/null | tr -d '\r' \
        | awk -F: '/STATE/ {gsub(/^[ \t]+|[ \t]+$/, "", $2); print $2; exit}'
}

# wsk_wait_done <max_secs> — poll Results\Done every 2 s; 1 on timeout.
wsk_wait_done() {
    local max="$1" i
    for i in $(seq 1 $(( max / 2 ))); do
        if _guest_ssh "reg query $WSK_KEY\\Results /v Done" 2>/dev/null | tr -d '\r' \
                | grep -qE 'Done +REG_DWORD +0x1$'; then
            return 0
        fi
        sleep 2
    done
    return 1
}

# wsk_results — stdout: "ok failed bytes elapsed_us mbit stalls connections status"
#   (decimal; status = the first failed thread's NTSTATUS in hex, 0x0 if none).
wsk_results() {
    _guest_ssh "reg query $WSK_KEY\\Results" 2>/dev/null | tr -d '\r' | awk '
        $2 ~ /^REG_(DWORD|QWORD)$/ { v[$1] = strtonum($3) }
        END { printf "%d %d %d %d %d %d %d 0x%x\n", v["Ok"], v["Failed"], v["Bytes"], v["ElapsedUs"], v["MbitPerSec"], v["Stalls"], v["Connections"], v["Status"] }'
}

# wsk_stop [<max_secs>] — sc stop, then wait for STOPPED (the unload joins
# the worker threads; a thread stuck in a call is cancelled by the driver
# after one second).  1 if the service is still not stopped in time.
wsk_stop() {
    local max="${1:-120}" i
    _guest_ssh "sc stop $WSK_SERVICE" >/dev/null 2>&1 || true
    for i in $(seq 1 "$max"); do
        case "$(wsk_state)" in
            1*|*STOPPED*) return 0 ;;
        esac
        sleep 1
    done
    return 1
}

# wsk_mbit_to_gbps <mbit> — "12.34"
wsk_mbit_to_gbps() { awk -v m="$1" 'BEGIN { printf "%.2f", m / 1000 }'; }
