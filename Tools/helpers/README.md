# Helper Scripts

This directory contains various helper scripts for use with the KVM Guest Drivers for Windows.

## Overview

These PowerShell scripts are designed to assist with system configuration, diagnostics, and management tasks related to KVM guest drivers on Windows systems.

## Current Scripts

### EnableKernelMemoryDump.ps1

This script enables the "Always Keep Memory Dump" feature in Windows by setting the appropriate registry key.

#### What it does

1. Sets the `AlwaysKeepMemoryDump` registry value to 1 under `HKLM:\System\CurrentControlSet\Control\CrashControl`.
2. Verifies the change and outputs the result.

## Planned Scripts

Additional helper scripts are planned for this directory. They may include:

- System information collection
- Driver installation and management
- Performance optimization for KVM guests
- Troubleshooting tools

## Usage Guidelines

1. **Prerequisites:**
   - PowerShell (Windows 10/Windows Server 2016 or later recommended)
   - Administrative privileges (for scripts that modify system settings)
   - Ensure scripts run with an appropriate execution policy:
     ```powershell
     Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process -Force
     ```

2. **Running Scripts:**
   - Open PowerShell as an administrator.
   - Navigate to the script's directory.
   - Execute the desired script, e.g.:
     ```powershell
     .\ScriptName.ps1
     ```

3. **Output:**
   - Each script will provide its own output, which may include:
     - Console messages
     - Log files
     - Modified system settings

## Contributing

Contributions to this collection of helper scripts are welcome. If you have ideas for new scripts or improvements to existing ones, please consider the following:

1. Ensure your script has a clear, descriptive name.
2. Include detailed comments explaining the scripts purpose and usage.
3. Follow existing style conventions and PowerShell best practices.
4. Update this README with information about new scripts.
5. If applicable, include error handling and logging.


## Disclaimer

These scripts may modify system settings. Use them at your own risk and always back up your system before making changes.
