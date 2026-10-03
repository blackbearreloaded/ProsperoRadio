// ProsperoRadio - What every screen shares: service state, playback and settings.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session.hpp"

#include "app/draw.hpp"
#include "app/levels.hpp"
#include "app/platform.hpp"
#include "app/theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace radio
{

Session::Session(const ui::Fonts &shared_fonts) : fonts(shared_fonts), theme(night_signal())
{
    toasts.style.theme = theme;
    // Under the status corner: there a toast covers artwork, never the list
    // the player is moving through.
    toasts.style.anchor = ui::ToastAnchor::top_right;
    toasts.style.frosted = true;
    toasts.style.margin = 24.0f;
    toasts.set_bounds({kMargin - 24.0f, 96.0f, gfx::kVirtualWidth - 2.0f * (kMargin - 24.0f),
                       gfx::kVirtualHeight - 192.0f});
}

void Session::load_settings()
{
    std::string text;
    if (radio_settings_load(&text))
    {
        // One "key=value" a line; what is missing or unknown keeps its default.
        std::size_t from = 0;
        while (from < text.size())
        {
            std::size_t to = text.find('\n', from);
            if (to == std::string::npos)
                to = text.size();
            const std::string line = text.substr(from, to - from);
            from = to + 1;
            const std::size_t equals = line.find('=');
            if (equals == std::string::npos)
                continue;
            const std::string key = line.substr(0, equals);
            const float value = std::strtof(line.c_str() + equals + 1, nullptr);
            if (key == "reduced_motion")
                settings.reduced_motion = value != 0.0f;
            else if (key == "interface_sounds")
                settings.interface_sounds = value != 0.0f;
            else if (key == "quiet_while_playing")
                settings.quiet_while_playing = value != 0.0f;
            else if (key == "volume")
                settings.volume = std::clamp(value, 0.0f, 1.0f);
            else if (key == "bass_db")
                settings.bass_db = std::clamp(value, -12.0f, 12.0f);
            else if (key == "treble_db")
                settings.treble_db = std::clamp(value, -12.0f, 12.0f);
            else if (key == "balance")
                settings.balance = std::clamp(value, -1.0f, 1.0f);
            else if (key == "mono")
                settings.mono = value != 0.0f;
        }
    }
    apply_audio();
}

void Session::save_settings() const
{
    char text[400];
    std::snprintf(text, sizeof(text),
                  "reduced_motion=%d\ninterface_sounds=%d\nquiet_while_playing=%d\n"
                  "volume=%.2f\nbass_db=%.0f\ntreble_db=%.0f\nbalance=%.2f\nmono=%d\n",
                  settings.reduced_motion ? 1 : 0, settings.interface_sounds ? 1 : 0,
                  settings.quiet_while_playing ? 1 : 0, static_cast<double>(settings.volume),
                  static_cast<double>(settings.bass_db), static_cast<double>(settings.treble_db),
                  static_cast<double>(settings.balance), settings.mono ? 1 : 0);
    radio_settings_save(text);
}

void Session::apply_audio() const
{
    radio_audio_settings_t audio;
    audio.volume = settings.volume;
    audio.bass_db = settings.bass_db;
    audio.treble_db = settings.treble_db;
    audio.balance = settings.balance;
    audio.mono = settings.mono;
    radio_audio_configure(audio);
}

bool Session::active() const
{
    return status.playback_state == RADIO_PLAYBACK_CONNECTING ||
           status.playback_state == RADIO_PLAYBACK_BUFFERING ||
           status.playback_state == RADIO_PLAYBACK_PLAYING ||
           status.playback_state == RADIO_PLAYBACK_STOPPING;
}

bool Session::is_current(const radio_station_t &station) const
{
    return has_playing && (active() || has_pending_) &&
           std::strcmp(playing.uuid, station.uuid) == 0;
}

void Session::load_facets()
{
    const auto load = [](radio_facet_kind_t kind, std::vector<radio_facet_t> &facets)
    {
        facets.clear();
        const unsigned count = radio_service_get_facet_count(kind);
        facets.reserve(count);
        for (unsigned i = 0; i < count; ++i)
        {
            radio_facet_t facet{};
            if (radio_service_get_facet(kind, i, &facet))
                facets.push_back(facet);
        }
    };
    load(RADIO_FACET_COUNTRY, countries);
    load(RADIO_FACET_GENRE, genres);
    load(RADIO_FACET_LANGUAGE, languages);
}

void Session::poll(float dt)
{
    clock += dt;
    radio_service_get_status(&status);

    if (!polled_ || status.catalog_generation != last_generation_)
    {
        // The catalogue changed underneath: every page in memory may be stale.
        browse.invalidate();
        play_list_.invalidate();
        load_facets();
        ++revision;
        last_generation_ = status.catalog_generation;
    }
    if (polled_ && last_refreshing_ && !status.refreshing && !status.searching &&
        status.catalog_state == RADIO_CATALOG_READY)
    {
        toasts.push(ui::StatusKind::success, "Catalogue updated",
                    group_digits(status.catalog_size) + " stations");
    }
    last_refreshing_ = status.refreshing;

    // A station the app did not start (a restart found one playing).
    if (!has_playing && active() && radio_service_get_playing_station(&playing))
        has_playing = true;

    // The service plays one stream at a time and must have let go of it
    // before the next one starts.
    if (has_pending_ && (status.playback_state == RADIO_PLAYBACK_STOPPED ||
                         status.playback_state == RADIO_PLAYBACK_ERROR))
    {
        start_pending();
        radio_service_get_status(&status);
    }

    if (status.playback_state == RADIO_PLAYBACK_PLAYING)
        listening += dt;
    if (polled_ && status.playback_state == RADIO_PLAYBACK_ERROR &&
        last_state_ != RADIO_PLAYBACK_ERROR && has_playing)
    {
        char detail[160];
        std::snprintf(detail, sizeof(detail), "%s did not answer (%08x).", playing.name,
                      static_cast<unsigned>(status.error_code));
        toasts.push(ui::StatusKind::danger, "This station cannot be played", detail, 6.0f);
    }
    last_state_ = status.playback_state;
    polled_ = true;

    update_levels(dt);
}

void Session::update_levels(float dt)
{
    float measured[kBands] = {};
    const bool live = on_air() && radio_levels(measured, kBands);
    float low = 0.0f;
    for (int i = 0; i < kBands; ++i)
    {
        const float wanted = live ? std::clamp(measured[i], 0.0f, 1.0f) : 0.0f;
        float &level = levels[static_cast<std::size_t>(i)];
        // Fast up, slow down: a bar jumps with a beat and then falls back.
        const float rate = wanted > level ? 30.0f : 7.0f;
        level += (wanted - level) * (1.0f - std::exp(-rate * dt));
        float &peak = peaks[static_cast<std::size_t>(i)];
        peak = std::max(level, peak - 0.7f * dt);
        if (i < 6)
            low += level;
    }
    energy = low / 6.0f;
}

void Session::play(const Catalog &list, unsigned index)
{
    const radio_station_t *station = list.peek(index);
    if (station == nullptr)
        return;
    pending_ = *station;
    has_pending_ = true;
    if (&list != &play_list_ && !same_spec(list.spec(), play_list_.spec()))
        play_list_.set_spec(list.spec());
    play_index_ = index;
    has_context_ = true;
    // The screens show the new station at once; the sound follows.
    playing = pending_;
    has_playing = true;
    listening = 0.0f;
    if (active())
        radio_service_stop();
}

void Session::start_pending()
{
    has_pending_ = false;
    int slot = play_list_.select(play_index_);
    const radio_station_t *found = slot >= 0 ? play_list_.peek(play_index_) : nullptr;
    if (found == nullptr || std::strcmp(found->uuid, pending_.uuid) != 0)
    {
        // The list moved under the station (a sync landed): look for it on
        // the page the service now holds.
        slot = -1;
        const unsigned first = play_index_ - play_index_ % Catalog::kPageSize;
        for (unsigned i = 0; i < Catalog::kPageSize; ++i)
        {
            const radio_station_t *other = play_list_.peek(first + i);
            if (other != nullptr && std::strcmp(other->uuid, pending_.uuid) == 0)
            {
                slot = static_cast<int>(i);
                play_index_ = first + i;
                break;
            }
        }
    }
    if (slot < 0)
    {
        toasts.push(ui::StatusKind::warning, "This station left the list",
                    "The catalogue changed. Pick it again from the list.");
        return;
    }
    radio_service_play(static_cast<unsigned>(slot));
}

void Session::stop()
{
    has_pending_ = false;
    if (active())
        radio_service_stop();
}

void Session::resume()
{
    if (!has_playing || active() || !has_context_)
        return;
    pending_ = playing;
    has_pending_ = true;
    listening = 0.0f;
}

bool Session::can_zap(int direction) const
{
    if (!has_context_ || !play_list_.known())
        return has_context_ && direction > 0;
    if (direction < 0)
        return play_index_ > 0;
    return play_index_ + 1 < play_list_.total();
}

void Session::zap(int direction)
{
    if (!can_zap(direction))
        return;
    const unsigned target = direction < 0 ? play_index_ - 1 : play_index_ + 1;
    if (play_list_.fetch(target) != nullptr)
        play(play_list_, target);
}

bool Session::toggle_favorite(Catalog &list, unsigned index, ui::Feedback &feedback)
{
    const int slot = list.select(index);
    const radio_station_t *station = slot >= 0 ? list.peek(index) : nullptr;
    if (station == nullptr)
        return false;
    const radio_station_t copy = *station;
    radio_service_toggle_favorite(static_cast<unsigned>(slot));
    const bool now = radio_service_is_favorite(copy.uuid);
    feedback.play(now ? audio::Cue::favorite_on : audio::Cue::favorite_off);
    toasts.push(now ? ui::StatusKind::success : ui::StatusKind::info,
                now ? "Added to Favorites" : "Removed from Favorites", copy.name, 3.0f);
    // A list of favourites just changed its members; the others only a mark.
    if (browse.spec().view == View::favorites)
        browse.invalidate();
    else
        browse.refresh_favorites();
    if (play_list_.spec().view == View::favorites)
        play_list_.invalidate();
    else
        play_list_.refresh_favorites();
    return now;
}

} // namespace radio
