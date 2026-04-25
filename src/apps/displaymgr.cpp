// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// displaymgr — HDMI framebuffer resource manager
//
// Talks to VideoCore VI via mailbox property tags.
// Allocates framebuffer, publishes pointer to /pps/display/status.
// Other apps write pixels via memfs capability → displaymgr blits to fb.
//
// Subscribes to:
//   /pps/cmd/display     → init framebuffer (width, height, depth)
//   /pps/display/blit    → copy capability buffer to framebuffer
//   /pps/cmd/disptext    → render text string at x,y (built-in font)
//
// Publishes:
//   /pps/display/status  → width, height, depth, pitch, fb_addr, state
//
// The bar TV shows album art + now-playing while the phone controls.

import std;
import qnx.kernel;
import rp4.hal;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::pps;
// rp4::hal not pulled in via using-namespace — local mbox clashes with
// rp4::hal::mbox. Qualify hal symbols explicitly.
namespace hal = rp4::hal;

// ─── VideoCore mailbox (BCM2711) ───────────────────────────────────────────
// The mailbox is the only way to talk to VideoCore from ARM.
// Property tags request services: framebuffer, clocks, memory, etc.

namespace mbox {
    inline constexpr std::uintptr_t base    = hal::peripheral_base + 0xB880;
    inline constexpr std::uintptr_t read    = base + 0x00;
    inline constexpr std::uintptr_t status  = base + 0x18;
    inline constexpr std::uintptr_t write   = base + 0x20;

    inline constexpr std::uint32_t full     = 0x80000000;
    inline constexpr std::uint32_t empty    = 0x40000000;
    inline constexpr std::uint32_t channel  = 8;  // property tags ARM→VC

    /** @brief Send a message buffer to VideoCore via mailbox. */
    inline auto call(volatile std::uint32_t* buf) -> bool {
        auto addr = reinterpret_cast<std::uintptr_t>(buf);
        // Wait until mailbox is not full
        while (hal::mmio_read(status) & full) {}
        // Write address (aligned to 16 bytes) | channel
        hal::mmio_write(write, static_cast<std::uint32_t>((addr & ~0xF) | channel));
        // Wait for response
        while (true) {
            while (hal::mmio_read(status) & empty) {}
            auto resp = hal::mmio_read(read);
            if ((resp & 0xF) == channel) return (buf[1] == 0x80000000);
        }
    }

    // Property tag IDs
    inline constexpr std::uint32_t tag_set_phys_wh  = 0x00048003;
    inline constexpr std::uint32_t tag_set_virt_wh  = 0x00048004;
    inline constexpr std::uint32_t tag_set_depth    = 0x00048005;
    inline constexpr std::uint32_t tag_set_pixel_order = 0x00048006;
    inline constexpr std::uint32_t tag_alloc_buffer = 0x00040001;
    inline constexpr std::uint32_t tag_get_pitch    = 0x00040008;
    inline constexpr std::uint32_t tag_end          = 0x00000000;
}

// ─── Framebuffer state ─────────────────────────────────────────────────────

static volatile std::uint32_t* g_fb = nullptr;
static std::uint32_t g_width  = 0;
static std::uint32_t g_height = 0;
static std::uint32_t g_pitch  = 0;
static std::uint32_t g_depth  = 0;

