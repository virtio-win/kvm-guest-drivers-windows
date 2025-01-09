/*
 * This file contains common definitions of netkvm driver and user-mode
 * components related to VIOPROT operation (SRIOV failover implementation)
 *
 * Copyright (c) 2023 Red Hat, Inc.
 *
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

#pragma once

#define NETKVM_DEVICE_NAME "NETKVMD"

typedef struct _NETKVMD_ADAPTER
{
    UCHAR MacAddress[6];
    UCHAR Virtio;
    UCHAR IsStandby;

    UCHAR VirtioLink;
    UCHAR SuppressLink;
    UCHAR Started;
    UCHAR HasVf;
    ULONG VfIfIndex;

    UCHAR VfLink;
    UCHAR Reserved[47];
} NETKVMD_ADAPTER;

// input buffer = none, output buffer = NETKVMD_ADAPTER[] (variable length)
#define IOCTL_NETKVMD_QUERY_ADAPTERS CTL_CODE(FILE_DEVICE_NETWORK, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)

typedef struct _NETKVMD_SET_LINK
{
    UCHAR MacAddress[6];
    UCHAR LinkOn;
} NETKVMD_SET_LINK;

// input buffer = NETKVMD_SET_LINK
#define IOCTL_NETKVMD_SET_LINK CTL_CODE(FILE_DEVICE_NETWORK, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)
