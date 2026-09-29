#include "EraShift/Graphics/BitmapFont.hpp"

#include <array>
#include <cctype>

namespace EraShift::Graphics {
namespace {

// ---------------------------------------------------------------------------
// Glyph art
//
// Each entry is 35 characters: 7 rows of 5 columns, '#' = ink, '.' = paper.
// Entries are ordered by ASCII code from ' ' (32) to '~' (126). Lowercase
// letters reuse the uppercase art, which is the conventional behaviour for a
// console-style font and keeps the table small.
// ---------------------------------------------------------------------------
/// A glyph of pure paper, used as the marker for "reuse the uppercase art".
constexpr std::string_view kBlankArt =
    ".................................";

constexpr std::size_t kFirstGlyph = 32;
constexpr std::size_t kGlyphCount  = 95;

constexpr std::array<std::string_view, kGlyphCount> kGlyphArt = {
    /* ' '  */ "...................................",
    /* !    */ "..#....#....#....#....#.........#..",
    /* "    */ ".#.#..#.#..........................",
    /* #    */ ".#.#..#.#.#####.#.#.#####.#.#..#.#.",
    /* $    */ "..#...#####.#...###...#.#####...#..",
    /* %    */ "##..###..#...#...#...#...#..###..##",
    /* &    */ ".##..#..#.#.#...#...#.#.##..#..##.#",
    /* '    */ "..#....#...........................",
    /* (    */ "...#...#...#....#....#.....#.....#.",
    /* )    */ ".#.....#.....#....#....#...#...#...",
    /* *    */ ".....#.#.#.###.#####.###.#.#.#.....",
    /* +    */ ".......#....#..#####..#....#.......",
    /* ,    */ "...........................#...#...",
    /* -    */ "...............#####...............",
    /* .    */ "...........................#....#..",
    /* /    */ "....#...#...#...#...#....#........#",
    /* 0    */ ".###.#...##..###.#.###..##...#.###.",
    /* 1    */ "..#...##....#....#....#....#...###.",
    /* 2    */ ".###.#...#....#...#...#...#...#####",
    /* 3    */ "#####...#...#.....#.....##...#.###.",
    /* 4    */ "...#...##..#.#.#..#.#####...#....#.",
    /* 5    */ "######....####.....#....##...#.###.",
    /* 6    */ "..##..#...#....####.#...##...#.###.",
    /* 7    */ "#####....#...#...#...#....#....#...",
    /* 8    */ ".###.#...##...#.###.#...##...#.###.",
    /* 9    */ ".###.#...##...#.####....#...#..##..",
    /* :    */ ".......#....#.........#....#.......",
    /* ;    */ ".......#....#.........#...#...#....",
    /* <    */ "...#...#...#...#.....#.....#.....#.",
    /* =    */ "..........#####.....#####..........",
    /* >    */ ".#.....#.....#.....#...#...#...#...",
    /* ?    */ ".###.#...#....#...#...#.........#..",
    /* @    */ ".###.#...##.####.#.##.####.....###.",
    /* A    */ "..#...#.#.#...##...#######...##...#",
    /* B    */ "####.#...##...#####.#...##...#####.",
    /* C    */ ".###.#...##....#....#....#...#.###.",
    /* D    */ "###..#..#.#...##...##...##..#.###..",
    /* E    */ "######....#....####.#....#....#####",
    /* F    */ "######....#....####.#....#....#....",
    /* G    */ ".###.#...##....#.####...##...#.###.",
    /* H    */ "#...##...##...#######...##...##...#",
    /* I    */ ".###...#....#....#....#....#...###.",
    /* J    */ "..###...#....#....#....#.#..#..##..",
    /* K    */ "#...##..#.#.#..##...#.#..#..#.#...#",
    /* L    */ "#....#....#....#....#....#....#####",
    /* M    */ "#...###.###.#.##.#.##...##...##...#",
    /* N    */ "#...###..###..##.#.##..###..###...#",
    /* O    */ ".###.#...##...##...##...##...#.###.",
    /* P    */ "####.#...##...#####.#....#....#....",
    /* Q    */ ".###.#...##...##...##.#.##..#..##.#",
    /* R    */ "####.#...##...#####.#.#..#..#.#...#",
    /* S    */ ".#####....#.....###.....#....#####.",
    /* T    */ "#####..#....#....#....#....#....#..",
    /* U    */ "#...##...##...##...##...##...#.###.",
    /* V    */ "#...##...##...##...##...#.#.#...#..",
    /* W    */ "#...##...##...##.#.##.#.###.###...#",
    /* X    */ "#...##...#.#.#...#...#.#.#...##...#",
    /* Y    */ "#...##...#.#.#...#....#....#....#..",
    /* Z    */ "#####....#...#...#...#...#....#####",
    /* [    */ ".###..#....#....#....#....#....###.",
    /* \\   */ "#....#.....#.....#....#.....#.....#",
    /* ]    */ ".###....#....#....#....#....#..###.",
    /* ^    */ "..#...#.#.#...#....................",
    /* _    */ "..............................#####",
    /* `    */ ".#.....#...........................",
    /* a    */ "..#...#.#.#...##...#######...##...#",
    /* b    */ "####.#...##...#####.#...##...#####.",
    /* c    */ ".###.#...##....#....#....#...#.###.",
    /* d    */ "###..#..#.#...##...##...##..#.###..",
    /* e    */ "######....#....####.#....#....#####",
    /* f    */ "######....#....####.#....#....#....",
    /* g    */ ".###.#...##....#.####...##...#.###.",
    /* h    */ "#...##...##...#######...##...##...#",
    /* i    */ ".###...#....#....#....#....#...###.",
    /* j    */ "..###...#....#....#....#.#..#..##..",
    /* k    */ "#...##..#.#.#..##...#.#..#..#.#...#",
    /* l    */ "#....#....#....#....#....#....#####",
    /* m    */ "#...###.###.#.##.#.##...##...##...#",
    /* n    */ "#...###..###..##.#.##..###..###...#",
    /* o    */ ".###.#...##...##...##...##...#.###.",
    /* p    */ "####.#...##...#####.#....#....#....",
    /* q    */ ".###.#...##...##...##.#.##..#..##.#",
    /* r    */ "####.#...##...#####.#.#..#..#.#...#",
    /* s    */ ".#####....#.....###.....#....#####.",
    /* t    */ "#####..#....#....#....#....#....#..",
    /* u    */ "#...##...##...##...##...##...#.###.",
    /* v    */ "#...##...##...##...##...#.#.#...#..",
    /* w    */ "#...##...##...##.#.##.#.###.###...#",
    /* x    */ "#...##...#.#.#...#...#.#.#...##...#",
    /* y    */ "#...##...#.#.#...#....#....#....#..",
    /* z    */ "#####....#...#...#...#...#....#####",
    /* {    */ "...#...#....#...#.....#....#.....#.",
    /* |    */ "..#....#....#....#....#....#....#..",
    /* }    */ ".#.....#....#.....#...#....#...#...",
    /* ~    */ "...........#..##.#.##..#...........",
};

/// Rightmost column with any ink set, or -1 when the glyph is blank.
constexpr std::int8_t rightmostInk(const std::array<std::uint8_t, 5>& columns) noexcept
{
    for (int col = 4; col >= 0; --col) {
        if (columns[static_cast<std::size_t>(col)] != 0) {
            return static_cast<std::int8_t>(col);
        }
    }
    return -1;
}

/// Fills the 5 column bitmaps from one 35-character art string.
constexpr std::array<std::uint8_t, BitmapFont::kGlyphWidth> compileGlyph(std::string_view art) noexcept
{
    std::array<std::uint8_t, BitmapFont::kGlyphWidth> columns{};

    for (int row = 0; row < BitmapFont::kGlyphHeight; ++row) {
        for (int col = 0; col < BitmapFont::kGlyphWidth; ++col) {
            const std::size_t index = static_cast<std::size_t>(row) *
                                          static_cast<std::size_t>(BitmapFont::kGlyphWidth) +
                                      static_cast<std::size_t>(col);
            if (index >= art.size()) {
                continue;
            }
            if (art[index] == '#') {
                columns[static_cast<std::size_t>(col)] |= static_cast<std::uint8_t>(1u << row);
            }
        }
    }
    return columns;
}

using GlyphBitmap = std::array<std::uint8_t, BitmapFont::kGlyphWidth>;

/// Full table, resolved at compile time. `{}` entries mean "not present" and
/// are filled in at runtime lookup by falling back to the uppercase art.
struct GlyphTables {
    std::array<GlyphBitmap, kGlyphCount> upper{};
    std::array<GlyphBitmap, kGlyphCount> lower{};
    /// Rightmost column holding ink, or -1 when the glyph is blank. Drives the
    /// proportional advance so "I" and punctuation do not get a full-cell gap.
    std::array<std::int8_t, kGlyphCount> rightInk{};

