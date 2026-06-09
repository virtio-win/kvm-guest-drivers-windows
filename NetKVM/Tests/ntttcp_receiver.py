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
SENDER_IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.122.220"
TEST_MODE = sys.argv[2] if len(sys.argv) > 2 else "all"  # all, tcp, udp
CONTROL_PORT = 9999
TEST_TIME = 10  # Set to 300 for formal testing (Microsoft recommended duration)
CROSS_PLATFORM = False  # Auto-detected: True when sender and receiver are different OS

def get_my_ip():
    """Auto-detect local IP by checking which interface routes to SENDER_IP."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect((SENDER_IP, 1))  # UDP, no actual data sent
        return s.getsockname()[0]

MY_IP = get_my_ip()

def detect_interface(target_ip):
    """Auto-detect network interface name for a given target IP via routing table."""
    try:
        out = subprocess.check_output(["ip", "route", "get", target_ip],
                                      text=True, stderr=subprocess.DEVNULL)
        m = re.search(r'dev\s+(\S+)', out)
        return m.group(1) if m else ""
    except Exception:
        return ""

INTERFACE_NAME = "" if IS_WINDOWS else detect_interface(SENDER_IP)

# Thread counts to test (iterate like iperf_wrapper's PARALLEL_THREADS)
THREAD_COUNTS = [1, 2, 4, 8, 16]

# Generate results filename with timestamp
TIMESTAMP = datetime.now().strftime("%Y%m%d_%H%M%S")
CSV_FILENAME = f"ntttcp_results_{TIMESTAMP}.csv"
LOG_FILENAME = f"ntttcp_results_{TIMESTAMP}.log"

# Packet sizes to test
PACKET_SIZES = [32, 64, 128, 256, 512, 1024, 1460, 2048, 4096, 8192, 16384, 32768, 65536]
UDP_SIZES = [32, 64, 128, 256, 512, 1024, 1472]  # MTU-safe sizes

# Build scenarios based on test mode
def build_scenarios():
    scenarios = []
    if TEST_MODE in ("all", "tcp"):
        for threads in THREAD_COUNTS:
            for size in PACKET_SIZES:
                scenarios.append(("TCP", size, threads))
    if TEST_MODE in ("all", "udp"):
        for threads in THREAD_COUNTS:
            for size in UDP_SIZES:
                scenarios.append(("UDP", size, threads))
    return scenarios

SCENARIOS = build_scenarios()

log_file = None

def log(msg=""):
    """Print to both console and log file."""
    print(msg)
    if log_file:
        log_file.write(msg + "\n")
        log_file.flush()

def wait_for_sender():
    """Wait for sender service to come online, detect its platform."""
    global CROSS_PLATFORM
    log(f"Waiting for sender service at {SENDER_IP}:{CONTROL_PORT}...")
    while True:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.settimeout(3)
                s.connect((SENDER_IP, CONTROL_PORT))
                s.sendall(b"PLATFORM")
                sender_platform = s.recv(64).decode().strip()
            break
        except (ConnectionRefusedError, socket.timeout, OSError):
            time.sleep(2)
    my_platform = platform.system()
    CROSS_PLATFORM = (sender_platform != my_platform) and sender_platform != ""
    if CROSS_PLATFORM:
        log(f"Sender is online ({sender_platform}). Local: {my_platform}, IP: {MY_IP}")
        log(f"Cross-platform detected, using no-sync mode.\n")
    else:
        log(f"Sender is online. Local IP: {MY_IP}\n")

def signal_sender(proto, size, threads, duration):
    """Signals the remote sender with test parameters and duration."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(5)
            s.connect((SENDER_IP, CONTROL_PORT))
            no_sync = "1" if CROSS_PLATFORM else "0"
            s.sendall(f"{proto}:{size}:{threads}:{MY_IP}:{duration}:{no_sync}".encode())
        return True
    except Exception:
        return False

