# NETKVM support for 'failover' feature

If the virtio-net-pci device is configured with 'failover=on'
in the command line the virtio-net is not expected to be used
as a standalone device but in combination with Virtual function
(VF) of a SRIOV-capable host adapter. In this case the virtio-net
and the VF must have identical MAC addresses.
Unlike implementation of virtio-net failover feature in Linux
failover virtio-net device in Windows starts function only after
the VIOPROT protocol driver is installed:
1. System start without VIOPROT installed:
   NETKVM driver **does not indicate link-up** until it recognizes
   presence of respective VF with the same MAC address (for that
   the VIOPROT is required)
2. Upon VIOPROT installation the NETKVM adapter **indicates link-up**
   and starts working using VF data channel to its respective
   SRIOV physical function (PF)
3. When the migration process is started the QEMU hides the VF
   device on the PCI bus, VF adapter on the source VM disappears
   and the NETKVM continues transferring data via TAP adapter
   on the host it is configured to use (typically this TAP
   adapter is attached to the same bridge as the PF.
4. Until the migration is completed the virtio-net/NETKVM is
   the **fallback network adapter on the guest OS** (similar to Linux).
   Note that on this stage of the migration the VF is not used
   by the VM anymore. But because the VF is present and has the same
   MAC address as the virtio-net device it is possible that the PF
   will drop the traffic that comes from the virtio-net via its TAP
   as the network patckets usually have the same source MAC address
   as the VF. So, it is highly recommended to change the MAC address
   of the VF as soon as it is hidden on the VM PCI bus.
5. When the migration is done QEMU attaches a new VF to the PCI bus
   of the VM on the destination system. NETKVM recognizes it and switches
   to the datapath of the new VF.


# Notify object of VIOPROT protocol supported by NETKVM

The notify object runs on protocol installation (it checks all the
existing adapters) and on installation of new adapter (it checks the
new adapter):

If the adapter's PnP ID matches the list of supported ones, the notify
object unbinds it from all the protocols and binds only to VIOPROT.
It there is NETKVM adapter with the MAC address identical to one of
checked netwrok adapter, these 2 adapters with the same MAC address
will be 'teamed' (subject to compatibility check) and the NETKVM uses
the teamed adapter for RX and TX.

## Protocol installation
(by admin, from the directory where vioprot.inf is located)
See: InstallProtocol.cmd

    netcfg -v -l vioprot.inf -c p -i VIOPROT

## Protocol uninstallation (example of the batch):
See: UninstallProtocol.cmd
    netcfg -v -u VIOPROT
    timeout /t 3
    for %%f in (c:\windows\inf\oem*.inf) do call :checkinf %%f
    echo Done
    timeout /t 3
    goto :eof
    :checkinf
    type %1 | findstr /i vioprot.cat
    if not errorlevel 1 goto :removeinf
    echo %1 is not VIOPROT inf file
    goto :eof
    :removeinf
    echo This is VIOPROT inf file
    pnputil /d "%~nx1"
    timeout /t 2
    goto :eof

## Supported adapters

Built-in list at the moment of writing:
* "ven_8086&dev_1515" - Intel X540 Virtual Function
* "ven_8086&dev_10ca" - Intel 82576 Virtual Function
* "ven_8086&dev_15a8" - Intel Ethernet Connection X552
* "ven_8086&dev_154c" - Intel 700 Series VF
* "ven_8086&dev_10ed" - Intel 82599 VF
* "ven_15b3&dev_101a" - Mellanox MT28800 Family
* "ven_14e4&dev_16dc" - Broadcom NetXtreme-E Virtual Function
* "ven_14e4&dev_16af" - Broadcom NetXtreme II BCM57810 Virtual Function
* "...bdrv\\l2ndv&pci_16af14e4" - Broadcom NetXtreme II BCM57810 Virtual Function (QLogic miniport driver)

## Change the list of supported adapter without rebuilding the binary

Registry key: **HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\VioProt**

*Name*         | *Type*   | *Value* | *Note*
---------------|----------|---------|--------
**NoDefaults** | DWORD    | 1       | Disables built-in list
**Supported**  | MULTI_SZ | List    | Substrings in format "ven_XXXX&dev_XXXX", for example: "ven_8086&dev_1520XXXX"

**Note: Never include PnP ID of NETKVM adapter!**
