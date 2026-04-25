// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// AArch64 exception vector table for EL1.
// 4 groups x 4 vectors, each 0x80 bytes aligned.
//
// We handle:
//   - Synchronous EL1 (svc from user-space resource managers)
//   - IRQ EL1 (timer tick → kernel.tick())

.section .text
.balign 0x800
.global vectors

vectors:
// ─── Current EL, SP_EL0 (unused) ───────────────────────────────────────────
.balign 0x80
    b       .unhandled
.balign 0x80
    b       .unhandled
.balign 0x80
    b       .unhandled
.balign 0x80
    b       .unhandled

// ─── Current EL, SP_ELx ────────────────────────────────────────────────────
.balign 0x80
    b       sync_handler        // Synchronous (svc trap)
.balign 0x80
    b       irq_handler         // IRQ (timer)
.balign 0x80
    b       .unhandled          // FIQ
.balign 0x80
    b       .unhandled          // SError

// ─── Lower EL, AArch64 ─────────────────────────────────────────────────────
.balign 0x80
    b       sync_handler
.balign 0x80
    b       irq_handler
.balign 0x80
    b       .unhandled
.balign 0x80
    b       .unhandled

// ─── Lower EL, AArch32 (not supported) ─────────────────────────────────────
.balign 0x80
    b       .unhandled
.balign 0x80
    b       .unhandled
.balign 0x80
    b       .unhandled
.balign 0x80
    b       .unhandled

// ─── Handlers ───────────────────────────────────────────────────────────────

.unhandled:
    wfe
    b       .unhandled

sync_handler:
    // Save caller-saved registers
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x29, x30, [sp, #-16]!

    // x8 = syscall number (QNX convention: syscall ID in x8)
    // x0-x5 = arguments
    bl      syscall_dispatch

    // Restore
    ldp     x29, x30, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    eret

irq_handler:
    stp     x0, x1, [sp, #-16]!
    stp     x29, x30, [sp, #-16]!

    bl      timer_tick_handler

    ldp     x29, x30, [sp], #16
    ldp     x0, x1, [sp], #16
    eret
