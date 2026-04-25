/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
 */

#include <unistd.h>
#include <stdint.h>
#include "qnxmicro_syscall.h"

int
isatty(int fd)
{
    long r = __qnxmicro_syscall3(SYSCALL_ISATTY, (uintptr_t)fd, 0, 0);
    return r > 0 ? 1 : 0;
}
