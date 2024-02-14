/*
 * Copyright (C) 2014-2017 Red Hat, Inc.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
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

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (status >= 0)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS              ((NTSTATUS)0x00000000L)
#define STATUS_UNSUCCESSFUL         ((NTSTATUS)0xC0000001L)
#define STATUS_NOT_IMPLEMENTED      ((NTSTATUS)0xC0000002L)
#define STATUS_NOT_SUPPORTED        ((NTSTATUS)0xC00000BBL)
#define STATUS_PORT_UNREACHABLE     ((NTSTATUS)0xC000023FL)
#endif

#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER    ((NTSTATUS)0xC000000DL)
#endif

// CNG RNG Provider Interface.

NTSTATUS WINAPI VirtRngOpenAlgorithmProvider(OUT BCRYPT_ALG_HANDLE *Algorithm,
    IN LPCWSTR AlgId, IN ULONG Flags);

NTSTATUS WINAPI VirtRngGetProperty(IN BCRYPT_HANDLE Object,
    IN LPCWSTR Property, OUT PUCHAR Output, IN ULONG Length,
    OUT ULONG *Result, IN ULONG Flags);

NTSTATUS WINAPI VirtRngSetProperty(IN OUT BCRYPT_HANDLE Object,
    IN LPCWSTR Property, IN PUCHAR Input, IN ULONG Length, IN ULONG Flags);

NTSTATUS WINAPI VirtRngCloseAlgorithmProvider(
    IN OUT BCRYPT_ALG_HANDLE Algorithm, IN ULONG Flags);

NTSTATUS WINAPI VirtRngGenRandom(IN OUT BCRYPT_ALG_HANDLE Algorithm,
    IN OUT PUCHAR Buffer, IN ULONG Length, IN ULONG Flags);
