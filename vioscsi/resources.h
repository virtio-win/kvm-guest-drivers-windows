/*
 * Main include file
 * This file contains various definitions and globals
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
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

#ifndef ___RESOURCES_H__
#define ___RESOURCES_H__

#define VENDORID                      0x1AF4
#define PRODUCTID                     0x1004
#define MANUFACTURER                  L"Red Hat, Inc."
#define SERIALNUMBER                  L""
#define MODEL                         L"VirtIO-SCSI"
#define MODELDESCRIPTION              L"Red Hat VirtIO SCSI pass-through controller"
#define HARDWAREVERSION               L"v1.0"
#define DRIVERVERSION                 L"v1.0"
#define OPTIONROMVERSION              L"v1.0"
#define FIRMWAREVERSION               L"v1.0"
#define DRIVERNAME                    L"vioscsi.sys"
#define HBASYMBOLICNAME               L"Red Hat VirtIO SCSI pass-through controller"
#define REDUNDANTOPTIONROMVERSION     OPTIONROMVERSION
#define REDUNDANTFIRMWAREVERSION      FIRMWAREVERSION
#define MFRDOMAIN                     L"Red Hat, Inc."
#define PORTSYMBOLICNAME              L"PortSymbolicName"
#define CLUSDISK                      L"CLUSDISK"
#define HBA_ID                        1234567890987654321ULL
#endif //___RESOURCES_H__

