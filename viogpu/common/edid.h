#pragma once

#include "viogpu.h"

typedef struct _ESTABLISHED_TIMINGS_1_2 {
    USHORT Timing_800x600_60 : 1;
    USHORT Timing_800x600_56 : 1;
    USHORT Timing_640x480_75 : 1;
    USHORT Timing_640x480_72 : 1;
    USHORT Timing_640x480_67 : 1;
    USHORT Timing_640x480_60 : 1;
    USHORT Timing_720x400_88 : 1;
    USHORT Timing_720x400_70 : 1;

    USHORT Timing_1280x1024_75 : 1;
    USHORT Timing_1024x768_75 : 1;
    USHORT Timing_1024x768_70 : 1;
    USHORT Timing_1024x768_60 : 1;
    USHORT Timing_1024x768_87 : 1;
    USHORT Timing_832x624_75 : 1;
    USHORT Timing_800x600_75 : 1;
    USHORT Timing_800x600_72 : 1;
} ESTABLISHED_TIMINGS_1_2, * PESTABLISHED_TIMINGS_1_2;


typedef struct _ESTABLISHED_TIMINGS_3 {
    UCHAR Timing_1152x864_75 : 1;
    UCHAR Timing_1024x768_85 : 1;
    UCHAR Timing_800x600_85 : 1;
    UCHAR Timing_848x480_60 : 1;
    UCHAR Timing_640x480_85 : 1;
    UCHAR Timing_720x400_85 : 1;
    UCHAR Timing_640x400_85 : 1;
    UCHAR Timing_640x350_85 : 1;

    UCHAR Timing_1280x1024_85 : 1;
    UCHAR Timing_1280x1024_60 : 1;
    UCHAR Timing_1280x960_85 : 1;
    UCHAR Timing_1280x960_60 : 1;
    UCHAR Timing_1280x768_85 : 1;
    UCHAR Timing_1280x768_75 : 1;
    UCHAR Timing_1280x768_60 : 1;
    UCHAR Timing_1280x768_60_RB : 1;

    UCHAR Timing_1400x1050_75 : 1;
    UCHAR Timing_1400x1050_60 : 1;
    UCHAR Timing_1400x1050_60_RB : 1;
    UCHAR Timing_1440x900_85 : 1;
    UCHAR Timing_1440x900_75 : 1;
    UCHAR Timing_1440x900_60 : 1;
    UCHAR Timing_1440x900_60_RB : 1;
    UCHAR Timing_1360x768_60 : 1;

    UCHAR Timing_1600x1200_70 : 1;
    UCHAR Timing_1600x1200_65 : 1;
    UCHAR Timing_1600x1200_60 : 1;
    UCHAR Timing_1680x1050_85 : 1;
    UCHAR Timing_1680x1050_75 : 1;
    UCHAR Timing_1680x1050_60 : 1;
    UCHAR Timing_1680x1050_60_RB : 1;
    UCHAR Timing_1400x1050_85 : 1;

    UCHAR Timing_1920x1200_60 : 1;
    UCHAR Timing_1920x1200_60_RB : 1;
    UCHAR Timing_1856x1392_75 : 1;
    UCHAR Timing_1856x1392_60 : 1;
    UCHAR Timing_1792x1344_75 : 1;
    UCHAR Timing_1792x1344_60 : 1;
    UCHAR Timing_1600x1200_85 : 1;
    UCHAR Timing_1600x1200_75 : 1;

    UCHAR Timing_Reserved : 4;
    UCHAR Timing_1920x1440_75 : 1;
    UCHAR Timing_1920x1440_60 : 1;
    UCHAR Timing_1920x1200_85 : 1;
    UCHAR Timing_1920x1200_75 : 1;
} ESTABLISHED_TIMINGS_3, * PESTABLISHED_TIMINGS_3;

#pragma pack(push)
#pragma pack(1)

typedef union _VIDEO_INPUT_DEFINITION {
    typedef struct _DIGITAL {
        UCHAR DviStandard : 4;
        UCHAR ColorBitDepth : 3;
        UCHAR Digital : 1;
    } DIGITAL;
    typedef struct _ANALOG {
        UCHAR VSyncSerration : 1;
        UCHAR GreenVideoSync : 1;
        UCHAR VompositeSync : 1;
        UCHAR SeparateSync : 1;
        UCHAR BlankToBlackSetup : 1;
        UCHAR SignalLevelStandard : 2;
        UCHAR Digital : 1;
    } ANALOG;
} VIDEO_INPUT_DEFINITION, * PVIDEO_INPUT_DEFINITION;

typedef struct _MANUFACTURER_TIMINGS {
    UCHAR Reserved : 7;
    UCHAR Timing_1152x870_75 : 1;
} MANUFACTURER_TIMINGS, * PMANUFACTURER_TIMINGS;

