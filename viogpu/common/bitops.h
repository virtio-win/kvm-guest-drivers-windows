#pragma once
#include "helper.h"

typedef struct _BLT_INFO {
    PVOID pBits;
    UINT Pitch;
    UINT BitsPerPel;
    POINT Offset;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    UINT Width;
    UINT Height;
} BLT_INFO;


VOID GetPitches(
    _In_ CONST BLT_INFO* pBltInfo,
    _Out_ LONG* pPixelPitch,
    _Out_ LONG* pRowPitch);

BYTE* GetRowStart(
    _In_ CONST BLT_INFO* pBltInfo,
    _In_ CONST RECT* pRect);

VOID CopyBitsGeneric(
    _Out_ BLT_INFO* pDst,
    _In_ CONST BLT_INFO* pSrc,
    _In_ UINT  NumRects,
    _In_reads_(NumRects) CONST RECT *pRects);

VOID CopyBits32_32(
    _Out_ BLT_INFO* pDst,
    _In_ CONST BLT_INFO* pSrc,
    _In_ UINT  NumRects,
    _In_reads_(NumRects) CONST RECT *pRects);

VOID BltBits(
    _Out_ BLT_INFO* pDst,
    _In_ CONST BLT_INFO* pSrc,
    _In_ UINT  NumRects,
    _In_reads_(NumRects) CONST RECT *pRects);


UINT BPPFromPixelFormat(D3DDDIFORMAT Format);
