// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// emmcmgr — EMMC/SD card resource manager
//
// Wraps Circle's EMMC driver (MIT, John Cronin / R. Stange) as a
// FatFs diskio backend. storagemgr calls FatFs → FatFs calls diskio →
// emmcmgr talks to BCM2711 EMMC hardware.
//
// Subscribes to:
//   /pps/cmd/mount    → mount SD card, init FatFs
//   /pps/cmd/eject    → unmount
//
// Publishes:
//   /pps/emmc/status  → mounted, card_type, capacity, blocks

import std;
import qnx.kernel;
import rp4.hal;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::pps;
using namespace rp4::hal;

// ─── BCM2711 EMMC registers ───────────────────────────────────────────────
// Ported from Circle addon/SDCard/emmc.cpp (MIT license).

namespace emmc {
    inline constexpr std::uintptr_t base = peripheral_base + 0x340000;

    // Register offsets
    inline constexpr std::uintptr_t arg2        = base + 0x00;
    inline constexpr std::uintptr_t blksizecnt  = base + 0x04;
    inline constexpr std::uintptr_t arg1        = base + 0x08;
    inline constexpr std::uintptr_t cmdtm       = base + 0x0C;
    inline constexpr std::uintptr_t resp0       = base + 0x10;
    inline constexpr std::uintptr_t resp1       = base + 0x14;
    inline constexpr std::uintptr_t resp2       = base + 0x18;
    inline constexpr std::uintptr_t resp3       = base + 0x1C;
    inline constexpr std::uintptr_t data        = base + 0x20;
    inline constexpr std::uintptr_t status      = base + 0x24;
    inline constexpr std::uintptr_t control0    = base + 0x28;
    inline constexpr std::uintptr_t control1    = base + 0x2C;
    inline constexpr std::uintptr_t interrupt   = base + 0x30;
    inline constexpr std::uintptr_t irpt_mask   = base + 0x34;
    inline constexpr std::uintptr_t irpt_en     = base + 0x38;
    inline constexpr std::uintptr_t slotisr_ver = base + 0x3C + 0xFC;

    // Status bits
    inline constexpr std::uint32_t cmd_inhibit  = 1 << 0;
    inline constexpr std::uint32_t dat_inhibit  = 1 << 1;
    inline constexpr std::uint32_t read_ready   = 1 << 5;

    /** @brief Reset EMMC controller. */
    inline auto reset() -> bool {
        auto ctrl1 = mmio_read(control1);
        ctrl1 |= (1 << 24);  // SRST_HC
        mmio_write(control1, ctrl1);
        // Wait for reset to complete
        for (int i = 0; i < 10000; ++i) {
            if (!(mmio_read(control1) & (1 << 24))) return true;
            for (volatile int d = 0; d < 100; ++d) {}
        }
        return false;
    }

    /** @brief Read EMMC version. */
    [[nodiscard]] inline auto version() -> std::uint32_t {
        return (mmio_read(slotisr_ver) >> 16) & 0xFF;
    }

    // TODO: Full SD card init sequence (CMD0, CMD8, ACMD41, CMD2, CMD3, CMD7)
    // TODO: Block read (CMD17/CMD18)
    // TODO: Block write (CMD24/CMD25)
    // These are ~800 lines in Circle. Port incrementally.
}

// ─── Entry ─────────────────────────────────────────────────────────────────

auto emmcmgr_main(Kernel& k) -> void {
    auto& pps = k.pps();

    constexpr auto pid = ProcessId{16};

    (void)pps.subscribe("/pps/cmd/mount", pid, [&](const Notification&) {
        uart::puts("emmcmgr: resetting EMMC...\n");
        if (!emmc::reset()) {
            (void)pps.publish("/pps/emmc/status", "state", "reset failed");
            uart::puts("emmcmgr: reset failed\n");
            return;
        }

        auto ver = emmc::version();
        (void)pps.publish("/pps/emmc/status", "version", std::to_string(ver));
        (void)pps.publish("/pps/emmc/status", "state", "reset ok");
        uart::puts("emmcmgr: EMMC v");
        uart::putc('0' + static_cast<char>(ver));
        uart::puts(" — needs SD init sequence\n");

        // TODO: SD card init (CMD0 → CMD7)
        // TODO: Wire as FatFs diskio backend
        // TODO: Notify storagemgr via /pps/emmc/status mounted=true
    });

    // Auto-mount at boot
    (void)pps.publish("/pps/cmd/mount", "value", "1");
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 emmcmgr (host build) ===");
    return 0;
}
