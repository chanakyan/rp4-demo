// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// ks_chromatic.dsp — Karplus-Strong across one octave
// 12 parallel strings triggered by buttons.
// Demonstrates Par combinator at scale.
import("pm.lib");

note(midi) = pm.ks(pm.f2l(440 * 2 ^ ((midi - 69) / 12)), 0.5);

// C4 to B4 (MIDI 60–71)
process = note(60), note(62), note(64), note(65),
          note(67), note(69), note(71), note(72);
