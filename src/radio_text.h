#pragma once

#include <string>

// Converts UTF-8 metadata to the visual glyph order expected by RmlUi's
// left-to-right bitmap-font interface. Non-RTL text is returned unchanged.
std::string RadioVisualText(const char* text);
