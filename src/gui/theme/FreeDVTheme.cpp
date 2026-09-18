#include "FreeDVTheme.h"

#include <algorithm>
#include <wx/settings.h>
#include <wx/window.h>

namespace FreeDVTheme
{
namespace
{
bool darkModeEnabled = false;
}

void SetDarkModeEnabled(bool enabled)
{
    darkModeEnabled = enabled;
}

const Palette& GetPalette()
{
    static const Palette palette = {
        wxColour(24, 26, 30),    // window background
        wxColour(34, 37, 43),    // elevated panel surface
        wxColour(235, 237, 240), // primary text
        wxColour(170, 177, 188), // secondary text
        wxColour(74, 81, 93),    // border / separator
        wxColour(103, 174, 255), // accent
        wxColour(103, 204, 142), // success
        wxColour(240, 193, 91),  // warning
        wxColour(242, 120, 120)  // danger / error
    };
    return palette;
}

wxFont GetFont(TypographyRole role)
{
    wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    switch (role)
    {
        case TypographyRole::Body:
            break;
        case TypographyRole::Secondary:
            font.SetPointSize(std::max(1, font.GetPointSize() - 1));
            break;
        case TypographyRole::Emphasized:
            font.SetWeight(wxFONTWEIGHT_BOLD);
            break;
        case TypographyRole::Heading:
            font.SetPointSize(font.GetPointSize() + 2);
            font.SetWeight(wxFONTWEIGHT_BOLD);
            break;
    }
    return font;
}

void ApplyWindowSurface(wxWindow& window)
{
    if (!darkModeEnabled)
    {
        return;
    }

    const auto& palette = GetPalette();
    window.SetOwnBackgroundColour(palette.windowBackground);
    window.SetOwnForegroundColour(palette.primaryText);
}
}
