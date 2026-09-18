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

Each display is intended to have independent movement, resizing, visibility, and
persistent state. Movement, resizing, and close-to-hide behavior exist today;
workspace persistence and final visibility management remain planned work. The
current mode switch is Tools > Independent displays (test). Entering Independent
mode currently shows all six frames; the planned first-use policy differs.

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

The current implementation has been manually validated on Windows for:

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

A full Windows cross-compiled build also completed successfully. The runtime
checks above are manual validation, not a claim of automated test coverage or
equivalent validation on other platforms.

## Planned work

### 1. Independent display management

Add final display visibility selectors/checkboxes and synchronize each selector
when its display is closed with X. Implement transient Frm Mic presentation during
TX/Voice Keyer without changing persistent visibility unless the user explicitly
selects it.

### 2. Workspace persistence

Persist the selected Notebook/Independent mode, Notebook Main window geometry,
Independent Control geometry, and geometry and visibility for each of the six
independent displays separately. Preserve Reporter geometry separately as well;
eventually persist Independent Control orientation.

On the first-ever entry into Independent mode with no saved workspace, show
Waterfall by default. Once saved state exists, restore its exact visibility,
including the case of zero visible displays. Recover sensibly when saved windows
belong to missing or disconnected monitors, and clamp restored display geometry
to sensible minimum usable dimensions.

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

Independent display geometry and visibility are not yet persisted across
application restarts. Detached windows may initially use default or stacked
placement. Workspace persistence is intended to address this limitation, including
minimum usable detached-window sizing during persistence/restoration. The current
Control minimum-size handling does not establish a detached-display sizing policy.
