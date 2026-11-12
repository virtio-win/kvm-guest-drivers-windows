#  Measure-RebootCycle.ps1
#  Reports the timing of the most recent Windows reboot cycle by reading
#  shutdown and boot events from the event logs. Optionally triggers a
#  reboot so you can measure a fresh cycle.
#
#  Minimum supported OS: Windows 10 / Windows Server 2016 (32-bit and 64-bit)
#  Requires: Administrator privileges, PowerShell 5.1+
#
#  Usage:
#    powershell.exe -ExecutionPolicy Bypass -File .\Measure-RebootCycle.ps1
#    powershell.exe -ExecutionPolicy Bypass -File .\Measure-RebootCycle.ps1 -Json
#    powershell.exe -ExecutionPolicy Bypass -File .\Measure-RebootCycle.ps1 -Reboot
#    powershell.exe -ExecutionPolicy Bypass -File .\Measure-RebootCycle.ps1 -Reboot -Force

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

#Requires -RunAsAdministrator

param(
    [switch]$Reboot,
    [switch]$Force,
    [switch]$Json,
    [switch]$Help
)

$DiagLog = "Microsoft-Windows-Diagnostics-Performance/Operational"
$SystemLog = "System"

if ($Help) {
    Write-Host @"
Measure-RebootCycle.ps1 - Reboot cycle timing for Windows VMs

Reports shutdown and boot timing from the most recent reboot cycle.

Parameters:
  (none)         Show the last reboot cycle report
  -Json          Output as JSON
  -Reboot        Trigger a reboot (run the script again after boot to see results)
  -Reboot -Force Skip the confirmation prompt before rebooting
  -Help          Show this help message

Examples:
  .\Measure-RebootCycle.ps1              # Report last reboot timing
  .\Measure-RebootCycle.ps1 -Reboot      # Reboot, then run again to see timing
  .\Measure-RebootCycle.ps1 -Json        # JSON output for automation
"@
    exit 0
}

function Get-EventDataMap {
    param($Event)

    [xml]$xml = $Event.ToXml()
    $map = @{}

    foreach ($d in $xml.Event.EventData.Data) {
        if ($d.Name) {
            $map[$d.Name] = $d.'#text'
        }
    }

    return $map
}

if ($Reboot) {
    if (-not $Force) {
        $answer = Read-Host "This will REBOOT the machine immediately. Continue? (y/N)"
        if ($answer -ne 'y' -and $answer -ne 'Y') {
            Write-Host "Cancelled."
            exit 0
        }
    }

    Write-Host "Rebooting now... Run this script again after boot to see the report."
    shutdown.exe /r /t 0 /c "Reboot timing measurement"
    exit
}

$logEnabled = $false
try {
    $logConfig = Get-WinEvent -ListLog $DiagLog -ErrorAction Stop
    $logEnabled = $logConfig.IsEnabled
} catch {
    Write-Error "The event log '$DiagLog' is not available on this system."
    Write-Error "This log requires Windows 10 / Server 2016 or later."
    exit 1
}

if (-not $logEnabled) {
    Write-Warning "The Diagnostics-Performance log is disabled. Enabling it now..."
    wevtutil sl $DiagLog /e:true
    Write-Warning "Log enabled. Reboot to generate boot timing events."
    exit 1
}

$bootEvents = @(Get-WinEvent -FilterHashtable @{
    LogName = $DiagLog
    Id      = 100
} -MaxEvents 2 -ErrorAction SilentlyContinue)

if ($bootEvents.Count -eq 0) {
    Write-Warning "No boot events (Event ID 100) found."
    exit 1
}

$bootEvent = $bootEvents[0]
$bootData = Get-EventDataMap $bootEvent
$bootTime = $bootEvent.TimeCreated
$previousBootTime = if ($bootEvents.Count -gt 1) { $bootEvents[1].TimeCreated } else { $null }

$shutdownEvent = Get-WinEvent -FilterHashtable @{
    LogName = $DiagLog
    Id      = 200
} -MaxEvents 1 -ErrorAction SilentlyContinue

if ($shutdownEvent -and $previousBootTime -and $shutdownEvent.TimeCreated -lt $previousBootTime) {
    $shutdownEvent = $null
}

