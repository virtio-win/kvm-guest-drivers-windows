/*
 * Copyright (C) 2015-2016 Red Hat, Inc.
 *
 * This software is licensed under the GNU General Public License,
 * version 2 (GPLv2) (see COPYING for details), subject to the following
 * clarification.
 *
 * With respect to binaries built using the Microsoft(R) Windows Driver
 * Kit (WDK), GPLv2 does not extend to any code contained in or derived
 * from the WDK ("WDK Code"). As to WDK Code, by using or distributing
 * such binaries you agree to be bound by the Microsoft Software License
 * Terms for the WDK. All WDK Code is considered by the GPLv2 licensors
 * to qualify for the special exception stated in section 3 of GPLv2
 * (commonly known as the system library exception).
 *
 * There is NO WARRANTY for this software, express or implied,
 * including the implied warranties of NON-INFRINGEMENT, TITLE,
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Refer to the LICENSE file for full details of the license.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 */

#include "pvpanic.h"
#include "bugcheck.tmh"

static KBUGCHECK_CALLBACK_RECORD CallbackRecord;

KBUGCHECK_CALLBACK_ROUTINE PVPanicOnBugCheck;

VOID PVPanicOnBugCheck(IN PVOID Buffer, IN ULONG Length)
{
	if ((Buffer != NULL) && (Length == sizeof(PVOID)))
	{
		PUCHAR PortAddress = (PUCHAR)Buffer;
		WRITE_PORT_UCHAR(PortAddress, (UCHAR)(PVPANIC_PANICKED));
	}
}

BOOLEAN PVPanicRegisterBugCheckCallback(IN PVOID PortAddress)
{
	KeInitializeCallbackRecord(&CallbackRecord);

	return KeRegisterBugCheckCallback(&CallbackRecord, PVPanicOnBugCheck,
		(PVOID)PortAddress, sizeof(PVOID), (PUCHAR)("PVPanic"));
}

BOOLEAN PVPanicDeregisterBugCheckCallback()
{
	return KeDeregisterBugCheckCallback(&CallbackRecord);
}
