# VSock TCP Bridge for Windows

`vstbridge.exe` is a **Windows-compatible alternative** to the Linux `systemd-ssh-proxy` utility. It enables communication with virtual machines via **VSock (Virtio Sockets)**, allowing SSH access through a transport mechanism not natively supported in Windows.

🔗 Reference Linux implementation:
[systemd ssh-proxy.c (GitHub)](https://github.com/systemd/systemd/blob/v257.6/src/ssh-generator/ssh-proxy.c)

---

## ❓ Why This Project?

Windows lacks built-in support for VSock (Virtio or Hyper-V sockets), which is available on Linux systems and used by `systemd` to enable seamless SSH access to virtual machines.

`vstbridge.exe` bridges that gap by providing a **VSock-to-TCP proxy service**, allowing Windows systems to run commands in guest virtual machines via SSH over VSock.

This is similar in concept to `hvc.exe`, the Hyper-V socket support utility.

---

## 🚀 Usage

### 🧩 SSH with Libvirt Proxy

For general guidance on SSH proxying via VSock using `libvirt`, refer to the official documentation:
📖 [Libvirt SSH Proxy](https://libvirt.org/ssh-proxy.html)

### 🔧 Pure SSH Example

To SSH into a Windows VM using VSock, run:

```bash
ssh -o ProxyCommand="socat - VSOCK-CONNECT:<vm_id>:22" <win_user>@0.0.0.0
```

Replace:

* `<vm_id>` — with the VSock CID (Context ID) of the guest VM.
* `<win_user>` — with the actual username in the Windows guest.

> ⚠️ Ensure that port `22` is open in the guest VM and SSH server is running.

---

## 🛠️ Installation

1. Download `vstbridge.exe` and place it in a suitable location, such as `C:\Program Files\vstbridge\`.
2. (Optional) Register it as a Windows service to start automatically.
3. Ensure your firewall allows traffic on the required ports, typically TCP 22.

---

## 💡 How It Works

* VSock is used as a fast, host-to-guest transport.
* The bridge listens on the VSock channel and forwards traffic to TCP (usually SSH on port 22).
* You configure your SSH client (or `socat`) to use this bridge as a `ProxyCommand`.

---

## 🖥️ System Requirements

* Windows 10/11 or Windows Server 2019/2022/2025 with enabled SSH service
* Administrator privileges to bind services or listen on VSock
* Virtualization platform supporting VSock (e.g., QEMU/KVM with virtio-vsock)
