#include "callsign_watchlist.h"
#include "gui/theme/FreeDVTheme.h"
#include "gui/util/WindowPositionRestore.h"
#include "gui/util/DpiUtils.h"

#include <algorithm>
#include <cmath>
#include <wx/dcclient.h>
#include <wx/image.h>
#include <wx/imaglist.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/config.h>
#include <wx/listctrl.h>
#include <wx/log.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace
{
wxString StatusText(CallsignWatchlist::Status status)
{
    switch (status)
    {
        case CallsignWatchlist::Status::Listed: return _("Listed");
        case CallsignWatchlist::Status::NotListed: return _("Not Listed");
        case CallsignWatchlist::Status::Unknown: return _("Unknown");
    }
    return _("Unknown");
}
}

CallsignWatchlistWindow::CallsignWatchlistWindow(wxWindow* parent, FreeDVConfiguration& configuration)
    : wxFrame(parent, wxID_ANY, _("Callsign Watchlist"), wxDefaultPosition, wxSize(760, 400))
    , configuration_(configuration)
    , appearanceTimer_(this)
{
    SetName("watchlist");
    auto* panel = new wxPanel(this);
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* controls = new wxBoxSizer(wxHORIZONTAL);
    entry_ = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    entry_->SetName("watchlist.callsign");
    entry_->SetHint(_("Callsign (including stations not yet seen)"));
    add_ = new wxButton(panel, wxID_ANY, _("Add"));
    add_->SetName("watchlist.add");
    remove_ = new wxButton(panel, wxID_ANY, _("Remove"));
    remove_->SetName("watchlist.remove");
    filter_ = new wxChoice(panel, wxID_ANY);
    filter_->SetName("watchlist.filter");
    filter_->Append(_("All"));
    filter_->Append(_("Currently Listed"));
    filter_->SetSelection(0);
    controls->Add(entry_, 1, wxRIGHT, ::FromDIP(this, 6));
    controls->Add(add_, 0, wxRIGHT, ::FromDIP(this, 6));
    controls->Add(remove_, 0, wxRIGHT, ::FromDIP(this, 6));
    controls->Add(filter_, 0);
    layout->Add(controls, 0, static_cast<int>(wxEXPAND) | static_cast<int>(wxALL), ::FromDIP(this, 8));
    list_ = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->SetName("watchlist.entries");
    list_->InsertColumn(0, _("Callsign"), wxLIST_FORMAT_LEFT, ::FromDIP(this, 125));
    list_->InsertColumn(1, _("Status"), wxLIST_FORMAT_LEFT, ::FromDIP(this, 110));
    list_->InsertColumn(2, _("Last Seen (local)"), wxLIST_FORMAT_LEFT, ::FromDIP(this, 235));
    list_->InsertColumn(3, _("Last Update (local)"), wxLIST_FORMAT_LEFT, ::FromDIP(this, 235));
    layout->Add(list_, 1, static_cast<int>(wxEXPAND) | static_cast<int>(wxLEFT) | static_cast<int>(wxRIGHT), ::FromDIP(this, 8));
    auto* explanation = new wxStaticText(panel, wxID_ANY,
        _(L"Green: Listed/recent | Yellow: 1\u201312h | Red: >12h | Gray: Unknown/never seen"));
    explanation->SetName("watchlist.legend");
    explanation->SetToolTip(_(
        L"Listed means a current Reporter session exists, including stations hidden by Reporter filters.\n"
        L"Not Listed means the Reporter is available and has no session for the callsign.\n"
        L"Unknown means the Reporter is unavailable or synchronizing; indicators are gray.\n"
        L"Green means Listed, or Not Listed and last seen less than one hour ago.\n"
        L"Yellow means Not Listed and last seen 1\u201312 hours ago; red means more than 12 hours ago.\n"
        L"Never-seen callsigns are gray. Recent activity alone does not mean currently connected.\n"
        L"Last Seen records the last Reporter observation; Last Update records the last observed status or metadata change.\n"
        L"Timestamps use local time for each observation, including daylight-saving time.\n"
        L"History is kept until you remove the callsign."));
    layout->Add(explanation, 0, wxALL, ::FromDIP(this, 8));
    panel->SetSizer(layout);
    auto* frameLayout = new wxBoxSizer(wxVERTICAL);
    frameLayout->Add(panel, 1, wxEXPAND);
    SetSizer(frameLayout);
    SetMinSize(wxSize(std::max(::FromDIP(this, 520),
        explanation->GetBestSize().x + ::FromDIP(this, 24)), ::FromDIP(this, 240)));

    add_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddCallsign(); });
    entry_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { AddCallsign(); });
    remove_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveSelected(); });
    filter_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RefreshRows(); });
    list_->Bind(wxEVT_LIST_COL_CLICK, [this](wxListEvent& event) {
        const int column = event.GetColumn();
        ascending_ = column == sortColumn_ ? !ascending_ : true;
        sortColumn_ = column;
        RefreshRows();
    });
    list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { remove_->Enable(true); });
    list_->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { remove_->Enable(false); });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { UpdateTimedAppearance(); }, appearanceTimer_.GetId());
    Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
        if (!shuttingDown_) configuration_.watchlistWindowVisible = event.IsShown();
        if (event.IsShown() && !shuttingDown_)
        {
            UpdateTimedAppearance();
            appearanceTimer_.Start(1000);
        }
        else appearanceTimer_.Stop();
        event.Skip();
    });
    Bind(wxEVT_MOVE, [this](wxMoveEvent& event) { CaptureGeometry(); event.Skip(); });
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { CaptureGeometry(); event.Skip(); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        CaptureGeometry();
        if (!shuttingDown_) SaveGeometry();
        if (!shuttingDown_)
        {
            configuration_.watchlistWindowVisible = false;
            Hide();
            appearanceTimer_.Stop();
            if (event.CanVeto()) event.Veto();
        }
        // Retain ownership until MainFrame has exported its configuration.
        else if (event.CanVeto()) event.Veto();
    });
    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
        RefreshAppearance();
        event.Skip();
    });
    configuration_.callsignWatchlist.SetChangedHandler([this]() {
        if (IsShown() && !shuttingDown_) RefreshRows();
    });
    RefreshAppearance();
}

