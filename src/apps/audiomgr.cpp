// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// audiomgr — stereo audio resource manager for RPi4
//
// Subscribes to:
//   /pps/cmd/mute     → mute/unmute DAC
//   /pps/cmd/unmute   → unmute DAC
//   /pps/cmd/volume   → set volume (0-255)
//   /pps/cmd/tone     → play 440 Hz test tone
//   /pps/audio/pcm    → play PCM buffer (capability ID from storagemgr)
//
// Publishes:
//   /pps/audio/status → state, format, frame_bytes
//
// Knows nothing about shell or storagemgr. Talks only through PPS.

import std;
import qnx.kernel;
import rp4.hal;
import rp4.config;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::ipc;
using namespace qnx::memory;
using namespace qnx::pps;
using namespace rp4::hal;

// ─── 440 Hz tone generator ─────────────────────────────────────────────────

static auto play_tone() -> void {
    constexpr int sr = 48000;
    constexpr int freq = 440;
    constexpr int duration = sr * 5;
    for (int n = 0; n < duration; ++n) {
        int phase = (n * freq * 2) % sr;
        int x = phase * 32768 / sr - 16384;
        int x3 = (x * x / 16384) * x / 16384;
        auto sample = static_cast<std::int16_t>(x - x3 / 6);
        while (!i2s::write_sample(sample, sample)) {}
    }
}

// ─── Entry ─────────────────────────────────────────────────────────────────

auto audiomgr_main(Kernel& k) -> void {
    auto& pps = k.pps();

    // Publish initial status
    (void)pps.publish("/pps/audio/status", "state", "idle");
    (void)pps.publish("/pps/audio/status", "format",
        std::format("{}kHz/{}-bit/{}ch",
            rp4::config::sample_rate / 1000,
            rp4::config::bits_per_sample,
            rp4::config::channels));

    // Subscribe to command PPS objects
    // Notification has .path, .key, .value, .kind

    constexpr auto pid = ProcessId{10};

    (void)pps.subscribe("/pps/cmd/mute", pid, [&](const Notification&) {
        dac::mute(true);
        (void)pps.publish("/pps/audio/status", "state", "muted");
    });

    (void)pps.subscribe("/pps/cmd/unmute", pid, [&](const Notification&) {
        dac::mute(false);
        (void)pps.publish("/pps/audio/status", "state", "idle");
    });

    (void)pps.subscribe("/pps/cmd/volume", pid, [&](const Notification& n) {
        unsigned v = 0;
        for (auto c : n.value) {
            if (c >= '0' && c <= '9') v = v * 10 + (c - '0');
        }
        dac::set_volume(static_cast<std::uint8_t>(v));
        (void)pps.publish("/pps/audio/status", "volume", std::to_string(v));
    });

    (void)pps.subscribe("/pps/cmd/tone", pid, [&](const Notification&) {
        (void)pps.publish("/pps/audio/status", "state", "playing");
        play_tone();
        (void)pps.publish("/pps/audio/status", "state", "idle");
    });

    (void)pps.subscribe("/pps/audio/pcm", pid, [&](const Notification& n) {
        auto& mem = k.memory();
        unsigned id = 0;
        for (auto c : n.value) {
            if (c >= '0' && c <= '9') id = id * 10 + (c - '0');
        }
        auto span = mem.read_span(CapabilityId{static_cast<int>(id)});
        if (!span) return;

        // Read sample rate from PPS — storagemgr published it
        auto sr_result = pps.read("/pps/audio/status", "sample_rate");
        if (sr_result) {
            unsigned sr = 0;
            for (auto c : *sr_result) {
                if (c >= '0' && c <= '9') sr = sr * 10 + (c - '0');
            }
            if (sr > 0 && sr != rp4::config::sample_rate) {
                // Switch I2S clock to match source sample rate
                i2s::set_sample_rate(sr);
            }
        }

        (void)pps.publish("/pps/audio/status", "state", "playing");

        auto pcm = *span;
        for (std::size_t i = 0; i + 3 < pcm.size(); i += 4) {
            auto left  = static_cast<std::int16_t>(
                std::to_integer<int>(pcm[i]) |
                (std::to_integer<int>(pcm[i + 1]) << 8));
            auto right = static_cast<std::int16_t>(
                std::to_integer<int>(pcm[i + 2]) |
                (std::to_integer<int>(pcm[i + 3]) << 8));
            while (!i2s::write_sample(left, right)) {}
        }

        (void)pps.publish("/pps/audio/status", "state", "idle");
    });

    // Cooperative single-threaded: callbacks fire synchronously inside publish().
    // No event loop needed. This function returns after setup.
    // When boot.cpp calls shell_main, shell publishes → callbacks fire inline.
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 audiomgr (host build) ===");
    std::println("  subscribes to /pps/cmd/{{mute,unmute,volume,tone}}");
    std::println("  subscribes to /pps/audio/pcm");
    std::println("  publishes /pps/audio/status");
    audiomgr_main(k);
    return 0;
}
