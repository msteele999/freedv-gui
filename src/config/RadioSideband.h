#ifndef FREEDV_RADIO_SIDEBAND_H
#define FREEDV_RADIO_SIDEBAND_H

// Radio preference only: both RADE protocols use unchanged modem audio.
// Stations must select matching radio sidebands.
enum class RadioSideband : long
{
    Default = 0,
    LSB = 1
};

namespace RadioSidebands
{
constexpr RadioSideband FromStoredValue(long value)
{
    return value == static_cast<long>(RadioSideband::LSB)
        ? RadioSideband::LSB : RadioSideband::Default;
}
}

#endif
