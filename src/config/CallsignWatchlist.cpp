#include "CallsignWatchlist.h"
#include "LocalTimestamp.h"

#include <algorithm>
#include <wx/datetime.h>
#include <wx/intl.h>
#include <wx/regex.h>
#include <wx/tokenzr.h>
#include <wx/wxcrt.h>

wxString CallsignWatchlist::Normalize(wxString callsign)
{
    callsign.Trim(true).Trim(false);
    // Also protects the config's comma/pipe-delimited record representation.
    bool hasAlphanumeric = false;
    wxString normalized;
    for (auto character : callsign)
    {
        auto c = character.GetValue();
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A'; // Callsigns use ASCII, independent of locale.
        const bool alphanumeric = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alphanumeric && c != '/' && c != '-')
            return {};
        hasAlphanumeric |= alphanumeric;
        normalized += wxUniChar(c);
    }
    return hasAlphanumeric ? normalized : wxString();
}

CallsignWatchlist::Timestamp CallsignWatchlist::Now()
{
    return wxDateTime::UNow().GetValue().GetValue();
}

CallsignWatchlist::Timestamp CallsignWatchlist::ParseReporterTime(const wxString& time)
{
    // Server timestamps are ISO 8601. Missing zone means UTC, as in Reporter.
    // Compute UTC directly, rather than first constructing a local wall time:
    // some valid UTC times fall in the local clock's spring-forward gap.
    wxRegEx pattern("^([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2})(\\.[0-9]+)?(Z|[+-][0-9]{2}:[0-9]{2})?$");
    if (!pattern.Matches(time))
        return 0;
    const auto civil = pattern.GetMatch(time, 1);
    const int year = wxAtoi(civil.Mid(0, 4));
    const int month = wxAtoi(civil.Mid(5, 2));
    const int day = wxAtoi(civil.Mid(8, 2));
    const int hour = wxAtoi(civil.Mid(11, 2));
    const int minute = wxAtoi(civil.Mid(14, 2));
    const int second = wxAtoi(civil.Mid(17, 2));
    const auto leapYearCount = [](int before) { return before / 4 - before / 100 + before / 400; };
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int monthDays[] = {31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > monthDays[month - 1] ||
        hour > 23 || minute > 59 || second > 59)
        return 0;
    Timestamp days = 365LL * (year - 1970) + leapYearCount(year - 1) - leapYearCount(1969) + day - 1;
    for (int m = 1; m < month; ++m) days += monthDays[m - 1];
    const auto zone = pattern.GetMatch(time, 3);
    int offset = 0;
    if (!zone.empty() && zone != "Z")
    {
        const int hours = wxAtoi(zone.Mid(1, 2));
        const int minutes = wxAtoi(zone.Mid(4, 2));
        if (hours > 23 || minutes > 59)
            return 0;
        offset = (hours * 60 + minutes) * 60 * (zone[0] == '-' ? -1 : 1);
    }
    auto fraction = pattern.GetMatch(time, 2).Mid(1).Left(3);
    while (fraction.length() < 3)
        fraction += '0';
    const Timestamp result = ((days * 24 + hour) * 3600 + minute * 60 + second - offset) * 1000 + wxAtoi(fraction);
    return result > 0 && result <= 253402300799999LL ? result : 0;
}

wxString CallsignWatchlist::FormatLocalTime(Timestamp time)
{
    return WatchlistTime::FormatLocalTimestamp(time);
}

CallsignWatchlist::Indicator CallsignWatchlist::IndicatorFor(Status status, Timestamp lastSeen, Timestamp now)
{
    if (status == Status::Unknown || lastSeen <= 0) return Indicator::Gray;
    if (status == Status::Listed || now < lastSeen) return Indicator::Green;
    const auto age = now - lastSeen;
    constexpr Timestamp hour = 60LL * 60 * 1000;
    if (age < hour) return Indicator::Green;
    if (age <= 12 * hour) return Indicator::Yellow;
    return Indicator::Red;
}

bool CallsignWatchlist::Contains(const wxString& callsign) const
{
    return records_.count(Normalize(callsign)) != 0;
}

bool CallsignWatchlist::Add(const wxString& callsign)
{
    auto normalized = Normalize(callsign);
    if (normalized.empty() || Contains(normalized))
        return false;
    auto& record = records_[normalized];
    for (const auto& session : sessions_)
        if (session.second.callsign == normalized)
            MergeHistory(record, session.second.history);
    dirty_ = viewDirty_ = true;
    return true;
}

bool CallsignWatchlist::Remove(const wxString& callsign)
{
    const auto normalized = Normalize(callsign);
    if (!records_.erase(normalized))
        return false;
    highlights_.erase(normalized);
    dirty_ = viewDirty_ = true;
    return true;
}

CallsignWatchlist::Status CallsignWatchlist::GetStatus(const wxString& callsign) const
{
    if (connection_ != Connection::Ready)
        return Status::Unknown;
    const auto normalized = Normalize(callsign);
    for (const auto& session : sessions_)
        if (session.second.callsign == normalized)
            return Status::Listed;
    return Status::NotListed;
}

int CallsignWatchlist::HighlightRemainingMs(const wxString& callsign) const
{
    auto found = highlights_.find(Normalize(callsign));
    if (found == highlights_.end() || GetStatus(callsign) != Status::Listed)
        return 0;
    return static_cast<int>(std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(
        found->second - std::chrono::steady_clock::now()).count()));
}

