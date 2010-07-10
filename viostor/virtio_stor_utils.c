/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * File: virtio_stor_utils.c
 *
 * This file contains debug print routine implementation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include"virtio_stor_utils.h"

ULONG
_cdecl
RhelDbgPrintToComPort(
    IN LPTSTR Format,
    ...
    )
{
    ULONG rc = 0;
    CHAR szbuffer[512];

    va_list marker;

    va_start( marker, Format );
    rc = vsnprintf(szbuffer, 512, Format, marker);
    va_end(marker);
    if (rc != -1) {
        WRITE_PORT_BUFFER_UCHAR(RHEL_DEBUG_PORT, (PUCHAR)&szbuffer[0], rc);
        WRITE_PORT_UCHAR(RHEL_DEBUG_PORT, '\r');
    } else {
        //
        // Just put an O for overflow, shouldn't get this really but
        // goot to let the user know in some way or another...
        //
        WRITE_PORT_UCHAR(RHEL_DEBUG_PORT, 'O');
        WRITE_PORT_UCHAR(RHEL_DEBUG_PORT, '\n');
    }
    return rc;
}

