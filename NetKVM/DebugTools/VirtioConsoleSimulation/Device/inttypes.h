#ifndef intttypes_h
#define intttypes_h

typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned char uint8_t;
typedef unsigned long uint32_t;
typedef unsigned __int64 uint64_t;
typedef long int32_t;

typedef void QEMUFile;
typedef int bool;

#define offsetof(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))

#define MIN(x,y) (((x) < (y)) ? (x) : (y))

#define unlikely(x) (x)
#define true TRUE
#define false FALSE

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define __attribute__(...)

#endif
