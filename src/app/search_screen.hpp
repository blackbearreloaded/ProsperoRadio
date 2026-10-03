// ProsperoRadio - Discover: a search box and filters beside their live results.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/held_step.hpp"
#include "app/letter_rail.hpp"
#include "app/session.hpp"
#include "ui/components/choice.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/search_field.hpp"
#include "ui/components/select.hpp"
#include "ui/components/stat.hpp"
#include "ui/components/tag_select.hpp"
#include "ui/glyphs.hpp"

#include <string>

namespace radio
{

// The Discover tab. The panel on the left narrows the catalogue; the grid on
// the right is the answer, kept up to date after every change, so there is no
// "show results" step. Text comes from the system keyboard.
class SearchScreen
{
  public:
    enum class Result : std::uint8_t
    {
        none,
        open_player,
    };

    explicit SearchScreen(Session &session);

    void enter();
    // The catalogue brought new countries, genres or languages.
    void facets_changed();
    // The list the filters describe; the app shows it while this tab is open.
    ListSpec spec() const;

    Result handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;
    // The open dropdown, if any: drawn after everything else on the screen.
    void draw_popovers(ui::Canvas &canvas) const;
    int hints(ui::Hint *out, int capacity) const;

    // A dropdown is open and takes every input.
    bool modal() const;
    // The station under the focus of the results, for the backdrop.
    const radio_station_t *focused() const;

  private:
    enum class Zone : std::uint8_t
    {
        field,
        country,
        language,
        genre,
        bitrate,
        quick,
        results,
    };

    static void typed(const char *text, void *self);
    void apply();
    void reset_filters(ui::Feedback &feedback);
    void go(Zone zone, ui::Feedback &feedback);
    void sync_quick();
    std::string summary() const;
    Result handle_results(const InputFrame &input, ui::Feedback &feedback);

    Session &session_;
    ui::SearchField field_;
    ui::Select country_;
    ui::Select language_;
    ui::Select genre_;
    ui::ChoicePicker bitrate_;
    ui::TagSelect quick_;
    ui::GridView results_;
    LetterRail rail_;
    HeldStep held_; // L2 / R2
    ui::EmptyState empty_;
    Zone zone_ = Zone::field;
    Zone panel_zone_ = Zone::field; // where the focus returns to from the results
    float age_ = 0.0f;
    // The filters, as the service takes them.
    std::string text_;
    std::string country_code_;
    std::string genre_value_;
    std::string language_value_;
    unsigned bitrate_min_ = 0;
};

} // namespace radio
