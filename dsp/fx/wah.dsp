// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// wah.dsp — auto-wah (envelope-controlled bandpass)
// Bandpass center frequency follows input envelope.
import("stdfaust.lib");

freq_lo  = 300;
freq_hi  = 2500;
q        = 5;
rate     = hslider("rate", 2, 0.1, 10, 0.01);

// LFO sweeps bandpass center
sweep = freq_lo + (freq_hi - freq_lo) * (1 + os.osc(rate)) * 0.5;

// Second-order resonant bandpass
wah = fi.resonbp(sweep, q, 1);

process = _ : wah;
