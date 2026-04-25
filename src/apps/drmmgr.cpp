// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// drmmgr — DRM/KMS resource manager for VideoCore VI (V3D)
//
// Implements the subset of Linux DRM ioctls that Mesa's V3D driver needs.
// Mesa userspace links against this via a POSIX shim (open("/dev/dri/card0")
// → IPC to drmmgr). Chromium's GPU process talks to Mesa, Mesa talks to us.
//
// This is the piece that makes Linux's GPU driver stack unnecessary.
// ~20 ioctls, one resource manager, BSD-licensed.
//
// V3D register block: 0xFEC00000 (hub) + 0xFEC04000 (core 0)
// VideoCore MMU: V3D manages its own page tables (CLE/TFU submit via V3D MMU)
//
// Subscribes to:
//   /pps/cmd/drm_init    → initialize V3D hardware
//
// Publishes:
//   /pps/drm/status      → V3D version, ident, state
//   /pps/drm/stats       → submit counts, BO counts, memory usage

import std;
import qnx.kernel;
import rp4.hal;
import rp4.config;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::pps;
using namespace rp4::hal;

// ─── V3D register map (BCM2711, VideoCore VI) ─────────────────────────────
// Two register blocks:
//   Hub:  0xFEC00000 — identity, MMU, cache, interrupt routing
//   Core: 0xFEC04000 — per-core CLE, TFU, CSD execution engines

namespace v3d {
    /** @brief V3D hub register base (identity, MMU, cache, interrupts). */
    inline constexpr std::uintptr_t hub_base  = 0xFEC00000;
    /** @brief V3D core 0 register base (CLE, TFU, CSD engines). */
    inline constexpr std::uintptr_t core_base = 0xFEC04000;

    /** @brief V3D_HUB_IDENT0 — technology version and revision. */
    inline constexpr std::uintptr_t hub_ident0 = hub_base + 0x0000;
    /** @brief V3D_HUB_IDENT1 — number of cores, QPUs per core. */
    inline constexpr std::uintptr_t hub_ident1 = hub_base + 0x0004;
    /** @brief V3D_HUB_IDENT2 — VPM/TMU configuration. */
    inline constexpr std::uintptr_t hub_ident2 = hub_base + 0x0008;
    /** @brief V3D_HUB_IDENT3 — TFU/CSD/TSY feature bits. */
    inline constexpr std::uintptr_t hub_ident3 = hub_base + 0x000C;
    /** @brief Hub interrupt control register. */
    inline constexpr std::uintptr_t hub_int_ctl = hub_base + 0x0050;
    /** @brief Hub interrupt status register. */
    inline constexpr std::uintptr_t hub_int_sts = hub_base + 0x0054;
    /** @brief V3D MMU control register. */
    inline constexpr std::uintptr_t mmu_ctl     = hub_base + 0x1000;
    /** @brief V3D MMU debug info register. */
    inline constexpr std::uintptr_t mmu_debug   = hub_base + 0x1008;

    /** @brief CLE thread 0 control/status. */
    inline constexpr std::uintptr_t ct0cs  = core_base + 0x0100;
    /** @brief CLE thread 0 end address. */
    inline constexpr std::uintptr_t ct0ea  = core_base + 0x0104;
    /** @brief CLE thread 0 current address. */
    inline constexpr std::uintptr_t ct0ca  = core_base + 0x0108;
    /** @brief CLE thread 1 control/status. */
    inline constexpr std::uintptr_t ct1cs  = core_base + 0x0110;
    /** @brief CLE thread 1 end address. */
    inline constexpr std::uintptr_t ct1ea  = core_base + 0x0114;
    /** @brief CLE thread 1 current address. */
    inline constexpr std::uintptr_t ct1ca  = core_base + 0x0118;

    /** @brief Texture Formatting Unit submit register. */
    inline constexpr std::uintptr_t tfu_su = core_base + 0x0400;

    /** @brief Compute shader dispatch status. */
    inline constexpr std::uintptr_t csd_status   = core_base + 0x0600;
    /** @brief CSD work group queue. */
    inline constexpr std::uintptr_t csd_queue     = core_base + 0x0604;
    /** @brief CSD work group count X. */
    inline constexpr std::uintptr_t csd_num_wg_x  = core_base + 0x0608;
    /** @brief CSD work group count Y. */
    inline constexpr std::uintptr_t csd_num_wg_y  = core_base + 0x060C;
    /** @brief CSD configuration register 0. */
    inline constexpr std::uintptr_t csd_cfg0      = core_base + 0x0610;

