"""NetKVM ntttcp performance test - sender side.

Run this script on the machine that SENDS traffic; run ntttcp_receiver.py
on the peer. The pair is role-symmetric: to test the reverse direction,
swap which machine runs which script and run again - no code changes
needed. Each direction exercises different driver paths (TX vs RX
virtqueues), so full coverage means one run per direction.
"""

import socket
import subprocess
import signal
import platform
import os
import re
import sys
import ipaddress
import threading
import xml.etree.ElementTree as ET
from datetime import datetime

IS_WINDOWS = platform.system() == "Windows"
NTTTCP_CLIENT = "ntttcp.exe" if IS_WINDOWS else "ntttcp"

last_results = "0.0:0.0:0"
tests_run = 0
run_done = threading.Event()
run_done.set()

# Sanity limits for requests received over the control socket
MAX_THREADS = 128
MAX_SIZE = 65536
MAX_DURATION = 3600

def detect_interface(target_ip):
    """Auto-detect network interface name for a given target IP via routing table."""
    try:
        out = subprocess.check_output(["ip", "route", "get", target_ip],
                                      text=True, stderr=subprocess.DEVNULL)
        m = re.search(r'dev\s+(\S+)', out)
        return m.group(1) if m else ""
    except Exception:
        return ""

def validate_request(proto, size, threads, target_ip, duration):
    """Validate a test request received over the control socket.

    Returns an error string, or None when the request is valid.
    All fields are attacker-controlled input and are also used to build
    the ntttcp command line, so nothing may pass through unchecked.
    """
    if proto not in ("TCP", "UDP"):
        return f"bad proto '{proto}'"
    try:
        size_i, threads_i, duration_i = int(size), int(threads), int(duration)
    except (TypeError, ValueError):
        return "size/threads/duration must be integers"
    if not 1 <= size_i <= MAX_SIZE:
        return f"size {size_i} out of range 1..{MAX_SIZE}"
    if not 1 <= threads_i <= MAX_THREADS:
        return f"threads {threads_i} out of range 1..{MAX_THREADS}"
    if not 1 <= duration_i <= MAX_DURATION:
        return f"duration {duration_i} out of range 1..{MAX_DURATION}"
    try:
        ipaddress.ip_address(target_ip)
    except ValueError:
        return f"bad target_ip '{target_ip}'"
    return None

def parse_sender_xml(xml_file):
    """Parse sender's XML output for throughput, CPU, and PPS."""
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
            pkts = int(root.findtext('packets_sent', '0'))
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
            pkts = int(root.findtext('packets_sent', '0'))
            if pkts > 0 and realtime > 0:
                pps = int(pkts / realtime)
            # System-wide avg busy%: 100-idle covers all busy categories
            # (incl. hard-irq), matching Windows ntttcp's kernel+user-idle.
            idle = float(root.findtext('idle', '100'))
            cpu = round(100.0 - idle, 2)
    except Exception as e:
        print(f"  XML parse error: {e}")
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

def run_ntttcp(cmd, xml_file, duration):
    global last_results
    try:
        if os.path.exists(xml_file):
            os.remove(xml_file)
        timeout_s = int(duration) + 60
        if IS_WINDOWS:
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)
        else:
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE,
                                    preexec_fn=os.setsid)
        try:
            proc.communicate(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            kill_proc(proc)
            proc.communicate()
            print(f"[!] WARNING: ntttcp timed out after {duration}s + 60s margin")

        if proc.returncode != 0:
            print(f"[!] WARNING: ntttcp exited with code {proc.returncode}")

        if os.path.exists(xml_file):
            mbps, cpu, pps = parse_sender_xml(xml_file)
        else:
            mbps, cpu, pps = 0.0, 0.0, 0
            print(f"[!] WARNING: no XML output (ntttcp rc={proc.returncode})")
        last_results = f"{mbps}:{cpu}:{pps}"
        print(f"[=] Send stats: Thr={mbps}Mbps CPU={cpu}% PPS={pps}")
    finally:
        run_done.set()

def start_sender_service():
    global tests_run
    # Optional: restrict the control socket to a specific interface IP
    # (default 0.0.0.0 — fine for isolated lab networks)
    bind_ip = sys.argv[1] if len(sys.argv) > 1 else "0.0.0.0"
    PORT = 9999
    start_time = datetime.now()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((bind_ip, PORT))
        s.listen(1)
        print(f"[*] NetKVM Performance Sender Service started on {bind_ip}:{PORT}...")
        while True:
            conn, addr = s.accept()
            with conn:
                data = conn.recv(1024).decode().strip()
                if not data:
                    continue

                if data == "QUIT":
                    run_done.wait()
                    conn.sendall(b"BYE")
                    break

                if data == "RESULTS":
                    run_done.wait()
                    conn.sendall(last_results.encode())
                    continue

                if data == "PLATFORM":
                    conn.sendall(platform.system().encode())
                    continue

                parts = data.split(':')
                if len(parts) < 5:
                    conn.sendall(b"ERR:expected proto:size:threads:ip:duration[:no_sync]")
                    continue

                proto, size, threads, target_ip, duration = parts[:5]
                no_sync = parts[5] if len(parts) > 5 else "0"

                err = validate_request(proto, size, threads, target_ip, duration)
                if err:
                    print(f"[!] Rejected request from {addr[0]}: {err}")
                    conn.sendall(f"ERR:{err}".encode())
                    continue

                # The sender runs the same thread count the receiver asked for;
                # oversubscribed threads make rows CPU-limited, not driver-limited
                if int(threads) > (os.cpu_count() or 1):
                    print(f"[!] WARNING: {threads} threads oversubscribe "
                          f"{os.cpu_count() or 1} CPUs on this sender; "
                          f"throughput may be CPU-limited")

                # Serialize runs: never overlap two ntttcp processes sharing state
                run_done.wait()
                xml_file = f"ntttcp_send_{os.getpid()}_{tests_run}.xml"

                is_udp = "-u" if proto == "UDP" else ""

                if IS_WINDOWS:
                    timeout_ms = (int(duration) + 30) * 1000
                    cmd = [NTTTCP_CLIENT, "-s", "-m", f"{threads},*,{target_ip}"]
                    if is_udp:
                        cmd.append(is_udp)
                    cmd += ["-l", str(size), "-t", str(duration), "-to", str(timeout_ms)]
                    if no_sync == "1":
                        cmd.append("-ns")
                    cmd += ["-xml", xml_file]
                else:
                    nic = detect_interface(target_ip)
                    cmd = [NTTTCP_CLIENT, f"-s{target_ip}"]
                    if is_udp:
                        cmd.append(is_udp)
                    cmd += ["-b", str(size), "-P", str(threads), "-n", "1", "-N"]
                    if nic:
                        cmd += ["--show-nic-packets", nic]
                    cmd += ["-t", str(duration), "-x", xml_file]

                print(f"[!] Executing: {' '.join(cmd)}")
                # ACK the receiver before spawning ntttcp: the receiver uses
                # this as its "sender accepted and is starting" alignment
                # point instead of guessing with a blind sleep
                conn.sendall(b"ACK")
                tests_run += 1
                run_done.clear()
                t = threading.Thread(target=run_ntttcp, args=(cmd, xml_file, duration))
                t.daemon = True
                t.start()

    elapsed = datetime.now() - start_time
    print(f"\n[*] Sender service finished: {tests_run} tests executed, total time: {elapsed}")

if __name__ == "__main__":
    start_sender_service()
