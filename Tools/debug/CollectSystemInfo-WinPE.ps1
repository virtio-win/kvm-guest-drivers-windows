#  This script collects various system information for diagnostic
#  purposes in WinPE/WinRE environments. The collected data includes 
#  system configuration, event logs, driver lists, registry information, 
#  update logs, services, installed applications, installed KBs, and memory dumps.

#  Copyright (c) 2025 Red Hat, Inc. and/or its affiliates. All rights reserved.

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

#  This script is designed to run in WinPE/WinRE environments
#  For gathering event logs run the script as an administrator

#  IncludeSensitiveData is used to include memory dumps add this parameter to your command line to collect them
#  Example:  .\CollectSystemInfo-WinPE.ps1 -IncludeSensitiveData

param (
    [switch]$IncludeSensitiveData,
    [switch]$Help
)

Add-Type -AssemblyName 'System.IO.Compression.FileSystem'

function Compress-Files {
    param (
        [string]$SourcePath,
        [string]$DestinationPath
    )

    [System.IO.Compression.ZipFile]::CreateFromDirectory($SourcePath, $DestinationPath)
}

# ---------- Helpers (logging + offline hive) ----------
function Write-Step($name){ Write-Host "[$name] started..." }
function Write-Done($name, $extra = ""){ if ($extra) { Write-Host "[$name] completed. $extra" } else { Write-Host "[$name] completed." } }
function Warn($msg){ Write-Warning $msg }

function Mount-OfflineHive($hivePath, $mountKey){
    if (-not (Test-Path $hivePath)) { throw "Hive not found at $hivePath" }
    & reg.exe load $mountKey $hivePath | Out-Null
}

function Unmount-OfflineHive($mountKey){
    try { [gc]::Collect(); Start-Sleep -Milliseconds 250 } catch {}
    & reg.exe unload $mountKey 2>$null | Out-Null
}

function Get-CurrentControlSet($mountKey){
    $sel = Get-ItemProperty -Path "HKLM:\$mountKey\Select" -ErrorAction SilentlyContinue
    $cur = if ($sel -and $sel.Current) { [int]$sel.Current } else { 1 }
    return ('ControlSet{0:D3}' -f $cur)
}

function Show-Help {
    Write-Host "Usage: .\CollectSystemInfo-WinPE.ps1 [-IncludeSensitiveData] [-Help]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -IncludeSensitiveData  Include sensitive data (memory dump)"
    Write-Host "  -Help                  Show this help message"
    Write-Host ""
    Write-Host "This script is designed to run in WinPE/WinRE environments."
    Write-Host "If no parameters are provided, the script will run with default behavior."
}