CallsignWatchlistWindow::~CallsignWatchlistWindow()
{
    shuttingDown_ = true;
    appearanceTimer_.Stop();
    configuration_.callsignWatchlist.SetChangedHandler({});
}

void CallsignWatchlistWindow::PrepareForShutdown()
{
    if (shuttingDown_) return;
    configuration_.watchlistWindowVisible = IsShown();
    CaptureGeometry();
    tracking_ = false;
    shuttingDown_ = true;
    appearanceTimer_.Stop();
    configuration_.callsignWatchlist.SetChangedHandler({});
}

void CallsignWatchlistWindow::RestoreGeometry()
{
    tracking_ = false;
    if (IsIconized() || IsMaximized()) Restore();
    normalGeometry_ = RestoreWindowGeometry(this,
        wxRect(configuration_.watchlistWindowLeft, configuration_.watchlistWindowTop,
               configuration_.watchlistWindowWidth, configuration_.watchlistWindowHeight), GetMinSize());
    tracking_ = true;
}

void CallsignWatchlistWindow::CaptureGeometry()
{
    if (!tracking_ || shuttingDown_ || IsBeingDeleted() || !IsShown() || IsIconized() || IsMaximized())
        return;
    const auto rect = GetRect();
    if (!rect.IsEmpty()) normalGeometry_ = rect;
    if (!normalGeometry_.IsEmpty())
    {
        configuration_.watchlistWindowLeft = normalGeometry_.x;
        configuration_.watchlistWindowTop = normalGeometry_.y;
        configuration_.watchlistWindowWidth = normalGeometry_.width;
        configuration_.watchlistWindowHeight = normalGeometry_.height;
    }
}

void CallsignWatchlistWindow::SaveGeometry()
{
    if (normalGeometry_.IsEmpty()) return;
    if (auto* config = wxConfigBase::Get(false))
    {
        config->Write(configuration_.watchlistWindowLeft.getElementName(), static_cast<long>(normalGeometry_.x));
        config->Write(configuration_.watchlistWindowTop.getElementName(), static_cast<long>(normalGeometry_.y));
        config->Write(configuration_.watchlistWindowWidth.getElementName(), static_cast<long>(normalGeometry_.width));
        config->Write(configuration_.watchlistWindowHeight.getElementName(), static_cast<long>(normalGeometry_.height));
        config->Flush();
    }
}

