// ProsperoRadio - A–Z and Favorites: an alphabetical list with a letter rail.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/held_step.hpp"
#include "app/platform.hpp"
#include "app/session.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/jump_bar.hpp"
#include "ui/components/stat.hpp"
#include "ui/glyphs.hpp"

namespace radio
{

// The stations by name, one per row, after ProsperoPuzzles' library: the
// focused station's details on the right, the letters on a rail at the
// edge. L2 and R2 jump to the previous and next letter (held, they go on),
// right takes the rail, where up and down move through the letters.
class LetterScreen
{
  public:
    enum class Result : std::uint8_t
    {
        none,
        open_player,    // confirm on the station that is already playing
        browse_popular, // the empty Favorites list sends the player there
    };

    explicit LetterScreen(Session &session);

    // The screen came (back) into view, or its list changed.
    void enter();
    void reset();

    Result handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;
    int hints(ui::Hint *out, int capacity) const;

  private:
    void count_letters();
    // Where a bucket of the rail starts in the list; -1 when it is empty.
    int first_of(int bucket) const;
    int bucket_at(int index) const;
    void jump_to_bucket(int bucket);
    void draw_details(ui::Canvas &canvas, const radio_station_t &station, unsigned index) const;

    Session &session_;
    ui::GridView list_; // one column: a virtual list of any length
    ui::JumpBar rail_;
    ui::EmptyState empty_;
    HeldStep held_;
    unsigned counts_[kInitialBuckets] = {};
    unsigned offsets_[kInitialBuckets + 1] = {};
    bool counted_ = false;
    float retry_ = 0.0f;
    unsigned counted_total_ = 0;
    unsigned counted_revision_ = 0;
    bool on_rail_ = false;
    float age_ = 0.0f;
    ui::Pulse star_;
};

} // namespace radio
