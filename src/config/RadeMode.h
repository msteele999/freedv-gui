#ifndef FREEDV_RADE_MODE_H
#define FREEDV_RADE_MODE_H

#include <array>

// A mode identifier, not rade_open() flags. The persisted user selection and
// FreeDVInterface's active session each own a separate value of this type.
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

// Protocol identity for status and reporting; UI annotations belong above.
inline const char* ProtocolName(RadeMode mode)
{
    switch (mode)
    {
    case RadeMode::RADEV1: return "RADEV1";
    case RadeMode::RADEV2: return "RADEV2";
    }
    return "unknown";
}

constexpr bool CanStart(RadeMode mode)
{
    return IsValid(static_cast<long>(mode));
}
}

#endif // FREEDV_RADE_MODE_H
