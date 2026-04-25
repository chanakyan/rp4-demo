// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// Bare-metal memory allocator for rp4.
//
// QNX way: fixed pools, deterministic, O(1), no fragmentation.
// The kernel uses std::vector which calls operator new/delete.
// We back those with a static arena + free-list allocator.
//
// Design:
//   - 1 MB static arena in BSS (no MMU, no heap, no brk)
//   - Free-list with coalescing (supports vector growth/shrink)
//   - Deterministic: bounded allocation time, no syscalls
//   - Failure = abort (microkernel has no OOM recovery path)
//
// Why not bump-only:
//   std::vector does realloc patterns (grow, copy, free old).
//   A bump allocator leaks on every vector resize.
//   Free-list handles this correctly.

#include <cstddef>
#include <cstdint>
#include <cstring>

// ─── Arena ──────────────────────────────────────────────────────────────────

// 1 MB static pool — lives in BSS, zero-initialized by _start
static constexpr std::size_t arena_size = 1024 * 1024;
alignas(16) static std::uint8_t arena[arena_size];

// ─── Free-list block header ─────────────────────────────────────────────────

struct Block {
    std::size_t size;   // usable size (excludes header)
    Block*      next;   // next free block (null if allocated or last)
    bool        free;   // true if available
};

static constexpr std::size_t block_hdr = sizeof(Block);
static constexpr std::size_t min_alloc = 16;  // minimum usable block size

// Head of free list
static Block* free_list = nullptr;
static bool   initialized = false;

// ─── Init: single free block spanning entire arena ──────────────────────────

static void arena_init() {
    free_list = reinterpret_cast<Block*>(arena);
    free_list->size = arena_size - block_hdr;
    free_list->next = nullptr;
    free_list->free = true;
    initialized = true;
}

// ─── Align up to 16 bytes ───────────────────────────────────────────────────

static constexpr std::size_t align_up(std::size_t n) {
    return (n + 15) & ~std::size_t{15};
}

// ─── Allocate ───────────────────────────────────────────────────────────────

extern "C" void* arena_alloc(std::size_t requested) {
    if (!initialized) arena_init();
    if (requested == 0) requested = min_alloc;

    std::size_t needed = align_up(requested);

    // First-fit search
    Block* curr = free_list;
    while (curr) {
        if (curr->free && curr->size >= needed) {
            // Split if remainder is large enough for another block
            std::size_t remainder = curr->size - needed;
            if (remainder > block_hdr + min_alloc) {
                auto* split = reinterpret_cast<Block*>(
                    reinterpret_cast<std::uint8_t*>(curr) + block_hdr + needed);
                split->size = remainder - block_hdr;
                split->next = curr->next;
                split->free = true;

                curr->size = needed;
                curr->next = split;
            }

            curr->free = false;
            return reinterpret_cast<std::uint8_t*>(curr) + block_hdr;
        }
        curr = curr->next;
    }

    // Out of memory — deterministic failure
    // In a real QNX microkernel this would return ENOMEM.
    // Here we trap (bare-metal has no recovery).
#ifndef RP4_HOST_BUILD
    asm volatile("brk #0");
#endif
    return nullptr;
}

// ─── Free + coalesce ────────────────────────────────────────────────────────

extern "C" void arena_free(void* ptr) {
    if (!ptr) return;

    auto* block = reinterpret_cast<Block*>(
        static_cast<std::uint8_t*>(ptr) - block_hdr);
    block->free = true;

    // Coalesce adjacent free blocks (forward pass)
    Block* curr = free_list;
    while (curr) {
        if (curr->free && curr->next && curr->next->free) {
            curr->size += block_hdr + curr->next->size;
            curr->next = curr->next->next;
            continue;  // check again (may coalesce multiple)
        }
        curr = curr->next;
    }
}

// operator new/delete, libc stubs, and runtime are now in src/shim/libc_shim.c
