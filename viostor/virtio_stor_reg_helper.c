/*
 * This file contains Windows registry helper functions for virtio.
 *
 * Copyright (c) 2008-2026 Red Hat, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "virtio_stor_reg_helper.h"

#if defined(EVENT_TRACING)
#include "virtio_stor_reg_helper.tmh"
#endif

BOOLEAN VioStorReadRegistryParameter(PVOID DeviceExtension, PUCHAR ValueName, LONG offset)
{
    BOOLEAN bReadResult = FALSE;
    BOOLEAN bUseAltPerHbaRegRead = FALSE;
    ULONG pBufferLength = sizeof(ULONG);
    UCHAR *pBuffer = NULL;
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    ULONG spgspn_rc, i, j;
    STOR_ADDRESS HwAddress = {0};
    PSTOR_ADDRESS pHwAddress = &HwAddress;
    CHAR valname_as_str[64] = {0};
    CHAR hba_id_as_str[4] = {0};
    USHORT shAdapterId = (USHORT)adaptExt->slot_number - 1;
    ULONG value_as_ulong;

    /* Get a clean buffer to store the registry value... */
    pBuffer = StorPortAllocateRegistryBuffer(DeviceExtension, &pBufferLength);
    if (pBuffer == NULL)
    {
        RhelDbgPrint(TRACE_LEVEL_WARNING, " StorPortAllocateRegistryBuffer failed to allocate buffer\n");
        return FALSE;
    }
    RtlZeroMemory(pBuffer, sizeof(ULONG));

    /* Check if we can get a System PortNumber to access the \Parameters\Device(d) subkey to get a per HBA value.
     * FIXME NOTE
     *
     * Regarding StorPortGetSystemPortNumber():
     *
     * StorPort always reports STOR_STATUS_INVALID_DEVICE_STATE and does not update pHwAddress->Port.
     * Calls to StorPortRegistryRead() and StorPortRegistryWrite() only read or write to \Parameters\Device-1,
     * which appears to be an uninitialized value. Therefore, the alternate per HBA read technique will always be used.
     *
     * Please refer to PR #1216 for more details.
     *
     * FIXME NOTE END
     */
    pHwAddress->Type = STOR_ADDRESS_TYPE_BTL8;
    pHwAddress->AddressLength = STOR_ADDR_BTL8_ADDRESS_LENGTH;
    RhelDbgPrint(TRACE_REGISTRY,
                 " Checking whether the HBA system port number and HBA specific registry are available for reading... "
                 "\n");
    spgspn_rc = StorPortGetSystemPortNumber(DeviceExtension, pHwAddress);
    if (spgspn_rc == STOR_STATUS_INVALID_DEVICE_STATE)
    {
        RhelDbgPrint(TRACE_REGISTRY,
                     " WARNING : !!!...HBA Port not ready yet...!!! Returns : 0x%x (STOR_STATUS_INVALID_DEVICE_STATE) "
                     "\n",
                     spgspn_rc);
        /*
         * When we are unable to get a valid system PortNumber, we need to
         * use an alternate per HBA registry read technique. The technique
         * implemented here uses per HBA registry value names based on the
         * Storport provided slot_number minus one, padded to hundreds,
         * e.g. \Parameters\Device\Valuename_123.
         *
         * This permits up to 999 HBAs. That ought to be enough... c( O.O )ɔ
         */
        bUseAltPerHbaRegRead = TRUE;
        RhelDbgPrint(TRACE_REGISTRY,
                     " Using alternate per HBA registry read technique [\\Parameters\\Device\\Value_(ddd)]. \n");

        /* Grab the first 60 characters of the target Registry Value.
         * Value name limit is 16,383 characters, so this is important.
         * We leave the last 4 characters for the hba_id_as_str values.
         * NULL terminator wraps things up. Also used in TRACING.
         */
        CopyBufferToAnsiString(&valname_as_str, ValueName, '\0', 60);
        CopyBufferToAnsiString(&hba_id_as_str, &shAdapterId, '\0', 4);

        /* Convert from integer to padded ASCII numbers. */
        if (shAdapterId / 100)
        {
            j = 0;
            hba_id_as_str[j] = (UCHAR)(shAdapterId / 100) + 48;
        }
        else
        {
            hba_id_as_str[0] = 48;
            if (shAdapterId / 10)
            {
                j = 1;
                hba_id_as_str[j] = (UCHAR)(shAdapterId / 10) + 48;
            }
            else
            {
                hba_id_as_str[1] = 48;
                j = 2;
                hba_id_as_str[j] = (UCHAR)shAdapterId + 48;
            }
        }
        if ((j < 1) && (shAdapterId / 10))
        {
            j = 1;
            hba_id_as_str[j] = (UCHAR)(((shAdapterId - ((shAdapterId / 100) * 100)) / 10) + 48);
        }
        else if ((j < 2) && (shAdapterId > 9))
        {
            j = 2;
            hba_id_as_str[j] = (UCHAR)((shAdapterId - ((shAdapterId / 10) * 10)) + 48);
        }
        else
        {
            j = 1;
            hba_id_as_str[j] = 48;
        }
        if ((j < 2) && (shAdapterId > 0))
        {
            j = 2;
            hba_id_as_str[j] = (UCHAR)((shAdapterId - ((shAdapterId / 10) * 10)) + 48);
        }
        else if (j < 2)
        {
            j = 2;
            hba_id_as_str[j] = 48;
        }
        /* NULL-terminate the string. */
        hba_id_as_str[3] = '\0';
        /* Skip the exisitng ValueName. */
        for (i = 0; valname_as_str[i] != '\0'; ++i)
        {
        }
        /* Append an underscore. */
        valname_as_str[i] = '\x5F';
        /* Append the padded HBA ID and NULL terminator. */
        for (j = 0; j < 4; ++j)
        {
            valname_as_str[i + j + 1] = hba_id_as_str[j];
        }

        PUCHAR ValueNamePerHba = (UCHAR *)&valname_as_str;
        bReadResult = StorPortRegistryRead(DeviceExtension,
                                           ValueNamePerHba,
                                           1,
                                           MINIPORT_REG_DWORD,
                                           pBuffer,
                                           &pBufferLength);
    }
    else
    {
        RhelDbgPrint(TRACE_REGISTRY, " HBA Port : %u | Returns : 0x%x \n", pHwAddress->Port, spgspn_rc);
        RhelDbgPrint(TRACE_REGISTRY, " Using StorPort-based per HBA registry read [\\Parameters\\Device(d)]. \n");
        /* FIXME : THIS DOES NOT WORK. IT WILL NOT READ \Parameters\Device(d) subkeys...
         * NOTE  : Only MINIPORT_REG_DWORD values are supported.
         */
        bReadResult = StorPortRegistryRead(DeviceExtension, ValueName, 0, MINIPORT_REG_DWORD, pBuffer, &pBufferLength);
        /* Grab the first 64 characters of the target Registry Value.
         * Value name limit is 16,383 characters, so this is important.
         * NULL terminator wraps things up. Used in TRACING.
         */
        CopyBufferToAnsiString(&valname_as_str, ValueName, '\0', 64);
    }

    if ((bReadResult == FALSE) || (pBufferLength == 0))
    {
        RhelDbgPrint(TRACE_REGISTRY,
                     " StorPortRegistryRead was unable to find a per HBA value %s. Attempting to find a global "
                     "value... \n",
                     (bUseAltPerHbaRegRead) ? "using \\Parameters\\Device\\Value_(ddd) value names"
                                            : "at the \\Parameters\\Device(d) subkey");
        bReadResult = FALSE;
        pBufferLength = sizeof(ULONG);
        RtlZeroMemory(pBuffer, sizeof(ULONG));

        /* Do a "Global" read of the Parameters\Device subkey...
         * NOTE : Only MINIPORT_REG_DWORD values are supported.
         */
        bReadResult = StorPortRegistryRead(DeviceExtension, ValueName, 1, MINIPORT_REG_DWORD, pBuffer, &pBufferLength);
        /* Grab the first 64 characters of the target Registry Value.
         * Value name limit is 16,383 characters, so this is important.
         * NULL terminator wraps things up. Used in TRACING.
         */
        CopyBufferToAnsiString(&valname_as_str, ValueName, '\0', 64);
    }
    /* Give me the DWORD Registry Value as a ULONG from the pointer.
     * Used in TRACING.
     */
    memcpy(&value_as_ulong, pBuffer, sizeof(ULONG));

    if ((bReadResult == FALSE) || (pBufferLength == 0))
    {
        RhelDbgPrint(TRACE_REGISTRY,
                     " StorPortRegistryRead of %s returned NOT FOUND or EMPTY, pBufferLength = %d, Possible "
                     "pBufferLength Hint = 0x%x (%lu) \n",
                     valname_as_str,
                     pBufferLength,
                     value_as_ulong,
                     value_as_ulong);
        StorPortFreeRegistryBuffer(DeviceExtension, pBuffer);
        return FALSE;
    }
    else
    {
        RhelDbgPrint(TRACE_REGISTRY,
                     " StorPortRegistryRead of %s returned SUCCESS, pBufferLength = %d, Value = 0x%x (%lu) \n",
                     valname_as_str,
                     pBufferLength,
                     value_as_ulong,
                     value_as_ulong);

        StorPortCopyMemory((PVOID)((UINT_PTR)adaptExt + offset), (PVOID)pBuffer, sizeof(ULONG));

        StorPortFreeRegistryBuffer(DeviceExtension, pBuffer);

        return TRUE;
    }
}

USHORT CopyBufferToAnsiString(void *_pDest, const void *_pSrc, const char delimiter, size_t _maxlength)
{
    PCHAR dst = (PCHAR)_pDest;
    PCHAR src = (PCHAR)_pSrc;
    USHORT _length = _maxlength;

    while (_length && (*src != delimiter))
    {
        *dst++ = *src++;
        --_length;
    };
    *dst = '\0';
    return _length;
}
