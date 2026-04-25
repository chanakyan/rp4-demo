// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// memfsmgr — in-memory filesystem resource manager
//
// Subscribes to:
//   /pps/cmd/store   → store a named buffer (capability ID + name)
//   /pps/cmd/load    → publish capability for a named buffer
//   /pps/cmd/memls   → list all stored buffers
//   /pps/cmd/memrm   → remove a named buffer
//
// Publishes:
//   /pps/memfs/status  → buffer count, total bytes
//   /pps/memfs/listing → names of stored buffers
//   /pps/memfs/loaded  → capability ID for a requested buffer
//
// No disk. No FatFs. No EMMC. Pure RAM.

import std;
import qnx.kernel;
import rp4.hal;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::memory;
using namespace qnx::pps;
using namespace rp4::hal;

// ─── In-memory file table ──────────────────────────────────────────────────

struct MemFile {
    std::array<char, 64> name{};
    CapabilityId cap{0};
    std::size_t bytes{0};
};

static constexpr std::size_t max_files = 64;
static std::array<MemFile, max_files> files{};
static std::size_t file_count = 0;

static auto find_file(std::string_view name) -> MemFile* {
    for (std::size_t i = 0; i < file_count; ++i) {
        if (std::string_view{files[i].name.data()} == name) return &files[i];
    }
    return nullptr;
}

static auto update_status(Pps& pps) -> void {
    std::size_t total = 0;
    for (std::size_t i = 0; i < file_count; ++i) total += files[i].bytes;
    (void)pps.publish("/pps/memfs/status", "count", std::to_string(file_count));
    (void)pps.publish("/pps/memfs/status", "bytes", std::to_string(total));
}

// ─── Entry ─────────────────────────────────────────────────────────────────

auto memfsmgr_main(Kernel& k) -> void {
    auto& pps = k.pps();

    (void)pps.publish("/pps/memfs/status", "count", "0");
    (void)pps.publish("/pps/memfs/status", "bytes", "0");

    constexpr auto pid = ProcessId{13};

    // store: "name cap_id bytes"
    (void)pps.subscribe("/pps/cmd/store", pid, [&](const Notification& n) {
        if (file_count >= max_files) return;

        // Parse: "name cap_id bytes"
        auto v = n.value;
        auto sp1 = v.find(' ');
        if (sp1 == std::string_view::npos) return;
        auto name = v.substr(0, sp1);
        auto rest = v.substr(sp1 + 1);
        auto sp2 = rest.find(' ');

        unsigned cap_val = 0;
        unsigned byte_count = 0;
        for (auto c : rest.substr(0, sp2)) {
            if (c >= '0' && c <= '9') cap_val = cap_val * 10 + (c - '0');
        }
        if (sp2 != std::string_view::npos) {
            for (auto c : rest.substr(sp2 + 1)) {
                if (c >= '0' && c <= '9') byte_count = byte_count * 10 + (c - '0');
            }
        }

        auto& f = files[file_count++];
        auto len = std::min(name.size(), f.name.size() - 1);
        for (std::size_t i = 0; i < len; ++i) f.name[i] = name[i];
        f.name[len] = '\0';
        f.cap = CapabilityId{static_cast<int>(cap_val)};
        f.bytes = byte_count;

        update_status(pps);
        uart::puts("memfs: stored ");
        uart::puts(f.name.data());
        uart::putc('\n');
    });

    // load: "name" → publishes capability ID to /pps/memfs/loaded
    (void)pps.subscribe("/pps/cmd/load", pid, [&](const Notification& n) {
        auto* f = find_file(n.value);
        if (f) {
            (void)pps.publish("/pps/memfs/loaded", "cap",
                std::to_string(f->cap.value));
            (void)pps.publish("/pps/memfs/loaded", "name",
                std::string(n.value));
        } else {
            (void)pps.publish("/pps/memfs/loaded", "error", "not found");
        }
    });

    // memls: list all files
    (void)pps.subscribe("/pps/cmd/memls", pid, [&](const Notification&) {
        std::string listing;
        for (std::size_t i = 0; i < file_count; ++i) {
            listing += files[i].name.data();
            listing += " (";
            listing += std::to_string(files[i].bytes);
            listing += " bytes)\n";
        }
        (void)pps.publish("/pps/memfs/listing", "files",
            listing.empty() ? "(empty)" : listing);
    });

    // memrm: remove by name
    (void)pps.subscribe("/pps/cmd/memrm", pid, [&](const Notification& n) {
        for (std::size_t i = 0; i < file_count; ++i) {
            if (std::string_view{files[i].name.data()} == n.value) {
                // Shift remaining files down
                for (std::size_t j = i; j + 1 < file_count; ++j) {
                    files[j] = files[j + 1];
                }
                --file_count;
                update_status(pps);
                return;
            }
        }
    });

    // Cooperative: callbacks fire synchronously inside publish().
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 memfsmgr (host build) ===");
    return 0;
}
