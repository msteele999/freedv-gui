# UI modernization

## Purpose

Improve the FreeDV desktop UI/UX while preserving modem, audio, radio-control,
reporting, and other operational behavior as closely as practical. This is an
engineering and design record, not a replacement for README.md or USER_MANUAL.md.

Workspace architecture and behavior precede broad visual restyling. Future visual
goals include dark mode, improved typography and font sizing, spacing, visual
hierarchy, control/group presentation, buttons and interactive controls, and a
coherent visual system shared with FreeDV Reporter.

## Design principles

- Preserve existing operational behavior and event handlers wherever practical.
- Prefer presentation-layer changes over functional changes.
- Reuse operational controls; do not maintain synchronized copies.
- Prefer standard cross-platform wxWidgets behavior and APIs. Isolate
  platform-specific code where necessary for equivalent behavior or appearance.
- Preserve semantic state indications, including PTT, Voice Keyer, recording,
  synchronization/status, and Reporter state colors.
- Keep changes suitable for potential upstream contribution where practical.
- Avoid unrelated refactoring.

## Workspace architecture

### Notebook / Tabbed

The existing Notebook workspace remains the default for a fresh installation.
Its six displays are Waterfall, Spectrum, Frm Radio, Frm Mic, Frm Decoder, and
SNR. Preserve the existing responsive wxWidgets sizer behavior. Notebook is a
purpose-built workspace and should not be forced to resemble Independent Windows.

### Independent Windows

`DisplayWorkspace` coordinates the same six plot instances. It transfers them
from the notebook into top-level `DisplayFrame` windows rather than duplicating
them. It retains notebook page order, selection, and layout for the return
transition. Closing a detached frame hides it without destroying its plot.

Each display can be moved, resized, shown, and hidden independently. The
Independent Control Displays checkboxes reflect current frame visibility;
closing a frame unchecks its selector without destroying the plot. The mode
switch remains Tools > Independent displays (test).

Workspace persistence restores the last active presentation. With no saved
Independent visibility, entry shows only Waterfall. Once Independent visibility
has been captured and saved, entry restores the exact selection, including zero
visible displays. These rules also apply to switching presentations within a
session; Frm Mic operational requests do not alter user-selected visibility.

Independent mode uses a purpose-built **FreeDV Control** presentation for the
non-display controls. `TopFrame` moves existing control-group sizers between the
Notebook and Independent layouts, retaining widget parents, handlers, and state.
The inactive Notebook layout is retained for restoration but is not attached to
the Independent layout. The Control presentation contains no notebook, hidden
notebook placeholder, display canvas, or reserved central display area.

The current landscape organization is:

- SNR, Level, and Sync groups at the left.
- A wider central stack of TX Attenuation, Speaker/Mic Level, and Radio Frequency,
  followed by conditional Mode and Squelch controls. Mode selectors are horizontal
  in this presentation.
- A separate vertical Stats group, with Reset above the existing statistic rows.
- Main Control buttons at the right: Start/Stop Modem, Analog/Digital, Tune,
  Voice Keyer, and XMIT.
- A wide station/history row and lower Audio Recording, Logging, and FDV Reporting
  groups.

The editable `m_cboReportFrequency`, current-callsign `m_txtCtrlCallSign`, Clear
button, and `m_cboLastReportedCallsigns` retain their existing semantics and
conditional behavior. The station-history `wxComboCtrl` retains its popup with
Callsign, Frequency, Date/Time, and SNR columns and its relationship to Log QSO.
Control minimum sizing follows the active layout and visible controls.

A vertical Control orientation may be added later by recomposing these same
controls, not by creating a second operational implementation.

### Legacy Modes behavior

The existing `enableLegacyModes` configuration controls visibility of the Mode
group, Squelch, ReSync, and Center RX. When enabled, the live mode selectors are
RADEV1, 700D, 700E, and 1600. These are operational radio buttons, not informational
labels. Existing mode-dependent enabling remains authoritative; for example,
Squelch and the related receive controls are disabled for RADEV1. The workspace
change introduces no new mode-selection behavior.

## Completed checkpoints

These checkpoints are on `feature/ui-modernization`:

| Commit | Checkpoint | Established behavior |
| --- | --- | --- |
| `b9e6ceed` | Add UI theme foundation and display workspace abstraction | Added `FreeDVTheme` palette and typography roles, opt-in window-surface styling for the main application and Reporter, and `DisplayWorkspace` identities and presentation coordination for the six existing plots, including Frm Mic selection/return and refresh operations. |
| `f31ae58b` | Add independent display window support | Added `DisplayFrame` hosts, reversible plot transfer and notebook-layout restoration, close-to-hide and resize repaint behavior, the temporary mode-switch command, and detached-window focus handling for keyboard PTT and operating-window focus. |
| `93d9519f` | Add independent control workspace layout | Added the dedicated landscape Control presentation using shared control-group sizers, Notebook restoration, layout-derived minimum sizing, and integration with workspace switching and configuration reload. |
| `5ca099d0` | Add independent display visibility controls | Added the Independent-only Displays selectors, explicit workspace visibility operations, and X-close synchronization while retaining the existing plot instances. |

The theme foundation includes a session-only `--dark-mode` switch, disabled by
default. Its surface styling does not recursively restyle controls, apply fonts,
or theme native window decorations. It is groundwork for the planned visual
system, not completed application-wide dark mode.

