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

    netcfg -v -l vioprot.inf -c p -i VIOPROT

## Protocol uninstallation (example of the batch):

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
* "ven_15b3&dev_101a" - Mellanox MT28800 Family

## Change the list of supported adapter without rebuilding the binary

Registry key: **HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\VioProt**

*Name* | *Type* | *Value* | *Note*
-------|--------|---------|--------
**NoDefaults** | DWORD | 1 | Disables built-in list
**Supported** | MULTI_SZ | List | Substrings in format "ven_XXXX&dev_XXXX", for example: "ven_8086&dev_1520XXXX"

**Note: Never include PnP ID of NETKVM adapter!**
