import socket
import subprocess
import signal
import platform
import os
import re
import threading
import xml.etree.ElementTree as ET
from datetime import datetime

IS_WINDOWS = platform.system() == "Windows"
NTTTCP_CLIENT = "ntttcp.exe" if IS_WINDOWS else "ntttcp"

last_results = "0.0:0.0:0"
tests_run = 0
run_done = threading.Event()
run_done.set()

def detect_interface(target_ip):
    """Auto-detect network interface name for a given target IP via routing table."""
    try:
        out = subprocess.check_output(["ip", "route", "get", target_ip],
                                      text=True, stderr=subprocess.DEVNULL)
        m = re.search(r'dev\s+(\S+)', out)
        return m.group(1) if m else ""
    except Exception:
        return ""

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
            user = float(root.findtext('user', '0'))
            softirq = float(root.findtext('softirq', '0'))
            system = float(root.findtext('system', '0'))
            cpu = round(user + system + softirq, 2)
    except Exception as e:
        print(f"  XML parse error: {e}")
    return mbps, cpu, pps

def run_ntttcp(cmd, xml_file, duration):
    global last_results
    try:
        if os.path.exists(xml_file):
            os.remove(xml_file)
        timeout_s = int(duration) + 60
        if IS_WINDOWS:
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)
            try:
                proc.communicate(timeout=timeout_s)
            except subprocess.TimeoutExpired:
                subprocess.run(["taskkill", "/f", "/pid", str(proc.pid)],
                               capture_output=True, timeout=10)
                proc.communicate()
                print(f"[!] WARNING: ntttcp timed out after {duration}s + 60s margin")
        else:
            proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE,
                                    preexec_fn=os.setsid)
            try:
                proc.communicate(timeout=timeout_s)
            except subprocess.TimeoutExpired:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                proc.communicate()
                print(f"[!] WARNING: ntttcp timed out after {duration}s + 60s margin")

        if os.path.exists(xml_file):
            mbps, cpu, pps = parse_sender_xml(xml_file)
        else:
            mbps, cpu, pps = 0.0, 0.0, 0
        last_results = f"{mbps}:{cpu}:{pps}"
        print(f"[=] Send stats: Thr={mbps}Mbps CPU={cpu}% PPS={pps}")
    finally:
        run_done.set()

def start_sender_service():
    global tests_run
    PORT = 9999
    start_time = datetime.now()
    xml_file = f"ntttcp_send_{os.getpid()}.xml"
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(('0.0.0.0', PORT))
        s.listen(1)
        print(f"[*] NetKVM Performance Sender Service started (Port {PORT})...")
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
                    continue

                proto, size, threads, target_ip, duration = parts[:5]
                no_sync = parts[5] if len(parts) > 5 else "0"
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
                    cmd = f"{NTTTCP_CLIENT} -s{target_ip} {is_udp} -b {size} -P {threads} -n 1 -N"
                    if nic:
                        cmd += f" --show-nic-packets {nic}"
                    cmd += f" -t {duration} -x {xml_file}"

                print(f"[!] Executing: {cmd}")
                tests_run += 1
                run_done.clear()
                t = threading.Thread(target=run_ntttcp, args=(cmd, xml_file, duration))
                t.daemon = True
                t.start()

    elapsed = datetime.now() - start_time
    print(f"\n[*] Sender service finished: {tests_run} tests executed, total time: {elapsed}")

if __name__ == "__main__":
    start_sender_service()
