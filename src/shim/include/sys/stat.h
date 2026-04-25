// SPDX-License-Identifier: BSD-2-Clause
/* Bare-metal shim: sys/stat.h — extend picolibc's stat with stat64 alias */
#ifndef _SHIM_SYS_STAT_H
#define _SHIM_SYS_STAT_H

#include_next <sys/stat.h>

/* dash uses stat64 — alias to stat (no large-file distinction on bare-metal) */
#define stat64 stat
#define fstat64 fstat
#define lstat64 lstat

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#endif
