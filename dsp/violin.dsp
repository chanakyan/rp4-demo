// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// violin.dsp — bowed string physical model
import("pm.lib");

stringLength = pm.f2l(440);  // A4
bowPressure  = 0.5;
bowPosition  = 0.12;
trigger      = button("bow");

process = pm.violin(stringLength, bowPressure, bowPosition, trigger);
