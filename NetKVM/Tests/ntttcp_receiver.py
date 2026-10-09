"""NetKVM ntttcp performance test - receiver side.

Run this script on the machine that RECEIVES traffic; run ntttcp_sender.py
on the peer. The pair is role-symmetric: to test the reverse direction
(e.g. host->guest after testing guest->host), just swap which machine
runs which script and run again - no code changes needed. Each direction
exercises different driver paths (TX vs RX virtqueues), so full coverage
means one run per direction.

Usage: python ntttcp_receiver.py <sender_ip> [all|tcp|udp]
"""

import socket
import subprocess
import signal
import time
import sys
import csv
import os
import re
import platform
import xml.etree.ElementTree as ET
from datetime import datetime

# === Platform Detection ===
IS_WINDOWS = platform.system() == "Windows"
NTTTCP_CLIENT = "ntttcp.exe" if IS_WINDOWS else "ntttcp"

# === Configuration ===
CONTROL_PORT = 9999
DATA_PORT_BASE = 5001  # ntttcp default first data port (one per thread, consecutive)
TEST_TIME = 10  # quick-smoke default; use >= 60 for formal runs. Short windows
                # amplify ntttcp's stats-API tail latency inside its realtime
                # bracket (measured: median 2ms, tail up to 21s; at 100s the
                # packet counter no longer collapses -> zero-PPS disappears)
CROSS_PLATFORM = False  # Auto-detected: True when sender and receiver are different OS
SENDER_WAIT_TIMEOUT = 300  # Max seconds to wait for the sender service

def get_my_ip(sender_ip):
    """Auto-detect local IP by checking which interface routes to the sender."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect((sender_ip, 1))  # UDP, no actual data sent
        return s.getsockname()[0]

def detect_interface(target_ip):
    """Auto-detect network interface name for a given target IP via routing table.

    Prints what the route lookup returned so an empty result can be
    told apart from a parsing failure (the reason packets_received
    stays 0 in ntttcp-for-linux without --show-nic-packets).
    """
    try:
        out = subprocess.check_output(["ip", "route", "get", target_ip],
                                      text=True, stderr=subprocess.DEVNULL)
        m = re.search(r'dev\s+(\S+)', out)
        iface = m.group(1) if m else ""
        if not iface:
            print(f"[!] route lookup for {target_ip} returned no 'dev <iface>': "
                  f"{out.strip()[:200]!r}")
        return iface
    except Exception as e:
        print(f"[!] 'ip route get {target_ip}' failed: {e}")
        return ""

# Thread counts to test; capped at this machine's CPU count so rows measure
# the driver, not a CPU-starved scheduler. NOTE: the same count is applied on
# the sender too -- if the sender is smaller than this machine, lower it here
# (the sender prints an oversubscription warning when it happens).
def default_thread_counts():
    caps = [t for t in [1, 2, 4, 8, 16] if t <= (os.cpu_count() or 1)]
    return caps or [1]

# Packet sizes to test
PACKET_SIZES = [32, 64, 128, 256, 512, 1024, 1460, 2048, 4096, 8192, 16384, 32768, 65536]
UDP_SIZES = [32, 64, 128, 256, 512, 1024, 1472]  # MTU-safe sizes

# Build scenarios based on test mode
def build_scenarios(test_mode, thread_counts):
    scenarios = []
    if test_mode in ("all", "tcp"):
        for threads in thread_counts:
            for size in PACKET_SIZES:
                scenarios.append(("TCP", size, threads))
    if test_mode in ("all", "udp"):
        for threads in thread_counts:
            for size in UDP_SIZES:
                scenarios.append(("UDP", size, threads))
    return scenarios

log_file = None

def log(msg=""):
    """Print to both console and log file."""
    print(msg)
    if log_file:
        log_file.write(msg + "\n")
        log_file.flush()

def wait_for_sender(sender_ip, timeout_s=SENDER_WAIT_TIMEOUT):
    """Wait for sender service to come online, detect its platform.

    Returns the sender platform string, or None if not reachable in time.
    """
    global CROSS_PLATFORM
    log(f"Waiting for sender service at {sender_ip}:{CONTROL_PORT} (max {timeout_s}s)...")
    deadline = time.time() + timeout_s
    sender_platform = None
    while time.time() < deadline:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.settimeout(3)
                s.connect((sender_ip, CONTROL_PORT))
                s.sendall(b"PLATFORM")
                sender_platform = s.recv(64).decode().strip()
            break
        except (ConnectionRefusedError, socket.timeout, OSError):
            time.sleep(2)
    if sender_platform is None:
        return None
    my_platform = platform.system()
    CROSS_PLATFORM = (sender_platform != my_platform) and sender_platform != ""
    if CROSS_PLATFORM:
        log(f"Sender is online ({sender_platform}). Cross-platform detected, using no-sync mode.\n")
    else:
        log(f"Sender is online. Same platform ({my_platform}).\n")
    return sender_platform

def probe_bound_ports(proto, pid):
    """Return the set of local data ports bound by the given pid.

    Uses netstat on Windows and ss on Linux. Returns None when the listing
    tool itself failed, so the caller can degrade to a fixed delay quickly
    instead of polling a broken probe for the whole timeout.
    """
    try:
        if IS_WINDOWS:
            r = subprocess.run(["netstat", "-ano", "-p",
                                "tcp" if proto == "TCP" else "udp"],
                               capture_output=True, text=True, timeout=5)
        else:
            r = subprocess.run(["ss", "-tlnp" if proto == "TCP" else "-ulnp"],
                               capture_output=True, text=True, timeout=5)
    except Exception:
        return None
    if r.returncode != 0:
        return None
    ports = set()
    for line in r.stdout.splitlines():
        parts = line.split()
        try:
            if IS_WINDOWS:
                # TCP: proto local foreign state pid
                # UDP: proto local foreign pid
                if proto == "TCP":
                    if len(parts) < 5 or parts[3] != "LISTENING":
                        continue
                    row_pid = int(parts[4])
                else:
                    if len(parts) < 4:
                        continue
                    row_pid = int(parts[3])
                if row_pid != pid:
                    continue
                ports.add(int(parts[1].rsplit(":", 1)[1]))
            else:
                # ss: state recv-q send-q local peer process
                if len(parts) < 5:
                    continue
                if f"pid={pid}," not in line and f"pid={pid})" not in line:
                    continue
                ports.add(int(parts[3].rsplit(":", 1)[1]))
        except (ValueError, IndexError):
            continue
    return ports

def wait_port_listening(proc, proto, threads, timeout_s=15):
    """Wait until this receiver's ntttcp binds all its data ports.

    Replaces the old blind sleep(2): the sender is only signalled after the
    local ntttcp is really listening, so the sender never starts before the
    receiver is ready. The pid is matched as well, so a leftover ntttcp from
    a previous row cannot fake readiness. Returns False on timeout, tool
    failure, or early process death.
    """
    wanted = {DATA_PORT_BASE + i for i in range(threads)}
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if proc.poll() is not None:
            return False  # ntttcp died before it could listen
        bound = probe_bound_ports(proto, proc.pid)
        if bound is None:
            return False  # probe tool unavailable; degrade to fixed delay
        if wanted <= bound:
            return True
        time.sleep(0.1)
    return False

def signal_sender(sender_ip, my_ip, proto, size, threads, duration):
    """Signals the remote sender with test parameters and duration."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(5)
            s.connect((sender_ip, CONTROL_PORT))
            no_sync = "1" if CROSS_PLATFORM else "0"
            s.sendall(f"{proto}:{size}:{threads}:{my_ip}:{duration}:{no_sync}".encode())
            # "ACK" = the sender accepted the request and is about to spawn
            # its ntttcp; an empty reply (older sender) is tolerated too
            reply = s.recv(128).decode().strip()
            if reply.startswith("ERR"):
                log(f"  Sender rejected request: {reply}")
                return False
        return True
    except Exception:
        return False