typedef struct _STANDARD_TIMING_DESCRIPTOR {
    UCHAR HorizontalActivePixels;
    UCHAR RefreshRate : 6;
    UCHAR ImageAspectRatio : 2;
} STANDARD_TIMING_DESCRIPTOR, * PSTANDARD_TIMING_DESCRIPTOR;

typedef struct _EDID_DETAILED_DESCRIPTOR {
    USHORT PixelClock;
    UCHAR HorizontalActiveLow;
    UCHAR HorizontalBlankingLow;
    UCHAR HorizontalBlankingHigh : 4;
    UCHAR horizontalActiveHigh : 4;
    UCHAR VerticalActiveLow;
    UCHAR VerticalBlankingLow;
    UCHAR VerticalBlankingHigh : 4;
    UCHAR VerticalActiveHigh : 4;
    UCHAR HorizontalSyncOffsetLow;
    UCHAR HorizontalSyncPulseWidthLow;
    UCHAR VerticalSyncPulseWidthLow : 4;
    UCHAR VerticalSyncOffsetLow : 4;
    UCHAR VerticalSyncPulseWidthHigh : 2;
    UCHAR VerticalSyncOffsetHigh : 2;
    UCHAR HorizontalSyncPulseWidthHigh : 2;
    UCHAR HorizontalSyncOffsetHigh : 2;
    UCHAR HorizontalImageSizeLow;
    UCHAR VerticalImageSizeLow;
    UCHAR VerticalImageSizeHigh : 4;
    UCHAR HorizontalImageSizeHigh : 4;
    UCHAR HorizontalBorder;
    UCHAR VerticalBorder;
    UCHAR StereoModeLow : 1;
    UCHAR SignalPulsePolarity : 1;
    UCHAR SignalSerrationPolarity : 1;
    UCHAR SignalSync : 2;
    UCHAR StereoModeHigh : 2;
    UCHAR Interlaced : 1;
}EDID_DETAILED_DESCRIPTOR, * PEDID_DETAILED_DESCRIPTOR;

typedef struct _EDID_DISPLAY_DESCRIPTOR {
    USHORT Indicator;
    UCHAR Reserved0;
    UCHAR Tag;
    UCHAR Reserved1;
    UCHAR Data[13];
}EDID_DISPLAY_DESCRIPTOR, * PEDID_DISPLAY_DESCRIPTOR;


typedef struct _EDID_DATA_V1 {
    UCHAR Header[8];
    UCHAR VendorID[2];
    UCHAR ProductID[2];
    UCHAR SerialNumber[4];
    UCHAR WeekYearMFG[2];
    UCHAR Version[1];
    UCHAR Revision[1];
    VIDEO_INPUT_DEFINITION VideoInputDefinition[1];
    UCHAR  MaximumHorizontalImageSize[1];
    UCHAR  MaximumVerticallImageSize[1];
    UCHAR  DisplayTransferCharacteristics[1];
    FEATURES_SUPPORT FeaturesSupport;
    COLOR_CHARACTERISTICS ColorCharacteristics;
    ESTABLISHED_TIMINGS_1_2 EstablishedTimings;
    MANUFACTURER_TIMINGS ManufacturerTimings;
    STANDARD_TIMING_DESCRIPTOR StandardTimings[8];
    EDID_DETAILED_DESCRIPTOR EDIDDetailedTimings[4];
    UCHAR ExtensionFlag[1];
    UCHAR Checksum[1];
} EDID_DATA_V1, * PEDID_DATA_V1;

typedef struct _EDID_CTA_861 {
    UCHAR ExtentionTag[1];
    UCHAR Revision[1];
    UCHAR DTDBegin[1];
    UCHAR DTDsNumber[1];
    UCHAR Data[122];
    UCHAR Checksum[1];
} EDID_CTA_861, * PEDID_CTA_861;

typedef struct  _VIC_MODE {
    USHORT      Index;
    VIOGPU_DISP_MODE Resolution;
} VIC_MODE, * PVIC_MODE;

#pragma pack(pop)

typedef enum _IMG_ASPECT_RATIO
{
    AR_16_10 = 0,
    AR_4_3,
    AR_5_4,
    AR_16_9
}IMG_ASPECT_RATIO;


bool GetStandardTimingResolution(PSTANDARD_TIMING_DESCRIPTOR desc, PVIOGPU_DISP_MODE mode);
bool GetVICResolution(USHORT idx, PVIOGPU_DISP_MODE mode);

extern VIOGPU_DISP_MODE gpu_disp_modes[64];
extern UCHAR g_gpu_edid[EDID_V1_BLOCK_SIZE];
