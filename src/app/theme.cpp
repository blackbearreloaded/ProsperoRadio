// ProsperoRadio - Night Signal: the app's design language as kit tokens.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/theme.hpp"

namespace radio
{

ui::Theme night_signal()
{
    // The kit's Acrylic theme is the recipe (glass surfaces, hairlines of
    // light); the palette, the corners and the pace are this app's own.
    ui::Theme t = ui::themes()[0];
    t.id = "night-signal";
    t.name = "Night Signal";
    t.family = "Frosted glass";
    t.summary = "Glass panels over a slow aurora, one teal accent";

    t.backdrop = night_sky(tone::teal, Color::rgb(0x2a4296), 0.0f, 0.0f);
    t.page = Color::rgb(0x08131a);
    t.surface = Color::rgb(0x12222c, 0.5f);
    t.surface_high = Color::rgb(0xffffff, 0.11f);
    t.text = Color::rgb(0xf1f5f7);
    t.text_muted = Color::rgb(0xf1f5f7, 0.64f);
    t.primary = tone::teal;
    t.on_primary = Color::rgb(0x04222a);
    t.secondary = Color::rgb(0xffffff, 0.1f);
    t.on_secondary = Color::rgb(0xf1f5f7);
    t.accent = tone::teal;
    t.outline = Color::rgb(0xffffff, 0.18f);
    t.focus = Color::rgb(0xffffff);
    t.shadow = Color::rgb(0x000000, 0.45f);
    t.light = Color::rgb(0xffffff, 0.22f);
    t.danger = Color::rgb(0xf16f72);
    t.success = tone::live;
    t.warning = Color::rgb(0xf0bd61);

    t.style = ui::SurfaceStyle::glass;
    t.corner = ui::Corner::round;
    t.radius = 16.0f;
    t.radius_card = 24.0f;
    t.border = 1.5f;
    t.pill_chips = true;
    t.shadow_offset = 12.0f;
    t.shadow_blur = 34.0f;
    t.focus_width = 3.0f;
    t.focus_gap = 5.0f;

    t.heading = ui::FontRole::display;
    t.label = ui::FontRole::semibold;
    t.omega = 18.0f;
    t.damping = 1.0f;
    t.sounds = audio::SoundSet::glass;
    t.dark = true;
    return t;
}

gfx::BackdropSpec night_sky(Color lean, Color lean_dark, float amount, float time)
{
    gfx::BackdropSpec sky;
    sky.mode = gfx::BackdropMode::aurora;
    sky.colors[0] = tone::night;
    sky.colors[1] = Color::rgb(0x081a24);
    // The clouds stay dark: they are a mood behind text, not a picture.
    sky.colors[2] = gfx::mix(Color::rgb(0x14666e), gfx::mix(lean, tone::night, 0.45f), amount);
    sky.colors[3] = gfx::mix(Color::rgb(0x263c8a), gfx::mix(lean_dark, lean, 0.25f), amount);
    sky.time = time;
    return sky;
}

} // namespace radio
