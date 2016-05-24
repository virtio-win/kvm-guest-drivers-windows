//////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2007  Qumranet All Rights Reserved
//
// Module Name:
// osdep.h
//
// Abstract:
// Windows OS dependent definitions of data types
//
// Author:
// Yan Vugenfirer  - February 2007.
//
//////////////////////////////////////////////////////////////////////////////////////////

#if defined(IGNORE_VIRTIO_OSDEP_H)
// to make simulation environment easy
#include "external_os_dep.h"
#else

#ifndef __OS_DEP_H
#define __OS_DEP_H

#include <ntddk.h>

#define ktime_t ULONGLONG
#define ktime_get() KeQueryPerformanceCounter(NULL).QuadPart

#define likely(x) x
#define unlikely(x) x

#define ENOSPC 1
#define BUG_ON(a) ASSERT(!(a))
#define WARN_ON(a)
#define BUG() ASSERT(0)

#define BUILD_BUG_ON(condition) \
    static_assert(!(condition), "Compile-time assumption failed")

#if !defined(__cplusplus) && !defined(bool)
#define bool int
#define false FALSE
#define true TRUE
#endif

#define inline __forceinline

#ifdef DBG
#define DEBUG
#endif

#define mb()   KeMemoryBarrier()
#define rmb()  KeMemoryBarrier()
#define wmb()  KeMemoryBarrier()

#define BIT_ULL(nr) (1ULL << (nr))

#define SMP_CACHE_BYTES 64

#endif
#endif
