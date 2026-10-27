#include "LocalTimestamp.h"

#include <ctime>
#include <cstdint>
#include <wx/intl.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wx/dynlib.h>

namespace
{
// The stable ICU C ABI is supplied by Windows, loaded from System32 so no new
// packaged dependency or versioned ICU import library is required.
// https://learn.microsoft.com/windows/win32/intl/international-components-for-unicode--icu-
class WindowsTimeFormatter
{
public:
    WindowsTimeFormatter()
    {
        wchar_t directory[MAX_PATH];
        const auto length = GetSystemDirectoryW(directory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH ||
            !library_.Load(wxString(directory) + L"\\icu.dll", wxDL_NOW | wxDL_VERBATIM | wxDL_QUIET))
            return;
        if (!library_.HasSymbol("udat_open") || !library_.HasSymbol("udat_format") ||
            !library_.HasSymbol("udat_close") || !library_.HasSymbol("ucal_getTimeZoneIDForWindowsID"))
            return;
        open_ = reinterpret_cast<Open>(library_.GetSymbol("udat_open"));
        format_ = reinterpret_cast<Format>(library_.GetSymbol("udat_format"));
        close_ = reinterpret_cast<Close>(library_.GetSymbol("udat_close"));
        zone_ = reinterpret_cast<Zone>(library_.GetSymbol("ucal_getTimeZoneIDForWindowsID"));
    }

    ~WindowsTimeFormatter()
    {
        if (formatter_) close_(formatter_);
    }

    wxString FormatTime(long long timestamp, const wxString& key)
    {
        if (!open_ || key.empty()) return {};
        if (!formatter_ || key != cachedKey_)
        {
            if (formatter_) close_(formatter_);
            formatter_ = nullptr;
            std::int32_t error = 0;
            wchar_t zone[128];
            const auto length = zone_(key.wc_str(), static_cast<std::int32_t>(key.length()),
                                      nullptr, zone, 128, &error);
            if (error > 0 || length <= 0 || length >= 128) return {};
            // 'z' is ICU's date-specific short zone name (e.g. EST versus EDT).
            // English zone labels are intentional; no localized long-name initials.
            constexpr wchar_t pattern[] = L"yyyy-MM-dd HH:mm:ss z";
            constexpr int patternStyle = -2; // UDAT_PATTERN, stable ICU C enum.
            formatter_ = open_(patternStyle, patternStyle, "en_US", zone, length,
                               pattern, -1, &error);
            if (error > 0 || !formatter_)
            {
                if (formatter_) close_(formatter_);
                formatter_ = nullptr;
                return {};
            }
            cachedKey_ = key;
        }
        std::int32_t error = 0;
        wchar_t output[128];
        const auto length = format_(formatter_, static_cast<double>(timestamp), output, 128, nullptr, &error);
        return error <= 0 && length > 0 && length < 128 ? wxString(output, length) : wxString();
    }

private:
    // Windows UChar is UTF-16, as is wchar_t. UErrorCode and the style enums
    // are 32-bit C ABI values. No ICU C++ objects cross the DLL boundary.
    using Open = void* (*)(int, int, const char*, const wchar_t*, std::int32_t,
                          const wchar_t*, std::int32_t, std::int32_t*);
    using Format = std::int32_t (*)(const void*, double, wchar_t*, std::int32_t, void*, std::int32_t*);
    using Close = void (*)(void*);
    using Zone = std::int32_t (*)(const wchar_t*, std::int32_t, const char*, wchar_t*, std::int32_t, std::int32_t*);
    wxDynamicLibrary library_;
    Open open_ = nullptr;
    Format format_ = nullptr;
    Close close_ = nullptr;
    Zone zone_ = nullptr;
    void* formatter_ = nullptr;
    wxString cachedKey_;
};

wxString NativeWindowsTime(long long timestamp, const DYNAMIC_TIME_ZONE_INFORMATION& zone)
{
    // Exact OS conversion is also a safe fallback on stripped/older Windows
    // without ICU, or when the user has explicitly disabled automatic DST.
    ULARGE_INTEGER ticks;
    ticks.QuadPart = static_cast<ULONGLONG>(timestamp) * 10000ULL + 116444736000000000ULL;
    FILETIME utcFile{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&utcFile, &utc) || !SystemTimeToTzSpecificLocalTimeEx(&zone, &utc, &local))
        return _("Unknown");
    FILETIME localFile;
    if (!SystemTimeToFileTime(&local, &localFile)) return _("Unknown");
    ULARGE_INTEGER localTicks;
    localTicks.LowPart = localFile.dwLowDateTime;
    localTicks.HighPart = localFile.dwHighDateTime;
    const auto minutes = (static_cast<long long>(localTicks.QuadPart) - static_cast<long long>(ticks.QuadPart)) / 600000000LL;
    const auto absolute = minutes < 0 ? -minutes : minutes;
    return wxString::Format("%04u-%02u-%02u %02u:%02u:%02u UTC%c%02lld:%02lld",
        static_cast<unsigned>(local.wYear), static_cast<unsigned>(local.wMonth), static_cast<unsigned>(local.wDay),
        static_cast<unsigned>(local.wHour), static_cast<unsigned>(local.wMinute), static_cast<unsigned>(local.wSecond),
        minutes < 0 ? '-' : '+', absolute / 60, absolute % 60);
}
}
#endif

namespace WatchlistTime
{
#if defined(_WIN32)
wxString FormatWindowsZone(long long timestamp, const wxString& windowsZone)
{
    if (timestamp <= 0 || timestamp > 253402300799999LL) return {};
    static WindowsTimeFormatter formatter; // UI-thread only, like the watchlist.
    return formatter.FormatTime(timestamp, windowsZone);
}
#endif

wxString FormatLocalTimestamp(long long timestamp)
{
    if (timestamp <= 0 || timestamp > 253402300799999LL) return _("Unknown");
#if defined(_WIN32)
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    if (GetDynamicTimeZoneInformation(&zone) == TIME_ZONE_ID_INVALID) return _("Unknown");
    if (!zone.DynamicDaylightTimeDisabled)
    {
        const auto formatted = FormatWindowsZone(timestamp, zone.TimeZoneKeyName);
        if (!formatted.empty()) return formatted;
    }
    return NativeWindowsTime(timestamp, zone);
#else
    // strftime uses the rules and abbreviation at this instant, not today's
    // tzname or DST flag. tzset picks up OS/process timezone changes too.
    const auto seconds = static_cast<std::time_t>(timestamp / 1000);
    if (static_cast<long long>(seconds) != timestamp / 1000) return _("Unknown");
    tzset();
    std::tm local{};
    char output[128];
    if (!localtime_r(&seconds, &local) || std::strftime(output, sizeof(output), "%Y-%m-%d %H:%M:%S %Z", &local) == 0)
        return _("Unknown");
    return wxString::FromUTF8(output);
#endif
}
}
