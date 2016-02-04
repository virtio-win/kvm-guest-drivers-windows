/*
 * Copyright (C) 2015-2016 Red Hat, Inc.
 *
 * This software is licensed under the GNU General Public License,
 * version 2 (GPLv2) (see COPYING for details), subject to the following
 * clarification.
 *
 * With respect to binaries built using the Microsoft(R) Windows Driver
 * Kit (WDK), GPLv2 does not extend to any code contained in or derived
 * from the WDK ("WDK Code"). As to WDK Code, by using or distributing
 * such binaries you agree to be bound by the Microsoft Software License
 * Terms for the WDK. All WDK Code is considered by the GPLv2 licensors
 * to qualify for the special exception stated in section 3 of GPLv2
 * (commonly known as the system library exception).
 *
 * There is NO WARRANTY for this software, express or implied,
 * including the implied warranties of NON-INFRINGEMENT, TITLE,
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Refer to the LICENSE file for full details of the license.
 *
 * Written By: Gal Hammer <ghammer@redhat.com>
 *
 */

//
// Define the tracing flags.
//
// Tracing GUID - 5eeabb8c-be9a-40d0-99fd-86f2a0b21378
//

#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID( \
        PVPanicTraceGuid, (5eeabb8c,be9a,40d0,99fd,86f2a0b21378), \
        WPP_DEFINE_BIT(DBG_ALL)         \
        WPP_DEFINE_BIT(DBG_INIT)        \
        WPP_DEFINE_BIT(DBG_POWER)       \
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
// This comment block is scanned by the trace preprocessor to define our
// Trace function.
//
// begin_wpp config
// FUNC Trace{FLAG=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...);
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp
//

