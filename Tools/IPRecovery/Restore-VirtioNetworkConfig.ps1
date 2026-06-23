<#
.SYNOPSIS
    Restores VirtIO network adapter configuration from a saved configuration file.

.DESCRIPTION
    This script restores network settings for VirtIO network adapters from a previously
    saved configuration file. It reads the configuration file created by the VirtIO
    installer and applies the saved static IP settings, DNS configuration, and gateway
    settings to matching network adapters.

.PARAMETER ConfigFile
    Path to the configuration file. Defaults to C:\virtio.cfg

.EXAMPLE
    .\Restore-VirtioNetworkConfig.ps1
    Restores network configuration from C:\virtio.cfg

.EXAMPLE
    .\Restore-VirtioNetworkConfig.ps1 -ConfigFile D:\backup\virtio.cfg
    Restores network configuration from a custom location

.NOTES
    Copyright (C) 2024 Red Hat, Inc.
    Written By: Vadim Rozenfeld <vrozenfe@redhat.com>

    Requirements:
    - Must be run with Administrator privileges
    - VirtIO network adapters must be present
    - Configuration file must exist
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ConfigFile = "C:\virtio.cfg"
)

#Requires -RunAsAdministrator

# Configuration file field names
$MACADDR = "MACAddress"
$DESCR = "Description"
$DHCPEN = "DHCPEnabled"
$IPADDR = "IPAddress"
$IPSUBNET = "IPSubnet"
$IPADDRPRIM = "IPAddressPrim"
$IPSUBNETPRIM = "IPSubnetPrim"
$IPCONMET = "IPConnectionMetric"
$DNSDOM = "DNSDomain"
$IPEN = "IPEnabled"
$DEFIPGW = "DefaultIPGateway"
$IPFLTSECEN = "IPFilterSecurityEnabled"
$DNSDMSUFSRCHORD = "DNSDomainSuffixSearchOrder"
$DNSHOSTNAME = "DNSHostName"
$DNSWINSRES = "DNSEnabledForWINSResolution"
$DNSSRCHORD = "DNSServerSearchOrder"
$INDX = "Index"
$SRVSNAME = "ServiceName"
$SETTINGID = "SettingID"

# VT types from COM
$VT_I4 = 3
$VT_BSTR = 8
$VT_BOOL = 11
$VT_ARRAY_BSTR = 8200  # VT_ARRAY | VT_BSTR

function Test-IPv4Address {
    param([string]$Address)

    if ($Address -match '^(\d{1,3}\.){3}\d{1,3}$') {
        $octets = $Address -split '\.'
        foreach ($octet in $octets) {
            if ([int]$octet -lt 0 -or [int]$octet -gt 255) {
                return $false
            }
        }
        return $true
    }
    return $false
}

function Split-AddressList {
    param(
        [string]$AddressList,
        [string]$Type = "None"
    )

    if ([string]::IsNullOrEmpty($AddressList)) {
        return @()
    }

    $addresses = $AddressList -split ','
    $result = @()

    foreach ($addr in $addresses) {
        $addr = $addr.Trim()
        if ($Type -eq "IPv4") {
            if (Test-IPv4Address $addr) {
                $result += $addr
            }
        } elseif ($Type -eq "IPv6") {
            if (-not (Test-IPv4Address $addr)) {
                $result += $addr
            }
        } else {
            $result += $addr
        }
    }

    return $result
}

function Read-ConfigFile {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        throw "Configuration file not found: $Path"
    }

    $adapters = @()
    $currentAdapter = @{}

    Get-Content $Path | ForEach-Object {
        $line = $_.Trim()
        if ([string]::IsNullOrEmpty($line)) {
            return
        }

        $parts = $line -split '\t'
        if ($parts.Count -ne 3) {
            return
        }

        $fieldName = $parts[0]
        $dataType = [int]$parts[1]
        $value = $parts[2]

        switch ($dataType) {
            $VT_I4 {
                $currentAdapter[$fieldName] = [int]$value
            }
            $VT_BSTR {
                $currentAdapter[$fieldName] = $value
            }
            $VT_BOOL {
                $currentAdapter[$fieldName] = [bool][int]$value
            }
            $VT_ARRAY_BSTR {
                $currentAdapter[$fieldName] = $value
            }
        }

        # When we hit ServiceName, that's the end of an adapter config
        if ($fieldName -eq $SRVSNAME) {
            $adapters += $currentAdapter
            $currentAdapter = @{}
        }
    }

    return $adapters
}

