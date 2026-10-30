#ifndef FREEDV_RADE_MODE_H
#define FREEDV_RADE_MODE_H

#include <array>

// User selection only. This must not be used as rade_open() flags or as
// evidence of which codec is active (FreeDVInterface owns that information).
enum class RadeMode : long
{
    RADEV1 = 1,
    RADEV2 = 2
};

namespace RadeModes
{
constexpr std::array<RadeMode, 2> selectable{{RadeMode::RADEV1, RadeMode::RADEV2}};

constexpr bool IsValid(long value)
{
    return value == static_cast<long>(RadeMode::RADEV1) ||
           value == static_cast<long>(RadeMode::RADEV2);
}

constexpr RadeMode FromStoredValue(long value)
{
    return IsValid(value) ? static_cast<RadeMode>(value) : RadeMode::RADEV1;
}

inline const char* DisplayName(RadeMode mode)
{
    switch (mode)
    {
    case RadeMode::RADEV1: return "RADEV1";
    case RadeMode::RADEV2: return "RADEV2 (experimental)";
    }
    return DisplayName(RadeMode::RADEV1);
}

// Phase 1: selection/persistence are available, but only V1 can start.
constexpr bool CanStart(RadeMode mode)
{
    return mode == RadeMode::RADEV1;
}
}

#endif // FREEDV_RADE_MODE_H