def get_sender_results():
    """Connect to sender and retrieve last test results (throughput:cpu:pps)."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(30)
            s.connect((SENDER_IP, CONTROL_PORT))
            s.sendall(b"RESULTS")
            data = s.recv(1024).decode().strip()
            parts = data.split(':')
            return float(parts[0]), float(parts[1]), int(parts[2])
    except Exception:
        return 0.0, 0.0, 0

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
            user = float(root.findtext('user', '0'))
            softirq = float(root.findtext('softirq', '0'))
            system = float(root.findtext('system', '0'))
            cpu = round(user + system + softirq, 2)
    except Exception as e:
        log(f"  XML parse error: {e}")
    return mbps, cpu, pps

# Main Execution
start_time = datetime.now()
wait_for_sender()
log_file = open(LOG_FILENAME, mode='w')
with open(CSV_FILENAME, mode='w', newline='') as csv_file:
    csv_writer = csv.writer(csv_file)
    csv_writer.writerow(["Protocol", "Size(B)", "Threads",
                         "Recv_Thr(Mbps)", "Recv_PPS", "Recv_CPU(%)",
                         "Send_Thr(Mbps)", "Send_PPS", "Send_CPU(%)"])

    log(f"### NetKVM Performance Report (Mode: {TEST_MODE}, Duration: {TEST_TIME}s)")
    log(f"### Results saved to: {CSV_FILENAME}")
    hdr = "| {:8} | {:7} | {:4} | {:>10} | {:>7} | {:>6} | {:>10} | {:>7} | {:>6} |"
    sep = "| {:8} | {:7} | {:4} | {:>10} | {:>7} | {:>6} | {:>10} | {:>7} | {:>6} |"
    log(hdr.format("Protocol", "Size(B)", "Thr",
                    "R_Mbps", "R_PPS", "R_CPU%", "S_Mbps", "S_PPS", "S_CPU%"))
    log(sep.format(":---", ":---", ":--", ":---", ":---", ":---", ":---", ":---", ":---"))

    def kill_proc(proc):
        if IS_WINDOWS:
            subprocess.run(["taskkill", "/f", "/pid", str(proc.pid)],
                           capture_output=True, timeout=10)
        else:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)

    timeout_s = TEST_TIME + 45
    failed = 0
    for idx, (proto, size, threads) in enumerate(SCENARIOS):
        try:
            is_udp = "-u" if proto == "UDP" else ""
            recv_l = max(size, 65536) if proto == "UDP" else size
            xml_file = f"ntttcp_recv_{os.getpid()}.xml"
            if os.path.exists(xml_file):
                os.remove(xml_file)
            if idx > 0:
                time.sleep(2)

            if IS_WINDOWS:
                timeout_ms = (TEST_TIME + 30) * 1000
                rcv_cmd = [NTTTCP_CLIENT, "-r"]
                if is_udp:
                    rcv_cmd.append(is_udp)
                rcv_cmd += ["-m", f"{threads},*,{MY_IP}", "-l", str(recv_l),
                            "-t", str(TEST_TIME), "-to", str(timeout_ms)]
                if CROSS_PLATFORM:
                    rcv_cmd.append("-ns")
                rcv_cmd += ["-xml", xml_file]
            else:
                rcv_cmd = f"{NTTTCP_CLIENT} -r {is_udp} -b {recv_l} -P {threads} -N"
                if INTERFACE_NAME:
                    rcv_cmd += f" --show-nic-packets {INTERFACE_NAME}"
                rcv_cmd += f" -x {xml_file} -t {TEST_TIME}"

            if IS_WINDOWS:
                proc = subprocess.Popen(rcv_cmd, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, universal_newlines=True)
            else:
                proc = subprocess.Popen(rcv_cmd, shell=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, universal_newlines=True,
                                        preexec_fn=os.setsid)
            time.sleep(2)

            if signal_sender(proto, size, threads, TEST_TIME):
                try:
                    stdout, stderr = proc.communicate(timeout=timeout_s)
                except subprocess.TimeoutExpired:
                    kill_proc(proc)
                    stdout, stderr = proc.communicate()
                    log(f"  WARNING: ntttcp timed out")

                if os.path.exists(xml_file):
                    r_thr, r_cpu, r_pps = parse_stats_xml(xml_file)
                else:
                    log(f"  WARNING: XML not generated. stderr: {stderr.strip()}")
                    r_thr, r_cpu, r_pps = 0.0, 0.0, 0

                s_thr, s_cpu, s_pps = get_sender_results()

                row = "| {:8} | {:7} | {:4} | {:>10} | {:>7} | {:>6} | {:>10} | {:>7} | {:>6} |"
                log(row.format(proto, size, threads,
                               r_thr, r_pps, f"{r_cpu}%", s_thr, s_pps, f"{s_cpu}%"))
                csv_writer.writerow([proto, size, threads,
                                     r_thr, r_pps, r_cpu, s_thr, s_pps, s_cpu])
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
            s.connect((SENDER_IP, CONTROL_PORT))
            s.sendall(b"QUIT")
            s.recv(16)
    except Exception:
        pass

    summary = f"\nTest completed at {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}, total time: {datetime.now() - start_time}"
    if failed > 0:
        summary += f", {failed}/{len(SCENARIOS)} tests failed"
    log(summary)

log_file.close()