function Get-PrimaryIPOrder {
    param(
        [string]$SettingID,
        [string]$PrimaryIP,
        [string]$PrimarySubnet
    )

    if ([string]::IsNullOrEmpty($SettingID) -or
        [string]::IsNullOrEmpty($PrimaryIP) -or
        [string]::IsNullOrEmpty($PrimarySubnet)) {
        return $null
    }

    try {
        # Get current control set
        $currentControlSet = (Get-ItemProperty "HKLM:\SYSTEM\Select").Current
        $controlSet = "ControlSet{0:D3}" -f $currentControlSet

        $regPath = "HKLM:\SYSTEM\$controlSet\Services\Tcpip\Parameters\Interfaces\$SettingID"

        if (Test-Path $regPath) {
            $regIPAddress = (Get-ItemProperty $regPath).IPAddress
            $regSubnetMask = (Get-ItemProperty $regPath).SubnetMask

            if (-not $regSubnetMask) {
                $regSubnetMask = (Get-ItemProperty $regPath).IPMask
            }

            if ($regIPAddress -and $regIPAddress.Count -gt 0) {
                return @{
                    IP = $regIPAddress[0]
                    Subnet = if ($regSubnetMask) { $regSubnetMask[0] } else { $null }
                }
            }
        }
    } catch {
        Write-Warning "Failed to read IP order from registry: $_"
    }

    return $null
}

