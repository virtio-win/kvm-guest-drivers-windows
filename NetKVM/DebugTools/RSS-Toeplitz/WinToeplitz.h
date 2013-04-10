#ifndef win_toeplitz_h
#define win_toeplitz_h

#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif
#endif

#define WTEP_MAX_KEY_SIZE   40

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;

typedef struct _tagHASH_CALC_SG_BUF_ENTRY
{
    PBYTE chunkPtr;
    ULONG chunkLen;
} HASH_CALC_SG_BUF_ENTRY, *PHASH_CALC_SG_BUF_ENTRY;

EXTERN_C void toeplitzw_initialize(uint8_t *key, int keysize);
EXTERN_C UINT32 ToeplitsHash(const PHASH_CALC_SG_BUF_ENTRY sgBuff, int sgEntriesNum, UINT8 *fullKey);

EXTERN_C uint8_t workingkey[];

#endif
