// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// console — PPS debug monitor on UART
//
// Subscribes to /pps/ (all objects). Prints every PPS notification
// to UART as it happens. Plug in a serial terminal, see everything.
//
// This is printf debugging via PPS. No special protocol.
// Every publish() in the system shows up here in real time.

import std;
import qnx.kernel;
import rp4.hal;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::pps;
using namespace rp4::hal;

auto console_main(Kernel& k) -> void {
    auto& pps = k.pps();

    constexpr auto pid = ProcessId{11};

    // Subscribe to everything — wildcard prefix "/"
    (void)pps.subscribe("/", pid, [](const Notification& n) {
        // Format: [kind] path key=value
        switch (n.kind) {
            case Notification::Kind::created:  uart::puts("[+] "); break;
            case Notification::Kind::modified: uart::puts("[~] "); break;
            case Notification::Kind::deleted:  uart::puts("[-] "); break;
        }
        for (auto c : n.path) uart::putc(c);
        uart::putc(' ');
        for (auto c : n.key) uart::putc(c);
        uart::putc('=');
        for (auto c : n.value) uart::putc(c);
        uart::putc('\n');
    });
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 console (PPS debug monitor) ===");
    return 0;
}
