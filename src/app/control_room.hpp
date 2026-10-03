// ProsperoRadio - Settings, after the kit's "Control Room": a rail, live controls.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/session.hpp"
#include "ui/components/form.hpp"
#include "ui/components/progress.hpp"
#include "ui/components/sidenav.hpp"
#include "ui/glyphs.hpp"

#include <string>

namespace radio
{

// A full screen: the categories on a rail at the left, the focused
// category's controls on a frosted panel at the right. Every control applies
// at once (the sound changes while the station plays) and is saved a moment
// after the last change.
class ControlRoom
{
  public:
    enum class Result : std::uint8_t
    {
        none,
        close,
        refresh, // "Update now" in Catalogue
    };

    ControlRoom(Session &session, std::string version);

    void open(ui::Feedback &feedback);
    bool is_open() const
    {
        return open_;
    }
    // 0 closed .. 1 open, for the screens behind it.
    float shown() const
    {
        return amount_.value;
    }

    Result handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;
    int hints(ui::Hint *out, int capacity) const;

  private:
    enum Category : int
    {
        kSound,
        kInterface,
        kCatalogue,
        kAbout,
    };

    void show_category(int category);
    void apply(int row);
    void reset_sound();
    void draw_meters(ui::Canvas &canvas, const Rect &box) const;
    void draw_about(ui::Canvas &canvas, const Rect &box) const;

    Session &session_;
    std::string version_;
    ui::SideNav nav_;
    ui::Form form_;
    ui::Meter left_;
    ui::Meter right_;
    int category_ = kSound;
    bool on_rail_ = false;
    bool open_ = false;
    bool dirty_ = false;
    float since_change_ = 0.0f;
    float saved_ = 0.0f; // seconds the "Saved" mark has left to show
    float age_ = 0.0f;
    tween::Spring amount_;
};

} // namespace radio
