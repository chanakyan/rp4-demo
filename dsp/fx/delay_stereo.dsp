// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// delay_stereo.dsp — stereo ping-pong delay
// Left and right channels alternate with cross-feedback.

dt_ms = hslider("delay_ms", 375, 50, 2000, 1);
fb    = hslider("feedback", 0.4, 0, 0.9, 0.01);
mix   = hslider("mix", 0.3, 0, 1, 0.01);

dt = int(ma.SR * dt_ms * 0.001);

// Ping-pong: L feeds R delay, R feeds L delay
ping = + ~ (@(dt * 2) * fb);
pong = + ~ (@(dt * 2) * fb);

process = _ <: (_ * (1 - mix) , _ * (1 - mix)),
               (_ : ping * mix, _ : pong * mix)
          :> _, _;
