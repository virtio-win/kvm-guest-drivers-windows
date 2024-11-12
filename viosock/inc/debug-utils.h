/*
 * Exports definition for virtio socket WSK interface
 *
 * Copyright (c) 2021 Virtuozzo International GmbH
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

#ifndef __DEBUG_UTILS_H__
#define __DEBUG_UTILS_H__

#include "..\..\virtio\kdebugprint.h"
#include "wpp-trace.h"
#include <Evntrace.h>

#ifndef EVENT_TRACING

#if defined(DBG) || defined(_DEBUG)

#define DEBUG_ENTER_FUNCTION(Format, ...)                                      \
  DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,                            \
             "[0x%x:0x%x:%u]: " __FUNCTION__ "(" Format ")\n",                 \
             PtrToUlong(PsGetCurrentProcessId()),                              \
             PtrToUlong(PsGetCurrentThreadId()), KeGetCurrentIrql(),           \
             __VA_ARGS__)

#define DEBUG_ENTER_FUNCTION_NO_ARGS()                                         \
  DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,                            \
             "[0x%x:0x%x:%u]: " __FUNCTION__ "()\n",                           \
             PtrToUlong(PsGetCurrentProcessId()),                              \
             PtrToUlong(PsGetCurrentThreadId()), KeGetCurrentIrql())

#define DEBUG_EXIT_FUNCTION(Format, ...)                                       \
  DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,                            \
             "[0x%x:0x%x:%u]: " __FUNCTION__ "(-):" Format "\n",               \
             PtrToUlong(PsGetCurrentProcessId()),                              \
             PtrToUlong(PsGetCurrentThreadId()), KeGetCurrentIrql(),           \
             __VA_ARGS__)

#define DEBUG_EXIT_FUNCTION_VOID()                                             \
  DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,                            \
             "[0x%x:0x%x:%u]: " __FUNCTION__ "(-)\n",                          \
             PtrToUlong(PsGetCurrentProcessId()),                              \
             PtrToUlong(PsGetCurrentThreadId()), KeGetCurrentIrql())

#else

#define DEBUG_ENTER_FUNCTION(Format, ...)                                      \
  do {                                                                         \
  } while (FALSE)

#define DEBUG_ENTER_FUNCTION_NO_ARGS()                                         \
  do {                                                                         \
  } while (FALSE)

#define DEBUG_EXIT_FUNCTION(Format, ...)                                       \
  do {                                                                         \
  } while (FALSE)

#define DEBUG_EXIT_FUNCTION_VOID()                                             \
  do {                                                                         \
  } while (FALSE)

#endif

#define DEBUG_ERROR(Format, ...)                                               \
  DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,                            \
             "[0x%x:0x%x:%u]: " __FUNCTION__ " (" __FILE__                     \
                                             ":%u): ERROR: " Format "\n",      \
             PtrToUlong(PsGetCurrentProcessId()),                              \
             PtrToUlong(PsGetCurrentThreadId()), KeGetCurrentIrql(), __LINE__, \
             __VA_ARGS__)

#define DEBUG_WARNING(Format, ...)                                             \
  DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,                          \
             "[0x%x:0x%x:%u]: " __FUNCTION__ " (" __FILE__                     \
                                             ":%u): WARNING: " Format "\n",    \
             PtrToUlong(PsGetCurrentProcessId()),                              \
             PtrToUlong(PsGetCurrentThreadId()), KeGetCurrentIrql(), __LINE__, \
             __VA_ARGS__)

#define DEBUG_TRACE(Format, ...)                                               \
  DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,                            \
             "[0x%x:0x%x:%u]: " __FUNCTION__ " (" __FILE__                     \
                                             ":%u): TRACE: " Format "\n",      \
             PtrToUlong(PsGetCurrentProcessId()),                              \
             PtrToUlong(PsGetCurrentThreadId()), KeGetCurrentIrql(), __LINE__, \
             __VA_ARGS__)

#define DEBUG_INFO(Format, ...)                                                \
  DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,                             \
             "[0x%x:0x%x:%u]: " __FUNCTION__ ": INFO: " Format "\n",           \
             PtrToUlong(PsGetCurrentProcessId()),                              \
             PtrToUlong(PsGetCurrentThreadId()), KeGetCurrentIrql(),           \
             __VA_ARGS__)

#endif

#endif
