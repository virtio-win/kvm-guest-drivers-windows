// SharedIoctls.h
// This file is shared between the Kernel Driver and the User-Mode App.

#pragma once

// CTL_CODE, FILE_DEVICE_UNKNOWN, METHOD_BUFFERED and FILE_ANY_ACCESS come from
// wdm.h/ntddk.h in kernel mode and from winioctl.h in user mode. Pull in the
// user-mode header when it has not already been provided so this file is
// self-contained for the user-mode controller; the kernel driver includes
// wdm.h before this header.
#ifndef CTL_CODE
#ifndef _KERNEL_MODE
#include <winioctl.h>
#endif
#endif

#define IOCTL_SPLITTER_ENABLE       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SPLITTER_DISABLE      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SPLITTER_QUERY_STATUS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)