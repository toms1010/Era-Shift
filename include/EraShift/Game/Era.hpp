// Era Shift - the three eras.
//
// The whole game is one physical space observed from three different points in
// time. `Era` is therefore not a skin: the same tile can be solid in the Past
// and empty in the Present, an enemy can exist in two eras and not the third,
// and an objective can demand the player be somewhere else to solve it.
//
// This header is deliberately in Core and free of SDL so the timeline rules can
// be unit tested headlessly, which is the only way to pin down mechanics like
// "how long is a shift and what does it cost".

#pragma once

#include <cstdint>
#include <string_view>

namespace EraShift::Game {

/// The three states the world can be observed in.
enum class Era : std::uint8_t {
    Past    = 0,
    Present = 1,
    Future  = 2,
};

/// Number of eras. Part of the save file format, so it must not change.
inline constexpr int kEraCount = 3;

/// Bitmask selecting a set of eras. Used for "solid in the Present only" tiles
/// and "exists in Past and Future" enemies.
using EraMask = std::uint8_t;

inline constexpr EraMask kEraBitPast    = 1u << 0;
inline constexpr EraMask kEraBitPresent = 1u << 1;
inline constexpr EraMask kEraBitFuture  = 1u << 2;
inline constexpr EraMask kEraMaskAll    = kEraBitPast | kEraBitPresent | kEraBitFuture;

/// The bit for a single era.
[[nodiscard]] constexpr EraMask eraBit(Era era) noexcept
{
    return static_cast<EraMask>(1u << static_cast<unsigned>(era));
}

/// True when `mask` includes `era`.
[[nodiscard]] constexpr bool maskIncludes(EraMask mask, Era era) noexcept
{
    return (mask & eraBit(era)) != 0;
}

/// Display name, e.g. "Present". Also the value written to save files and level
/// data, so it is stable.
[[nodiscard]] std::string_view eraName(Era era) noexcept;

/// Parses an era name, case-insensitively.
/// @return false when `name` is not one of the three eras.
[[nodiscard]] bool parseEra(std::string_view name, Era& out) noexcept;

/// The next era in the cycle Past -> Present -> Future -> Past.
[[nodiscard]] constexpr Era nextEra(Era era) noexcept
{
    return static_cast<Era>((static_cast<unsigned>(era) + 1u) % static_cast<unsigned>(kEraCount));
}

} // namespace EraShift::Game
