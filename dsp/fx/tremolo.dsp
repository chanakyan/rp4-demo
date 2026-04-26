// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// tremolo.dsp — amplitude modulation
// LFO modulates gain. Classic Fender amp effect.
import("stdfaust.lib");

rate  = hslider("rate", 5, 0.5, 20, 0.1);
depth = hslider("depth", 0.5, 0, 1, 0.01);

// Unipolar LFO: 0..(1-depth) + depth
mod = 1 - depth * (1 - os.osc(rate)) * 0.5;

process = _ * mod;
