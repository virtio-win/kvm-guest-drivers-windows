# Shared helpers for viosock/tests/run/ scripts. Sourced, not executed.

# Colored logging (only on TTY).
if [ -t 1 ]; then
    _c_red=$'\033[31m'; _c_yellow=$'\033[33m'; _c_green=$'\033[32m'
    _c_dim=$'\033[2m'; _c_reset=$'\033[0m'
else
    _c_red=""; _c_yellow=""; _c_green=""; _c_dim=""; _c_reset=""
fi

log()  { printf '%s[%s]%s %s\n'  "$_c_dim" "$(date +%H:%M:%S)" "$_c_reset" "$*" ; }
info() { printf '%s%s%s\n'       "$_c_green"  "$*" "$_c_reset" ; }
warn() { printf '%s%s%s\n' >&2   "$_c_yellow" "$*" "$_c_reset" ; }
err()  { printf '%s%s%s\n' >&2   "$_c_red"    "$*" "$_c_reset" ; }
die()  { err "$*"; exit 1 ; }

# config_read <config-path> <key>  --> value, trimmed. Empty if missing.
config_read() {
    local cfg="$1" key="$2"
    awk -v k="$key" '
        /^[[:space:]]*#/ { next }
        {
            idx = index($0, "=")
            if (idx == 0) next
            keypart = substr($0, 1, idx - 1)
            val     = substr($0, idx + 1)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", keypart)
            if (keypart != k) next
            gsub(/^[[:space:]]+/, "", val)
            gsub(/[[:space:]]+#.*$/, "", val)
            gsub(/[[:space:]]+$/, "", val)
            print val
            exit
        }
    ' "$cfg"
}

# discover_config [<explicit>]  --> path
#   Resolution order:
#     1. explicit arg (if set and non-empty)
#     2. $VSOCK_CI_CONFIG env var (if set)
#     3. exactly one .config in /tmp/vsock-ci/       (CI-standard drop)
#     4. exactly one .config next to this script     (in-repo dev flow)
#     5. exactly one .config in ../..                (viosock/ dir)
discover_config() {
    local explicit="${1:-}"
    if [ -n "$explicit" ]; then
        [ -r "$explicit" ] || die "config not readable: $explicit"
        printf '%s\n' "$explicit"; return
    fi
    if [ -n "${VSOCK_CI_CONFIG:-}" ]; then
        [ -r "$VSOCK_CI_CONFIG" ] || die "\$VSOCK_CI_CONFIG unreadable: $VSOCK_CI_CONFIG"
        printf '%s\n' "$VSOCK_CI_CONFIG"; return
    fi
    local script_dir; script_dir=$(cd "$(dirname "${BASH_SOURCE[1]}")" && pwd)
    local candidates=()
    # Try each search location in order, first non-empty wins.
    for dir in /tmp/vsock-ci "$script_dir" "$script_dir/../.."; do
        [ -d "$dir" ] || continue
        candidates=()
        while IFS= read -r -d '' f; do candidates+=("$f"); done < <(find "$dir" -maxdepth 1 -name '*.config' -print0 2>/dev/null)
        [ "${#candidates[@]}" -gt 0 ] && break
    done
    if [ "${#candidates[@]}" -eq 0 ]; then
        die "no *.config found in /tmp/vsock-ci, $script_dir, or $script_dir/../..; pass --config or set \$VSOCK_CI_CONFIG"
    fi
    if [ "${#candidates[@]}" -gt 1 ]; then
        err "multiple *.config candidates, pass --config <path>:"; printf '  %s\n' "${candidates[@]}" >&2
        exit 2
    fi
    printf '%s\n' "${candidates[0]}"
}

# read_list <list-path> [<variant-filter>]
#   Emits one enabled entry per line, format: "<id> <variant>" (tab-separated).
#   `#`-prefixed lines are treated as disabled and skipped.
#   Row format expected in the file: "<id> <variant> <description>"
#   If <variant-filter> is given, only rows with that variant are emitted.
read_list() {
    local path="$1" filter="${2:-}"
    [ -r "$path" ] || die "list not readable: $path"
    awk -v filter="$filter" '
        /^[[:space:]]*#/ { next }
        /^[[:space:]]*$/ { next }
        {
            id=$1; variant=$2
            if (id !~ /^[0-9]+$/) next
            if (variant == "" || variant ~ /^[0-9]/) {
                print "list " FILENAME " line " NR ": missing variant column" > "/dev/stderr"
                next
            }
            if (filter != "" && variant != filter) next
            printf "%s\t%s\n", id, variant
        }
    ' "$path"
}

# guest_load <config-path>
#   Reads the guest connection fields from the config into caller-scope
#   globals:
#     $ssh_key            path to SSH private key (~ expanded)
#     $guest_user         SSH login user (default: Administrator)
#     $guest_ip           IPv4 of the guest
#     $_guest_ssh_host    convenience: "$guest_user@$guest_ip"
#     $_guest_ssh_opts    ssh -o flag array (extra opts can be inserted
#                         between it and $_guest_ssh_host)
#     $_guest_scp_opts    scp -o flag array (same shape, minus -q)
#
#   Host-key checking is disabled for CI-controlled VMs (ephemeral,
#   keys change per fresh image).
guest_load() {
    local cfg="$1"
    ssh_key=$(config_read "$cfg" ssh_key)
    [ -n "$ssh_key" ] || die "config $cfg has no ssh_key="
    ssh_key="${ssh_key/#\~/$HOME}"
    [ -r "$ssh_key" ] || die "ssh_key not readable: $ssh_key"

    guest_user=$(config_read "$cfg" guest_user); : "${guest_user:=Administrator}"

    guest_ip=$(config_read "$cfg" guest_ip)
    [ -n "$guest_ip" ] || die "config $cfg has no guest_ip="

    # Where the guest keeps the test binary/binaries.  variant_to_cmd
    # prepends this before the exe basename; install-tests.sh reads the
    # same field to decide where to scp vsock_test.exe.  Default `C:`.
    guest_bin_dir=$(config_read "$cfg" guest_bin_dir); : "${guest_bin_dir:=C:}"

    _guest_ssh_host="$guest_user@$guest_ip"
    _guest_ssh_opts=(-i "$ssh_key"
                     -o StrictHostKeyChecking=no
                     -o UserKnownHostsFile=/dev/null
                     -o BatchMode=yes
                     -o LogLevel=ERROR)
    _guest_scp_opts=(-i "$ssh_key"
                     -o StrictHostKeyChecking=no
                     -o UserKnownHostsFile=/dev/null
                     -o BatchMode=yes
                     -o LogLevel=ERROR
                     -q)
}

# _guest_ssh <cmd...>       — shorthand for ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" "$@"
# For invocations that need `timeout`/other wrappers, expand the arrays
# directly:  timeout N ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" cmd
_guest_ssh()      { ssh "${_guest_ssh_opts[@]}" "$_guest_ssh_host" "$@"; }
_guest_scp_to()   { scp "${_guest_scp_opts[@]}" "$1" "$_guest_ssh_host:$2"; }
_guest_scp_from() { scp "${_guest_scp_opts[@]}" "$_guest_ssh_host:$1" "$2"; }

# _guest_ps <ps-script>  — run PowerShell on guest via -EncodedCommand
#   Base64-UTF16LE encodes the whole script, sidestepping every cmd
#   quoting layer.  Requires iconv and base64.  Guest stdout/stderr
#   passes through to our stdout/stderr; exit code is the PS-side exit.
_guest_ps() {
    local b64
    b64=$(printf '%s' "$1" | iconv -f UTF-8 -t UTF-16LE | base64 -w0)
    _guest_ssh "powershell -NoProfile -EncodedCommand $b64"
}

# wait_guest_port <port> [<max-seconds>]
#   Polls the guest (via _guest_ssh) with Get-NetTCPConnection until <port>
#   is in Listen state, up to <max-seconds> (default 15).
wait_guest_port() {
    local port="$1" max="${2:-15}" i
    for i in $(seq 1 $(( max * 2 ))); do
        if _guest_ssh "powershell -NoProfile -Command \"if (Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }\"" \
                >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.5
    done
    return 1
}

# variant_to_cmd <variant> <bits> [<bin-dir>]  --> stdout: guest command
#   The returned string is prepended verbatim before the runner's
#   --mode=client/server, --control-host/-port, --peer-cid, --pick args.
#   A variant can therefore map to a bare "<exe>" or "<exe> --extra-flag=...".
#
#   <bits> selects between x64 (default) and x86 builds. Runners take the
#   choice as a sweep-wide flag (`--bits`/`--x86`) so a single list row
#   can be re-run against both bit-widths without touching the list.
#
#   Add a new variant here — that's the only central mapping.
variant_to_cmd() {
    local variant="$1" bits="${2:-x64}" bin_dir="${3:-C:}"
    local exe flags
    case "$bits" in
        x64) exe='vsock_test.exe' ;;
        x86) exe='vsock_test_x86.exe' ;;
        *)   err "unknown bits: $bits"; return 1 ;;
    esac
    case "$variant" in
        posix) flags='' ;;
        wsa)   flags=' --variant wsa' ;;
        # Example future variant, sharing the exe with wsa:
        # overlapped) flags=' --variant overlapped' ;;
        *)     err "unknown variant: $variant"; return 1 ;;
    esac
    printf '%s\\%s%s\n' "$bin_dir" "$exe" "$flags"
}

