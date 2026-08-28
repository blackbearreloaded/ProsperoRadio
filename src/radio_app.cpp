// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "radio_app.hpp"

#include "radio_ime.hpp"
#include "radio_text.hpp"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace
{

constexpr unsigned kFocusDiscover = 4;
constexpr unsigned kFocusPlay = 7;
constexpr unsigned kFocusCredits = 8;

Rml::Element *Find(Rml::ElementDocument *document, const char *id)
{
    return document ? document->GetElementById(id) : nullptr;
}

void SetText(Rml::ElementDocument *document, const char *id, const char *text)
{
    if (Rml::Element *element = Find(document, id))
    {
        const std::string visual = RadioVisualText(text);
        const Rml::String encoded = Rml::StringUtilities::EncodeRml(visual);
        if (element->GetInnerRML() != encoded)
            element->SetInnerRML(encoded);
    }
}

void SetMultilineText(Rml::ElementDocument *document, const char *id, const char *text)
{
    const std::string visual = RadioVisualText(text);
    Rml::String encoded = Rml::StringUtilities::EncodeRml(visual);
    for (std::size_t at = 0; (at = encoded.find('\n', at)) != Rml::String::npos;)
    {
        encoded.replace(at, 1, "<br/>");
        at += 5;
    }
    if (Rml::Element *element = Find(document, id))
    {
        if (element->GetInnerRML() != encoded)
            element->SetInnerRML(encoded);
    }
}

void SetClass(Rml::ElementDocument *document, const char *id, const char *class_name, bool enabled)
{
    if (Rml::Element *element = Find(document, id))
        element->SetClass(class_name, enabled);
}

void SetVisible(Rml::ElementDocument *document, const char *id, bool visible)
{
    SetClass(document, id, "hidden", !visible);
}

void SetProperty(Rml::ElementDocument *document, const char *id, const char *name,
                 const char *value)
{
    if (Rml::Element *element = Find(document, id))
        element->SetProperty(name, value);
}

void SetPixelProperty(Rml::ElementDocument *document, const char *id, const char *name, int value)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%dpx", value);
    SetProperty(document, id, name, text);
}

void FirstValue(const char *values, char *output, std::size_t capacity)
{
    std::size_t count = 0;
    while (values[count] && values[count] != ',' && count + 1 < capacity)
    {
        output[count] = values[count];
        ++count;
    }
    while (count && output[count - 1] == ' ')
        --count;
    output[count] = '\0';
}

bool ContainsCi(const char *text, const char *needle)
{
    if (!*needle)
        return true;
    for (; *text; ++text)
    {
        const char *left = text;
        const char *right = needle;
        while (*left && *right &&
               std::tolower(static_cast<unsigned char>(*left)) ==
                   std::tolower(static_cast<unsigned char>(*right)))
        {
            ++left;
            ++right;
        }
        if (!*right)
            return true;
    }
    return false;
}

bool PlaybackActive(radio_playback_state_t state)
{
    return state == RADIO_PLAYBACK_CONNECTING || state == RADIO_PLAYBACK_BUFFERING ||
           state == RADIO_PLAYBACK_PLAYING || state == RADIO_PLAYBACK_STOPPING;
}

const char *StationColor(const char *uuid)
{
    static const char *palette[] = {"#2c6d87", "#b25535", "#3d7e68", "#6a4b91",
                                    "#8a6539", "#35628a", "#864d65", "#497556"};
    unsigned hash = 0;
    for (; *uuid; ++uuid)
        hash = hash * 33U + static_cast<unsigned char>(*uuid);
    return palette[hash % (sizeof(palette) / sizeof(palette[0]))];
}

void CopyString(char *destination, std::size_t capacity, const char *source)
{
    if (!capacity)
        return;
    std::size_t count = 0;
    while (source && source[count] && count + 1 < capacity)
    {
        destination[count] = source[count];
        ++count;
    }
    destination[count] = '\0';
}

const radio_facet_t *FindFacet(const std::vector<radio_facet_t> &facets, const char *value)
{
    for (const radio_facet_t &facet : facets)
        if (std::strcmp(facet.value, value) == 0)
            return &facet;
    return nullptr;
}

} // namespace

bool RadioApp::Initialize(Rml::ElementDocument *document)
{
    document_ = document;
    if (!document_)
        return false;
    for (unsigned &station : card_stations_)
        station = InvalidStation;
    radio_service_init();
    service_started_ = true;
    RebuildFacets();
    RefreshAll();
    UpdateFocus();
    return true;
}

void RadioApp::Shutdown()
{
    if (service_started_)
    {
        radio_service_shutdown();
        service_started_ = false;
    }
    document_ = nullptr;
}

