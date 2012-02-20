#include "stdafx.h"
#include "winToeplitz.h"


static uint8_t workingkey[WTEP_MAX_KEY_SIZE];

void toeplitzw_initialize(uint8_t *key, int keysize)
{
	if (keysize > WTEP_MAX_KEY_SIZE) keysize = WTEP_MAX_KEY_SIZE;
	memcpy(workingkey, key, keysize);
}

// Little Endian version ONLY
uint32_t toeplitzw_hash(const uint8_t *vector, int len)
{
	uint32_t next, res = 0;
	union { uint8_t bytes[4]; uint32_t l;} key;
	int byte, bit;
	key.bytes[0] = workingkey[3];
	key.bytes[1] = workingkey[2];
	key.bytes[2] = workingkey[1];
	key.bytes[3] = workingkey[0];
	for (byte = 0; byte < len; byte++)
	{
		next = *(workingkey + byte + 4);
		for (bit = 0; bit < 8; bit++)
		{
			uint8_t vecb;
			vecb = (vector[byte] & (1 << (7 - bit))) ? 1 : 0;
			if (vecb)
			{
				res ^= key.l;
			}
			key.l = key.l << 1;
			key.l |= (next & 0x80) ? 1 : 0;
			next = next << 1;
		}
	}
	return res;
}