# variant_to_image <variant> <bits>  --> stdout: image name for taskkill /IM
#   Extracts the basename of the exe from variant_to_cmd. Windows taskkill
#   /IM matches processes by ImageName regardless of the CLI args they were
#   launched with, so two variants that share the same exe get a single
#   taskkill target.
variant_to_image() {
    # bin_dir here doesn't matter — we only care about the exe basename.
    local cmd; cmd=$(variant_to_cmd "$1" "${2:-x64}" "C:") || return 1
    local exe="${cmd%% *}"
    printf '%s\n' "${exe##*\\}"
}

# variant_all_images  --> stdout: newline-separated unique image names across
# all defined variants (both bit-widths); used by the runner-level cleanup.
variant_all_images() {
    for v in posix wsa; do
        for b in x64 x86; do
            variant_to_image "$v" "$b" 2>/dev/null || true
        done
    done | sort -u
}

# junit_begin <path> <suite-name>
#   Open a new JUnit XML file with a single <testsuites>/<testsuite> root.
#   Testcases are appended by junit_case_* helpers. Call junit_finalize
#   at the end of the sweep to compute counters + write the closing tags.
#
# The intermediate file holds one testcase per line (a self-describing
# JSON envelope). junit_finalize reads them back to produce the final
# XML with correct summary attributes.
junit_begin() {
    local path="$1" suite="$2"
    mkdir -p "$(dirname "$path")"
    : > "$path.raw"
    printf '%s\n' "$suite" > "$path.suite"
}

