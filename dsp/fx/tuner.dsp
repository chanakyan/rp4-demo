// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// tuner.dsp — autocorrelation pitch detector
// Outputs estimated frequency as a DC signal.
// Use with PPS: audiomgr publishes pitch to /pps/tuner.
import("stdfaust.lib");

// Zero-crossing rate as crude pitch estimate
// (real tuner would use autocorrelation in dsp.cppm native)
zc = abs(_ - _') > 0.1;
zc_rate = zc : ba.count(int(ma.SR * 0.05)) : *(ma.SR / (ma.SR * 0.05)) : *(0.5);

process = _ <: _, zc_rate;
