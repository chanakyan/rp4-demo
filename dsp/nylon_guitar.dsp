// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// nylon_guitar.dsp — physical model via pm.lib
// Karplus-Strong extended with pluck position and damping.
import("pm.lib");

stringLength = pm.f2l(220);  // A3
pluckPos     = 0.5;
gain         = 0.8;
trigger      = button("pluck");

process = pm.nylonGuitar(stringLength, pluckPos, gain, trigger);
