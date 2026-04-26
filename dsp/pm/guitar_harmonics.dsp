// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// guitar_harmonics.dsp — natural harmonics on nylon guitar
// Pluck at 1/2, 1/3, 1/4, 1/5 of string for harmonics.
import("pm.lib");

stringLength = pm.f2l(110);  // A2 open string

// Pluck position controls which harmonic rings
// 0.5 = octave, 0.33 = fifth, 0.25 = double octave
harmonic = nentry("harmonic", 2, 2, 5, 1);
pluckPos = 1 / harmonic;
trigger  = button("pluck");

process = pm.nylonGuitar(stringLength, pluckPos, 0.8, trigger);
