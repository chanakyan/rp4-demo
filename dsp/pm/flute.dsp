// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// flute.dsp — blown pipe physical model
// Jet + cylindrical bore resonator.
import("pm.lib");

freq     = hslider("freq", 440, 200, 2000, 1);
pressure = hslider("pressure", 0.5, 0, 1, 0.01);
trigger  = button("blow");

process = pm.flute(pm.f2l(freq), pressure, trigger);
