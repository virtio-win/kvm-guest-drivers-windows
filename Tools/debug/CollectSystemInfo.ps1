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

function Show-Help {
    Write-Host "Usage: .\CollectSystemInfo.ps1 [-IncludeSensitiveData] [-Help]"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -IncludeSensitiveData  Include sensitive data (memory dump)"
    Write-Host "  -Help                  Show this help message"
    Write-Host ""
    Write-Host "If no parameters are provided, the script will run with default behavior."
}

function Export-SystemConfiguration {
    try {
        Write-Host 'Collecting system configuration started it may take a while...'
        Start-Process -FilePath 'msinfo32.exe' -ArgumentList '/report', (Join-Path $logfolderPath 'msinfo32.txt') -Wait
        Write-Host 'System configuration collection completed.'
    } catch {
        Write-Warning "Failed to collect system configuration: $_"
    }
}

function Export-EventLogs {
    try {
        $logNames = @('system', 'security', 'application')
        foreach ($logName in $logNames) {
            $logPath = Join-Path $logfolderPath "$logName.evtx"
            wevtutil epl $logName $logPath
            wevtutil al $logPath
        }
        Write-Host 'Event logs collection completed.'
    } catch {
        Write-Warning "Failed to collect event logs: $_"
    }
}

function Get-PerfIOLimits {
    param($Drive, $Duration = 60, $Interval = 1)
    $disk = if ($Drive -match ':$') { $Drive } else { "${Drive}:" }
    $counters = @(
        "\LogicalDisk($disk)\Disk Reads/sec",      # IOPS (Reads)
        "\LogicalDisk($disk)\Disk Writes/sec",     # IOPS (Writes)
        "\LogicalDisk($disk)\Avg. Disk sec/Read",  # Read Latency
        "\LogicalDisk($disk)\Avg. Disk sec/Write", # Write Latency
        "\LogicalDisk($disk)\Disk Bytes/sec",      # Throughput (Total)
        "\LogicalDisk($disk)\Disk Read Bytes/sec", # Throughput (Read)
        "\LogicalDisk($disk)\Disk Write Bytes/sec",# Throughput (Write)
        "\LogicalDisk($disk)\Disk Transfers/sec",  # Total I/O Operations
        "\LogicalDisk($disk)\Current Disk Queue Length" # Queue Length
    )
    $maxSamples = [math]::Ceiling($Duration / $Interval)
    $data = $null
    $out = [ordered]@{}

    foreach ($path in $counters) {
        $out[$path] = "PerfCounter Error"
    }

    try {
        $data = Get-Counter -Counter $counters -SampleInterval $Interval -MaxSamples $maxSamples -ErrorAction Stop
        
        foreach ($path in $counters) {
            $vals = $data.CounterSamples |
                    Where-Object Path -like "*$path" | 
                    Select-Object -ExpandProperty CookedValue
            if ($vals.Count -eq 0) {
                 $out[$path] = "N/A" # Keep N/A if counter exists but has no values
                 continue
            }
            if ($path -match 'Reads/sec|Writes/sec|Transfers/sec') {
                $maxValue = (@($vals) | Measure-Object -Maximum).Maximum
                $out[$path] = "{0:N2} IOPS" -f $maxValue
            } elseif ($path -match 'Bytes/sec') {
                $maxValue = (@($vals) | Measure-Object -Maximum).Maximum
                $out[$path] = "{0:N2} KB/s" -f ($maxValue / 1024)
            } else {
                $minValue = (@($vals) | Measure-Object -Minimum).Minimum
                $ms = $minValue * 1000
                $out[$path] = "{0:N2} ms" -f $ms
            }
        }
    } catch {
        Write-Warning "Failed to get performance counters for drive $disk. Error: $($_.Exception.Message)"
    }

    return $out
}

