# Callsign Watchlist

Open **Watchlist > Open Callsign Watchlist** in the FreeDV Reporter window.
The resizable window is modeless: closing it hides it, and Reporter continues
collecting watch history. Add callsigns manually, including stations not yet
seen, or use **Add to Watchlist** / **Remove from Watchlist** in Reporter's
callsign context menu. Removing an entry explicitly deletes its stored history.
Entries are trimmed, converted to ASCII uppercase, and deduplicated. Letters,
digits, slash and hyphen are accepted.

Click a column heading to sort; click again to reverse the order. **Currently
Listed** limits this window's view without deleting entries or history.

## Presence and history

- **Listed:** at least one matching active Reporter session exists, even if
  Reporter hides it through its band, idle or column filters.
- **Not Listed:** Reporter has finished synchronizing and has no matching session.
- **Unknown:** Reporter is unavailable, disabled or synchronizing. This says
  nothing about whether the station is actually on the air.

Last Seen records the local observation time of meaningful incoming Reporter
data, including initial synchronization. Unchanged rows and UI timers do not
advance it. Exact duplicate events and older server updates do not refresh it.
Last Update comes only from valid Reporter `last_update` timestamps. Missing or
malformed timestamps never replace a valid date. Dates are stored as UTC Unix
milliseconds and displayed in local time with the date-specific timezone
abbreviation, such as EST or EDT. Today's DST state is never applied to old
records. Linux/macOS use the OS timezone rules; Windows maps the OS timezone
key through the system ICU data and requests its short specific zone name.
Timestamp columns grow to fit their formatted contents. The stored values and
their UTC representation are unchanged.
Sub-millisecond server precision is truncated to milliseconds.

Each watched callsign's newest dates are retained indefinitely. Disconnection,
reconnection, idle filtering, closing the window, and application restarts do
not clear or age out history. A station arriving after synchronization briefly
gets a five-second accent tint; initial synchronization and reconnection do
not generate appearance highlights. Listed rows retain bold text and a status
label after the tint expires. There are no sounds or desktop notifications.

A small themed circular indicator sits beside each callsign:

- Green: Listed, or Not Listed with Last Seen less than one hour ago.
- Yellow: Not Listed, Last Seen between one and twelve hours ago (inclusive).
- Red: Not Listed, Last Seen more than twelve hours ago.
- Gray: never seen or Reporter status Unknown, regardless of age.

The Status column remains the source of current presence: a green dot beside
Not Listed means recent history, not an active connection. Future Last Seen
dates caused by a local clock adjustment count as recent until the clock
catches up. The open window checks once per second and changes only icons
whose category changed, plus any expired row highlight. It does not rebuild
the list, re-sort, disturb selection, or write history on timer ticks. The
timer stops when hidden or shutting down. Appearance notifications regenerate
the icon palette and refresh the rows for Light, Dark and System modes.

## Persistence and windows

The active application configuration stores all records under
`/Reporting/FreeDV/Watchlist/Records`, using the existing configuration store's
comma-separated string-list convention. Each record is
`CALLSIGN|lastSeenUtcMilliseconds|lastUpdateUtcMilliseconds`; zero means unknown.
All watched records are saved and flushed after a Reporter event batch changes
history, regardless of window visibility. Add/remove operations also flush
immediately. Failed persistence is logged and remains pending for retry on the
next existing Reporter queue drain. The presentation timer never participates
in history collection or persistence.

Watchlists belong to their configuration profile, like other FreeDV settings.
Switching profiles flushes the old profile before loading the new one; it does
not delete the old profile's records. Observations can only be collected while
FreeDV is running and connected to Reporter; activity missed while offline
cannot be reconstructed beyond the snapshot supplied on reconnect.

Geometry uses `/Windows/CallsignWatchlist/{left,top,width,height}`. The window
shares MainFrame's existing WindowSnapManager with Reporter, Decoded Speech and
the detached displays. RestoreWindowGeometry handles missing monitors and
changed work areas. Hidden/minimized/maximized transitions do not overwrite its
last ordinary rectangle. Light/Dark selections refresh the Watchlist immediately;
System re-resolves the platform appearance on system-colour notifications.
Other application windows retain their established appearance/restart behavior.
Reporter TX/RX colour preferences are untouched.

## Validation added

`config_CallsignWatchlistTest` covers normalization, duplicates, persistence,
old-history retention, monotonic dates, offset parsing (including DST boundary
values), multiple sessions, connectivity and synchronization transitions,
duplicate/stale observations, corrupted saved dates, and persistence retry.

