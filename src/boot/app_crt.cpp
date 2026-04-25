// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// app_crt.cpp — resource manager runtime (the "crt0" for rp4 apps)
//
// Compiled once. Linked with any app for host testing.
// The app provides app_main(Kernel&). This file provides main().
//
// Host:  clang++ app.o app_crt.o -o app
// Cross: app_crt is not used — boot.cpp calls app entry points directly.

import std;
import qnx.kernel;

using namespace qnx::kernel;

/// App provides this. extern "C" — no mangling. objcopy renames trivially.
extern "C" void app_main(Kernel&);

int main() {
    Kernel k;
    k.init();
    app_main(k);
    return 0;
}