void CallsignWatchlistWindow::Open(bool activate)
{
    if (shuttingDown_) return;
    configuration_.watchlistWindowVisible = true;
    RestoreGeometry(); // Recover again if monitors changed while hidden.
    if (activate)
    {
        Show();
        Raise();
    }
    else ShowWithoutActivating();
    RefreshAppearance();
}

void CallsignWatchlistWindow::AddCallsign()
{
    auto& model = configuration_.callsignWatchlist;
    const auto callsign = CallsignWatchlist::Normalize(entry_->GetValue());
    if (callsign.empty())
    {
        entry_->SetToolTip(_("Enter a callsign using letters, digits, slash or hyphen."));
        entry_->SetFocus();
        return;
    }
    model.Add(callsign);
    entry_->ChangeValue(wxEmptyString);
    if (!model.Commit(wxConfigBase::Get(false)))
        wxLogWarning("Could not save callsign watchlist; history remains pending in memory.");
    RefreshRows();
}

void CallsignWatchlistWindow::RemoveSelected()
{
    const auto selection = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selection < 0) return;
    auto& model = configuration_.callsignWatchlist;
    model.Remove(list_->GetItemText(selection));
    if (!model.Commit(wxConfigBase::Get(false)))
        wxLogWarning("Could not save callsign watchlist; history remains pending in memory.");
    RefreshRows();
}

void CallsignWatchlistWindow::RefreshAppearance()
{
    if (shuttingDown_) return;
    const auto palette = FreeDVTheme::GetAppearancePalette(
        static_cast<FreeDVTheme::AppearanceMode>(configuration_.appearanceMode.get()));
    SetBackgroundColour(palette.windowBackground);
    SetForegroundColour(palette.primaryText);
    for (auto* child : GetChildren())
    {
        child->SetBackgroundColour(palette.windowBackground);
        child->SetForegroundColour(palette.primaryText);
        for (auto* control : child->GetChildren())
        {
            control->SetForegroundColour(palette.primaryText);
            control->SetBackgroundColour(palette.panelSurface);
        }
    }
    entry_->SetBackgroundColour(palette.windowBackground);
    list_->SetBackgroundColour(palette.windowBackground);
    RebuildIndicatorImages();
    RefreshRows();
    Refresh();
}

void CallsignWatchlistWindow::RefreshRows()
{
    if (shuttingDown_) return;
    auto& model = configuration_.callsignWatchlist;
    wxString selected;
    const auto selection = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selection >= 0) selected = list_->GetItemText(selection);
    std::vector<wxString> rows;
    for (const auto& record : model.Records())
        if (filter_->GetSelection() == 0 || model.GetStatus(record.first) == CallsignWatchlist::Status::Listed)
            rows.push_back(record.first);
    std::sort(rows.begin(), rows.end(), [this, &model](const wxString& left, const wxString& right) {
        int comparison = 0;
        if (sortColumn_ == 1)
            comparison = StatusText(model.GetStatus(left)).Cmp(StatusText(model.GetStatus(right)));
        else if (sortColumn_ >= 2)
        {
            const auto& l = model.Records().at(left);
            const auto& r = model.Records().at(right);
            const auto a = sortColumn_ == 2 ? l.lastSeen : l.lastUpdate;
            const auto b = sortColumn_ == 2 ? r.lastSeen : r.lastUpdate;
            comparison = (a > b) - (a < b);
        }
        if (comparison == 0) comparison = left.Cmp(right);
        return ascending_ ? comparison < 0 : comparison > 0;
    });
    const auto palette = FreeDVTheme::GetAppearancePalette(
        static_cast<FreeDVTheme::AppearanceMode>(configuration_.appearanceMode.get()));
    const auto blend = [](unsigned char base, unsigned char accent) {
        return static_cast<unsigned char>((base * 4 + accent) / 5);
    };
    const wxColour highlight(blend(palette.windowBackground.Red(), palette.accent.Red()),
                             blend(palette.windowBackground.Green(), palette.accent.Green()),
                             blend(palette.windowBackground.Blue(), palette.accent.Blue()));
    const auto now = CallsignWatchlist::Now();
    list_->Freeze();
    list_->DeleteAllItems();
    rowAppearances_.clear();
    for (const auto& callsign : rows)
    {
        const auto& record = model.Records().at(callsign);
        const auto status = model.GetStatus(callsign);
        const auto indicator = CallsignWatchlist::IndicatorFor(status, record.lastSeen, now);
        const auto row = list_->InsertItem(list_->GetItemCount(), callsign, static_cast<int>(indicator));
        list_->SetItem(row, 1, StatusText(status));
        list_->SetItem(row, 2, CallsignWatchlist::FormatLocalTime(record.lastSeen));
        list_->SetItem(row, 3, CallsignWatchlist::FormatLocalTime(record.lastUpdate));
        const auto remaining = model.HighlightRemainingMs(callsign);
        rowAppearances_.push_back({callsign, status, indicator, remaining > 0});
        list_->SetItemBackgroundColour(row, remaining > 0 ? highlight : palette.windowBackground);
        list_->SetItemTextColour(row, palette.primaryText);
        auto font = list_->GetFont();
        // Status text plus weight keeps the indicator accessible without colour.
        font.SetWeight(status == CallsignWatchlist::Status::Listed ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
        list_->SetItemFont(row, font);
        if (callsign == selected) list_->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }
    list_->Thaw();
    FitTimestampColumns();
    remove_->Enable(list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) >= 0);
}

