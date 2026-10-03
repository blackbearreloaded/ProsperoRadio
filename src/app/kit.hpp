// ProsperoRadio - Short names for the interface kit the screens are built on.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/input.hpp"
#include "core/tween.hpp"
#include "gfx/backdrop_spec.hpp"
#include "gfx/draw_list.hpp"
#include "ui/components/component.hpp"
#include "ui/feedback.hpp"
#include "ui/fonts.hpp"
#include "ui/motion.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <cstdint>

namespace radio
{

namespace gfx = hui::gfx;
namespace ui = hui::ui;
namespace tween = hui::tween;
namespace audio = hui::audio;

using gfx::Color;
using gfx::Rect;
using hui::Action;
using hui::Direction;
using hui::InputFrame;

inline constexpr Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color kClear{0.0f, 0.0f, 0.0f, 0.0f};

// The screen's safe area: nothing readable sits closer to the sides.
inline constexpr float kMargin = 96.0f;

// One frame, back to front: backdrop -> scene -> [glass copy] -> overlay.
// Night Signal is a glass theme, so the interface is drawn in `overlay` and
// its panels frost whatever `scene` holds.
struct Frame
{
    gfx::BackdropSpec backdrop;
    gfx::DrawList scene;
    gfx::DrawList overlay;
    std::uint32_t glass_texture = 0;

    void reset()
    {
        backdrop = {};
        scene.clear();
        overlay.clear();
    }
};

// Text centred vertically on cy sits on this baseline.
inline float baseline_for(float cy, float size)
{
    return cy + 0.35f * size;
}

} // namespace radio
