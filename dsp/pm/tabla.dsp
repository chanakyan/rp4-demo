// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// tabla.dsp — Indian percussion via struck membrane model
// Lower tuning, higher gain for the bass drum (bayan) character.
import("pm.lib");

freq    = hslider("freq", 110, 50, 300, 1);  // low tuning
gain    = 1.0;
trigger = button("strike");

process = pm.djembe(freq, gain, trigger);
