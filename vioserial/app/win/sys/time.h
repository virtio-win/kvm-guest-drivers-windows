#ifndef __win_sys_time_h__
#define __win_sys_time_h__

#define clock_gettime(x, data) ((timespec_get(data, TIME_UTC) != 0) ? 0 : 1)

#endif