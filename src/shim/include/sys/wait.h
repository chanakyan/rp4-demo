// SPDX-License-Identifier: BSD-2-Clause
/* Bare-metal stub: sys/wait.h */
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H
#include <sys/types.h>

#define WNOHANG   1
#define WUNTRACED 2

#define WIFEXITED(s)   (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) (((s) & 0x7f) > 0 && ((s) & 0x7f) < 0x7f)
#define WTERMSIG(s)    ((s) & 0x7f)
#define WIFSTOPPED(s)  (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)    (((s) >> 8) & 0xff)

typedef int pid_t;
pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait3(int *status, int options, void *rusage);

#endif
