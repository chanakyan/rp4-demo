// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// top — process monitor
//
// Reads from the process manager, formats a table, writes to console.
// Not a resource manager — it's a client that reads /proc and displays.
//
// Output format (like QNX pidin):
//   PID  NAME          STATE    THREADS  MEM     PRI
//   100  audiomgr      running  1        3840    20
//   101  console       running  1        0       15
//   102  dash          running  1        0       10

import std;
import qnx.kernel;
import rp4.proc;

using namespace qnx;
using namespace qnx::kernel;
using namespace rp4::proc;

/**
 * @brief Format and print the process table.
 * @param pm Process manager to query.
 */
auto top_display(const ProcessManager& pm) -> void {
    std::println("  PID  NAME            STATE    THREADS  MEM       PRI");
    std::println("  ---  ----            -----    -------  ---       ---");

    for (const auto& p : pm.all()) {
        if (p.state == ProcessState::dead) continue;

        std::string_view state_str = "unknown";
        switch (p.state) {
            case ProcessState::running: state_str = "running"; break;
            case ProcessState::stopped: state_str = "stopped"; break;
            case ProcessState::zombie:  state_str = "zombie";  break;
            case ProcessState::dead:    state_str = "dead";    break;
        }

        // Get max priority among threads
        int max_pri = 0;
        for (auto tid : p.threads) {
            // We'd query scheduler here — for display we use thread count
        }

        std::println("  {:<4} {:<15} {:<8} {:<8} {:<9} {}",
                     p.pid.value, p.name, state_str,
                     p.threads.size(), p.mem_used, max_pri);
    }

    std::println("");
    std::println("  processes: {}  threads: {}",
                 pm.count(),
                 [&]() {
                     std::size_t n = 0;
                     for (const auto& p : pm.all())
                         if (p.state == ProcessState::running) n += p.threads.size();
                     return n;
                 }());
}

/**
 * @brief top entry point — spawn some processes and display.
 */
auto top_main(Kernel& k) -> void {
    ProcessManager pm{k};

    // Spawn system processes
    auto audio_pid = pm.spawn(SpawnInfo{
        .name = "audiomgr", .priority = Priority{20}, .parent = ProcessId{0}});
    auto con_pid = pm.spawn(SpawnInfo{
        .name = "console", .priority = Priority{15}, .parent = ProcessId{0}});
    auto shell_pid = pm.spawn(SpawnInfo{
        .name = "dash", .priority = Priority{10}, .parent = ProcessId{0}});
    auto net_pid = pm.spawn(SpawnInfo{
        .name = "lwip", .priority = Priority{18}, .parent = ProcessId{0}});
    auto db_pid = pm.spawn(SpawnInfo{
        .name = "sqlite", .priority = Priority{8}, .parent = ProcessId{0}});

    // Track some memory
    if (audio_pid) pm.track_alloc(*audio_pid, 3840);   // audio frame
    if (db_pid)    pm.track_alloc(*db_pid, 65536);      // sqlite page cache
    if (net_pid)   pm.track_alloc(*net_pid, 16384);     // lwip buffers

    // Add extra threads
    if (net_pid) (void)pm.thread_add(*net_pid, Priority{18});  // rx thread
    if (db_pid)  (void)pm.thread_add(*db_pid, Priority{8});    // wal thread

    std::println("=== rp4 top ===");
    std::println("");
    top_display(pm);

    // Kill a process
    if (db_pid) {
        std::println("  kill sqlite (pid={})", db_pid->value);
        (void)pm.kill(*db_pid);
        std::println("");
        top_display(pm);
    }
}

int main() {
    Kernel k;
    k.init();
    top_main(k);
    return 0;
}
