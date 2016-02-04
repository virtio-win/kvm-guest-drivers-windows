/**********************************************************************
 * Copyright (c) 2012-2016 Red Hat, Inc.
 *
 * File: utils.c
 *
 * This file contains debug print routine implementation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include "utils.h"
#include <ntstrsafe.h>

int virtioDebugLevel;
int bDebugPrint;
int nViostorDebugLevel;

#if defined(COM_DEBUG)

#define RHEL_DEBUG_PORT     ((PUCHAR)0x3F8)
#define TEMP_BUFFER_SIZE    256

static void DebugPrintFuncSerial(const char *format, ...)
{
    char buf[TEMP_BUFFER_SIZE];
    NTSTATUS status;
    size_t len;
    va_list list;
    va_start(list, format);
    status = RtlStringCbVPrintfA(buf, sizeof(buf), format, list);
    if (status == STATUS_SUCCESS)
    {
        len = strlen(buf);
    }
    else
    {
        len = 2;
        buf[0] = 'O';
        buf[1] = '\n';
    }
    if (len)
    {
        WRITE_PORT_BUFFER_UCHAR(RHEL_DEBUG_PORT, (PUCHAR)buf, len);
        WRITE_PORT_UCHAR(RHEL_DEBUG_PORT, '\r');
    }
}
#endif

#if defined(PRINT_DEBUG)
static void DebugPrintFunc(const char *format, ...)
{
    va_list list;
    va_start(list, format);
    vDbgPrintEx(DPFLTR_DEFAULT_ID, 9 | DPFLTR_MASK, format, list);
}
#endif

static void DebugPrintFuncWPP(const char *format, ...)
{
// TODO later, if needed
}

static void NoDebugPrintFunc(const char *format, ...)
{

}
void InitializeDebugPrints(IN PDRIVER_OBJECT  DriverObject, IN PUNICODE_STRING RegistryPath)
{
    //TBD - Read nDebugLevel and bDebugPrint from the registry
    bDebugPrint = 1;
    virtioDebugLevel = 0;
    nViostorDebugLevel = TRACE_LEVEL_ERROR;

#if defined(EVENT_TRACING)
    VirtioDebugPrintProc = DebugPrintFuncWPP;
#elif defined(PRINT_DEBUG)
    VirtioDebugPrintProc = DebugPrintFunc;
#elif defined(COM_DEBUG)
    VirtioDebugPrintProc = DebugPrintFuncSerial;
#else
    VirtioDebugPrintProc = NoDebugPrintFunc;
#endif
}

tDebugPrintFunc VirtioDebugPrintProc;

char *DbgGetScsiOpStr(IN PSCSI_REQUEST_BLOCK Srb)
{
    PCDB pCdb = SRB_CDB(Srb);
    UCHAR scsiOp = pCdb->CDB6GENERIC.OperationCode;
    char *scsiOpStr = "?";

    if (pCdb) {
        switch (scsiOp){
            #undef MAKE_CASE
            #define MAKE_CASE(scsiOpCode) case scsiOpCode: scsiOpStr = #scsiOpCode; break;

            MAKE_CASE(SCSIOP_TEST_UNIT_READY)
            MAKE_CASE(SCSIOP_REWIND)    // aka SCSIOP_REZERO_UNIT
            MAKE_CASE(SCSIOP_REQUEST_BLOCK_ADDR)
            MAKE_CASE(SCSIOP_REQUEST_SENSE)
            MAKE_CASE(SCSIOP_FORMAT_UNIT)
            MAKE_CASE(SCSIOP_READ_BLOCK_LIMITS)
            MAKE_CASE(SCSIOP_INIT_ELEMENT_STATUS)   // aka SCSIOP_REASSIGN_BLOCKS
            MAKE_CASE(SCSIOP_RECEIVE)       // aka SCSIOP_READ6
            MAKE_CASE(SCSIOP_SEND)  // aka SCSIOP_WRITE6, SCSIOP_PRINT
            MAKE_CASE(SCSIOP_SLEW_PRINT)    // aka SCSIOP_SEEK6, SCSIOP_TRACK_SELECT
            MAKE_CASE(SCSIOP_SEEK_BLOCK)
            MAKE_CASE(SCSIOP_PARTITION)
            MAKE_CASE(SCSIOP_READ_REVERSE)
            MAKE_CASE(SCSIOP_FLUSH_BUFFER)      // aka SCSIOP_WRITE_FILEMARKS
            MAKE_CASE(SCSIOP_SPACE)
            MAKE_CASE(SCSIOP_INQUIRY)
            MAKE_CASE(SCSIOP_VERIFY6)
            MAKE_CASE(SCSIOP_RECOVER_BUF_DATA)
            MAKE_CASE(SCSIOP_MODE_SELECT)
            MAKE_CASE(SCSIOP_RESERVE_UNIT)
            MAKE_CASE(SCSIOP_RELEASE_UNIT)
            MAKE_CASE(SCSIOP_COPY)
            MAKE_CASE(SCSIOP_ERASE)
            MAKE_CASE(SCSIOP_MODE_SENSE)
            MAKE_CASE(SCSIOP_START_STOP_UNIT)   // aka SCSIOP_STOP_PRINT, SCSIOP_LOAD_UNLOAD
            MAKE_CASE(SCSIOP_RECEIVE_DIAGNOSTIC)
            MAKE_CASE(SCSIOP_SEND_DIAGNOSTIC)
            MAKE_CASE(SCSIOP_MEDIUM_REMOVAL)
            MAKE_CASE(SCSIOP_READ_FORMATTED_CAPACITY)
            MAKE_CASE(SCSIOP_READ_CAPACITY)
            MAKE_CASE(SCSIOP_READ)
            MAKE_CASE(SCSIOP_WRITE)
            MAKE_CASE(SCSIOP_SEEK)  // aka SCSIOP_LOCATE, SCSIOP_POSITION_TO_ELEMENT
            MAKE_CASE(SCSIOP_WRITE_VERIFY)
            MAKE_CASE(SCSIOP_VERIFY)
            MAKE_CASE(SCSIOP_SEARCH_DATA_HIGH)
            MAKE_CASE(SCSIOP_SEARCH_DATA_EQUAL)
            MAKE_CASE(SCSIOP_SEARCH_DATA_LOW)
            MAKE_CASE(SCSIOP_SET_LIMITS)
            MAKE_CASE(SCSIOP_READ_POSITION)
            MAKE_CASE(SCSIOP_SYNCHRONIZE_CACHE)
            MAKE_CASE(SCSIOP_COMPARE)
            MAKE_CASE(SCSIOP_COPY_COMPARE)
            MAKE_CASE(SCSIOP_WRITE_DATA_BUFF)
            MAKE_CASE(SCSIOP_READ_DATA_BUFF)
            MAKE_CASE(SCSIOP_CHANGE_DEFINITION)
            MAKE_CASE(SCSIOP_READ_SUB_CHANNEL)
            MAKE_CASE(SCSIOP_READ_TOC)
            MAKE_CASE(SCSIOP_READ_HEADER)
            MAKE_CASE(SCSIOP_PLAY_AUDIO)
            MAKE_CASE(SCSIOP_GET_CONFIGURATION)
            MAKE_CASE(SCSIOP_PLAY_AUDIO_MSF)
            MAKE_CASE(SCSIOP_PLAY_TRACK_INDEX)
            MAKE_CASE(SCSIOP_PLAY_TRACK_RELATIVE)
            MAKE_CASE(SCSIOP_GET_EVENT_STATUS)
            MAKE_CASE(SCSIOP_PAUSE_RESUME)
            MAKE_CASE(SCSIOP_LOG_SELECT)
            MAKE_CASE(SCSIOP_LOG_SENSE)
            MAKE_CASE(SCSIOP_STOP_PLAY_SCAN)
            MAKE_CASE(SCSIOP_READ_DISK_INFORMATION)
            MAKE_CASE(SCSIOP_READ_TRACK_INFORMATION)
            MAKE_CASE(SCSIOP_RESERVE_TRACK_RZONE)
            MAKE_CASE(SCSIOP_SEND_OPC_INFORMATION)
            MAKE_CASE(SCSIOP_MODE_SELECT10)
            MAKE_CASE(SCSIOP_MODE_SENSE10)
            MAKE_CASE(SCSIOP_CLOSE_TRACK_SESSION)
            MAKE_CASE(SCSIOP_READ_BUFFER_CAPACITY)
            MAKE_CASE(SCSIOP_SEND_CUE_SHEET)
            MAKE_CASE(SCSIOP_PERSISTENT_RESERVE_IN)
            MAKE_CASE(SCSIOP_PERSISTENT_RESERVE_OUT)
            MAKE_CASE(SCSIOP_REPORT_LUNS)
            MAKE_CASE(SCSIOP_BLANK)
            MAKE_CASE(SCSIOP_SEND_KEY)
            MAKE_CASE(SCSIOP_REPORT_KEY)
            MAKE_CASE(SCSIOP_MOVE_MEDIUM)
            MAKE_CASE(SCSIOP_LOAD_UNLOAD_SLOT)  // aka SCSIOP_EXCHANGE_MEDIUM
            MAKE_CASE(SCSIOP_SET_READ_AHEAD)
            MAKE_CASE(SCSIOP_READ_DVD_STRUCTURE)
            MAKE_CASE(SCSIOP_REQUEST_VOL_ELEMENT)
            MAKE_CASE(SCSIOP_SEND_VOLUME_TAG)
            MAKE_CASE(SCSIOP_READ_ELEMENT_STATUS)
            MAKE_CASE(SCSIOP_READ_CD_MSF)
            MAKE_CASE(SCSIOP_SCAN_CD)
            MAKE_CASE(SCSIOP_SET_CD_SPEED)
            MAKE_CASE(SCSIOP_PLAY_CD)
            MAKE_CASE(SCSIOP_MECHANISM_STATUS)
            MAKE_CASE(SCSIOP_READ_CD)
            MAKE_CASE(SCSIOP_SEND_DVD_STRUCTURE)
            MAKE_CASE(SCSIOP_INIT_ELEMENT_RANGE)
            MAKE_CASE(SCSIOP_READ16)
            MAKE_CASE(SCSIOP_WRITE16)
            MAKE_CASE(SCSIOP_VERIFY16)
            MAKE_CASE(SCSIOP_SYNCHRONIZE_CACHE16)
            MAKE_CASE(SCSIOP_READ_CAPACITY16)
        }
    }
    return scsiOpStr;
}
