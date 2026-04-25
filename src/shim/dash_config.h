// SPDX-License-Identifier: BSD-2-Clause
/* Bare-metal config.h override for dash.
 * Replaces the autoconf-generated config.h from macOS.
 * Disables features that need POSIX runtime support we don't have yet.
 */
#ifndef DASH_BAREMETAL_CONFIG_H
#define DASH_BAREMETAL_CONFIG_H

#define PACKAGE_NAME "dash"
#define PACKAGE_VERSION "0.5.13"

/* Features we provide */
#define HAVE_ISALPHA 1
#define HAVE_PATHS_H 1
#define HAVE_MEMPCPY 0
#define HAVE_STPCPY 0
#define HAVE_STRCHRNUL 0
#define HAVE_STRSIGNAL 0
#define HAVE_STRTOD 1
#define HAVE_STRTOIMAX 0
#define HAVE_STRTOUMAX 0
#define HAVE_BSEARCH 1
#define HAVE_KILLPG 0
#define HAVE_MEMRCHR 0
#define HAVE_F_DUPFD_CLOEXEC 0
#define USE_TEE 0
#define USE_MEMFD_CREATE 0

/* No dirent64 — alias to dirent */
#define dirent64 dirent
#define HAVE_SYSCONF 0
#define HAVE_GETRLIMIT 0
#define HAVE_GETPWNAM 0

/* Features we do NOT provide — the key fix */
/* #undef HAVE_SIGSETMASK */
/* #undef HAVE_FNMATCH */
/* #undef HAVE_GLOB */
/* #undef HAVE_ALLOCA_H */
/* #undef HAVE_GETPWNAM */

/* Signal handling — use sigprocmask path, not sigsetmask */
#define HAVE_SIGPROCMASK 1

/* Types */
#define HAVE_DECL_STRTOIMAX 0
#define HAVE_DECL_STRTOUMAX 0

#endif
