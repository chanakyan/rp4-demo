// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// chorus.dsp — modulated delay for thickening
// LFO modulates delay time around a center point.
import("stdfaust.lib");

rate  = hslider("rate", 1.5, 0.1, 10, 0.01);
depth = hslider("depth", 0.002, 0, 0.01, 0.0001);
mix   = hslider("mix", 0.5, 0, 1, 0.01);

// LFO: sine oscillator scaled to delay range
lfo = os.osc(rate) * depth * ma.SR;

// Modulated delay
chorus = _ <: _, @(int(ma.SR * 0.02 + lfo)) :> *(0.5);

process = _ <: (_ * (1 - mix)), (_ : chorus * mix) :> _;