function Export-SystemConfiguration-Offline {
    param(
        [Parameter(Mandatory=$true)][string]$WindowsDrive
    )
    $softHivePath = Join-Path $WindowsDrive 'Windows\System32\config\SOFTWARE'
    $sysHivePath  = Join-Path $WindowsDrive 'Windows\System32\config\SYSTEM'
    $softKeyName = 'OfflineSOFTWARE'
    $sysKeyName  = 'OfflineSYSTEM'
    $softMountKey = "HKLM\$softKeyName"
    $sysMountKey  = "HKLM\$sysKeyName"
    try {
        if (-not (Test-Path $softHivePath)) { throw "SOFTWARE hive not found at $softHivePath" }
        if (-not (Test-Path $sysHivePath))  { throw "SYSTEM hive not found at $sysHivePath" }
        Mount-OfflineHive -hivePath $softHivePath -mountKey $softMountKey
        Mount-OfflineHive -hivePath $sysHivePath  -mountKey $sysMountKey

        $cv = Get-ItemProperty -Path ("HKLM:\{0}\Microsoft\Windows NT\CurrentVersion" -f $softKeyName) -ErrorAction SilentlyContinue
        $ccs = Get-CurrentControlSet -mountKey $sysKeyName
        $cn  = Get-ItemProperty -Path ("HKLM:\{0}\{1}\Control\ComputerName\ComputerName" -f $sysKeyName, $ccs) -ErrorAction SilentlyContinue

        $installDate = ''
        try {
            if ($cv.InstallDate) {
                $epoch = [DateTimeOffset]::FromUnixTimeSeconds([int64]$cv.InstallDate).UtcDateTime
                $installDate = $epoch.ToString('yyyy-MM-dd HH:mm:ss') + ' UTC'
            }
        } catch { 
            Write-Warning "Failed to parse install date from offline hive: $_" 
        }

        $ubr = ''
        if ($cv.PSObject.Properties.Name -contains 'UBR') { $ubr = "." + [string]$cv.UBR }

        $lines = @()
        $lines += "OS Name:            $($cv.ProductName)"
        $lines += "OS Edition:         $($cv.EditionID)"
        $lines += "OS Version:         $($cv.CurrentVersion) ($($cv.DisplayVersion))"
        $lines += "OS Build:           $($cv.CurrentBuild)$ubr"
        if ($cv.BuildLabEx) { $lines += "Build Lab:         $($cv.BuildLabEx)" }
        $lines += "Installation Type:  $($cv.InstallationType)"
        if ($installDate) { $lines += "Install Date:       $installDate" }
        $lines += "Computer Name:      $($cn.ComputerName)"

        $out = Join-Path $logfolderPath 'msinfo32.txt'
        $lines -join [Environment]::NewLine | Out-File -FilePath $out -Encoding UTF8
        Write-Host 'System configuration collection completed.'
    } catch {
        Write-Warning "Failed to collect system configuration: $_"
    } finally {
        try { [gc]::Collect(); Start-Sleep -Milliseconds 250 } catch {}
        Unmount-OfflineHive -mountKey $softMountKey
        Unmount-OfflineHive -mountKey $sysMountKey
    }
}

function Export-EventLogs-Offline {
    param(
        [Parameter(Mandatory=$true)][string]$WindowsDrive
    )
    try {
        $logNames = @('application','system','security')
        foreach ($name in $logNames) {
            $evtxDir = Join-Path $WindowsDrive 'Windows\System32\winevt\Logs'
            $src = Join-Path $evtxDir "$name.evtx"
            try {
                if (Test-Path $src) {
                    Copy-Item -Path $src -Destination (Join-Path $logfolderPath "$name.evtx") -ErrorAction Stop
                }
            } catch {
                Write-Warning "Failed to copy offline EVTX file ${src}: $_"
            }
        }
        Write-Host "Copied EVTX files (application/system/security) from $WindowsDrive to $logfolderPath."
    } catch {
        Write-Warning "Failed to collect event logs: $_"
    }
}

function Export-DriversList-Offline {
    param(
        [Parameter(Mandatory=$true)][string]$WindowsDrive
    )
    try {
        $targetCsv = Join-Path $logfolderPath 'drv_list.csv'
        $rows = @()

        $repo = Join-Path $WindowsDrive 'Windows\System32\DriverStore\FileRepository'
        if (Test-Path $repo) {
            foreach ($dir in Get-ChildItem -Path $repo -Directory -ErrorAction SilentlyContinue) {
                try {
                    foreach ($inf in Get-ChildItem -Path (Join-Path $dir.FullName '*.inf') -ErrorAction SilentlyContinue) {
                        $rows += [pscustomobject]@{
                            Directory = $dir.Name
                            InfName   = $inf.Name
                            InfPath   = $inf.FullName
                        }
                    }
                } catch {
                    Write-Warning ("Failed to enumerate INF files in '{0}': {1}" -f $dir.FullName, $_)
                }
            }
        }

        $infDir = Join-Path $WindowsDrive 'Windows\INF'
        if (Test-Path $infDir) {
            foreach ($inf in Get-ChildItem -Path (Join-Path $infDir 'oem*.inf') -ErrorAction SilentlyContinue) {
                $rows += [pscustomobject]@{
                    Directory = 'Windows\\INF'
                    InfName   = $inf.Name
                    InfPath   = $inf.FullName
                }
            }
        }

        if ($rows -and $rows.Count -gt 0) {
            $rows | Export-Csv -Path $targetCsv -NoTypeInformation -Force
            Write-Host 'Drivers list collection completed.'
        } else {
            Write-Warning 'No drivers found to export from offline Windows.'
        }
    } catch {
        Write-Warning "Failed to collect drivers list: $_"
    }
}