static auto init_framebuffer(std::uint32_t w, std::uint32_t h, std::uint32_t depth) -> bool {
    // Mailbox buffer — must be 16-byte aligned
    alignas(16) static volatile std::uint32_t buf[36];

    std::size_t i = 0;
    buf[i++] = 0;                          // total size (filled at end)
    buf[i++] = 0;                          // request code
    // Set physical size
    buf[i++] = mbox::tag_set_phys_wh;
    buf[i++] = 8; buf[i++] = 0;
    buf[i++] = w; buf[i++] = h;
    // Set virtual size (same, no double buffer)
    buf[i++] = mbox::tag_set_virt_wh;
    buf[i++] = 8; buf[i++] = 0;
    buf[i++] = w; buf[i++] = h;
    // Set depth
    buf[i++] = mbox::tag_set_depth;
    buf[i++] = 4; buf[i++] = 0;
    buf[i++] = depth;
    // Set pixel order (RGB)
    buf[i++] = mbox::tag_set_pixel_order;
    buf[i++] = 4; buf[i++] = 0;
    buf[i++] = 1;  // RGB
    // Allocate buffer
    buf[i++] = mbox::tag_alloc_buffer;
    buf[i++] = 8; buf[i++] = 0;
    buf[i++] = 4096;  // alignment
    buf[i++] = 0;     // size (returned)
    // Get pitch
    buf[i++] = mbox::tag_get_pitch;
    buf[i++] = 4; buf[i++] = 0;
    buf[i++] = 0;     // pitch (returned)
    // End tag
    buf[i++] = mbox::tag_end;
    // Set total size
    buf[0] = static_cast<std::uint32_t>(i * 4);

    if (!mbox::call(const_cast<volatile std::uint32_t*>(buf))) return false;

    // Extract results — alloc_buffer response is at index 23
    auto fb_addr = buf[23] & 0x3FFFFFFF;  // convert bus addr to ARM physical
    auto fb_size = buf[24];
    g_pitch = buf[28];
    g_fb = reinterpret_cast<volatile std::uint32_t*>(static_cast<std::uintptr_t>(fb_addr));
    g_width = w;
    g_height = h;
    g_depth = depth;

    return (g_fb != nullptr && fb_size > 0);
}

// ─── Simple pixel operations ───────────────────────────────────────────────

static auto put_pixel(std::uint32_t x, std::uint32_t y, std::uint32_t color) -> void {
    if (!g_fb || x >= g_width || y >= g_height) return;
    auto offset = y * (g_pitch / 4) + x;
    g_fb[offset] = color;
}

static auto fill_rect(std::uint32_t x, std::uint32_t y,
                       std::uint32_t w, std::uint32_t h, std::uint32_t color) -> void {
    for (std::uint32_t dy = 0; dy < h; ++dy)
        for (std::uint32_t dx = 0; dx < w; ++dx)
            put_pixel(x + dx, y + dy, color);
}

static auto clear(std::uint32_t color = 0x000F0C07) -> void {
    if (!g_fb) return;
    auto pixels = g_pitch / 4 * g_height;
    for (std::uint32_t i = 0; i < pixels; ++i) g_fb[i] = color;
}

// ─── Entry ─────────────────────────────────────────────────────────────────

auto displaymgr_main(Kernel& k) -> void {
    auto& pps = k.pps();

    constexpr auto pid = ProcessId{19};

    // Init framebuffer on command (or auto at boot)
    (void)pps.subscribe("/pps/cmd/display", pid, [&](const Notification&) {
        hal::uart::puts("displaymgr: init 1920x1080x32...\n");
        if (init_framebuffer(1920, 1080, 32)) {
            clear(0x000F0C07);  // dark background (our clay theme)
            (void)pps.publish("/pps/display/status", "state", "ready");
            (void)pps.publish("/pps/display/status", "width", "1920");
            (void)pps.publish("/pps/display/status", "height", "1080");
            (void)pps.publish("/pps/display/status", "depth", "32");
            (void)pps.publish("/pps/display/status", "pitch",
                std::to_string(g_pitch));
            hal::uart::puts("displaymgr: framebuffer ready\n");
        } else {
            (void)pps.publish("/pps/display/status", "state", "no HDMI");
            hal::uart::puts("displaymgr: no HDMI connected\n");
        }
    });

    // Show now-playing on HDMI when audio status changes
    (void)pps.subscribe("/pps/audio/status", pid, [&](const Notification& n) {
        if (!g_fb) return;
        // Simple: paint a colored bar at bottom when playing
        if (n.key == "state" && n.value == "playing") {
            fill_rect(0, 1040, 1920, 40, 0x00C4924A);  // clay bar
        } else if (n.key == "state" && n.value == "idle") {
            fill_rect(0, 1040, 1920, 40, 0x000F0C07);  // clear bar
        }
    });

    // Auto-init
    (void)pps.publish("/pps/cmd/display", "value", "1");
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 displaymgr (host build) ===");
    return 0;
}
