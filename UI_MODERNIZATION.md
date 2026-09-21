# UI modernization

## Purpose

Improve the FreeDV desktop UI/UX while preserving modem, audio, radio-control,
reporting, and other operational behavior as closely as practical. This is an
engineering and design record, not a replacement for README.md or USER_MANUAL.md.

The work started with workspace behavior and window management before moving on
to appearance. The visual work is now frozen. The current implementation includes
Light and Dark appearance, typography, signal-display styling, updated primary
controls, improved spacing where it was useful, and a small amount of matching
treatment in FreeDV Reporter.

## UI modernization changes at a glance

- Added an Independent Windows workspace using the existing six FreeDV signal
  displays and operational controls rather than duplicated implementations.
- Added persistent Independent Control and display geometry, visibility, workspace
  selection, multi-monitor recovery, and transient Frm Mic presentation during
  transmit and Voice Keyer operation.
- Added native window snapping for the Independent workspace while retaining
  normal platform window movement and resizing.
- Added W2MWS build attribution to the modernization fork. This is fork-specific
  and is not intended to be included in an upstream contribution.
- Added native Light and Dark appearance selection with persisted startup
  preference and a session-only command-line Dark override.
- Replaced the SNR and Level indicators with platform-neutral gauges while
  preserving their existing operational ranges and semantics.
- Unified Waterfall, Spectrum, waveform, SNR plot, and gauge presentation under
  the existing Multicolor, Black & White, and Blue Tint Signal Display Style
  preference.
- Improved signal visualization with magnitude-aware waveform coloring, stronger
  waveform edge definition, 2-pixel Spectrum and SNR traces, translucent plot
  fills, and a theme-accent frame around signal plot areas.
- Restored a high-visibility Multicolor SNR gauge palette after integration of
  the shared signal-display colors.
- Corrected initial Spectrum buffer state so an idle display starts at the
  minimum-magnitude floor instead of appearing as maximum signal.
- Added a restrained typography hierarchy to the main controls using native
  platform fonts with emphasized group headings and a stronger Independent
  Control heading.
- Added compatibility guards and helpers required by the supported wxWidgets
  versions, including wxWidgets 3.0 DPI scaling and wxWidgets 3.2 appearance
  handling, without introducing platform-specific UI implementations.
- Gave the five primary Control buttons a little more visual weight while keeping
  their existing native rendering and operational state colors.
- Added Local and UTC clocks to the main controls. The same Time group moves with
  the existing controls between Notebook and Independent layouts.
- Added a little more inset around the Stats fields so the text no longer sits
  against the edge of the group.
- Emphasized the FreeDV Reporter column headings using the public wxWidgets
  DataView header-attribute API. Reporter rows and their existing state colors
  are unchanged.
- Preserved modem, DSP, audio, radio-control, Reporter, configuration, and
  operational event behavior while concentrating modernization changes in the
  presentation and workspace layers.

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
Independent Control Displays checkboxes reflect user-selected visibility;
closing a frame unchecks its selector without destroying the plot. Select the
workspace using the checkable Tools > Independent Windows item or the Workspace
radio buttons (Notebook / Independent). Notebook places Workspace above the left
status groups; Independent Control places it beside Displays in the lower
supporting area. All selectors reflect the active workspace and use the same
transition path. They are disabled during transmission/changeover, Voice Keyer
operation or recording, and application shutdown.

Workspace persistence restores the last active presentation. With no saved
Independent visibility, entry shows only Waterfall. Once Independent visibility
has been captured and saved, entry restores the exact selection, including zero
visible displays. These rules also apply to switching presentations within a
session; Frm Mic operational requests do not alter user-selected visibility.

In Independent mode, an operational Frm Mic request during PTT or Voice Keyer
temporarily shows the existing frame if the user has it hidden. Its checkbox
remains unchecked, and this transient presentation does not become persisted
visibility. Frm Mic automatically hides when the operation ends. If it was already
user-visible, it remains visible throughout and after the operation.

Checking Frm Mic while it is transiently visible promotes it to normal
user-selected visibility, so it remains visible afterward. X-closing transient
Frm Mic keeps it hidden for the rest of that operation; the next operation may
show it again. Notebook Frm Mic selection and return behavior, including split-group
handling, remain unchanged.

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

Independent Frm Mic transient presentation was manually validated on Windows for:

- Temporary display during normal PTT and automatic hide on return to RX.
- Promotion to persistent visibility by checking Frm Mic during TX.
- X-close suppression for the remainder of that TX, followed by automatic display
  on the next TX.
- Already-visible Frm Mic remaining visible through TX/RX.
- Transient presentation during Voice Keyer recording.

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
shutdown capture user-selected visibility even when a display is hidden or Frm Mic
is transiently shown.

Phase 4 persistence has passed focused compilation, source-level checks, and the
Windows runtime checks listed above. Recovery after physically disconnecting a
monitor and GTK/Wayland placement remain unvalidated.

## Visual modernization - frozen

The visual modernization pass is complete and is now frozen. Further appearance
changes should only be made if testing turns up a specific problem. The remaining
work is validation, documentation, and preparation of the changes for possible
upstream contribution.

The visual work was built on top of the completed workspace architecture. Native
wxWidgets appearance selection now provides coherent Light and Dark presentation
for application windows and native controls. The selected
appearance is persisted through the existing FreeDV configuration store, with
Appearance selectors available in both Notebook and Independent Control
presentations. The `--dark-mode` command-line option remains available as a
session-only override.

Appearance changes take effect on the next launch rather than attempting live
restyling. This keeps behavior consistent across supported platforms, including
Windows where wxWidgets does not support changing application appearance after
top-level windows have been created.

