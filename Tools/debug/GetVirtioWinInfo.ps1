#  This script quickly gathers virtio-win component information including
#  driver versions, MSI version, QEMU Guest Agent version, and reboot status.
#
#  This is a lightweight, fast diagnostic tool designed to help customers
#  quickly identify version inconsistencies in their virtio-win installation.

#  Copyright (c) 2026 Red Hat, Inc. and/or its affiliates. All rights reserved.

#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#  3. Neither the names of the copyright holders nor the names of their contributors
#     may be used to endorse or promote products derived from this software
#     without specific prior written permission.
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
#  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
#  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
#  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
#  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
#  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
#  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
#  SUCH DAMAGE.

#  This script is intentionally separate from CollectSystemInfo.ps1 for the following reasons:
#  1. Fast execution - customers need quick version info without full diagnostics
#  2. Focused purpose - only virtio-win component versions and reboot status
#  3. Different use case - troubleshooting version inconsistencies vs full system diagnostics
#  4. Lightweight - can be run frequently without overhead

param (
    [switch]$Help,
    [switch]$Export,
    [string]$OutputFile = "",
    [switch]$CheckMismatches
)

function Show-Help {
    Write-Host "GetVirtioWinInfo.ps1 - Quick virtio-win component information"
    Write-Host ""
    Write-Host "Usage: .\GetVirtioWinInfo.ps1 [-Export] [-OutputFile <path>] [-CheckMismatches] [-Help]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -Export           Export results to a timestamped file (e.g., VirtioWinInfo_YYYY-MM-DD_HH-mm-ss.txt)"
    Write-Host "  -OutputFile       Specify custom output file path"
    Write-Host "  -CheckMismatches  Highlight version mismatches between drivers and MSI package"
    Write-Host "  -Help             Show this help message"
    Write-Host ""
    Write-Host "This script displays:"
    Write-Host "  - Installed virtio-win driver versions"
    Write-Host "  - virtio-win MSI/installer version"
    Write-Host "  - QEMU Guest Agent version"
    Write-Host "  - System reboot pending status"
    Write-Host "  - Version mismatch warnings (with -CheckMismatches)"
}

function Get-VirtioDriverVersion {
    param(
        [string]$DriverName,
        [string]$ServiceName
    )

    try {
        # Try to get driver info from PnP device
        $device = Get-PnpDevice -Class $DriverName -ErrorAction SilentlyContinue |
                  Where-Object { $_.Status -eq 'OK' } |
                  Select-Object -First 1

        if ($device) {
            $driver = Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_DriverVersion' -ErrorAction SilentlyContinue
            if ($driver) {
                return $driver.Data
            }
        }

        # Fallback: check registry for service driver
        $servicePath = "HKLM:\SYSTEM\CurrentControlSet\Services\$ServiceName"
        if (Test-Path $servicePath) {
            $imagePathProp = Get-ItemProperty -Path $servicePath -Name 'ImagePath' -ErrorAction SilentlyContinue
            if ($imagePathProp -and $imagePathProp.ImagePath) {
                $rawPath = $imagePathProp.ImagePath
                # Handle \SystemRoot\ prefix and parse out arguments
                $driverPath = $rawPath -replace '\\SystemRoot\\', "$env:SystemRoot\"
                $driverPath = $driverPath -replace '\\\?\?\\', ''  # Remove \??\ prefix if present

                # Extract just the path if arguments are present
                if ($driverPath -match '^"([^"]+)"') {
                    $driverPath = $matches[1]
                } elseif ($driverPath -match '^(\S+\.sys)') {
                    $driverPath = $matches[1]
                }

                if (Test-Path $driverPath) {
                    $versionInfo = (Get-Item $driverPath).VersionInfo
                    return $versionInfo.FileVersion
                } else {
                    # Edge case: service exists but driver file is missing/corrupted
                    return "Service Exists - Driver File Missing"
                }
            }
        }

        return "Not Installed"
    } catch {
        return "Error: $_"
    }
}

function Get-VirtioDrivers {
    $drivers = @{
        'NetKVM (Network)' = @{Service = 'netkvm'; Class = 'Net'}
        'viostor (Storage SCSI)' = @{Service = 'viostor'; Class = 'SCSIAdapter'}
        'vioscsi (Storage SCSI)' = @{Service = 'vioscsi'; Class = 'SCSIAdapter'}
        'viorng (RNG)' = @{Service = 'viorng'; Class = 'System'}
        'Balloon (Memory)' = @{Service = 'Balloon'; Class = 'System'}
        'vioserial (Serial)' = @{Service = 'vioser'; Class = 'System'}
        'viofs (Shared Filesystem)' = @{Service = 'viofs'; Class = 'System'}
        'viosock (Socket)' = @{Service = 'viosock'; Class = 'System'}
        'fwcfg (Firmware Config)' = @{Service = 'fwcfg'; Class = 'System'}
        'pvpanic (Panic Notification)' = @{Service = 'pvpanic'; Class = 'System'}
        'viomem (Memory)' = @{Service = 'viomem'; Class = 'System'}
        'vioinput (Input)' = @{Service = 'vioinput'; Class = 'HIDClass'}
        'viogpu (GPU)' = @{Service = 'viogpu'; Class = 'Display'}
    }

    $results = @{}
    foreach ($name in $drivers.Keys) {
        $service = $drivers[$name].Service
        $results[$name] = Get-VirtioDriverVersion -DriverName $drivers[$name].Class -ServiceName $service
    }

    return $results
}

