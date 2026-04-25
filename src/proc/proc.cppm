// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// rp4.proc — process manager
//
// QNX procnto equivalent. Manages the process table, spawn/kill,
// and /proc resource manager for top/ps.
//
// Every process is a thread group with a shared address space.
// On bare-metal without MMU, processes are cooperative: they share
// the single address space but are tracked separately for
// scheduling, resource accounting, and IPC routing.
//
// Design (from QNX):
//   - Process = pid + thread group + open connections + capabilities
//   - Spawn = create thread(s), register in process table
//   - Kill = destroy threads, revoke capabilities, cleanup connections
//   - /proc = resource manager that exposes process table via IPC

export module rp4.proc;
import std;
import qnx.kernel;

export namespace rp4::proc {

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::scheduler;
using namespace qnx::ipc;
using namespace qnx::memory;

// ─── Process descriptor ────────────────────────────────────────────────────

/** @brief Process state in the lifecycle. */
enum class ProcessState {
    running,    ///< At least one thread is alive
    stopped,    ///< All threads stopped (signal)
    zombie,     ///< Exited, waiting for parent to wait()
    dead        ///< Fully cleaned up
};

/** @brief A process: named thread group with resource tracking. */
struct Process {
    ProcessId               pid;          ///< Unique process identifier
    std::string             name;         ///< Executable name (e.g. "audiomgr")
    ProcessState            state;        ///< Lifecycle state
    std::vector<ThreadId>   threads;      ///< Threads owned by this process
    std::vector<ChannelId>  channels;     ///< IPC channels created
    std::vector<ConnectionId> connections;///< IPC connections held
    std::vector<CapabilityId> capabilities;///< Memory capabilities held
    std::size_t             mem_used;     ///< Bytes allocated (tracked)
    int                     exit_code;    ///< Exit status (valid in zombie state)
    ProcessId               parent;       ///< Parent pid (who spawned this)
};

// ─── Spawn descriptor ──────────────────────────────────────────────────────

/** @brief Parameters for spawning a new process. */
struct SpawnInfo {
    std::string name;       ///< Process name
    Priority    priority;   ///< Initial thread priority
    ProcessId   parent;     ///< Parent process
};

// ─── Process manager ───────────────────────────────────────────────────────

/**
 * @brief Process manager — owns the process table, handles spawn/kill/wait.
 *
 * QNX procnto design: the process table is the source of truth for
 * what's running. Every spawn goes through here. Every kill cleans
 * up through here. /proc reads from here.
 */
class ProcessManager {
    std::vector<Process> procs_;
    int next_pid_ = 100;  // pids 0-99 reserved for kernel/boot
    std::reference_wrapper<Kernel> kernel_;

public:
    /**
     * @brief Construct the process manager with a reference to the kernel.
     * @param k Kernel instance (for scheduler, IPC, memory access).
     */
    explicit ProcessManager(Kernel& k) : kernel_{k} {}

    // ─── Spawn ─────────────────────────────────────────────────────────────

    /**
     * @brief Spawn a new process.
     *
     * Creates a process table entry, creates the initial thread via
     * the kernel scheduler, and returns the new pid.
     *
     * @param info Spawn parameters (name, priority, parent).
     * @return ProcessId of the new process.
     */
    [[nodiscard]] auto spawn(SpawnInfo info) -> Result<ProcessId> {
        auto pid = ProcessId{next_pid_++};
        auto tid = kernel_.get().scheduler().thread_create(pid, info.priority);
        if (!tid) return std::unexpected(tid.error());

        procs_.push_back(Process{
            .pid = pid,
            .name = std::move(info.name),
            .state = ProcessState::running,
            .threads = {*tid},
            .channels = {},
            .connections = {},
            .capabilities = {},
            .mem_used = 0,
            .exit_code = 0,
            .parent = info.parent
        });

        return pid;
    }

    // ─── Kill ──────────────────────────────────────────────────────────────

    /**
     * @brief Kill a process: destroy all threads, revoke capabilities, cleanup.
     *
     * @param pid Process to kill.
     * @return Void on success.
     */
    [[nodiscard]] auto kill(ProcessId pid) -> VoidResult {
        auto idx = find(pid);
        if (!idx) return std::unexpected(KernelError::not_found);
        auto& p = procs_[*idx];

        // Destroy all threads
        for (auto tid : p.threads) {
            (void)kernel_.get().scheduler().thread_destroy(tid);
        }

        // Revoke all capabilities (cascade)
        for (auto cap : p.capabilities) {
            (void)kernel_.get().memory().capability_revoke(cap);
        }

        // Unregister any resource managers this process owned
        // (handled by driver::Namespace when we add unregister-by-pid)

        p.state = ProcessState::zombie;
        p.threads.clear();
        p.channels.clear();
        p.connections.clear();
        p.capabilities.clear();

        return {};
    }

