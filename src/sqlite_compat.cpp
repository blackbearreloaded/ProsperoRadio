// PSRadio - SQLite compatibility boundary.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/types.h>
#include <time.h>

int fchown(int descriptor, uid_t owner, gid_t group)
{
    (void)descriptor;
    (void)owner;
    (void)group;
    return 0;
}

struct tm *localtime_r(const time_t *timer, struct tm *result)
{
    (void)timer;
    (void)result;
    return nullptr;
}