function Get-VirtioWinMSIVersion {
    try {
        # Check Windows Installer registry for virtio-win MSI
        $uninstallPaths = @(
            'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
        )

        foreach ($path in $uninstallPaths) {
            $apps = Get-ItemProperty -Path $path -ErrorAction SilentlyContinue |
                    Where-Object { $_.DisplayName -like '*virtio-win*' -or $_.DisplayName -like '*VirtIO*' }

            if ($apps) {
                foreach ($app in $apps) {
                    if ($app.DisplayVersion) {
                        return @{
                            Version = $app.DisplayVersion
                            Name = $app.DisplayName
                            InstallDate = $app.InstallDate
                        }
                    }
                }
            }
        }

        return @{
            Version = "Not Installed"
            Name = "N/A"
            InstallDate = "N/A"
        }
    } catch {
        return @{
            Version = "Error: $_"
            Name = "N/A"
            InstallDate = "N/A"
        }
    }
}

function Get-QEMUGuestAgentVersion {
    try {
        # Check if QEMU Guest Agent service exists
        $service = Get-Service -Name 'QEMU-GA' -ErrorAction SilentlyContinue

        if (-not $service) {
            return @{
                Version = "Not Installed"
                Status = "N/A"
                Path = "N/A"
            }
        }

        # Get service executable path using CIM (modern replacement for WMI)
        $cimService = Get-CimInstance -ClassName Win32_Service -Filter "Name='QEMU-GA'" -ErrorAction SilentlyContinue
        if ($cimService -and $cimService.PathName) {
            # Parse executable path from PathName (may include arguments)
            # Handle formats: "C:\Path\exe.exe" args, "C:\Path\exe.exe", C:\Path\exe.exe args
            $rawPath = $cimService.PathName
            $servicePath = $rawPath

            if ($rawPath -match '^"([^"]+)"') {
                # Quoted path: extract everything between first pair of quotes
                $servicePath = $matches[1]
            } elseif ($rawPath -match '^(\S+\.exe)') {
                # Unquoted path: extract up to and including .exe
                $servicePath = $matches[1]
            }

            if (Test-Path $servicePath) {
                $versionInfo = (Get-Item $servicePath).VersionInfo
                return @{
                    Version = $versionInfo.FileVersion
                    Status = $service.Status
                    Path = $servicePath
                }
            } else {
                # Edge case: service exists but executable is missing
                return @{
                    Version = "Service Exists - Executable Missing"
                    Status = $service.Status
                    Path = $servicePath
                }
            }
        }

        return @{
            Version = "Unknown"
            Status = $service.Status
            Path = "Path not found"
        }
    } catch {
        return @{
            Version = "Error: $_"
            Status = "N/A"
            Path = "N/A"
        }
    }
}

