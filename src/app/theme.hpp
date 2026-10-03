// ProsperoRadio - Night Signal: the app's design language as kit tokens.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/kit.hpp"

namespace radio
{

// Colours that mean something in this app and are not part of a ui::Theme.
namespace tone
{
inline const Color teal = Color::rgb(0x6cdbd9);  // the brand accent
inline const Color live = Color::rgb(0x64e6a6);  // "on air"
inline const Color gold = Color::rgb(0xffd166);  // a favourite
inline const Color ink = Color::rgb(0x0b0d16);   // icons on a white button
inline const Color night = Color::rgb(0x050d14); // the page behind everything
} // namespace tone

// Frosted glass over a slow aurora, the brand teal as the one accent.
ui::Theme night_signal();

// The aurora behind the screens, with its two clouds leaning toward `lean`
// (the colour of the station in focus) by `amount` (0..1).
gfx::BackdropSpec night_sky(Color lean, Color lean_dark, float amount, float time);

} // namespace radio
