// ProsperoRadio - Where the audio thread applies the listener's sound settings.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

// Called by the output thread with each block before it goes to the system:
// interleaved stereo, 48 kHz, changed in place. It never waits.
void radio_audio_process(int16_t *stereo, unsigned frames);
