// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// tone.dsp — sine wave generator
// 440 Hz test tone at -12 dB
import("stdfaust.lib");
freq = 440;
process = os.osc(freq) * 0.25;
