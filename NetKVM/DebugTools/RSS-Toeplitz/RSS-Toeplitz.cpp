// RSS-Toeplitz.cpp : Little-endian test for RSS Toeplitz hash
//

#include "stdafx.h"
#include "WinToeplitz.h"


static uint8_t testKey[WTEP_MAX_KEY_SIZE] = {
0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa };

static struct
{
    uint32_t resultIP;
    uint32_t resultTCP;
    uint8_t sourceIP[4];
    uint8_t destIP[4];
    uint16_t sourcePort;
    uint16_t destPort;
} testData[] =
{
    { 0x323e8fc2, 0x51ccc178, { 66, 9, 149, 187 }, { 161, 142, 100, 80 }, 2794, 1766 },
    { 0xd718262a, 0xc626b0ea, { 199, 92, 111, 2 }, { 65, 69, 140, 83 } , 14230, 4739 },
    { 0xd2d0a5de, 0x5c2b394a, { 24, 19, 198, 95 }, { 12, 22, 207, 184 }, 12898, 38024 },
    { 0x82989176, 0xafc7327f, { 38, 27, 205, 30 }, { 209, 142, 163, 6 }, 48228,  2217 },
    { 0x5d1809c5, 0x10e828a2, { 153, 39, 163, 191 }, { 202, 188, 127, 2 }, 44251, 1303 },
};

#define ITERATIONS_NUMBER (1000000UL)

int _tmain(int argc, _TCHAR* argv[])
{
    int i;
    uint8_t vector[12];

    unsigned long numSucessfullTCP = 0;
    unsigned long numFailedTCP = 0;
    unsigned long numSucessfullIP = 0;
    unsigned long numFailedIP = 0;
    ULONGLONG StartTickCount, FinishTickCount;

    toeplitzw_initialize(testKey, sizeof(testKey));

    StartTickCount = GetTickCount64();

    for (unsigned long it = 0; it < ITERATIONS_NUMBER; ++it)
    {
        for (i = 0; i < sizeof(testData)/sizeof(testData[0]); ++i)
        {
            uint32_t res;
            HASH_CALC_SG_BUF_ENTRY sgBuffer[2];

            vector[0] = testData[i].sourceIP[0];
            vector[1] = testData[i].sourceIP[1];
            vector[2] = testData[i].sourceIP[2];
            vector[3] = testData[i].sourceIP[3];
            vector[4] = testData[i].destIP[0];
            vector[5] = testData[i].destIP[1];
            vector[6] = testData[i].destIP[2];
            vector[7] = testData[i].destIP[3];
            vector[8] = testData[i].sourcePort >> 8;
            vector[9] = testData[i].sourcePort & 0xff;
            vector[10] = testData[i].destPort >> 8;
            vector[11] = testData[i].destPort & 0xff;

            sgBuffer[0].chunkPtr = vector;
            sgBuffer[0].chunkLen = 8;
            sgBuffer[1].chunkPtr = vector + 8;
            sgBuffer[1].chunkLen = 4;

            res = ToeplitsHash(sgBuffer, 1, workingkey);
            if (res == testData[i].resultIP)
            {
                ++numSucessfullIP;
            }
            else
            {
                ++numFailedIP;
                printf("IP calculation failed for data sample %d\n", i);
            }
            res = ToeplitsHash(sgBuffer, 2, workingkey);
            if (res == testData[i].resultTCP)
            {
                ++numSucessfullTCP;
            }
            else
            {
                ++numFailedTCP;
                printf("TCP calculation failed for data sample %d\n", i);
            }
        }
    }

    FinishTickCount = GetTickCount64();

    printf("Correct IP calculations     %lu\n", numSucessfullIP);
    printf("Correct TCP calculations    %lu\n", numSucessfullTCP);
    printf("Wrong IP calculations       %lu\n", numFailedIP);
    printf("Wrong TCP calculations      %lu\n", numFailedTCP);
    printf("Total test time             %lu Ms\n", FinishTickCount - StartTickCount);
    printf("\n\n");

    if(numFailedIP || numFailedTCP)
    {
        printf("Test FAILED\n");
        return -1;
    }
    else
    {
        printf("Test PASSED\n");
        return 0;
    }
}

static void invertbits(const uint8_t *from, int len, uint8_t *to)
{
    //                           0x00  0x01  0x02  0x03  0x04  0x05  0x06  0x07  0x08  0x09  0x0a  0x0b  0x0c  0x0d  0x0e  0x0f
    static uint8_t trans[16] = { 0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e, 0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b, 0x07, 0x0f };
    while (len--)
    {
        uint8_t lo = trans[*from >> 4];
        uint8_t hi = trans[*from & 0xf];
        *to = (hi << 4) | lo;
        ++from;
        ++to;
    }
}

static void printbitsofbyte(uint8_t val, char last)
{
    int i;
    char s[9];
    s[8] = 0;
    for (i = 0; i < 8; ++i) s[i] = (val & (1 << i)) ? '1' : '0';
    printf("%s%c", s, last);
}

static void printbits(const char *name, uint8_t *p, int len)
{
    int i;
    printf("%s", name);
    for (i = 0; i < len/4; ++i)
    {
        printbitsofbyte(p[4*i], ' ');
        printbitsofbyte(p[4*i + 1], ' ');
        printbitsofbyte(p[4*i + 2], ' ');
        printbitsofbyte(p[4*i + 3], '\n');
    }
    for (i = 0; i < len % 4; ++i)
    {
        printbitsofbyte(p[(len/4)*4 + i], ' ');
    }
    printf("\n");
}
