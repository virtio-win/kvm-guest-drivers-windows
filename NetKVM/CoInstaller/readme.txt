# NetKVMConfig general description

NetKVMConfig is a tool for NetKVM driver configuration. NetKVMConfig provides means to enumerate, read and change values of NetKVM driver parameters. NetKVMConfig also provides easy to use interface for scripting that allows product maintenance and testing automation.

NetKVMConfig is implemented as Windows Netsh extension module. It provides Netsh-like standard command line interface and uses Netsh infrastructure to provide both interactive and batch modes of operation.

NetKVMConfig works with Windows standard parameters database (driver parameters key in registry created by INF file, see http://msdn.microsoft.com/en-us/library/ff570842%28v=VS.85%29.aspx for details) for network drivers thus securing NetKVM driver parameters set and values are synchronized all over the network adapter configuration tools included into original Windows distribution.

## NetKVMConfig usage manual

Following operations are supported:
1.	Registration in NetSH (instllation)
rundll32 netkvmco.dll,RegisterNetKVMNetShHelper

2.	Unregistration (removal)
rundll32 netkvmco.dll,UnregisterNetKVMNetShHelper

3.	List NetKVM devices
netsh netkvm show devices
Parameters: None
Remarks: Lists NetKVM devices currently installed. Unique index is assigned to each device. This index to be used to identify the device for other operations.

4.	Restart given NetKVM device
netsh netkvm restart [idx=]0-N
Parameters: IDX - device index from "show devices" output
Remarks: Restarts device specified by index.
Examples: “netsh netkvm restart idx=0” or “netsh netkvm restart 2”

5.	Show NetKVM device parameters
netsh netkvm show parameters [idx=]0-N
Parameters: IDX - device index from "show devices" output
Remarks: Shows parameters for device specified by index.
Examples: “netsh netkvm show parameters idx=0” or “netsh netkvm show parameters 2”

6.	Show detailed information about specified NetKVM device parameter
netsh netkvm show paraminfo [idx=]0-N [param=]name
Parameters:
	IDX - device index from "show devices" output.
	PARAM - name of parameter.
Remarks: Shows detailed information about given parameter.
Examples:
	netsh netkvm show paraminfo idx=0 param=window
	netsh netkvm show paraminfo 2 rx_buffers

7.	Read NetKVM device parameter value
netsh netkvm getparam [idx=]0-N [param=]name
Parameters:
	IDX - device index from "show devices" output.
	PARAM - name of parameter.
Remarks: Retrieves given parameter value.
Examples:
	netsh netkvm getparam idx=0 param=window
	netsh netkvm getparam 2 rx_buffers

8.	Change NetKVM device parameter value
netsh netkvm setparam [idx=]0-N [param=]name [value=]value
Parameters:
	IDX - device index from "show devices" output.
	PARAM - name of parameter.
	VALUE - value of the parameter.
Remarks: Set given parameter value.
Examples:
	netsh netkvm setparam idx=0 param=window value=10
	netsh netkvm setparam 2 rx_buffers 45

9.	Auxiliary operations
Getting help for particular command: <command name> ?
	Example: netsh netkvm setparam ?

10.	Working in interactive mode:
The same commands are supported in netsh interactive mode using standard netsh invocation syntax and shortcuts. For more information see netsh documentation (see http://www.microsoft.com/resources/documentation/windows/xp/all/proddocs/en-us/netsh.mspx?mfr=true for details)
