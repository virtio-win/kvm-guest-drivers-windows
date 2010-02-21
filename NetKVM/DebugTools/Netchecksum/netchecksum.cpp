/**********************************************************************
 * Copyright (c) 2008  Red Hat, Inc.
 *
 * File: netchecksum.cpp
 *
 * Defines the entry point for the console application
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#define _CRT_SECURE_NO_WARNINGS

#include "stdafx.h"
extern "C" {
#include "ndis56common.h"
}

BYTE buf[0x10000];


void ProcessFile(FILE *f, ULONG flags)
{
	BOOL bContinue = TRUE;
	UINT offset = 0;
	memset(buf, 0, sizeof(buf));
	while (bContinue)
	{
		char s[3];
		if (fread(s, 1, 1, f) == 1)
		{
			if (isxdigit(s[0]) && fread(s+1, 1, 1, f) == 1 && isxdigit(s[1]))
			{
				ULONG val;
				s[2] = 0;
				sscanf(s, "%x", &val);
				buf[offset++] = (UCHAR)val;
			}
			else if (isalpha(s[0])) bContinue = FALSE;
		}
	}
	if (offset > 14)
	{
		if (flags)
		{
			DPrintf(0, ("processing buffer of %d started", offset));
			ParaNdis_CheckSumVerify(buf + 14, offset - 14, pcrIpChecksum | pcrTcpChecksum | flags, __FUNCTION__);
			DPrintf(0, ("processing buffer of %d finished", offset));
		}
		DPrintf(0, ("Verification of buffer of %d started", offset));
		ParaNdis_CheckSumVerify(buf + 14, offset - 14, pcrIpChecksum | pcrTcpChecksum, __FUNCTION__);
		DPrintf(0, ("Verification of buffer of %d finished", offset));
	}

}

struct
{
	LPCSTR file;
	ULONG flags;
}Jobs[] =
{
	{ "tcp-short.txt", pcrFixIPChecksum },
	{ "tcp-ph.txt",    pcrFixXxpChecksum },
	{ "tcp-cs.txt",    pcrFixPHChecksum },
	{ "tcp-badcs.txt", pcrFixXxpChecksum },
	{ "tcp-badcs.txt", pcrFixPHChecksum },
};
int _tmain(int argc, _TCHAR* argv[])
{
	int i;
	FILE *f;
	for (i = 0; i < sizeof(Jobs)/sizeof(Jobs[0]); ++i)
	{
		f = fopen(Jobs[i].file,"rt");
		if (f)
		{
			DPrintf(0, ("Processing file %s started", Jobs[i].file));
			ProcessFile(f, Jobs[i].flags);
			DPrintf(0, ("Processing file %s finished", Jobs[i].file));
			DPrintf(0, ("===================================="));
			fclose(f);
		}
	}
	return 0;
}

