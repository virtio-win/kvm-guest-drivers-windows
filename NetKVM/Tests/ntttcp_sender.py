import socket
import subprocess

NTTTCP_CLIENT = "ntttcp.exe"

# Passive service to launch sender commands on request
def start_sender_service():
    PORT = 9999
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('0.0.0.0', PORT))
        s.listen(1)
        print("[*] NetKVM Performance Sender Service started (Port 9999)...")
        while True:
            conn, addr = s.accept()
            with conn:
                # Command format: "PROTOCOL:SIZE:THREADS:TARGET_IP:NO_NAGLE:DURATION"
                data = conn.recv(1024).decode().strip()
                if not data: continue
                parts = data.split(':')
                if len(parts) < 6: continue
                
                proto, size, threads, target_ip, no_nagle, duration = parts
                
                is_udp = "-u" if proto == "UDP" else ""
                nagle_flag = "-ndl" if (proto == "TCP" and no_nagle == "1") else ""
                
                # Execute ntttcp sender with the duration requested by the receiver
                cmd = f"{NTTTCP_CLIENT} -s -m {threads},*,{target_ip} {is_udp} {nagle_flag} -l {size} -t {duration}"
                print(f"[!] Executing: {cmd}")
                subprocess.Popen(cmd, shell=True) 

if __name__ == "__main__":
    start_sender_service()
