// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// compressor.dsp — dynamics compressor
// Envelope follower → gain reduction above threshold.
import("stdfaust.lib");

threshold = hslider("threshold", -20, -60, 0, 0.1);
ratio     = hslider("ratio", 4, 1, 20, 0.1);
attack    = hslider("attack_ms", 10, 0.1, 100, 0.1);
release   = hslider("release_ms", 100, 10, 1000, 1);
makeup    = hslider("makeup_db", 0, 0, 40, 0.1);

// Envelope follower (peak, one-pole)
att = exp(-1 / (ma.SR * attack * 0.001));
rel = exp(-1 / (ma.SR * release * 0.001));
envelope = abs : +(1e-20) : ba.slidingMax(int(ma.SR * 0.01), int(ma.SR * 0.01));

// dB conversion
to_db(x) = 20 * log10(max(x, 1e-20));
from_db(x) = 10 ^ (x / 20);

// Gain computer
compute_gain(env_db) = min(0, (threshold - env_db) * (1 - 1/ratio));

process = _ <: _, (abs : to_db : compute_gain : from_db) : * : *(from_db(makeup));
