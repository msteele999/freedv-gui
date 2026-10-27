#ifndef FREEDV_LOCAL_TIMESTAMP_H
#define FREEDV_LOCAL_TIMESTAMP_H

#include <wx/string.h>

namespace WatchlistTime
{
// Presentation only. Input remains UTC Unix milliseconds; no persisted changes.
wxString FormatLocalTimestamp(long long timestamp);
#if defined(_WIN32)
// Explicit Windows zone key, also used for deterministic DST regression cases
// without changing the computer's timezone. Empty if system ICU is unavailable.
wxString FormatWindowsZone(long long timestamp, const wxString& windowsZone);
#endif
}
#endif
