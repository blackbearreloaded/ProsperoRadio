// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "radio_text.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace
{

struct ArabicForm
{
    std::uint32_t base;
    std::uint32_t isolated;
    std::uint32_t final;
    std::uint32_t initial;
    std::uint32_t medial;
};

constexpr ArabicForm kArabicForms[] = {
    {0x0621, 0xfe80, 0, 0, 0},
    {0x0622, 0xfe81, 0xfe82, 0, 0},
    {0x0623, 0xfe83, 0xfe84, 0, 0},
    {0x0624, 0xfe85, 0xfe86, 0, 0},
    {0x0625, 0xfe87, 0xfe88, 0, 0},
    {0x0626, 0xfe89, 0xfe8a, 0xfe8b, 0xfe8c},
    {0x0627, 0xfe8d, 0xfe8e, 0, 0},
    {0x0628, 0xfe8f, 0xfe90, 0xfe91, 0xfe92},
    {0x0629, 0xfe93, 0xfe94, 0, 0},
    {0x062a, 0xfe95, 0xfe96, 0xfe97, 0xfe98},
    {0x062b, 0xfe99, 0xfe9a, 0xfe9b, 0xfe9c},
    {0x062c, 0xfe9d, 0xfe9e, 0xfe9f, 0xfea0},
    {0x062d, 0xfea1, 0xfea2, 0xfea3, 0xfea4},
    {0x062e, 0xfea5, 0xfea6, 0xfea7, 0xfea8},
    {0x062f, 0xfea9, 0xfeaa, 0, 0},
    {0x0630, 0xfeab, 0xfeac, 0, 0},
    {0x0631, 0xfead, 0xfeae, 0, 0},
    {0x0632, 0xfeaf, 0xfeb0, 0, 0},
    {0x0633, 0xfeb1, 0xfeb2, 0xfeb3, 0xfeb4},
    {0x0634, 0xfeb5, 0xfeb6, 0xfeb7, 0xfeb8},
    {0x0635, 0xfeb9, 0xfeba, 0xfebb, 0xfebc},
    {0x0636, 0xfebd, 0xfebe, 0xfebf, 0xfec0},
    {0x0637, 0xfec1, 0xfec2, 0xfec3, 0xfec4},
    {0x0638, 0xfec5, 0xfec6, 0xfec7, 0xfec8},
    {0x0639, 0xfec9, 0xfeca, 0xfecb, 0xfecc},
    {0x063a, 0xfecd, 0xfece, 0xfecf, 0xfed0},
    {0x0640, 0x0640, 0x0640, 0x0640, 0x0640},
    {0x0641, 0xfed1, 0xfed2, 0xfed3, 0xfed4},
    {0x0642, 0xfed5, 0xfed6, 0xfed7, 0xfed8},
    {0x0643, 0xfed9, 0xfeda, 0xfedb, 0xfedc},
    {0x0644, 0xfedd, 0xfede, 0xfedf, 0xfee0},
    {0x0645, 0xfee1, 0xfee2, 0xfee3, 0xfee4},
    {0x0646, 0xfee5, 0xfee6, 0xfee7, 0xfee8},
    {0x0647, 0xfee9, 0xfeea, 0xfeeb, 0xfeec},
    {0x0648, 0xfeed, 0xfeee, 0, 0},
    {0x0649, 0xfeef, 0xfef0, 0, 0},
    {0x064a, 0xfef1, 0xfef2, 0xfef3, 0xfef4},
    {0x067e, 0xfb56, 0xfb57, 0xfb58, 0xfb59},
    {0x0686, 0xfb7a, 0xfb7b, 0xfb7c, 0xfb7d},
    {0x0698, 0xfb8a, 0xfb8b, 0, 0},
    {0x06a9, 0xfb8e, 0xfb8f, 0xfb90, 0xfb91},
    {0x06af, 0xfb92, 0xfb93, 0xfb94, 0xfb95},
    {0x06cc, 0xfbfc, 0xfbfd, 0xfbfe, 0xfbff},
};

enum class Direction : unsigned char
{
    Neutral,
    Left,
    Right
};

const ArabicForm *FindArabic(std::uint32_t codepoint)
{
    for (const ArabicForm &form : kArabicForms)
        if (form.base == codepoint)
            return &form;
    return nullptr;
}

bool IsArabicMark(std::uint32_t codepoint)
{
    return (codepoint >= 0x064b && codepoint <= 0x065f) || codepoint == 0x0670;
}

bool IsRtl(std::uint32_t codepoint)
{
    return (codepoint >= 0x0590 && codepoint <= 0x08ff) ||
           (codepoint >= 0xfb1d && codepoint <= 0xfdff) ||
           (codepoint >= 0xfe70 && codepoint <= 0xfeff);
}

Direction Classify(std::uint32_t codepoint)
{
    if (IsRtl(codepoint))
        return Direction::Right;
    if (codepoint <= 0x20 || (codepoint >= 0x2000 && codepoint <= 0x206f) ||
        (codepoint < 0x80 &&
         !((codepoint >= '0' && codepoint <= '9') || (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= 'a' && codepoint <= 'z'))))
        return Direction::Neutral;
    return Direction::Left;
}

std::vector<std::uint32_t> Decode(const char *text)
{
    std::vector<std::uint32_t> result;
    const auto *input = reinterpret_cast<const unsigned char *>(text ? text : "");
    while (*input)
    {
        std::uint32_t value = *input++;
        unsigned continuation = 0;
        if ((value & 0xe0) == 0xc0)
        {
            value &= 0x1f;
            continuation = 1;
        }
        else if ((value & 0xf0) == 0xe0)
        {
            value &= 0x0f;
            continuation = 2;
        }
        else if ((value & 0xf8) == 0xf0)
        {
            value &= 0x07;
            continuation = 3;
        }
        else if (value >= 0x80)
        {
            result.push_back(0xfffd);
            continue;
        }
        bool valid = true;
        for (unsigned index = 0; index < continuation; ++index)
        {
            if ((input[index] & 0xc0) != 0x80)
            {
                valid = false;
                break;
            }
            value = (value << 6) | (input[index] & 0x3f);
        }
        if (!valid)
            result.push_back(0xfffd);
        else
        {
            input += continuation;
            result.push_back(value);
        }
    }
    return result;
}

void Encode(std::string &output, std::uint32_t value)
{
    if (value < 0x80)
        output.push_back(static_cast<char>(value));
    else if (value < 0x800)
    {
        output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
    else if (value < 0x10000)
    {
        output.push_back(static_cast<char>(0xe0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
    else
    {
        output.push_back(static_cast<char>(0xf0 | (value >> 18)));
        output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

int PreviousLetter(const std::vector<std::uint32_t> &text, int at)
{
    for (--at; at >= 0; --at)
        if (!IsArabicMark(text[static_cast<std::size_t>(at)]))
            return at;
    return -1;
}

int NextLetter(const std::vector<std::uint32_t> &text, int at)
{
    for (++at; at < static_cast<int>(text.size()); ++at)
        if (!IsArabicMark(text[static_cast<std::size_t>(at)]))
            return at;
    return -1;
}

std::uint32_t LamAlef(std::uint32_t codepoint, bool connected)
{
    switch (codepoint)
    {
    case 0x0622:
        return connected ? 0xfef6 : 0xfef5;
    case 0x0623:
        return connected ? 0xfef8 : 0xfef7;
    case 0x0625:
        return connected ? 0xfefa : 0xfef9;
    case 0x0627:
        return connected ? 0xfefc : 0xfefb;
    default:
        return 0;
    }
}

void ShapeArabic(std::vector<std::uint32_t> &text)
{
    const std::vector<std::uint32_t> source = text;
    std::vector<std::uint32_t> shaped;
    shaped.reserve(source.size());
    for (int at = 0; at < static_cast<int>(source.size()); ++at)
    {
        const ArabicForm *current = FindArabic(source[static_cast<std::size_t>(at)]);
        if (!current)
        {
            shaped.push_back(source[static_cast<std::size_t>(at)]);
            continue;
        }
        const int previous_at = PreviousLetter(source, at);
        const int next_at = NextLetter(source, at);
        const ArabicForm *previous =
            previous_at >= 0 ? FindArabic(source[static_cast<std::size_t>(previous_at)]) : nullptr;
        const ArabicForm *next =
            next_at >= 0 ? FindArabic(source[static_cast<std::size_t>(next_at)]) : nullptr;
        const bool joins_previous = current->final && previous && previous->initial;

        if (current->base == 0x0644 && next_at == at + 1)
        {
            const std::uint32_t ligature =
                LamAlef(source[static_cast<std::size_t>(next_at)], joins_previous);
            if (ligature)
            {
                shaped.push_back(ligature);
                ++at;
                continue;
            }
        }

        const bool joins_next = current->initial && next && next->final;
        if (joins_previous && joins_next && current->medial)
            shaped.push_back(current->medial);
        else if (joins_previous)
            shaped.push_back(current->final);
        else if (joins_next)
            shaped.push_back(current->initial);
        else
            shaped.push_back(current->isolated);
    }
    text = std::move(shaped);
}

std::string ShapeLine(const char *text)
{
    std::vector<std::uint32_t> glyphs = Decode(text);
    bool has_rtl = false;
    Direction base = Direction::Neutral;
    for (std::uint32_t glyph : glyphs)
    {
        const Direction direction = Classify(glyph);
        if (base == Direction::Neutral && direction != Direction::Neutral)
            base = direction;
        if (direction == Direction::Right)
            has_rtl = true;
    }
    if (!has_rtl)
        return text ? text : "";

    ShapeArabic(glyphs);
    std::vector<Direction> directions(glyphs.size());
    Direction previous = Direction::Neutral;
    for (std::size_t index = 0; index < glyphs.size(); ++index)
    {
        directions[index] = Classify(glyphs[index]);
        if (directions[index] == Direction::Neutral)
            directions[index] = previous;
        else
            previous = directions[index];
    }
    Direction next = base;
    for (std::size_t index = directions.size(); index-- > 0;)
    {
        if (directions[index] == Direction::Neutral)
            directions[index] = next;
        else
            next = directions[index];
    }

    struct Run
    {
        std::size_t begin;
        std::size_t end;
        Direction direction;
    };
    std::vector<Run> runs;
    for (std::size_t begin = 0; begin < glyphs.size();)
    {
        std::size_t end = begin + 1;
        while (end < glyphs.size() && directions[end] == directions[begin])
            ++end;
        runs.push_back({begin, end, directions[begin]});
        begin = end;
    }

    std::string output;
    auto append_run = [&](const Run &run)
    {
        if (run.direction == Direction::Right)
        {
            for (std::size_t index = run.end; index-- > run.begin;)
                Encode(output, glyphs[index]);
        }
        else
        {
            for (std::size_t index = run.begin; index < run.end; ++index)
                Encode(output, glyphs[index]);
        }
    };
    if (base == Direction::Right)
    {
        for (std::size_t index = runs.size(); index-- > 0;)
            append_run(runs[index]);
    }
    else
    {
        for (const Run &run : runs)
            append_run(run);
    }
    return output;
}

} // namespace

std::string RadioVisualText(const char *text)
{
    const std::string input = text ? text : "";
    std::string output;
    std::size_t begin = 0;
    while (begin <= input.size())
    {
        const std::size_t end = input.find('\n', begin);
        const std::string line =
            input.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        output += ShapeLine(line.c_str());
        if (end == std::string::npos)
            break;
        output.push_back('\n');
        begin = end + 1;
    }
    return output;
}