# junit_case_ok <path> <name> <time-seconds>
junit_case_ok() {
    local path="$1" name="$2" time="$3"
    printf 'ok\t%s\t%s\n' "$time" "$name" >> "$path.raw"
}

# junit_case_skipped <path> <name> <time-seconds> <message>
junit_case_skipped() {
    local path="$1" name="$2" time="$3" msg="$4"
    printf 'skipped\t%s\t%s\t%s\n' "$time" "$name" "$msg" >> "$path.raw"
}

# junit_case_failed <path> <name> <time-seconds> <message> [<detail-file>]
#   detail-file is embedded verbatim inside <failure>...</failure> (CDATA).
junit_case_failed() {
    local path="$1" name="$2" time="$3" msg="$4" detail_file="${5:-}"
    printf 'failed\t%s\t%s\t%s\t%s\n' "$time" "$name" "$msg" "$detail_file" >> "$path.raw"
}

# _junit_xml_escape <text>  --> stdout with XML entities escaped
_junit_xml_escape() {
    printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g'
}

# junit_finalize <path>
#   Read <path>.raw + <path>.suite, emit <path> as valid JUnit XML with
#   accurate tests/failures/skipped/time counters. Removes the intermediates.
junit_finalize() {
    local path="$1"
    local suite; suite=$(cat "$path.suite" 2>/dev/null || echo "unknown")
    local tests=0 failures=0 skipped=0 total_time="0"
    # First pass: counters.
    while IFS=$'\t' read -r kind t rest; do
        tests=$((tests+1))
        case "$kind" in
            failed)  failures=$((failures+1)) ;;
            skipped) skipped=$((skipped+1)) ;;
        esac
        total_time=$(awk -v a="$total_time" -v b="$t" 'BEGIN{ printf "%.3f", a+b }')
    done < "$path.raw"
    # Emit XML.
    {
        printf '<?xml version="1.0" encoding="UTF-8"?>\n'
        printf '<testsuites>\n'
        printf '  <testsuite name="%s" tests="%d" failures="%d" skipped="%d" time="%s">\n' \
            "$(_junit_xml_escape "$suite")" "$tests" "$failures" "$skipped" "$total_time"
        while IFS=$'\t' read -r kind t name msg detail_file; do
            case "$kind" in
                ok)
                    printf '    <testcase name="%s" classname="%s" time="%s"/>\n' \
                        "$(_junit_xml_escape "$name")" "$(_junit_xml_escape "$suite")" "$t"
                    ;;
                skipped)
                    printf '    <testcase name="%s" classname="%s" time="%s">\n' \
                        "$(_junit_xml_escape "$name")" "$(_junit_xml_escape "$suite")" "$t"
                    printf '      <skipped message="%s"/>\n' "$(_junit_xml_escape "$msg")"
                    printf '    </testcase>\n'
                    ;;
                failed)
                    printf '    <testcase name="%s" classname="%s" time="%s">\n' \
                        "$(_junit_xml_escape "$name")" "$(_junit_xml_escape "$suite")" "$t"
                    printf '      <failure message="%s">' "$(_junit_xml_escape "$msg")"
                    if [ -n "$detail_file" ] && [ -r "$detail_file" ]; then
                        printf '<![CDATA[\n'
                        cat "$detail_file"
                        printf ']]>'
                    fi
                    printf '</failure>\n'
                    printf '    </testcase>\n'
                    ;;
            esac
        done < "$path.raw"
        printf '  </testsuite>\n'
        printf '</testsuites>\n'
    } > "$path"
    rm -f "$path.raw" "$path.suite"
}
