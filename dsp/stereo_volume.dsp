// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// stereo_volume.dsp — stereo gain with balance
gain = hslider("gain", 0.5, 0, 1, 0.01);
bal  = hslider("balance", 0.5, 0, 1, 0.01);
process = _ * gain * (1 - bal), _ * gain * bal;
