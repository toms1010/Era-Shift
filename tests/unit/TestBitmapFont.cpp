// Era Shift - tests for the built-in bitmap font.
//
// A broken glyph table would silently produce unreadable text on every machine
// that has no TTF font, so the art is validated here rather than discovered by
// looking at a screenshot.

#include "EraShift/Graphics/BitmapFont.hpp"

#include <doctest/doctest.h>

#include <set>
#include <string>

using namespace EraShift::Graphics;

namespace {

/// Renders a character as ASCII art so a failure is readable in the test log.
std::string renderGlyph(char c)
{
    const std::uint8_t* columns = BitmapFont::glyph(c);
    std::string result;
    for (int row = 0; row < BitmapFont::kGlyphHeight; ++row) {
        for (int col = 0; col < BitmapFont::kGlyphWidth; ++col) {
            result.push_back((columns[col] & (1u << row)) ? '#' : '.');
        }
        result.push_back('\n');
    }
    return result;
}

} // namespace

TEST_CASE("every printable ASCII character has a glyph")
{
    for (int code = 32; code <= 126; ++code) {
        const char c = static_cast<char>(code);
        const std::uint8_t* columns = BitmapFont::glyph(c);
        REQUIRE(columns != nullptr);
    }
}

TEST_CASE("a space is blank and a visible letter is not")
{
    SUBCASE("space has no ink")
    {
        const std::uint8_t* columns = BitmapFont::glyph(' ');
        for (int col = 0; col < BitmapFont::kGlyphWidth; ++col) {
            CHECK(columns[col] == 0);
        }
    }

    SUBCASE("letters and digits have ink")
    {
        const std::set<char> visible = {'A', 'Z', 'a', 'z', '0', '9', '?', '!'};
        for (const char c : visible) {
            const std::uint8_t* columns = BitmapFont::glyph(c);
            int ink = 0;
            for (int col = 0; col < BitmapFont::kGlyphWidth; ++col) {
                for (int bit = 0; bit < BitmapFont::kGlyphHeight; ++bit) {
                    ink += (columns[col] & (1u << bit)) ? 1 : 0;
                }
            }
            CHECK_MESSAGE(ink > 4, "glyph '", c, "' is nearly empty:\n", renderGlyph(c));
        }
    }
}

TEST_CASE("lowercase reuses the uppercase art")
{
    for (char upper = 'A'; upper <= 'Z'; ++upper) {
        const char lower = static_cast<char>(upper - 'A' + 'a');
        const std::uint8_t* a = BitmapFont::glyph(upper);
        const std::uint8_t* b = BitmapFont::glyph(lower);
        for (int col = 0; col < BitmapFont::kGlyphWidth; ++col) {
            CHECK(a[col] == b[col]);
        }
    }
}

TEST_CASE("out-of-range characters fall back to a visible box")
{
    for (const char c : {'\n', '\t', static_cast<char>(0x01), static_cast<char>(200)}) {
        const std::uint8_t* columns = BitmapFont::glyph(c);
        REQUIRE(columns != nullptr);
        int ink = 0;
        for (int col = 0; col < BitmapFont::kGlyphWidth; ++col) {
            for (int bit = 0; bit < BitmapFont::kGlyphHeight; ++bit) {
                ink += (columns[col] & (1u << bit)) ? 1 : 0;
            }
        }
        CHECK_MESSAGE(ink > 0, "fallback glyph must not be blank");
    }
}

