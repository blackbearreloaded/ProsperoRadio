// ProsperoRadio - The A–Z rail beside every list of stations.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/letter_rail.hpp"

#include <algorithm>
#include <climits>

namespace radio
{

namespace
{

constexpr const char *kMarks[kInitialBuckets] = {
    "#", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "\xE2\x80\xA6"};

} // namespace

int letter_bucket(const char *name)
{
    while (*name == ' ')
        ++name;
    const unsigned char first = static_cast<unsigned char>(*name);
    const int folded = first >= 'A' && first <= 'Z' ? first + 32 : first;
    if (folded < 'a')
        return 0;
    return folded <= 'z' ? 1 + folded - 'a' : 27;
}

LetterRail::LetterRail(const ui::Theme &theme)
{
    bar_.style.theme = theme;
    bar_.style.item_size = 26.0f;
    bar_.style.thickness = 44.0f;
    bar_.style.text_size = 17.0f;
    bar_.style.exits.left = true;
    std::vector<ui::JumpEntry> marks;
    for (int i = 0; i < kInitialBuckets; ++i)
        marks.push_back({kMarks[i], true, i});
    bar_.set_entries(std::move(marks));
    bar_.set_focused(false);
}

void LetterRail::set_bounds(const Rect &bounds)
{
    bar_.set_bounds(bounds);
}

void LetterRail::track(const Catalog &list, unsigned revision, float dt)
{
    retry_ = std::max(0.0f, retry_ - dt);
    if (!list.known() || retry_ > 0.0f)
        return;
    if (known_ && same_spec(spec_, list.spec()) && revision_ == revision && total_ == list.total())
        return;
    spec_ = list.spec();
    revision_ = revision;
    total_ = list.total();
    known_ = list.letter_starts(starts_);
    if (!known_)
    {
        retry_ = 2.0f; // not every frame
        return;
    }
    for (int i = 0; i < kInitialBuckets; ++i)
        bar_.set_enabled(i, starts_[i] != UINT_MAX);
}

void LetterRail::follow(const radio_station_t *station)
{
    if (station != nullptr && !focused_)
        bar_.set_current(letter_bucket(station->name));
}

void LetterRail::take_focus(ui::Feedback &feedback)
{
    focused_ = true;
    bar_.set_focused(true);
    feedback.play(audio::Cue::focus, 1.04f);
}

void LetterRail::give_back()
{
    focused_ = false;
    bar_.set_focused(false);
}

int LetterRail::handle(const InputFrame &input, ui::Feedback &feedback, bool *leave)
{
    *leave = false;
    if (!focused_)
        return -1;
    const ui::Event event = bar_.handle(input, feedback);
    if (event == ui::Event::cancelled || event == ui::Event::activated ||
        bar_.exit() == Direction::left)
    {
        *leave = true;
        return -1;
    }
    if (event == ui::Event::changed && known_)
    {
        const unsigned start = starts_[bar_.current()];
        return start == UINT_MAX ? -1 : static_cast<int>(start);
    }
    return -1;
}

void LetterRail::update(float dt)
{
    bar_.update(dt);
}

void LetterRail::draw(ui::Canvas &canvas) const
{
    if (known_)
        bar_.draw(canvas);
}

} // namespace radio
