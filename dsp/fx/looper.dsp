// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
// looper.dsp — simple loop recorder/player
// Record into delay buffer, play back with overdub.

buf_seconds = 4;
buf_len = int(ma.SR * buf_seconds);

record  = button("record");
play    = button("play");
overdub = hslider("overdub", 0.8, 0, 1, 0.01);

// Write pointer advances while recording
write_pos = +(record) ~ %(buf_len);

// Read pointer advances while playing
read_pos = +(play) ~ %(buf_len);

// The buffer: record adds to existing, play reads
process = _ * record + @(buf_len) * overdub * play;