The six `gui_watchlist_*` scenarios cover management/context menus, sorting,
filtering, collection with hidden windows, restart retention, idle hiding,
duplicate sessions, reconnection, appearance suppression/expiry, Light/Dark/System
notifications, live application appearance selection, and geometry restoration.
The existing Reporter context-menu expectations were updated as well. Additional
unit cases cover Eastern and Pacific DST transitions, a non-US zone, exact
one-hour/twelve-hour indicator boundaries, Unknown/unseen states and read-only
history. GUI coverage includes real timer-driven aging, selection retention,
unchanged saved timestamps, and palette changes for the circular indicators.

These tests have **not been executed**. No CMake configuration or build was run.
Before release, build and run the new suites locally on Windows, macOS and Linux.
Also validate dragging/sizing snaps against all application windows, changing
monitors/DPI, minimizing/maximizing, real OS Light/Dark transitions in System
mode, profile switching, and shutdown while Reporter data is arriving. Native
window decorations and actual desktop-theme switches need platform validation;
the automated system-colour scenario supplies a notification without changing
the desktop's real appearance. The W2MWS release number remains unchanged.

The timestamp abbreviation depends on the OS timezone database. Where no
unambiguous short name is available, ICU may use a compact GMT offset. Windows
systems without the combined system `icu.dll` (or with automatic DST explicitly
disabled) use an exact OS local-time conversion with a compact `UTC+/-HH:MM`
label instead of guessing an abbreviation. The explicit-zone Windows DST unit
cases require system ICU. See [Windows ICU availability](https://learn.microsoft.com/windows/win32/intl/international-components-for-unicode--icu-).
Indicator transitions can be delayed by up to one timer interval, or longer
if the UI thread is busy or the machine is asleep.

## Auxiliary window visibility and compact legend

Watchlist and Decoded Speech visibility are independent configuration booleans:
`/Windows/CallsignWatchlist/visible` and `/Windows/DecodedSpeech/visible`.
Both default to closed for first launch and configurations without these keys.
User opening/closing updates the in-memory preference. Normal application exit
snapshots both windows before teardown and saves through the usual configuration
export. Destruction-time hide/close events cannot replace that snapshot.

A deferred callback after configuration, appearance and workspace initialization
restores open windows with `ShowWithoutActivating()`. Existing geometry recovery,
ownership and snapping registrations are retained. Merely showing either window
does not start reception or transcription; the existing explicit auto-start
modem preference continues to work independently.

The single-line legend reads:
`Green: Listed/recent | Yellow: 1–12h | Red: >12h | Gray: Unknown/never seen`.
Hover over it for explanations of Reporter presence, recency, Unknown, local
timestamps and history retention. It uses the existing theme-aware text styling.

`ConfigMigrationTest` adds defaults, all four independent visibility combinations,
save/reload, legacy profile reload and geometry checks. The new
`gui_watchlist_visibility_and_footer` scenario exercises normal shutdown/restart,
both user close actions, restored geometry, retained and enabled snap registration,
startup focus, no active receive worker/modem or transcript content, and the legend
and tooltip. These additions have not been executed. Actual native dragging and
resize snapping, OS shutdown/session-end behavior, focus policy and tooltip
presentation still require local testing on each supported platform. Abrupt
termination cannot guarantee saving preferences, just as with other settings.

## File inventory

Created:

- `src/config/CallsignWatchlist.h` and `src/config/CallsignWatchlist.cpp`
- `src/config/LocalTimestamp.h` and `src/config/LocalTimestamp.cpp`
- `src/config/test/CallsignWatchlistTest.cpp`
- `src/gui/dialogs/callsign_watchlist.h` and `src/gui/dialogs/callsign_watchlist.cpp`
- `src/gui/theme/SystemAppearance_osx.mm`
- `test/gui/test_watchlist.py`
- `docs/CALLSIGN_WATCHLIST.md`

Modified:

- `CMakeLists.txt` (GUI test registration only; release number unchanged)
- `src/config/CMakeLists.txt`
- `src/config/FreeDVConfiguration.h` and `src/config/FreeDVConfiguration.cpp`
- `src/gui/dialogs/CMakeLists.txt`
- `src/gui/dialogs/freedv_reporter.h` and `src/gui/dialogs/freedv_reporter.cpp`
- `src/gui/theme/CMakeLists.txt`
- `src/gui/theme/FreeDVTheme.h` and `src/gui/theme/FreeDVTheme.cpp`
- `src/main.h` and `src/main.cpp`
- `src/test_agent.cpp` (test-only window/list inspection and actions)
- `test/gui/test_reporter.py` (callsign-menu expectations)
