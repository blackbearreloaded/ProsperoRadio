// ProsperoRadio - A scrolling view of the station catalogue, a few pages at a time.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/catalog.hpp"

#include "app/platform.hpp"

#include <algorithm>
#include <cstring>

namespace radio
{

namespace
{

radio_catalog_order_t order_of(const ListSpec &spec)
{
    if (spec.view == View::trending)
        return RADIO_CATALOG_ORDER_TRENDING;
    if (spec.view == View::voted)
        return RADIO_CATALOG_ORDER_VOTED;
    if (spec.view == View::favorites)
        return RADIO_CATALOG_ORDER_NAME; // Favorites is kept A to Z
    return RADIO_CATALOG_ORDER_POPULAR;
}

bool query_service(const ListSpec &spec, unsigned offset, unsigned limit, unsigned *total)
{
    return radio_service_query_page(spec.view == View::discover ? &spec.query : nullptr,
                                    order_of(spec), spec.view == View::favorites, offset, limit,
                                    total);
}

} // namespace

bool Catalog::letter_starts(unsigned *starts) const
{
    return radio_catalog_letter_starts(spec_.view == View::discover ? &spec_.query : nullptr,
                                       order_of(spec_), spec_.view == View::favorites, starts);
}

bool same_spec(const ListSpec &a, const ListSpec &b)
{
    return a.view == b.view && std::strcmp(a.query.name, b.query.name) == 0 &&
           std::strcmp(a.query.country_code, b.query.country_code) == 0 &&
           std::strcmp(a.query.tag, b.query.tag) == 0 &&
           std::strcmp(a.query.language, b.query.language) == 0 &&
           a.query.bitrate_min == b.query.bitrate_min;
}

bool has_filters(const radio_catalog_query_t &query)
{
    return query.name[0] != '\0' || query.country_code[0] != '\0' || query.tag[0] != '\0' ||
           query.language[0] != '\0' || query.bitrate_min != 0U;
}

Catalog::Catalog()
{
    pages_.reserve(kPages);
}

void Catalog::set_spec(const ListSpec &spec)
{
    spec_ = spec;
    invalidate();
}

void Catalog::invalidate()
{
    pages_.clear();
    total_ = 0;
    known_ = false;
}

const Catalog::Page *Catalog::find(unsigned number) const
{
    for (const Page &page : pages_)
    {
        if (page.number == number)
            return &page;
    }
    return nullptr;
}

Catalog::Page *Catalog::load(unsigned number)
{
    unsigned total = 0;
    if (!query_service(spec_, number * kPageSize, kPageSize, &total))
        return nullptr;
    total_ = total;
    known_ = true;

    Page *page = const_cast<Page *>(find(number));
    if (page == nullptr)
    {
        if (pages_.size() < kPages)
        {
            pages_.emplace_back();
            page = &pages_.back();
        }
        else
        {
            // The page that was looked at longest ago makes room.
            page = &*std::min_element(pages_.begin(), pages_.end(),
                                      [](const Page &a, const Page &b) { return a.used < b.used; });
        }
    }
    radio_service_status_t status{};
    radio_service_get_status(&status);
    page->number = number;
    page->used = ++stamp_;
    page->count = 0;
    for (unsigned i = 0; i < std::min(status.station_count, kPageSize); ++i)
    {
        if (!radio_service_get_station(i, &page->stations[page->count]))
            break;
        page->favorite[page->count] = radio_service_is_favorite(page->stations[page->count].uuid);
        ++page->count;
    }
    return page;
}

const radio_station_t *Catalog::peek(unsigned index) const
{
    const Page *page = find(index / kPageSize);
    const unsigned slot = index % kPageSize;
    if (page == nullptr || slot >= page->count)
        return nullptr;
    page->used = ++stamp_;
    return &page->stations[slot];
}

bool Catalog::favorite(unsigned index) const
{
    const Page *page = find(index / kPageSize);
    const unsigned slot = index % kPageSize;
    return page != nullptr && slot < page->count && page->favorite[slot];
}

void Catalog::prefetch(unsigned first, unsigned last)
{
    if (!known_)
    {
        load(first / kPageSize);
        return;
    }
    if (total_ == 0)
        return;
    last = std::min(last, total_ - 1);
    for (unsigned number = first / kPageSize; number <= last / kPageSize; ++number)
    {
        if (find(number) == nullptr)
        {
            load(number);
            return;
        }
    }
}

const radio_station_t *Catalog::fetch(unsigned index)
{
    if (find(index / kPageSize) == nullptr)
        load(index / kPageSize);
    return peek(index);
}

int Catalog::select(unsigned index)
{
    const Page *page = load(index / kPageSize);
    const unsigned slot = index % kPageSize;
    return page != nullptr && slot < page->count ? static_cast<int>(slot) : -1;
}

void Catalog::refresh_favorites()
{
    for (Page &page : pages_)
    {
        for (unsigned i = 0; i < page.count; ++i)
            page.favorite[i] = radio_service_is_favorite(page.stations[i].uuid);
    }
}

} // namespace radio