function Export-VirtioWinStorageDrivers-Offline {
    param(
        [Parameter(Mandatory=$true)][string]$WindowsDrive
    )
    $hivePath = Join-Path $WindowsDrive 'Windows\System32\config\SYSTEM'
    $mountName = 'OfflineSYSTEM'
    $mountKey  = "HKLM\$mountName"
    try {
        if (-not (Test-Path $hivePath)) { throw "SYSTEM hive not found at $hivePath" }
        Mount-OfflineHive -hivePath $hivePath -mountKey $mountKey

        $ccs  = Get-CurrentControlSet -mountKey $mountName
        $base = ("HKLM:\{0}\{1}\Services" -f $mountName, $ccs)

        $queries = @(
            @{ Path = (Join-Path $base 'Disk');                Values = @('IoTimeoutValue', 'TimeoutValue') },
            @{ Path = (Join-Path $base 'viostor\Parameters');  Values = @('IoTimeoutValue', 'TimeoutValue') },
            @{ Path = (Join-Path $base 'vioscsi\Parameters');  Values = @('IoTimeoutValue', 'TimeoutValue') }
        )

        $outFile = Join-Path $logfolderPath 'virtio_disk.txt'
        foreach ($q in $queries) {
            foreach ($v in $q.Values) {
                try {
                    $prop = Get-ItemProperty -Path $q.Path -Name $v -ErrorAction SilentlyContinue
                    $val  = if ($null -ne $prop -and ($prop.PSObject.Properties.Name -contains $v)) { $prop.$v } else { '' }
                    "$($q.Path)\$v : $val" | Out-File -FilePath $outFile -Append -Encoding UTF8
                } catch {
                    Write-Warning "Failed to read $v at $($q.Path): $_"
                }
            }
        }
        Write-Host 'Virtio-Win storage drivers configuration collection completed.'
    } catch {
        Write-Warning "Failed to collect Virtio-Win storage drivers configuration: $_"
    } finally {
        try { [gc]::Collect(); Start-Sleep -Milliseconds 250 } catch {}
        Unmount-OfflineHive -mountKey $mountKey
    }
}

function Export-WindowsUpdateLogs-Offline {
    param(
        [Parameter(Mandatory=$true)][string]$WindowsDrive
    )
    try {
        $logPath = Join-Path $logfolderPath 'WindowsUpdate.log'
        if (Test-Path $logPath) { Remove-Item -Path $logPath -Force -ErrorAction SilentlyContinue }

        $found = $false
        $dismDir = Join-Path $WindowsDrive 'Windows\Logs\DISM'
        if (Test-Path $dismDir) {
            $logs = Get-ChildItem -Path $dismDir -Filter '*.log' -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime
            if ($logs -and $logs.Count -gt 0) {
                foreach ($lf in $logs) {
                    try {
                        "===== $($lf.Name) =====" | Out-File -FilePath $logPath -Append -Encoding UTF8
                        Get-Content -Path $lf.FullName -ErrorAction Stop | Out-File -FilePath $logPath -Append -Encoding UTF8
                        "" | Out-File -FilePath $logPath -Append -Encoding UTF8
                        $found = $true
                    } catch {
                        Write-Warning ("Failed to append DISM log '{0}': {1}" -f $lf.FullName, $_)
                    }
                }
            }
        }

        if (-not $found) {
            Write-Warning 'Windows Update logs: no DISM logs found to export.'
        }
        Write-Host 'Windows Update logs collection completed.'
    } catch {
        Write-Warning "Failed to collect Windows Update logs: $_"
    }
}

