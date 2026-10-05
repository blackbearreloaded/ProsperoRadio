/* ProsperoRadio - The boilerplate's self-update kit: the console side.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* With filesystem access the app's folder is not /app0 and its data is not
 * /download0: the paths come from radio_update.cpp. */
const char *radio_self_update_path(int which);
#define SELF_UPDATE_HELPER_PATH radio_self_update_path(0)
#define SELF_UPDATE_PARAM_PATH radio_self_update_path(1)
#define SELF_UPDATE_SEQUENCE_PATH radio_self_update_path(2)

#include "../../third_party/update_check/self_update_ps5.c"
