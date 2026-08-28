/*
 * PSRadio - Host tests for native import-stub compatibility.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "elf_object.hpp"

#include <gtest/gtest.h>

#include <algorithm>

TEST(Ps5ImportStub, UsesThePs5ImportSymbolTableAsExports)
{
    const auto bytes = ps5::elf::read_file("vendor/ps5/sdk/stubs/libSceOpusDec_stub.a");
    const ps5::elf::Stub stub = ps5::elf::read_stub(bytes, "libSceOpusDec_stub.a");

    EXPECT_EQ(stub.soname, "libSceOpusDec.prx");
    EXPECT_EQ(stub.module_name, "libSceOpusDec");
    EXPECT_NE(std::find(stub.exports.begin(), stub.exports.end(), "sceOpusDecDecode"),
              stub.exports.end());
}

TEST(Ps5ImportStub, SupportsTheOpusCeltDecoderFallback)
{
    const auto bytes = ps5::elf::read_file("vendor/ps5/sdk/stubs/libSceOpusCeltDec_stub.a");
    const ps5::elf::Stub stub = ps5::elf::read_stub(bytes, "libSceOpusCeltDec_stub.a");

    EXPECT_EQ(stub.soname, "libSceOpusCeltDec.prx");
    EXPECT_EQ(stub.module_name, "libSceOpusCeltDec");
    EXPECT_NE(std::find(stub.exports.begin(), stub.exports.end(), "sceOpusCeltDecGetSize"),
              stub.exports.end());
}
