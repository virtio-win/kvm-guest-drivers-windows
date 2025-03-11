/*
 * Include file for the SRB status table
 *
 * Copyright (c) 2025 Virtuozzo
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

#ifndef __VIRTIO_STATUS_TABLE_H__
#define __VIRTIO_STATUS_TABLE_H__

#include <ntddk.h>

typedef struct _STATUS_TABLE_ENTRY
{
    unsigned char Status;
    ULONG64 Id;
    int Present : 1;
    int Deleted : 1;
} STATUS_TABLE_ENTRY, *PSTATUS_TABLE_ENTRY;

#define VIRTIO_STATUS_TABLE_SIZE 769

typedef struct _STATUS_TABLE
{
    STATUS_TABLE_ENTRY Entries[VIRTIO_STATUS_TABLE_SIZE];
    volatile LONG Lock;
} STATUS_TABLE, *PSTATUS_TABLE;

void StatusTableInit(PSTATUS_TABLE Table);
unsigned char *StatusTableInsert(PSTATUS_TABLE Table, ULONG64 Id);
void StatusTableDelete(PSTATUS_TABLE Table, ULONG64 Id, unsigned char *Status);

#endif
