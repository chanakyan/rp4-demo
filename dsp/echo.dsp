// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// echo.dsp — simple delay + feedback
// dt = delay time in samples, fb = feedback coefficient
dt = 24000;  // 0.5 seconds at 48 kHz
fb = 0.4;
process = + ~ (@(dt) * fb);
