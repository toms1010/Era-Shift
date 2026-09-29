// Era Shift - built-in 5x7 bitmap font.
//
// The engine ships its own font so the title screen, HUD and debug overlay can
// render on any machine regardless of what fonts are installed. Glyphs are 5
// pixels wide and 7 tall, stored column-major in a byte per column with bit 0
// at the top, which is the classic 5x7 console layout.
//
// TTF fonts remain available through ResourceManager for the final UI; this
// font is the always-available fallback and the debug layer's font.
//
// Readability notes (these drove the current design):
//   * Advances are proportional. A monospaced grid puts a huge gap around "I"
//     and around punctuation, which is what makes cheap bitmap fonts look
//     amateurish. Each glyph advances by its own ink width plus the spacing.
//   * Text can be emboldened by dilating it one pixel to the right, which is
//     what makes a title read at a distance without needing a real font.
//   * Text can be drawn with a one-pixel drop shadow so it stays legible over
//     a busy background.

#pragma once

#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Math.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"

#include <cstdint>
#include <string_view>

namespace EraShift::Graphics {

/// Default gap between glyphs, in font pixels.
inline constexpr int BitmapFontSpacing = 1;

/// Horizontal rendering options for a run of text.
///
/// Declared at namespace scope rather than nested in BitmapFont so it can be a
/// default argument: a default member initialiser is not yet usable inside the
/// class that encloses it.
struct FontStyle {
    int  scale   = 1;             ///< Integer pixel multiplier. 1 is native size.
    bool bold    = false;         ///< Dilate one pixel to the right.
    int  spacing = BitmapFontSpacing; ///< Gap between glyphs, in font pixels.
};

class BitmapFont {
public:
    static constexpr int kGlyphWidth  = 5;
    static constexpr int kGlyphHeight = 7;

    /// Default gap between glyphs, in font pixels.
    static constexpr int kSpacing = BitmapFontSpacing;
    /// Recommended distance between baselines, in font pixels. Gives text a
    /// little leading so multi-line blocks do not look cramped.
    static constexpr int kLineHeight = kGlyphHeight + 2;
    /// Advance of a space character, in font pixels.
    static constexpr int kSpaceAdvance = 3;

    /// First and one-past-last character this font covers.
    static constexpr char kFirstChar = ' ';
    static constexpr char kLastChar  = '~';

    /// The style type used by this font.
    using Style = FontStyle;

    /// Column bitmaps for a character. Unknown characters render as a filled
    /// box so a missing glyph is obvious rather than invisible.
    [[nodiscard]] static const std::uint8_t* glyph(char c) noexcept;

    /// Index of the rightmost column containing ink, or -1 for a blank glyph
    /// such as the space. Used to derive proportional advances.
    [[nodiscard]] static int rightmostInkColumn(char c) noexcept;

    /// How far the pen moves after drawing `c`, in font pixels.
    [[nodiscard]] static int advance(char c, const Style& style = Style{}) noexcept;

    /// Width in pixels of `text` under `style`.
    [[nodiscard]] static int measureWidth(std::string_view text, int scale = 1) noexcept;
    [[nodiscard]] static int measureWidth(std::string_view text, const Style& style) noexcept;
    [[nodiscard]] static int measureHeight(int scale = 1) noexcept { return kGlyphHeight * scale; }

    /// Draws text with its top-left corner at `position`, in screen space.
    static void draw(Renderer2D& renderer, const Vec2& position, std::string_view text,
                     Color color, int scale = 1);
    static void draw(Renderer2D& renderer, const Vec2& position, std::string_view text,
                     Color color, const Style& style);

    /// Draws text centred horizontally on `centerX`, with the vertical centre at
    /// `centerY`. Used for titles.
    static void drawCentered(Renderer2D& renderer, float centerX, float centerY, std::string_view text,
                             Color color, int scale = 1);
    static void drawCentered(Renderer2D& renderer, float centerX, float centerY, std::string_view text,
                             Color color, const Style& style);

    /// Draws text centred both horizontally and vertically.
    static void drawCenteredIn(Renderer2D& renderer, const Rect& area, std::string_view text,
                               Color color, int scale = 1);

    /// Draws text right-aligned so its right edge sits at `rightX`.
    static void drawRight(Renderer2D& renderer, float rightX, float y, std::string_view text,
                          Color color, int scale = 1);

    /// Draws text with a one-pixel drop shadow, which keeps it readable over
    /// bright backgrounds without needing a font renderer.
    static void drawShadowed(Renderer2D& renderer, const Vec2& position, std::string_view text,
                             Color color, Color shadow, int scale = 1);
    static void drawShadowed(Renderer2D& renderer, const Vec2& position, std::string_view text,
                             Color color, Color shadow, const Style& style);

private:
    /// Draws one glyph. Horizontal runs are merged *within each row*: merging
    /// across rows would fill the counters of letters such as E, A and I and
    /// render them as solid blocks.
    static void drawGlyph(Renderer2D& renderer, const Vec2& position, char c, Color color,
                          const Style& style);
};

} // namespace EraShift::Graphics
