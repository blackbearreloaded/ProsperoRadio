// ProsperoRadio - The letter rail's counts, from the radio service.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/platform.hpp"

// radio_service.cpp (added by the build): the counts, on the service's own
// connection to the catalogue and under its lock.
bool radio_service_initials(bool favorites_only, unsigned *counts, unsigned buckets);

bool radio_catalog_initials(bool favorites_only, unsigned counts[kInitialBuckets])
{
    return radio_service_initials(favorites_only, counts, kInitialBuckets);
}

bool radio_service_letter_starts(const radio_catalog_query_t *query, radio_catalog_order_t order,
                                 bool favorites_only, unsigned *starts, unsigned buckets);

bool radio_catalog_letter_starts(const radio_catalog_query_t *query, radio_catalog_order_t order,
                                 bool favorites_only, unsigned starts[kInitialBuckets])
{
    return radio_service_letter_starts(query, order, favorites_only, starts, kInitialBuckets);
}
