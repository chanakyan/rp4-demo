// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// helloworld — /dev/hello resource manager
//
// Proof of life. Registers /dev/hello, replies "hello world" to any read.

import std;
import qnx.kernel;
import rp4.hal;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::ipc;
using namespace rp4::hal;

extern "C" auto helloworld_main(Kernel& k) -> void {
    auto& sched = k.scheduler();
    auto& ipc   = k.ipc();
    auto& ns    = k.ns();

    constexpr auto pid = ProcessId{20};
    auto tid = sched.thread_create(pid, Priority{10});
    auto ch  = ipc.channel_create(pid);
    (void)ns.register_resource("/dev/hello", *ch, pid);

    while (true) {
        auto msg = ipc.msg_receive(*tid, *ch);
        if (!msg) break;
        (void)ipc.msg_reply(msg->sender, as_bytes("hello world\n"));
    }
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 helloworld ===");
    helloworld_main(k);
    return 0;
}