TEST_CASE("measurement matches the per-glyph advances")
{
    // Advances are proportional: each glyph contributes its own ink width plus
    // the spacing, so the total is the sum of the advances minus the trailing
    // gap after the final glyph.
    const int a = BitmapFont::advance('A');
    const int b = BitmapFont::advance('B');
    const int c = BitmapFont::advance('C');

    // The gap after the final glyph is not part of the text, so a single
    // glyph measures as its advance minus that trailing spacing. Without this
    // rule, centring text would be off by half a space.
    CHECK(BitmapFont::measureWidth("", 1) == 0);
    CHECK(BitmapFont::measureWidth("A", 1) == a - BitmapFont::kSpacing);
    CHECK(BitmapFont::measureWidth("ABC", 1) == a + b + c - BitmapFont::kSpacing);
    CHECK(BitmapFont::measureWidth("ABC", 3) == 3 * (a + b + c - BitmapFont::kSpacing));
    CHECK(BitmapFont::measureWidth("ABC", 0) == 0);
    CHECK(BitmapFont::measureHeight(2) == 2 * BitmapFont::kGlyphHeight);
}

TEST_CASE("narrow glyphs advance less than wide ones")
{
    // The whole point of proportional advances: "I" must not reserve a full
    // cell, otherwise every word containing an I looks gappy.
    CHECK(BitmapFont::advance('I') < BitmapFont::advance('M'));
    CHECK(BitmapFont::advance('.') < BitmapFont::advance('M'));
    CHECK(BitmapFont::advance(' ') < BitmapFont::advance('M'));
    CHECK(BitmapFont::advance(' ') == BitmapFont::kSpaceAdvance);
}

TEST_CASE("rightmost ink column drives the advance")
{
    CHECK(BitmapFont::rightmostInkColumn(' ') == -1);
    // 'I' is drawn as ".###." so its ink stops at column 3.
    CHECK(BitmapFont::rightmostInkColumn('I') == 3);
    // 'M' fills all five columns.
    CHECK(BitmapFont::rightmostInkColumn('M') == 4);
}

TEST_CASE("spacing and bold change the measured width")
{
    FontStyle tight;
    tight.spacing = 0;
    FontStyle loose;
    loose.spacing = 3;
    CHECK(BitmapFont::measureWidth("ERA", loose) > BitmapFont::measureWidth("ERA", tight));

    FontStyle bold;
    bold.bold = true;
    CHECK(BitmapFont::measureWidth("ERA", bold) > BitmapFont::measureWidth("ERA", FontStyle{}));
}

TEST_CASE("glyph counters are not filled in")
{
    // Regression test: an earlier rasteriser merged ink runs across rows, which
    // drew E, I and A as solid blocks. Each of these has at least one pixel of
    // background inside its bounding box.
    const struct { char c; int row; } hollows[] = {
        {'E', 1},   // " #   "
        {'A', 1},   // " # # "
        {'B', 1},   // " #   " (the counters on the right are empty)
        {'0', 1},   // "#   #"
        {'O', 1},   // "#   #"
        {'P', 3},   // "     " below the bowl
        {'8', 1},   // "#   #"
    };

    for (const auto& hollow : hollows) {
        const std::uint8_t* columns = BitmapFont::glyph(hollow.c);
        bool anyBackground = false;
        for (int col = 0; col < BitmapFont::kGlyphWidth; ++col) {
            if ((columns[col] & (1u << hollow.row)) == 0) {
                anyBackground = true;
                break;
            }
        }
        CHECK_MESSAGE(anyBackground, "'", hollow.c, "' is solid on row ", hollow.row,
                      ":\n", renderGlyph(hollow.c));
    }
}

TEST_CASE("the letters used by the title screen are legible")
{
    // Each of these must have ink in every row, otherwise the title would show
    // gaps. This is a cheap regression test for a garbled glyph table.
    const std::string title = "ERA SHIFT";
    for (const char c : title) {
        if (c == ' ') {
            continue;
        }
        const std::uint8_t* columns = BitmapFont::glyph(c);
        for (int row = 0; row < BitmapFont::kGlyphHeight; ++row) {
            int inkInRow = 0;
            for (int col = 0; col < BitmapFont::kGlyphWidth; ++col) {
                inkInRow += (columns[col] & (1u << row)) ? 1 : 0;
            }
            CHECK_MESSAGE(inkInRow > 0, "row ", row, " of '", c,
                          "' is empty:\n", renderGlyph(c));
        }
    }
}
