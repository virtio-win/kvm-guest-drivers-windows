#include "bitops.h"
#if !DBG
#include "bitops.tmh"
#endif

#pragma code_seg(push)
#pragma code_seg()

VOID
GetPitches(
    _In_ CONST BLT_INFO* pBltInfo,
    _Out_ LONG* pPixelPitch,
    _Out_ LONG* pRowPitch
)
{
    switch (pBltInfo->Rotation) {
    case D3DKMDT_VPPR_IDENTITY:
        *pPixelPitch = (pBltInfo->BitsPerPel / BITS_PER_BYTE);
        *pRowPitch = pBltInfo->Pitch;
        return;
    case D3DKMDT_VPPR_ROTATE90:
        *pPixelPitch = -((LONG)pBltInfo->Pitch);
        *pRowPitch = (pBltInfo->BitsPerPel / BITS_PER_BYTE);
        return;
    case D3DKMDT_VPPR_ROTATE180:
        *pPixelPitch = -((LONG)pBltInfo->BitsPerPel / BITS_PER_BYTE);
        *pRowPitch = -((LONG)pBltInfo->Pitch);
        return;
    case D3DKMDT_VPPR_ROTATE270:
        *pPixelPitch = pBltInfo->Pitch;
        *pRowPitch = -((LONG)pBltInfo->BitsPerPel / BITS_PER_BYTE);
        return;
    default:
        VioGpuDbgBreak();
        VIOGPU_LOG_ASSERTION1("Invalid rotation (0x%I64x) specified", pBltInfo->Rotation);
        *pPixelPitch = 0;
        *pRowPitch = 0;
        return;
    }
}

BYTE* GetRowStart(_In_ CONST BLT_INFO* pBltInfo, _In_ CONST RECT* pRect)
{
    BYTE* pRet = NULL;
    LONG OffLeft = pRect->left + pBltInfo->Offset.x;
    LONG OffTop = pRect->top + pBltInfo->Offset.y;
    LONG BytesPerPixel = (pBltInfo->BitsPerPel / BITS_PER_BYTE);

    switch (pBltInfo->Rotation) {
    case D3DKMDT_VPPR_IDENTITY:
        pRet = ((BYTE*)pBltInfo->pBits +
            (ULONGLONG)OffTop * pBltInfo->Pitch +
            (ULONGLONG)OffLeft * BytesPerPixel);
        break;
    case D3DKMDT_VPPR_ROTATE90:
        pRet = ((BYTE*)pBltInfo->pBits +
            ((ULONGLONG)pBltInfo->Height - 1 - OffLeft) * pBltInfo->Pitch +
            (ULONGLONG)OffTop * BytesPerPixel);
        break;
    case D3DKMDT_VPPR_ROTATE180:
        pRet = ((BYTE*)pBltInfo->pBits +
            ((ULONGLONG)pBltInfo->Height - 1 - OffTop) * pBltInfo->Pitch +
            ((ULONGLONG)pBltInfo->Width - 1 - OffLeft) * BytesPerPixel);
        break;
    case D3DKMDT_VPPR_ROTATE270:
        pRet = ((PBYTE)pBltInfo->pBits +
            (ULONGLONG)OffLeft * pBltInfo->Pitch +
            ((ULONGLONG)pBltInfo->Width - 1 - OffTop) * BytesPerPixel);
        break;
    default:
    {
        VIOGPU_LOG_ASSERTION1("Invalid rotation (0x%I64x) specified", pBltInfo->Rotation);
    }
    }

    return pRet;
}

