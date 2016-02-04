/*
 * Copyright (C) 2014-2016 Red Hat, Inc.
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