void RadioApp::RebuildFacets()
{
    country_facets_.clear();
    genre_facets_.clear();
    language_facets_.clear();

    auto load = [](radio_facet_kind_t kind, std::vector<radio_facet_t> &facets)
    {
        const unsigned count = radio_service_get_facet_count(kind);
        facets.reserve(count);
        for (unsigned i = 0; i < count; ++i)
        {
            radio_facet_t facet{};
            if (radio_service_get_facet(kind, i, &facet))
                facets.push_back(facet);
        }
    };
    load(RADIO_FACET_COUNTRY, country_facets_);
    load(RADIO_FACET_GENRE, genre_facets_);
    load(RADIO_FACET_LANGUAGE, language_facets_);

    const bool build_countries = country_facets_.empty();
    const bool build_genres = genre_facets_.empty();
    const bool build_languages = language_facets_.empty();
    if (build_countries || build_genres || build_languages)
    {
        auto add = [](std::vector<radio_facet_t> &facets, const char *value, const char *label)
        {
            if (!*value)
                return;
            for (radio_facet_t &facet : facets)
            {
                if (std::strcmp(facet.value, value) == 0)
                {
                    ++facet.station_count;
                    return;
                }
            }
            if (facets.size() >= RADIO_MAX_FACETS)
                return;
            radio_facet_t facet{};
            CopyString(facet.value, sizeof(facet.value), value);
            CopyString(facet.label, sizeof(facet.label), label);
            facet.station_count = 1;
            facets.push_back(facet);
        };
        radio_service_status_t status{};
        radio_service_get_status(&status);
        for (unsigned i = 0; i < status.station_count; ++i)
        {
            radio_station_t station{};
            if (!radio_service_get_station(i, &station))
                continue;
            if (build_countries)
                add(country_facets_, station.country_code,
                    *station.country ? station.country : station.country_code);
            char value[64];
            FirstValue(station.tags, value, sizeof(value));
            if (build_genres)
                add(genre_facets_, value, value);
            FirstValue(station.language, value, sizeof(value));
            if (build_languages)
                add(language_facets_, value, value);
        }
        auto sort = [](std::vector<radio_facet_t> &facets)
        {
            std::sort(facets.begin(), facets.end(),
                      [](const radio_facet_t &left, const radio_facet_t &right)
                      { return left.station_count > right.station_count; });
        };
        sort(country_facets_);
        sort(genre_facets_);
        sort(language_facets_);
    }

    auto retained = [](const std::vector<radio_facet_t> &facets, const char *selected)
    {
        if (!*selected)
            return true;
        for (const radio_facet_t &facet : facets)
            if (std::strcmp(facet.value, selected) == 0)
                return true;
        return false;
    };
    if (!retained(country_facets_, filter_country_))
        filter_country_[0] = '\0';
    if (!retained(genre_facets_, filter_genre_))
        filter_genre_[0] = '\0';
    if (!retained(language_facets_, filter_language_))
        filter_language_[0] = '\0';
}

bool RadioApp::StationVisible(const radio_station_t &station) const
{
    if (view_ == View::Popular || view_ == View::Trending || view_ == View::Voted)
        return true;
    if (view_ == View::Favorites)
        return radio_service_is_favorite(station.uuid);

    if (*filter_country_ && std::strcmp(station.country_code, filter_country_) != 0)
        return false;
    if (*filter_genre_ && !ContainsCi(station.tags, filter_genre_))
        return false;
    if (*filter_language_ && !ContainsCi(station.language, filter_language_))
        return false;
    if (filter_bitrate_ && station.bitrate < filter_bitrate_)
        return false;
    if (!*search_query_)
        return true;
    return ContainsCi(station.name, search_query_) || ContainsCi(station.tags, search_query_) ||
           ContainsCi(station.country, search_query_) || ContainsCi(station.state, search_query_) ||
           ContainsCi(station.language, search_query_);
}

void RadioApp::BuildVisibleList()
{
    radio_catalog_query_t query{};
    const radio_catalog_query_t *query_ptr = nullptr;
    if (view_ == View::Discover)
    {
        CopyString(query.name, sizeof(query.name), search_query_);
        CopyString(query.country_code, sizeof(query.country_code), filter_country_);
        CopyString(query.tag, sizeof(query.tag), filter_genre_);
        CopyString(query.language, sizeof(query.language), filter_language_);
        query.bitrate_min = filter_bitrate_;
        query_ptr = &query;
    }
    radio_catalog_order_t order = RADIO_CATALOG_ORDER_POPULAR;
    if (view_ == View::Trending)
        order = RADIO_CATALOG_ORDER_TRENDING;
    else if (view_ == View::Voted)
        order = RADIO_CATALOG_ORDER_VOTED;
    const bool favorites_only = view_ == View::Favorites;
    unsigned total = 0;
    bool loaded =
        radio_service_query_page(query_ptr, order, favorites_only, page_start_, CardCount, &total);
    if (loaded && total && page_start_ >= total)
    {
        page_start_ = ((total - 1U) / CardCount) * CardCount;
        loaded = radio_service_query_page(query_ptr, order, favorites_only, page_start_, CardCount,
                                          &total);
    }
    visible_indices_.clear();
    if (loaded)
    {
        radio_service_status_t status{};
        radio_service_get_status(&status);
        visible_indices_.reserve(status.station_count);
        for (unsigned i = 0; i < status.station_count; ++i)
            visible_indices_.push_back(i);
        visible_count_ = total;
    }
    else
        visible_count_ = 0;
    if (selected_slot_ >= CardCount)
        selected_slot_ = 0;
}

