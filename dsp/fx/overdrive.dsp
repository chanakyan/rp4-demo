// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// overdrive.dsp — soft clipping distortion
// tanh waveshaper with pre-gain and tone control.

gain = hslider("gain", 4, 1, 20, 0.1);
tone = hslider("tone", 0.5, 0, 1, 0.01);

// Soft clip via tanh
clip(x) = x : min(1) : max(-1);
softclip = *(gain) : tanh;

// Simple one-pole lowpass for tone
lpf(f) = *(1 - f) : + ~ *(f);

process = _ : softclip : lpf(1 - tone);
