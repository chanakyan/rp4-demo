// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// shell — UART command line → PPS publisher
//
// Reads a line from UART, writes key=value to /pps/cmd/.
// Subscribes to /pps/*/status for display.
// Knows nothing about audiomgr, storagemgr, or any other app.
// History, tab completion, ctrl-a/u/c. That's it.

import std;
import qnx.kernel;
import rp4.hal;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::pps;
using namespace rp4::hal;

// ─── UART RX ───────────────────────────────────────────────────────────────

namespace shell_io {
    inline auto getc() -> char {
        constexpr auto aux_base = rp4::hal::peripheral_base + 0x215000;
        constexpr auto mu_lsr   = aux_base + 0x54;
        constexpr auto mu_io    = aux_base + 0x40;
        while (!(mmio_read(mu_lsr) & 0x01)) {
#ifndef RP4_HOST_BUILD
            asm volatile("wfi");
#endif
        }
        return static_cast<char>(mmio_read(mu_io) & 0xFF);
    }
}

// ─── String helpers ────────────────────────────────────────────────────────

static auto streq(const char* a, const char* b) -> bool {
    while (*a && *b) { if (*a++ != *b++) return false; }
    return *a == *b;
}

// ─── History ───────────────────────────────────────────────────────────────

struct History {
    static constexpr std::size_t max_entries = 32;
    static constexpr std::size_t max_line    = 128;

    std::array<std::array<char, max_line>, max_entries> entries{};
    std::size_t count = 0;
    std::size_t head  = 0;

    auto push(std::string_view line) -> void {
        if (line.empty()) return;
        if (count > 0 && get(0) == line) return;
        auto& dst = entries[head];
        auto n = std::min(line.size(), max_line - 1);
        for (std::size_t i = 0; i < n; ++i) dst[i] = line[i];
        dst[n] = '\0';
        head = (head + 1) % max_entries;
        if (count < max_entries) ++count;
    }

    auto get(std::size_t idx) const -> std::string_view {
        if (idx >= count) return {};
        auto pos = (head + max_entries - 1 - idx) % max_entries;
        return {entries[pos].data()};
    }
};

static History hist;

// ─── Known PPS commands (for tab completion) ───────────────────────────────
// Shell doesn't know what apps exist. It knows PPS command names.
// Apps register their commands by publishing /pps/cmd/<name> at boot.
// For now, a static list. Future: read /pps/cmd/ directory.

static constexpr std::array command_names = {
    std::string_view{"play"},
    std::string_view{"stop"},
    std::string_view{"mute"},
    std::string_view{"unmute"},
    std::string_view{"volume"},
    std::string_view{"ls"},
    std::string_view{"config"},
    std::string_view{"status"},
    std::string_view{"help"},
    std::string_view{"reboot"},
};

// ─── Line editor ───────────────────────────────────────────────────────────

struct LineEditor {
    std::array<char, History::max_line> buf{};
    std::size_t len = 0;
    std::size_t hist_idx = 0;
    bool browsing = false;

    auto clear() -> void {
        for (std::size_t j = 0; j < len; ++j) uart::putc('\b');
        for (std::size_t j = 0; j < len; ++j) uart::putc(' ');
        for (std::size_t j = 0; j < len; ++j) uart::putc('\b');
        len = 0; buf[0] = '\0';
    }

    auto set(std::string_view s) -> void {
        clear();
        len = std::min(s.size(), buf.size() - 1);
        for (std::size_t i = 0; i < len; ++i) { buf[i] = s[i]; uart::putc(s[i]); }
        buf[len] = '\0';
    }

    auto view() const -> std::string_view { return {buf.data(), len}; }

