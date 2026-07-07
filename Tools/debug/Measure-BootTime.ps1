#  Measure-BootTime.ps1
#  Reports Windows boot timing from the Diagnostics-Performance event log.
#  Queries Event ID 100 (boot completed) to display boot duration metrics
#  for the most recent startup.
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

Reports boot timing from the Diagnostics-Performance event log (Event ID 100).

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

$LogName = "Microsoft-Windows-Diagnostics-Performance/Operational"

$logEnabled = $false
try {
    $logConfig = Get-WinEvent -ListLog $LogName -ErrorAction Stop
    $logEnabled = $logConfig.IsEnabled
} catch {
    Write-Error "The event log '$LogName' is not available on this system."
    Write-Error "This log requires Windows 10 / Server 2016 or later."
    exit 1
}

if (-not $logEnabled) {
    Write-Warning "The Diagnostics-Performance log is disabled. Enabling it now..."
    wevtutil sl $LogName /e:true
    Write-Warning "Log enabled. Reboot to generate boot timing events."
    exit 1
}

try {
    $bootEvents = Get-WinEvent -FilterHashtable @{
        LogName = $LogName
        Id      = 100
    } -MaxEvents $Count -ErrorAction Stop
} catch [Exception] {
    if ($_.FullyQualifiedErrorId -match "NoMatchingEventsFound") {
        Write-Warning "No boot events (Event ID 100) found in the log."
        Write-Warning "The system may not have completed a full boot cycle since the log was enabled."
        exit 0
    }
    throw
}

$results = @(foreach ($event in $bootEvents) {
    [xml]$xml = $event.ToXml()

    $eventData = @{}
    foreach ($d in $xml.Event.EventData.Data) {
        if ($d.Name) {
            $eventData[$d.Name] = $d.'#text'
        }
    }

    [PSCustomObject]@{
        BootEventTime       = $event.TimeCreated.ToString("o")
        BootTime_ms         = [int]$eventData["BootTime"]
        MainPathBootTime_ms = [int]$eventData["MainPathBootTime"]
        BootPostBootTime_ms = [int]$eventData["BootPostBootTime"]
        SystemBootInstance  = [int]$eventData["SystemBootInstance"]
        IsDegradation       = $eventData["BootIsDegradation"]
    }
})

if ($Json) {
    ConvertTo-Json -InputObject $results -Depth 3
} else {
    $results | Format-List
}
