#include "DisplayWorkspace.h"
#include "DisplayFrame.h"

#include <wx/aui/auibook.h>
#include <algorithm>

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

    if (IsIndependent())
    {
        // A manually hidden frame stays hidden until the next presentation switch.
        plot->Refresh();
        return;
    }

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
    if (IsIndependent())
        return micReturnPage_;
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
    if (!IsIndependent())
        notebook_.ChangeSelection(page);
}

void DisplayWorkspace::RefreshAll()
{
    if (IsIndependent())
    {
        for (auto* plot : plots_)
            if (plot != nullptr)
                plot->Refresh();
        return;
    }
    for (std::size_t index = 0; index < notebook_.GetPageCount(); ++index)
    {
        notebook_.GetPage(index)->Refresh();
    }
}

void DisplayWorkspace::SetLayoutHandlers(std::function<wxString()> save,
                                         std::function<void(const wxString&)> restore)
{
    saveLayout_ = std::move(save);
    restoreLayout_ = std::move(restore);
}

wxString DisplayWorkspace::GetNotebookLayout() const
{
    return IsIndependent() ? layout_ : saveLayout_();
}

DisplayFrame* DisplayWorkspace::FrameFor(wxWindow* plot) const
{
    for (std::size_t i = 0; i < plots_.size(); ++i)
        if (plots_[i] == plot)
            return frames_[i];
    return nullptr;
}

bool DisplayWorkspace::SetIndependent(bool independent)
{
    if (switching_)
        return false;
    if (independent == IsIndependent())
        return true;

    struct TransitionGuard
    {
        bool& flag;
        explicit TransitionGuard(bool& value) : flag(value) { flag = true; }
        ~TransitionGuard() { flag = false; }
    } guard(switching_);

    if (!independent)
        return ReturnToNotebook();

    if (!saveLayout_ || !restoreLayout_ || notebook_.GetPageCount() != plots_.size())
        return false;
    pages_.clear();
    for (std::size_t i = 0; i < notebook_.GetPageCount(); ++i)
    {
        auto* plot = notebook_.GetPage(i);
        if (std::find(plots_.begin(), plots_.end(), plot) == plots_.end())
            return false;
        pages_.push_back({plot, notebook_.GetPageText(i)});
    }
    selection_ = notebook_.GetSelection();
    micReturnPage_ = CaptureMicReturnPage();
    layout_ = saveLayout_();

    // Create all hosts before removing anything. The owner retains them even
    // when empty, so rollback never destroys a plot through a temporary host.
    for (const auto& page : pages_)
    {
        const auto index = std::find(plots_.begin(), plots_.end(), page.plot) - plots_.begin();
        if (frames_[index] == nullptr)
            frames_[index] = new DisplayFrame(wxGetTopLevelParent(&notebook_), page.caption,
                                             notebook_.GetClientSize());
        frames_[index]->SetTitle(page.caption);
    }

    // Keep the snapshot authoritative even if recovery encounters another
    // failure. Every plot remains parented to a live notebook or live frame.
    presentation_ = Presentation::Independent;
    for (const auto& page : pages_)
    {
        if (!notebook_.RemovePage(0) || !FrameFor(page.plot)->Attach(*page.plot))
        {
            ReturnToNotebook();
            return false;
        }
    }
    notebook_.Hide();
    notebook_.GetParent()->Layout();
    notebook_.GetParent()->Refresh();
    for (auto* frame : frames_)
        frame->Show();
    RefreshAll();
    return true;
}

bool DisplayWorkspace::ReturnToNotebook()
{
    for (auto* frame : frames_)
        if (frame != nullptr)
            frame->Hide();

    // Also works after a partially completed transfer: retain pages already
    // in the correct slot and insert each missing page in snapshot order.
    for (std::size_t i = 0; i < pages_.size(); ++i)
    {
        const auto& page = pages_[i];
        if (i < notebook_.GetPageCount() && notebook_.GetPage(i) == page.plot)
            continue;
        auto* frame = FrameFor(page.plot);
        if (!frame->Detach())
        {
            frame->Show();
            notebook_.Show();
            return false;
        }
        if ((page.plot->GetParent() != &notebook_ && !page.plot->Reparent(&notebook_)) ||
            !notebook_.InsertPage(i, page.plot, page.caption, false))
        {
            // Best effort to keep a failed page usable in its existing host.
            frame->Attach(*page.plot);
            frame->Show();
            notebook_.Show();
            return false;
        }
    }
    notebook_.Show();
    restoreLayout_(layout_);
    if (selection_ >= 0 && selection_ < static_cast<int>(notebook_.GetPageCount()))
        notebook_.ChangeSelection(selection_);
    presentation_ = Presentation::Notebook;
    notebook_.GetParent()->Layout();
    notebook_.GetParent()->Refresh();
    RefreshAll();
    return true;
}

bool DisplayWorkspace::HasActiveDisplay() const
{
    if (!IsIndependent())
        return false;
    for (auto* frame : frames_)
        if (frame != nullptr && frame->IsShown() && frame->IsEnabled() && frame->IsActive())
            return true;
    return false;
}

void DisplayWorkspace::FocusOperatingWindow()
{
    if (IsIndependent())
        wxGetTopLevelParent(&notebook_)->SetFocus();
    else
        notebook_.SetFocus();
}
