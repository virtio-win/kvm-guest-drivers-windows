#include "stdafx.h"
#include "winToeplitz.h"

uint8_t workingkey[WTEP_MAX_KEY_SIZE];

void toeplitzw_initialize(uint8_t *key, int keysize)
{
    if (keysize > WTEP_MAX_KEY_SIZE) keysize = WTEP_MAX_KEY_SIZE;
    memcpy(workingkey, key, keysize);
}

#define RtlUlongByteSwap(ul) _byteswap_ulong(ul)

// Little Endian version ONLY
UINT32 ToeplitsHash(const PHASH_CALC_SG_BUF_ENTRY sgBuff, int sgEntriesNum, UINT8 *fullKey)
{
#define TOEPLITZ_MAX_BIT_NUM (7)
#define TOEPLITZ_BYTE_HAS_BIT(byte, bit) ((byte) & (1 << (TOEPLITZ_MAX_BIT_NUM - (bit))))
#define TOEPLITZ_BYTE_BIT_STATE(byte, bit) (((byte) >> (TOEPLITZ_MAX_BIT_NUM - (bit))) & 1)

    UINT32 firstKeyWord, res = 0;
    UINT byte, bit;
    PHASH_CALC_SG_BUF_ENTRY sgEntry;
    UINT8 *next_key_byte = fullKey + sizeof(firstKeyWord);
    firstKeyWord = RtlUlongByteSwap(*(UINT32*)fullKey);

    for(sgEntry = sgBuff; sgEntry < sgBuff + sgEntriesNum; ++sgEntry)
    {
        for (byte = 0; byte < sgEntry->chunkLen; ++byte)
        {
            for (bit = 0; bit <= TOEPLITZ_MAX_BIT_NUM; ++bit)
            {
                if (TOEPLITZ_BYTE_HAS_BIT(sgEntry->chunkPtr[byte], bit))
                {
                    res ^= firstKeyWord;
                }
                firstKeyWord = (firstKeyWord << 1) | TOEPLITZ_BYTE_BIT_STATE(*next_key_byte, bit);
            }
            ++next_key_byte;
        }
    }
    return res;

#undef TOEPLITZ_BYTE_HAS_BIT
#undef TOEPLITZ_BYTE_BIT_STATE
#undef TOEPLITZ_MAX_BIT_NUM
}


