// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// kernel_main — C++ entry from _start.
// Initializes hardware, starts the kernel, launches resource managers.
// This is the bare-metal glue between the kernel model and real hardware.

import std;
import qnx.kernel;
import rp4.hal;

using namespace qnx;
using namespace qnx::kernel;
using namespace rp4::hal;

// Forward declarations for vector table handlers + shim support
extern "C" {
    void kernel_main();
    void syscall_dispatch();
    void timer_tick_handler();
    void uart_putc_raw(char c);
}

/// @brief C-linkage UART putc for libc_shim.c (cannot import C++ modules).
void uart_putc_raw(char c) {
    uart::putc(c);
}

static Kernel k;

/// @brief Timer IRQ handler — drives preemptive scheduling.
/// No PPS, no allocation, no C++ runtime. Just ack + tick.
void timer_tick_handler() {
    timer::ack();
    k.tick();
}

/// @brief SVC trap handler — dispatches kernel syscalls from user-space apps.
/// Syscall number in x8, args in x0-x5 (AArch64 convention).
void syscall_dispatch() {
    // TODO: read x8 for SyscallId, dispatch to kernel subsystems
    // For now, placeholder — apps run cooperatively in host build
}

/// @brief Bare-metal entry point after stack/BSS/vectors are set up.
void kernel_main() {
    // ─── Hardware init ──────────────────────────────────────────────────────
    uart::puts("rp4: boot\n");

    // I2C + DAC (HiFi DAC Pro — PCM5122 over I2C1)
    i2c::init();
    if (dac::init()) {
        uart::puts("rp4: DAC ok (PCM5122 @ 0x4D)\n");
    } else {
        uart::puts("rp4: DAC not found (no HAT?)\n");
    }

    // I2S audio output
    gpio::setup_i2s_pins();
    i2s::init_48khz_stereo();

    // Timer: program tick, route to Core 0 IRQ, unmask at CPU
    timer::set_next_tick_ms(1);
    irq::enable_timer_irq();
    irq::unmask();

    uart::puts("rp4: hw init done\n");

    // ─── Kernel init ────────────────────────────────────────────────────────
    k.init();
    uart::puts("rp4: kernel ready\n");

    // ─── Launch resource managers ───────────────────────────────────────────
    // Apps are linked in statically for now. Each registers via the
    // kernel's driver::Namespace, receives messages via IPC.
    // The audiomgr registers /dev/audio and drives I2S from shared memory.

    // ─── Launch resource managers ───────────────────────────────────────────
    uart::puts("rp4: launching apps\n");

    extern auto emmcmgr_main(Kernel&) -> void;
    extern auto genetmgr_main(Kernel&) -> void;
    extern auto audiomgr_main(Kernel&) -> void;
    extern auto memfsmgr_main(Kernel&) -> void;
    extern auto httpmgr_main(Kernel&) -> void;
    extern auto btmgr_main(Kernel&) -> void;
    // extern auto storagemgr_main(Kernel&) -> void;  // needs FatFs on top of emmcmgr
    extern auto shell_main(Kernel&) -> void;

    // Register PPS subscriptions — order matters (dependencies first)
    emmcmgr_main(k);     // /pps/cmd/mount → SD card
    genetmgr_main(k);    // /pps/cmd/netstart → Ethernet + MAC
    audiomgr_main(k);    // /pps/cmd/{mute,unmute,volume,tone}
    memfsmgr_main(k);    // /pps/cmd/{store,load,memls,memrm}
    httpmgr_main(k);     // /pps/cmd/httpstart → lwIP + mDNS (rp4.local)
    btmgr_main(k);      // /pps/usb/ → BT dongle → A2DP sink → PCM

    extern auto displaymgr_main(Kernel&) -> void;
    displaymgr_main(k);  // /pps/cmd/display → VideoCore mailbox → HDMI fb
    // storagemgr_main(k);  // needs FatFs on emmcmgr

    uart::puts("rp4: apps registered\n");

    // ─── Drop to shell on UART ─────────────────────────────────────────────
    // This is the interactive prompt. Reads UART, dispatches commands.
    // Does not return unless user types 'reboot'.
    shell_main(k);

    // ─── Reboot (shell returned) ───────────────────────────────────────────
    uart::puts("rp4: rebooting...\n");
    irq::mask();
#ifndef RP4_HOST_BUILD
    // BCM2711 PM watchdog reset: write to PM_WDOG + PM_RSTC
    constexpr auto pm_base = peripheral_base + 0x100000;
    constexpr auto pm_passwd = 0x5A000000u;
    mmio_write(pm_base + 0x24, pm_passwd | 1);   // PM_WDOG: timeout = 1 tick
    mmio_write(pm_base + 0x1C, pm_passwd | 0x20); // PM_RSTC: full reset
    while (true) { asm volatile("wfi"); }  // wait for watchdog
#endif
}
