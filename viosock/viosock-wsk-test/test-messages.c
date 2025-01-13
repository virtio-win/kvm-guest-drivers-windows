/*
 * Copyright (c) 2023 Virtuozzo International GmbH
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

#include <ntifs.h>
#include <wsk.h>
#include <bcrypt.h>
#include "..\inc\debug-utils.h"
#include "test-messages.h"
#ifdef EVENT_TRACING
#include "test-messages.tmh"
#endif

static ULONG _randSeed;
static ULONG _hashObjectSize;
static BCRYPT_ALG_HANDLE _hashProvider;

NTSTATUS
VioWskMessageGenerate(_Inout_opt_ PVIOWSK_MSG_HASH_OBJECT HashObject, _Out_ PWSK_BUF WskBuffer, _Out_ PVOID *FlatBuffer)
{
    PMDL mdl = NULL;
    PVOID buffer = NULL;
    SIZE_T bufferSize = 0;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("HashObject=0x%p; WskBuffer=0x%p", HashObject, WskBuffer);

    Status = STATUS_SUCCESS;
    bufferSize = VIOWSK_MSG_SIZE;
    buffer = ExAllocatePoolUninitialized(NonPagedPool, bufferSize, VIOWSK_TEST_MSG_TAG);
    if (!buffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    if (HashObject)
    {
        Status = VioWskMessageRefreshHashObject(HashObject);
        if (!NT_SUCCESS(Status))
        {
            goto FreeBuffer;
        }

        for (SIZE_T i = 0; i < (VIOWSK_MSG_SIZE - 32) / sizeof(ULONG); ++i)
        {
            ((PULONG)buffer)[i] = RtlRandomEx(&_randSeed);
        }

        Status = BCryptHashData(HashObject->HashHandle, (PUCHAR)buffer, VIOWSK_MSG_SIZE - 32, 0);
        if (!NT_SUCCESS(Status))
        {
            goto FreeBuffer;
        }

        Status = BCryptFinishHash(HashObject->HashHandle, (PUCHAR)buffer + VIOWSK_MSG_SIZE - 32, 32, 0);
        if (!NT_SUCCESS(Status))
        {
            goto FreeBuffer;
        }
    }

    WskBuffer->Length = VIOWSK_MSG_SIZE;
    WskBuffer->Offset = 0;
    mdl = IoAllocateMdl(buffer, VIOWSK_MSG_SIZE, FALSE, FALSE, NULL);
    if (!mdl)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto FreeBuffer;
    }

    MmBuildMdlForNonPagedPool(mdl);
    WskBuffer->Mdl = mdl;
    *FlatBuffer = buffer;
    mdl = NULL;
    buffer = NULL;

FreeBuffer:
    if (buffer)
    {
        ExFreePoolWithTag(buffer, VIOWSK_TEST_MSG_TAG);
    }
Exit:
    DEBUG_EXIT_FUNCTION("0x%x, *FlatBuffer=0x%p", Status, *FlatBuffer);
    return Status;
}

NTSTATUS
VIoWskMessageVerify(_In_ PVIOWSK_MSG_HASH_OBJECT HashObject, _In_ const WSK_BUF *WskBuf, _Out_ PBOOLEAN Verified)
{
    ULONG offset = 0;
    unsigned char digest[32];
    unsigned char original[sizeof(digest)];
    SIZE_T remainingLength = 0;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("HashObject=0x%p; WskBuffer=0x%p; Verified=0x%p", HashObject, WskBuf, Verified);

    Status = VioWskMessageRefreshHashObject(HashObject);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    offset = WskBuf->Offset;
    remainingLength = WskBuf->Length - sizeof(digest);
    for (PMDL mdl = WskBuf->Mdl; mdl != NULL; mdl = mdl->Next)
    {
        ULONG mdlSize = 0;
        ULONG bytesToCopy = 0;
        PVOID mdlBuffer = NULL;

        mdlBuffer = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority);
        if (!mdlBuffer)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        mdlSize = MmGetMdlByteCount(mdl) - offset;
        bytesToCopy = mdlSize;
        if (bytesToCopy > remainingLength)
        {
            bytesToCopy = (ULONG)remainingLength;
        }

        Status = BCryptHashData(HashObject->HashHandle, (unsigned char *)mdlBuffer + offset, bytesToCopy, 0);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        offset = 0;
        remainingLength -= bytesToCopy;
        if (remainingLength == 0)
        {
            if (mdlSize > bytesToCopy + sizeof(digest))
            {
                mdlSize = bytesToCopy + sizeof(digest);
            }

            remainingLength = sizeof(original) - (mdlSize - bytesToCopy);
            memcpy(original, (unsigned char *)mdlBuffer + bytesToCopy, mdlSize - bytesToCopy);
            mdl = mdl->Next;
            while (remainingLength > 0)
            {
                mdl = mdl->Next;
                mdlBuffer = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority);
                if (!mdlBuffer)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                mdlSize = MmGetMdlByteCount(mdl) - offset;
                if (mdlSize > remainingLength)
                {
                    mdlSize = (ULONG)remainingLength;
                }

                memcpy(original + sizeof(original) - remainingLength, mdlBuffer, mdlSize);
                remainingLength -= mdlSize;
            }

            break;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    Status = BCryptFinishHash(HashObject->HashHandle, digest, sizeof(digest), 0);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    *Verified = (memcmp(original, digest, sizeof(digest)) == 0);
Exit:
    DEBUG_EXIT_FUNCTION("0x%x, *Verified=%u", Status, *Verified);
    return Status;
}

NTSTATUS
VIoWskMessageVerifyBuffer(_In_ PVIOWSK_MSG_HASH_OBJECT HashObject, _In_ const void *Buffer, _Out_ PBOOLEAN Verified)
{
    unsigned char digest[32];
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("HashObject=0x%p; Buffer=0x%p; Verified=0x%p", HashObject, Buffer, Verified);

    Status = VioWskMessageRefreshHashObject(HashObject);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    Status = BCryptHashData(HashObject->HashHandle, (PUCHAR)Buffer, VIOWSK_MSG_SIZE - sizeof(digest), 0);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    Status = BCryptFinishHash(HashObject->HashHandle, digest, sizeof(digest), 0);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    *Verified = (memcmp((unsigned char *)Buffer + VIOWSK_MSG_SIZE - sizeof(digest), digest, sizeof(digest)) == 0);

Exit:
    DEBUG_EXIT_FUNCTION("0x%x, *Verified=%u", Status, *Verified);
    return Status;
}

void VioWskMessageAdvance(_Inout_ PWSK_BUF WskBuffer, _In_ SIZE_T Length)
{
    PMDL currentMdl = NULL;
    ULONG advanceAmount = 0;
    DEBUG_ENTER_FUNCTION("WskBuffer=0x%p; Length=%Iu", WskBuffer, Length);

    currentMdl = WskBuffer->Mdl;
    while (WskBuffer->Length > 0 && Length > 0)
    {
        advanceAmount = MmGetMdlByteCount(WskBuffer->Mdl) - WskBuffer->Offset;
        if (advanceAmount > Length)
        {
            advanceAmount = (ULONG)Length;
        }

        WskBuffer->Offset += advanceAmount;
        if (WskBuffer->Offset == MmGetMdlByteCount(currentMdl))
        {
            WskBuffer->Mdl = WskBuffer->Mdl->Next;
            WskBuffer->Offset = 0;
            IoFreeMdl(currentMdl);
            currentMdl = WskBuffer->Mdl;
        }

        WskBuffer->Length -= advanceAmount;
        Length -= advanceAmount;
    }

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

void VioWskMessageFree(_In_ PWSK_BUF WskBuffer, _In_opt_ PVOID FlatBuffer)
{
    PMDL mdl = NULL;
    DEBUG_ENTER_FUNCTION("WskBuffer=0x%p; FlatBuffer=0x%p", WskBuffer, FlatBuffer);

    mdl = WskBuffer->Mdl;
    while (mdl != NULL)
    {
        mdl = mdl->Next;
        IoFreeMdl(WskBuffer->Mdl);
        WskBuffer->Mdl = mdl;
    }

    if (FlatBuffer)
    {
        ExFreePoolWithTag(FlatBuffer, VIOWSK_TEST_MSG_TAG);
    }

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

NTSTATUS
VioWskMessageCreateHashObject(_Out_ PVIOWSK_MSG_HASH_OBJECT Object)
{
    PVOID ho = NULL;
    BCRYPT_HASH_HANDLE hh = NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("Object=0x%p", Object);

    ho = ExAllocatePoolUninitialized(NonPagedPool, _hashObjectSize, VIOWSK_TEST_MSG_TAG);
    if (!ho)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Status = BCryptCreateHash(_hashProvider, &hh, ho, _hashObjectSize, NULL, 0, 0);
    if (!NT_SUCCESS(Status))
    {
        DEBUG_ERROR("BCryptCreateHash: 0x%x", Status);
        goto FreeRecvMessage;
    }

    Object->HashObject = ho;
    Object->HashHandle = hh;
    ho = NULL;
    hh = NULL;
FreeRecvMessage:
    if (ho)
    {
        ExFreePoolWithTag(ho, VIOWSK_TEST_MSG_TAG);
    }
Exit:
    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}

NTSTATUS
VioWskMessageRefreshHashObject(_Inout_ PVIOWSK_MSG_HASH_OBJECT Object)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION("Object=0x%p", Object);

    if (Object->HashHandle)
    {
        Status = BCryptDestroyHash(Object->HashHandle);
        if (!NT_SUCCESS(Status))
        {
            __debugbreak();
        }

        Object->HashHandle = NULL;
    }

    Status = BCryptCreateHash(_hashProvider, &Object->HashHandle, Object->HashObject, _hashObjectSize, NULL, 0, 0);

    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}

void VIoWskMessageDestroyHashObject(_In_ PVIOWSK_MSG_HASH_OBJECT Object)
{
    DEBUG_ENTER_FUNCTION("Object=0x%p", Object);

    if (Object->HashHandle)
    {
        NTSTATUS Status = STATUS_UNSUCCESSFUL;

        Status = BCryptDestroyHash(Object->HashHandle);
        if (!NT_SUCCESS(Status))
        {
            __debugbreak();
        }
    }

    ExFreePoolWithTag(Object->HashObject, VIOWSK_TEST_MSG_TAG);

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}

NTSTATUS
VioWskMessageModuleInit()
{
    ULONG returnedLength = 0;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    DEBUG_ENTER_FUNCTION_NO_ARGS();

    Status = BCryptOpenAlgorithmProvider(&_hashProvider, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_PROV_DISPATCH);
    if (!NT_SUCCESS(Status))
    {
        DEBUG_ERROR("Unable to open SHA256 provider: 0x%x", Status);
        goto Exit;
    }

    Status = BCryptGetProperty(_hashProvider,
                               BCRYPT_OBJECT_LENGTH,
                               (PUCHAR)&_hashObjectSize,
                               sizeof(_hashObjectSize),
                               &returnedLength,
                               0);
    if (!NT_SUCCESS(Status))
    {
        DEBUG_ERROR("BCryptGetProperty: 0x%x", Status);
        goto CloseProvider;
    }

CloseProvider:
    if (!NT_SUCCESS(Status))
    {
        BCryptCloseAlgorithmProvider(_hashProvider, 0);
    }
Exit:
    DEBUG_EXIT_FUNCTION("0x%x", Status);
    return Status;
}

void VioWskMessageModuleFinit()
{
    DEBUG_ENTER_FUNCTION_NO_ARGS();

    BCryptCloseAlgorithmProvider(_hashProvider, 0);

    DEBUG_EXIT_FUNCTION_VOID();
    return;
}