function Export-ServicesList-Offline {
    param(
        [Parameter(Mandatory=$true)][string]$WindowsDrive
    )
    $hivePath = Join-Path $WindowsDrive 'Windows\System32\config\SYSTEM'
    $mountName = 'OfflineSYSTEM'
    $mountKey  = "HKLM\$mountName"
    try {
        if (-not (Test-Path $hivePath)) { throw "SYSTEM hive not found at $hivePath" }
        Mount-OfflineHive -hivePath $hivePath -mountKey $mountKey

        $ccs = Get-CurrentControlSet -mountKey $mountName
        $servicesPath = ("HKLM:\{0}\{1}\Services" -f $mountName, $ccs)

        $rows = @()
        foreach ($svcKey in Get-ChildItem -Path $servicesPath -ErrorAction SilentlyContinue) {
            try {
                $p = Get-ItemProperty -Path $svcKey.PSPath -ErrorAction SilentlyContinue
                $startDword = $p.Start
                $delayed    = $p.DelayedAutoStart
                $startType  = switch ($startDword) {
                    0 { 'Boot' }
                    1 { 'System' }
                    2 { if ($delayed -eq 1) { 'Automatic (Delayed)' } else { 'Automatic' } }
                    3 { 'Manual' }
                    4 { 'Disabled' }
                    Default { [string]$startDword }
                }
                $rows += [pscustomobject]@{
                    Name        = $svcKey.PSChildName
                    DisplayName = $p.DisplayName
                    StartType   = $startType
                }
            } catch {
                Write-Warning ("Failed to read service key '{0}': {1}" -f $svcKey.PSChildName, $_)
            }
        }
        $rows | Export-Csv -Path (Join-Path $logfolderPath 'Services.csv') -NoTypeInformation
        Write-Host 'Services list collection completed.'
    } catch {
        Write-Warning "Failed to collect list of services: $_"
    } finally {
        try { [gc]::Collect(); Start-Sleep -Milliseconds 250 } catch {}
        Unmount-OfflineHive -mountKey $mountKey
    }
}

function Export-InstalledApplications-Offline {
    param(
        [Parameter(Mandatory=$true)][string]$WindowsDrive
    )
    $hivePath = Join-Path $WindowsDrive 'Windows\System32\config\SOFTWARE'
    $mountName = 'OfflineSOFTWARE'
    $mountKey  = "HKLM\$mountName"
    try {
        if (-not (Test-Path $hivePath)) { throw "SOFTWARE hive not found at $hivePath" }
        Mount-OfflineHive -hivePath $hivePath -mountKey $mountKey

        $paths = @(
            ("HKLM:\{0}\Microsoft\Windows\CurrentVersion\Uninstall" -f $mountName),
            ("HKLM:\{0}\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall" -f $mountName)
        )
        $rows = @()
        foreach ($path in $paths) {
            foreach ($appKey in Get-ChildItem -Path $path -ErrorAction SilentlyContinue) {
                try {
                    $p = Get-ItemProperty -Path $appKey.PSPath -ErrorAction SilentlyContinue
                    if ($p.DisplayName) {
                        $rows += [pscustomobject]@{
                            DisplayName    = $p.DisplayName
                            DisplayVersion = $p.DisplayVersion
                            Publisher      = $p.Publisher
                            InstallDate    = $p.InstallDate
                        }
                    }
                } catch {
                    Write-Warning ("Failed to read uninstall entry '{0}': {1}" -f $appKey.PSChildName, $_)
                }
            }
        }
        $rows | Export-Csv -Path (Join-Path $logfolderPath 'InstalledApplications.csv') -NoTypeInformation
        Write-Host 'Installed applications collection completed.'
    } catch {
        Write-Warning "Failed to collect list of installed applications: $_"
    } finally {
        try { [gc]::Collect(); Start-Sleep -Milliseconds 250 } catch {}
        Unmount-OfflineHive -mountKey $mountKey
    }
}

