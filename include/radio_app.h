#pragma once

#include "radio_input.h"
#include "radio_service.h"

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
    struct Facet {
        char value[48]{};
        unsigned count = 0;
    };

    enum class View {
        Popular,
        Trending,
        Voted,
        Favorites,
        Discover,
        Count
    };

    static constexpr unsigned CardCount = 4;
    static constexpr unsigned FacetMax = 24;
    static constexpr unsigned InvalidStation = ~0U;

    Rml::ElementDocument* document_ = nullptr;
    unsigned visible_indices_[RADIO_MAX_STATIONS]{};
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
    char filter_country_[48]{};
    char filter_genre_[48]{};
    char filter_language_[48]{};
    unsigned filter_bitrate_ = 0;
    Facet country_facets_[FacetMax]{};
    Facet genre_facets_[FacetMax]{};
    Facet language_facets_[FacetMax]{};
    unsigned country_facet_count_ = 0;
    unsigned genre_facet_count_ = 0;
    unsigned language_facet_count_ = 0;

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