void RadioApp::RefreshAll()
{
    BuildVisibleList();
    RefreshCards();
    RefreshTabs();
    RefreshHeading();
    RefreshDiscover();
    RefreshDetail();
    radio_service_status_t status{};
    radio_service_get_status(&status);
    RefreshPlayback(status);
    RefreshConnection(status);
    UpdateSearch();
}

void RadioApp::RefreshTabs()
{
    for (unsigned i = 0; i < static_cast<unsigned>(View::Count); ++i)
    {
        char id[16];
        std::snprintf(id, sizeof(id), "tab-%u", i);
        SetClass(document_, id, "active", i == static_cast<unsigned>(view_));
    }
}

void RadioApp::RefreshHeading()
{
    static const char *titles[] = {"Popular stations", "Trending now", "Top rated",
                                   "Your favorites", "Discover"};
    static const char *subtitles[] = {
        "Most played on Radio Browser in the last 24 hours",
        "Stations gaining the most listeners across the last two days",
        "Community favorites ranked by cumulative Radio Browser votes",
        "Saved locally on this console",
        "Choose Country, Genre, or Language below - Triangle opens search"};
    const unsigned view = static_cast<unsigned>(view_);
    SetText(document_, "heading", titles[view]);
    SetText(document_, "subtitle", subtitles[view]);

    const unsigned page_count = visible_count_ ? (visible_count_ + CardCount - 1) / CardCount : 0;
    const unsigned page_number = visible_count_ ? page_start_ / CardCount + 1 : 0;
    char text[112];
    if (!visible_count_)
        CopyString(text, sizeof(text), "No matching stations");
    else
    {
        const unsigned last =
            page_start_ + CardCount < visible_count_ ? page_start_ + CardCount : visible_count_;
        std::snprintf(text, sizeof(text), "Page %u of %u   /   %u-%u of %u stations", page_number,
                      page_count, page_start_ + 1, last, visible_count_);
    }
    SetText(document_, "page-label", text);
    const bool has_previous_page = page_start_ >= CardCount;
    const bool has_next_page = page_start_ + CardCount < visible_count_;
    SetText(document_, "page-prev-label", has_previous_page ? "Previous page" : "");
    SetText(document_, "page-next-label", has_next_page ? "Next page" : "");
    SetClass(document_, "page-prev", "available", has_previous_page);
    SetClass(document_, "page-next", "available", has_next_page);

    constexpr int track_width = 420;
    int thumb_width = page_count > 1 ? track_width / static_cast<int>(page_count) : track_width;
    if (thumb_width < 34)
        thumb_width = 34;
    const int thumb_left = page_count > 1
                               ? static_cast<int>(page_number - 1) * (track_width - thumb_width) /
                                     static_cast<int>(page_count - 1)
                               : 0;
    SetPixelProperty(document_, "page-thumb", "width", thumb_width);
    SetPixelProperty(document_, "page-thumb", "left", thumb_left);
}

void RadioApp::RefreshDiscover()
{
    SetVisible(document_, "discover-panel", view_ == View::Discover);
    const radio_facet_t *country = FindFacet(country_facets_, filter_country_);
    const radio_facet_t *genre = FindFacet(genre_facets_, filter_genre_);
    const radio_facet_t *language = FindFacet(language_facets_, filter_language_);
    const char *values[] = {country ? country->label : "All countries",
                            genre ? genre->label : "All genres",
                            language ? language->label : "All languages"};
    const char *names[] = {"COUNTRY", "GENRE", "LANGUAGE"};
    const unsigned choices[] = {static_cast<unsigned>(country_facets_.size()),
                                static_cast<unsigned>(genre_facets_.size()),
                                static_cast<unsigned>(language_facets_.size())};
    const unsigned matches[] = {country ? country->station_count : choices[0],
                                genre ? genre->station_count : choices[1],
                                language ? language->station_count : choices[2]};
    for (unsigned i = 0; i < 3; ++i)
    {
        const bool selected = (i == 0 && *filter_country_) || (i == 1 && *filter_genre_) ||
                              (i == 2 && *filter_language_);
        char text[120];
        std::snprintf(text, sizeof(text), "%s  /  %s  (%u %s)", names[i], values[i],
                      selected ? matches[i] : choices[i], selected ? "stations" : "choices");
        char id[32];
        std::snprintf(id, sizeof(id), "discover-label-%u", i);
        SetText(document_, id, text);
    }
}

