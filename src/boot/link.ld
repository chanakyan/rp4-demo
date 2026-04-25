/* SPDX-License-Identifier: BSD-2-Clause */
/* Linker script for RPi4 bare-metal (kernel8.img loaded at 0x80000) */

ENTRY(_start)

SECTIONS
{
    . = 0x80000;

    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4096) {
        *(.data .data.*)
    }

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Arena lives in BSS (1 MB), stack sits above it */
    . = ALIGN(4096);
    . += 0x20000;  /* 128 KB stack (above the 1 MB arena in BSS) */
    _stack_top = .;
}
