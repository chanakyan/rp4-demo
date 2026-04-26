// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// djembe.dsp — struck membrane physical model
import("pm.lib");

freq    = 220;
gain    = 0.9;
trigger = button("strike");

process = pm.djembe(freq, gain, trigger);
