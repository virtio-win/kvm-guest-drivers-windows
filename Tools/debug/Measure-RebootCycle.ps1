#  Measure-RebootCycle.ps1
#  Reports the timing of the most recent Windows reboot cycle by reading
#  shutdown and boot events from the event logs. Uses detailed
#  Diagnostics-Performance data when available and falls back to the System
#  log on Windows Server. Optionally triggers a reboot so you can measure a
#  fresh cycle.
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
$RebootComment = "virtio-win reboot timing measurement"

if ($Help) {
    Write-Host @"
Measure-RebootCycle.ps1 - Reboot cycle timing for Windows VMs

Reports shutdown and boot timing from the most recent reboot cycle. On systems
without the Diagnostics-Performance log, timing is derived from standard System
log events and ends when the Event Log service starts.

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

function ConvertTo-UtcEventTime {
    param(
        [string]$Value,
        $Fallback
    )

    if (-not [string]::IsNullOrWhiteSpace($Value)) {
        try {
            return ([datetimeoffset]::Parse(
                $Value,
                [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::AssumeUniversal
            )).UtcDateTime
        } catch {
            # Use the event record timestamp when the payload is malformed.
        }
    }

    if ($null -ne $Fallback) {
        return $Fallback.ToUniversalTime()
    }

    return $null
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
    shutdown.exe /r /t 0 /c $RebootComment
    exit
}

$diagLogEnabled = $false
try {
    $logConfig = Get-WinEvent -ListLog $DiagLog -ErrorAction Stop
    $diagLogEnabled = $logConfig.IsEnabled
} catch {
    # This channel is not present on Windows Server. Use the System log below.
}

$diagBootEvents = @()
if ($diagLogEnabled) {
    $diagBootEvents = @(Get-WinEvent -FilterHashtable @{
        LogName = $DiagLog
        Id      = 100
    } -MaxEvents 2 -ErrorAction SilentlyContinue)
}

$kernelStartEvents = @(Get-WinEvent -FilterHashtable @{
    LogName      = $SystemLog
    ProviderName = "Microsoft-Windows-Kernel-General"
    Id           = 12
} -MaxEvents 2 -ErrorAction SilentlyContinue)

if ($kernelStartEvents.Count -eq 0) {
    Write-Warning "No kernel start events (System log, Kernel-General Event ID 12) found."
    exit 1
}

$latestKernelEvent = $kernelStartEvents[0]
$latestKernelData = Get-EventDataMap $latestKernelEvent
$latestKernelStart = ConvertTo-UtcEventTime `
    -Value $latestKernelData["StartTime"] -Fallback $latestKernelEvent.TimeCreated
$previousKernelStart = $null
if ($kernelStartEvents.Count -gt 1) {
    $previousKernelData = Get-EventDataMap $kernelStartEvents[1]
    $previousKernelStart = ConvertTo-UtcEventTime `
        -Value $previousKernelData["StartTime"] -Fallback $kernelStartEvents[1].TimeCreated
}

$matchingDiagEvent = $null
$matchingDiagData = $null
$matchingDiagEndTime = $null
foreach ($event in $diagBootEvents) {
    $eventData = Get-EventDataMap $event
    $eventEndTime = ConvertTo-UtcEventTime $eventData["BootEndTime"] $null
    if ($eventEndTime -and $eventEndTime -ge $latestKernelStart) {
        $matchingDiagEvent = $event
        $matchingDiagData = $eventData
        $matchingDiagEndTime = $eventEndTime
        break
    }
}

$measurementSource = $null
$measurementScope = $null
$bootEvent = $null
$bootData = $null
$bootTime = $null
$bootCompletionTime = $null
$previousBootTime = $null
$eventLogReady = $null
$shutdownEvent = $null
$shutdownData = $null
$kernelShutdownEvent = $null
$rebootEvent = $null
$rebootType = $null

if ($matchingDiagEvent) {
    $measurementSource = "DiagnosticsPerformance"
    $measurementScope = "WindowsBootPerformance"
    $bootEvent = $matchingDiagEvent
    $bootData = $matchingDiagData
    $bootTime = $bootEvent.TimeCreated
    $bootCompletionTime = $matchingDiagEndTime
    $previousBootTime = $previousKernelStart

    $shutdownEvent = Get-WinEvent -FilterHashtable @{
        LogName = $DiagLog
        Id      = 200
    } -MaxEvents 1 -ErrorAction SilentlyContinue

    if ($shutdownEvent) {
        $shutdownData = Get-EventDataMap $shutdownEvent
        $shutdownEndTime = ConvertTo-UtcEventTime `
            -Value $shutdownData["ShutdownEndTime"] `
            -Fallback $shutdownEvent.TimeCreated
        if (($previousBootTime -and $shutdownEndTime -lt $previousBootTime) -or
            $shutdownEndTime -gt $latestKernelStart) {
            $shutdownEvent = $null
            $shutdownData = $null
        }
    }
} else {
    $measurementSource = "SystemEventLog"
    $measurementScope = "KernelStartToEventLogReady"
    $bootEvent = $latestKernelEvent
    $bootTime = $latestKernelStart
    $previousBootTime = $previousKernelStart

    $eventLogReady = Get-WinEvent -FilterHashtable @{
        LogName      = $SystemLog
        ProviderName = "EventLog"
        Id           = 6005
    } -MaxEvents 50 -ErrorAction SilentlyContinue |
        Where-Object { $_.TimeCreated.ToUniversalTime() -ge $bootTime } |
        Sort-Object TimeCreated |
        Select-Object -First 1

    if ($eventLogReady) {
        $bootCompletionTime = $eventLogReady.TimeCreated.ToUniversalTime()
    }
}

$cycleFilter = @{
    LogName      = $SystemLog
    ProviderName = "User32"
    Id           = 1074
    EndTime      = $latestKernelEvent.TimeCreated
}
if ($kernelStartEvents.Count -gt 1) {
    $cycleFilter["StartTime"] = $kernelStartEvents[1].TimeCreated
}
$rebootEvent = Get-WinEvent -FilterHashtable $cycleFilter -MaxEvents 1 -ErrorAction SilentlyContinue

if ($rebootEvent) {
    $rebootData = Get-EventDataMap $rebootEvent
    $rebootType = $rebootData["param5"]
    $isRestart = [string]::Equals(
        $rebootType,
        "restart",
        [StringComparison]::OrdinalIgnoreCase
    ) -or [string]::Equals(
        $rebootData["param6"],
        $RebootComment,
        [StringComparison]::Ordinal
    )
    if (-not $isRestart) {
        $rebootEvent = $null
    }
}

if (-not $previousBootTime) {
    # Without the previous boot boundary, the latest event may be from an
    # unrelated cycle and cannot be correlated safely.
    $rebootEvent = $null
}

if ($measurementSource -eq "SystemEventLog") {
    $shutdownFilter = @{
        LogName      = $SystemLog
        ProviderName = "Microsoft-Windows-Kernel-General"
        Id           = 13
        EndTime      = $latestKernelEvent.TimeCreated
    }
    if ($kernelStartEvents.Count -gt 1) {
        $shutdownFilter["StartTime"] = $kernelStartEvents[1].TimeCreated
        $kernelShutdownEvent = Get-WinEvent -FilterHashtable $shutdownFilter `
            -MaxEvents 1 -ErrorAction SilentlyContinue
    }
}

$jsonReport = [ordered]@{
    Computer          = $env:COMPUTERNAME
    ReportTime        = (Get-Date).ToString("o")
    MeasurementSource = $measurementSource
    MeasurementScope  = $measurementScope
}

$lines = @()
$lines += "Reboot cycle report"
$lines += "==================="
$lines += ""
$lines += "Computer: $env:COMPUTERNAME"
$lines += "Measurement source: $measurementSource"
$lines += "Measurement scope:  $measurementScope"
$lines += ""

if ($rebootEvent) {
    $lines += "Reboot initiation"
    $lines += "-----------------"
    $lines += "Event 1074 time:  $($rebootEvent.TimeCreated)"
    $lines += ""

    $jsonReport["RebootInitiation"] = [ordered]@{
        EventTime = $rebootEvent.TimeCreated.ToString("o")
        Type      = $rebootType
    }
} else {
    $lines += "Reboot initiation"
    $lines += "-----------------"
    if ($rebootType) {
        $lines += "The correlated Event 1074 could not be verified as a restart."
        $lines += "Reported shutdown type: $rebootType"
    } else {
        $lines += "Event 1074 not found."
    }
    $lines += ""

    $jsonReport["RebootInitiation"] = $null
}

if ($measurementSource -eq "DiagnosticsPerformance" -and $shutdownEvent) {
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
} elseif ($measurementSource -eq "SystemEventLog" -and $kernelShutdownEvent) {
    $initiationToKernelShutdown = $null
    if ($rebootEvent) {
        $initiationToKernelShutdown = [long][math]::Round(
            ($kernelShutdownEvent.TimeCreated.ToUniversalTime() -
                $rebootEvent.TimeCreated.ToUniversalTime()).TotalMilliseconds
        )
    }

    $lines += "Shutdown / restart phase"
    $lines += "------------------------"
    $lines += "Kernel-General Event 13 time:       $($kernelShutdownEvent.TimeCreated)"
    $lines += "InitiationToKernelShutdown_ms:      $initiationToKernelShutdown"
    $lines += ""

    $jsonReport["Shutdown"] = [ordered]@{
        EventTime                      = $kernelShutdownEvent.TimeCreated.ToString("o")
        InitiationToKernelShutdown_ms  = $initiationToKernelShutdown
        ShutdownTime_ms                = $null
        ShutdownUserSessionTime_ms     = $null
        ShutdownSystemSessionsTime_ms  = $null
    }
} else {
    $lines += "Shutdown / restart phase"
    $lines += "------------------------"
    $lines += "A correlated shutdown timing event was not found."
    $lines += ""

    $jsonReport["Shutdown"] = $null
}

if ($measurementSource -eq "DiagnosticsPerformance") {
    $lines += "Boot phase"
    $lines += "----------"
    $lines += "Event 100 time:        $bootTime"
    $lines += "BootTime_ms:           $($bootData["BootTime"])"
    $lines += "MainPathBootTime_ms:   $($bootData["MainPathBootTime"])"
    $lines += "BootPostBootTime_ms:   $($bootData["BootPostBootTime"])"
    $lines += "SystemBootInstance:    $($bootData["SystemBootInstance"])"
    $lines += "BootIsDegradation:     $($bootData["BootIsDegradation"])"
    $lines += ""

    $jsonReport["Boot"] = [ordered]@{
        EventTime                       = $bootTime.ToString("o")
        BootEndTime                     = $bootCompletionTime.ToString("o")
        BootTime_ms                     = [int]$bootData["BootTime"]
        MainPathBootTime_ms             = [int]$bootData["MainPathBootTime"]
        BootPostBootTime_ms             = [int]$bootData["BootPostBootTime"]
        KernelStartToEventLogReady_ms   = $null
        SystemBootInstance              = [int]$bootData["SystemBootInstance"]
        BootIsDegradation               = $bootData["BootIsDegradation"]
    }
} else {
    $kernelToEventLog = $null
    $eventLogReadyTime = $null
    if ($eventLogReady) {
        $eventLogReadyUtc = $eventLogReady.TimeCreated.ToUniversalTime()
        $kernelToEventLog = [long][math]::Round(
            ($eventLogReadyUtc - $bootTime).TotalMilliseconds
        )
        $eventLogReadyTime = $eventLogReadyUtc.ToString("o")
    }

    $lines += "Boot phase"
    $lines += "----------"
    $lines += "Kernel-General Event 12 time:       $bootTime"
    $lines += "EventLog Event 6005 time:           $($eventLogReady.TimeCreated)"
    $lines += "KernelStartToEventLogReady_ms:      $kernelToEventLog"
    $lines += ""

    $jsonReport["Boot"] = [ordered]@{
        EventTime                       = $bootTime.ToString("o")
        EventLogReadyTime               = $eventLogReadyTime
        BootTime_ms                     = $null
        MainPathBootTime_ms             = $null
        BootPostBootTime_ms             = $null
        KernelStartToEventLogReady_ms   = $kernelToEventLog
        SystemBootInstance              = $null
        BootIsDegradation               = $null
    }
}

if ($rebootEvent -and $bootCompletionTime) {
    $wallClock = ($bootCompletionTime -
        $rebootEvent.TimeCreated.ToUniversalTime()).TotalSeconds
    if ($wallClock -gt 0) {
        $lines += "Wall-clock reboot duration"
        $lines += "--------------------------"
        $lines += "From reboot initiation to measured endpoint: $([math]::Round($wallClock, 2)) seconds"
        $lines += ""

        $jsonReport["WallClockReboot_s"] = [math]::Round($wallClock, 2)
    }
}

if ($Json) {
    $jsonReport | ConvertTo-Json -Depth 3
} else {
    $lines -join [Environment]::NewLine
}
