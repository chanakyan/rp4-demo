/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
 *
 * qnxmicro_syscall.h — syscall interface for qnx-micro kernel
 *
 * On AArch64: svc #0 with syscall number in x8, args in x0-x5.
 * Returns result in x0 (negative = -errno).
 *
 * These map directly to the kernel's SyscallId enum:
 *   SYSCALL_OPEN    → namespace resolve + connect_attach
 *   SYSCALL_READ    → msg_send (read request to resource manager)
 *   SYSCALL_WRITE   → msg_send (write request to resource manager)
 *   SYSCALL_CLOSE   → disconnect
 *   SYSCALL_IOCTL   → msg_send (devctl to resource manager)
 */

#ifndef QNXMICRO_SYSCALL_H
#define QNXMICRO_SYSCALL_H

#include <stdint.h>

/* Syscall numbers — must match kernel's dispatch table in vectors.S */
#define SYSCALL_OPEN    0
#define SYSCALL_READ    1
#define SYSCALL_WRITE   2
#define SYSCALL_CLOSE   3
#define SYSCALL_IOCTL   4
#define SYSCALL_MMAP    5
#define SYSCALL_MUNMAP  6
#define SYSCALL_FORK    7
#define SYSCALL_EXEC    8
#define SYSCALL_EXIT    9
#define SYSCALL_WAIT    10
#define SYSCALL_PIPE    11
#define SYSCALL_DUP     12
#define SYSCALL_LSEEK   13
#define SYSCALL_STAT    14
#define SYSCALL_ISATTY  15

static inline long
__qnxmicro_syscall3(int num, uintptr_t a0, uintptr_t a1, uintptr_t a2)
{
#ifdef __aarch64__
    register long x8 __asm__("x8") = num;
    register long x0 __asm__("x0") = (long)a0;
    register long x1 __asm__("x1") = (long)a1;
    register long x2 __asm__("x2") = (long)a2;

    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x8)
        : "memory"
    );
    return x0;
#else
    /* Host build: stub — return ENOSYS */
    (void)num; (void)a0; (void)a1; (void)a2;
    return -38; /* ENOSYS */
#endif
}

#endif /* QNXMICRO_SYSCALL_H */