void RadioApp::RefreshCard(unsigned slot)
{
    char id[32];
    std::snprintf(id, sizeof(id), "card-%u", slot);
    if (page_start_ + slot >= visible_count_ || slot >= visible_indices_.size())
    {
        card_stations_[slot] = InvalidStation;
        SetVisible(document_, id, false);
        return;
    }

    const unsigned index = visible_indices_[slot];
    radio_station_t station{};
    if (!radio_service_get_station(index, &station))
    {
        card_stations_[slot] = InvalidStation;
        SetVisible(document_, id, false);
        return;
    }
    card_stations_[slot] = index;
    SetVisible(document_, id, true);

    std::snprintf(id, sizeof(id), "art-%u", slot);
    SetProperty(document_, id, "background-color", StationColor(station.uuid));
    std::snprintf(id, sizeof(id), "badge-%u", slot);
    SetText(document_, id, *station.country_code ? station.country_code : "AAC");
    std::snprintf(id, sizeof(id), "name-%u", slot);
    SetText(document_, id, station.name);

    char tag[40];
    FirstValue(station.tags, tag, sizeof(tag));
    if (!*tag)
        CopyString(tag, sizeof(tag), "Music");
    char text[192];
    std::snprintf(text, sizeof(text), "%s  |  %s  |  %s %u kbps",
                  *station.country_code ? station.country_code : "World", tag, station.codec,
                  station.bitrate);
    std::snprintf(id, sizeof(id), "meta-%u", slot);
    SetText(document_, id, text);

    if (view_ == View::Popular)
        std::snprintf(text, sizeof(text), "#%u  %u plays", page_start_ + slot + 1U,
                      station.click_count);
    else if (view_ == View::Trending)
        std::snprintf(text, sizeof(text), "#%u  %+d", page_start_ + slot + 1U, station.click_trend);
    else if (view_ == View::Voted)
        std::snprintf(text, sizeof(text), "#%u  %u votes", page_start_ + slot + 1U, station.votes);
    else
        text[0] = '\0';
    std::snprintf(id, sizeof(id), "rank-%u", slot);
    SetText(document_, id, text);

    std::snprintf(id, sizeof(id), "favorite-%u", slot);
    SetClass(document_, id, "saved", radio_service_is_favorite(station.uuid));
    radio_service_status_t status{};
    radio_service_get_status(&status);
    std::snprintf(id, sizeof(id), "live-%u", slot);
    SetVisible(document_, id,
               PlaybackActive(status.playback_state) && radio_service_station_is_playing(index));
}

void RadioApp::RefreshCards()
{
    for (unsigned i = 0; i < CardCount; ++i)
        RefreshCard(i);
    if (card_stations_[selected_slot_] == InvalidStation)
        selected_slot_ = 0;
}

void RadioApp::RefreshDetail()
{
    if (card_stations_[selected_slot_] == InvalidStation)
    {
        SetText(document_, "detail-badge", "...");
        SetText(document_, "detail-name", visible_count_ ? "Loading" : "No stations found");
        SetText(document_, "detail-meta", "Open Discover to change filters");
        SetText(document_, "detail-codec", "Radio Browser catalog");
        SetText(document_, "detail-metric", "");
    }
    else
    {
        radio_station_t station{};
        if (radio_service_get_station(card_stations_[selected_slot_], &station))
        {
            SetProperty(document_, "detail-art", "background-color", StationColor(station.uuid));
            SetText(document_, "detail-badge",
                    *station.country_code ? station.country_code : "AAC");
            SetText(document_, "detail-name", station.name);
            char line[192];
            if (*station.state)
            {
                std::snprintf(line, sizeof(line), "%s / %s  |  %s", station.country, station.state,
                              *station.language ? station.language : "Unknown language");
            }
            else
            {
                std::snprintf(line, sizeof(line), "%s  |  %s",
                              *station.country ? station.country : "Worldwide",
                              *station.language ? station.language : "Unknown language");
            }
            SetText(document_, "detail-meta", line);
            char tag[64];
            FirstValue(station.tags, tag, sizeof(tag));
            std::snprintf(line, sizeof(line), "%s  |  %s %u kbps", *tag ? tag : "Music",
                          station.codec, station.bitrate);
            SetText(document_, "detail-codec", line);
            std::snprintf(line, sizeof(line), "%u daily plays  |  %u votes", station.click_count,
                          station.votes);
            SetText(document_, "detail-metric", line);
        }
    }
    for (unsigned i = 0; i < CardCount; ++i)
    {
        char id[20];
        std::snprintf(id, sizeof(id), "card-%u", i);
        SetClass(document_, id, "selected", i == selected_slot_);
    }
}

