// SPDX-License-Identifier: BSD-2-Clause
// Force libc++ into bare-metal mode.
// Included via -include before anything else.
// This must be processed before __config_site via -include flag.
#pragma once

// Satisfy the thread API check — we declare "external" but never use it.
// The kernel's scheduler handles threads; libc++ never calls pthread.
#define _LIBCPP_HAS_THREAD_API_EXTERNAL 1

// Disable features that require OS support we don't provide
#define _LIBCPP_DISABLE_AVAILABILITY
#define _LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER
