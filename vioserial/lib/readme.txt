=================================================
Virtio-Serial driver user-mode interface library.
=================================================


1. API

1.1 VIOSStartup
The VIOSStartup method activates the Virtio-Serial driver user-mode interface library.

DLL_API
BOOL
VIOSStartup(void)

Parameters
None

Return Value
If the VIOSStartup method encounters no errors, it returns TRUE. Otherwise it returns FALSE.

Comments
None

1.2 VIOSCleanup
The VIOSCleanup method deactivates the Virtio-Serial driver user-mode interface library.
DLL_API
VOID
VIOSCleanup(void)

Parameters
None

Return Value
None

Comments
None

1.3 FindPort
The FindPort method finds port by name.
DLL_API
BOOL
FindPort(const wchar_t* name)

Parameters
    name
        Specifies the Unicode string name of the Port which should be fined.

Return Value
If the FindPort method encounters no errors, it returns TRUE. Otherwise it returns FALSE.

Comments
None

1.4 OpenPort
The OpenPort routine opens an existing Port.
DLL_API
PVOID
OpenPort(const wchar_t* name)

Parameters
    name
        Specifies the Unicode string name of the Port which should be opened.

Return Value
None

Comments
If the OpenPort method encounters no errors, it returns a valid pointer to a Port object. Otherwise it returns NULL.

1.5 ReadPort
The ReadPort routine reads data from an open Port.

DLL_API
BOOL
ReadPort(PVOID port, PVOID buf, PULONG size);

Parameters
    port
        Specifies the pointer to a Port object. This pointer is obtained by a successful call to OpenPort.
    buf
        Pointer to a caller-allocated buffer that receives the data read from the Port.
    size
        The size, in bytes, of the buffer pointed to by buf.

Return Value
If the ReadPort method encounters no errors, it returns TRUE. Otherwise it retunrs FALSE.

Comments
None

1.6 WritePort
The WritePort routine writes data to an open Port.

DLL_API
BOOL
WritePort(PVOID port, PVOID buf, ULONG size);

    port
        Specifies the pointer to a Port object. This pointer is obtained by a successful call to OpenPort.
    buf
        Pointer to a caller-allocated buffer that contains the data to write to the Port.

    size
        The size, in bytes, of the buffer pointed to by buf.

Return Value
If the WritePort method encounters no errors, it returns TRUE. Otherwise it returns FALSE.

Comments
None

1.7 ClosePort
The ClosePort routine closes a Port.
DLL_API
VOID
ClosePort(PVOID port)

Parameters
    port
        Pointer to a Port object.

Return Value
None
Comments
None

1.8 NumPorts
The NumPorts method returns number of Virtio-Serial Ports in the system.
DLL_API
UINT
NumPorts(void);

Parameters
None

Return Value
The number of Virtio-Serial Ports in the system.

Comments
None

1.9 PortSymbolicName
The PortSymbolicName method
DLL_API
wchar_t*
PortSymbolicName(int index)

Parameters
    index
        Specifies the Unicode string name of the Port which should be opened.

Return Value
If the ReadPort method encounters no errors, it returns TRUE. Otherwise it returns FALSE.

Comments
None

