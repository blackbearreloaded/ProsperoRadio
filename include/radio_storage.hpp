// ProsperoRadio - Where the app keeps its files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// With filesystem access (the elevation helper, asked for first thing in main)
// everything ProsperoRadio writes lives under /data/prosperoradio:
//   catalog/   the station catalogue and its staging copy
//   config/    favourites (and settings)
//   logs/      prosperoradio.log and the previous session's
// and the app's own files are read from its install folder. Without it (no
// elfldr on the console, or the request failed) the sandbox paths stay:
// /app0 and /download0, as in every earlier version.

enum radio_file_t
{
    RADIO_FILE_CATALOG,
    RADIO_FILE_CATALOG_STAGING,
    RADIO_FILE_FAVORITES,
    RADIO_FILE_FAVORITES_TEMP,
    RADIO_FILE_LEGACY_CACHE,
    RADIO_FILE_LOG,
    RADIO_FILE_SETTINGS,
    RADIO_FILE_COUNT
};

// Asks for filesystem access, settles every path, creates the folders, brings
// over what an earlier version kept in the sandbox, and opens the log. Call it
// once, first, while the process still has a single thread.
void radio_storage_init(void);

// -1 before radio_storage_init, 0 granted, -2 not asked for (a test's
// dev/no-elevation.txt), otherwise the status that refused it.
int radio_storage_status(void);
// The folder the app's own files are in (eboot.bin, assets, sce_sys).
const char *radio_storage_app_dir(void);
// The folder the app's data is in.
const char *radio_storage_data_dir(void);
// The full path of one of the app's files.
const char *radio_storage_file(radio_file_t file);
