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

#include "viorng.h"

#include "virtio.tmh"

static void NoDebugPrintFunc(const char *format, ...)
{
    UNREFERENCED_PARAMETER(format);
}

typedef void (*tDebugPrintFunc)(const char *format, ...);
tDebugPrintFunc VirtioDebugPrintProc = NoDebugPrintFunc;

int virtioDebugLevel = 0;
int bDebugPrint = 0;
