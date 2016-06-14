/**********************************************************************
 * Copyright (c) 2016  Red Hat, Inc.
 *
 * File: resources.h
 *
 * Main include file
 * This file contains vrious definitions and globals
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#ifndef ___RESOURCES_H__
#define ___RESOURCES_H__

#ifndef _NT_TARGET_MAJ
#include "vioscsi-2012.h"
#endif

#define VENDORID                      0x1AF4
#define PRODUCTID                     0x1004
#define MANUFACTURER                  L"Red Hat Inc."
#define SERIALNUMBER                  L"SerialNumber"
#define MODEL                         L"VirtIO-SCSI"
#define MODELDESCRIPTION              L"Red Hat VirtIO SCSI pass-through controller"
#define HARDWAREVERSION               L"v1.0"
#define DRIVERVERSION                 _MAJORVERSION_
//#define OPTIONROMVERSION              L"OptionROMVersion"
#define FIRMWAREVERSION               L"v1.0"
#define DRIVERNAME                    L"vioscsi.sys"
#define HBASYMBOLICNAME               L"Red Hat VirtIO SCSI pass-through controller"
//#define REDUNDANTOPTIONROMVERSION     L"RedundantOptionROMVersion"
//#define REDUNDANTFIRMWAREVERSION      L"RedundantFirmwareVersion"
#define MFRDOMAIN                     L"Red, Hat Inc."

#define CLUSDISK                      L"CLUSDISK"
#define HBA_ID                        1234567890987654321ULL

#endif //___RESOURCES_H__