function Export-InstalledKBs-Offline {
    param(
        [Parameter(Mandatory=$true)][string]$WindowsDrive
    )
    $hivePath = Join-Path $WindowsDrive 'Windows\System32\config\SOFTWARE'
    $mountName = 'OfflineSOFTWARE'
    $mountKey  = "HKLM\$mountName"
    try {
        if (-not (Test-Path $hivePath)) { throw "SOFTWARE hive not found at $hivePath" }
        Mount-OfflineHive -hivePath $hivePath -mountKey $mountKey

        $rows = @()
        $seen = @{}
 
        # 1) From Component Based Servicing packages (Installed state)
        $pkgRoot = ("HKLM:\{0}\Microsoft\Windows\CurrentVersion\Component Based Servicing\Packages" -f $mountName)
        foreach ($pkg in Get-ChildItem -Path $pkgRoot -ErrorAction SilentlyContinue) {
            try {
                $name = $pkg.PSChildName
                $m = [regex]::Match($name, 'KB\d{5,}')
                if ($m.Success) {
                    $kb = $m.Value
                    $p = Get-ItemProperty -Path $pkg.PSPath -ErrorAction SilentlyContinue
                    $state = $p.CurrentState
                    if ($state -eq 112) {
                        if (-not $seen.ContainsKey($kb)) {
                            $desc = if ($name -match 'Security') { 'Security Update' } else { 'Update' }
                            $rows += [pscustomobject]@{
                                Description = $desc
                                HotFixID    = $kb
                                InstalledOn = $null
                            }
                            $seen[$kb] = $true
                        }
                    }
                }
            } catch {
                Write-Warning ("Failed to process CBS package '{0}': {1}" -f $pkg.PSChildName, $_)
            }
        }

        # 2) From Uninstall keys (DisplayName contains KB)
        $uninstRoots = @(
            ("HKLM:\{0}\Microsoft\Windows\CurrentVersion\Uninstall" -f $mountName),
            ("HKLM:\{0}\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall" -f $mountName)
        )
        foreach ($root in $uninstRoots) {
            foreach ($appKey in Get-ChildItem -Path $root -ErrorAction SilentlyContinue) {
                try {
                    $p = Get-ItemProperty -Path $appKey.PSPath -ErrorAction SilentlyContinue
                    $dn = [string]$p.DisplayName
                    if (-not [string]::IsNullOrEmpty($dn)) {
                        $m = [regex]::Match($dn, 'KB\d{5,}')
                        if ($m.Success) {
                            $kb = $m.Value
                            if (-not $seen.ContainsKey($kb)) {
                                $desc = if ($p.ReleaseType) { [string]$p.ReleaseType } else { 'Update' }
                                $installedOn = $null
                                if ($p.InstallDate -and ($p.InstallDate -match '^[0-9]{8}$')) {
                                    try {
                                        $d = [datetime]::ParseExact($p.InstallDate, 'yyyyMMdd', $null)
                                        $installedOn = $d
                                    } catch { Write-Warning ("Failed to parse InstallDate on '{0}': {1}" -f $appKey.PSChildName, $_) }
                                }
                                $rows += [pscustomobject]@{
                                    Description = $desc
                                    HotFixID    = $kb
                                    InstalledOn = $installedOn
                                }
                                $seen[$kb] = $true
                            }
                        }
                    }
                } catch {
                    Write-Warning ("Failed to process uninstall key '{0}': {1}" -f $appKey.PSChildName, $_)
                }
            }
        }
 
        $rows | Export-Csv -Path (Join-Path $logfolderPath 'InstalledKBs.csv') -NoTypeInformation
        Write-Host 'Installed KBs collection completed.'
    } catch {
        Write-Warning "Failed to collect list of installed KBs: $_"
    } finally {
        try { [gc]::Collect(); Start-Sleep -Milliseconds 250 } catch {}
        Unmount-OfflineHive -mountKey $mountKey
    }
}

