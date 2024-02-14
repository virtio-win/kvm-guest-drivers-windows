/*
 * Defines the entry point for the console application
 *
 * Copyright (c) 2008-2017 Red Hat, Inc.
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

#define _CRT_SECURE_NO_WARNINGS

#include "stdafx.h"
extern "C" {
#include "ndis56common.h"
}

BYTE buf[0x10000];


bool ProcessFile(FILE *f, ULONG flags, ULONG result[4])
{
    bool bContinue = TRUE;
    UINT offset = 0;
    memset(buf, 0, sizeof(buf));
    tTcpIpPacketParsingResult res;
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
    bContinue = false;
    if (offset > 14)
    {
        ULONG expected;
        ULONG pass = 0;
        bContinue = true;

        if (bContinue)
        {
            pass++;
            expected = result[pass - 1];
            DPrintf(0, ("Pass %d buffer of %d started", pass, offset));
            res = ParaNdis_ReviewIPPacket(buf + 14, offset - 14, __FUNCTION__);
            DPrintf(0, ("Pass %d buffer of %d finished", pass, offset));
            if (res.value != expected)
            {
                DPrintf(0, ("%d pass FAILED: expected %08X, received %08X", pass, expected, res.value));
                bContinue = false;
            }
        }

        if (bContinue)
        {
            pass++;
            expected = result[pass - 1];
            DPrintf(0, ("Pass %d buffer of %d started", pass, offset));
            res = ParaNdis_CheckSumVerify(buf + 14, offset - 14, pcrAnyChecksum, __FUNCTION__);
            DPrintf(0, ("Pass %d buffer of %d finished", pass, offset));
            if (res.value != expected)
            {
                DPrintf(0, ("%d pass FAILED: expected %08X, received %08X", pass, expected, res.value));
                bContinue = false;
            }
        }

        if (bContinue)
        {
            pass++;
            expected = result[pass - 1];
            DPrintf(0, ("Pass %d buffer of %d started", pass, offset));
            res = ParaNdis_CheckSumVerify(buf + 14, offset - 14, pcrAnyChecksum | flags, __FUNCTION__);
            DPrintf(0, ("Pass %d buffer of %d finished", pass, offset));
            if (res.value != expected)
            {
                DPrintf(0, ("%d pass FAILED: expected %08X, received %08X", pass, expected, res.value));
                bContinue = false;
            }
        }

        if (bContinue)
        {
            pass++;
            expected = result[pass - 1];
            DPrintf(0, ("Pass %d buffer of %d started", pass, offset));
            res = ParaNdis_CheckSumVerify(buf + 14, offset - 14, pcrAnyChecksum, __FUNCTION__);
            DPrintf(0, ("Pass %d buffer of %d finished", pass, offset));
            if (res.value != expected)
            {
                DPrintf(0, ("%d pass FAILED: expected %08X, received %08X", pass, expected, res.value));
                bContinue = false;
            }
        }
    }
    return bContinue;
}

struct
{
    LPCSTR file;
    ULONG flags;
    ULONG result[4];
}Jobs[] =
{
    // TCP packet with valid IPCS and PHCS, populate TCP CS
    { "tcp-ph.txt",    pcrFixXxpChecksum, { 0x28140182, 0x2814019A, 0x2814099A, 0x281401AA } },
    // TCP packet with bad IPCS and valid TCPCS, fix IPCS
    { "tcp-short.txt", pcrFixIPChecksum, { 0x28140182, 0x281401AE, 0x281405AE, 0x281401AA }  },
    // TCP packet with valid IPCS and TCPCS, populate PHCS
    { "tcp-cs.txt",    pcrFixPHChecksum, { 0x28140182, 0x281401AA, 0x281409BA, 0x2814019A }  },
    // TCP packet with valid IPCS and bad TCPCS, populate TCPCS
    { "tcp-badcs.txt", pcrFixXxpChecksum, { 0x28140182, 0x281401BA, 0x281409BA, 0x281401AA }  },
    // TCP packet with valid IPCS and bad TCPCS, populate PHCS
    { "tcp-badcs.txt", pcrFixPHChecksum, { 0x28140182, 0x281401BA, 0x281409BA, 0x2814019A }  },
    // TCP packet with valid TCPCS, populate TCPCS
    { "tcpv6-cs.txt",  pcrFixXxpChecksum, { 0x3C28018B, 0x3C2801AB, 0x3C2801AB, 0x3C2801AB }  },
    // TCP packet with valid TCPCS, populate PHCS
    { "tcpv6-cs.txt",  pcrFixPHChecksum, { 0x3C28018B, 0x3C2801AB, 0x3C2809BB, 0x3C28019B }  },
    // TCP packet with valid UDPCS, populate UDPCS
    { "udpv6-cs.txt",  pcrFixXxpChecksum, { 0x3028038B, 0x302803AB, 0x302803AB, 0x302803AB }  },
    // TCP packet with valid UDPCS, populate PHCS
    { "udpv6-cs.txt",  pcrFixPHChecksum | pcrFixIPChecksum, { 0x3028038B, 0x302803AB, 0x30280BBB, 0x3028039B }  },
};

int _tmain(int argc, _TCHAR* argv[])
{
    bool bOK = true;
    int i;
    FILE *f;
    for (i = 0; bOK && i < sizeof(Jobs)/sizeof(Jobs[0]); ++i)
    {
        f = fopen(Jobs[i].file,"rt");
        if (f)
        {
            DPrintf(0, ("Processing file %s started", Jobs[i].file));
            bOK = ProcessFile(f, Jobs[i].flags, Jobs[i].result);
            DPrintf(0, ("Processing file %s finished", Jobs[i].file));
            DPrintf(0, ("===================================="));
            fclose(f);
        }
    }

    DPrintf(0, ("Unit test %s", bOK ? "PASSED" : "FAILED"));
    
    //getchar();

    return 0;
}


