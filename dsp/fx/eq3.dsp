// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// eq3.dsp — three-band parametric EQ
// Low shelf + mid peak + high shelf.
import("stdfaust.lib");

lo_gain = hslider("low_db", 0, -12, 12, 0.1);
lo_freq = 200;

mid_gain = hslider("mid_db", 0, -12, 12, 0.1);
mid_freq = hslider("mid_freq", 1000, 200, 5000, 1);
mid_q    = hslider("mid_q", 1, 0.3, 10, 0.1);

hi_gain = hslider("high_db", 0, -12, 12, 0.1);
hi_freq = 4000;

process = _ : fi.low_shelf(lo_gain, lo_freq)
            : fi.peak_eq(mid_gain, mid_freq, mid_freq / mid_q)
            : fi.high_shelf(hi_gain, hi_freq);
