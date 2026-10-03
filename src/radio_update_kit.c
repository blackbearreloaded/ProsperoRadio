/* ProsperoRadio - The boilerplate's update-check kit, without its own networking.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The app already carries libcurl with its own console shims (radio_http), so
 * only the kit's parsing and decisions are built; radio_update.cpp fetches. */

#define UPDATE_CHECK_NO_NETWORK 1
#include "../third_party/update_check/update_check.c"
