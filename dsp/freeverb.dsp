// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// freeverb.dsp — Schroeder reverb (4 combs + 2 allpasses)
// Classic Freeverb topology. This is the stress test for the
// state monad: 4 parallel delay lines with feedback, each with
// its own State, composed via Par + Merge + Seq.

import("stdfaust.lib");

// Comb filter: y(n) = x(n-dt) + fb * y(n-dt)
comb(dt, fb) = + ~ (@(dt) * fb);

// Allpass filter: y(n) = -g*x(n) + x(n-dt) + g*y(n-dt)
allpass(dt, g) = _ <: (*(g) + @(dt)) , (_) :> _;

// 4 parallel combs → sum → 2 serial allpasses
// Comb lengths chosen for 48 kHz
wet = hslider("wet", 0.3, 0, 1, 0.01);
damp = hslider("damp", 0.5, 0, 1, 0.01);

reverb = _ <: comb(1557, 0.84),
              comb(1617, 0.84),
              comb(1491, 0.84),
              comb(1422, 0.84)
         :> allpass(225, 0.5)
          : allpass(556, 0.5);

process = _ <: (_ * (1 - wet)), (_ : reverb * wet) :> _;
