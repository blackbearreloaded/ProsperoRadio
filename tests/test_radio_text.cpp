/*
 * PSRadio - Host tests for deterministic metadata text layout.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "radio_text.hpp"

#include <gtest/gtest.h>

TEST(RadioText, LeavesLeftToRightScriptsUnchanged)
{
    EXPECT_EQ(RadioVisualText("KEXP 90.3 FM"), "KEXP 90.3 FM");
    EXPECT_EQ(RadioVisualText("Радио Москва"), "Радио Москва");
    EXPECT_EQ(RadioVisualText("中国广播"), "中国广播");
}

TEST(RadioText, ReordersAndShapesRightToLeftMetadata)
{
    EXPECT_EQ(RadioVisualText("שלום 24"), "24 םולש");
    EXPECT_EQ(RadioVisualText("سلام"), "\xef\xbb\xa1\xef\xbb\xbc\xef\xba\xb3");
    EXPECT_NE(RadioVisualText("العربية"), "العربية");
}