void RadioApp::RefreshPlayback(const radio_service_status_t &status)
{
    const char *text = "Ready to play";
    bool warning = false;
    bool error = false;
    char state_text[128];
    switch (status.playback_state)
    {
    case RADIO_PLAYBACK_CONNECTING:
        text = "Connecting to station";
        warning = true;
        break;
    case RADIO_PLAYBACK_BUFFERING:
        if (status.sample_rate != 0U)
        {
            std::snprintf(state_text, sizeof(state_text), "Buffering  |  %u Hz  |  %u ch",
                          status.sample_rate, status.channels);
            text = state_text;
        }
        else
        {
            text = "Buffering native audio";
        }
        warning = true;
        break;
    case RADIO_PLAYBACK_PLAYING:
        std::snprintf(state_text, sizeof(state_text), "Playing  |  %u Hz  |  %u ch",
                      status.sample_rate, status.channels);
        text = state_text;
        break;
    case RADIO_PLAYBACK_STOPPING:
        text = "Stopping stream";
        warning = true;
        break;
    case RADIO_PLAYBACK_ERROR:
        std::snprintf(state_text, sizeof(state_text), "Playback failed  |  %08x",
                      static_cast<unsigned>(status.error_code));
        text = state_text;
        error = true;
        break;
    default:
        break;
    }
    SetText(document_, "detail-status", text);
    SetClass(document_, "detail-status", "warning", warning);
    SetClass(document_, "detail-status", "error", error);
    SetClass(document_, "detail-status-dot", "warning", warning);
    SetClass(document_, "detail-status-dot", "error", error);

    const bool selected_playing = PlaybackActive(status.playback_state) &&
                                  card_stations_[selected_slot_] != InvalidStation &&
                                  radio_service_station_is_playing(card_stations_[selected_slot_]);
    SetClass(document_, "play-icon", "stop", selected_playing);
    SetClass(document_, "play-button", "playing", selected_playing);
    SetText(document_, "play-label",
            selected_playing                        ? "Stop station"
            : PlaybackActive(status.playback_state) ? "Switch station"
                                                    : "Play station");

    if (PlaybackActive(status.playback_state))
    {
        radio_station_t station{};
        if (radio_service_get_playing_station(&station))
        {
            SetProperty(document_, "now-art", "background-color", StationColor(station.uuid));
            SetText(document_, "now-badge", *station.country_code ? station.country_code : "AAC");
            SetText(document_, "now-name", station.name);
            char tag[40];
            FirstValue(station.tags, tag, sizeof(tag));
            if (!*tag)
                CopyString(tag, sizeof(tag), "Music");
            char meta[192];
            std::snprintf(meta, sizeof(meta), "%s  |  %s  |  %s %u kbps",
                          *station.country_code ? station.country_code : "World", tag,
                          station.codec, station.bitrate);
            SetText(document_, "now-meta", meta);
            SetText(document_, "now-state", text);
            SetClass(document_, "now-state", "warning", warning);
            SetClass(document_, "now-state", "error", error);
        }
    }
    else
    {
        SetText(document_, "now-badge", "--");
        SetText(document_, "now-name", "Nothing playing");
        SetText(document_, "now-meta", "Choose a station and press Cross");
        SetText(document_, "now-state", "Audio decoders ready");
        SetClass(document_, "now-state", "warning", false);
        SetClass(document_, "now-state", "error", false);
    }
    SetClass(document_, "now-art", "playing", status.playback_state == RADIO_PLAYBACK_PLAYING);
    for (unsigned i = 0; i < CardCount; ++i)
        RefreshCard(i);
}

void RadioApp::RefreshConnection(const radio_service_status_t &status)
{
    const char *text = "Database ready";
    char progress[72];
    bool warning = false;
    bool error = false;
    if (status.refreshing)
    {
        if (status.sync_station_count)
            std::snprintf(progress, sizeof(progress), "%s - %u found",
                          status.searching ? "Searching stations" : "Updating database",
                          status.sync_station_count);
        else
            std::snprintf(progress, sizeof(progress), "%s",
                          status.searching ? "Searching stations" : "Updating database");
        text = progress;
        warning = true;
    }
    else if (status.catalog_state == RADIO_CATALOG_LOADING)
    {
        text = "Loading database";
        warning = true;
    }
    else if (status.catalog_state == RADIO_CATALOG_CACHED)
    {
        text = "Local cache ready";
        warning = true;
    }
    else if (status.catalog_state == RADIO_CATALOG_ERROR)
    {
        std::snprintf(progress, sizeof(progress), "%s | %d",
                      status.catalog_size ? "Offline cache" : "Database unavailable",
                      status.error_code);
        text = progress;
        error = true;
    }
    SetText(document_, "connection-label", text);
    SetClass(document_, "connection-label", "warning", warning);
    SetClass(document_, "connection-label", "error", error);
    SetClass(document_, "connection-dot", "warning", warning);
    SetClass(document_, "connection-dot", "error", error);
}

void RadioApp::UpdateEqualizer(const radio_service_status_t &status)
{
    static const unsigned char wave[16] = {18, 28, 42, 61, 44, 30, 52, 67,
                                           48, 24, 38, 58, 72, 49, 32, 22};
    const bool playing = status.playback_state == RADIO_PLAYBACK_PLAYING;
    const unsigned phase = SDL_GetTicks() / 90U;
    for (unsigned i = 0; i < 5; ++i)
    {
        const int height = playing ? wave[(phase + i * 3U) % 16U] : 14;
        char id[12];
        std::snprintf(id, sizeof(id), "eq-%u", i);
        SetPixelProperty(document_, id, "height", height);
        SetPixelProperty(document_, id, "top", 91 - height);
        SetClass(document_, id, "playing", playing);
    }
}

