// Presentation coordination for the existing FreeDV plots.
#ifndef FREEDV_DISPLAY_WORKSPACE_H
#define FREEDV_DISPLAY_WORKSPACE_H

#include <array>
#include <cstddef>

class wxAuiNotebook;
class wxWindow;

// Stable identities, independent of notebook order and translated captions.
enum class DisplayId
{
    Waterfall = 0,
    Spectrum = 1,
    FrmRadio = 2,
    FrmMic = 3,
    FrmDecoder = 4,
    SNR = 5,
    Count
};

// UI-thread-only, non-owning presentation controller. The notebook continues
// to own all six controls. Registration does not add, move, or select pages.
class DisplayWorkspace
{
public:
    explicit DisplayWorkspace(wxAuiNotebook& notebook);

    void RegisterDisplay(DisplayId id, wxWindow& plot);
    void ShowDisplay(DisplayId id);

    // Keep the existing notebook-index return value for currentNotebookTab.
    // Capture the active page in Frm Mic's split group, not just the notebook's
    // global selection. Configuration ownership remains with MainFrame.
    int CaptureMicReturnPage() const;
    void RestoreAfterMic(long page);

    // Preserve the existing RX-return repaint of every notebook page.
    void RefreshAll();

private:
    wxAuiNotebook& notebook_;
    std::array<wxWindow*, static_cast<std::size_t>(DisplayId::Count)> plots_{};
};

#endif // FREEDV_DISPLAY_WORKSPACE_H
