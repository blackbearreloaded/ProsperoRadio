// ProsperoRadio - A scrolling view of the station catalogue, a few pages at a time.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "radio_service.hpp"

#include <cstdint>
#include <vector>

namespace radio
{

enum class View : std::uint8_t
{
    popular,
    trending,
    voted,
    favorites,
    discover,
    count,
};

// Which stations a list shows: a view, and for Discover the filters.
struct ListSpec
{
    View view = View::popular;
    radio_catalog_query_t query{};
};
bool same_spec(const ListSpec &a, const ListSpec &b);
// True when the query narrows anything (Discover with nothing set shows all).
bool has_filters(const radio_catalog_query_t &query);

// The service hands out at most sixteen stations per query and keeps no more
// in memory. A list on screen needs the rows in view and a few beyond, so
// this keeps the last few pages it asked for and answers by absolute index.
// Nothing here grows with the catalogue.
class Catalog
{
  public:
    static constexpr unsigned kPageSize = 16; // the service's own window
    static constexpr unsigned kPages = 8;

    Catalog();

    // A different list: forgets every page.
    void set_spec(const ListSpec &spec);
    const ListSpec &spec() const
    {
        return spec_;
    }
    // The same list changed underneath (a sync, a favourite): pages are asked
    // for again as they are needed.
    void invalidate();

    // False until the first page answered.
    bool known() const
    {
        return known_;
    }
    unsigned total() const
    {
        return total_;
    }

    // The station at an index if its page is in memory, else null.
    const radio_station_t *peek(unsigned index) const;
    bool favorite(unsigned index) const;
    // Brings the pages of [first, last] in, one query per call at most, so a
    // fast scroll never stalls a frame on several.
    void prefetch(unsigned first, unsigned last);
    // The station at an index, read now if need be.
    const radio_station_t *fetch(unsigned index);

    // Makes the service's own window the page that holds `index` (play and
    // favourite act on that window) and returns the station's slot in it, or
    // -1 when the list no longer reaches that far.
    int select(unsigned index);
    // Reads the favourite marks of the pages in memory again.
    void refresh_favorites();
    // Where the first station under each mark of the letter rail is in this
    // list (UINT_MAX: none). False when the service could not say.
    bool letter_starts(unsigned *starts) const;

  private:
    struct Page
    {
        unsigned number = 0;
        mutable unsigned used = 0; // when it was last looked at
        unsigned count = 0;
        radio_station_t stations[kPageSize];
        bool favorite[kPageSize];
    };

    const Page *find(unsigned number) const;
    Page *load(unsigned number);

    ListSpec spec_;
    std::vector<Page> pages_;
    unsigned total_ = 0;
    bool known_ = false;
    mutable unsigned stamp_ = 0;
};

} // namespace radio