VOID CopyBitsGeneric(
    _In_  BLT_INFO* pDst,
    _In_ CONST BLT_INFO* pSrc,
    _In_ CONST RECT *pRect)
{
    LONG DstPixelPitch = 0;
    LONG DstRowPitch = 0;
    LONG SrcPixelPitch = 0;
    LONG SrcRowPitch = 0;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s Dst = %p Src = %p\n", __FUNCTION__, pDst->pBits, pSrc->pBits));

    GetPitches(pDst, &DstPixelPitch, &DstRowPitch);
    GetPitches(pSrc, &SrcPixelPitch, &SrcRowPitch);


    VIOGPU_ASSERT(pRect->right >= pRect->left);
    VIOGPU_ASSERT(pRect->bottom >= pRect->top);

    UINT NumPixels = pRect->right - pRect->left;
    UINT NumRows = pRect->bottom - pRect->top;

    BYTE* pDstRow = GetRowStart(pDst, pRect);
    CONST BYTE* pSrcRow = GetRowStart(pSrc, pRect);

    for (UINT y = 0; y < NumRows; y++) {
        BYTE* pDstPixel = pDstRow;
        CONST BYTE* pSrcPixel = pSrcRow;

        for (UINT x = 0; x < NumPixels; x++) {
            if (pDst->BitsPerPel == 32 && pSrc->BitsPerPel == 32) {
                UINT32* pDstPixelAs32 = (UINT32*)pDstPixel;
                UINT32* pSrcPixelAs32 = (UINT32*)pSrcPixel;
                *pDstPixelAs32 = *pSrcPixelAs32;
            }
            else {
                VioGpuDbgBreak();
            }
            pDstPixel += DstPixelPitch;
            pSrcPixel += SrcPixelPitch;
        }
        pDstRow += DstRowPitch;
        pSrcRow += SrcRowPitch;
    }
}

VOID CopyBits32_32(
    _In_ BLT_INFO* pDst,
    _In_ CONST BLT_INFO* pSrc,
    _In_ CONST RECT *pRect)
{
    VIOGPU_ASSERT((pDst->BitsPerPel == 32) &&
        (pSrc->BitsPerPel == 32));
    VIOGPU_ASSERT((pDst->Rotation == D3DKMDT_VPPR_IDENTITY) &&
        (pSrc->Rotation == D3DKMDT_VPPR_IDENTITY));

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pRect->right >= pRect->left);
    VIOGPU_ASSERT(pRect->bottom >= pRect->top);

    UINT NumPixels = pRect->right - pRect->left;
    UINT NumRows = pRect->bottom - pRect->top;
    UINT BytesToCopy = NumPixels * 4;
    BYTE* pStartDst = ((BYTE*)pDst->pBits +
        ((ULONGLONG)pRect->top + pDst->Offset.y) * pDst->Pitch +
        ((ULONGLONG)pRect->left + pDst->Offset.x) * 4);
    CONST BYTE* pStartSrc = ((BYTE*)pSrc->pBits +
        ((ULONGLONG)pRect->top + pSrc->Offset.y) * pSrc->Pitch +
        ((ULONGLONG)pRect->left + pSrc->Offset.x) * 4);

    for (UINT i = 0; i < NumRows; ++i) {
        RtlCopyMemory(pStartDst, pStartSrc, BytesToCopy);
        pStartDst += pDst->Pitch;
        pStartSrc += pSrc->Pitch;
    }
}


VOID BltBits(
    _In_ BLT_INFO* pDst,
    _In_ CONST BLT_INFO* pSrc,
    _In_ CONST RECT *pRect)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    __try {
        if (pDst->Rotation == D3DKMDT_VPPR_IDENTITY &&
            pSrc->Rotation == D3DKMDT_VPPR_IDENTITY) {
            CopyBits32_32(pDst, pSrc, pRect);
        }
        else {
            CopyBitsGeneric(pDst, pSrc, pRect);
        }
    }
#pragma prefast(suppress: __WARNING_EXCEPTIONEXECUTEHANDLER, "try/except is only able to protect against user-mode errors and these are the only errors we try to catch here");
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("Either dst (0x%p) or src (0x%p) bits encountered exception during access.\n", pDst->pBits, pSrc->pBits));
    }
}

UINT BPPFromPixelFormat(_In_ D3DDDIFORMAT Format)
{
    switch (Format)
    {
    case D3DDDIFMT_UNKNOWN: return 0;
    case D3DDDIFMT_A1: return 1;
    case D3DDDIFMT_P8: return 8;
    case D3DDDIFMT_R5G6B5: return 16;
    case D3DDDIFMT_R8G8B8: return 24;
    case D3DDDIFMT_X8R8G8B8:
    case D3DDDIFMT_A8R8G8B8: return 32;
    default: VIOGPU_LOG_ASSERTION1("Unknown D3DDDIFORMAT 0x%I64x", Format); return 0;
    }
}

#pragma code_seg(pop) // End Non-Paged Code