function Test-WinSatDisk {
    param($Drive)
    $ErrorActionPreference = 'SilentlyContinue'
    $seqOutput = Invoke-Command { winsat disk -seq -read -drive $Drive } 2>&1
    $ranOutput = Invoke-Command { winsat disk -ran -write -drive $Drive } 2>&1
    $ErrorActionPreference = 'Continue'

    $seqVal = $null
    $ranVal = $null

    foreach ($line in $seqOutput) {
        if ($line -match 'Disk\s+Sequential\s+\d+(\.\d+)?\s+Read\s+([\d\.]+)\s+MB/s') {
            $seqVal = $matches[2] + " MB/s"
            break
        }
    }

    foreach ($line in $ranOutput) {
        if ($line -match 'Disk\s+Random\s+\d+(\.\d+)?\s+Write\s+([\d\.]+)\s+MB/s') {
            $ranVal = $matches[2] + " MB/s"
            break
        }
    }

    if (-not $seqVal) {
        $seqVal = ($seqOutput | Select-String 'Sequential Read' | ForEach-Object { ($_ -split ':\s*', 2)[-1].Trim() }) -join ''
    }
     if (-not $ranVal) {
        $ranVal = ($ranOutput | Select-String 'Random Write'  | ForEach-Object { ($_ -split ':\s*', 2)[-1].Trim() }) -join ''
    }

    $result = [pscustomobject]@{
        SequentialRead = if ($seqVal -and $seqVal -notmatch '^\s*$') { $seqVal } else { 'N/A' }
        RandomWrite    = if ($ranVal -and $ranVal -notmatch '^\s*$') { $ranVal } else { 'N/A' }
    }
    return $result
}

function Export-IOLimits {
    param($Duration = 60, $Interval = 1)
    try {
        $disks = Get-Disk
        foreach ($disk in $disks) {
            $parts = Get-Partition -DiskNumber $disk.Number | Where-Object DriveLetter
            foreach ($p in $parts) {
                $drive = "$($p.DriveLetter):"
                $perf = Get-PerfIOLimits -Drive $drive -Duration $Duration -Interval $Interval
                $ws   = Test-WinSatDisk     -Drive $drive
                $outFile = Join-Path $logfolderPath "IOLimits_$($p.DriveLetter).txt"

                $perf.GetEnumerator() | ForEach-Object {
                    "$($_.Key): $($_.Value)" | Out-File -FilePath $outFile -Append
                }

                "SequentialRead: $($ws.SequentialRead)" | Out-File -FilePath $outFile -Append
                "RandomWrite:   $($ws.RandomWrite)"    | Out-File -FilePath $outFile -Append
                "Drive $drive I/O limits saved to $outFile" |
                    Out-File -FilePath (Join-Path $logfolderPath 'IOLimits.txt') -Append
            }
        }
        Write-Host 'IO Limits collection completed.'
    } catch {
        Write-Warning "Failed to collect IO Limits: $_"
    }
}

function Export-DriversList {
    try {
        Get-WindowsDriver -Online -All | Select-Object -Property * | Export-Csv -Path (Join-Path $logfolderPath 'drv_list.csv') -NoTypeInformation
        Write-Host 'Drivers list collection completed.'
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
            $property = Get-ItemProperty -Path $path -Name $value -ErrorAction SilentlyContinue
            $output = "$path\$value : $($property.$value)" 
            $output | Out-File -FilePath (Join-Path $logfolderPath 'virtio_disk.txt') -Append
        }
    }
    Write-Host 'Virtio-Win storage drivers configuration collection completed.'
}

function Export-WindowsUpdateLogs {
    try {
        $logPath = Join-Path $logfolderPath 'WindowsUpdate.log'
        $command = "Get-WindowsUpdateLog -LogPath '$logPath'"
        Start-Process -FilePath 'powershell.exe' -ArgumentList '-NoLogo', '-NoProfile', '-Command', $command -NoNewWindow -Wait -RedirectStandardOutput (Join-Path $logfolderPath 'OutputWindowsUpdate.log') -RedirectStandardError (Join-Path $logfolderPath 'ErrorWindowsUpdate.log')
        Write-Host 'Windows Update logs collection completed.'
    } catch {
        Write-Warning "Failed to collect Windows Update logs: $_"
    }
}

