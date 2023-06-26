# NETKVM: Windows driver for virtio-net network adapter

This README file covers specific features of NETKVM driver package.

- **[NETKVM configuration tool (netkvmco.dll)](#netkvmco)**
	- provides means to enumerate, read and change values of NetKVM driver parameters.
	NetKVMConfig also provides easy to use interface for scripting that allows product maintenance and testing automation.
- **[FAILOVER with SR-IOV virtual function](#netkvmp)**
	- provides support for virtio-net device in *standby* mode (see [qemu manual](https://www.qemu.org/docs/master/system/virtio-net-failover.html) for details)

## NETKVM configuration tool (netkvmco.dll) <a name="netkvmco"/>

### NetKVMConfig general description

NetKVMConfig is implemented as Windows Netsh extension module.
It provides Netsh-like standard command line interface and uses Netsh infrastructure to provide both interactive and batch modes of operation.
NetKVMConfig works with Windows standard parameters database (driver parameters key in registry created by INF file,
see [documentation](http://msdn.microsoft.com/en-us/library/ff570842%28v=VS.85%29.aspx) for details)
for network drivers thus securing NetKVM driver parameters set and values are synchronized all over the network adapter configuration tools included into original Windows distribution.

### NetKVMConfig usage manual

Following operations are supported:
- Registration in NetSH (installation)
	- `rundll32 netkvmco.dll,RegisterNetKVMNetShHelper`
	- Note: run the command from the administrator command line, netkvmco.dll must be in the current directory
- Unregistration (removal)
	- `rundll32 netkvmco.dll,UnregisterNetKVMNetShHelper`
- List NetKVM devices
	- `netsh netkvm show devices`
	- lists currently installed network. Unique index is assigned to each device. This index is to be used to identify the device for other operations.
- Restart given NetKVM device
	- `netsh netkvm restart [idx=]0-N`
	- Parameters: IDX - device index from "show devices" output
	- restarts device specified by the index
	- examples:
		- `netsh netkvm restart idx=0` or `netsh netkvm restart 2`
- Show NetKVM device parameters
	- `netsh netkvm show parameters [idx=]0-N`
	- Parameters: IDX - device index from "show devices" output
	- shows parameters for device specified by index.
	- examples:
		- `netsh netkvm show parameters idx=0` or `netsh netkvm show parameters 2`
- Show detailed information about specified NetKVM device parameter
	- `netsh netkvm show paraminfo [idx=]0-N [param=]name`
	- Parameters:
		- IDX - device index from "show devices" output.
		- PARAM - name of parameter.
	- shows detailed information about given parameter.
	- examples:
		- `netsh netkvm show paraminfo idx=0 param=window`
		- `netsh netkvm show paraminfo 2 rx_buffers`
- Read network device parameter value
	- `netsh netkvm getparam [idx=]0-N [param=]name`
	- Parameters:
		- IDX - device index from "show devices" output.
		- PARAM - name of parameter.
		- retrieves given parameter value.
	- examples:
		- `netsh netkvm getparam idx=0 param=window`
		- `netsh netkvm getparam 2 rx_buffers`
- Change NetKVM device parameter value
	- `netsh netkvm setparam [idx=]0-N [param=]name [value=]value`
	- Parameters:
		- IDX - device index from "show devices" output.
		- PARAM - name of parameter.
		- VALUE - value of the parameter.
	- sets given parameter value.
	- examples:
		- `netsh netkvm setparam idx=0 param=window value=10`
		- `netsh netkvm setparam 2 rx_buffers 64`
- Getting help for particular command:
	- `<command name> ?`
	- example: `netsh netkvm setparam ?`
- Working in interactive mode
	- The same commands are supported in netsh interactive mode using standard netsh invocation syntax and shortcuts.
	For more information see [NetSh documentation](https://learn.microsoft.com/en-us/windows-server/networking/technologies/netsh/netsh-contexts)

## FAILOVER with SR-IOV virtual function <a name="netkvmp"/>

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

## Protocol installation

(by admin, from the directory where **vioprot.inf** and **netkvmp.exe** are located)

`netkvmp install` or `netkvmp i`

## Protocol uninstallation

`netkvmp uninstall` or `netkvmp u` (by admin)