void RadioApp::UpdateFocus()
{
    for (unsigned i = 0; i < CardCount; ++i)
    {
        char id[20];
        std::snprintf(id, sizeof(id), "card-%u", i);
        SetClass(document_, id, "focused", !search_open_ && !credits_open_ && focus_ == i);
    }
    for (unsigned i = 0; i < 3; ++i)
    {
        char id[20];
        std::snprintf(id, sizeof(id), "discover-%u", i);
        SetClass(document_, id, "focused",
                 !search_open_ && !credits_open_ && focus_ == kFocusDiscover + i);
    }
    SetClass(document_, "play-button", "focused",
             !search_open_ && !credits_open_ && focus_ == kFocusPlay);
    SetClass(document_, "credit-button", "focused",
             !search_open_ && !credits_open_ && focus_ == kFocusCredits);
    for (unsigned i = 0; i < 4; ++i)
    {
        char id[20];
        std::snprintf(id, sizeof(id), "filter-%u", i);
        SetClass(document_, id, "focused", search_open_ && search_focus_ == i + 1);
    }
    SetClass(document_, "search-query", "focused", search_open_ && search_focus_ == 0);
    SetClass(document_, "search-reset", "focused", search_open_ && search_focus_ == 5);
    SetClass(document_, "search-apply", "focused", search_open_ && search_focus_ == 6);
    SetClass(document_, "credits-close", "focused", credits_open_);
}

void RadioApp::UpdateSearch()
{
    SetText(document_, "search-query-label",
            *search_edit_ ? search_edit_
                          : "Press Cross to type a station, genre, language, or country...");

    const radio_facet_t *country = FindFacet(country_facets_, filter_country_);
    const radio_facet_t *genre = FindFacet(genre_facets_, filter_genre_);
    const radio_facet_t *language = FindFacet(language_facets_, filter_language_);
    char text[144];
    std::snprintf(text, sizeof(text), "COUNTRY  /  %u %s\n<  %s  >",
                  country ? country->station_count : static_cast<unsigned>(country_facets_.size()),
                  *filter_country_ ? "stations" : "choices",
                  country ? country->label : "All countries");
    SetMultilineText(document_, "filter-label-0", text);
    std::snprintf(text, sizeof(text), "GENRE  /  %u %s\n<  %s  >",
                  genre ? genre->station_count : static_cast<unsigned>(genre_facets_.size()),
                  *filter_genre_ ? "stations" : "choices", genre ? genre->label : "All genres");
    SetMultilineText(document_, "filter-label-1", text);
    std::snprintf(
        text, sizeof(text), "LANGUAGE  /  %u %s\n<  %s  >",
        language ? language->station_count : static_cast<unsigned>(language_facets_.size()),
        *filter_language_ ? "stations" : "choices", language ? language->label : "All languages");
    SetMultilineText(document_, "filter-label-2", text);
    const char *bitrate = filter_bitrate_ == 0     ? "Any bitrate"
                          : filter_bitrate_ == 64  ? "64+ kbps"
                          : filter_bitrate_ == 128 ? "128+ kbps"
                          : filter_bitrate_ == 192 ? "192+ kbps"
                                                   : "256+ kbps";
    std::snprintf(text, sizeof(text), "QUALITY  /  5 choices\n<  %s  >", bitrate);
    SetMultilineText(document_, "filter-label-3", text);
}

void RadioApp::OpenSearch(unsigned filter)
{
    if (search_open_ || credits_open_)
        return;
    search_open_ = true;
    CopyString(search_edit_, sizeof(search_edit_), search_query_);
    search_focus_ = filter < 4 ? filter + 1 : 0;
    SetVisible(document_, "search-overlay", true);
    UpdateSearch();
    UpdateFocus();
}

void RadioApp::CloseSearch(bool apply)
{
    if (!search_open_)
        return;
    radio_ime_cancel();
    search_open_ = false;
    SetVisible(document_, "search-overlay", false);
    if (apply)
    {
        CopyString(search_query_, sizeof(search_query_), search_edit_);
        view_ = View::Discover;
        page_start_ = selected_slot_ = focus_ = 0;
        RefreshAll();
        if (*search_query_ || *filter_country_ || *filter_genre_ || *filter_language_ ||
            filter_bitrate_ != 0U)
        {
            radio_catalog_query_t query{};
            CopyString(query.name, sizeof(query.name), search_query_);
            CopyString(query.country_code, sizeof(query.country_code), filter_country_);
            CopyString(query.tag, sizeof(query.tag), filter_genre_);
            CopyString(query.language, sizeof(query.language), filter_language_);
            query.bitrate_min = filter_bitrate_;
            radio_service_search(&query);
        }
    }
    UpdateFocus();
}

void RadioApp::OpenCredits()
{
    if (search_open_ || credits_open_)
        return;
    credits_open_ = true;
    SetVisible(document_, "credits-overlay", true);
    UpdateFocus();
}

void RadioApp::CloseCredits()
{
    if (!credits_open_)
        return;
    credits_open_ = false;
    focus_ = kFocusCredits;
    SetVisible(document_, "credits-overlay", false);
    UpdateFocus();
}

void RadioApp::SetView(int direction)
{
    int next = static_cast<int>(view_) + direction;
    const int count = static_cast<int>(View::Count);
    if (next < 0)
        next = count - 1;
    if (next >= count)
        next = 0;
    view_ = static_cast<View>(next);
    page_start_ = selected_slot_ = focus_ = 0;
    RefreshAll();
    UpdateFocus();
}

