// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// guitar_amp.dsp — full signal chain: guitar → overdrive → eq → reverb
// The complete pedalboard as one .dsp. Exercises Seq composition deeply.
import("pm.lib");

// ─── Guitar ─────────────────────────────────────────────────────────────────

stringLength = pm.f2l(196);  // G3
trigger      = button("pluck");
guitar       = pm.elecGuitar(stringLength, 0.3, 0.9, trigger);

// ─── Overdrive ──────────────────────────────────────────────────────────────

drive = hslider("drive", 4, 1, 20, 0.1);
overdrive = *(drive) : tanh;

// ─── Tone stack (simple one-pole) ───────────────────────────────────────────

tone = hslider("tone", 0.5, 0, 1, 0.01);
lpf(f) = *(1 - f) : + ~ *(f);
tonestack = lpf(1 - tone);

// ─── Reverb ─────────────────────────────────────────────────────────────────

comb(dt, fb)   = + ~ (@(dt) * fb);
allpass(dt, g) = _ <: (*(g) + @(dt)), (_) :> _;
wet = hslider("reverb", 0.2, 0, 0.6, 0.01);

reverb = _ <: comb(1557, 0.82),
              comb(1617, 0.82),
              comb(1491, 0.82),
              comb(1422, 0.82)
         :> allpass(225, 0.5)
          : allpass(556, 0.5);

cab = _ <: (_ * (1 - wet)), (_ : reverb * wet) :> _;

// ─── Chain ──────────────────────────────────────────────────────────────────

process = guitar : overdrive : tonestack : cab;
