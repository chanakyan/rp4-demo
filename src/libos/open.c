/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
 *
 * libos/qnxmicro/open.c — POSIX open() for qnx-micro kernel
 *
 * Resolves pathname via kernel namespace (MsgSend to procnto equivalent),
 * returns a connection ID as the file descriptor.
 */

#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include "qnxmicro_syscall.h"

int
open(const char *path, int flags, ...)
{
    int fd = __qnxmicro_syscall3(SYSCALL_OPEN, (uintptr_t)path, (uintptr_t)flags, 0);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    return fd;
}
