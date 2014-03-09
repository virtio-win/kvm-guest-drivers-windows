/*
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Refer to the LICENSE file for full details of the license.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 */

#include <ntddk.h>
#include <ntstrsafe.h>

#include "trace.h"

#define TRACE_DEBUG_PORT ((PUCHAR)0x3f8)

#ifdef DBG

ULONG VirtRngDbgPrint(IN PCHAR Message, ...);

#define WPP_DEBUG(args) VirtRngDbgPrint args , VirtRngDbgPrint("\r")

/*#define VirtRngTraceMessage(Level, Flags, Message) do { \
		WRITE_PORT_UCHAR((PUCHAR)0x3f8, 'Z'); \
		DoTraceMessage Message; \
	} while (0)
	if (OsrDebugLevel >= (_level_) && \
	OsrDebugFlags & (_flag_)) {\
	DbgPrint (DRIVER_NAME": ");\
	DbgPrint _msg_; \
}*/

#endif // DBG

ULONG VirtRngDbgPrint(IN PCHAR Message, ...)
{
	NTSTATUS status;
	size_t length;

	WRITE_PORT_UCHAR((PUCHAR)0x3f8, 'X');

	status = RtlStringCbLengthA(Message, NTSTRSAFE_MAX_CCH * sizeof(CHAR),
		&length);

	if (NT_SUCCESS(status))
	{
		WRITE_PORT_BUFFER_UCHAR(TRACE_DEBUG_PORT, (PUCHAR)Message, length);
	}

	return status;
}
