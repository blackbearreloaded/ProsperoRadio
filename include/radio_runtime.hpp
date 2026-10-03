// ProsperoRadio - Process setup the radio service depends on.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Gives SDL its own allocator (large blocks come straight from the system, so
// the catalogue's 16 MiB buffers never touch the process heap) and starts SDL
// without video: the service uses its threads, locks and clock, the display
// belongs to OpenGL. Call it once, before anything else.
bool radio_runtime_init();
