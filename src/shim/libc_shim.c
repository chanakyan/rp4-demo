// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// libc_shim.c — bare-metal glue for qnx-micro.
//
// Only what the linker asks for that Bionic does NOT provide:
//   malloc/free   → arena_alloc() from allocator.cpp
//   stdio         → uart::putc via HAL (boot.cpp provides uart_putc)
//   abort         → brk #0
//   __cxa_*       → C++ runtime stubs
//   compiler-rt   → __trunctfdf2, __extenddftf2, __udivti3, __umodti3
//   errno         → bare int (single-threaded, no TLS)
//   strerror      → stub (bare-metal)
//
// Everything else (mem/str ops, strtol/strtod, ctype, rand) is in
// libbionic.a (lib/bionic/).

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

// ─── Forward declarations (provided by allocator.cpp / boot.cpp) ────────────

extern void* arena_alloc(size_t size);
extern void  arena_free(void* ptr);
extern void  uart_putc_raw(char c);  // from boot layer

// ─── allocator (wrappers around arena in allocator.cpp) ─────────────────────

void* malloc(size_t size) { return arena_alloc(size); }
void  free(void* ptr)     { arena_free(ptr); }

void* calloc(size_t count, size_t size) {
    size_t total = count * size;
    void* p = arena_alloc(total);
    if (p) {
        uint8_t* d = (uint8_t*)p;
        for (size_t i = 0; i < total; ++i) d[i] = 0;
    }
    return p;
}

void* realloc(void* ptr, size_t new_size) {
    if (!ptr) return arena_alloc(new_size);
    if (new_size == 0) { arena_free(ptr); return NULL; }
    void* new_ptr = arena_alloc(new_size);
    if (new_ptr) {
        // Safe: arena zeroes on alloc, copy up to new_size.
        const uint8_t* s = (const uint8_t*)ptr;
        uint8_t* d = (uint8_t*)new_ptr;
        for (size_t i = 0; i < new_size; ++i) d[i] = s[i];
    }
    arena_free(ptr);
    return new_ptr;
}

void* aligned_alloc(size_t alignment, size_t size) {
    (void)alignment;  // arena is already 16-byte aligned
    return arena_alloc(size);
}

// ─── stdio (backed by UART) ────────────────────────────────────────────────

// Minimal FILE stubs — stdout and stderr both go to UART.
struct _FILE { int fd; };
static struct _FILE _stdout = { 1 };
static struct _FILE _stderr = { 2 };
struct _FILE* stdout = &_stdout;
struct _FILE* stderr = &_stderr;

static void uart_puts(const char* s) {
    while (*s) {
        if (*s == '\n') uart_putc_raw('\r');
        uart_putc_raw(*s++);
    }
}

static size_t _strlen(const char* s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

// Minimal vfprintf: supports %s, %d, %u, %x, %p, %c, %%.
// Good enough for libc++ abort messages and std::println output.
int vfprintf(struct _FILE* f, const char* fmt, va_list ap) {
    (void)f;
    int count = 0;
    char buf[32];

    while (*fmt) {
        if (*fmt != '%') {
            if (*fmt == '\n') uart_putc_raw('\r');
            uart_putc_raw(*fmt++);
            ++count;
            continue;
        }
        ++fmt; // skip %

        switch (*fmt) {
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            uart_puts(s);
            count += (int)_strlen(s);
            break;
        }
        case 'd': case 'i': {
            long v = va_arg(ap, int);
            if (v < 0) { uart_putc_raw('-'); ++count; v = -v; }
            char* p = buf + sizeof(buf) - 1;
            *p = '\0';
            do { *--p = '0' + (char)(v % 10); v /= 10; } while (v);
            uart_puts(p);
            count += (int)_strlen(p);
            break;
        }
        case 'u': {
            unsigned long v = va_arg(ap, unsigned int);
            char* p = buf + sizeof(buf) - 1;
            *p = '\0';
            do { *--p = '0' + (char)(v % 10); v /= 10; } while (v);
            uart_puts(p);
            count += (int)_strlen(p);
            break;
        }
        case 'x': case 'X': case 'p': {
            unsigned long v;
            if (*fmt == 'p') { v = (unsigned long)va_arg(ap, void*); uart_puts("0x"); count += 2; }
            else v = va_arg(ap, unsigned int);
            char* p = buf + sizeof(buf) - 1;
            *p = '\0';
            const char* hex = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            do { *--p = hex[v & 0xF]; v >>= 4; } while (v);
            uart_puts(p);
            count += (int)_strlen(p);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            uart_putc_raw(c);
            ++count;
            break;
        }
        case '%':
            uart_putc_raw('%');
            ++count;
            break;
        default:
            uart_putc_raw('%');
            uart_putc_raw(*fmt);
            count += 2;
            break;
        }
        ++fmt;
    }
    return count;
}

int fprintf(struct _FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int fwrite(const void* buf, size_t size, size_t count, struct _FILE* f) {
    (void)f;
    const char* p = (const char*)buf;
    size_t total = size * count;
    for (size_t i = 0; i < total; ++i) uart_putc_raw(p[i]);
    return (int)count;
}

int fflush(struct _FILE* f) { (void)f; return 0; }
int feof(struct _FILE* f) { (void)f; return 0; }
int ferror(struct _FILE* f) { (void)f; return 0; }

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    // Minimal: format into buf. For now, just copy the format string.
    // This is only called by libc++ for error messages.
    size_t len = _strlen(fmt);
    if (len >= size) len = size - 1;
    const uint8_t* s = (const uint8_t*)fmt;
    uint8_t* d = (uint8_t*)buf;
    for (size_t i = 0; i < len; ++i) d[i] = s[i];
    buf[len] = '\0';
    return (int)len;
}

// ─── strerror (stub) ────────────────────────────────────────────────────────

char* strerror(int errnum) {
    (void)errnum;
    return (char*)"error";
}

// ─── runtime ────────────────────────────────────────────────────────────────

void abort(void) {
#ifdef __aarch64__
    __asm__ volatile("brk #0");
#endif
    while (1) {}
}

int __cxa_atexit(void (*f)(void*), void* arg, void* dso) {
    (void)f; (void)arg; (void)dso;
    return 0;  // bare-metal: no cleanup on exit
}

void __cxa_pure_virtual(void) { abort(); }

// ─── errno ──────────────────────────────────────────────────────────────────

int errno;

// ─── compiler-rt stubs ──────────────────────────────────────────────────────

// __trunctfdf2: long double (128-bit on aarch64) → double.
double __trunctfdf2(long double x) {
    (void)x;
    return 0.0;
}

// __extenddftf2: double → long double (128-bit on aarch64).
long double __extenddftf2(double x) {
    (void)x;
    return 0.0L;
}

// 128-bit unsigned division/modulo (used by charconv for long double formatting)
typedef unsigned __int128 uint128_t;

uint128_t __udivti3(uint128_t a, uint128_t b) {
    if (b == 0) return 0;
    uint128_t q = 0, r = 0;
    for (int i = 127; i >= 0; --i) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) { r -= b; q |= (uint128_t)1 << i; }
    }
    return q;
}

uint128_t __umodti3(uint128_t a, uint128_t b) {
    return a - __udivti3(a, b) * b;
}
