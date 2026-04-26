// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory

#include <catch2/catch_test_macros.hpp>
import std;
import qnx.kernel;
import rp4.hal;
import rp4.config;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::ipc;
using namespace qnx::memory;
namespace cfg = rp4::config;

TEST_CASE("audiomgr registers /dev/audio", "[audio]") {
    Kernel k;
    k.init();

    auto& sched = k.scheduler();
    auto& ipc   = k.ipc();
    auto& ns    = k.ns();

    auto drv_tid = sched.thread_create(pid(10), pri(20));
    auto drv_ch  = ipc.channel_create(pid(10));
    auto reg     = ns.register_resource("/dev/audio", *drv_ch, pid(10));

    REQUIRE(reg.has_value());

    auto resolved = ns.resolve("/dev/audio");
    REQUIRE(resolved.has_value());
    REQUIRE(*resolved == *drv_ch);
}

TEST_CASE("audiomgr capability is read-only for driver", "[audio][capability]") {
    Kernel k;
    k.init();

    auto& mem = k.memory();

    // App allocates buffer
    auto buf = mem.mmap(pid(1), cfg::max_audio_frame, perm_rw);
    REQUIRE(buf.has_value());

    // Grant read-only to driver
    auto drv_cap = mem.capability_grant(*buf, pid(10), Perm::read);
    REQUIRE(drv_cap.has_value());

    // Driver can read
    auto r = mem.read_span(*drv_cap);
    REQUIRE(r.has_value());
    REQUIRE(r->size() == cfg::max_audio_frame);

    // Driver cannot write
    auto w = mem.write_span(*drv_cap);
    REQUIRE_FALSE(w.has_value());
}

TEST_CASE("audiomgr VU meter gets sub-region only", "[audio][capability]") {
    Kernel k;
    k.init();

    auto& mem = k.memory();

    auto buf = mem.mmap(pid(1), cfg::max_audio_frame, perm_rw);
    REQUIRE(buf.has_value());

    // Grant first half (left channel approximation) to VU meter
    auto vu_cap = mem.capability_grant(*buf, pid(3), Perm::read, 0, cfg::half_frame);
    REQUIRE(vu_cap.has_value());

    auto span = mem.read_span(*vu_cap);
    REQUIRE(span.has_value());
    REQUIRE(span->size() == cfg::half_frame);  // half frame only
}

TEST_CASE("audiomgr cascade revoke from munmap", "[audio][capability]") {
    Kernel k;
    k.init();

    auto& mem = k.memory();

    auto buf = mem.mmap(pid(1), cfg::max_audio_frame, perm_rw);
    auto drv_cap = mem.capability_grant(*buf, pid(10), Perm::read);
    auto vu_cap  = mem.capability_grant(*buf, pid(3), Perm::read, 0, cfg::half_frame);

    // munmap root — all grants die
    auto result = mem.munmap(*buf);
    REQUIRE(result.has_value());

    REQUIRE_FALSE(mem.read_span(*drv_cap).has_value());
    REQUIRE_FALSE(mem.read_span(*vu_cap).has_value());
}

TEST_CASE("audiomgr IPC: client sends cap ID, driver reads buffer", "[audio][ipc]") {
    Kernel k;
    k.init();

    auto& sched = k.scheduler();
    auto& ipc   = k.ipc();
    auto& mem   = k.memory();
    auto& ns    = k.ns();

    // Setup driver
    auto drv_tid = sched.thread_create(pid(10), pri(20));
    auto drv_ch  = ipc.channel_create(pid(10));
    (void)ns.register_resource("/dev/audio", *drv_ch, pid(10));

    // Setup client
    auto client_tid = sched.thread_create(pid(1), pri(10));
    auto conn = ns.open("/dev/audio", pid(1));
    REQUIRE(conn.has_value());

    // Client allocates buffer and grants to driver
    auto buf = mem.mmap(pid(1), cfg::max_audio_frame, perm_rw);
    auto drv_cap = mem.capability_grant(*buf, pid(10), Perm::read);

    // Client sends cap ID over IPC
    auto cap_bytes = std::bit_cast<std::array<std::byte, sizeof(int)>>(drv_cap->value);
    (void)ipc.msg_send(*client_tid, *conn, cap_bytes);

    // Driver receives
    auto msg = ipc.msg_receive(*drv_tid, *drv_ch);
    REQUIRE(msg.has_value());
    REQUIRE(msg->data.size() == sizeof(int));

    // Driver reconstructs cap ID and reads
    auto received_cap = CapabilityId{std::bit_cast<int>(
        std::array<std::byte, 4>{msg->data[0], msg->data[1], msg->data[2], msg->data[3]})};

    auto span = mem.read_span(received_cap);
    REQUIRE(span.has_value());
    REQUIRE(span->size() == cfg::max_audio_frame);

    (void)ipc.msg_reply(msg->sender, as_bytes("ok"));
}

TEST_CASE("audio frame constants are correct", "[audio]") {
    constexpr auto rate = cfg::sample_rate;
    constexpr auto ms = cfg::frame_ms;
    constexpr auto bps = cfg::bits_per_sample;
    constexpr auto ch = cfg::channels;

    constexpr std::size_t samples = rate * ms / 1000;
    constexpr std::size_t bytes = samples * (bps / 8) * ch;

    REQUIRE(bytes == cfg::frame_bytes);
    REQUIRE(bytes == cfg::half_frame);
}
