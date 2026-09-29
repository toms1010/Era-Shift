// Era Shift - colour types.

#pragma once

#include <cstdint>
#include <iosfwd>

namespace EraShift::Graphics {

/// 8-bit-per-channel colour. Stored packed so it can be memcpy'd into vertex
/// data without conversion.
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    constexpr Color() = default;
    constexpr Color(std::uint8_t r_, std::uint8_t g_, std::uint8_t b_, std::uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_)
    {}

    /// Builds a colour from normalised floats in [0,1], clamping out-of-range
    /// values so a runaway particle cannot wrap around to an inverted colour.
    [[nodiscard]] static constexpr Color fromFloats(float r, float g, float b, float a = 1.0f) noexcept
    {
        const auto to8 = [](float v) noexcept {
            if (!(v > 0.0f)) {
                return std::uint8_t{0};   // also catches NaN
            }
            if (v >= 1.0f) {
                return std::uint8_t{255};
            }
            return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
        };
        return Color{to8(r), to8(g), to8(b), to8(a)};
    }

    /// Packs to 0xRRGGBBAA. This is the layout expected by shaders and by the
    /// debug overlay.
    [[nodiscard]] constexpr std::uint32_t packed() const noexcept
    {
        return (static_cast<std::uint32_t>(r) << 24) |
               (static_cast<std::uint32_t>(g) << 16) |
               (static_cast<std::uint32_t>(b) << 8) |
               static_cast<std::uint32_t>(a);
    }

    [[nodiscard]] static constexpr Color fromPacked(std::uint32_t value) noexcept
    {
        return Color{static_cast<std::uint8_t>((value >> 24) & 0xFFu),
                     static_cast<std::uint8_t>((value >> 16) & 0xFFu),
                     static_cast<std::uint8_t>((value >> 8) & 0xFFu),
                     static_cast<std::uint8_t>(value & 0xFFu)};
    }

    [[nodiscard]] constexpr Color withAlpha(std::uint8_t newAlpha) const noexcept
    {
        return Color{r, g, b, newAlpha};
    }

    [[nodiscard]] constexpr Color scaled(float factor) const noexcept
    {
        const auto to8 = [](std::uint8_t v, float f) noexcept {
            const float scaled = static_cast<float>(v) * f;
            if (!(scaled > 0.0f)) {
                return std::uint8_t{0};
            }
            if (scaled >= 255.0f) {
                return std::uint8_t{255};
            }
            return static_cast<std::uint8_t>(scaled + 0.5f);
        };
        return Color{to8(r, factor), to8(g, factor), to8(b, factor), a};
    }

    [[nodiscard]] constexpr Color lerpTo(const Color& other, float t) const noexcept
    {
        const auto mix = [t](std::uint8_t a0, std::uint8_t b0) noexcept {
            return static_cast<std::uint8_t>(static_cast<float>(a0) +
                                             (static_cast<float>(b0) - static_cast<float>(a0)) * t + 0.5f);
        };
        return Color{mix(r, other.r), mix(g, other.g), mix(b, other.b), mix(a, other.a)};
    }

    [[nodiscard]] constexpr Color lerpTo(const Color& other, float t, float fade) const noexcept
    {
        return lerpTo(other, t).withAlpha(static_cast<std::uint8_t>(static_cast<float>(a) * fade + 0.5f));
    }

    constexpr bool operator==(const Color& o) const noexcept
    {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
};

/// Stream inserter, used by logs and test failure messages.
std::ostream& operator<<(std::ostream& os, const Color& color);

namespace Palette {

// --- Era Shift colour identity ---------------------------------------------
// Each era owns a distinct hue family so the player always knows which era they
// are in from a single frame.

// Past: sunlit stone and living green.
constexpr Color PastSky        { 0x8A, 0xC8, 0xE8 };
constexpr Color PastGround     { 0x6B, 0x8E, 0x4E };
constexpr Color PastStone      { 0xB9, 0xAE, 0x93 };
constexpr Color PastAccent     { 0xF2, 0xC2, 0x6B };
constexpr Color PastFog        { 0xD8, 0xE4, 0xD0, 0x99 };

// Present: rust, concrete and sodium light.
constexpr Color PresentSky     { 0x6B, 0x77, 0x86 };
constexpr Color PresentGround  { 0x55, 0x59, 0x5E };
constexpr Color PresentStone   { 0x8A, 0x87, 0x82 };
constexpr Color PresentAccent  { 0xE0, 0x7A, 0x3C };
constexpr Color PresentFog     { 0x9A, 0x9E, 0xA4, 0x88 };

// Future: cold cyan over deep violet.
constexpr Color FutureSky      { 0x2A, 0x1E, 0x4A };
constexpr Color FutureGround   { 0x3A, 0x2C, 0x66 };
constexpr Color FutureStone    { 0x59, 0x4B, 0x8C };
constexpr Color FutureAccent   { 0x53, 0xE0, 0xE8 };
constexpr Color FutureFog      { 0x7B, 0x5C, 0xC4, 0x77 };

// --- Interface -------------------------------------------------------------
constexpr Color Black         { 0, 0, 0 };
constexpr Color White         { 255, 255, 255 };
constexpr Color Transparent   { 0, 0, 0, 0 };
constexpr Color PanelFill     { 0x12, 0x14, 0x1C, 0xD8 };
constexpr Color PanelBorder   { 0x5A, 0x6B, 0x8C, 0xFF };
constexpr Color TextPrimary   { 0xF2, 0xF4, 0xF8 };
constexpr Color TextDim       { 0x9A, 0xA4, 0xB4 };
constexpr Color Accent        { 0x53, 0xE0, 0xE8 };
constexpr Color AccentWarm    { 0xF2, 0xC2, 0x6B };
constexpr Color Warning       { 0xE0, 0x5C, 0x4C };
constexpr Color Positive      { 0x5C, 0xD0, 0x7A };
constexpr Color HealthFill    { 0xE0, 0x4C, 0x5C };
constexpr Color HealthBack    { 0x33, 0x1E, 0x24 };
constexpr Color EnergyFill    { 0x4C, 0xA8, 0xE0 };
constexpr Color EnergyBack    { 0x1E, 0x2A, 0x33 };
constexpr Color ParadoxFill   { 0xA0, 0x54, 0xC8 };
constexpr Color ParadoxBack   { 0x2A, 0x1E, 0x33 };

} // namespace Palette

} // namespace EraShift::Graphics
