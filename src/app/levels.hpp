// ProsperoRadio - The level of what is playing, for the visualizer.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Fills `bands` with the level of the audio being played right now, lowest
// frequencies first, each 0..1. Returns false and zeroes them when there is
// nothing to measure. Called once a frame from the interface thread, so it
// must not block on the decoder.
bool radio_levels(float *bands, unsigned count);
