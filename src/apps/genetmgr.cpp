// SPDX-License-Identifier: GPL-3.0-or-later
//
// genetmgr — Ethernet resource manager (BCM2711 GENET + BCM54213 PHY)
//
// Ported from Circle bare-metal framework (R. Stange), which is ported
// from the Linux Broadcom GENET driver. GPL-3.0-or-later.
//
// This file is GPL because the underlying driver is GPL (Linux origin).
// It communicates with the rest of rp4 (BSD-2-Clause) through PPS only.
// No BSD code calls GPL functions. The kernel routes messages.
//
// Subscribes to:
//   /pps/cmd/netstart  → init GENET, DHCP, start lwIP
//   /pps/cmd/netstop   → shutdown
//
// Publishes:
//   /pps/net/status    → link, speed, ip, mac, hostname
//   /pps/net/stats     → tx_packets, rx_packets, errors

import std;
import qnx.kernel;
import rp4.hal;
import rp4.config;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::pps;
using namespace rp4::hal;

// ─── GENET register map (BCM2711 datasheet + Linux driver) ─────────────────

namespace genet {
    // Base address: 0xFD580000 (ARM physical)
    inline constexpr std::uintptr_t base = 0xFD580000;

    // System registers
    inline constexpr std::uintptr_t sys_rev_ctrl  = base + 0x00;
    inline constexpr std::uintptr_t sys_port_ctrl = base + 0x04;
    inline constexpr std::uintptr_t sys_rbuf_flush= base + 0x08;
    inline constexpr std::uintptr_t sys_tbuf_flush= base + 0x0C;

    // UMAC registers (UniMAC)
    inline constexpr std::uintptr_t umac_base     = base + 0x0800;
    inline constexpr std::uintptr_t umac_cmd      = umac_base + 0x008;
    inline constexpr std::uintptr_t umac_mac0     = umac_base + 0x00C;  // MAC addr bytes 0-3
    inline constexpr std::uintptr_t umac_mac1     = umac_base + 0x010;  // MAC addr bytes 4-5
    inline constexpr std::uintptr_t umac_max_frame= umac_base + 0x014;
    inline constexpr std::uintptr_t umac_tx_flush = umac_base + 0x334;

    // MDIO (PHY management)
    inline constexpr std::uintptr_t mdio_cmd      = umac_base + 0x614;

    // RDMA/TDMA
    inline constexpr std::uintptr_t rdma_base     = base + 0x2000;
    inline constexpr std::uintptr_t tdma_base     = base + 0x4000;

    // UMAC CMD bits
    inline constexpr std::uint32_t cmd_tx_en = 1 << 0;
    inline constexpr std::uint32_t cmd_rx_en = 1 << 1;

    /** @brief Read GENET version. */
    [[nodiscard]] inline auto version() -> std::uint32_t {
        return mmio_read(sys_rev_ctrl) >> 24;
    }

    /** @brief Read MAC address from hardware. */
    inline auto read_mac(std::array<std::uint8_t, 6>& mac) -> void {
        auto mac0 = mmio_read(umac_mac0);
        auto mac1 = mmio_read(umac_mac1);
        mac[0] = (mac0 >> 24) & 0xFF;
        mac[1] = (mac0 >> 16) & 0xFF;
        mac[2] = (mac0 >>  8) & 0xFF;
        mac[3] = (mac0 >>  0) & 0xFF;
        mac[4] = (mac1 >> 24) & 0xFF;
        mac[5] = (mac1 >> 16) & 0xFF;
    }

    /** @brief Reset UMAC. */
    inline auto reset() -> void {
        mmio_write(sys_rbuf_flush, 1);
        for (volatile int d = 0; d < 10000; ++d) {}
        mmio_write(sys_rbuf_flush, 0);
        mmio_write(umac_cmd, 0);  // disable TX/RX
        mmio_write(umac_max_frame, 1536);  // standard MTU + headers
    }

    /** @brief Enable TX + RX. */
    inline auto enable() -> void {
        auto cmd = mmio_read(umac_cmd);
        mmio_write(umac_cmd, cmd | cmd_tx_en | cmd_rx_en);
    }

    // TODO: Full DMA ring setup (RDMA/TDMA buffer descriptors)
    // TODO: PHY init (BCM54213 via MDIO)
    // TODO: Interrupt handler for RX/TX completion
    // These are ~1500 lines in Circle. Port incrementally.
}

// ─── Entry ─────────────────────────────────────────────────────────────────

auto genetmgr_main(Kernel& k) -> void {
    auto& pps = k.pps();

    constexpr auto pid = ProcessId{15};

    (void)pps.subscribe("/pps/cmd/netstart", pid, [&](const Notification&) {
        uart::puts("genetmgr: initializing GENET v");

        auto ver = genet::version();
        std::array<char, 4> vbuf = {static_cast<char>('0' + ver), '\n', '\0', '\0'};
        uart::puts(vbuf.data());

        genet::reset();

        std::array<std::uint8_t, 6> mac{};
        genet::read_mac(mac);

        // Format MAC for PPS
        char macstr[18];
        auto hex = [](std::uint8_t b, char* out) {
            constexpr char h[] = "0123456789abcdef";
            out[0] = h[b >> 4]; out[1] = h[b & 0xF];
        };
        for (int i = 0; i < 6; ++i) {
            hex(mac[i], macstr + i*3);
            macstr[i*3 + 2] = (i < 5) ? ':' : '\0';
        }

        (void)pps.publish("/pps/net/status", "mac", macstr);
        (void)pps.publish("/pps/net/status", "state", "reset");
        (void)pps.publish("/pps/net/status", "hostname", "rp4.local");

        // TODO: PHY init (MDIO read/write to BCM54213)
        // TODO: DMA ring setup (buffer descriptors)
        // TODO: lwIP netif_add + dhcp_start
        // TODO: mdns_resp_init → rp4.local
        // TODO: httpd_init → port 80
        // TODO: genet::enable()

        (void)pps.publish("/pps/net/status", "state", "waiting for PHY/DMA init");
        uart::puts("genetmgr: MAC ");
        uart::puts(macstr);
        uart::puts(" — needs PHY + DMA to go live\n");
    });

    // Auto-start
    (void)pps.publish("/pps/cmd/netstart", "value", "1");
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 genetmgr (host build) ===");
    std::println("  GPL-3.0-or-later (ported from Linux GENET driver)");
    return 0;
}
