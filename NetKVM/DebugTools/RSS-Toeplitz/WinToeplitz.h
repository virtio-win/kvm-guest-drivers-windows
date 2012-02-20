#ifndef win_toeplitz_h
#define win_toeplitz_h

#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif
#endif

#define WTEP_MAX_KEY_SIZE	40

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;

EXTERN_C void toeplitzw_initialize(uint8_t *key, int keysize);
EXTERN_C uint32_t toeplitzw_hash(const uint8_t *vector, int len);


#endif