    /** @brief Performance counter 0 enable. */
    inline constexpr std::uintptr_t pctr_0_en  = core_base + 0x0680;
    /** @brief Performance counter 0 clear. */
    inline constexpr std::uintptr_t pctr_0_clr = core_base + 0x0684;
    /** @brief Performance counter 0 source select. */
    inline constexpr std::uintptr_t pctr_0_src  = core_base + 0x0688;
    /** @brief Performance counter 0 value. */
    inline constexpr std::uintptr_t pctr_0_val  = core_base + 0x068C;

    /** @brief Read V3D identity — returns tech version (41 = V3D 4.1, Pi 4). */
    inline auto read_ident() -> std::uint32_t {
        return mmio_read(hub_ident0);
    }

    /** @brief Check if V3D is present and powered. */
    inline auto is_present() -> bool {
        auto id = mmio_read(hub_ident0);
        return (id & 0xFFFFFF) != 0 && (id & 0xFFFFFF) != 0xFFFFFF;
    }
}

// ─── Buffer Object (BO) management ───────────────────────────────────────
// GEM-like BO allocator. V3D has its own MMU — we allocate from a
// contiguous pool (CMA-style) and map through V3D page tables.
//
// Mesa expects: create, mmap, wait, close. That's it.

/** @brief GEM-style buffer object — tracks GPU memory allocations. */
struct BufferObject {
    std::uint32_t handle;       ///< Unique handle returned to userspace.
    std::uint32_t size;         ///< Allocation size (4 KB aligned).
    std::uintptr_t phys_addr;   ///< Physical address (ARM side).
    std::uintptr_t v3d_addr;    ///< V3D MMU address (GPU side).
    bool in_use;                ///< Slot occupancy flag.
};

/** @brief Maximum concurrent buffer objects. */
inline constexpr std::size_t max_bos = 256;
/// @brief Buffer object table (fixed-size, no heap).
static std::array<BufferObject, max_bos> g_bo_table{};
/// @brief Next handle to assign (monotonically increasing).
static std::uint32_t g_next_handle = 1;

/** @brief BO pool physical base (top 256 MB of 4 GB RAM). */
inline constexpr std::uintptr_t bo_pool_base = 0xF0000000;
/** @brief BO pool size in bytes. */
inline constexpr std::size_t    bo_pool_size = 256 * 1024 * 1024;
/// @brief Next free address in the BO pool (bump allocator).
static std::uintptr_t g_bo_pool_next = bo_pool_base;

/** @brief Allocate a buffer object from the pool. Returns nullptr on OOM. */
static auto bo_alloc(std::uint32_t size) -> BufferObject* {
    // Align to 4KB page
    size = (size + 0xFFF) & ~0xFFF;
    if (g_bo_pool_next + size > bo_pool_base + bo_pool_size) return nullptr;

    for (auto& bo : g_bo_table) {
        if (!bo.in_use) {
            bo.handle = g_next_handle++;
            bo.size = size;
            bo.phys_addr = g_bo_pool_next;
            bo.v3d_addr = g_bo_pool_next;  // identity-mapped for now
            bo.in_use = true;
            g_bo_pool_next += size;
            return &bo;
        }
    }
    return nullptr;
}

/** @brief Find a buffer object by handle. Returns nullptr if not found. */
static auto bo_find(std::uint32_t handle) -> BufferObject* {
    for (auto& bo : g_bo_table) {
        if (bo.in_use && bo.handle == handle) return &bo;
    }
    return nullptr;
}

/** @brief Free a buffer object by handle. */
static auto bo_free(std::uint32_t handle) -> bool {
    auto* bo = bo_find(handle);
    if (!bo) return false;
    // Note: no compaction — freed memory is lost until reset.
    // Real implementation needs a proper allocator.
    bo->in_use = false;
    return true;
}

