// ProsperoRadio - What every screen shares: service state, playback and settings.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/catalog.hpp"
#include "app/kit.hpp"
#include "radio_service.hpp"
#include "ui/components/toast.hpp"

#include <array>
#include <vector>

namespace radio
{

struct Settings
{
    bool reduced_motion = false;
    bool interface_sounds = true;
    bool quiet_while_playing = true; // no interface sounds over a station
    // The station's sound, shaped by the platform as it plays.
    float volume = 1.0f;    // 0..1
    float bass_db = 0.0f;   // -12..12
    float treble_db = 0.0f; // -12..12
    float balance = 0.0f;   // -1 left .. 1 right
    bool mono = false;
};

// The one object the screens share. It polls the service once a frame, owns
// the list being browsed and the list being played from, and turns "play
// this" into the stop-then-start the service needs.
class Session
{
  public:
    static constexpr int kBands = 32;

    explicit Session(const ui::Fonts &fonts);

    const ui::Fonts &fonts;
    ui::Theme theme;
    Settings settings;

    // ---- the service, as of this frame ----
    radio_service_status_t status{};
    // The station that is playing, connecting, or was playing last.
    radio_station_t playing{};
    bool has_playing = false;
    float listening = 0.0f; // seconds on air since it started
    // Counts every change of the catalogue (a sync, a search that landed).
    unsigned revision = 0;
    std::vector<radio_facet_t> countries;
    std::vector<radio_facet_t> genres;
    std::vector<radio_facet_t> languages;

    // ---- lists ----
    Catalog browse; // what Home and Discover show

    // ---- the visualizer ----
    std::array<float, kBands> levels{};
    std::array<float, kBands> peaks{};
    float energy = 0.0f; // the low bands, for things that breathe with the bass

    // ---- shared presentation ----
    float clock = 0.0f; // free-running seconds
    int hour = -1;      // wall clock, when the platform knows it
    int minute = 0;
    ui::ToastStack toasts;

    void poll(float dt);

    // The settings as the listener left them: read once at start, written
    // whenever they change. apply_audio() hands the sound to the platform.
    void load_settings();
    void save_settings() const;
    void apply_audio() const;

    // Connecting, buffering, playing or stopping.
    bool active() const;
    bool on_air() const
    {
        return status.playback_state == RADIO_PLAYBACK_PLAYING;
    }
    // `station` is the one that is active right now.
    bool is_current(const radio_station_t &station) const;

    // Starts a station of a list, stopping what plays first when need be.
    void play(const Catalog &list, unsigned index);
    void stop();
    // Plays the last station again.
    void resume();
    // The neighbours of the playing station in the list it was started from.
    bool can_zap(int direction) const;
    void zap(int direction);
    const Catalog &play_list() const
    {
        return play_list_;
    }
    Catalog &play_list()
    {
        return play_list_;
    }
    unsigned play_index() const
    {
        return play_index_;
    }
    bool has_context() const
    {
        return has_context_;
    }

    // Flips the favourite mark of a station of a list, with its cue and its
    // toast. Returns the new state.
    bool toggle_favorite(Catalog &list, unsigned index, ui::Feedback &feedback);

  private:
    void load_facets();
    void start_pending();
    void update_levels(float dt);

    Catalog play_list_;
    unsigned play_index_ = 0;
    bool has_context_ = false;
    radio_station_t pending_{};
    bool has_pending_ = false;
    bool polled_ = false;
    unsigned last_generation_ = 0;
    bool last_refreshing_ = false;
    radio_playback_state_t last_state_ = RADIO_PLAYBACK_STOPPED;
};

} // namespace radio
