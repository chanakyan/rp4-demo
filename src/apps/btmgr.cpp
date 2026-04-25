// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// btmgr — Bluetooth A2DP sink resource manager
//
// Subscribes to:
//   /pps/usb/          → watches for BT dongle (class=wireless, subclass=bluetooth)
//   /pps/cmd/btpair    → initiate pairing
//   /pps/cmd/btdisco   → disconnect
//
// Publishes:
//   /pps/bt/status     → state, paired_device, codec, bitrate
//   /pps/audio/pcm     → decoded PCM from A2DP stream (same as storagemgr)
//
// The phone pairs over BT, streams Spotify/Apple Music.
// btmgr decodes SBC/AAC, publishes PCM. audiomgr plays it.
// audiomgr doesn't know the source. PPS doesn't care.
//
// No BlueZ. No dbus. No pulseaudio. No bluetoothd.
// Just HCI commands over USB bulk endpoints → SBC decode → PCM → PPS.

import std;
import qnx.kernel;
import rp4.hal;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::pps;
using namespace rp4::hal;

// ─── BT state ──────────────────────────────────────────────────────────────

static bool dongle_present = false;

// ─── Entry ─────────────────────────────────────────────────────────────────

auto btmgr_main(Kernel& k) -> void {
    auto& pps = k.pps();

    constexpr auto pid = ProcessId{17};

    (void)pps.publish("/pps/bt/status", "state", "no dongle");

    // Watch for BT dongle on USB
    (void)pps.subscribe("/pps/usb/", pid, [&](const Notification& n) {
        if (n.key == "subclass" && n.value == "bluetooth") {
            if (n.kind == Notification::Kind::created ||
                n.kind == Notification::Kind::modified) {
                dongle_present = true;
                (void)pps.publish("/pps/bt/status", "state", "dongle found");
                (void)pps.publish("/pps/bt/status", "dongle", n.path);
                uart::puts("btmgr: BT dongle detected\n");

                // TODO: HCI reset (USB control transfer to endpoint 0)
                // TODO: HCI_Write_Scan_Enable (discoverable + connectable)
                // TODO: Wait for HCI_Connection_Complete event
                // TODO: L2CAP → AVDTP → A2DP sink setup
                // TODO: SBC/AAC decode → PCM → /pps/audio/pcm

                (void)pps.publish("/pps/bt/status", "state", "needs HCI init");
            }
        }

        // Dongle removed
        if (n.key == "state" && n.value == "removed") {
            dongle_present = false;
            (void)pps.publish("/pps/bt/status", "state", "no dongle");
            uart::puts("btmgr: BT dongle removed\n");
        }
    });

    // Pair command
    (void)pps.subscribe("/pps/cmd/btpair", pid, [&](const Notification&) {
        if (!dongle_present) {
            (void)pps.publish("/pps/bt/status", "error", "no dongle");
            return;
        }
        (void)pps.publish("/pps/bt/status", "state", "pairing");
        // TODO: HCI_Write_Scan_Enable → discoverable
        // TODO: HCI_PIN_Code_Request_Reply or SSP
        // TODO: On success: publish paired_device name + MAC
        (void)pps.publish("/pps/bt/status", "state", "needs HCI pairing impl");
    });

    // Disconnect command
    (void)pps.subscribe("/pps/cmd/btdisco", pid, [&](const Notification&) {
        (void)pps.publish("/pps/bt/status", "state", "disconnected");
        // TODO: HCI_Disconnect
    });
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 btmgr (host build) ===");
    std::println("  A2DP sink: phone streams BT → PCM → DAC");
    return 0;
}