    constexpr GlyphTables() noexcept
    {
        for (std::size_t i = 0; i < kGlyphCount; ++i) {
            if (kGlyphArt[i].empty()) {
                continue;
            }
            // A row of dots means "same as the uppercase glyph", which is how
            // lowercase is implemented.
            const bool isPlaceholder = (kGlyphArt[i] == kBlankArt);
            if (isPlaceholder && i >= 65) {
                lower[i] = upper[i - 32];
                rightInk[i] = rightInk[i - 32];
            } else {
                const GlyphBitmap bitmap = compileGlyph(kGlyphArt[i]);
                upper[i] = bitmap;
                lower[i] = bitmap;
                rightInk[i] = rightmostInk(bitmap);
            }
        }
    }
};

constexpr GlyphTables kGlyphs{};

/// '?' shape, used for anything outside the printable ASCII range.
constexpr GlyphBitmap kFallbackGlyph = compileGlyph(
    ".###."
    "#...#"
    "....#"
    "...#."
    "..#.."
    "....."
    "..#..");

} // namespace

const std::uint8_t* BitmapFont::glyph(char c) noexcept
{
    const auto code = static_cast<unsigned char>(c);
    if (code < kFirstGlyph || code >= kFirstGlyph + kGlyphCount) {
        return kFallbackGlyph.data();
    }
    const std::size_t index = code - kFirstGlyph;
    if (kGlyphArt[index].empty()) {
        return kFallbackGlyph.data();
    }
    return kGlyphs.lower[index].data();
}

int BitmapFont::rightmostInkColumn(char c) noexcept
{
    const auto code = static_cast<unsigned char>(c);
    if (code < kFirstGlyph || code >= kFirstGlyph + kGlyphCount) {
        return 4;
    }
    return kGlyphs.rightInk[code - kFirstGlyph];
}

int BitmapFont::advance(char c, const Style& style) noexcept
{
    if (style.scale <= 0) {
        return 0;
    }
    // A space gets a fixed, slightly generous cell; a blank glyph reached by
    // any other route falls back to the space advance too.
    const int right = rightmostInkColumn(c);
    if (right < 0) {
        return kSpaceAdvance * style.scale;
    }
    const int fontPixels = right + 1 + style.spacing + (style.bold ? 1 : 0);
    return fontPixels * style.scale;
}

int BitmapFont::measureWidth(std::string_view text, int scale) noexcept
{
    Style style;
    style.scale = scale;
    return measureWidth(text, style);
}

int BitmapFont::measureWidth(std::string_view text, const Style& style) noexcept
{
    if (text.empty() || style.scale <= 0) {
        return 0;
    }
    int width = 0;
    for (const char c : text) {
        width += advance(c, style);
    }
    // The trailing spacing after the final glyph is not part of the text.
    return width > 0 ? width - style.spacing * style.scale : 0;
}

void BitmapFont::drawGlyph(Renderer2D& renderer, const Vec2& position, char c, Color color,
                           const Style& style)
{
    if (style.scale <= 0) {
        return;
    }

    std::uint8_t columns[BitmapFont::kGlyphWidth];
    const std::uint8_t* source = glyph(c);
    for (int col = 0; col < kGlyphWidth; ++col) {
        columns[col] = source[col];
    }

    // Bold is a one-pixel horizontal dilation: every column also takes the ink
    // of the column to its left.
    if (style.bold) {
        for (int col = kGlyphWidth - 1; col > 0; --col) {
            columns[col] = static_cast<std::uint8_t>(columns[col] | columns[col - 1]);
        }
    }

    const float scale = static_cast<float>(style.scale);

    // Merge horizontal runs *within each row*. Runs must not be merged across
    // rows: a letter such as E has ink in every column but not in every row,
    // and merging columns globally fills its counters and draws a solid block.
    for (int row = 0; row < kGlyphHeight; ++row) {
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << row);

        int col = 0;
        while (col < kGlyphWidth) {
            if ((columns[col] & bit) == 0) {
                ++col;
                continue;
            }

            int run = 1;
            while (col + run < kGlyphWidth && (columns[col + run] & bit) != 0) {
                ++run;
            }

            const Rect pixel{position.x + static_cast<float>(col) * scale,
                             position.y + static_cast<float>(row) * scale,
                             static_cast<float>(run) * scale,
                             scale};
            renderer.drawRect(pixel, color);
            col += run;
        }
    }
}

