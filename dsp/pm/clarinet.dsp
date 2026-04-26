// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// clarinet.dsp — single reed physical model
// Reed + cylindrical bore. Odd harmonics dominate.
import("pm.lib");

freq     = hslider("freq", 261, 100, 1500, 1);
pressure = hslider("pressure", 0.6, 0, 1, 0.01);
trigger  = button("blow");

process = pm.clarinet(pm.f2l(freq), pressure, trigger);