    auto read() -> std::string_view {
        len = 0; hist_idx = 0; browsing = false;

        while (len < buf.size() - 1) {
            auto c = shell_io::getc();
            if (c == '\r' || c == '\n') { uart::putc('\r'); uart::putc('\n'); break; }
            if (c == 0x7F || c == '\b') {
                if (len > 0) { --len; uart::putc('\b'); uart::putc(' '); uart::putc('\b'); }
                continue;
            }
            if (c == 0x15) { clear(); continue; }  // ctrl-u
            if (c == 0x03) { uart::puts("^C\n"); len = 0; buf[0] = '\0'; return view(); }
            if (c == 0x1B) {  // escape
                if (shell_io::getc() != '[') continue;
                auto seq = shell_io::getc();
                if (seq == 'A' && hist_idx < hist.count) { set(hist.get(hist_idx)); ++hist_idx; browsing = true; }
                else if (seq == 'B' && browsing) {
                    if (hist_idx > 1) { --hist_idx; set(hist.get(hist_idx - 1)); }
                    else { hist_idx = 0; clear(); browsing = false; }
                }
                continue;
            }
            if (c == '\t') {  // tab
                buf[len] = '\0';
                auto prefix = view();
                if (prefix.empty()) continue;
                std::string_view match; int matches = 0;
                for (auto cmd : command_names) { if (cmd.starts_with(prefix)) { match = cmd; ++matches; } }
                if (matches == 1) { set(match); }
                else if (matches > 1) {
                    uart::putc('\n');
                    for (auto cmd : command_names) {
                        if (cmd.starts_with(prefix)) { uart::puts("  "); for (auto ch : cmd) uart::putc(ch); uart::putc('\n'); }
                    }
                    uart::puts("rp4> ");
                    for (std::size_t j = 0; j < len; ++j) uart::putc(buf[j]);
                }
                continue;
            }
            buf[len++] = c; uart::putc(c); browsing = false; hist_idx = 0;
        }
        buf[len] = '\0';
        hist.push(view());
        return view();
    }
};

// ─── Dispatch: parse "key value" → pps.publish("/pps/cmd/key", ...) ────────

static auto dispatch(Kernel& k, std::string_view cmd) -> bool {
    if (cmd.empty()) return true;
    if (cmd == "reboot") return false;

    if (cmd == "help") {
        uart::puts("  <cmd> [arg]   → publishes to /pps/cmd/<cmd>\n");
        uart::puts("  status        → reads /pps/*/status\n");
        uart::puts("  reboot        → PM watchdog reset\n");
        uart::puts("  commands: ");
        for (auto c : command_names) { for (auto ch : c) uart::putc(ch); uart::putc(' '); }
        uart::putc('\n');
        return true;
    }

    auto& pps = k.pps();

    if (cmd == "status") {
        // Read all status objects
        auto entries = pps.list("/pps");
        for (auto& e : entries) {
            uart::puts("  ");
            for (auto ch : e) uart::putc(ch);
            uart::putc('\n');
        }
        return true;
    }

    // Parse "key value" or just "key"
    auto space = cmd.find(' ');
    auto key = (space != std::string_view::npos) ? cmd.substr(0, space) : cmd;
    auto val = (space != std::string_view::npos) ? cmd.substr(space + 1) : std::string_view{"1"};

    // Publish to /pps/cmd/<key>
    std::array<char, 64> path{};
    auto plen = std::min(key.size(), std::size_t{50});
    const char prefix[] = "/pps/cmd/";
    for (std::size_t i = 0; i < 9; ++i) path[i] = prefix[i];
    for (std::size_t i = 0; i < plen; ++i) path[9 + i] = key[i];
    path[9 + plen] = '\0';

    std::array<char, 128> valbuf{};
    auto vlen = std::min(val.size(), valbuf.size() - 1);
    for (std::size_t i = 0; i < vlen; ++i) valbuf[i] = val[i];
    valbuf[vlen] = '\0';

    (void)pps.publish(path.data(), "value", valbuf.data());

    uart::puts("  → ");
    uart::puts(path.data());
    uart::puts(" = ");
    uart::puts(valbuf.data());
    uart::putc('\n');
    return true;
}

// ─── Entry ─────────────────────────────────────────────────────────────────

auto shell_main(Kernel& k) -> void {
    uart::puts("\nrp4 shell — commands publish to /pps/cmd/\n");
    LineEditor ed;
    while (true) {
        uart::puts("rp4> ");
        auto cmd = ed.read();
        if (!dispatch(k, cmd)) break;
    }
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 shell (host build) ===");
    return 0;
}
