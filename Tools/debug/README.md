# Debug Tools

This directory contains diagnostic tools for Windows guest systems running virtio-win drivers.

## Tool Selection Guide

- **CollectSystemInfo.ps1** - Comprehensive diagnostics bundle (1-5 minutes)
- **GetVirtioWinInfo.ps1** - Quick version check and reboot status (~5 seconds)
- **CollectSystemInfo-WinPE.ps1** - Offline diagnostics from WinPE/WinRE

---

# CollectSystemInfo

## Overview

This PowerShell script is designed for comprehensive system diagnostics. It gathers a wide range of information, including system configuration, event logs, driver lists, SetupAPI logs, registry settings, update logs, services, uptime, processes, installed applications, installed KBs (knowledge base articles), network configuration, and optionally, memory dumps.

The collected data is organized into two subfolders within the time-stamped summary folder, one for log and the other for dump. and then compressed into two ZIP archives correspondingly for easy sharing and analysis.

## Usage

1. **Prerequisites:**
   - PowerShell (Windows 10/Windows Server 2016 or later)
   - Administrative privileges (for collecting event logs)
   - Ensure the script runs with an unrestricted execution policy (for Windows 10 and Windows Server 2016): 
     ```powershell   
     Set-ExecutionPolicy -ExecutionPolicy Unrestricted -Scope Process -Force
     ```

2. **Running the Script:**
   - Open PowerShell as an administrator.
   - Navigate to the script's directory.
   - Execute the script:
      ```powershell
      .\CollectSystemInfo.ps1 -IncludeSensitiveData
      ```
      - `-IncludeSensitiveData`: Optional switch to include memory dumps in the collection (use with caution).
      - `-Help`: Provide basic usage of the script.

3. **Output:**
   - A folder named `SystemInfo_YYYY-MM-DD_HH-MM-SS` will be created in the script's directory.
   - This folder contains the collected data folders:
      - A foler named `Log_folder_YYYY-MM-DD_HH-MM-SS` will be created for log data.
      - A ZIP archive named `Log_folder_YYYY-MM-DD_HH-MM-SS.zip` will also be created correspondingly.
      - A foler named `Dump_folder_YYYY-MM-DD_HH-MM-SS` will be created for dump files if add param `-IncludeSensitiveData`.
      - A ZIP archive named `Dump_folder_YYYY-MM-DD_HH-MM-SS.zip` will also be created correspondingly.

## Data Collected

- `msinfo32.txt`: Detailed hardware and software configuration report.
- `system.evtx`, `security.evtx`, `application.evtx`: System, Security, and Application event logs.
- `drv_list.csv`: List of all installed drivers.
- `virtio_disk.txt`: Specific configuration details for Virtio-Win storage drivers.
- `WindowsUpdate.log`: Detailed logs of Windows Update activity.
- `Services.csv`: List of services and their status.
- `WindowsUptime.txt`: Duration since the last system boot.
- `RunningProcesses.csv`: Snapshot of active processes.
- `InstalledApplications.csv`: List of installed applications.
- `InstalledKBs.csv`: List of installed Windows updates.
- `NetworkInterfaces.txt` and `IPConfiguration.txt`: Network configuration details.
- `setupapi*.log`: Logs related to device and driver installations.
- `MEMORY.DMP` and `Minidump` folder: Full or mini memory dumps (if `-IncludeSensitiveData` is used).
- `Collecting_Status.txt`: Generated during data collection and deleted after completion. If the script is interrupted, this file indicates incomplete data collection.

---

# GetVirtioWinInfo

## Overview

A fast, lightweight diagnostic tool designed to quickly identify version inconsistencies in virtio-win installations. This tool was created specifically to help customers troubleshoot driver version mismatches and installation issues without the overhead of full system diagnostics.

## Information Gathered

### VirtIO-Win MSI Package
- MSI/installer version
- Product name
- Installation date

### QEMU Guest Agent
- Version number
- Service status (Running/Stopped)
- Installation path

### VirtIO Drivers (All Types)
- **NetKVM** - Network adapter driver
- **viostor** - SCSI storage controller driver
- **vioscsi** - SCSI storage driver  
- **viorng** - Random Number Generator driver
- **Balloon** - Memory balloon driver
- **vioserial** - Serial port driver
- **viofs** - Shared filesystem (virtiofs) driver
- **viosock** - Socket communication driver
- **fwcfg** - Firmware configuration device driver
- **pvpanic** - Panic notification device driver
- **viomem** - Memory balloon device driver
- **vioinput** - Input device driver (keyboard/mouse/tablet)
- **viogpu** - GPU/Display adapter driver

