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
 * 3. Neither the names of the copyright holders nor the names of their
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission. THIS SOFTWARE IS PROVIDED
 * BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __VIOWSK_TEST_MESSAGES_H__
#define __VIOWSK_TEST_MESSAGES_H__

#include <bcrypt.h>
#include <ntifs.h>
#include <wsk.h>

#define VIOWSK_TEST_MSG_TAG (ULONG)'MKSW'

#define VIOWSK_MSG_SIZE 64

typedef struct _VIOWSK_MSG_HASH_OBJECT {
  PVOID HashObject;
  BCRYPT_HASH_HANDLE HashHandle;
} VIOWSK_MSG_HASH_OBJECT, *PVIOWSK_MSG_HASH_OBJECT;

NTSTATUS
VioWskMessageGenerate(_Inout_opt_ PVIOWSK_MSG_HASH_OBJECT HashObject,
                      _Out_ PWSK_BUF WskBuffer, _Out_ PVOID *FlatBuffer);

NTSTATUS
VIoWskMessageVerify(_In_ PVIOWSK_MSG_HASH_OBJECT HashObject,
                    _In_ const WSK_BUF *WskBuf, _Out_ PBOOLEAN Verified);

NTSTATUS
VIoWskMessageVerifyBuffer(_In_ PVIOWSK_MSG_HASH_OBJECT HashObject,
                          _In_ const void *Buffer, _Out_ PBOOLEAN Verified);

void VioWskMessageAdvance(_Inout_ PWSK_BUF WskBuffer, _In_ SIZE_T Length);

void VioWskMessageFree(_In_ PWSK_BUF WskBuffer, _In_opt_ PVOID FlatBuffer);

NTSTATUS
VioWskMessageCreateHashObject(_Out_ PVIOWSK_MSG_HASH_OBJECT Object);

NTSTATUS
VioWskMessageRefreshHashObject(_Inout_ PVIOWSK_MSG_HASH_OBJECT Object);

void VIoWskMessageDestroyHashObject(_In_ PVIOWSK_MSG_HASH_OBJECT Object);

NTSTATUS
VioWskMessageModuleInit();

void VioWskMessageModuleFinit();

#endif
