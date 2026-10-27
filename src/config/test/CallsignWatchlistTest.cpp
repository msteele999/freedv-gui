#include "../CallsignWatchlist.h"
#include "../LocalTimestamp.h"
#include "ConfigTestCommon.h"
#include <cstdlib>
#include <ctime>
#include <string>
#include <wx/init.h>

namespace
{
using Watchlist = CallsignWatchlist;
using Status = Watchlist::Status;
const wxString firstTime = "2026-10-27T12:00:00.125Z";
const wxString laterTime = "2026-10-27T13:00:00.500+00:00";

bool normalizationAndDuplicates()
{
    Watchlist list;
    return list.Add("  w2mws/p  ") && !list.Add("W2MWS/P") && list.Contains("w2mws/P") &&
        !list.Add(" ") && !list.Add("K1ABC,OTHER") && !list.Add("K1ABC|42") &&
        list.Records().size() == 1 && list.Remove("w2mws/p") && list.Records().empty();
}

bool sessionsAndConnectivity()
{
    Watchlist list;
    list.Add("K1ABC");
    if (list.GetStatus("K1ABC") != Status::Unknown) return false;
    list.BeginSync();
    list.Observe("one", "k1abc", firstTime, "connect", "one", 100);
    if (list.GetStatus("K1ABC") != Status::Unknown || list.HighlightRemainingMs("K1ABC") != 0) return false;
    list.CompleteSync();
    list.Observe("two", "K1ABC", firstTime, "connect", "two", 200);
    if (list.GetStatus("K1ABC") != Status::Listed || list.HighlightRemainingMs("K1ABC") != 0) return false;
    list.RemoveSession("one", firstTime);
    if (list.GetStatus("K1ABC") != Status::Listed) return false;
    list.RemoveSession("two", laterTime);
    if (list.GetStatus("K1ABC") != Status::NotListed) return false;
    list.Observe("three", "K1ABC", laterTime, "connect", "three", 300);
    if (list.HighlightRemainingMs("K1ABC") <= 0) return false;
    list.Disconnect();
    if (list.GetStatus("K1ABC") != Status::Unknown || list.Records().at("K1ABC").lastSeen != 300) return false;
    list.BeginSync();
    list.Observe("three", "K1ABC", laterTime, "connect", "three", 400);
    list.CompleteSync();
    return list.GetStatus("K1ABC") == Status::Listed && list.HighlightRemainingMs("K1ABC") == 0 &&
        list.Records().at("K1ABC").lastSeen == 400;
}

bool automaticReconnectWithoutConnectCallback()
{
    auto config = makeConfig();
    Watchlist list;
    list.Add("K1ABC");
    list.Add("OLD/P");
    list.BeginSync();
    list.Observe("old-one", "K1ABC", firstTime, "connect", "first session", 100);
    list.Observe("old-two", "OLD/P", firstTime, "connect", "absent on reconnect", 100);
    list.CompleteSync();
    list.Disconnect();
    if (!list.Commit(config.get())) return false;

    // A data update alone cannot manufacture presence while disconnected.
    list.Observe("old-one", "K1ABC", laterTime, "frequency", "14236000", 150);
    list.Observe("", "K1ABC", laterTime, "connect", "invalid session", 150);
    if (list.GetStatus("K1ABC") != Status::Unknown || list.Records().at("K1ABC").lastSeen != 100)
        return false;

    // The backend replays new_connection events on automatic reconnect, but
    // does not repeat its initial onReporterConnect callback.
    list.Observe("replay-one", "K1ABC", firstTime, "connect", "first replay", 200);
    list.Observe("replay-two", "K1ABC", laterTime, "connect", "second replay", 300);
    Status notifiedStatus = Status::NotListed;
    list.SetChangedHandler([&]() { notifiedStatus = list.GetStatus("K1ABC"); });
    if (!list.Commit(config.get()) || notifiedStatus != Status::Unknown ||
        list.HighlightRemainingMs("K1ABC") != 0) return false;
    list.CompleteSync();
    if (!list.Commit(config.get()) || notifiedStatus != Status::Listed ||
        list.HighlightRemainingMs("K1ABC") != 0 || list.GetStatus("OLD/P") != Status::NotListed)
        return false;
    list.RemoveSession("replay-one", laterTime);
    if (list.GetStatus("K1ABC") != Status::Listed) return false;
    list.RemoveSession("replay-two", laterTime);
    if (list.GetStatus("K1ABC") != Status::NotListed) return false;

    // An empty reconnect snapshot completes normally and retains all history.
    list.Disconnect();
    list.CompleteSync();
    return list.GetStatus("K1ABC") == Status::NotListed && list.Records().at("K1ABC").lastSeen == 300 &&
        list.Records().at("K1ABC").lastUpdate == Watchlist::ParseReporterTime(laterTime) &&
        list.Records().at("OLD/P").lastSeen == 100;
}

bool monotonicTimestampsAndMeaningfulObservations()
{
    Watchlist list;
    list.Add("K1ABC");
    list.BeginSync();
    list.Observe("one", "K1ABC", laterTime, "connect", "session", 100);
    const auto expected = Watchlist::ParseReporterTime(laterTime);
    list.Observe("one", "K1ABC", firstTime, "transmit", "TX", 200);
    if (list.Records().at("K1ABC").lastSeen != 100) return false;
    list.Observe("one", "K1ABC", laterTime, "connect", "session", 300);
    if (list.Records().at("K1ABC").lastSeen != 100) return false;
    list.Observe("one", "K1ABC", "invalid", "receive", "W2DEF|RADEV1|10", 400);
    list.Observe("two", "K1ABC", firstTime, "connect", "second", 350); // Clock moved back.
    list.RemoveSession("one", firstTime);
    const auto record = list.Records().at("K1ABC");
    return record.lastSeen == 400 && record.lastUpdate == expected;
}

bool timezoneAndInvalidTimestamps()
{
    return Watchlist::ParseReporterTime("1970-01-01T00:00:01Z") == 1000 &&
        Watchlist::ParseReporterTime("2026-03-08T02:30:00Z") ==
            Watchlist::ParseReporterTime("2026-03-07T21:30:00-05:00") &&
        Watchlist::ParseReporterTime(firstTime) > 0 &&
        Watchlist::ParseReporterTime(firstTime) == Watchlist::ParseReporterTime("2026-10-27T08:00:00.125000-04:00") &&
        Watchlist::ParseReporterTime(firstTime) == Watchlist::ParseReporterTime("2026-10-27T17:30:00.125+05:30") &&
        Watchlist::ParseReporterTime(firstTime) == Watchlist::ParseReporterTime("2026-10-27T12:00:00.125") &&
        Watchlist::ParseReporterTime("2026-10-27T12:00:00+25:00") == 0 &&
        Watchlist::ParseReporterTime("2026-10-27T12:00:00-04:99") == 0 &&
        Watchlist::ParseReporterTime("2026-02-30T00:00:00Z") == 0 &&
        Watchlist::ParseReporterTime("not a date") == 0 && Watchlist::ParseReporterTime("") == 0;
}

bool persistenceAndIndefiniteRetention()
{
    auto config = makeConfig();
    Watchlist list;
    list.Add("K1ABC");
    list.Add("UNSEEN");
    list.BeginSync();
    list.Observe("one", "K1ABC", "1980-01-01T00:00:00Z", "connect", "old", 315532800000LL);
    list.CompleteSync();
    // A disconnected session and arbitrarily old dates are never cleanup inputs.
    list.RemoveSession("one", "");
    list.Disconnect();
    if (!list.Commit(config.get())) return false; // No window is involved.
    auto disk = reload(*config);
    Watchlist restarted;
    restarted.load(disk.get());
    const auto history = restarted.Records().at("K1ABC");
    for (int reconnect = 0; reconnect < 5; ++reconnect)
    {
        restarted.BeginSync();
        restarted.CompleteSync();
        restarted.Disconnect();
    }
    if (restarted.Records().size() != 2 || history.lastSeen != 315532800000LL ||
        restarted.Records().at("K1ABC").lastUpdate != history.lastUpdate ||
        restarted.Records().at("UNSEEN").lastSeen != 0) return false;
    restarted.Remove("K1ABC");
    restarted.Commit(disk.get());
    auto removed = reload(*disk);
    Watchlist final;
    final.load(removed.get());
    return !final.Contains("K1ABC") && final.Contains("UNSEEN");
}

bool addAnAlreadyPresentSession()
{
    Watchlist list;
    list.BeginSync();
    list.Observe("one", "K1ABC", firstTime, "connect", "session", 100);
    list.CompleteSync();
    list.Add("k1abc");
    return list.GetStatus("K1ABC") == Status::Listed && list.Records().at("K1ABC").lastSeen == 100 &&
        list.HighlightRemainingMs("K1ABC") == 0;
}

bool corruptDatesAndDuplicateStoredRecords()
{
    auto config = makeConfig();
    config->Write("/Reporting/FreeDV/Watchlist/Records", "k1abc|100|500,K1ABC|200|garbage,UNSEEN|-3|oops");
    Watchlist list;
    list.load(config.get());
    return list.Records().size() == 2 && list.Records().at("K1ABC").lastSeen == 200 &&
        list.Records().at("K1ABC").lastUpdate == 500 && list.Records().at("UNSEEN").lastSeen == 0;
}

bool failedSaveRetriesAndBatchesNotify()
{
    Watchlist list;
    list.Add("K1ABC");
    int notifications = 0;
    list.SetChangedHandler([&notifications]() { ++notifications; });
    if (list.Commit(nullptr) || notifications != 1) return false;
    auto config = makeConfig();
    if (!list.Commit(config.get()) || notifications != 1) return false;
    auto disk = reload(*config);
    Watchlist restarted;
    restarted.load(disk.get());
    return restarted.Contains("K1ABC");
}

bool profilesRemainIndependent()
{
    auto first = makeConfig();
    auto second = makeConfig();
    first->Write("/Reporting/FreeDV/Watchlist/Records", "FIRST|100|200");
    second->Write("/Reporting/FreeDV/Watchlist/Records", "SECOND|300|400");
    Watchlist list;
    list.load(first.get());
    list.BeginSync();
    list.Observe("one", "FIRST", firstTime, "connect", "session", 500);
    list.Disconnect();
    list.Commit(first.get());
    list.load(second.get());
    if (!list.Contains("SECOND") || list.Contains("FIRST")) return false;
    list.Commit(second.get());
    Watchlist restored;
    restored.load(first.get());
    return restored.Contains("FIRST") && !restored.Contains("SECOND") &&
        restored.Records().at("FIRST").lastSeen == 500;
}

wxString formatInZone(const wxString& stamp, const char* windowsZone, const char* posixZone)
{
    const auto time = Watchlist::ParseReporterTime(stamp);
#if defined(_WIN32)
    (void)posixZone;
    return WatchlistTime::FormatWindowsZone(time, windowsZone);
#else
    (void)windowsZone;
    const char* oldZone = std::getenv("TZ");
    const bool wasSet = oldZone != nullptr;
    const std::string saved = wasSet ? oldZone : "";
    setenv("TZ", posixZone, 1);
    tzset();
    const auto result = Watchlist::FormatLocalTime(time);
    if (wasSet) setenv("TZ", saved.c_str(), 1);
    else unsetenv("TZ");
    tzset();
    return result;
#endif
}

bool timezoneAbbreviationsAndDst()
{
    // Deliberately format winter and summer in the same process: using today's
    // abbreviation for historical timestamps would fail these transition pairs.
    for (const auto& example : {
        std::make_pair("2026-03-08T06:59:59Z", "2026-03-08 01:59:59 EST"),
        std::make_pair("2026-03-08T07:00:00Z", "2026-03-08 03:00:00 EDT"),
        std::make_pair("2026-11-01T05:59:59Z", "2026-11-01 01:59:59 EDT"),
        std::make_pair("2026-11-01T06:00:00Z", "2026-11-01 01:00:00 EST")})
        if (!check("Eastern transition", formatInZone(example.first, "Eastern Standard Time", "America/New_York"), example.second))
            return false;
    for (const auto& example : {
        std::make_pair("2026-03-08T09:59:59Z", "2026-03-08 01:59:59 PST"),
        std::make_pair("2026-03-08T10:00:00Z", "2026-03-08 03:00:00 PDT"),
        std::make_pair("2026-01-15T12:00:00Z", "2026-01-15 04:00:00 PST"),
        std::make_pair("2026-07-15T12:00:00Z", "2026-07-15 05:00:00 PDT")})
        if (!check("Pacific transition", formatInZone(example.first, "Pacific Standard Time", "America/Los_Angeles"), example.second))
            return false;
    const auto india = formatInZone("2026-07-15T12:00:00Z", "India Standard Time", "Asia/Kolkata");
    return india.StartsWith("2026-07-15 17:30:00 ") && india.Mid(20).Find(' ') == wxNOT_FOUND &&
        Watchlist::FormatLocalTime(0) == "Unknown";
}

bool indicatorBoundariesAndReadOnlyHistory()
{
    using Indicator = Watchlist::Indicator;
    constexpr Watchlist::Timestamp now = 100000000000LL;
    constexpr Watchlist::Timestamp hour = 3600000;
    const auto indicator = [](Status status, Watchlist::Timestamp age) {
        constexpr Watchlist::Timestamp reference = 100000000000LL;
        return Watchlist::IndicatorFor(status, reference - age, reference);
    };
    if (indicator(Status::NotListed, hour - 1) != Indicator::Green ||
        indicator(Status::NotListed, hour) != Indicator::Yellow ||
        indicator(Status::NotListed, hour + 1) != Indicator::Yellow ||
        indicator(Status::NotListed, 12 * hour - 1) != Indicator::Yellow ||
        indicator(Status::NotListed, 12 * hour) != Indicator::Yellow ||
        indicator(Status::NotListed, 12 * hour + 1) != Indicator::Red ||
        indicator(Status::Listed, 100 * hour) != Indicator::Green ||
        indicator(Status::Unknown, 0) != Indicator::Gray ||
        indicator(Status::Unknown, 13 * hour) != Indicator::Gray ||
        indicator(Status::NotListed, -1000) != Indicator::Green ||
        Watchlist::IndicatorFor(Status::NotListed, 0, now) != Indicator::Gray ||
        Watchlist::IndicatorFor(Status::Listed, 0, now) != Indicator::Gray)
        return false;
    auto config = makeConfig();
    config->Write("/Reporting/FreeDV/Watchlist/Records", "K1ABC|500|600");
    Watchlist list;
    list.load(config.get());
    list.save(config.get());
    const auto before = contentsOf(*config);
    for (auto age : {hour - 1, hour, 12 * hour, 12 * hour + 1})
        Watchlist::IndicatorFor(Status::NotListed, list.Records().at("K1ABC").lastSeen, age);
    list.save(config.get());
    return before == contentsOf(*config) && list.Records().at("K1ABC").lastSeen == 500;
}
}

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return -1;
    executeTestCase("normalizationAndDuplicates", normalizationAndDuplicates);
    executeTestCase("sessionsAndConnectivity", sessionsAndConnectivity);
    executeTestCase("automaticReconnectWithoutConnectCallback", automaticReconnectWithoutConnectCallback);
    executeTestCase("monotonicTimestampsAndMeaningfulObservations", monotonicTimestampsAndMeaningfulObservations);
    executeTestCase("timezoneAndInvalidTimestamps", timezoneAndInvalidTimestamps);
    executeTestCase("persistenceAndIndefiniteRetention", persistenceAndIndefiniteRetention);
    executeTestCase("addAnAlreadyPresentSession", addAnAlreadyPresentSession);
    executeTestCase("corruptDatesAndDuplicateStoredRecords", corruptDatesAndDuplicateStoredRecords);
    executeTestCase("failedSaveRetriesAndBatchesNotify", failedSaveRetriesAndBatchesNotify);
    executeTestCase("profilesRemainIndependent", profilesRemainIndependent);
    executeTestCase("timezoneAbbreviationsAndDst", timezoneAbbreviationsAndDst);
    executeTestCase("indicatorBoundariesAndReadOnlyHistory", indicatorBoundariesAndReadOnlyHistory);
    return testResult();
}
