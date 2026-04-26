// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// bowed_cello.dsp — cello range bowed string
// Lower frequency, heavier bow pressure than violin.
import("pm.lib");

freq        = hslider("freq", 131, 65, 523, 1);  // C3 range
bowPressure = hslider("pressure", 0.7, 0, 1, 0.01);
bowPosition = hslider("bow_pos", 0.15, 0.05, 0.3, 0.01);
trigger     = button("bow");

process = pm.violin(pm.f2l(freq), bowPressure, bowPosition, trigger);