void BitmapFont::draw(Renderer2D& renderer, const Vec2& position, std::string_view text,
                      Color color, int scale)
{
    Style style;
    style.scale = scale;
    draw(renderer, position, text, color, style);
}

void BitmapFont::draw(Renderer2D& renderer, const Vec2& position, std::string_view text,
                      Color color, const Style& style)
{
    if (text.empty() || style.scale <= 0) {
        return;
    }

    float x = position.x;
    for (const char c : text) {
        if (c == '\n') {
            x = position.x;
            continue;
        }
        drawGlyph(renderer, Vec2{x, position.y}, c, color, style);
        x += static_cast<float>(advance(c, style));
    }
}

void BitmapFont::drawCentered(Renderer2D& renderer, float centerX, float centerY, std::string_view text,
                              Color color, int scale)
{
    Style style;
    style.scale = scale;
    drawCentered(renderer, centerX, centerY, text, color, style);
}

void BitmapFont::drawCentered(Renderer2D& renderer, float centerX, float centerY, std::string_view text,
                              Color color, const Style& style)
{
    const float width  = static_cast<float>(measureWidth(text, style));
    const float height = static_cast<float>(measureHeight(style.scale));
    draw(renderer, Vec2{centerX - width * 0.5f, centerY - height * 0.5f}, text, color, style);
}

void BitmapFont::drawCenteredIn(Renderer2D& renderer, const Rect& area, std::string_view text,
                                Color color, int scale)
{
    drawCentered(renderer, area.center().x, area.center().y, text, color, scale);
}

void BitmapFont::drawRight(Renderer2D& renderer, float rightX, float y, std::string_view text,
                           Color color, int scale)
{
    const float width = static_cast<float>(measureWidth(text, scale));
    draw(renderer, Vec2{rightX - width, y}, text, color, scale);
}

void BitmapFont::drawShadowed(Renderer2D& renderer, const Vec2& position, std::string_view text,
                              Color color, Color shadow, int scale)
{
    Style style;
    style.scale = scale;
    drawShadowed(renderer, position, text, color, shadow, style);
}

void BitmapFont::drawShadowed(Renderer2D& renderer, const Vec2& position, std::string_view text,
                              Color color, Color shadow, const Style& style)
{
    // The offset scales with the text so a large title gets a proportionally
    // thicker, more legible shadow.
    const float offset = static_cast<float>(std::max(style.scale, 1));
    draw(renderer, position + Vec2{offset, offset}, text, shadow, style);
    draw(renderer, position, text, color, style);
}

} // namespace EraShift::Graphics
