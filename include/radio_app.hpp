// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "radio_input.hpp"
#include "radio_service.hpp"

#include <vector>

namespace Rml {
class ElementDocument;
}

class RadioApp {
public:
    bool Initialize(Rml::ElementDocument* document);
    void Poll();
    void HandleInput(const radio_input_event_t& event);
    void Shutdown();

private:
    enum class View {
        Popular,
        Trending,
        Voted,
        Favorites,
        Discover,
        Count
    };

    static constexpr unsigned CardCount = 4;
    static constexpr unsigned InvalidStation = ~0U;

    Rml::ElementDocument* document_ = nullptr;
    std::vector<unsigned> visible_indices_;
    unsigned card_stations_[CardCount]{};
    unsigned visible_count_ = 0;
    unsigned page_start_ = 0;
    unsigned selected_slot_ = 0;
    char pending_play_uuid_[40]{};
    unsigned focus_ = 0;
    unsigned search_focus_ = 0;
    View view_ = View::Popular;
    bool search_open_ = false;
    bool credits_open_ = false;
    bool service_started_ = false;
    bool have_last_status_ = false;
    radio_service_status_t last_status_{};
    char search_query_[157]{};
    char search_edit_[157]{};
    char filter_country_[4]{};
    char filter_genre_[64]{};
    char filter_language_[64]{};
    unsigned filter_bitrate_ = 0;
    std::vector<radio_facet_t> country_facets_;
    std::vector<radio_facet_t> genre_facets_;
    std::vector<radio_facet_t> language_facets_;

    void RebuildFacets();
    void BuildVisibleList();
    bool StationVisible(const radio_station_t& station) const;
    void RefreshAll();
    void RefreshCards();
    void RefreshCard(unsigned slot);
    void RefreshTabs();
    void RefreshHeading();
    void RefreshDiscover();
    void RefreshDetail();
    void RefreshPlayback(const radio_service_status_t& status);
    void RefreshConnection(const radio_service_status_t& status);
    void UpdateEqualizer(const radio_service_status_t& status);
    void UpdateFocus();
    void UpdateSearch();
    void OpenSearch(unsigned filter);
    void CloseSearch(bool apply);
    void OpenCredits();
    void CloseCredits();
    void SetView(int direction);
    void ChangePage(int direction, unsigned focus_slot);
    void ToggleFavorite();
    void TogglePlayback();
    void HandleMainKey(radio_input_key_t key);
    void HandleSearchKey(radio_input_key_t key);
    void CycleFilter(unsigned filter, int direction);
    static void ImeResult(const char* text, void* user_data);
};
