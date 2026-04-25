// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// C++ mangled symbols that libc++ expects.
// Separate file because libc_shim.c is C99.

struct __file;
using FILE = __file;

namespace std {
inline namespace __1 {
    bool __is_posix_terminal(FILE* f) {
        (void)f;
        return true;  // UART is always a terminal
    }
}
}
