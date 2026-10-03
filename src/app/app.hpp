// ProsperoRadio - The interface: tabs, the screens, and everything that floats.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/control_room.hpp"
#include "app/home_screen.hpp"
#include "app/letter_screen.hpp"
#include "app/now_playing_screen.hpp"
#include "app/search_screen.hpp"
#include "app/session.hpp"
#include "ui/components/banner.hpp"
#include "ui/components/dialog.hpp"
#include "ui/components/loading_screen.hpp"
#include "ui/components/menu.hpp"
#include "ui/components/tabs.hpp"

#include <string>

namespace radio
{

// The whole interface as one object: the frame loop gives it the controller
// and the elapsed time, it gives back a frame to draw and the sounds to play.
// It talks to the radio through radio_service.hpp only, so the same code
// runs on the console and, against a stand-in service, on a PC.
// How long the notice of a newer release stays on screen.
constexpr float kUpdateNoticeSeconds = 10.0f;

class App
{
  public:
    // glass_texture is the renderer's blurred copy of the frame (panels frost
    // it); version is shown in About.
    App(const ui::Fonts &fonts, std::uint32_t glass_texture, std::string version);

    // The wall clock for the status corner; a negative hour hides it.
    void set_time(int hour, int minute);

    void update(const InputFrame &input, float dt, ui::Feedback &feedback);
    // Records the frame. It changes nothing: it may run more than once.
    void draw(Frame &frame) const;

    // The player asked to close the app: end it through the system.
    bool wants_quit() const
    {
        return quit_;
    }

  private:
    bool discovering() const
    {
        return tabs_.active() == static_cast<int>(View::discover);
    }
    // Favorites: the alphabetical list with the letter rail.
    bool lettered() const
    {
        return tabs_.active() == static_cast<int>(View::favorites);
    }
    void show_tab(int index, bool glide);
    void tab_changed();
    void open_player(ui::Feedback &feedback);
    void open_menu(ui::Feedback &feedback);
    void run_menu(int tag, ui::Feedback &feedback);
    void refresh(ui::Feedback &feedback);
    void handle_browse(const InputFrame &input, ui::Feedback &feedback);
    void follow_station(float dt);

    void draw_header(ui::Canvas &canvas) const;
    void draw_status(ui::Canvas &canvas) const;
    void draw_bottom_bar(ui::Canvas &canvas) const;
    float draw_mini_player(ui::Canvas &canvas, const Rect &bar) const;

    Session session_;
    HomeScreen home_;
    LetterScreen letters_;
    SearchScreen search_;
    NowPlayingScreen player_;
    ui::TabBar tabs_;
    ui::Menu menu_;
    ControlRoom room_;
    ui::Dialog about_;
    ui::Dialog closing_;
    ui::Banner offline_;
    ui::LoadingScreen loader_;

    std::uint32_t glass_texture_ = 0;
    std::string version_;
    bool player_open_ = false;
    tween::Spring player_amount_; // 0 browsing .. 1 Now Playing
    // The backdrop leans toward the colour of the station in focus.
    ui::SpringColor lean_;
    ui::SpringColor lean_dark_;
    tween::Spring lean_amount_;
    float drift_ = 0.0f;
    unsigned seen_revision_ = 0;
    bool quit_ = false;
};

} // namespace radio
