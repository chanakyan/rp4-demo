// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// flanger.dsp — short modulated delay with feedback
// Same topology as chorus but shorter delay = comb filtering.
import("stdfaust.lib");

rate  = hslider("rate", 0.5, 0.01, 5, 0.01);
depth = hslider("depth", 0.003, 0, 0.01, 0.0001);
fb    = hslider("feedback", 0.7, 0, 0.95, 0.01);
mix   = hslider("mix", 0.5, 0, 1, 0.01);

lfo = os.osc(rate) * depth * ma.SR;
center = int(ma.SR * 0.005);

flanger = + ~ (@(center + int(lfo)) * fb);

process = _ <: (_ * (1 - mix)), (_ : flanger * mix) :> _;
