#  Measure-BootTime.ps1
#  Reports Windows boot timing from the event logs. Uses the detailed
#  Diagnostics-Performance data when available and falls back to events in
#  the System log on Windows Server.
#
#  Minimum supported OS: Windows 10 / Windows Server 2016 (32-bit and 64-bit)
#  Requires: Administrator privileges, PowerShell 5.1+
#
#  Usage:
#    powershell.exe -ExecutionPolicy Bypass -File .\Measure-BootTime.ps1
#    powershell.exe -ExecutionPolicy Bypass -File .\Measure-BootTime.ps1 -Count 5
#    powershell.exe -ExecutionPolicy Bypass -File .\Measure-BootTime.ps1 -Json

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
    [ValidateRange(1, 50)]
    [int]$Count = 1,

    [switch]$Json,

    [switch]$Help
)

if ($Help) {
    Write-Host @"
Measure-BootTime.ps1 - Windows boot time diagnostics

Reports boot timing from the Windows event logs. Detailed boot metrics are
shown when Diagnostics-Performance Event ID 100 is available. Otherwise, the
script reports the time from kernel start until the Event Log service starts.

Parameters:
  -Count <int>   Number of recent boot events to display (default: 1, max: 50)
  -Json          Output results as JSON (for automation)
  -Help          Show this help message

Examples:
  .\Measure-BootTime.ps1              # Last boot event
  .\Measure-BootTime.ps1 -Count 5     # Last 5 boot events
  .\Measure-BootTime.ps1 -Json        # JSON output for scripting
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

$DiagLog = "Microsoft-Windows-Diagnostics-Performance/Operational"
$SystemLog = "System"

$diagLogEnabled = $false
try {
    $logConfig = Get-WinEvent -ListLog $DiagLog -ErrorAction Stop
    $diagLogEnabled = $logConfig.IsEnabled
} catch {
    # This channel is not present on Windows Server. Use the System log below.
}

$bootEvents = @()
if ($diagLogEnabled) {
    $bootEvents = @(Get-WinEvent -FilterHashtable @{
        LogName = $DiagLog
        Id      = 100
    } -MaxEvents $Count -ErrorAction SilentlyContinue)
}

if ($bootEvents.Count -gt 0) {
    $results = @(foreach ($event in $bootEvents) {
        $eventData = Get-EventDataMap $event

        [PSCustomObject]@{
            MeasurementSource                 = "DiagnosticsPerformance"
            MeasurementScope                  = "WindowsBootPerformance"
            BootEventTime                     = $event.TimeCreated.ToString("o")
            EventLogReadyTime                 = $null
            BootTime_ms                       = [int]$eventData["BootTime"]
            MainPathBootTime_ms               = [int]$eventData["MainPathBootTime"]
            BootPostBootTime_ms               = [int]$eventData["BootPostBootTime"]
            KernelStartToEventLogReady_ms     = $null
            SystemBootInstance                = [int]$eventData["SystemBootInstance"]
            IsDegradation                     = $eventData["BootIsDegradation"]
        }
    })
} else {
    $kernelStartEvents = @(Get-WinEvent -FilterHashtable @{
        LogName      = $SystemLog
        ProviderName = "Microsoft-Windows-Kernel-General"
        Id           = 12
    } -MaxEvents $Count -ErrorAction SilentlyContinue)

    if ($kernelStartEvents.Count -eq 0) {
        Write-Warning "No kernel start events (System log, Kernel-General Event ID 12) found."
        exit 0
    }

    $serviceStartEvents = @(Get-WinEvent -FilterHashtable @{
        LogName      = $SystemLog
        ProviderName = "EventLog"
        Id           = 6005
    } -MaxEvents ([math]::Max($Count * 4, 50)) -ErrorAction SilentlyContinue)

    $results = @(for ($i = 0; $i -lt $kernelStartEvents.Count; $i++) {
        $kernelStart = $kernelStartEvents[$i]
        $kernelStartData = Get-EventDataMap $kernelStart
        $kernelStartTime = ConvertTo-UtcEventTime `
            -Value $kernelStartData["StartTime"] -Fallback $kernelStart.TimeCreated
        $nextKernelStartTime = $null
        if ($i -gt 0) {
            $nextKernelStartData = Get-EventDataMap $kernelStartEvents[$i - 1]
            $nextKernelStartTime = ConvertTo-UtcEventTime `
                -Value $nextKernelStartData["StartTime"] `
                -Fallback $kernelStartEvents[$i - 1].TimeCreated
        }

        $eventLogReady = $serviceStartEvents |
            Where-Object {
                $readyTime = $_.TimeCreated.ToUniversalTime()
                $readyTime -ge $kernelStartTime -and
                (-not $nextKernelStartTime -or $readyTime -lt $nextKernelStartTime)
            } |
            Sort-Object TimeCreated |
            Select-Object -First 1

        $startupDuration = $null
        $eventLogReadyTime = $null
        if ($eventLogReady) {
            $eventLogReadyUtc = $eventLogReady.TimeCreated.ToUniversalTime()
            $startupDuration = [long][math]::Round(
                ($eventLogReadyUtc - $kernelStartTime).TotalMilliseconds
            )
            $eventLogReadyTime = $eventLogReadyUtc.ToString("o")
        }

        [PSCustomObject]@{
            MeasurementSource                 = "SystemEventLog"
            MeasurementScope                  = "KernelStartToEventLogReady"
            BootEventTime                     = $kernelStartTime.ToString("o")
            EventLogReadyTime                 = $eventLogReadyTime
            BootTime_ms                       = $null
            MainPathBootTime_ms               = $null
            BootPostBootTime_ms               = $null
            KernelStartToEventLogReady_ms     = $startupDuration
            SystemBootInstance                = $null
            IsDegradation                     = $null
        }
    })
}

if ($Json) {
    ConvertTo-Json -InputObject $results -Depth 3
} else {
    $results | Format-List
}