void RadioApp::ChangePage(int direction, unsigned focus_slot)
{
    if (direction > 0 && page_start_ + CardCount < visible_count_)
        page_start_ += CardCount;
    else if (direction < 0 && page_start_ >= CardCount)
        page_start_ -= CardCount;
    else
        return;
    selected_slot_ = focus_slot;
    focus_ = focus_slot;
    BuildVisibleList();
    RefreshCards();
    if (card_stations_[selected_slot_] == InvalidStation)
        selected_slot_ = focus_ = 0;
    RefreshHeading();
    RefreshDetail();
    UpdateFocus();
}

void RadioApp::ToggleFavorite()
{
    if (card_stations_[selected_slot_] == InvalidStation)
        return;
    radio_service_toggle_favorite(card_stations_[selected_slot_]);
    RefreshAll();
    UpdateFocus();
}

void RadioApp::TogglePlayback()
{
    if (card_stations_[selected_slot_] == InvalidStation)
        return;
    radio_service_status_t status{};
    radio_service_get_status(&status);
    if (PlaybackActive(status.playback_state))
    {
        pending_play_uuid_[0] = '\0';
        if (!radio_service_station_is_playing(card_stations_[selected_slot_]))
        {
            radio_station_t station{};
            if (radio_service_get_station(card_stations_[selected_slot_], &station))
                CopyString(pending_play_uuid_, sizeof(pending_play_uuid_), station.uuid);
        }
        radio_service_stop();
    }
    else
        radio_service_play(card_stations_[selected_slot_]);
}

void RadioApp::CycleFilter(unsigned filter, int direction)
{
    auto cycle =
        [direction](char *selected, std::size_t capacity, const std::vector<radio_facet_t> &facets)
    {
        const unsigned count = static_cast<unsigned>(facets.size());
        if (!count)
        {
            selected[0] = '\0';
            return;
        }
        int current = -1;
        for (unsigned i = 0; i < count; ++i)
            if (std::strcmp(selected, facets[i].value) == 0)
                current = static_cast<int>(i);
        int next = current + direction;
        if (next < -1)
            next = static_cast<int>(count) - 1;
        if (next >= static_cast<int>(count))
            next = -1;
        if (next < 0)
            selected[0] = '\0';
        else
            CopyString(selected, capacity, facets[next].value);
    };
    if (filter == 0)
        cycle(filter_country_, sizeof(filter_country_), country_facets_);
    else if (filter == 1)
        cycle(filter_genre_, sizeof(filter_genre_), genre_facets_);
    else if (filter == 2)
        cycle(filter_language_, sizeof(filter_language_), language_facets_);
    else
    {
        static const unsigned rates[] = {0, 64, 128, 192, 256};
        int current = 0;
        for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i)
            if (rates[i] == filter_bitrate_)
                current = static_cast<int>(i);
        current += direction;
        if (current < 0)
            current = static_cast<int>(sizeof(rates) / sizeof(rates[0])) - 1;
        if (current >= static_cast<int>(sizeof(rates) / sizeof(rates[0])))
            current = 0;
        filter_bitrate_ = rates[current];
    }
    UpdateSearch();
    RefreshDiscover();
}

void RadioApp::HandleSearchKey(radio_input_key_t key)
{
    if (key == RADIO_INPUT_CIRCLE)
    {
        CloseSearch(false);
        return;
    }
    if (key == RADIO_INPUT_OPTIONS)
    {
        radio_service_refresh();
        return;
    }
    if (key == RADIO_INPUT_UP)
        search_focus_ = search_focus_ ? search_focus_ - 1 : 6;
    else if (key == RADIO_INPUT_DOWN)
        search_focus_ = search_focus_ < 6 ? search_focus_ + 1 : 0;
    else if ((key == RADIO_INPUT_LEFT || key == RADIO_INPUT_RIGHT) && search_focus_ >= 5)
        search_focus_ = search_focus_ == 5 ? 6 : 5;
    else if ((key == RADIO_INPUT_LEFT || key == RADIO_INPUT_RIGHT) && search_focus_ >= 1 &&
             search_focus_ <= 4)
    {
        CycleFilter(search_focus_ - 1, key == RADIO_INPUT_LEFT ? -1 : 1);
    }
    else if (key == RADIO_INPUT_CROSS)
    {
        if (search_focus_ == 0)
            radio_ime_request(search_edit_, ImeResult, this);
        else if (search_focus_ <= 4)
            CycleFilter(search_focus_ - 1, 1);
        else if (search_focus_ == 5)
        {
            search_edit_[0] = filter_country_[0] = filter_genre_[0] = filter_language_[0] = '\0';
            filter_bitrate_ = 0;
            UpdateSearch();
        }
        else
            CloseSearch(true);
    }
    UpdateFocus();
}

