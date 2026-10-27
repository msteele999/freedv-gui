#ifndef FREEDV_CALLSIGN_WATCHLIST_WINDOW_H
#define FREEDV_CALLSIGN_WATCHLIST_WINDOW_H

#include <wx/frame.h>
#include <wx/timer.h>
#include <vector>
#include "config/FreeDVConfiguration.h"

class wxChoice;
class wxListCtrl;
class wxTextCtrl;
class wxButton;

// MainFrame owns this modeless frame. Closing hides it; the configuration's
// watchlist continues to collect and persist observations in Reporter callbacks.
class CallsignWatchlistWindow : public wxFrame
{
public:
    CallsignWatchlistWindow(wxWindow* parent, FreeDVConfiguration& configuration);
    ~CallsignWatchlistWindow() override;
    void RestoreGeometry();
    void CaptureGeometry();
    void PrepareForShutdown();
    void Open(bool activate = true);
    void RefreshAppearance();

private:
    FreeDVConfiguration& configuration_;
    wxListCtrl* list_;
    wxTextCtrl* entry_;
    wxChoice* filter_;
    wxButton* add_;
    wxButton* remove_;
    wxTimer appearanceTimer_;
    struct RowAppearance
    {
        wxString callsign;
        CallsignWatchlist::Status status;
        CallsignWatchlist::Indicator indicator;
        bool highlighted;
    };
    std::vector<RowAppearance> rowAppearances_;
    wxRect normalGeometry_;
    int sortColumn_ = 0;
    bool ascending_ = true;
    bool tracking_ = false;
    bool shuttingDown_ = false;

    void RefreshRows();
    void UpdateTimedAppearance();
    void RebuildIndicatorImages();
    void FitTimestampColumns();
    void SaveGeometry();
    void AddCallsign();
    void RemoveSelected();
};
#endif
