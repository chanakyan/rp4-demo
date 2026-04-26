// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// ks_basic.dsp — raw Karplus-Strong synthesis
// The simplest physical model: delay line + lowpass feedback.
// No pluck position, no body resonance — just the algorithm.
import("pm.lib");

stringLength = pm.f2l(330);  // E4
damping      = 0.5;

process = pm.ks(stringLength, damping);
