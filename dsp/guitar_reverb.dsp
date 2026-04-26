// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// guitar_reverb.dsp — nylon guitar into freeverb
// Composition demo: physical model : reverb via Seq combinator.
import("pm.lib");

// Guitar
stringLength = pm.f2l(330);  // E4
trigger      = button("pluck");
guitar       = pm.nylonGuitar(stringLength, 0.5, 0.8, trigger);

// Reverb (inline, no separate import needed)
comb(dt, fb)    = + ~ (@(dt) * fb);
allpass(dt, g)  = _ <: (*(g) + @(dt)), (_) :> _;
wet = 0.3;

reverb = _ <: comb(1557, 0.84),
              comb(1617, 0.84),
              comb(1491, 0.84),
              comb(1422, 0.84)
         :> allpass(225, 0.5)
          : allpass(556, 0.5);

process = guitar : (_ <: (_ * (1 - wet)), (_ : reverb * wet) :> _);