Implementation boundaries are principally
`src/gui/displays/DisplayWorkspace.{h,cpp}`,
`src/gui/displays/DisplayFrame.{h,cpp}`, `src/topFrame.{h,cpp}`, and
`src/gui/theme/FreeDVTheme.{h,cpp}`. Existing application handlers remain responsible
for operational behavior.

## Runtime validation

The earlier workspace and landscape Control checkpoints were manually validated
on Windows for:

- All six detached displays operating live.
- Notebook-to-Independent transitions and return, including repeated transitions.
- Closing detached displays and subsequently returning to Notebook.
- Detached display resizing and repainting.
- Keyboard PTT while a detached display has focus.
- Voice Keyer operation.
- Notebook restoration and responsive Notebook resizing.
- Independent Control layout with Legacy Modes disabled and enabled.
- Mode and Squelch conditional visibility.
- Selecting 700D and starting the modem, with Sync and Stats reflecting live
  operation.

Phase 4 was manually validated on Windows for:

- Notebook restart preserving saved size and multi-monitor position.
- Startup directly into Independent mode preserving Control geometry.
- Independent display geometry restoration across monitors and visibility
  restoration.
- First-use Independent entry showing Waterfall only when no visibility state
  exists, and exact persistence of a saved zero-visible-display selection.
- X-closing a detached display immediately synchronizing its visibility checkbox.
- Independent-to-Notebook-to-Independent switching within one session preserving
  Control geometry, display positions and sizes, and visibility.
- Normal modem/RX operation with live displays after the persistence changes.

A full Windows cross-compiled build also completed successfully. The runtime
checks above are manual validation, not a claim of automated test coverage or
equivalent validation on other platforms.

## Workspace persistence

`FreeDVConfiguration` owns the persisted fields. `MainFrame` captures the outgoing
workspace before switching, restores the incoming workspace, and captures the
active workspace during configuration export/shutdown. Configuration reload first
returns the plots to Notebook, loads the configuration and existing tab layout,
then restores the configured presentation. Notebook tab-layout persistence retains
its existing experimental-feature gate.

Configuration keys:

| Path | State |
| --- | --- |
| `/MainFrame/{left,top,width,height}` | Existing Notebook Main geometry; never updated from Independent Control. |
| `/Windows/Independent/active` | Last active workspace: false for Notebook (default), true for Independent. |
| `/Windows/Independent/visibilitySaved` | Distinguishes first use from a saved selection with all displays hidden. |
| `/Windows/Independent/Control/{left,top,width,height}` | Independent Control geometry. |
| `/Windows/Independent/Displays/<id>/{left,top,width,height,visible}` | Detached display geometry and visibility. |

Display key identities are `Waterfall`, `Spectrum`, `FrmRadio`, `FrmMic`,
`FrmDecoder`, and `SNR`, corresponding to stable `DisplayId` values, not notebook
page order or translated captions. Reporter geometry remains separate.

Frames are attached while hidden so saved visibility can be applied without first
showing all six. Both presentations use wxWidgets monitor work areas for geometry
restoration, accepting negative multi-monitor coordinates and recovering
inaccessible rectangles onto an available monitor. Sizes are bounded to the work area where
possible while respecting the Notebook or Control layout minimum, or a 320 by 240
DIP detached frame minimum. Positions with a usable portion of the title bar on a
work area are preserved, including windows straddling monitors; inaccessible positions are
brought within an available work area. Actual positioning reuses
`RestoreWindowPosition`, including its GTK initial-placement handling.

Minimized or maximized windows retain their previously captured ordinary geometry
rather than saving an iconized or maximized rectangle. Workspace switching and
shutdown capture current visibility even when a display is hidden.

Phase 4 persistence has passed focused compilation, source-level checks, and the
Windows runtime checks listed above. Recovery after physically disconnecting a
monitor and GTK/Wayland placement remain unvalidated.

## Planned work

### 1. Remaining workspace behavior

Implement transient Frm Mic presentation during TX/Voice Keyer without changing
persistent visibility unless the user explicitly selects it. A future vertical
Control orientation should reuse the existing controls and persist its orientation.

### 2. Persistence validation

Remaining checks include physically missing/disconnected-monitor recovery,
configuration reload/reset, and minimum usable sizes. Validate GTK/Wayland
placement and other supported desktop platforms separately; the completed Windows
checks do not establish behavior on those platforms.

### 3. Docking / snapping

After ordinary geometry persistence is reliable, allow Independent Control and
display windows to dock/snap to one another while retaining ordinary independent
window operation. Do not couple the initial persistence implementation to docking.

### 4. Visual modernization

After workspace architecture is mature, develop dark mode, typography/font sizing,
spacing, group treatment, button/control appearance, and visual hierarchy as a
shared visual system for the main application and Reporter. Continue preserving
semantic operational state colors throughout this work.

## Current known limitations

Windows without saved geometry may initially use default or stacked placement.
Docking/snapping, transient Independent Frm Mic presentation, and vertical Control
orientation remain unimplemented. Physically missing/disconnected-monitor recovery
and GTK/Wayland placement have not been runtime-validated. Validation does not yet
cover all supported desktop platforms.
