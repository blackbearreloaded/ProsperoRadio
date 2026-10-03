// ProsperoRadio - Where the audio thread shows the visualizer what it plays.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

// Called by the output thread with each block it hands to the system:
// interleaved stereo, 48 kHz. It copies and returns; it never waits.
void radio_levels_feed(const int16_t *stereo, unsigned frames);
