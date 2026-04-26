// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// steel_drum.dsp — tuned percussion via modalBar
// Steel drum is a struck metal plate with specific mode ratios.
import("pm.lib");

freq    = hslider("freq", 523, 200, 2000, 1);  // C5
gain    = 0.9;
trigger = button("strike");

process = pm.marimba(freq, gain, trigger);