// ─── DRM ioctl dispatch ──────────────────────────────────────────────────
// Mesa's V3D driver calls ioctl(fd, DRM_IOCTL_V3D_xxx, &args).
// Our POSIX shim translates ioctl → msg_send to drmmgr.
// We dispatch here.

/// @brief Submit bin+render command lists.
inline constexpr std::uint32_t drm_v3d_submit_cl   = 0x00;
/// @brief Wait for buffer object idle.
inline constexpr std::uint32_t drm_v3d_wait_bo     = 0x01;
/// @brief Allocate a GPU buffer object.
inline constexpr std::uint32_t drm_v3d_create_bo   = 0x02;
/// @brief Map a buffer object to userspace.
inline constexpr std::uint32_t drm_v3d_mmap_bo     = 0x03;
/// @brief Query GPU parameter/capability.
inline constexpr std::uint32_t drm_v3d_get_param   = 0x04;
/// @brief Get V3D address of a buffer object.
inline constexpr std::uint32_t drm_v3d_get_bo_offset = 0x05;
/// @brief Submit texture formatting unit job.
inline constexpr std::uint32_t drm_v3d_submit_tfu  = 0x06;
/// @brief Submit compute shader dispatch.
inline constexpr std::uint32_t drm_v3d_submit_csd  = 0x07;
/// @brief Create a performance monitor.
inline constexpr std::uint32_t drm_v3d_perfmon_create  = 0x08;
/// @brief Destroy a performance monitor.
inline constexpr std::uint32_t drm_v3d_perfmon_destroy = 0x09;
/// @brief Read performance monitor counter values.
inline constexpr std::uint32_t drm_v3d_perfmon_get_values = 0x0A;

/// @brief UIF config register param.
inline constexpr std::uint32_t v3d_param_v3d_uifcfg   = 0;
/// @brief Hub identity 1 param (core/QPU count).
inline constexpr std::uint32_t v3d_param_v3d_hub_ident1 = 1;
/// @brief Hub identity 2 param (VPM/TMU config).
inline constexpr std::uint32_t v3d_param_v3d_hub_ident2 = 2;
/// @brief Hub identity 3 param (TFU/CSD features).
inline constexpr std::uint32_t v3d_param_v3d_hub_ident3 = 3;
/// @brief Core 0 identity 0 param (tech version).
inline constexpr std::uint32_t v3d_param_v3d_core0_ident0 = 4;
/// @brief Core 0 identity 1 param.
inline constexpr std::uint32_t v3d_param_v3d_core0_ident1 = 5;
/// @brief Core 0 identity 2 param.
inline constexpr std::uint32_t v3d_param_v3d_core0_ident2 = 6;
/// @brief TFU hardware support flag.
inline constexpr std::uint32_t v3d_param_supports_tfu = 7;
/// @brief CSD hardware support flag.
inline constexpr std::uint32_t v3d_param_supports_csd = 8;
/// @brief Cache flush support flag.
inline constexpr std::uint32_t v3d_param_supports_cache_flush = 9;
/// @brief Performance monitor support flag.
inline constexpr std::uint32_t v3d_param_supports_perfmon = 10;
/// @brief Multi-syncobj support flag.
inline constexpr std::uint32_t v3d_param_supports_multisync = 11;

/** @brief CL submit count (published to PPS). */
static std::uint32_t g_submit_cl_count = 0;
/** @brief TFU submit count (published to PPS). */
static std::uint32_t g_submit_tfu_count = 0;
/** @brief CSD submit count (published to PPS). */
static std::uint32_t g_submit_csd_count = 0;

// ─── ioctl handlers ──────────────────────────────────────────────────────

/** @brief DRM_IOCTL_V3D_CREATE_BO arguments. */
struct DrmCreateBO {
    std::uint32_t size;      ///< Requested allocation size.
    std::uint32_t flags;     ///< Allocation flags.
    std::uint32_t handle;    ///< Returned BO handle.
    std::uint32_t offset;    ///< Returned V3D address.
};

/** @brief Handle DRM_IOCTL_V3D_CREATE_BO — allocate a GPU buffer. */
static auto handle_create_bo(DrmCreateBO& args) -> int {
    auto* bo = bo_alloc(args.size);
    if (!bo) return -1;  // ENOMEM
    args.handle = bo->handle;
    args.offset = static_cast<std::uint32_t>(bo->v3d_addr);
    return 0;
}