### System Reboot Status
Detects pending reboots from multiple sources:
- Component Based Servicing (CBS)
- Windows Update
- Pending File Rename Operations
- Computer Rename pending
- SCCM/ConfigMgr (if installed)

## Usage

### Basic Usage
```powershell
.\GetVirtioWinInfo.ps1
```
Displays all component versions and system status to console.

### Check Version Mismatches
```powershell
.\GetVirtioWinInfo.ps1 -CheckMismatches
```
Analyzes drivers against the MSI package version and highlights inconsistencies with `[MISMATCH]` markers. Particularly useful for:
- Partial driver updates
- Mixed installation sources (MSI vs manual installation)
- Troubleshooting "driver not working" issues

### Export Results
```powershell
.\GetVirtioWinInfo.ps1 -Export
```
Saves output to timestamped file: `VirtioWinInfo_YYYY-MM-DD_HH-MM-SS.txt`

### Custom Output File
```powershell
.\GetVirtioWinInfo.ps1 -OutputFile "C:\diagnostics\virtio-check.txt"
```

### Combined Usage
```powershell
.\GetVirtioWinInfo.ps1 -CheckMismatches -Export
```
Performs mismatch analysis and exports results to file.

### Help
```powershell
.\GetVirtioWinInfo.ps1 -Help
```

## Example Output

### Without Mismatch Detection
```
======================================================================
VirtIO-Win Component Information
Generated: 2026-07-16 14:41:33
======================================================================

VirtIO-Win MSI Package:
----------------------------------------------------------------------
  Version:      1.9.58
  Product:      Virtio-win-driver-installer
  Install Date: 20260716

QEMU Guest Agent:
----------------------------------------------------------------------
  Version: 110.2.2
  Status:  Running
  Path:    C:\Program Files\Qemu-ga\qemu-ga.exe

VirtIO Drivers:
----------------------------------------------------------------------
  Balloon (Memory)               1.9.58.0
  NetKVM (Network)               1.9.58.0
  viofs (Shared Filesystem)      1.9.58.0
  viorng (RNG)                   1.9.58.0
  vioscsi (Storage SCSI)         1.9.58.0
  vioserial (Serial)             1.9.58.0
  viosock (Socket)               Not Installed
  viostor (Storage SCSI)         1.9.58.0

System Reboot Status:
----------------------------------------------------------------------
  Status: No reboot required

======================================================================
```

### With Mismatch Detection (`-CheckMismatches`)
```
VirtIO Drivers:
----------------------------------------------------------------------
  Balloon (Memory)               10.0.17763.1007 [MISMATCH]
  NetKVM (Network)               10.0.17763.1 [MISMATCH]
  viofs (Shared Filesystem)      10.0.17763.1007 [MISMATCH]
  viorng (RNG)                   10.0.17763.1007 [MISMATCH]
  vioscsi (Storage SCSI)         10.0.17763.1192 [MISMATCH]
  vioserial (Serial)             10.0.17763.1007 [MISMATCH]
  viosock (Socket)               10.0.17763.1007 [MISMATCH]
  viostor (Storage SCSI)         10.0.17763.1192 [MISMATCH]

Version Mismatch Analysis:
----------------------------------------------------------------------
  WARNING: 8 driver(s) do not match MSI package version

  - Balloon (Memory) : Expected 1.9.58, Found 10.0.17763.1007
  - NetKVM (Network) : Expected 1.9.58, Found 10.0.17763.1
  - vioscsi (Storage SCSI) : Expected 1.9.58, Found 10.0.17763.1192
  ...

  Recommendation: Reinstall virtio-win MSI package or update drivers
```

## Troubleshooting Common Issues

### No MSI Detected
If the MSI shows "Not Installed", drivers were likely installed manually via Device Manager or Windows Update rather than the virtio-win MSI package. The `-CheckMismatches` feature will indicate this scenario.

### Service Exists - Driver/Executable Missing  
Indicates corrupted installation where registry entries exist but files are missing. Reinstall virtio-win package to resolve.

### Version Mismatches
Common causes:
- Partial MSI upgrade (some drivers updated, others weren't)
- Manual driver updates via Device Manager
- Windows Update overriding MSI-installed drivers
- Mixed installation from multiple virtio-win versions

**Solution:** Reinstall the complete virtio-win MSI package to ensure consistency.

## Prerequisites

- Windows 10 / Windows Server 2016 or later
- PowerShell 5.1 or later
- May require Administrator privileges for complete information

## Related Tools

- **CollectSystemInfo.ps1** - Full system diagnostics including virtio-win information plus comprehensive system data
- **CollectSystemInfo-WinPE.ps1** - Offline diagnostics when Windows won't boot

---

## Contributing

Contributions are welcome! Feel free to open issues or submit pull requests.
