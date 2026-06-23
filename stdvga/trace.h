/*
 * Copyright (c) 2026 Alibaba Cloud Computing Ltd.
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

//
// Shared debug tracing utilities for the stdvga driver.
// In DBG builds, appends trace messages to \SystemRoot\stdvga_trace.log.
// In release builds, all functions compile to no-ops.
//

static __inline VOID TraceLog(_In_ const char *msg)
{
#if DBG
    UNICODE_STRING fileName;
    RtlInitUnicodeString(&fileName, L"\\SystemRoot\\stdvga_trace.log");

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &fileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    IO_STATUS_BLOCK iosb;
    HANDLE hFile = NULL;
    NTSTATUS st = ZwCreateFile(&hFile,
                               FILE_APPEND_DATA | SYNCHRONIZE,
                               &oa,
                               &iosb,
                               NULL,
                               FILE_ATTRIBUTE_NORMAL,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               FILE_OPEN_IF,
                               FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                               NULL,
                               0);

    if (NT_SUCCESS(st))
    {
        SIZE_T len = strlen(msg);
        ZwWriteFile(hFile, NULL, NULL, NULL, &iosb, (PVOID)msg, (ULONG)len, NULL, NULL);
        char nl = '\n';
        ZwWriteFile(hFile, NULL, NULL, NULL, &iosb, &nl, 1, NULL, NULL);
        ZwClose(hFile);
    }
#else
    UNREFERENCED_PARAMETER(msg);
#endif
}

static __inline VOID TraceLogStatus(_In_ const char *prefix, _In_ NTSTATUS status)
{
#if DBG
    char buf[128];
    RtlStringCchPrintfA(buf, sizeof(buf), "%s 0x%08X", prefix, (ULONG)status);
    TraceLog(buf);
#else
    UNREFERENCED_PARAMETER(prefix);
    UNREFERENCED_PARAMETER(status);
#endif
}

static __inline VOID TraceLogInt(_In_ const char *prefix, _In_ int value)
{
#if DBG
    char buf[128];
    RtlStringCchPrintfA(buf, sizeof(buf), "%s=0x%08X", prefix, value);
    TraceLog(buf);
#else
    UNREFERENCED_PARAMETER(prefix);
    UNREFERENCED_PARAMETER(value);
#endif
}
