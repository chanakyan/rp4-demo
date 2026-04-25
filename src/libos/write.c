/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
 *
 * libos/qnxmicro/write.c — POSIX write() for qnx-micro kernel
 *
 * Sends a write message to the resource manager that owns this fd.
 * fd is a connection ID. The kernel routes the message to the correct channel.
 */

#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include "qnxmicro_syscall.h"

ssize_t
write(int fd, const void *buf, size_t count)
{
    ssize_t r = __qnxmicro_syscall3(SYSCALL_WRITE, (uintptr_t)fd, (uintptr_t)buf, count);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return r;
}