def get_sender_results(sender_ip):
    """Connect to sender and retrieve last test results (throughput:cpu:pps).

    Returns the tuple, or None if the sender could not be reached.
    """
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(30)
            s.connect((sender_ip, CONTROL_PORT))
            s.sendall(b"RESULTS")
            data = s.recv(1024).decode().strip()
            parts = data.split(':')
            return float(parts[0]), float(parts[1]), int(parts[2])
    except Exception:
        return None

def parse_stats_xml(xml_file):
    """Parse ntttcp XML output for throughput, CPU, and PPS."""
    mbps, cpu, pps = 0.0, 0.0, 0
    try:
        tree = ET.parse(xml_file)
        root = tree.getroot()
        if IS_WINDOWS:
            for elem in root.findall('throughput'):
                if elem.get('metric') == 'mbps':
                    mbps = round(float(elem.text), 2)
                    break
            for elem in root.findall('realtime'):
                if elem.get('metric') == 's':
                    realtime = float(elem.text)
                    break
            else:
                realtime = 0
            pkts = int(root.findtext('packets_received', '0'))
            if pkts > 0 and realtime > 0:
                pps = int(pkts / realtime)
            for elem in root.findall('cpu'):
                if elem.get('metric') == '%':
                    cpu = round(float(elem.text), 2)
                    break
        else:
            realtime = float(root.findtext('realtime', '0'))
            for elem in root.findall('throughput'):
                if elem.get('metric') == 'mbps':
                    mbps = round(float(elem.text), 2)
                    break
            pkts = int(root.findtext('packets_received', '0'))
            if pkts > 0 and realtime > 0:
                pps = int(pkts / realtime)
            # System-wide avg busy%: 100-idle covers all busy categories
            # (incl. hard-irq), matching Windows ntttcp's kernel+user-idle.
            idle = float(root.findtext('idle', '100'))
            cpu = round(100.0 - idle, 2)
    except Exception as e:
        log(f"  XML parse error: {e}")
    return mbps, cpu, pps

def kill_proc(proc):
    if proc.poll() is not None:
        return
    if IS_WINDOWS:
        subprocess.run(["taskkill", "/f", "/pid", str(proc.pid)],
                       capture_output=True, timeout=10)
    else:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except ProcessLookupError:
            pass

def xml_complete(xml_file):
    """True when the XML report is fully written and parseable.

    ntttcp-for-linux writes the XML file non-atomically, so file
    existence alone is not a usable completion signal.
    """
    try:
        root = ET.parse(xml_file).getroot()
        return root.findtext('realtime') is not None
    except ET.ParseError:
        return False
    except OSError:
        return False

def wait_for_xml(proc, xml_file, timeout_s):
    """Wait until a complete ntttcp XML report shows up (Linux receiver
    keeps running after the test completes in -N mode, so process exit
    is not a usable completion signal). Returns True when the XML is
    complete and parseable.
    """
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if os.path.exists(xml_file) and xml_complete(xml_file):
            return True
        if proc.poll() is not None:
            # Process died; give it a moment for the report to flush
            time.sleep(2)
            return os.path.exists(xml_file) and xml_complete(xml_file)
        time.sleep(1)
    return os.path.exists(xml_file) and xml_complete(xml_file)

