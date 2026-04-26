// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// volume.dsp — gain control
// process = input * gain
gain = hslider("gain", 0.5, 0, 1, 0.01);
process = _ * gain;
