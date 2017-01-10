/**********************************************************************
 * Copyright (c) 2008-2016 Red Hat, Inc.
 *
 * File: quverp.h
 *
 * This file contains version resource related definitions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/
#include <windows.h>
#include <ntverp.h>

#ifndef PARANDIS_MAJOR_DRIVER_VERSION
#error PARANDIS_MAJOR_DRIVER_VERSION not defined
#endif

#ifndef PARANDIS_MINOR_DRIVER_VERSION
#error PARANDIS_MINOR_DRIVER_VERSION not defined
#endif

#ifndef _RHEL_RELEASE_VERSION_
#error _RHEL_RELEASE_VERSION_ not defined
#endif

#if !defined(DRIVER_NT_TARGET_VERSION)
#define DRIVER_NT_TARGET_VERSION
#endif
#if (7-DRIVER_NT_TARGET_VERSION-7) == 14
#undef DRIVER_NT_TARGET_VERSION
#define DRIVER_NT_TARGET_VERSION       40
#endif

#if !defined(PARANDIS_MAJOR_DRIVER_VERSION)
#define PARANDIS_MAJOR_DRIVER_VERSION
#endif
#if (7-PARANDIS_MAJOR_DRIVER_VERSION-7) == 14
#undef PARANDIS_MAJOR_DRIVER_VERSION
#define PARANDIS_MAJOR_DRIVER_VERSION       101
#endif

#if !defined(PARANDIS_MINOR_DRIVER_VERSION)
#define PARANDIS_MINOR_DRIVER_VERSION
#endif
#if (7-PARANDIS_MINOR_DRIVER_VERSION-7) == 14
#undef PARANDIS_MINOR_DRIVER_VERSION
#define PARANDIS_MINOR_DRIVER_VERSION       58000
#endif

#if !defined(_RHEL_RELEASE_VERSION_)
#define _RHEL_RELEASE_VERSION_
#endif
#if (7-_RHEL_RELEASE_VERSION_-7) == 14
#undef _RHEL_RELEASE_VERSION_
#define _RHEL_RELEASE_VERSION_      65
#endif

#ifdef VER_COMPANYNAME_STR
#undef VER_COMPANYNAME_STR
#endif
#ifdef VER_LEGALTRADEMARKS_STR
#undef VER_LEGALTRADEMARKS_STR
#endif
#ifdef VER_PRODUCTBUILD
#undef VER_PRODUCTBUILD
#endif
#ifdef VER_PRODUCTBUILD_QFE
#undef VER_PRODUCTBUILD_QFE
#endif
#ifdef VER_PRODUCTNAME_STR
#undef VER_PRODUCTNAME_STR
#endif
#ifdef VER_PRODUCTMAJORVERSION
#undef VER_PRODUCTMAJORVERSION
#endif
#ifdef VER_PRODUCTMINORVERSION
#undef VER_PRODUCTMINORVERSION
#endif

#define VER_COMPANYNAME_STR             "Red Hat Inc."
#define VER_LEGALTRADEMARKS_STR         ""
#define VER_LEGALCOPYRIGHT_STR          "Copyright (C) 2008-2016 Red Hat Inc."

#define VER_PRODUCTBUILD                PARANDIS_MAJOR_DRIVER_VERSION
#define VER_PRODUCTBUILD_QFE            PARANDIS_MINOR_DRIVER_VERSION
#define VER_PRODUCTMAJORVERSION         DRIVER_NT_TARGET_VERSION
#define VER_PRODUCTMINORVERSION         _RHEL_RELEASE_VERSION_
#undef __BUILDMACHINE__

#define VER_LANGNEUTRAL

#define VER_FILETYPE                VFT_DRV
#define VER_FILESUBTYPE             VFT2_DRV_SYSTEM
#define VER_FILEDESCRIPTION_STR     "Red Hat NDIS Miniport Driver"
#define VER_INTERNALNAME_STR        "netkvm.sys"
#define VER_PRODUCTNAME_STR         "Red Hat VirtIO Ethernet Adapter"

#include "common.ver"
