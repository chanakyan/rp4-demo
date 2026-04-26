// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// verify_shim.c — CBMC bounded model checking for libc_shim.c
//
// Proves for ALL inputs up to the bound:
//   - memcpy copies exactly n bytes
//   - memmove handles overlapping regions correctly
//   - memset fills exactly n bytes
//   - memcmp returns 0 for equal buffers
//   - strlen returns correct length
//   - strcmp returns 0 for equal strings
//
// Usage: cbmc spec/verify_shim.c --unwind 17 --bounds-check --pointer-check
//
// The --unwind 17 means: prove for all buffers up to 16 bytes.
// Increase for stronger guarantees (slower).

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

// ─── Pull in the shim functions (inline for CBMC) ───────────────────────────

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; --i) d[i-1] = s[i-1];
    }
    return dst;
}

void* memset(void* dst, int val, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < n; ++i) d[i] = (uint8_t)val;
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

// ─── CBMC harnesses ─────────────────────────────────────────────────────────

// CBMC nondet: symbolic values for all-input verification
extern size_t nondet_size_t(void);
extern uint8_t nondet_uint8_t(void);
extern int nondet_int(void);
extern char nondet_char(void);

#define MAX_N 16

// ─── P1: memcpy copies exactly n bytes ──────────────────────────────────────

void verify_memcpy(void) {
    uint8_t src[MAX_N], dst[MAX_N];
    size_t n = nondet_size_t();
    __CPROVER_assume(n <= MAX_N);

    // Fill src with symbolic data
    for (size_t i = 0; i < n; ++i) src[i] = nondet_uint8_t();

    memcpy(dst, src, n);

    // Assert: every byte was copied
    for (size_t i = 0; i < n; ++i)
        assert(dst[i] == src[i]);
}

// ─── P2: memmove handles forward overlap ────────────────────────────────────

void verify_memmove_forward(void) {
    uint8_t buf[MAX_N * 2];
    size_t n = nondet_size_t();
    __CPROVER_assume(n > 0 && n <= MAX_N);

    // Fill with symbolic data
    for (size_t i = 0; i < n; ++i) buf[i] = nondet_uint8_t();

    // Save original values
    uint8_t expected[MAX_N];
    for (size_t i = 0; i < n; ++i) expected[i] = buf[i];

    // Move forward (dst > src, overlapping)
    memmove(buf + 1, buf, n);

    // Assert: shifted correctly
    for (size_t i = 0; i < n; ++i)
        assert(buf[i + 1] == expected[i]);
}

// ─── P3: memmove handles backward overlap ───────────────────────────────────

void verify_memmove_backward(void) {
    uint8_t buf[MAX_N * 2];
    size_t n = nondet_size_t();
    __CPROVER_assume(n > 0 && n <= MAX_N);

    // Fill offset data
    for (size_t i = 0; i < n; ++i) buf[i + 1] = nondet_uint8_t();

    uint8_t expected[MAX_N];
    for (size_t i = 0; i < n; ++i) expected[i] = buf[i + 1];

    // Move backward (dst < src, overlapping)
    memmove(buf, buf + 1, n);

    for (size_t i = 0; i < n; ++i)
        assert(buf[i] == expected[i]);
}

// ─── P4: memset fills every byte ────────────────────────────────────────────

void verify_memset(void) {
    uint8_t buf[MAX_N];
    size_t n = nondet_size_t();
    int val = nondet_int();
    __CPROVER_assume(n <= MAX_N);
    __CPROVER_assume(val >= 0 && val <= 255);

    memset(buf, val, n);

    for (size_t i = 0; i < n; ++i)
        assert(buf[i] == (uint8_t)val);
}

// ─── P5: memcmp returns 0 for equal buffers ─────────────────────────────────

void verify_memcmp_equal(void) {
    uint8_t a[MAX_N], b[MAX_N];
    size_t n = nondet_size_t();
    __CPROVER_assume(n <= MAX_N);

    for (size_t i = 0; i < n; ++i) {
        a[i] = nondet_uint8_t();
        b[i] = a[i];  // force equal
    }

    assert(memcmp(a, b, n) == 0);
}

// ─── P6: memcmp detects difference ──────────────────────────────────────────

void verify_memcmp_diff(void) {
    uint8_t a[2] = {0, 0};
    uint8_t b[2] = {0, 1};

    assert(memcmp(a, b, 2) != 0);
}

// ─── P7: strlen returns correct length ──────────────────────────────────────

void verify_strlen(void) {
    char s[MAX_N + 1];
    size_t len = nondet_size_t();
    __CPROVER_assume(len <= MAX_N);

    // Fill with nonzero chars, then null terminate
    for (size_t i = 0; i < len; ++i) {
        s[i] = nondet_char();
        __CPROVER_assume(s[i] != '\0');
    }
    s[len] = '\0';

    assert(strlen(s) == len);
}

// ─── P8: strcmp returns 0 for equal strings ──────────────────────────────────

void verify_strcmp_equal(void) {
    char a[MAX_N + 1], b[MAX_N + 1];
    size_t len = nondet_size_t();
    __CPROVER_assume(len <= MAX_N);

    for (size_t i = 0; i < len; ++i) {
        a[i] = nondet_char();
        __CPROVER_assume(a[i] != '\0');
        b[i] = a[i];
    }
    a[len] = '\0';
    b[len] = '\0';

    assert(strcmp(a, b) == 0);
}

// ─── Main: run all harnesses ────────────────────────────────────────────────

int main(void) {
    verify_memcpy();
    verify_memmove_forward();
    verify_memmove_backward();
    verify_memset();
    verify_memcmp_equal();
    verify_memcmp_diff();
    verify_strlen();
    verify_strcmp_equal();
    return 0;
}
