/*
 * This file contains debug print routine implementation.
 *
 * Copyright (c) 2012-2017 Red Hat, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission. THIS SOFTWARE IS
 * PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "helper.h"
#include "trace.h"

int virtioDebugLevel;
int bDebugPrint;
int nVioscsiDebugLevel;

#if !defined(EVENT_TRACING)

#if defined(COM_DEBUG)
#include <ntstrsafe.h>

#define RHEL_DEBUG_PORT ((PUCHAR)0x3F8)
#define TEMP_BUFFER_SIZE 256

static void
DebugPrintFuncSerial (const char *format, ...)
{
  char buf[TEMP_BUFFER_SIZE];
  NTSTATUS status;
  size_t len;
  va_list list;
  va_start (list, format);
  status = RtlStringCbVPrintfA (buf, sizeof (buf), format, list);
  if (status == STATUS_SUCCESS)
    {
      len = strlen (buf);
    }
  else
    {
      len = 2;
      buf[0] = 'O';
      buf[1] = '\n';
    }
  if (len)
    {
      WRITE_PORT_BUFFER_UCHAR (RHEL_DEBUG_PORT, (PUCHAR)buf, len);
      WRITE_PORT_UCHAR (RHEL_DEBUG_PORT, '\r');
    }
  va_end (list);
}
#elif defined(PRINT_DEBUG)
static void
DebugPrintFunc (const char *format, ...)
{
  va_list list;
  va_start (list, format);
  vDbgPrintEx (DPFLTR_DEFAULT_ID, 9 | DPFLTR_MASK, format, list);
  va_end (list);
}
#else
static void
NoDebugPrintFunc (const char *format, ...)
{
}
#endif

void
InitializeDebugPrints (IN PDRIVER_OBJECT DriverObject,
                       IN PUNICODE_STRING RegistryPath)
{
  // TBD - Read nDebugLevel and bDebugPrint from the registry
  bDebugPrint = 1;
  virtioDebugLevel = 0;
  nVioscsiDebugLevel = TRACE_LEVEL_ERROR;

#if defined(PRINT_DEBUG)
  VirtioDebugPrintProc = DebugPrintFunc;
#elif defined(COM_DEBUG)
  VirtioDebugPrintProc = DebugPrintFuncSerial;
#else
  VirtioDebugPrintProc = NoDebugPrintFunc;
#endif
}

tDebugPrintFunc VirtioDebugPrintProc;
#else
void
InitializeDebugPrints (IN PDRIVER_OBJECT DriverObject,
                       IN PUNICODE_STRING RegistryPath)
{
  // TBD - Read nDebugLevel and bDebugPrint from the registry
  bDebugPrint = 0;
  virtioDebugLevel = 0;
  nVioscsiDebugLevel = 4; // TRACE_LEVEL_ERROR;
}

tDebugPrintFunc VirtioDebugPrintProc = DbgPrint;
#endif

#undef MAKE_CASE
#define MAKE_CASE(scsiOpCode)                                                 \
  case scsiOpCode:                                                            \
    scsiOpStr = #scsiOpCode;                                                  \
    break;

char *
DbgGetScsiOpStr (IN UCHAR opCode)
{
  char *scsiOpStr = "?";
  switch (opCode)
    {
      MAKE_CASE (SCSIOP_TEST_UNIT_READY)    // Code 0x00
      MAKE_CASE (SCSIOP_REWIND)             // Code 0x01
      MAKE_CASE (SCSIOP_REQUEST_BLOCK_ADDR) // Code 0x02
      MAKE_CASE (SCSIOP_REQUEST_SENSE)      // Code 0x03
      MAKE_CASE (SCSIOP_FORMAT_UNIT)        // Code 0x04
      MAKE_CASE (SCSIOP_READ_BLOCK_LIMITS)  // Code 0x05
      MAKE_CASE (
          SCSIOP_INIT_ELEMENT_STATUS) // Code 0x07, aka SCSIOP_REASSIGN_BLOCKS
      MAKE_CASE (SCSIOP_READ6)        // Code 0x08, aka SCSIOP_RECEIVE
      MAKE_CASE (SCSIOP_WRITE6)     // Code 0x0A, aka SCSIOP_PRINT, SCSIOP_SEND
      MAKE_CASE (SCSIOP_SEEK6)      // Code 0x0B, aka SCSIOP_SET_CAPACITY,
                                    // SCSIOP_SLEW_PRINT, SCSIOP_TRACK_SELECT
      MAKE_CASE (SCSIOP_SEEK_BLOCK) // Code 0x0C
      MAKE_CASE (SCSIOP_PARTITION)  // Code 0x0D
      MAKE_CASE (SCSIOP_READ_REVERSE) // Code 0x0F
      MAKE_CASE (SCSIOP_FLUSH_BUFFER) // Code 0x10, aka SCSIOP_WRITE_FILEMARKS
      MAKE_CASE (SCSIOP_SPACE)        // Code 0x11
      MAKE_CASE (SCSIOP_INQUIRY)      // Code 0x12
      MAKE_CASE (SCSIOP_VERIFY6)      // Code 0x13
      MAKE_CASE (SCSIOP_RECOVER_BUF_DATA) // Code 0x14
      MAKE_CASE (SCSIOP_MODE_SELECT)      // Code 0x15
      MAKE_CASE (SCSIOP_RESERVE_UNIT)     // Code 0x16
      MAKE_CASE (SCSIOP_RELEASE_UNIT)     // Code 0x17
      MAKE_CASE (SCSIOP_COPY)             // Code 0x18
      MAKE_CASE (SCSIOP_ERASE)            // Code 0x19
      MAKE_CASE (SCSIOP_MODE_SENSE)       // Code 0x1A
      MAKE_CASE (SCSIOP_START_STOP_UNIT)  // Code 0x1B, aka SCSIOP_LOAD_UNLOAD,
                                          // SCSIOP_STOP_PRINT
      MAKE_CASE (SCSIOP_RECEIVE_DIAGNOSTIC)      // Code 0x1C
      MAKE_CASE (SCSIOP_SEND_DIAGNOSTIC)         // Code 0x1D
      MAKE_CASE (SCSIOP_MEDIUM_REMOVAL)          // Code 0x1E
      MAKE_CASE (SCSIOP_READ_FORMATTED_CAPACITY) // Code 0x23
      MAKE_CASE (SCSIOP_READ_CAPACITY)           // Code 0x25
      MAKE_CASE (SCSIOP_READ)                    // Code 0x28
      MAKE_CASE (SCSIOP_WRITE)                   // Code 0x2A
      MAKE_CASE (SCSIOP_SEEK)              // Code 0x2B, aka SCSIOP_LOCATE,
                                           // SCSIOP_POSITION_TO_ELEMENT
      MAKE_CASE (SCSIOP_WRITE_VERIFY)      // Code 0x2E
      MAKE_CASE (SCSIOP_VERIFY)            // Code 0x2F
      MAKE_CASE (SCSIOP_SEARCH_DATA_HIGH)  // Code 0x30
      MAKE_CASE (SCSIOP_SEARCH_DATA_EQUAL) // Code 0x31
      MAKE_CASE (SCSIOP_SEARCH_DATA_LOW)   // Code 0x32
      MAKE_CASE (SCSIOP_SET_LIMITS)        // Code 0x33
      MAKE_CASE (SCSIOP_READ_POSITION)     // Code 0x34
      MAKE_CASE (SCSIOP_SYNCHRONIZE_CACHE) // Code 0x35
      MAKE_CASE (SCSIOP_COMPARE)           // Code 0x39
      MAKE_CASE (SCSIOP_COPY_COMPARE)      // Code 0x3A
      MAKE_CASE (SCSIOP_WRITE_DATA_BUFF)   // Code 0x3B
      MAKE_CASE (SCSIOP_READ_DATA_BUFF)    // Code 0x3C
      MAKE_CASE (SCSIOP_WRITE_LONG)        // Code 0x3F
      MAKE_CASE (SCSIOP_CHANGE_DEFINITION) // Code 0x40
      MAKE_CASE (SCSIOP_WRITE_SAME)        // Code 0x41
      MAKE_CASE (SCSIOP_UNMAP)    // Code 0x42, aka SCSIOP_READ_SUB_CHANNEL
      MAKE_CASE (SCSIOP_READ_TOC) // Code 0x43
      MAKE_CASE (
          SCSIOP_READ_HEADER) // Code 0x44, aka SCSIOP_REPORT_DENSITY_SUPPORT
      MAKE_CASE (SCSIOP_PLAY_AUDIO)        // Code 0x45
      MAKE_CASE (SCSIOP_GET_CONFIGURATION) // Code 0x46
      MAKE_CASE (SCSIOP_PLAY_AUDIO_MSF)    // Code 0x47
      MAKE_CASE (SCSIOP_SANITIZE) // Code 0x48, aka SCSIOP_PLAY_TRACK_INDEX
      MAKE_CASE (SCSIOP_PLAY_TRACK_RELATIVE)   // Code 0x49
      MAKE_CASE (SCSIOP_GET_EVENT_STATUS)      // Code 0x4A
      MAKE_CASE (SCSIOP_PAUSE_RESUME)          // Code 0x4B
      MAKE_CASE (SCSIOP_LOG_SELECT)            // Code 0x4C
      MAKE_CASE (SCSIOP_LOG_SENSE)             // Code 0x4D
      MAKE_CASE (SCSIOP_STOP_PLAY_SCAN)        // Code 0x4E
      MAKE_CASE (SCSIOP_XDWRITE)               // Code 0x50
      MAKE_CASE (SCSIOP_READ_DISC_INFORMATION) // Code 0x51, aka SCSIOP_XPWRITE
      MAKE_CASE (SCSIOP_READ_TRACK_INFORMATION) // Code 0x52
      MAKE_CASE (
          SCSIOP_XDWRITE_READ) // Code 0x53, aka SCSIOP_RESERVE_TRACK_RZONE
      MAKE_CASE (SCSIOP_SEND_OPC_INFORMATION) // Code 0x54
      MAKE_CASE (SCSIOP_MODE_SELECT10)        // Code 0x55
      MAKE_CASE (
          SCSIOP_RESERVE_ELEMENT) // Code 0x56, aka SCSIOP_RESERVE_UNIT10
      MAKE_CASE (
          SCSIOP_RELEASE_ELEMENT)     // Code 0x57, aka SCSIOP_RELEASE_UNIT10
      MAKE_CASE (SCSIOP_REPAIR_TRACK) // Code 0x58
      MAKE_CASE (SCSIOP_MODE_SENSE10) // Code 0x5A
      MAKE_CASE (SCSIOP_CLOSE_TRACK_SESSION)    // Code 0x5B
      MAKE_CASE (SCSIOP_READ_BUFFER_CAPACITY)   // Code 0x5C
      MAKE_CASE (SCSIOP_SEND_CUE_SHEET)         // Code 0x5D
      MAKE_CASE (SCSIOP_PERSISTENT_RESERVE_IN)  // Code 0x5E
      MAKE_CASE (SCSIOP_PERSISTENT_RESERVE_OUT) // Code 0x5F
      MAKE_CASE (SCSIOP_OPERATION32)            // Code 0x7F
      MAKE_CASE (
          SCSIOP_XDWRITE_EXTENDED16) // Code 0x80, aka SCSIOP_WRITE_FILEMARKS16
      MAKE_CASE (SCSIOP_REBUILD16)   // Code 0x81, aka SCSIOP_READ_REVERSE16
      MAKE_CASE (SCSIOP_REGENERATE16) // Code 0x82
      MAKE_CASE (
          SCSIOP_WRITE_USING_TOKEN) // Code 0x83, aka SCSIOP_EXTENDED_COPY,
                                    // SCSIOP_POPULATE_TOKEN
      MAKE_CASE (
          SCSIOP_RECEIVE_ROD_TOKEN_INFORMATION) // Code 0x84, aka
                                                // SCSIOP_RECEIVE_COPY_RESULTS
      MAKE_CASE (SCSIOP_ATA_PASSTHROUGH16)      // Code 0x85
      MAKE_CASE (SCSIOP_ACCESS_CONTROL_IN)      // Code 0x86
      MAKE_CASE (SCSIOP_ACCESS_CONTROL_OUT)     // Code 0x87
      MAKE_CASE (SCSIOP_READ16)                 // Code 0x88
      MAKE_CASE (SCSIOP_COMPARE_AND_WRITE)      // Code 0x89
      MAKE_CASE (SCSIOP_WRITE16)                // Code 0x8A
      MAKE_CASE (SCSIOP_READ_ATTRIBUTES)        // Code 0x8C
      MAKE_CASE (SCSIOP_WRITE_ATTRIBUTES)       // Code 0x8D
      MAKE_CASE (SCSIOP_WRITE_VERIFY16)         // Code 0x8E
      MAKE_CASE (SCSIOP_VERIFY16)               // Code 0x8F
      MAKE_CASE (SCSIOP_PREFETCH16)             // Code 0x90
      MAKE_CASE (SCSIOP_SYNCHRONIZE_CACHE16) // Code 0x91, aka SCSIOP_SPACE16
      MAKE_CASE (SCSIOP_LOCK_UNLOCK_CACHE16) // Code 0x92, aka SCSIOP_LOCATE16
      MAKE_CASE (SCSIOP_WRITE_SAME16)        // Code 0x93, aka SCSIOP_ERASE16
      MAKE_CASE (SCSIOP_ZBC_OUT)             // Code 0x94
      MAKE_CASE (SCSIOP_ZBC_IN)              // Code 0x95
      MAKE_CASE (SCSIOP_READ_DATA_BUFF16)    // Code 0x9B
      MAKE_CASE (SCSIOP_GET_LBA_STATUS)      // Code 0x9E, aka
                                        // SCSIOP_GET_PHYSICAL_ELEMENT_STATUS,
                                        // SCSIOP_READ_CAPACITY16,
                                        // SCSIOP_REMOVE_ELEMENT_AND_TRUNCATE,
                                        // SCSIOP_SERVICE_ACTION_IN16
      MAKE_CASE (SCSIOP_SERVICE_ACTION_OUT16) // Code 0x9F
      MAKE_CASE (SCSIOP_REPORT_LUNS)          // Code 0xA0
      MAKE_CASE (SCSIOP_BLANK)      // Code 0xA1, aka SCSIOP_ATA_PASSTHROUGH12
      MAKE_CASE (SCSIOP_SEND_EVENT) // Code 0xA2
      MAKE_CASE (SCSIOP_MAINTENANCE_IN)   // Code 0xA3, aka SCSIOP_SEND_KEY
      MAKE_CASE (SCSIOP_MAINTENANCE_OUT)  // Code 0xA4, aka SCSIOP_REPORT_KEY
      MAKE_CASE (SCSIOP_MOVE_MEDIUM)      // Code 0xA5
      MAKE_CASE (SCSIOP_LOAD_UNLOAD_SLOT) // Code 0xA6
      MAKE_CASE (SCSIOP_SET_READ_AHEAD)   // Code 0xA7
      MAKE_CASE (SCSIOP_READ12)           // Code 0xA8
      MAKE_CASE (SCSIOP_SERVICE_ACTION_OUT12)         // Code 0xA9
      MAKE_CASE (SCSIOP_WRITE12)                      // Code 0xAA
      MAKE_CASE (SCSIOP_SEND_MESSAGE)                 // Code 0xAB
      MAKE_CASE (SCSIOP_GET_PERFORMANCE)              // Code 0xAC
      MAKE_CASE (SCSIOP_READ_DVD_STRUCTURE)           // Code 0xAD
      MAKE_CASE (SCSIOP_WRITE_VERIFY12)               // Code 0xAE
      MAKE_CASE (SCSIOP_VERIFY12)                     // Code 0xAF
      MAKE_CASE (SCSIOP_SEARCH_DATA_HIGH12)           // Code 0xB0
      MAKE_CASE (SCSIOP_SEARCH_DATA_EQUAL12)          // Code 0xB1
      MAKE_CASE (SCSIOP_SEARCH_DATA_LOW12)            // Code 0xB2
      MAKE_CASE (SCSIOP_SET_LIMITS12)                 // Code 0xB3
      MAKE_CASE (SCSIOP_READ_ELEMENT_STATUS_ATTACHED) // Code 0xB4
      MAKE_CASE (SCSIOP_REQUEST_VOL_ELEMENT)          // Code 0xB5, aka
                                             // SCSIOP_SECURITY_PROTOCOL_OUT
      MAKE_CASE (SCSIOP_SEND_VOLUME_TAG)      // Code 0xB6
      MAKE_CASE (SCSIOP_READ_DEFECT_DATA)     // Code 0xB7
      MAKE_CASE (SCSIOP_READ_ELEMENT_STATUS)  // Code 0xB8
      MAKE_CASE (SCSIOP_READ_CD_MSF)          // Code 0xB9
      MAKE_CASE (SCSIOP_REDUNDANCY_GROUP_IN)  // Code 0xBA
      MAKE_CASE (SCSIOP_REDUNDANCY_GROUP_OUT) // Code 0xBB
      MAKE_CASE (SCSIOP_SPARE_IN)             // Code 0xBC
      MAKE_CASE (SCSIOP_SPARE_OUT) // Code 0xBD, aka SCSIOP_MECHANISM_STATUS
      MAKE_CASE (SCSIOP_VOLUME_SET_IN)      // Code 0xBE
      MAKE_CASE (SCSIOP_VOLUME_SET_OUT)     // Code 0xBF
      MAKE_CASE (SCSIOP_INIT_ELEMENT_RANGE) // Code 0xE7
    }
  return scsiOpStr;
}