The SNR and Level indicators now use a custom platform-neutral gauge. The SNR
presentation range is aligned with the existing SNR plot range of -10 through
+35 dB, while the Level indicator preserves its existing 0-100 peak-level
behavior. These changes are presentation-only and do not alter DSP, audio-level,
or modem behavior.

Signal-display colors are centralized in the theme layer and follow the existing
FreeDV display styles: Multicolor, Black & White, and Blue Tint. Their numeric
identities remain compatible with the existing `/Waterfall/Color` setting. The
selected style is applied to all signal displays when configuration is loaded
and is synchronized immediately when the Display preference changes. The
historic display palette remains available without modification for intensity
rendering such as Waterfall. Plot traces use a higher-visibility derivative of
the selected palette so thin or low-level signals remain readable against dark
plot backgrounds without introducing an unrelated color scheme.

Frm Radio, Frm Mic, and Frm Decoder use symmetric magnitude-based coloring. Their
filled waveform presentation retains the selected signal-display palette while
using a visible low-magnitude center color and progressing through the palette
toward stronger positive and negative excursions.

Spectrum uses value-based coloring across its existing dB range with a 2-pixel
trace and a translucent color-matched fill extending to the plot baseline. SNR
uses the same 2-pixel trace and translucent-fill treatment across its existing
-10 through +35 dB range. The fill is presentation-only; plot scales, sampling,
scrolling, averaging, and signal data remain unchanged. Waterfall and Spectrum
buffers are initialized to the existing minimum-magnitude floor before their
first paint so an idle Spectrum starts in the quiet state rather than displaying
zero-initialized bins as maximum signal.

Windows runtime testing has covered Light and Dark startup, persisted appearance
selection, the command-line Dark override, workspace-specific selector placement,
semantic transmit-state coloring, the updated SNR and Level indicators, live
signal-display style changes, persisted signal-display style selection, and
Multicolor, Black & White, and Blue Tint rendering in Independent Windows. Full
Windows cross-builds pass after these changes.

The main application now uses a restrained typography hierarchy based on the
native platform GUI font. Independent Control uses the Heading role for its
primary title, while operational group headings use the Emphasized role. Body
controls retain native platform typography rather than applying a blanket custom
font treatment.

The five primary Control buttons -- Start/Stop Modem, Analog/Digital, Tune,
Voice Keyer, and XMIT -- use the Emphasized typography role and a 36 DIP minimum
height. They continue to use native button rendering and the existing operational
colors, including transmit, Tune, and Voice Keyer state indications.

A Time group now shows Local and UTC time in 24-hour HH:MM:SS format and updates
once per second. It appears below FDV Reporting in Notebook and below Radio
Frequency in Independent Control. This is the same control group moved between
the two layouts, not separate implementations.

The Stats group keeps its existing fields and behavior. In particular, some
values are expected to show `unk` while using RADE because those statistics are
not supplied in that mode. The visual change is deliberately small: the fields
have additional inset from the Stats group border.

Waveform plots now add a 2-pixel high-visibility outline derived from the same
magnitude-dependent Signal Display Style color as the underlying signal. Signal
plot areas also use a thin theme-accent frame across Waterfall, Spectrum,
Frm Radio, Frm Mic, Frm Decoder, and SNR. The treatment is intentionally more
prominent in Dark appearance and subtler in Light appearance.

FreeDV Reporter keeps its existing dense table layout, row colors, sorting,
filtering, and controls. Its column headings now use the Emphasized typography
role through `wxDataViewCtrl::SetHeaderAttr()` when building with wxWidgets 3.1
or newer. Builds using wxWidgets 3.0 skip this cosmetic enhancement and retain
the native Reporter header appearance.

Broader spacing changes were considered but deliberately avoided. The existing
density works well, particularly in Independent Control, and increasing padding
throughout the application would make the controls larger without adding much
clarity.

The existing theme foundation remains the central source for visual tokens and
platform-neutral styling. Standard wxWidgets behavior and public APIs are
preferred where practical, with narrowly scoped platform-specific treatment only
when required for equivalent appearance or behavior.

## Icebox

The following workspace enhancements are intentionally deferred and do not block
Phase 6:

- Persistent docking relationships and group movement.
- Automatic window arrangement.
- Vertical Independent Control orientation.

These features should build on the existing workspace and snapping architecture
without changing the independent-window model established through Phase 5A.

## Remaining validation

The visual design itself is frozen, but there are still a few validation items
before calling the branch ready for upstream review.

Remaining checks include physically missing/disconnected-monitor recovery,
configuration reload/reset, and minimum usable sizes. Linux CI has validated
source compatibility with the older supported wxWidgets configuration, and macOS
CI builds have completed successfully. A macOS build has also been installed and
run successfully with Dark appearance and Independent Windows. macOS
window-snapping behavior still needs another look, and native Linux runtime
validation remains outstanding.

The latest primary-control, clock, Stats-spacing, and Reporter-header changes
have been built and checked on Windows. A final CI run on the frozen branch is
still needed before preparing the upstream contribution. Any unrelated RADE
runtime-test failures should be compared with current upstream behavior rather
than treated automatically as UI regressions.

## Current known limitations

Windows without saved geometry may initially use default or stacked placement.
Persistent docking relationships, group movement, automatic arrangement, and
vertical Control orientation remain unimplemented. Physically
missing/disconnected-monitor recovery and GTK/Wayland placement have not been
runtime-validated. macOS Independent Windows are functional, but snapping
behavior still requires refinement. Native Linux runtime validation remains
outstanding.
