/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
 */

#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include "qnxmicro_syscall.h"

off_t
lseek(int fd, off_t offset, int whence)
{
    long r = __qnxmicro_syscall3(SYSCALL_LSEEK, (uintptr_t)fd, (uintptr_t)offset, (uintptr_t)whence);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (off_t)r;
}
