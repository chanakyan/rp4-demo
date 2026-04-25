// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// _start — bare-metal entry point for BCM2711 (Cortex-A72, AArch64)
// RPi4 firmware drops us at EL2. We drop to EL1, set up stack, call kernel_main.

.section .text.boot
.global _start

_start:
    // ─── Park secondary cores (only core 0 boots) ──────────────────────────
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbnz    x0, .park

    // ─── Drop from EL2 to EL1 ─────────────────────────────────────────────���
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    .at_el1

    // Configure EL1: AArch64, no trapping of FP/SIMD
    mov     x0, #(1 << 31)         // RW=1 (EL1 is AArch64)
    orr     x0, x0, #(1 << 1)     // SWIO
    msr     hcr_el2, x0

    // Disable EL2 timer traps
    mov     x0, #0x33FF
    msr     cptr_el2, x0

    // SPSR: EL1h (SP_EL1), interrupts masked
    mov     x0, #0x3C5
    msr     spsr_el2, x0

    // Return address: .at_el1
    adr     x0, .at_el1
    msr     elr_el2, x0
    eret

.at_el1:
    // ─── Set up EL1 stack ───────────────────────────────────────────────────
    ldr     x0, =_stack_top
    mov     sp, x0

    // ─── Zero BSS ───────────────────────────────────────────────────────────
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
.zero_bss:
    cmp     x0, x1
    b.ge    .bss_done
    str     xzr, [x0], #8
    b       .zero_bss
.bss_done:

    // ─── Install exception vector table ─────────────────────────────────────
    ldr     x0, =vectors
    msr     vbar_el1, x0

    // ─── Enable FP/SIMD (CPACR_EL1) ────────────────────────────────────────
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    // ─── Jump to C++ ────────────────────────────────────────────────────────
    bl      kernel_main

.park:
    wfe
    b       .park
