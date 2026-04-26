// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// string_quartet.dsp — 4 bowed strings in parallel
// Violin 1, Violin 2, Viola, Cello — all via pm.violin.
// Demonstrates Par combinator with 4 independent physical models.
import("pm.lib");

bow = button("bow");
pressure = 0.5;
pos = 0.12;

violin1 = pm.violin(pm.f2l(660), pressure, pos, bow);   // E5
violin2 = pm.violin(pm.f2l(440), pressure, pos, bow);   // A4
viola   = pm.violin(pm.f2l(330), pressure, pos, bow);    // E4
cello   = pm.violin(pm.f2l(131), pressure, 0.15, bow);  // C3

process = violin1, violin2, viola, cello :> *(0.25);