void RadioApp::HandleMainKey(radio_input_key_t key)
{
    if (key == RADIO_INPUT_TRIANGLE)
    {
        OpenSearch(InvalidStation);
        return;
    }
    if (key == RADIO_INPUT_OPTIONS)
    {
        radio_service_refresh();
        return;
    }
    if (key == RADIO_INPUT_L1)
    {
        SetView(-1);
        return;
    }
    if (key == RADIO_INPUT_R1)
    {
        SetView(1);
        return;
    }
    if (key == RADIO_INPUT_SQUARE)
    {
        ToggleFavorite();
        return;
    }
    if (key == RADIO_INPUT_CIRCLE)
    {
        if (view_ != View::Popular)
        {
            view_ = View::Popular;
            page_start_ = selected_slot_ = focus_ = 0;
            RefreshAll();
            UpdateFocus();
        }
        return;
    }

    if (focus_ < CardCount)
    {
        const unsigned slot = focus_;
        if (key == RADIO_INPUT_CROSS)
            TogglePlayback();
        else if (key == RADIO_INPUT_LEFT && (slot & 1U))
            focus_ = selected_slot_ = slot - 1;
        else if (key == RADIO_INPUT_RIGHT)
        {
            if (!(slot & 1U) && card_stations_[slot + 1] != InvalidStation)
                focus_ = selected_slot_ = slot + 1;
            else
                focus_ = kFocusPlay;
        }
        else if (key == RADIO_INPUT_UP)
        {
            if (slot >= 2)
                focus_ = selected_slot_ = slot - 2;
            else
            {
                ChangePage(-1, slot + 2);
                return;
            }
        }
        else if (key == RADIO_INPUT_DOWN)
        {
            if (slot < 2 && card_stations_[slot + 2] != InvalidStation)
                focus_ = selected_slot_ = slot + 2;
            else if (page_start_ + CardCount < visible_count_)
            {
                ChangePage(1, slot & 1U);
                return;
            }
            else if (view_ == View::Discover)
                focus_ = kFocusDiscover + slot % 3;
            else
                focus_ = kFocusPlay;
        }
        RefreshDetail();
    }
    else if (focus_ >= kFocusDiscover && focus_ < kFocusPlay)
    {
        const unsigned filter = focus_ - kFocusDiscover;
        if (key == RADIO_INPUT_CROSS)
            OpenSearch(filter);
        else if (key == RADIO_INPUT_LEFT && filter)
            --focus_;
        else if (key == RADIO_INPUT_RIGHT && filter < 2)
            ++focus_;
        else if (key == RADIO_INPUT_UP)
            focus_ = selected_slot_;
        else if (key == RADIO_INPUT_DOWN)
            focus_ = kFocusPlay;
    }
    else if (focus_ == kFocusPlay)
    {
        if (key == RADIO_INPUT_CROSS)
            TogglePlayback();
        else if (key == RADIO_INPUT_LEFT || key == RADIO_INPUT_UP)
            focus_ = selected_slot_;
        else if (key == RADIO_INPUT_RIGHT || key == RADIO_INPUT_DOWN)
            focus_ = kFocusCredits;
    }
    else if (focus_ == kFocusCredits)
    {
        if (key == RADIO_INPUT_CROSS)
            OpenCredits();
        else if (key == RADIO_INPUT_UP || key == RADIO_INPUT_LEFT || key == RADIO_INPUT_RIGHT)
            focus_ = kFocusPlay;
    }
    UpdateFocus();
}

void RadioApp::HandleInput(const radio_input_event_t &event)
{
    if (!event.pressed)
        return;
    if (credits_open_)
    {
        if (event.key == RADIO_INPUT_CROSS || event.key == RADIO_INPUT_CIRCLE)
            CloseCredits();
        return;
    }
    if (search_open_)
    {
        HandleSearchKey(event.key);
        return;
    }
    HandleMainKey(event.key);
}

void RadioApp::ImeResult(const char *text, void *user_data)
{
    RadioApp *app = static_cast<RadioApp *>(user_data);
    if (!app || !text)
        return;
    CopyString(app->search_edit_, sizeof(app->search_edit_), text);
    app->UpdateSearch();
}

void RadioApp::Poll()
{
    radio_service_status_t status{};
    radio_service_get_status(&status);
    if (!have_last_status_ || status.catalog_generation != last_status_.catalog_generation ||
        status.station_count != last_status_.station_count)
    {
        RebuildFacets();
        RefreshAll();
    }
    if (*pending_play_uuid_ && status.playback_state == RADIO_PLAYBACK_STOPPED)
    {
        char pending_uuid[sizeof(pending_play_uuid_)]{};
        CopyString(pending_uuid, sizeof(pending_uuid), pending_play_uuid_);
        pending_play_uuid_[0] = '\0';
        for (unsigned i = 0; i < status.station_count; ++i)
        {
            radio_station_t station{};
            if (radio_service_get_station(i, &station) &&
                std::strcmp(station.uuid, pending_uuid) == 0)
            {
                radio_service_play(i);
                radio_service_get_status(&status);
                break;
            }
        }
    }
    if (!have_last_status_ || status.playback_state != last_status_.playback_state ||
        status.playing_index != last_status_.playing_index ||
        status.sample_rate != last_status_.sample_rate ||
        status.channels != last_status_.channels || status.error_code != last_status_.error_code)
    {
        RefreshPlayback(status);
    }
    if (!have_last_status_ || status.catalog_state != last_status_.catalog_state ||
        status.refreshing != last_status_.refreshing ||
        status.searching != last_status_.searching ||
        status.sync_station_count != last_status_.sync_station_count ||
        status.error_code != last_status_.error_code)
        RefreshConnection(status);
    UpdateEqualizer(status);
    last_status_ = status;
    have_last_status_ = true;
}
