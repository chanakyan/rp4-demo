// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// phaser.dsp — 4-stage allpass phaser
// Notch frequencies swept by LFO. Classic 70s effect.
import("stdfaust.lib");

rate  = hslider("rate", 0.5, 0.01, 5, 0.01);
depth = hslider("depth", 0.7, 0, 1, 0.01);
fb    = hslider("feedback", 0.5, 0, 0.9, 0.01);

// LFO
lfo = (1 + os.osc(rate)) * 0.5;

// All-pass stage: notch at frequency f
ap1(f) = fi.allpassn(1, f);

// 4 stages with swept frequencies
f1 = 200 + 1000 * lfo * depth;
f2 = 400 + 2000 * lfo * depth;
f3 = 600 + 3000 * lfo * depth;
f4 = 800 + 4000 * lfo * depth;

phaser = (+ : ap1(f1) : ap1(f2) : ap1(f3) : ap1(f4)) ~ *(fb);

process = _ <: _, phaser :> *(0.5);