    // ─── Wait ──────────────────────────────────────────────────────────────

    /**
     * @brief Wait for a child process to exit (reap zombie).
     *
     * @param parent Parent pid.
     * @return Exit code of the reaped child, or error if no zombie children.
     */
    [[nodiscard]] auto wait(ProcessId parent) -> Result<int> {
        for (auto it = procs_.begin(); it != procs_.end(); ++it) {
            if (it->parent == parent && it->state == ProcessState::zombie) {
                int code = it->exit_code;
                it->state = ProcessState::dead;
                return code;
            }
        }
        return std::unexpected(KernelError::would_block);
    }

    // ─── Exit ──────────────────────────────────────────────────────────────

    /**
     * @brief Process exits voluntarily with an exit code.
     *
     * @param pid Process that is exiting.
     * @param code Exit status.
     * @return Void on success.
     */
    [[nodiscard]] auto exit(ProcessId pid, int code) -> VoidResult {
        auto idx = find(pid);
        if (!idx) return std::unexpected(KernelError::not_found);
        auto& p = procs_[*idx];

        // Destroy threads
        for (auto tid : p.threads) {
            (void)kernel_.get().scheduler().thread_destroy(tid);
        }

        p.state = ProcessState::zombie;
        p.exit_code = code;
        p.threads.clear();

        return {};
    }

    // ─── Thread management ─────────────────────────────────────────────────

    /**
     * @brief Add a thread to an existing process.
     *
     * @param pid Process to add thread to.
     * @param pri Priority for the new thread.
     * @return ThreadId of the new thread.
     */
    [[nodiscard]] auto thread_add(ProcessId pid, Priority pri) -> Result<ThreadId> {
        auto idx = find(pid);
        if (!idx) return std::unexpected(KernelError::not_found);
        auto& p = procs_[*idx];

        auto tid = kernel_.get().scheduler().thread_create(pid, pri);
        if (!tid) return std::unexpected(tid.error());

        p.threads.push_back(*tid);
        return *tid;
    }

    // ─── Resource tracking ─────────────────────────────────────────────────

    /** @brief Track a channel owned by a process. */
    auto track_channel(ProcessId pid, ChannelId ch) -> void {
        if (auto i = find(pid)) procs_[*i].channels.push_back(ch);
    }

    /** @brief Track a connection owned by a process. */
    auto track_connection(ProcessId pid, ConnectionId conn) -> void {
        if (auto i = find(pid)) procs_[*i].connections.push_back(conn);
    }

    /** @brief Track a capability owned by a process. */
    auto track_capability(ProcessId pid, CapabilityId cap) -> void {
        if (auto i = find(pid)) procs_[*i].capabilities.push_back(cap);
    }

    /** @brief Track memory allocation. */
    auto track_alloc(ProcessId pid, std::size_t bytes) -> void {
        if (auto i = find(pid)) procs_[*i].mem_used += bytes;
    }

    // ─── Queries (for /proc and top) ───────────────────────────────────────

    /** @brief Number of live processes. */
    [[nodiscard]] auto count() const -> std::size_t {
        return std::ranges::count_if(procs_, [](const Process& p) {
            return p.state == ProcessState::running;
        });
    }

    /** @brief Total number of processes (including zombies). */
    [[nodiscard]] auto total() const -> std::size_t { return procs_.size(); }

    /** @brief Get process info by pid (for /proc/<pid>/stat). */
    [[nodiscard]] auto info(ProcessId pid) const -> std::optional<std::reference_wrapper<const Process>> {
        for (const auto& p : procs_) {
            if (p.pid == pid) return std::cref(p);
        }
        return std::nullopt;
    }

    /** @brief Get all processes (for top). */
    [[nodiscard]] auto all() const -> std::span<const Process> {
        return procs_;
    }

    /** @brief Find process by name. */
    [[nodiscard]] auto find_by_name(std::string_view name) const
        -> std::optional<std::reference_wrapper<const Process>> {
        for (const auto& p : procs_) {
            if (p.name == name) return std::cref(p);
        }
        return std::nullopt;
    }

private:
    [[nodiscard]] auto find(ProcessId pid) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < procs_.size(); ++i) {
            if (procs_[i].pid == pid) return i;
        }
        return std::nullopt;
    }
};

} // namespace rp4::proc
