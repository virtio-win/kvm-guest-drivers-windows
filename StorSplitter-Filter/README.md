# StorSplitter Filter Driver

StorSplitter is a Windows kernel-mode storage filter driver built with the Windows Driver Framework (WDF). It acts as a safety layer
between the operating system and the storage stack to prevent I/O failures caused by oversized read/write requests.

By dynamically querying the underlying physical disk for its maximum transfer limits and logical sector size (via SCSI VPD Page `0xB0` 
and disk geometry IOCTLs), StorSplitter knows exactly how much data the hardware can handle at once. When an application or the OS attempts 
a transfer that exceeds this hardware limit, the driver transparently intercepts the request, slices it into smaller safe-sized chunks, 
dispatches them asynchronously, and seamlessly merges the results back to the caller. 

### Key Features
* **Dynamic Hardware Detection:** Automatically detects logical sector sizes (supports standard 512e and Advanced Format 4Kn drives) and hardware-specific transfer limits.
* **Transparent I/O Slicing:** Uses thread-safe WDF contexts and reference counting to split and reassemble massive requests without upper-level software knowing.
* **User-Mode Control:** Exposes a control device endpoint (`\\.\StorSplitterCtrl`) allowing user-mode applications to toggle the splitting behavior system-wide on the fly.
* **Resilient:** Includes strict Non-Paged Pool memory guards to ensure system stability under extreme low-memory conditions and heavy I/O loads.

## Usage

Run an elevated (Administrator) command prompt.

**Install** (a PnP Extension INF; PnP attaches it as a per-device upper filter on
matching VirtIO block disks automatically — reboot to load):
```
pnputil /add-driver StorSplitterFilter.inf /install
shutdown /r /t 0
```
It is scoped to VirtIO block (viostor) disks by hardware ID, so it does not filter
every disk on the system. vioscsi disks are not matched (their SCSI inquiry comes
from the host backend and is not a stable virtio identifier).

**Control at runtime** with `SplitterCtrl.exe`:
```
SplitterCtrl.exe status      # show whether splitting is ENABLED or DISABLED
SplitterCtrl.exe enable      # turn splitting on (system-wide)
SplitterCtrl.exe disable     # turn splitting off (pass-through)
```
Splitting is **enabled by default**. When enabled, oversized reads/writes are sliced automatically — no per-application changes needed. `SplitterCtrl.exe` returns a non-zero exit code on failure.

**Uninstall:** remove the driver package with `pnputil /delete-driver <oemNN.inf> /uninstall` (find the published name via `pnputil /enum-drivers`), then reboot.