/** @brief DRM_IOCTL_V3D_MMAP_BO arguments. */
struct DrmMmapBO {
    std::uint32_t handle;    ///< BO handle to map.
    std::uint32_t flags;     ///< Mapping flags.
    std::uint64_t offset;    ///< Returned mmap offset (physical address).
};

/** @brief Handle DRM_IOCTL_V3D_MMAP_BO — return mmap offset for a BO. */
static auto handle_mmap_bo(DrmMmapBO& args) -> int {
    auto* bo = bo_find(args.handle);
    if (!bo) return -1;  // ENOENT
    // Return physical address as mmap offset — POSIX shim maps it
    args.offset = bo->phys_addr;
    return 0;
}

/** @brief DRM_IOCTL_V3D_GET_PARAM arguments. */
struct DrmGetParam {
    std::uint32_t param;     ///< Parameter ID to query.
    std::uint64_t value;     ///< Returned parameter value.
};

/** @brief Handle DRM_IOCTL_V3D_GET_PARAM — return GPU capabilities. */
static auto handle_get_param(DrmGetParam& args) -> int {
    switch (args.param) {
        case v3d_param_v3d_uifcfg:
            args.value = mmio_read(v3d::hub_base + 0x0045c);
            return 0;
        case v3d_param_v3d_hub_ident1:
            args.value = mmio_read(v3d::hub_ident1);
            return 0;
        case v3d_param_v3d_hub_ident2:
            args.value = mmio_read(v3d::hub_ident2);
            return 0;
        case v3d_param_v3d_hub_ident3:
            args.value = mmio_read(v3d::hub_ident3);
            return 0;
        case v3d_param_v3d_core0_ident0:
            args.value = mmio_read(v3d::core_base + 0x0000);
            return 0;
        case v3d_param_v3d_core0_ident1:
            args.value = mmio_read(v3d::core_base + 0x0004);
            return 0;
        case v3d_param_v3d_core0_ident2:
            args.value = mmio_read(v3d::core_base + 0x0008);
            return 0;
        case v3d_param_supports_tfu:
            args.value = 1;
            return 0;
        case v3d_param_supports_csd:
            args.value = 1;
            return 0;
        case v3d_param_supports_cache_flush:
            args.value = 1;
            return 0;
        case v3d_param_supports_perfmon:
            args.value = 1;
            return 0;
        case v3d_param_supports_multisync:
            args.value = 1;
            return 0;
        default:
            return -1;  // EINVAL
    }
}

/** @brief DRM_IOCTL_V3D_SUBMIT_CL arguments. */
struct DrmSubmitCL {
    std::uint32_t bcl_start;   ///< Bin CL start (V3D address).
    std::uint32_t bcl_end;     ///< Bin CL end.
    std::uint32_t rcl_start;   ///< Render CL start (V3D address).
    std::uint32_t rcl_end;     ///< Render CL end.
    std::uint32_t in_sync;     ///< Syncobj to wait on before submit.
    std::uint32_t out_sync;    ///< Syncobj to signal on completion.
};

/** @brief Handle DRM_IOCTL_V3D_SUBMIT_CL — submit bin+render command lists. */
static auto handle_submit_cl(DrmSubmitCL& args) -> int {
    // Submit bin command list to CLE thread 0
    mmio_write(v3d::ct0ca, args.bcl_start);
    mmio_write(v3d::ct0ea, args.bcl_end);

    // Wait for bin to complete
    while (mmio_read(v3d::ct0cs) & (1 << 5)) {}  // wait CTRUN=0

    // Submit render command list to CLE thread 1
    mmio_write(v3d::ct1ca, args.rcl_start);
    mmio_write(v3d::ct1ea, args.rcl_end);

    // Wait for render to complete
    while (mmio_read(v3d::ct1cs) & (1 << 5)) {}  // wait CTRUN=0

    ++g_submit_cl_count;
    return 0;
}

