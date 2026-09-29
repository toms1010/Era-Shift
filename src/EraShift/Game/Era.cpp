#include "EraShift/Game/Era.hpp"

#include <algorithm>
#include <cctype>

namespace EraShift::Game {

namespace {

/// Case-insensitive comparison, so level data can say "present" or "Present".
bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char lhs, char rhs) {
               return std::tolower(static_cast<unsigned char>(lhs)) ==
                      std::tolower(static_cast<unsigned char>(rhs));
           });
}

} // namespace

std::string_view eraName(Era era) noexcept
{
    switch (era) {
        case Era::Past:    return "Past";
        case Era::Present: return "Present";
        case Era::Future:  return "Future";
    }
    return "Unknown";
}

bool parseEra(std::string_view name, Era& out) noexcept
{
    constexpr Era kAll[kEraCount] = {Era::Past, Era::Present, Era::Future};
    for (const Era candidate : kAll) {
        if (equalsIgnoreCase(name, eraName(candidate))) {
            out = candidate;
            return true;
        }
    }
    return false;
}

} // namespace EraShift::Game
