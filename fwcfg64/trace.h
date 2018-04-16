/*
 * This file contains trace and debugging related definitions
 *
 * Copyright (C) 2018 Virtuozzo International GmbH
 *
 */

//
// Tracing GUID - 2C697E85-A518-427D-9D00-F95368D51E17
//

#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID( \
        VmcoreinfoTraceGuid, (2C697E85,A518,427D,9D00,F95368D51E17), \
        WPP_DEFINE_BIT(DBG_ALL)        \
        WPP_DEFINE_BIT(DBG_INIT)       \
        WPP_DEFINE_BIT(DBG_PNP)        \
        WPP_DEFINE_BIT(DBG_POWER)      \
    )

#define WPP_FLAG_LEVEL_LOGGER(flag, level) \
    WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level) \
    (WPP_LEVEL_ENABLED(flag) && WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) \
    WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) \
    (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

//
// begin_wpp config
// FUNC Trace{FLAG=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...);
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp
//
