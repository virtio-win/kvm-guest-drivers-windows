/* HID passthrough driver.
 * Definitions shared by vioinput and hidpassthrough.
 *
 * Copyright (c) 2017 Red Hat, Inc.
 * Author: Ladi Prosek <lprosek@redhat.com>
 */

#pragma once
#include <wdm.h>

#define PDO_EXTENSION_V1 1
#define PDO_EXTENSION_VERSION PDO_EXTENSION_V1

typedef struct _tagPdoExtension
{
    /* This extension is an interface between vioinput and hidpassthrough.
     * It doesn't hurt to add a version field in case we need to extend it
     * in the future.
     */
    ULONG           Version;

    /* Pointer to the bus FDO that enumerated the PDO */
    PDEVICE_OBJECT  BusFdo;
} PDO_EXTENSION, *PPDO_EXTENSION;