void CallsignWatchlistWindow::RebuildIndicatorImages()
{
    const auto palette = FreeDVTheme::GetAppearancePalette(
        static_cast<FreeDVTheme::AppearanceMode>(configuration_.appearanceMode.get()));
    const int size = ::FromDIP(this, 16);
    auto* images = new wxImageList(size, size, true);
    // Image indices match CallsignWatchlist::Indicator. Only a real circular
    // bitmap is drawn; callsign text and the separate Status column stay intact.
    for (const auto& colour : {palette.success, palette.warning, palette.danger, palette.secondaryText})
    {
        wxImage image(size, size);
        image.InitAlpha();
        const double radius = (size - 4) / 2.0;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const double dx = x + 0.5 - size / 2.0, dy = y + 0.5 - size / 2.0;
                const double distance = std::sqrt(dx * dx + dy * dy);
                const auto& pixel = distance > radius - 1 ? palette.border : colour;
                image.SetRGB(x, y, pixel.Red(), pixel.Green(), pixel.Blue());
                image.SetAlpha(x, y, static_cast<unsigned char>(
                    255 * std::clamp(radius + 0.5 - distance, 0.0, 1.0)));
            }
        }
        images->Add(wxBitmap(image));
    }
    list_->AssignImageList(images, wxIMAGE_LIST_SMALL);
}

void CallsignWatchlistWindow::UpdateTimedAppearance()
{
    if (shuttingDown_ || !IsShown()) return;
    const auto& model = configuration_.callsignWatchlist;
    const auto now = CallsignWatchlist::Now();
    const auto palette = FreeDVTheme::GetAppearancePalette(
        static_cast<FreeDVTheme::AppearanceMode>(configuration_.appearanceMode.get()));
    for (std::size_t row = 0; row < rowAppearances_.size(); ++row)
    {
        auto& appearance = rowAppearances_[row];
        const auto record = model.Records().find(appearance.callsign);
        if (record == model.Records().end()) continue;
        const auto indicator = CallsignWatchlist::IndicatorFor(appearance.status, record->second.lastSeen, now);
        if (indicator != appearance.indicator)
        {
            list_->SetItemImage(static_cast<long>(row), static_cast<int>(indicator));
            appearance.indicator = indicator;
        }
        if (appearance.highlighted && model.HighlightRemainingMs(appearance.callsign) == 0)
        {
            list_->SetItemBackgroundColour(static_cast<long>(row), palette.windowBackground);
            appearance.highlighted = false;
        }
    }
    // No row reconstruction, sorting, selection changes, configuration writes,
    // or timestamp mutations on this one-second timer. Only changed cells paint.
}

void CallsignWatchlistWindow::FitTimestampColumns()
{
    wxClientDC dc(list_);
    for (int column : {2, 3})
    {
        dc.SetFont(list_->GetFont());
        wxListItem header;
        list_->GetColumn(column, header);
        int width = dc.GetTextExtent(header.GetText()).GetWidth();
        for (long row = 0; row < list_->GetItemCount(); ++row)
        {
            dc.SetFont(list_->GetItemFont(row));
            width = std::max(width, dc.GetTextExtent(list_->GetItemText(row, column)).GetWidth());
        }
        width += ::FromDIP(this, 28);
        if (list_->GetColumnWidth(column) < width) list_->SetColumnWidth(column, width);
    }
}
