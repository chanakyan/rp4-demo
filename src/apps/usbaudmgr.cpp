// SPDX-License-Identifier: GPL-3.0-or-later
//
// usbaudmgr — USB Audio Class resource manager
//
// Ported from Circle USB audio streaming (R. Stange, GPL-3.0-or-later).
// xHCI host controller + USB Audio Class 1.0/2.0 → isochronous PCM output.
//
// Subscribes to:
//   /pps/audio/pcm      → PCM capability → stream to USB DAC endpoint
//   /pps/cmd/usbinit    → enumerate USB, find audio device
//
// Publishes:
//   /pps/usb/audio      → vid, pid, name, sample_rates, channels, state
//   /pps/usb/status     → connected, speed, endpoint
//
// The USB DAC headphone amp plugs into RPi4 USB port.
// usbaudmgr enumerates it, opens the isochronous endpoint,
// streams PCM from memfs. audiomgr doesn't know — PPS routes.
//
// Alternative output to I2S DAC: both can play simultaneously
// if audiomgr publishes to both /pps/audio/i2s and /pps/audio/usb.

import std;
import qnx.kernel;
import rp4.hal;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::pps;
using namespace qnx::memory;
using namespace rp4::hal;

// ─── VL805 xHCI base (PCIe BAR on BCM2711) ────────────────────────────────

namespace xhci {
    // VL805 is on PCIe. The firmware maps it. Typical BAR0:
    inline constexpr std::uintptr_t base = 0xFD500000;

    // xHCI capability registers
    inline constexpr std::uintptr_t caplength  = base + 0x00;
    inline constexpr std::uintptr_t hcsparams1 = base + 0x04;
    inline constexpr std::uintptr_t hcsparams2 = base + 0x08;
    inline constexpr std::uintptr_t dboff      = base + 0x14;
    inline constexpr std::uintptr_t rtsoff     = base + 0x18;

    // xHCI operational registers (offset by CAPLENGTH)
    // These are computed at runtime after reading CAPLENGTH

    /** @brief Read xHCI version. */
    [[nodiscard]] inline auto version() -> std::uint32_t {
        return mmio_read(base + 0x02) >> 16;  // HCIVERSION
    }

    /** @brief Read number of ports. */
    [[nodiscard]] inline auto num_ports() -> std::uint32_t {
        return (mmio_read(hcsparams1) >> 24) & 0xFF;
    }

    // TODO: Full xHCI init (from Circle xhcidevice.cpp):
    //   1. Reset controller (USBCMD.HCRST)
    //   2. Set DCBAAP (Device Context Base Address Array Pointer)
    //   3. Set CRCR (Command Ring Control Register)
    //   4. Enable ports (PORTSC write)
    //   5. Start controller (USBCMD.RS)
    //   6. Wait for port status change events
    //   7. Enumerate connected devices (address, configure)
    //   8. Find USB Audio Class interface (bInterfaceClass=0x01)
    //   9. Parse audio streaming descriptors
    //   10. Open isochronous OUT endpoint
    //   11. Stream PCM frames
}

// ─── USB Audio Class constants ─────────────────────────────────────────────

namespace uac {
    inline constexpr std::uint8_t class_audio = 0x01;
    inline constexpr std::uint8_t subclass_streaming = 0x02;
    inline constexpr std::uint8_t subclass_control = 0x01;

    // Audio format type I (PCM)
    inline constexpr std::uint8_t format_pcm = 0x01;
}

// ─── Entry ─────────────────────────────────────────────────────────────────

auto usbaudmgr_main(Kernel& k) -> void {
    auto& pps = k.pps();

    constexpr auto pid = ProcessId{18};

    (void)pps.publish("/pps/usb/audio", "state", "init");

    (void)pps.subscribe("/pps/cmd/usbinit", pid, [&](const Notification&) {
        uart::puts("usbaudmgr: probing xHCI...\n");

        auto ver = xhci::version();
        auto ports = xhci::num_ports();

        (void)pps.publish("/pps/usb/status", "xhci_version",
            std::to_string(ver));
        (void)pps.publish("/pps/usb/status", "ports",
            std::to_string(ports));

        // TODO: Full xHCI init + USB enumeration
        // TODO: Find audio class device
        // TODO: Open isochronous endpoint
        // TODO: Subscribe to /pps/audio/pcm and stream to USB endpoint

        (void)pps.publish("/pps/usb/audio", "state", "needs xHCI init");
        uart::puts("usbaudmgr: xHCI probed — needs full init\n");
    });

    // PCM playback via USB (when xHCI is wired)
    (void)pps.subscribe("/pps/audio/usb", pid, [&](const Notification& n) {
        // Same pattern as audiomgr I2S path:
        // Read capability ID from PPS → read PCM from memfs →
        // stream to USB isochronous endpoint
        auto& mem = k.memory();
        unsigned id = 0;
        for (auto c : n.value) {
            if (c >= '0' && c <= '9') id = id * 10 + (c - '0');
        }
        auto span = mem.read_span(CapabilityId{static_cast<int>(id)});
        if (!span) return;

        (void)pps.publish("/pps/usb/audio", "state", "playing");

        // TODO: Write PCM to USB isochronous endpoint
        // For now: publish that we received the data
        (void)pps.publish("/pps/usb/audio", "frames",
            std::to_string(span->size() / 4));

        (void)pps.publish("/pps/usb/audio", "state", "idle");
    });

    // Auto-probe
    (void)pps.publish("/pps/cmd/usbinit", "value", "1");
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 usbaudmgr (host build) ===");
    std::println("  GPL-3.0 — USB Audio Class via xHCI");
    return 0;
}