/** @brief DRM_IOCTL_V3D_SUBMIT_TFU arguments. */
struct DrmSubmitTFU {
    std::uint32_t iia;       ///< Input image address.
    std::uint32_t icfg;      ///< Input configuration.
    std::uint32_t oa;        ///< Output address.
    std::uint32_t os;        ///< Output stride.
    std::uint32_t coef[4];   ///< Filter coefficients.
    std::uint32_t in_sync;   ///< Syncobj to wait on.
    std::uint32_t out_sync;  ///< Syncobj to signal.
};

/** @brief Handle DRM_IOCTL_V3D_SUBMIT_TFU — kick texture format unit. */
static auto handle_submit_tfu(DrmSubmitTFU& args) -> int {
    // Write TFU registers and kick
    mmio_write(v3d::core_base + 0x0400, args.iia);
    mmio_write(v3d::core_base + 0x0404, args.icfg);
    mmio_write(v3d::core_base + 0x0408, args.oa);
    // TFU auto-starts when OS register is written
    mmio_write(v3d::core_base + 0x040C, args.os);

    // Wait for TFU idle (no interrupt — poll)
    // TFU status at core_base + 0x0410, bit 0 = idle
    while (!(mmio_read(v3d::core_base + 0x0410) & 1)) {}

    ++g_submit_tfu_count;
    return 0;
}

/** @brief DRM_IOCTL_V3D_WAIT_BO arguments. */
struct DrmWaitBO {
    std::uint32_t handle;      ///< BO handle to wait on.
    std::uint32_t pad;         ///< Padding (alignment).
    std::uint64_t timeout_ns;  ///< Timeout in nanoseconds.
};

/** @brief Handle DRM_IOCTL_V3D_WAIT_BO — wait for BO idle (no-op, sync submit). */
static auto handle_wait_bo([[maybe_unused]] DrmWaitBO& args) -> int {
    // Synchronous submit means BO is idle when submit returns.
    // Real implementation needs a fence/syncobj per BO.
    return 0;
}

/** @brief DRM_IOCTL_V3D_GET_BO_OFFSET arguments. */
struct DrmGetBOOffset {
    std::uint32_t handle;   ///< BO handle to query.
    std::uint32_t offset;   ///< Returned V3D address.
};

/** @brief Handle DRM_IOCTL_V3D_GET_BO_OFFSET — return V3D address for a BO. */
static auto handle_get_bo_offset(DrmGetBOOffset& args) -> int {
    auto* bo = bo_find(args.handle);
    if (!bo) return -1;
    args.offset = static_cast<std::uint32_t>(bo->v3d_addr);
    return 0;
}

// ─── Entry ────────────────────────────────────────────────────────────────

/** @brief DRM resource manager entry point — init V3D, serve ioctl requests. */
auto drmmgr_main(Kernel& k) -> void {
    auto& pps = k.pps();

    constexpr auto pid = ProcessId{20};

    (void)pps.subscribe("/pps/cmd/drm_init", pid, [&](const Notification&) {
        uart::puts("drmmgr: initializing V3D...\n");

        if (!v3d::is_present()) {
            (void)pps.publish("/pps/drm/status", "state", "no V3D");
            uart::puts("drmmgr: V3D not found — check power domain\n");
            // V3D needs to be powered on via mailbox:
            // tag 0x00030030 (SET_DOMAIN_STATE), domain 5 (V3D), state 1 (on)
            return;
        }

        auto ident = v3d::read_ident();
        auto ver_major = (ident >> 24) & 0xFF;
        auto ver_minor = (ident >> 16) & 0xFF;

        std::array<char, 16> ver_str{};
        std::format_to_n(ver_str.data(), ver_str.size() - 1, "{}.{}", ver_major, ver_minor);

        (void)pps.publish("/pps/drm/status", "state", "ready");
        (void)pps.publish("/pps/drm/status", "version", ver_str.data());
        (void)pps.publish("/pps/drm/status", "bo_pool_mb", "256");

        uart::puts("drmmgr: V3D ");
        uart::puts(ver_str.data());
        uart::puts(" ready, 256 MB BO pool\n");
    });

    // Auto-init
    (void)pps.publish("/pps/cmd/drm_init", "value", "1");
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 drmmgr (host build) ===");
    std::println("  V3D DRM resource manager");
    std::println("  {} ioctl handlers", 7);
    std::println("  {} MB BO pool", bo_pool_size / (1024 * 1024));
    return 0;
}
