// ProsperoRadio - The update dialog: a newer release, its download, and the close that ends it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/kit.hpp"
#include "app/platform.hpp"
#include "ui/components/component.hpp"

#include <string>

namespace radio
{

class Session;

// A newer release is offered once, when the app opens: "Update now" downloads
// it, checks it and unpacks it beside the app while a ring fills, then the app
// closes so the update helper can replace its files; "Skip" keeps this version
// until the app is opened again. Circle cancels a download, and nothing of the
// app has changed until the ring is full. A failure says why and offers to try
// again. (The dialog is ProsperoEden's, in Night Signal's colours.)
class UpdateDialog
{
  public:
    explicit UpdateDialog(Session &session);

    // Shows the offer. `update` must be installable.
    void offer(const radio_update_t &update, ui::Feedback &feedback);
    // Open: it takes the input, and the screens behind it rest.
    bool is_open() const
    {
        return open_;
    }
    void handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt, ui::Feedback &feedback);
    void draw(ui::Canvas &canvas) const;

    // The update is staged and the helper waits: the app must close now.
    bool wants_quit() const
    {
        return quit_;
    }

  private:
    enum class Stage
    {
        offer,
        working,
        cancelling,
        closing,
        failed,
    };

    void begin(ui::Feedback &feedback);
    void fail(std::string why, ui::Feedback &feedback);
    void dismiss(ui::Feedback &feedback);
    float motion() const;

    Session &session_;
    radio_update_t update_;
    radio_update_progress_t progress_;
    Stage stage_ = Stage::offer;
    bool open_ = false;
    bool quit_ = false;
    int choice_ = 0;
    tween::Spring shown_;    // 0 closed .. 1 open
    tween::Spring choice_x_; // the focus plate between the two buttons
    tween::Spring height_;   // the panel is shorter while it works
    tween::Spring fraction_; // the ring
    float stage_time_ = 0.0f;
    float spin_ = 0.0f;
    float press_ = 0.0f;
};

} // namespace radio
