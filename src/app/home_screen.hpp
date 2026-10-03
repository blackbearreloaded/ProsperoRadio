// ProsperoRadio - Home: the station in focus as a hero, the list as a grid.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/held_step.hpp"
#include "app/letter_rail.hpp"
#include "app/session.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/stat.hpp"
#include "ui/glyphs.hpp"

namespace radio
{

// Popular, Trending, Top rated and Favorites all look like this: the top of
// the screen says everything about the station under the focus, the grid
// below scrolls through the whole list, sixteen stations in memory at a time.
class HomeScreen
{
  public:
    enum class Result : std::uint8_t
    {
        none,
        open_player,    // confirm on the station that is already playing
        browse_popular, // the empty Favorites screen sends the player there
        try_again,      // no catalogue and no way to Radio Browser: ask again
    };

    explicit HomeScreen(Session &session);

    // The screen came (back) into view: replay its entrance.
    void enter();
    // Another list is shown: the focus returns to its first station.
    void reset();
    // While a station plays, the hero shows it, after the kit's Now Playing:
    // artwork, name, the visualizer and the transport. Moving through the grid
    // brings back the station under the focus; up from the first row goes to
    // the transport.
    void show_player();

    Result handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;
    // What the buttons do here, for the hint row. Returns how many.
    int hints(ui::Hint *out, int capacity) const;

    // The station under the focus, when its page is in memory.
    const radio_station_t *focused() const;
    int focus() const
    {
        return grid_.focus();
    }

  private:
    // Nothing saved on the console and the download failed.
    bool unreachable() const;
    void begin_swap(int from);
    void draw_hero_text(ui::Canvas &canvas, const radio_station_t &station, unsigned index,
                        bool favorite, float alpha, float dx) const;
    void draw_hero_art(ui::Canvas &canvas, const radio_station_t &station, float alpha) const;
    void draw_waiting(ui::Canvas &canvas) const;
    // The hero while a station plays.
    bool player_shown() const;
    Result handle_player(const InputFrame &input, ui::Feedback &feedback);
    void draw_player(ui::Canvas &canvas, float alpha) const;
    Rect player_button(int index) const;
    float appear(int order) const;

    Session &session_;
    ui::GridView grid_;
    ui::EmptyState empty_;
    float age_ = 0.0f;
    // The hero cross-fades: the station that was in focus leaves while the
    // new one arrives.
    radio_station_t previous_{};
    unsigned previous_index_ = 0;
    bool previous_favorite_ = false;
    bool has_previous_ = false;
    float travel_ = 1.0f;
    tween::Timer swap_;
    ui::Pulse star_;
    // ---- the playing station in the hero ----
    bool player_mode_ = false;    // the hero shows the playing station
    bool player_focus_ = false;   // the transport has the focus
    int button_ = 1;              // previous, play / stop, next, favourite, full screen
    tween::Spring player_amount_; // 0 the focused station .. 1 the playing one
    tween::Spring ring_x_;        // the focus ring glides between the buttons
    ui::Pulse press_;
    HeldStep held_; // L2 / R2
    LetterRail rail_;
};

} // namespace radio
