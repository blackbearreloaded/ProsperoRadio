// ProsperoRadio - Now Playing: the station on air, a visualizer and what is next.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/session.hpp"
#include "ui/components/list.hpp"
#include "ui/components/progress.hpp"
#include "ui/glyphs.hpp"

#include <array>

namespace radio
{

// A place to sit while listening. It follows the kit's "Now Playing" design
// (breathing artwork, a spectrum, a transport row, a glass list) with what a
// live stream changes: there is nothing to scrub, so the line under the
// spectrum counts the time on air, and "previous" and "next" turn the dial
// through the list the station was started from.
class NowPlayingScreen
{
  public:
    enum class Result : std::uint8_t
    {
        none,
        close,
    };

    explicit NowPlayingScreen(Session &session);

    void enter();
    Result handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;
    int hints(ui::Hint *out, int capacity) const;

  private:
    enum Control : int
    {
        kFavorite,
        kPrevious,
        kMain, // stop while a station plays, play when it does not
        kNext,
        kList,
        kControls,
    };
    enum class Zone : std::uint8_t
    {
        transport,
        list,
    };
    // Where the main block sits: beside the list, or centred without it.
    struct Layout
    {
        float x = 0.0f;
        float w = 0.0f;
        float cx = 0.0f;
        Rect art;
        float column_x = 0.0f;
        float column_w = 0.0f;
    };

    Layout layout() const;
    float appear(int order) const;
    float slide(float in, float distance) const;
    Rect ring_target() const;
    void activate(int control, ui::Feedback &feedback);
    void turn(int direction, ui::Feedback &feedback);
    void favorite(ui::Feedback &feedback);
    void refuse(ui::Feedback &feedback, float direction);
    void rebuild_list();
    const char *state_word() const;

    void draw_header(gfx::DrawList &list, const Layout &l) const;
    void draw_artwork(gfx::DrawList &list, const ui::Fonts &fonts, const Layout &l) const;
    void draw_station_text(ui::Canvas &canvas, const Layout &l, const radio_station_t &station,
                           float alpha, float dx) const;
    void draw_info(ui::Canvas &canvas, const Layout &l) const;
    void draw_visualizer(gfx::DrawList &list, const Layout &l) const;
    void draw_on_air(ui::Canvas &canvas, const Layout &l) const;
    void draw_transport(ui::Canvas &canvas, const Layout &l) const;
    void draw_focus_ring(gfx::DrawList &list, const Layout &l, bool light) const;
    void draw_up_next(ui::Canvas &canvas) const;

    Session &session_;
    ui::ListView up_next_;
    ui::Spinner spinner_;
    Zone zone_ = Zone::transport;
    int button_ = kMain;
    bool list_open_ = true;
    bool is_favorite_ = false;
    float age_ = 0.0f;

    // The list shows a window of the play list around the station on air.
    unsigned window_base_ = 0;
    unsigned window_signature_ = ~0U;

    // The station shown, and the one leaving while a change cross-fades.
    radio_station_t shown_{};
    bool has_shown_ = false;
    unsigned shown_index_ = 0;
    radio_station_t previous_{};
    bool has_previous_ = false;
    float travel_ = 1.0f;
    tween::Timer swap_;

    tween::Spring live_; // 1 while the station is on air
    tween::Spring glow_; // follows the bass, softly
    tween::Spring list_amount_;
    tween::Spring list_focus_;
    ui::SpringRect ring_;
    ui::SpringColor accent_;
    ui::SpringColor body_;
    std::array<ui::Pulse, kControls> press_{};
    ui::Pulse nudge_;
    float nudge_direction_ = 0.0f;
    ui::Pulse star_;
};

} // namespace radio
