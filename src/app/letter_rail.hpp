// ProsperoRadio - The A–Z rail beside every list of stations.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/catalog.hpp"
#include "app/kit.hpp"
#include "app/platform.hpp"
#include "ui/components/jump_bar.hpp"

namespace radio
{

// Which mark of the rail a name is under, the way the catalogue sorts names
// (letters compared as lower case, leading spaces ignored): 0 for digits and
// signs before A, 1..26 for A to Z, 27 for what comes after Z.
int letter_bucket(const char *name);

// The kit's jump bar with the marks # A–Z …, for a list in any order: a mark
// takes the focus to the first station of the list whose name starts there
// (in a ranked list, the best-ranked one). Marks with no station are dimmed
// and stepped over. The screen decides when the rail has the focus.
class LetterRail
{
  public:
    explicit LetterRail(const ui::Theme &theme);

    void set_bounds(const Rect &bounds);
    // Reads where each mark starts again when the list changed (another
    // list, a sync, a favourite). Call once a frame.
    void track(const Catalog &list, unsigned revision, float dt);
    // The mark of the station in focus, while the list has the focus.
    void follow(const radio_station_t *station);
    bool ready() const
    {
        return known_;
    }

    bool focused() const
    {
        return focused_;
    }
    void take_focus(ui::Feedback &feedback);
    void give_back();
    // While focused: the index the list should show (-1 for none); *leave
    // says the rail handed the focus back (left, confirm or back).
    int handle(const InputFrame &input, ui::Feedback &feedback, bool *leave);

    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    ui::JumpBar bar_;
    unsigned starts_[kInitialBuckets] = {};
    bool known_ = false;
    bool focused_ = false;
    ListSpec spec_{};
    unsigned revision_ = 0;
    unsigned total_ = 0;
    float retry_ = 0.0f;
};

} // namespace radio
