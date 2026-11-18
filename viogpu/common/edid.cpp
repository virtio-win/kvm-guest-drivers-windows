#include "edid.h"

static const UCHAR EdidHeader[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

#define CTA_EXTENSION_TAG         0x02
#define CTA_VIDEO_DATA_BLOCK      2
#define DISPLAYID_EXTENSION_TAG   0x70
// Detailed timing data blocks: Type I (DisplayID 1.3) and Type VII (DisplayID 2.0), 20 bytes per timing.
#define DISPLAYID_TYPE_I_TIMING   0x03
#define DISPLAYID_TYPE_VII_TIMING 0x22
#define DISPLAYID_TIMING_SIZE     20
#define DETAILED_DESCRIPTOR_SIZE  18

// A resolution that one bit of an EDID announces.
typedef struct _EDID_BIT_MODE
{
    UCHAR Byte;
    UCHAR Bit;
    USHORT Width;
    USHORT Height;
} EDID_BIT_MODE;

// clang-format off
// Established Timings I and II and the manufacturer's timing: base block bytes 35-37. Byte 36 bit 4, 1024x768 at
// 87 Hz, is interlaced and left out.
static const EDID_BIT_MODE EstablishedModes[] = {
    {35, 7, 720, 400},  {35, 6, 720, 400},  {35, 5, 640, 480},  {35, 4, 640, 480},
    {35, 3, 640, 480},  {35, 2, 640, 480},  {35, 1, 800, 600},  {35, 0, 800, 600},
    {36, 7, 800, 600},  {36, 6, 800, 600},  {36, 5, 832, 624},  {36, 3, 1024, 768},
    {36, 2, 1024, 768}, {36, 1, 1024, 768}, {36, 0, 1280, 1024}, {37, 7, 1152, 870},
};

// Established Timings III: bytes 6-11 of a display descriptor with tag 0xF7.
static const EDID_BIT_MODE EstablishedModes3[] = {
    {6, 7, 640, 350},    {6, 6, 640, 400},    {6, 5, 720, 400},    {6, 4, 640, 480},
    {6, 3, 848, 480},    {6, 2, 800, 600},    {6, 1, 1024, 768},   {6, 0, 1152, 864},
    {7, 7, 1280, 768},   {7, 6, 1280, 768},   {7, 5, 1280, 768},   {7, 4, 1280, 768},
    {7, 3, 1280, 960},   {7, 2, 1280, 960},   {7, 1, 1280, 1024},  {7, 0, 1280, 1024},
    {8, 7, 1360, 768},   {8, 6, 1440, 900},   {8, 5, 1440, 900},   {8, 4, 1440, 900},
    {8, 3, 1440, 900},   {8, 2, 1400, 1050},  {8, 1, 1400, 1050},  {8, 0, 1400, 1050},
    {9, 7, 1400, 1050},  {9, 6, 1680, 1050},  {9, 5, 1680, 1050},  {9, 4, 1680, 1050},
    {9, 3, 1680, 1050},  {9, 2, 1600, 1200},  {9, 1, 1600, 1200},  {9, 0, 1600, 1200},
    {10, 7, 1600, 1200}, {10, 6, 1600, 1200}, {10, 5, 1792, 1344}, {10, 4, 1792, 1344},
    {10, 3, 1856, 1392}, {10, 2, 1856, 1392}, {10, 1, 1920, 1200}, {10, 0, 1920, 1200},
    {11, 7, 1920, 1200}, {11, 6, 1920, 1200}, {11, 5, 1920, 1440}, {11, 4, 1920, 1440},
};

typedef struct _EDID_VIC_MODE
{
    UCHAR Vic;
    USHORT Width;
    USHORT Height;
} EDID_VIC_MODE;

// CTA-861 Video Identification Codes with progressive scan and no pixel repetition, with the active area Linux's
// drm_edid.c gives them. Interlaced and double-clocked VICs are left out.
static const EDID_VIC_MODE VicModes[] = {
    {1, 640, 480}, {2, 720, 480}, {3, 720, 480}, {4, 1280, 720},
    {12, 2880, 240}, {13, 2880, 240}, {14, 1440, 480}, {15, 1440, 480},
    {16, 1920, 1080}, {17, 720, 576}, {18, 720, 576}, {19, 1280, 720},
    {27, 2880, 288}, {28, 2880, 288}, {29, 1440, 576}, {30, 1440, 576},
    {31, 1920, 1080}, {32, 1920, 1080}, {33, 1920, 1080}, {34, 1920, 1080},
    {35, 2880, 480}, {36, 2880, 480}, {37, 2880, 576}, {38, 2880, 576},
    {41, 1280, 720}, {42, 720, 576}, {43, 720, 576}, {47, 1280, 720},
    {48, 720, 480}, {49, 720, 480}, {52, 720, 576}, {53, 720, 576},
    {56, 720, 480}, {57, 720, 480}, {60, 1280, 720}, {61, 1280, 720},
    {62, 1280, 720}, {63, 1920, 1080}, {64, 1920, 1080}, {65, 1280, 720},
    {66, 1280, 720}, {67, 1280, 720}, {68, 1280, 720}, {69, 1280, 720},
    {70, 1280, 720}, {71, 1280, 720}, {72, 1920, 1080}, {73, 1920, 1080},
    {74, 1920, 1080}, {75, 1920, 1080}, {76, 1920, 1080}, {77, 1920, 1080},
    {78, 1920, 1080}, {79, 1680, 720}, {80, 1680, 720}, {81, 1680, 720},
    {82, 1680, 720}, {83, 1680, 720}, {84, 1680, 720}, {85, 1680, 720},
    {86, 2560, 1080}, {87, 2560, 1080}, {88, 2560, 1080}, {89, 2560, 1080},
    {90, 2560, 1080}, {91, 2560, 1080}, {92, 2560, 1080}, {93, 3840, 2160},
    {94, 3840, 2160}, {95, 3840, 2160}, {96, 3840, 2160}, {97, 3840, 2160},
    {98, 4096, 2160}, {99, 4096, 2160}, {100, 4096, 2160}, {101, 4096, 2160},
    {102, 4096, 2160}, {103, 3840, 2160}, {104, 3840, 2160}, {105, 3840, 2160},
    {106, 3840, 2160}, {107, 3840, 2160}, {108, 1280, 720}, {109, 1280, 720},
    {110, 1680, 720}, {111, 1920, 1080}, {112, 1920, 1080}, {113, 2560, 1080},
    {114, 3840, 2160}, {115, 4096, 2160}, {116, 3840, 2160}, {117, 3840, 2160},
    {118, 3840, 2160}, {119, 3840, 2160}, {120, 3840, 2160}, {121, 5120, 2160},
    {122, 5120, 2160}, {123, 5120, 2160}, {124, 5120, 2160}, {125, 5120, 2160},
    {126, 5120, 2160}, {127, 5120, 2160}, {193, 5120, 2160}, {194, 7680, 4320},
    {195, 7680, 4320}, {196, 7680, 4320}, {197, 7680, 4320}, {198, 7680, 4320},
    {199, 7680, 4320}, {200, 7680, 4320}, {201, 7680, 4320}, {202, 7680, 4320},
    {203, 7680, 4320}, {204, 7680, 4320}, {205, 7680, 4320}, {206, 7680, 4320},
    {207, 7680, 4320}, {208, 7680, 4320}, {209, 7680, 4320}, {210, 10240, 4320},
    {211, 10240, 4320}, {212, 10240, 4320}, {213, 10240, 4320}, {214, 10240, 4320},
    {215, 10240, 4320}, {216, 10240, 4320}, {217, 10240, 4320}, {218, 4096, 2160},
    {219, 4096, 2160},
};
// clang-format on

// The mode list being built: each resolution once, at most Max of them.
typedef struct _EDID_MODE_LIST
{
    PVIOGPU_DISP_MODE Modes;
    ULONG Count;
    ULONG Max;
} EDID_MODE_LIST;

static void AddMode(EDID_MODE_LIST *list, ULONG width, ULONG height)
{
    if (width == 0 || height == 0 || width > MAXUSHORT || height > MAXUSHORT)
    {
        return;
    }
    for (ULONG i = 0; i < list->Count; i++)
    {
        if (list->Modes[i].XResolution == width && list->Modes[i].YResolution == height)
        {
            return;
        }
    }
    if (list->Count < list->Max)
    {
        list->Modes[list->Count].XResolution = (USHORT)width;
        list->Modes[list->Count].YResolution = (USHORT)height;
        list->Count++;
    }
}

static void AddBitModes(EDID_MODE_LIST *list, const UCHAR *bytes, const EDID_BIT_MODE *table, ULONG count)
{
    for (ULONG i = 0; i < count; i++)
    {
        if (bytes[table[i].Byte] & (1 << table[i].Bit))
        {
            AddMode(list, table[i].Width, table[i].Height);
        }
    }
}

// A 2-byte standard timing. Aspect ratio 00 means 16:10 from EDID 1.3 on and 1:1 before.
static void AddStandardTiming(EDID_MODE_LIST *list, const UCHAR *timing, UCHAR revision)
{
    if (timing[0] == 0x00 || (timing[0] == 0x01 && timing[1] == 0x01))
    {
        return; // unused
    }
    ULONG width = (timing[0] + 31) * 8;
    ULONG height;
    switch (timing[1] >> 6)
    {
        case 0:
            height = revision >= 3 ? width * 10 / 16 : width;
            break;
        case 1:
            height = width * 3 / 4;
            break;
        case 2:
            height = width * 4 / 5;
            break;
        default:
            height = width * 9 / 16;
            break;
    }
    AddMode(list, width, height);
}

// An 18-byte detailed timing descriptor. False for a display descriptor, whose pixel clock is 0.
static bool GetDetailedTiming(const UCHAR *descriptor, VIOGPU_DISP_MODE *mode, bool *interlaced)
{
    if (descriptor[0] == 0 && descriptor[1] == 0)
    {
        return false;
    }
    mode->XResolution = (USHORT)(descriptor[2] | ((descriptor[4] & 0xF0) << 4));
    mode->YResolution = (USHORT)(descriptor[5] | ((descriptor[7] & 0xF0) << 4));
    *interlaced = (descriptor[17] & 0x80) != 0;
    return true;
}

static void ParseBaseBlock(EDID_MODE_LIST *list, const UCHAR *base, VIOGPU_DISP_MODE *preferred)
{
    UCHAR revision = base[EDID_OFFSET_REVISION];

    AddBitModes(list, base, EstablishedModes, ARRAYSIZE(EstablishedModes));
    for (ULONG offset = 38; offset < 54; offset += 2)
    {
        AddStandardTiming(list, base + offset, revision);
    }

    for (ULONG offset = 54; offset < EDID_OFFSET_EXTENSIONS; offset += DETAILED_DESCRIPTOR_SIZE)
    {
        const UCHAR *descriptor = base + offset;
        VIOGPU_DISP_MODE mode = {0};
        bool interlaced = false;

        if (GetDetailedTiming(descriptor, &mode, &interlaced))
        {
            if (interlaced)
            {
                continue;
            }
            // The first detailed timing is the preferred one: always from EDID 1.4 on, before that when the
            // feature byte says so.
            if (offset == 54 && (revision >= 4 || (base[EDID_OFFSET_FEATURES] & 0x02)))
            {
                *preferred = mode;
            }
            AddMode(list, mode.XResolution, mode.YResolution);
            continue;
        }
        switch (descriptor[3])
        {
            case 0xF7: // Established Timings III
                AddBitModes(list, descriptor, EstablishedModes3, ARRAYSIZE(EstablishedModes3));
                break;
            case 0xFA: // six more standard timings
                for (ULONG i = 5; i < 17; i += 2)
                {
                    AddStandardTiming(list, descriptor + i, revision);
                }
                break;
        }
    }
}

static void AddVic(EDID_MODE_LIST *list, UCHAR svd)
{
    // SVDs 129-192 carry VICs 1-64 with the native flag in bit 7; every other value is the VIC itself.
    UCHAR vic = (svd >= 129 && svd <= 192) ? (svd & 0x7F) : svd;
    for (ULONG i = 0; i < ARRAYSIZE(VicModes); i++)
    {
        if (VicModes[i].Vic == vic)
        {
            AddMode(list, VicModes[i].Width, VicModes[i].Height);
            return;
        }
    }
}

static void ParseCtaExtension(EDID_MODE_LIST *list, const UCHAR *ext)
{
    // Byte 2: where the detailed timings start. 0: neither data blocks nor detailed timings; 4: no data blocks;
    // 127: data blocks up to the checksum and no detailed timings.
    ULONG dtdOffset = ext[2];
    if (dtdOffset < 4 || dtdOffset > EDID_OFFSET_CHECKSUM)
    {
        return;
    }
    // The data block collection exists from revision 3 on: a header byte (tag in bits 7-5, length in bits 4-0) and
    // the payload.
    if (ext[1] >= 3)
    {
        for (ULONG offset = 4; offset < dtdOffset;)
        {
            ULONG tag = ext[offset] >> 5;
            ULONG length = ext[offset] & 0x1F;
            if (offset + 1 + length > dtdOffset)
            {
                break;
            }
            if (tag == CTA_VIDEO_DATA_BLOCK)
            {
                for (ULONG i = 1; i <= length; i++)
                {
                    AddVic(list, ext[offset + i]);
                }
            }
            offset += 1 + length;
        }
    }
    for (ULONG offset = dtdOffset; offset + DETAILED_DESCRIPTOR_SIZE <= EDID_OFFSET_CHECKSUM;
         offset += DETAILED_DESCRIPTOR_SIZE)
    {
        VIOGPU_DISP_MODE mode = {0};
        bool interlaced = false;
        if (!GetDetailedTiming(ext + offset, &mode, &interlaced))
        {
            break; // padding after the last one
        }
        if (!interlaced)
        {
            AddMode(list, mode.XResolution, mode.YResolution);
        }
    }
}

static void ParseDisplayIdExtension(EDID_MODE_LIST *list, const UCHAR *ext, VIOGPU_DISP_MODE *preferred)
{
    // The section: version, payload length, product type, extension count, the data blocks, a checksum byte.
    ULONG end = 5 + ext[2];
    if (end >= EDID_OFFSET_CHECKSUM)
    {
        return;
    }
    // The section has a checksum of its own, from the version byte to the checksum byte.
    UCHAR sum = 0;
    for (ULONG i = 1; i <= end; i++)
    {
        sum += ext[i];
    }
    if (sum != 0)
    {
        return;
    }
    // A data block: tag, revision, payload length, payload.
    for (ULONG offset = 5; offset + 3 <= end;)
    {
        UCHAR tag = ext[offset];
        ULONG length = ext[offset + 2];
        if (offset + 3 + length > end)
        {
            break;
        }
        if (tag == DISPLAYID_TYPE_I_TIMING || tag == DISPLAYID_TYPE_VII_TIMING)
        {
            for (ULONG t = offset + 3; t + DISPLAYID_TIMING_SIZE <= offset + 3 + length; t += DISPLAYID_TIMING_SIZE)
            {
                const UCHAR *timing = ext + t;
                // Byte 3: bit 7 preferred, bit 4 interlaced. Active pixels and lines are stored minus 1.
                if (timing[3] & 0x10)
                {
                    continue;
                }
                ULONG width = (timing[4] | (timing[5] << 8)) + 1;
                ULONG height = (timing[12] | (timing[13] << 8)) + 1;
                if ((timing[3] & 0x80) && preferred->XResolution == 0 && width <= MAXUSHORT && height <= MAXUSHORT)
                {
                    preferred->XResolution = (USHORT)width;
                    preferred->YResolution = (USHORT)height;
                }
                AddMode(list, width, height);
            }
        }
        offset += 3 + length;
    }
}

static bool BlockChecksumOk(const UCHAR *block)
{
    UCHAR sum = 0;
    for (ULONG i = 0; i < EDID_BLOCK_SIZE; i++)
    {
        sum += block[i];
    }
    return sum == 0;
}

ULONG EdidValidSize(const UCHAR *edid, ULONG size)
{
    if (edid == NULL || size < EDID_BLOCK_SIZE ||
        RtlCompareMemory(edid, EdidHeader, sizeof(EdidHeader)) != sizeof(EdidHeader) ||
        edid[EDID_OFFSET_VERSION] != 1 || !BlockChecksumOk(edid))
    {
        return 0;
    }
    ULONG blocks = 1;
    while (blocks <= edid[EDID_OFFSET_EXTENSIONS] && (blocks + 1) * EDID_BLOCK_SIZE <= size &&
           BlockChecksumOk(edid + blocks * EDID_BLOCK_SIZE))
    {
        blocks++;
    }
    return blocks * EDID_BLOCK_SIZE;
}

void EdidSetChecksum(UCHAR *block)
{
    UCHAR sum = 0;
    for (ULONG i = 0; i < EDID_OFFSET_CHECKSUM; i++)
    {
        sum += block[i];
    }
    block[EDID_OFFSET_CHECKSUM] = (UCHAR)(0x100 - sum);
}

ULONG EdidGetModes(const UCHAR *edid, ULONG size, PVIOGPU_DISP_MODE modes, ULONG maxModes, PVIOGPU_DISP_MODE preferred)
{
    EDID_MODE_LIST list = {modes, 0, maxModes};
    VIOGPU_DISP_MODE displayIdPreferred = {0};

    preferred->XResolution = 0;
    preferred->YResolution = 0;
    if (size < EDID_BLOCK_SIZE)
    {
        return 0;
    }
    ParseBaseBlock(&list, edid, preferred);
    for (ULONG offset = EDID_BLOCK_SIZE; offset + EDID_BLOCK_SIZE <= size; offset += EDID_BLOCK_SIZE)
    {
        switch (edid[offset])
        {
            case CTA_EXTENSION_TAG:
                ParseCtaExtension(&list, edid + offset);
                break;
            case DISPLAYID_EXTENSION_TAG:
                ParseDisplayIdExtension(&list, edid + offset, &displayIdPreferred);
                break;
        }
    }
    // A display too large for a detailed timing descriptor states its preferred timing in DisplayID.
    if (preferred->XResolution == 0)
    {
        *preferred = displayIdPreferred;
    }
    return list.Count;
}

VIOGPU_DISP_MODE gpu_disp_modes[64] = {0};

// clang-format off
UCHAR g_gpu_edid[EDID_V1_BLOCK_SIZE] = {
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF ,0xFF, 0x00, // Header
    0x49, 0x14,                                     // Manufacturef Id
    0x34, 0x12,                                     // Manufacturef product code
    0x00, 0x00, 0x00, 0x00,                         // serial number
    0xff, 0x1d,                                     // year of manufacture
    0x01,                                           // EDID version
    0x04,                                           // EDID revision
    0xa3,                                           // VideoInputDefinition digital, 8-bit, HDMI
    0x00,                                           //MaximumHorizontalImageSize
    0x00,                                           //MaximumVerticallImageSize
    0x78,                                           //DisplayTransferCharacteristics
    0x22,                                           //FeatureSupport
    0xEE, 0x95, 0xA3, 0x54, 0x4C,                   //ColorCharacteristics
    0x99, 0x26, 0x0F, 0x50, 0x54,
    0x00, 0x00,                                     //EstablishedTimings
    0x00,                                           //ManufacturerTimings
    0x01, 0x01,                                     //StandardTimings[8]
    0x01, 0x01,
    0x01, 0x01,
    0x01, 0x01,
    0x01, 0x01,
    0x01, 0x01,
    0x01, 0x01,
    0x01, 0x01,
    0x6c, 0x20, 0x80, 0x30, 0x42, 0x00,             // Descriptor 1
    0x32, 0x30, 0x40, 0xc0, 0x13, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x1e,
    0x00, 0x00, 0x00, 0xFD, 0x00, 0x32,             // Descriptor 2
    0x7d, 0x1e, 0xa0, 0x78, 0x01, 0x0a,
    0x20, 0x20 ,0x20, 0x20, 0x20, 0x20,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00,             // Descriptor 3
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00,             // Descriptor 4
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,                                           // Number of Extentions
    0x00                                            // CheckSum
};
// clang-format on