function Export-WindowsUptime {
    try {
        $uptime = (Get-Date) - (gcim Win32_OperatingSystem).LastBootUpTime
        $uptime.ToString() | Out-File -FilePath (Join-Path $logfolderPath 'WindowsUptime.txt')
        Write-Host 'Windows uptime collection completed.'
    } catch {
        Write-Warning "Failed to collect Windows uptime: $_"
    }
}

function Export-ServicesList {
    try {
        Get-Service | Select-Object -Property Name, DisplayName, Status, StartType | Export-Csv -Path (Join-Path $logfolderPath 'Services.csv') -NoTypeInformation
        Write-Host 'Services list collection completed.'
    } catch {
        Write-Warning "Failed to collect list of services: $_"
    }
}

function Export-RunningProcesses {
    try {
        Get-Process | Select-Object -Property Id, ProcessName, StartTime | Export-Csv -Path (Join-Path $logfolderPath 'RunningProcesses.csv') -NoTypeInformation
        Write-Host 'Running processes collection completed.'
    } catch {
        Write-Warning "Failed to collect list of running processes: $_"
    }
}

function Export-InstalledApplications {
    try {
        Get-ItemProperty -Path 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*' |
        Select-Object -Property DisplayName, DisplayVersion, Publisher, InstallDate |
        Export-Csv -Path (Join-Path $logfolderPath 'InstalledApplications.csv') -NoTypeInformation
        Write-Host 'Installed applications collection completed.'
    } catch {
        Write-Warning "Failed to collect list of installed applications: $_"
    }
}

function Export-InstalledKBs {
    try {
        Get-HotFix | Select-Object -Property Description, HotFixID, InstalledOn | Export-Csv -Path (Join-Path $logfolderPath 'InstalledKBs.csv') -NoTypeInformation
        Write-Host 'Installed KBs collection completed.'
    } catch {
        Write-Warning "Failed to collect list of installed KBs: $_"
    }
}

function Export-NetworkConfiguration {
    try {
        Get-NetAdapterAdvancedProperty | Out-File -FilePath (Join-Path $logfolderPath 'NetworkInterfaces.txt')
        ipconfig /all | Out-File -FilePath (Join-Path $logfolderPath 'IPConfiguration.txt')

        Write-Host 'Network configuration collection completed.'
    } catch {
        Write-Warning "Failed to collect network configuration: $_"
    }
}

function Export-WindowsMemoryDump {
    $memoryDumpPaths = @("$env:SystemRoot\MEMORY.DMP", "$env:SystemRoot\Minidump")

    foreach ($dump in $memoryDumpPaths) {
        Copy-Item -Path $dump -Destination $dumpfolderPath -Recurse -ErrorAction SilentlyContinue
    }
    Write-Host 'Windows memory dump collection completed.'
}

function Export-SetupAPILogs {
    try {
        $infPath = "$env:SystemRoot\INF"
        $files = Get-ChildItem -Path $infPath -Filter 'setupapi*.log'

        if (Test-Path "$env:SystemRoot\setupapi.log") {
            $files += Get-Item "$env:SystemRoot\setupapi.log"
        }

        foreach ($file in $files) {
            try {
                Copy-Item -Path $file.FullName -Destination $logfolderPath -ErrorAction Stop
            } catch {
                Write-Warning "Failed to copy $($file.Name): $_"
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
    Export-IOLimits
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
    Export-NetworkConfiguration
    Export-SetupAPILogs 

    if ($IncludeSensitiveData) {
        Write-Output "Dump folder path: $dumpfolderPath"
        New-Item -Path $dumpfolderPath -ItemType Directory | Out-Null
        Export-WindowsMemoryDump
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