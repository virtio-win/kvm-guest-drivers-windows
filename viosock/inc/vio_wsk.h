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

#ifndef _VIO_WSK_H
#define _VIO_WSK_H

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER

#ifdef  __cplusplus
extern "C" {
#endif

_Must_inspect_result_
NTSTATUS
VioWskRegister(
    _In_ PWSK_CLIENT_NPI    WskClientNpi,
    _Out_ PWSK_REGISTRATION WskRegistration
);

VOID
VioWskDeregister(
    _In_ PWSK_REGISTRATION WskRegistration
);

_Must_inspect_result_
NTSTATUS
VioWskCaptureProviderNPI(
    _In_ PWSK_REGISTRATION  WskRegistration,
    _In_ ULONG              WaitTimeout,
    _Out_ PWSK_PROVIDER_NPI WskProviderNpi
);

VOID
VioWskReleaseProviderNPI(
    _In_ PWSK_REGISTRATION WskRegistration
);

_Must_inspect_result_
NTSTATUS
VioWskQueryProviderCharacteristics(
    _In_ PWSK_REGISTRATION              WskRegistration,
    _Out_ PWSK_PROVIDER_CHARACTERISTICS WskProviderCharacteristics
);

#ifdef  __cplusplus
}
#endif

#endif /* _VIO_WSK_H */
