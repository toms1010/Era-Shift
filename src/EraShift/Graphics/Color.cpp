#include "EraShift/Graphics/Color.hpp"

#include <ostream>

namespace EraShift::Graphics {

std::ostream& operator<<(std::ostream& os, const Color& color)
{
    // Hex matches the packed() layout, so it reads unambiguously in a log.
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%08X", color.packed());
    return os << buffer;
}

} // namespace EraShift::Graphics