def main():
    global log_file
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print("Usage: python ntttcp_receiver.py <sender_ip> [all|tcp|udp]")
        print("  sender_ip: IP of the machine running ntttcp_sender.py (required)")
        sys.exit(2)
    sender_ip = sys.argv[1]
    test_mode = sys.argv[2] if len(sys.argv) > 2 else "all"

    thread_counts = default_thread_counts()
    my_ip = get_my_ip(sender_ip)
    interface_name = "" if IS_WINDOWS else detect_interface(sender_ip)
    if not IS_WINDOWS and not interface_name:
        print("WARNING: could not autodetect the interface toward the sender; "
              "PPS will be reported as 0 (ntttcp needs --show-nic-packets <iface>)")
    scenarios = build_scenarios(test_mode, thread_counts)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_filename = f"ntttcp_results_{timestamp}.csv"
    log_filename = f"ntttcp_results_{timestamp}.log"

    start_time = datetime.now()
    if wait_for_sender(sender_ip) is None:
        print(f"ERROR: sender service at {sender_ip}:{CONTROL_PORT} not reachable "
              f"within {SENDER_WAIT_TIMEOUT}s")
        sys.exit(1)

    log_file = open(log_filename, mode='w')
    with open(csv_filename, mode='w', newline='') as csv_file:
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(["Protocol", "Size(B)", "Threads",
                             "Recv_Thr(Mbps)", "Recv_PPS", "Recv_CPU(%)",
                             "Send_Thr(Mbps)", "Send_PPS", "Send_CPU(%)",
                             "Status"])

        log(f"### NetKVM Performance Report (Mode: {test_mode}, Duration: {TEST_TIME}s)")
        log(f"### Results saved to: {csv_filename}")
        log("### CPU metric (system-wide avg utilization): Windows = kernel+user-idle; Linux = 100-idle")
        hdr = "| {:8} | {:7} | {:4} | {:>10} | {:>7} | {:>6} | {:>10} | {:>7} | {:>6} | {:>14} |"
        log(hdr.format("Protocol", "Size(B)", "Thr",
                       "R_Mbps", "R_PPS", "R_CPU%", "S_Mbps", "S_PPS", "S_CPU%", "Status"))
        log(hdr.format(":---", ":---", ":--", ":---", ":---", ":---", ":---", ":---", ":---", ":---"))

        timeout_s = TEST_TIME + 45
        failed = 0
        for idx, (proto, size, threads) in enumerate(scenarios):
            try:
                is_udp = "-u" if proto == "UDP" else ""
                recv_l = max(size, 65536) if proto == "UDP" else size
                # Unique per-test filename so a bad row keeps its XML for
                # post-mortem (Windows TCP stats can read 0 under a system
                # stall, see the WARNING below)
                xml_file = f"ntttcp_recv_{os.getpid()}_{idx}.xml"
                if os.path.exists(xml_file):
                    os.remove(xml_file)
                if idx > 0:
                    time.sleep(2)

                if IS_WINDOWS:
                    timeout_ms = (TEST_TIME + 30) * 1000
                    rcv_cmd = [NTTTCP_CLIENT, "-r"]
                    if is_udp:
                        rcv_cmd.append(is_udp)
                    rcv_cmd += ["-m", f"{threads},*,{my_ip}", "-l", str(recv_l),
                                "-t", str(TEST_TIME), "-to", str(timeout_ms)]
                    if CROSS_PLATFORM:
                        rcv_cmd.append("-ns")
                    rcv_cmd += ["-xml", xml_file]
                else:
                    rcv_cmd = [NTTTCP_CLIENT, "-r"]
                    if is_udp:
                        rcv_cmd.append(is_udp)
                    rcv_cmd += ["-b", str(recv_l), "-P", str(threads), "-N"]
                    if interface_name:
                        rcv_cmd += ["--show-nic-packets", interface_name]
                    rcv_cmd += ["-x", xml_file, "-t", str(TEST_TIME)]

                if IS_WINDOWS:
                    proc = subprocess.Popen(rcv_cmd, stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, universal_newlines=True)
                else:
                    proc = subprocess.Popen(rcv_cmd, stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, universal_newlines=True,
                                            preexec_fn=os.setsid)
                # Signal the sender only after the local ntttcp is really
                # listening on its data ports — replaces the old blind
                # sleep(2) guess with an observable readiness fact
                if not wait_port_listening(proc, proto, threads):
                    log("  WARNING: could not confirm ntttcp listening; "
                        "falling back to fixed 2s delay")
                    time.sleep(2)

                if signal_sender(sender_ip, my_ip, proto, size, threads, TEST_TIME):
                    stderr = ""
                    xml_ready = False
                    if IS_WINDOWS:
                        try:
                            stdout, stderr = proc.communicate(timeout=timeout_s)
                        except subprocess.TimeoutExpired:
                            kill_proc(proc)
                            stdout, stderr = proc.communicate()
                            log(f"  WARNING: ntttcp timed out")
                        xml_ready = os.path.exists(xml_file)
                    else:
                        # Linux -N receiver does not exit on its own; a complete
                        # XML report is the real completion signal
                        xml_ready = wait_for_xml(proc, xml_file, timeout_s)
                        if not xml_ready:
                            log(f"  WARNING: ntttcp report missing/incomplete after {timeout_s}s")
                        kill_proc(proc)
                        stdout, stderr = proc.communicate()

                    status = "OK"
                    if IS_WINDOWS and proc.returncode not in (0, None):
                        # Windows receiver exits on its own; non-zero is a real failure
                        status = f"FAILED(rc={proc.returncode})"
                        failed += 1
                    # Linux receiver is killed by us once the XML report lands,
                    # so its exit code carries no signal

                    if xml_ready:
                        r_thr, r_cpu, r_pps = parse_stats_xml(xml_file)
                    else:
                        log(f"  WARNING: XML not generated. stderr: {str(stderr).strip()[:200]}")
                        r_thr, r_cpu, r_pps = 0.0, 0.0, 0
                        status = "FAILED(no xml)"
                        failed += 1
                    if status == "OK" and r_thr == 0.0 and r_pps == 0:
                        # Complete XML but no measurements at all — treat as failure
                        status = "FAILED(no data)"
                        failed += 1
                    if status == "OK" and r_thr > 0 and r_pps == 0:
                        # Traffic flowed but the packet counter did not move:
                        # on Windows ntttcp derives packets_received from
                        # GetTcpStatisticsEx2 snapshots, whose lazy per-CPU
                        # aggregation can stall for the whole window when the
                        # system is under a deep stall (observed 1/480 rows:
                        # throughput halved + CPU crushed + InSegs delta 0)
                        log(f"  WARNING: {r_thr} Mbps flowed but PPS=0 — "
                            f"packet-counter snapshot anomaly (see {xml_file})")

                    s_res = get_sender_results(sender_ip)
                    if s_res is None:
                        log("  WARNING: could not fetch sender results")
                        s_thr, s_cpu, s_pps = 0.0, 0.0, 0
                        if status == "OK":
                            status = "FAILED(no sender data)"
                            failed += 1
                    else:
                        s_thr, s_cpu, s_pps = s_res

                    row = "| {:8} | {:7} | {:4} | {:>10} | {:>7} | {:>6} | {:>10} | {:>7} | {:>6} | {:>14} |"
                    log(row.format(proto, size, threads,
                                   r_thr, r_pps, f"{r_cpu}%", s_thr, s_pps, f"{s_cpu}%", status))
                    csv_writer.writerow([proto, size, threads,
                                         r_thr, r_pps, r_cpu, s_thr, s_pps, s_cpu, status])
                    csv_file.flush()
                else:
                    log(f"| ERROR: Sender signal failed for {proto} {size}B |")
                    kill_proc(proc)
                    proc.communicate()
                    failed += 1
            except Exception as e:
                log(f"| ERROR: {proto} {size}B {threads}T failed: {e} |")
                failed += 1
                try:
                    kill_proc(proc)
                    proc.communicate()
                except Exception:
                    pass

        # Signal sender to exit
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.settimeout(5)
                s.connect((sender_ip, CONTROL_PORT))
                s.sendall(b"QUIT")
                s.recv(16)
        except Exception:
            pass

        summary = f"\nTest completed at {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}, total time: {datetime.now() - start_time}"
        summary += f", {failed}/{len(scenarios)} tests failed"
        log(summary)
        log_file.close()

    sys.exit(1 if failed > 0 else 0)

if __name__ == "__main__":
    main()
