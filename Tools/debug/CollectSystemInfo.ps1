#  This script collects various system information for diagnostic
#  purposes. The collected data includes system configuration,
#  event logs, driver lists, registry information, update logs,
#  services, uptime, running processes, installed applications,
#  installed KBs, and memory dumps.

#  Copyright (c) 2024 Red Hat, Inc. and/or its affiliates. All rights reserved.

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


#  Ensure the script runs with an unrestricted execution policy (for Windows 10 and Windows Server 2016)
#  Set-ExecutionPolicy -ExecutionPolicy Unrestricted -Scope Process -Force

#  For gathering event logs run the script as an administrator

#  IncludeSensitiveData is used to include memory dumps add this parameter to your command line to collect them
#  Example:  .\CollectSystemInfo.ps1 -IncludeSensitiveData

param (
    [switch]$IncludeSensitiveData
)

function Export-SystemConfiguration {
    try {
        Start-Process -FilePath 'msinfo32.exe' -ArgumentList '/report', 'msinfo32.txt' -Wait
    } catch {
        Write-Warning "Failed to collect system configuration: $_"
    }
}

function Export-EventLogs {
    try {
        $logNames = @('system', 'security', 'application')
        foreach ($logName in $logNames) {
            wevtutil epl $logName "$logName.evtx"
            wevtutil al "$logName.evtx"
        }
    } catch {
        Write-Warning "Failed to collect event logs: $_"
    }
}

function Export-DriversList {
    try {
        Get-WindowsDriver -Online -All | Select-Object -Property * | Export-Csv -Path 'drv_list.csv' -NoTypeInformation
    } catch {
        Write-Warning "Failed to collect drivers list: $_"
    }
}

function Export-VirtioWinStorageDrivers {
    $registryPaths = @(
        'HKLM:\SYSTEM\CurrentControlSet\Services\Disk',
        'HKLM:\SYSTEM\CurrentControlSet\Services\viostor\Parameters',
        'HKLM:\SYSTEM\CurrentControlSet\Services\vioscsi\Parameters'
    )
    $valuesToQuery = @('IoTimeoutValue', 'TimeoutValue')

    foreach ($path in $registryPaths) {
        foreach ($value in $valuesToQuery) {
            try {
                $property = Get-ItemProperty -Path $path -Name $value -ErrorAction Stop
                $output = "$path\$value : $($property.$value)"
                $output | Out-File -FilePath 'virtio_disk.txt' -Append
            } catch {
                Write-Warning "Failed to collect ${path}\${value}: $_"
            }
        }
    }
}

function Export-WindowsUpdateLogs {
    try {
        $logPath = Join-Path -Path (Get-Location) -ChildPath 'WindowsUpdate.log'
        $command = "Get-WindowsUpdateLog -LogPath '$logPath'"
        Start-Process -FilePath 'powershell.exe' -ArgumentList '-NoLogo', '-NoProfile', '-Command', $command -NoNewWindow -Wait -RedirectStandardOutput 'OutputWindowsUpdate.log' -RedirectStandardError 'ErrorWindowsUpdate.log'
    } catch {
        Write-Warning "Failed to collect Windows Update logs: $_"
    }
}

function Export-WindowsUptime {
    try {
        $uptime = (Get-Date) - (gcim Win32_OperatingSystem).LastBootUpTime
        $uptime.ToString() | Out-File -FilePath 'WindowsUptime.txt'
        Write-Host 'Windows uptime collection completed.'
    } catch {
        Write-Warning "Failed to collect Windows uptime: $_"
    }
}

function Export-ServicesList {
    try {
        Get-Service | Select-Object -Property Name, DisplayName, Status, StartType | Export-Csv -Path 'Services.csv' -NoTypeInformation
    } catch {
        Write-Warning "Failed to collect list of services: $_"
    }
}

function Export-RunningProcesses {
    try {
        Get-Process | Select-Object -Property Id, ProcessName, StartTime | Export-Csv -Path 'RunningProcesses.csv' -NoTypeInformation
    } catch {
        Write-Warning "Failed to collect list of running processes: $_"
    }
}

function Export-InstalledApplications {
    try {
        Get-ItemProperty -Path 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*' |
        Select-Object -Property DisplayName, DisplayVersion, Publisher, InstallDate |
        Export-Csv -Path 'InstalledApplications.csv' -NoTypeInformation
    } catch {
        Write-Warning "Failed to collect list of installed applications: $_"
    }
}

function Export-InstalledKBs {
    try {
        Get-HotFix | Select-Object -Property Description, HotFixID, InstalledOn | Export-Csv -Path 'InstalledKBs.csv' -NoTypeInformation
    } catch {
        Write-Warning "Failed to collect list of installed KBs: $_"
    }
}

function Get-WindowsMemoryDump {
    $memoryDumpPaths = @("$env:SystemRoot\MEMORY.DMP", "$env:SystemRoot\Minidump")
    
    return $memoryDumpPaths | Where-Object { Test-Path -Path $_ }
}


function Get-ExportedFiles {
    $exportedFiles = @()
    $filesPattern = @('*.txt', '*.csv', '*.evtx', '*.log')
    $fileInCurrentDirectory = foreach ($pattern in $filesPattern) {
        Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue
    }
    $exportedFiles += $fileInCurrentDirectory
    if (Test-Path -Path 'LocaleMetaData') {
        $exportedFiles += 'LocaleMetaData'
    }

    return $exportedFiles
}

function Remove-TempFiles {
    param ([string[]]$Path)
    try {
        Remove-Item -Path $Path -Recurse -Force
    } catch {
        Write-Warning "Failed to delete file: $resolvedFile - $_"
    }
}

function Write-InformationToArchive {
    try {
        $date = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
        $archiveFile = "SystemInfo_$date.zip"
        $exportedFiles = Get-ExportedFiles
        if ($IncludeSensitiveData) {
            $dumpFiles = Get-WindowsMemoryDump
            $outerFiles += $dumpFiles
        }
        
        $collectedFiles = $exportedFiles + $outerFiles
        
        Compress-Archive -Path $collectedFiles -DestinationPath $archiveFile
        Remove-TempFiles $exportedFiles
        Write-Host "Archiving completed ($archiveFile)."
    } catch {
        Write-Warning "Failed to archive information: $_"
    }
}

Export-SystemConfiguration
Export-EventLogs
Export-DriversList
Export-VirtioWinStorageDrivers
Export-WindowsUpdateLogs
Export-ServicesList
Export-WindowsUptime
Export-RunningProcesses
Export-InstalledApplications
Export-InstalledKBs

Write-InformationToArchive
