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

#ifndef __VIOWSK_TEST_MESSAGES_H__
#define __VIOWSK_TEST_MESSAGES_H__


#include <ntifs.h>
#include <wsk.h>
#include <bcrypt.h>



#define VIOWSK_TEST_MSG_TAG					(ULONG)'MKSW'

#define VIOWSK_MSG_SIZE						64

NTSTATUS
VioWskMessageGenerate(
	_In_opt_ BCRYPT_HASH_HANDLE SHA256Handle,
	_Out_ PWSK_BUF WskBuffer,
	_Out_ PVOID* FlatBuffer
);

NTSTATUS
VIoWskMessageVerify(
	_In_ BCRYPT_HASH_HANDLE SHA256Handle,
	_In_ const WSK_BUF* WskBuf,
	_Out_ PBOOLEAN Verified
);

NTSTATUS
VIoWskMessageVerifyBuffer(
	_In_ BCRYPT_HASH_HANDLE SHA256Handle,
	_In_ const void* Buffer,
	_Out_ PBOOLEAN Verified
);

void
VioWskMessageAdvance(
	_Inout_ PWSK_BUF WskBuffer,
	_In_ SIZE_T Length
);

void
VioWskMessageFree(
	_In_ PWSK_BUF WskBuffer,
	_In_opt_ PVOID FlatBuffer
);




#endif