void CallsignWatchlist::BeginSync()
{
    sessions_.clear();
    highlights_.clear();
    connection_ = Connection::Synchronizing;
    viewDirty_ = true;
}

void CallsignWatchlist::CompleteSync()
{
    connection_ = Connection::Ready;
    viewDirty_ = true;
}

void CallsignWatchlist::Disconnect()
{
    sessions_.clear();
    highlights_.clear();
    connection_ = Connection::Unavailable;
    viewDirty_ = true;
}

void CallsignWatchlist::MergeHistory(Record& target, const Record& source)
{
    const auto seen = std::max(target.lastSeen, source.lastSeen);
    const auto update = std::max(target.lastUpdate, source.lastUpdate);
    if (seen != target.lastSeen || update != target.lastUpdate)
    {
        target = {seen, update};
        dirty_ = viewDirty_ = true;
    }
}

void CallsignWatchlist::Observe(const std::string& sid, const wxString& callsign,
                               const wxString& serverTime, const wxString& kind,
                               const wxString& detail, Timestamp observedAt)
{
    const auto normalized = Normalize(callsign);
    if (sid.empty() || normalized.empty())
        return;
    if (connection_ == Connection::Unavailable)
    {
        // Automatic transport reconnects replay sessions without repeating the
        // initial Reporter connect callback. Stay Unknown until CompleteSync().
        if (kind != "connect") return;
        BeginSync();
    }
    const auto wasListed = GetStatus(normalized) == Status::Listed;
    const auto previous = sessions_.find(sid);
    const bool affectsWatchlist = Contains(normalized) ||
        (previous != sessions_.end() && Contains(previous->second.callsign));
    auto& session = sessions_[sid];
    if (session.callsign != normalized)
        session = Session{normalized, {}, {}};
    const auto update = ParseReporterTime(serverTime);
    if (update > 0 && update < session.history.lastUpdate)
        return; // A delayed event must not make old activity look new.
    const wxString fingerprint = serverTime + "|" + detail;
    auto found = session.observations.find(kind);
    if (found != session.observations.end() && found->second == fingerprint)
        return;
    session.observations[kind] = fingerprint;
    session.history.lastSeen = std::max(session.history.lastSeen, observedAt);
    session.history.lastUpdate = std::max(session.history.lastUpdate, update);
    if (Contains(normalized))
    {
        MergeHistory(records_.at(normalized), session.history);
        if (!wasListed && connection_ == Connection::Ready)
            highlights_[normalized] = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    }
    viewDirty_ |= affectsWatchlist;
}

void CallsignWatchlist::RemoveSession(const std::string& sid, const wxString& serverTime)
{
    auto found = sessions_.find(sid);
    if (found == sessions_.end())
        return;
    const auto callsign = found->second.callsign;
    const bool watched = Contains(callsign);
    if (watched)
    {
        auto history = found->second.history;
        history.lastUpdate = std::max(history.lastUpdate, ParseReporterTime(serverTime));
        MergeHistory(records_.at(callsign), history);
    }
    sessions_.erase(found);
    if (GetStatus(callsign) != Status::Listed)
        highlights_.erase(callsign);
    viewDirty_ |= watched;
}

void CallsignWatchlist::load(wxConfigBase* config)
{
    ConfigurationDataElement<std::vector<wxString>> stored("/Reporting/FreeDV/Watchlist/Records", {});
    load_(config, stored);
    records_.clear();
    highlights_.clear();
    dirty_ = false;
    for (const auto& row : stored.get())
    {
        const auto fields = wxSplit(row, '|');
        const auto callsign = Normalize(fields.empty() ? wxString() : fields[0]);
        if (callsign.empty())
            continue;
        Record record;
        long long value;
        if (fields.size() > 1 && fields[1].ToLongLong(&value) && value > 0 && value <= 253402300799999LL)
            record.lastSeen = value;
        if (fields.size() > 2 && fields[2].ToLongLong(&value) && value > 0 && value <= 253402300799999LL)
            record.lastUpdate = value;
        // Merge duplicate legacy/case variants; never discard their newest dates.
        MergeHistory(records_[callsign], record);
    }
    for (const auto& session : sessions_)
        if (Contains(session.second.callsign))
            MergeHistory(records_.at(session.second.callsign), session.second.history);
    viewDirty_ = true;
}

wxString CallsignWatchlist::Serialize() const
{
    std::vector<wxString> rows;
    rows.reserve(records_.size());
    for (const auto& record : records_)
        rows.push_back(record.first + wxString::Format("|%lld|%lld", record.second.lastSeen, record.second.lastUpdate));
    // Same representation as WxWidgetsConfigStore's string-vector specialization.
    wxString result;
    for (const auto& row : rows)
    {
        if (!result.empty()) result += ',';
        result += row;
    }
    return result;
}

void CallsignWatchlist::save(wxConfigBase* config)
{
    config->Write("/Reporting/FreeDV/Watchlist/Records", Serialize());
}

bool CallsignWatchlist::Commit(wxConfigBase* config)
{
    bool success = true;
    if (dirty_)
    {
        success = config && config->Write("/Reporting/FreeDV/Watchlist/Records", Serialize()) && config->Flush();
        if (success) dirty_ = false;
    }
    if (viewDirty_)
    {
        viewDirty_ = false;
        if (changed_) changed_();
    }
    return success;
}
