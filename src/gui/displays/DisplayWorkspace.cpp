#include "DisplayWorkspace.h"

#include <wx/aui/auibook.h>

DisplayWorkspace::DisplayWorkspace(wxAuiNotebook& notebook)
    : notebook_(notebook)
{
}

void DisplayWorkspace::RegisterDisplay(DisplayId id, wxWindow& plot)
{
    const auto index = static_cast<std::size_t>(id);
    wxCHECK_RET(index < plots_.size(), "Invalid display identifier");
    wxCHECK_RET(plots_[index] == nullptr, "Display already registered");
    plots_[index] = &plot;
}

void DisplayWorkspace::ShowDisplay(DisplayId id)
{
    const auto index = static_cast<std::size_t>(id);
    wxCHECK_RET(index < plots_.size(), "Invalid display identifier");
    auto* plot = plots_[index];
    wxCHECK_RET(plot != nullptr, "Display not registered");

    // Preserve the existing workaround: GetPageIndex sometimes returns the
    // wrong result for selecting Frm Mic, so find the page by window identity.
    for (std::size_t pageIndex = 0; pageIndex < notebook_.GetPageCount(); ++pageIndex)
    {
        auto* page = notebook_.GetPage(pageIndex);
        if (page == plot)
        {
            notebook_.ChangeSelection(pageIndex);
            page->Refresh();
            break;
        }
    }
}

int DisplayWorkspace::CaptureMicReturnPage() const
{
    auto savedTab = notebook_.GetSelection();

    // More than one split group can be visible. Restore the page that was
    // active in Frm Mic's group, even if another group had global selection.
#if wxCHECK_VERSION(3,1,4)
    auto* mic = plots_[static_cast<std::size_t>(DisplayId::FrmMic)];
    wxAuiTabCtrl* fromMicTabControl = nullptr;
    int fromMicTabIndex = 0;
    if (mic != nullptr && notebook_.FindTab(mic, &fromMicTabControl, &fromMicTabIndex))
    {
        int localActiveIdx = fromMicTabControl->GetActivePage();
        if (localActiveIdx >= 0 && localActiveIdx < (int)fromMicTabControl->GetPageCount())
        {
            wxWindow* activeWindow = fromMicTabControl->GetWindowFromIdx(localActiveIdx);
            savedTab = notebook_.GetPageIndex(activeWindow);
        }
    }
#endif // wxCHECK_VERSION(3,1,4)

    return savedTab;
}

void DisplayWorkspace::RestoreAfterMic(long page)
{
    notebook_.ChangeSelection(page);
}

void DisplayWorkspace::RefreshAll()
{
    for (std::size_t index = 0; index < notebook_.GetPageCount(); ++index)
    {
        notebook_.GetPage(index)->Refresh();
    }
}
