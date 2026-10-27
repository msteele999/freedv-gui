// Persistent watch records and transient Reporter sessions. UI-thread only.
#ifndef FREEDV_CALLSIGN_WATCHLIST_H
#define FREEDV_CALLSIGN_WATCHLIST_H

#include "WxWidgetsConfigStore.h"
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <utility>

class CallsignWatchlist : public WxWidgetsConfigStore
{
public:
    using Timestamp = long long; // UTC milliseconds since the Unix epoch; 0 = unknown.
    enum class Status { Listed, NotListed, Unknown };
    enum class Indicator { Green, Yellow, Red, Gray };
    struct Record
    {
        Timestamp lastSeen = 0;
        Timestamp lastUpdate = 0;
    };

    static wxString Normalize(wxString callsign);
    static Timestamp Now();
    static Timestamp ParseReporterTime(const wxString& time);
    static wxString FormatLocalTime(Timestamp time);
    static Indicator IndicatorFor(Status status, Timestamp lastSeen, Timestamp now);

    bool Add(const wxString& callsign);
    bool Remove(const wxString& callsign);
    bool Contains(const wxString& callsign) const;
    const std::map<wxString, Record>& Records() const { return records_; }
    Status GetStatus(const wxString& callsign) const;
    int HighlightRemainingMs(const wxString& callsign) const;

    void BeginSync();
    void CompleteSync();
    void Disconnect();
    void Observe(const std::string& sid, const wxString& callsign,
                 const wxString& serverTime, const wxString& kind,
                 const wxString& detail, Timestamp observedAt = Now());
    void RemoveSession(const std::string& sid, const wxString& serverTime);

    void load(wxConfigBase* config) override;
    void save(wxConfigBase* config) override;
    // Called once per queued Reporter batch, even while both windows are hidden.
    // A failed write/flush keeps history dirty so the next batch retries it.
    bool Commit(wxConfigBase* config);
    void SetChangedHandler(std::function<void()> handler) { changed_ = std::move(handler); }

private:
    struct Session
    {
        wxString callsign;
        Record history;
        std::map<wxString, wxString> observations;
    };
    enum class Connection { Unavailable, Synchronizing, Ready };
    Connection connection_ = Connection::Unavailable;
    std::map<wxString, Record> records_;
    std::map<std::string, Session> sessions_;
    std::map<wxString, std::chrono::steady_clock::time_point> highlights_;
    std::function<void()> changed_;
    bool dirty_ = false;
    bool viewDirty_ = false;

    void MergeHistory(Record& target, const Record& source);
    wxString Serialize() const;
};
#endif
