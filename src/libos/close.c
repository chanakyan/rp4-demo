/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
 *
 * libos/qnxmicro/close.c — POSIX close() for qnx-micro kernel
 *
 * Disconnects from the resource manager. Releases the connection ID.
 */

#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include "qnxmicro_syscall.h"

int
close(int fd)
{
    int r = (int)__qnxmicro_syscall3(SYSCALL_CLOSE, (uintptr_t)fd, 0, 0);
    if (r < 0) {
        errno = -r;
        return -1;
    }
    return 0;
}