function Test-RebootPending {
    try {
        $rebootPending = $false
        $reasons = @()

        # Check Component Based Servicing
        $cbsReboot = Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending' -ErrorAction SilentlyContinue
        if ($cbsReboot) {
            $rebootPending = $true
            $reasons += "Component Based Servicing"
        }

        # Check Windows Update
        $wuReboot = Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired' -ErrorAction SilentlyContinue
        if ($wuReboot) {
            $rebootPending = $true
            $reasons += "Windows Update"
        }

        # Check Pending File Rename Operations
        $pendingFileRename = Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' -Name 'PendingFileRenameOperations' -ErrorAction SilentlyContinue
        if ($pendingFileRename -and $pendingFileRename.PendingFileRenameOperations) {
            $rebootPending = $true
            $reasons += "Pending File Rename Operations"
        }

        # Check for pending computer rename
        $activeComputerName = (Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\ComputerName\ActiveComputerName' -ErrorAction SilentlyContinue).ComputerName
        $pendingComputerName = (Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\ComputerName\ComputerName' -ErrorAction SilentlyContinue).ComputerName
        if ($activeComputerName -and $pendingComputerName -and ($activeComputerName -ne $pendingComputerName)) {
            $rebootPending = $true
            $reasons += "Computer Rename Pending"
        }

        # Check for SCCM/ConfigMgr client reboot pending
        try {
            $sccmReboot = Invoke-CimMethod -Namespace 'root\ccm\ClientSDK' -ClassName 'CCM_ClientUtilities' -MethodName 'DetermineIfRebootPending' -ErrorAction SilentlyContinue
            if ($sccmReboot -and ($sccmReboot.RebootPending -or $sccmReboot.IsHardRebootPending)) {
                $rebootPending = $true
                $reasons += "SCCM/ConfigMgr"
            }
        } catch {
            # SCCM not installed or not accessible - this is fine
        }

        return @{
            Pending = $rebootPending
            Reasons = $reasons
        }
    } catch {
        return @{
            Pending = "Error"
            Reasons = @("Error checking reboot status: $_")
        }
    }
}

function Test-VersionMismatch {
    param(
        [string]$DriverVersion,
        [string]$MSIVersion
    )

    # Skip checks for non-installed or error states
    if (($DriverVersion -match "Not Installed|Error|Missing") -or ($MSIVersion -match "Not Installed|Error|Missing")) {
        return $false
    }

    try {
        $driverVer = [version]$DriverVersion
        $msiVer = [version]$MSIVersion

        # Return true if major or minor versions don't match
        return ($driverVer.Major -ne $msiVer.Major) -or ($driverVer.Minor -ne $msiVer.Minor)
    } catch {
        # If we can't parse versions, can't determine mismatch
        return $false
    }
}

function Format-Output {
    param(
        [hashtable]$Drivers,
        [hashtable]$MSI,
        [hashtable]$QEMUGA,
        [hashtable]$Reboot,
        [bool]$CheckMismatches
    )

    $output = @()
    $mismatches = @()

    $output += "=" * 70
    $output += "VirtIO-Win Component Information"
    $output += "Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    $output += "=" * 70
    $output += ""

    # MSI Information
    $output += "VirtIO-Win MSI Package:"
    $output += "-" * 70
    $output += "  Version:      $($MSI.Version)"
    $output += "  Product:      $($MSI.Name)"
    $output += "  Install Date: $($MSI.InstallDate)"
    $output += ""

    # QEMU Guest Agent
    $output += "QEMU Guest Agent:"
    $output += "-" * 70
    $output += "  Version: $($QEMUGA.Version)"
    $output += "  Status:  $($QEMUGA.Status)"
    $output += "  Path:    $($QEMUGA.Path)"
    $output += ""

    # Driver Versions
    $output += "VirtIO Drivers:"
    $output += "-" * 70
    foreach ($driver in $Drivers.Keys | Sort-Object) {
        $version = $Drivers[$driver]
        $mismatchMarker = ""

        if ($CheckMismatches -and (Test-VersionMismatch -DriverVersion $version -MSIVersion $MSI.Version)) {
            $mismatchMarker = " [MISMATCH]"
            $mismatches += "  - $driver : Expected $($MSI.Version), Found $version"
        }

        $output += "  {0,-30} {1}{2}" -f $driver, $version, $mismatchMarker
    }
    $output += ""

    # Version Mismatch Summary (if checking enabled)
    if ($CheckMismatches) {
        $output += "Version Mismatch Analysis:"
        $output += "-" * 70
        if ($MSI.Version -eq "Not Installed") {
            $output += "  Status: No MSI package installed - cannot verify driver version consistency"
            $output += ""
            $output += "  Note: Drivers appear to be manually installed or from Windows Update"
            $output += "        To enable version checking, install the virtio-win MSI package"
        } elseif ($mismatches.Count -gt 0) {
            $output += "  WARNING: $($mismatches.Count) driver(s) do not match MSI package version"
            $output += ""
            foreach ($mismatch in $mismatches) {
                $output += $mismatch
            }
            $output += ""
            $output += "  Recommendation: Reinstall virtio-win MSI package or update drivers"
        } else {
            $output += "  Status: All installed drivers match MSI package version"
        }
        $output += ""
    }

    # Reboot Status
    $output += "System Reboot Status:"
    $output += "-" * 70
    if ($Reboot.Pending -eq $true) {
        $output += "  Status: REBOOT REQUIRED"
        $output += "  Reasons:"
        foreach ($reason in $Reboot.Reasons) {
            $output += "    - $reason"
        }
    } elseif ($Reboot.Pending -eq "Error") {
        $output += "  Status: Error checking reboot status"
        $output += "  Reasons:"
        foreach ($reason in $Reboot.Reasons) {
            $output += "    - $reason"
        }
    } else {
        $output += "  Status: No reboot required"
    }
    $output += ""
    $output += "=" * 70

    return $output
}

# Main execution
if ($Help) {
    Show-Help
    return
}

Write-Host "Gathering VirtIO-Win component information..." -ForegroundColor Cyan
Write-Host ""

# Collect all information
$drivers = Get-VirtioDrivers
$msi = Get-VirtioWinMSIVersion
$qemuGA = Get-QEMUGuestAgentVersion
$reboot = Test-RebootPending

# Format output
$output = Format-Output -Drivers $drivers -MSI $msi -QEMUGA $qemuGA -Reboot $reboot -CheckMismatches $CheckMismatches

# Display to console
$output | ForEach-Object { Write-Host $_ }

# Export if requested
if ($Export -or -not [string]::IsNullOrWhiteSpace($OutputFile)) {
    if ([string]::IsNullOrWhiteSpace($OutputFile)) {
        $OutputFile = Join-Path $PSScriptRoot "VirtioWinInfo_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').txt"
    }

    try {
        $output | Out-File -FilePath $OutputFile -Encoding UTF8
        Write-Host ""
        Write-Host "Output exported to: $OutputFile" -ForegroundColor Green
    } catch {
        Write-Warning "Failed to export to file: $_"
    }
}
