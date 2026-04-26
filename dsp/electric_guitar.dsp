// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// electric_guitar.dsp — electric guitar physical model
import("pm.lib");

stringLength = pm.f2l(330);  // E4
pluckPos     = 0.3;
gain         = 0.9;
trigger      = button("pluck");

process = pm.elecGuitar(stringLength, pluckPos, gain, trigger);