function Restore-AdapterConfig {
    param($AdapterConfig)

    $macAddr = $AdapterConfig[$MACADDR]
    $serviceName = $AdapterConfig[$SRVSNAME]

    if ([string]::IsNullOrEmpty($macAddr) -or $serviceName -ne "netkvm") {
        Write-Verbose "Skipping adapter (MAC: $macAddr, Service: $serviceName)"
        return
    }

    Write-Host "Processing adapter: $macAddr"

    # Find the matching network adapter configuration
    $wmiAdapter = Get-WmiObject -Class Win32_NetworkAdapterConfiguration | Where-Object {
        $_.MACAddress -eq $macAddr -and $_.ServiceName -eq $serviceName
    }

    if (-not $wmiAdapter) {
        Write-Warning "Adapter not found for MAC address: $macAddr"
        return
    }

    Write-Host "  Found adapter: $($AdapterConfig[$DESCR])"

    # Parse IP addresses and subnets
    $ipv4Addresses = Split-AddressList $AdapterConfig[$IPADDR] "IPv4"
    $ipv4Subnets = Split-AddressList $AdapterConfig[$IPSUBNET] "IPv4"
    $ipv6Addresses = Split-AddressList $AdapterConfig[$IPADDR] "IPv6"
    $ipv6Subnets = Split-AddressList $AdapterConfig[$IPSUBNET] "IPv6"

    # Parse gateways
    $ipv4Gateways = Split-AddressList $AdapterConfig[$DEFIPGW] "IPv4"
    $ipv6Gateways = Split-AddressList $AdapterConfig[$DEFIPGW] "IPv6"

    # Parse DNS settings
    $dnsServers = Split-AddressList $AdapterConfig[$DNSSRCHORD] "None"
    $dnsDomainSuffixes = Split-AddressList $AdapterConfig[$DNSDMSUFSRCHORD] "None"

    # Reorder IPv4 addresses to match registry order
    $primaryIP = $AdapterConfig[$IPADDRPRIM]
    $primarySubnet = $AdapterConfig[$IPSUBNETPRIM]

    if (-not [string]::IsNullOrEmpty($primaryIP) -and -not [string]::IsNullOrEmpty($primarySubnet)) {
        for ($i = 0; $i -lt $ipv4Addresses.Count; $i++) {
            if ($ipv4Addresses[$i] -eq $primaryIP -and $ipv4Subnets[$i] -eq $primarySubnet) {
                if ($i -ne 0) {
                    # Swap to make it first
                    $tempIP = $ipv4Addresses[0]
                    $tempSubnet = $ipv4Subnets[0]
                    $ipv4Addresses[0] = $ipv4Addresses[$i]
                    $ipv4Subnets[0] = $ipv4Subnets[$i]
                    $ipv4Addresses[$i] = $tempIP
                    $ipv4Subnets[$i] = $tempSubnet
                }
                break
            }
        }
    }

    # Restore IPv4 static configuration
    if ($ipv4Addresses.Count -gt 0 -and $ipv4Subnets.Count -gt 0) {
        Write-Host "  Restoring IPv4 addresses..."
        try {
            $result = $wmiAdapter.EnableStatic($ipv4Addresses, $ipv4Subnets)
            if ($result.ReturnValue -eq 0) {
                Write-Host "    IPv4 addresses restored successfully"
            } else {
                Write-Warning "    IPv4 EnableStatic returned code: $($result.ReturnValue)"
            }
        } catch {
            Write-Warning "    Failed to restore IPv4 addresses: $_"
        }
    }

    # Restore IPv6 static configuration
    if ($ipv6Addresses.Count -gt 0 -and $ipv6Subnets.Count -gt 0) {
        Write-Host "  Restoring IPv6 addresses..."
        try {
            $result = $wmiAdapter.EnableStatic($ipv6Addresses, $ipv6Subnets)
            if ($result.ReturnValue -eq 0) {
                Write-Host "    IPv6 addresses restored successfully"
            } else {
                Write-Warning "    IPv6 EnableStatic returned code: $($result.ReturnValue)"
            }
        } catch {
            Write-Warning "    Failed to restore IPv6 addresses: $_"
        }
    }

    # Restore IPv4 gateways
    if ($ipv4Gateways.Count -gt 0) {
        Write-Host "  Restoring IPv4 gateways..."
        try {
            $result = $wmiAdapter.SetGateways($ipv4Gateways, @(1) * $ipv4Gateways.Count)
            if ($result.ReturnValue -eq 0) {
                Write-Host "    IPv4 gateways restored successfully"
            } else {
                Write-Warning "    SetGateways returned code: $($result.ReturnValue)"
            }
        } catch {
            Write-Warning "    Failed to restore IPv4 gateways: $_"
        }
    }

    # Restore IPv6 gateways
    if ($ipv6Gateways.Count -gt 0) {
        Write-Host "  Restoring IPv6 gateways..."
        try {
            $result = $wmiAdapter.SetGateways($ipv6Gateways, @(1) * $ipv6Gateways.Count)
            if ($result.ReturnValue -eq 0) {
                Write-Host "    IPv6 gateways restored successfully"
            } else {
                Write-Warning "    SetGateways returned code: $($result.ReturnValue)"
            }
        } catch {
            Write-Warning "    Failed to restore IPv6 gateways: $_"
        }
    }

    # Restore DNS servers
    if ($dnsServers.Count -gt 0) {
        Write-Host "  Restoring DNS servers..."
        try {
            $result = $wmiAdapter.SetDNSServerSearchOrder($dnsServers)
            if ($result.ReturnValue -eq 0) {
                Write-Host "    DNS servers restored successfully"
            } else {
                Write-Warning "    SetDNSServerSearchOrder returned code: $($result.ReturnValue)"
            }
        } catch {
            Write-Warning "    Failed to restore DNS servers: $_"
        }
    }

    # Restore DNS domain settings
    $dnsHostName = $AdapterConfig[$DNSHOSTNAME]
    $dnsDomain = $AdapterConfig[$DNSDOM]

    if (-not [string]::IsNullOrEmpty($dnsHostName) -or
        -not [string]::IsNullOrEmpty($dnsDomain) -or
        $dnsServers.Count -gt 0 -or
        $dnsDomainSuffixes.Count -gt 0) {

        Write-Host "  Restoring DNS configuration..."
        try {
            $result = $wmiAdapter.SetDNSDomain($dnsDomain)
            if ($result.ReturnValue -eq 0) {
                Write-Host "    DNS domain configuration restored successfully"
            } else {
                Write-Warning "    SetDNSDomain returned code: $($result.ReturnValue)"
            }
        } catch {
            Write-Warning "    Failed to restore DNS domain: $_"
        }
    }

    Write-Host "  Adapter restoration completed"
}

# Main script execution
try {
    Write-Host "VirtIO Network Configuration Restore Utility"
    Write-Host "Copyright (C) 2026 Red Hat, Inc."
    Write-Host ""
    Write-Host "Using configuration file: $ConfigFile"
    Write-Host ""

    # Read configuration file
    Write-Host "Reading configuration file..."
    $adapters = Read-ConfigFile -Path $ConfigFile

    if ($adapters.Count -eq 0) {
        Write-Warning "No adapter configurations found in file"
        exit 1
    }

    Write-Host "Found $($adapters.Count) adapter configuration(s)"
    Write-Host ""

    # Restore each adapter
    foreach ($adapter in $adapters) {
        Restore-AdapterConfig -AdapterConfig $adapter
        Write-Host ""
    }

    Write-Host "Network configuration restoration completed"
    Write-Host "You may need to restart the network adapters or reboot for changes to take full effect."

} catch {
    Write-Error "An error occurred: $_"
    Write-Error $_.ScriptStackTrace
    exit 1
}