$rebootEvent = Get-WinEvent -FilterHashtable @{
    LogName = $SystemLog
    Id      = 1074
} -MaxEvents 1 -ErrorAction SilentlyContinue

if ($rebootEvent -and $previousBootTime -and $rebootEvent.TimeCreated -lt $previousBootTime) {
    $rebootEvent = $null
}

$jsonReport = [ordered]@{
    Computer = $env:COMPUTERNAME
    ReportTime = (Get-Date).ToString("o")
}

$lines = @()
$lines += "Reboot cycle report"
$lines += "==================="
$lines += ""
$lines += "Computer: $env:COMPUTERNAME"
$lines += ""

if ($rebootEvent) {
    $lines += "Reboot initiation"
    $lines += "-----------------"
    $lines += "Event 1074 time:  $($rebootEvent.TimeCreated)"
    $lines += ""

    $jsonReport["RebootInitiation"] = [ordered]@{
        EventTime = $rebootEvent.TimeCreated.ToString("o")
    }
} else {
    $lines += "Reboot initiation"
    $lines += "-----------------"
    $lines += "Event 1074 not found."
    $lines += ""

    $jsonReport["RebootInitiation"] = $null
}

if ($shutdownEvent) {
    $shutdownData = Get-EventDataMap $shutdownEvent

    $lines += "Shutdown / restart phase"
    $lines += "------------------------"
    $lines += "Event 200 time:                 $($shutdownEvent.TimeCreated)"
    $lines += "ShutdownTime_ms:                $($shutdownData["ShutdownTime"])"
    $lines += "ShutdownUserSessionTime_ms:     $($shutdownData["ShutdownUserSessionTime"])"
    $lines += "ShutdownSystemSessionsTime_ms:  $($shutdownData["ShutdownSystemSessionsTime"])"
    $lines += ""

    $jsonReport["Shutdown"] = [ordered]@{
        EventTime                     = $shutdownEvent.TimeCreated.ToString("o")
        ShutdownTime_ms               = [int]$shutdownData["ShutdownTime"]
        ShutdownUserSessionTime_ms    = [int]$shutdownData["ShutdownUserSessionTime"]
        ShutdownSystemSessionsTime_ms = [int]$shutdownData["ShutdownSystemSessionsTime"]
    }
} else {
    $lines += "Shutdown / restart phase"
    $lines += "------------------------"
    $lines += "Event 200 not found."
    $lines += ""

    $jsonReport["Shutdown"] = $null
}

$lines += "Boot phase"
$lines += "----------"
$lines += "Event 100 time:        $bootTime"
$lines += "BootTime_ms:           $($bootData["BootTime"])"
$lines += "MainPathBootTime_ms:   $($bootData["MainPathBootTime"])"
$lines += "BootPostBootTime_ms:   $($bootData["BootPostBootTime"])"
$lines += "SystemBootInstance:     $($bootData["SystemBootInstance"])"
$lines += "BootIsDegradation:     $($bootData["BootIsDegradation"])"
$lines += ""

$jsonReport["Boot"] = [ordered]@{
    EventTime           = $bootTime.ToString("o")
    BootTime_ms         = [int]$bootData["BootTime"]
    MainPathBootTime_ms = [int]$bootData["MainPathBootTime"]
    BootPostBootTime_ms = [int]$bootData["BootPostBootTime"]
    SystemBootInstance  = [int]$bootData["SystemBootInstance"]
    BootIsDegradation   = $bootData["BootIsDegradation"]
}

if ($rebootEvent -and $bootEvent) {
    $wallClock = ($bootTime - $rebootEvent.TimeCreated).TotalSeconds
    if ($wallClock -gt 0) {
        $lines += "Wall-clock reboot duration"
        $lines += "--------------------------"
        $lines += "From reboot initiation to boot complete: $([math]::Round($wallClock, 2)) seconds"
        $lines += ""

        $jsonReport["WallClockReboot_s"] = [math]::Round($wallClock, 2)
    }
}

if ($Json) {
    $jsonReport | ConvertTo-Json -Depth 3
} else {
    $lines -join [Environment]::NewLine
}
