// ProsperoRadio - The update dialog: a newer release, its notes, its download.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/kit.hpp"
#include "app/platform.hpp"
#include "ui/components/component.hpp"

#include <string>
#include <vector>

namespace radio
{

class Session;

// A newer release is offered once, when the app opens: "Update now" downloads
// it, checks it and unpacks it beside the app while a ring fills, then the app
// closes so the update helper can replace its files; "Skip" keeps this version
// until the app is opened again. When the release has notes, "What's new"
// (or Triangle) shows them in a view of their own that scrolls. Circle cancels
// a download, and nothing of the app has changed until the ring is full. A
// failure says why and offers to try again. (The dialog is ProsperoEden's, in
// Night Signal's colours.)
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
        notes,
        working,
        cancelling,
        closing,
        failed,
    };
    // One laid-out line of the notes, and the tinted box behind a callout.
    struct NoteLine
    {
        std::string text;
        float y = 0.0f;
        float height = 0.0f;
        float size = 0.0f;
        float indent = 0.0f;
        bool heading = false;
        bool bullet = false;
        bool muted = false;
    };
    struct NoteBox
    {
        float top = 0.0f;
        float bottom = 0.0f;
        bool warning = false;
    };

    void begin(ui::Feedback &feedback);
    void fail(std::string why, ui::Feedback &feedback);
    void dismiss(ui::Feedback &feedback);
    void open_notes(ui::Feedback &feedback);
    void close_notes(ui::Feedback &feedback);
    void layout_notes();
    void scroll_notes(float by, ui::Feedback &feedback);
    float notes_max_scroll() const;
    void draw_notes(ui::Canvas &canvas, float height) const;
    void draw_buttons(ui::Canvas &canvas, const char *const *labels, int count, float top,
                      Color accent) const;
    bool has_notes() const
    {
        return !update_.notes.empty();
    }
    float motion() const;

    Session &session_;
    radio_update_t update_;
    radio_update_progress_t progress_;
    Stage stage_ = Stage::offer;
    bool open_ = false;
    bool quit_ = false;
    int choice_ = 0;
    tween::Spring shown_;    // 0 closed .. 1 open
    tween::Spring choice_x_; // the focus plate among the buttons
    tween::Spring height_;   // shorter while it works, taller with the notes
    tween::Spring fraction_; // the ring
    float stage_time_ = 0.0f;
    float spin_ = 0.0f;
    float press_ = 0.0f;

    // ---- the notes view ----
    std::vector<NoteLine> notes_lines_;
    std::vector<NoteBox> notes_boxes_;
    float notes_height_ = 0.0f;
    float notes_target_ = 0.0f;
    tween::Spring notes_scroll_;
    tween::Spring notes_bounce_; // the give at either end
};

} // namespace radio
