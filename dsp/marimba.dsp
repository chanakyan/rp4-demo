// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// marimba.dsp — struck bar physical model
import("pm.lib");

freq    = 440;
gain    = 0.8;
trigger = button("strike");

process = pm.marimba(freq, gain, trigger);
