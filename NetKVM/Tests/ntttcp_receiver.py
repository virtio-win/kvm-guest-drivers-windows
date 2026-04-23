import socket
import subprocess
import time
import sys
import csv
import os
from datetime import datetime

# === Configuration ===
NTTTCP_CLIENT = "ntttcp.exe"
SENDER_IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.122.220"
TEST_MODE = sys.argv[2] if len(sys.argv) > 2 else "all"  # all, tcp-nonagle, tcp-nagle, udp
MY_IP = "192.168.122.31"
CONTROL_PORT = 9999
TEST_TIME = 300  # Microsoft recommended duration for formal testing

# Dynamic Thread Count: vCPU count * 2
NUM_CPUS = os.cpu_count()
AUTO_THREADS = NUM_CPUS * 2

# Generate results filename with timestamp
TIMESTAMP = datetime.now().strftime("%Y%m%d_%H%M%S")
CSV_FILENAME = f"perf_results_{TIMESTAMP}.csv"

# Packet sizes to test
PACKET_SIZES = [32, 64, 128, 256, 512, 1024, 1460, 2048, 4096, 8192, 16384, 32768, 65536]
UDP_SIZES = [32, 64, 128, 256, 512, 1024, 1472]  # MTU-safe sizes

# Build scenarios based on test mode
def build_scenarios():
    scenarios = []
    if TEST_MODE in ("all", "tcp-nonagle"):
        # TCP with Nagle Disabled (no batching delay, lower latency)
        for size in PACKET_SIZES:
            scenarios.append(("TCP", size, AUTO_THREADS, True))
    if TEST_MODE in ("all", "tcp-nagle"):
        # TCP with Nagle Enabled (default, batches small packets)
        for size in PACKET_SIZES:
            scenarios.append(("TCP", size, AUTO_THREADS, False))
    if TEST_MODE in ("all", "udp"):
        # UDP tests
        for size in UDP_SIZES:
            scenarios.append(("UDP", size, AUTO_THREADS, False))
    return scenarios

SCENARIOS = build_scenarios()

def signal_sender(proto, size, threads, no_nagle, duration):
    """Signals the remote sender with test parameters and duration"""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(5)
            s.connect((SENDER_IP, CONTROL_PORT))
            nagle_val = "1" if no_nagle else "0"
            # Format: PROTOCOL:SIZE:THREADS:TARGET_IP:NO_NAGLE:DURATION
            s.sendall(f"{proto}:{size}:{threads}:{MY_IP}:{nagle_val}:{duration}".encode())
        return True
    except: return False

def parse_stats(output):
    """
    Extracts Throughput, PPS, and CPU from NTTTCP output.
    Note: Parsing may need adjustment for different ntttcp versions.
    """
    mbps, cpu, pps = "0.0", "0.0", "0"
    lines = [l.strip() for l in output.split('\n') if l.strip()]
    realtime = float(TEST_TIME)
    for i, line in enumerate(lines):
        if "realtime(s)" in line and i + 2 < len(lines):
            data = lines[i+2].split()
            if len(data) >= 2: realtime = float(data[1])
            if len(data) >= 4: mbps = round(float(data[3]) * 8, 2)
    for i, line in enumerate(lines):
        if "Packets Received" in line and i + 2 < len(lines):
            data = lines[i+2].split()
            if len(data) >= 2: 
                pkts = int(data[1])
                pps = int(pkts / realtime)
            if len(data) >= 5: cpu = data[4]
    return mbps, cpu, pps

# Main Execution
start_time = datetime.now()
with open(CSV_FILENAME, mode='w', newline='') as csv_file:
    csv_writer = csv.writer(csv_file)
    csv_writer.writerow(["Protocol", "Size(B)", "Threads", "Nagle", "Throughput(Mbps)", "PPS", "CPU(%)"])

    print(f"\n### NetKVM Performance Report (Mode: {TEST_MODE}, Duration: {TEST_TIME}s, Threads: {AUTO_THREADS})")
    print(f"### Results saved to: {CSV_FILENAME}")
    print("| Protocol | Size (B) | Threads | Nagle    | Thr(Mbps) | PPS     | CPU     |")
    print("| :---     | :---     | :---    | :---     | :---      | :---    | :---    |")

    for proto, size, threads, no_nagle in SCENARIOS:
        is_udp = "-u" if proto == "UDP" else ""
        nagle_str = "Disabled" if (proto == "TCP" and no_nagle) else ("Enabled" if proto == "TCP" else "N/A")
        recv_l = max(size, 65536) if proto == "UDP" else size
        
        rcv_cmd = f"{NTTTCP_CLIENT} -r {is_udp} -m {threads},*,{MY_IP} -l {recv_l} -t {TEST_TIME}"
        
        proc = subprocess.Popen(rcv_cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        time.sleep(2) 
        
        if signal_sender(proto, size, threads, no_nagle, TEST_TIME):
            stdout, stderr = proc.communicate()
            thr, cpu, pps = parse_stats(stdout)
            
            print(f"| {proto:8} | {size:8} | {threads:7} | {nagle_str:8} | {str(thr):9} | {str(pps):7} | {str(cpu):7}% |")
            csv_writer.writerow([proto, size, threads, nagle_str, thr, pps, cpu])
        else:
            print(f"| ERROR: Sender signal failed for {proto} {size}B |")
            proc.kill()

print(f"\nTest completed at {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}, total time: {datetime.now() - start_time}")