function Export-WindowsMemoryDump-Offline {
    param(
        [Parameter(Mandatory=$true)][string]$WindowsDrive
    )
    try {
        $memoryDumpPaths = @(
            (Join-Path $WindowsDrive 'Windows\MEMORY.DMP'),
            (Join-Path $WindowsDrive 'Windows\Minidump')
        )

        foreach ($dump in $memoryDumpPaths) {
            try {
                Copy-Item -Path $dump `
                          -Destination $dumpfolderPath -Recurse -ErrorAction Stop
            } catch {
                Write-Warning "Failed to copy memory dump from '$dump': $_"
            }
        }
        Write-Host 'Windows memory dump collection completed.'
    } catch {
        Write-Warning "Failed to collect Windows memory dump: $_"
    }
}

function Export-SetupAPILogs-Offline {
    param(
        [Parameter(Mandatory=$true)][string]$WindowsDrive
    )
    try {
        $targetDir = $logfolderPath

        $infDir = Join-Path $WindowsDrive 'Windows\INF'
        $legacy = Join-Path $WindowsDrive 'Windows\setupapi.log'
        try {
            Copy-Item -Path (Join-Path $infDir 'setupapi*.log') `
                      -Destination $targetDir -ErrorAction Stop
        } catch {
            Write-Warning "Failed to copy SetupAPI logs from ${infDir}: $_"
        }
        if (Test-Path $legacy) {
            try {
                Copy-Item -Path $legacy `
                          -Destination $targetDir -ErrorAction Stop
            } catch {
                Write-Warning "Failed to copy legacy SetupAPI log ${legacy}: $_"
            }
        }
        Write-Host 'SetupAPI logs collection completed.'
    } catch {
        Write-Warning "Failed to collect SetupAPI logs: $_"
    }
}

function Write-InformationToArchive {
    param (
        [string]$FolderPath,
        [string]$SubFolderPath,
        [string]$ArchiveFileName
    )
    try {
        $archivePath = Join-Path -Path $FolderPath -ChildPath "$ArchiveFileName.zip"
        Compress-Files -SourcePath $SubFolderPath -DestinationPath $archivePath
        Write-Host "Archiving completed ($ArchiveFileName.zip)."
    } catch {
        Write-Warning "Failed to archive ($ArchiveFileName.zip): $_"
    }
}

function StopTranscriptAndCloseFile {
    if ($transcriptStarted) {
        Stop-Transcript | Out-Null
        $transcriptStarted = $false
    }
}

function Get-OfflineWindowsDrive {
    $candidates = @()
    try {
        $candidates = Get-Volume | Where-Object { $_.DriveLetter } | ForEach-Object { "$($_.DriveLetter):\" }
    } catch {
        $candidates = Get-PSDrive -PSProvider FileSystem |
                      Where-Object { $_.Root -ne 'X:\' } |
                      ForEach-Object { $_.Root }
    }

    foreach ($root in $candidates) {
        if ($root -eq 'X:\') { continue }
        try {
            $rootNorm  = $root.TrimEnd('\')          # e.g. 'D:'
            $sysHive   = Join-Path $rootNorm 'Windows\System32\config\SYSTEM'
            $winloadEx = Join-Path $rootNorm 'Windows\System32\winload.exe'
            $winloadEf = Join-Path $rootNorm 'Windows\System32\winload.efi'
            $evtxDir   = Join-Path $rootNorm 'Windows\System32\winevt\Logs'
            if ((Test-Path $sysHive) -and ((Test-Path $winloadEx) -or (Test-Path $winloadEf)) -and (Test-Path $evtxDir)) {
                Write-Host "Found Windows installation on $rootNorm"
                return $rootNorm
            }
        } catch { continue }
    }

    Write-Warning "Could not find a Windows installation drive."
    return $null
}

$validParams = @('IncludeSensitiveData', 'Help')
if ($Help -or $args -contains '-?' -or $args -contains '--Help') {
    Show-Help
    return
}

foreach ($param in $args) {
    if ($param -notlike '-*' -or ($param -like '-*' -and $validParams -notcontains $param.TrimStart('-'))) {
        Write-Host "A parameter cannot be found that matches parameter name '$param'"
        Show-Help
        return
    }
}

$breakHandler = {
    Write-Host "Script interrupted by user. Stopping transcript..."
    StopTranscriptAndCloseFile
    exit
}
Register-EngineEvent -SourceIdentifier ConsoleBreak -Action $breakHandler | Out-Null
Register-EngineEvent -SourceIdentifier PowerShell.Exiting -Action $breakHandler | Out-Null

$timestamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$folderName = "SystemInfo_$timestamp"
$logfolderName = "Log_folder_$timestamp"
$dumpfolderName = "Dump_folder_$timestamp"
$folderPath = Join-Path -Path (Get-Location) -ChildPath $folderName
$logfolderPath = Join-Path -Path $folderPath -ChildPath $logfolderName
$dumpfolderPath = Join-Path -Path $folderPath -ChildPath $dumpfolderName
$progressFile = "$folderPath\Collecting_Status.txt"
New-Item -Path $logfolderPath -ItemType Directory | Out-Null
New-Item -Path $progressFile -ItemType File | Out-Null
Write-Host "Starting system info collecting into $folderPath"
Write-Output "Log folder path: $logfolderPath"

try {
    Start-Transcript -Path $progressFile -Append
    $transcriptStarted = $true
    
    # --- OFFLINE (WINPE/WINRE) MODE ---
    Write-Host "WinPE/WinRE environment detected. Running in offline mode."
    $offlineDrive = Get-OfflineWindowsDrive
    if (-not $offlineDrive) {
        Write-Warning "Cannot proceed without a target Windows drive."
        return
    }

    Export-SystemConfiguration-Offline  -WindowsDrive $offlineDrive
    Export-EventLogs-Offline            -WindowsDrive $offlineDrive
    Export-DriversList-Offline          -WindowsDrive $offlineDrive
    Export-VirtioWinStorageDrivers-Offline -WindowsDrive $offlineDrive
    Export-WindowsUpdateLogs-Offline    -WindowsDrive $offlineDrive
    Export-ServicesList-Offline         -WindowsDrive $offlineDrive
    Export-InstalledApplications-Offline -WindowsDrive $offlineDrive
    Export-InstalledKBs-Offline         -WindowsDrive $offlineDrive
    Export-SetupAPILogs-Offline         -WindowsDrive $offlineDrive
    if ($IncludeSensitiveData) {
        Write-Output "Dump folder path: $dumpfolderPath"
        New-Item -Path $dumpfolderPath -ItemType Directory | Out-Null
        Export-WindowsMemoryDump-Offline -WindowsDrive $offlineDrive
    }
} catch {
    $errorMsg = "An error occurred: $_"
    Write-Host $errorMsg
    Add-Content -Path $progressFile -Value $errorMsg
} finally {
    StopTranscriptAndCloseFile
    Unregister-Event -SourceIdentifier ConsoleBreak
    Unregister-Event -SourceIdentifier PowerShell.Exiting
}

Remove-Item -Path $progressFile -ErrorAction SilentlyContinue
Write-InformationToArchive -FolderPath $folderPath -SubFolderPath $logfolderPath -ArchiveFileName $logfolderName
if ($IncludeSensitiveData) {
    Write-InformationToArchive -FolderPath $folderPath -SubFolderPath $dumpfolderPath -ArchiveFileName $dumpfolderName
}
