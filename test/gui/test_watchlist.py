#!/usr/bin/env python3
"""Callsign Watchlist integration scenarios. Uses the local Reporter server.

Registered with CTest's gui label; building/running is a separate local step.
No public Reporter traffic, notifications or user's real settings are involved.
"""
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import freedvtest
from freedvtest.reporter import ReporterServer
from test_reporter import (reporter_settings, open_reporter, wait_for_callsigns,
                           quit_and_check_log, check, check_equal, LIST, CALLSIGN)

ENTRIES = "watchlist.entries"
RECORDS = "/Reporting/FreeDV/Watchlist/Records"
GEOMETRY = "/Windows/CallsignWatchlist/"


def open_watchlist(app):
    open_reporter(app)
    app.menu("Watchlist/Open Callsign Watchlist", window="reporter")
    app.wait_for_window("watchlist")
    app.activate("watchlist")


def rows(app):
    return {item["columns"][0]: item for item in app.get(ENTRIES)["items"]}


def status(app, callsign):
    row = rows(app).get(callsign)
    return row["columns"][1] if row else None


def wait_status(app, callsign, expected):
    app.wait_until(lambda: status(app, callsign) == expected,
                   description="%s to be %s in Watchlist" % (callsign, expected))


def stored(config):
    result = {}
    for entry in freedvtest.read_config(config).get(RECORDS, "").split(","):
        if entry:
            callsign, seen, update = entry.split("|")
            result[callsign] = (int(seen), int(update))
    return result


def add(app, callsign):
    app.set_value("watchlist.callsign", callsign)
    app.click("watchlist.add")


def management():
    with ReporterServer() as server:
        station = server.station("K1ABC", "FN42")
        station.set_frequency(14236000)
        with freedvtest.start(settings=reporter_settings(server.hostname)) as app:
            open_watchlist(app)
            wait_for_callsigns(app, ["K1ABC"])
            add(app, "  neverseen/p  ")
            add(app, "NEVERSEEN/P")
            check_equal(list(rows(app)), ["NEVERSEEN/P"], "Normalized, unique manual entry")
            wait_status(app, "NEVERSEEN/P", "Not Listed")
            check_equal(rows(app)["NEVERSEEN/P"]["columns"][2:], ["Unknown", "Unknown"], "Unseen history")
            app.activate("reporter")
            app.row_context_menu(LIST, (CALLSIGN, "K1ABC"), CALLSIGN, choose="Add to Watchlist")
            wait_status(app, "K1ABC", "Listed")
            app.set_value("reporter.cbBandFilter", "40 m")
            wait_for_callsigns(app, [])
            check_equal(status(app, "K1ABC"), "Listed", "Band filtering is not a disconnect")
            app.set_value("reporter.cbBandFilter", "All")
            wait_for_callsigns(app, ["K1ABC"])
            app.activate("watchlist")
            app.set_value("watchlist.filter", "Currently Listed")
            check_equal(list(rows(app)), ["K1ABC"], "Currently Listed filter")
            app.set_value("watchlist.filter", "All")
            # Each sortable column reverses deterministically when clicked again.
            for column in range(4):
                app.command("list_column_click", name=ENTRIES, column=column)
                first = list(rows(app))
                app.command("list_column_click", name=ENTRIES, column=column)
                check_equal(list(rows(app)), list(reversed(first)), "Sort column %d" % column)
            app.set_value(ENTRIES, "NEVERSEEN/P")
            app.click("watchlist.remove")
            check_equal(list(rows(app)), ["K1ABC"], "Manual removal")
            app.activate("reporter")
            app.row_context_menu(LIST, (CALLSIGN, "K1ABC"), CALLSIGN, choose="Remove from Watchlist")
            check_equal(rows(app), {}, "Context removal")
            quit_and_check_log(app)


def history():
    with tempfile.TemporaryDirectory(prefix="freedv-watchlist-") as directory, ReporterServer() as server:
        config = os.path.join(directory, "freedv.conf")
        station = server.station("K1ABC", "FN42")
        station.set_frequency(14236000)
        settings = reporter_settings(server.hostname)
        settings[RECORDS] = "K1ABC|0|0,OLD/P|315532800000|315532800000,UNSEEN|0|0"
        freedvtest.write_config(config, settings)
        with freedvtest.start(config=config) as app:
            app.wait_for_window("main")
            # The Watchlist and Reporter have never been opened.
            app.wait_until(lambda: stored(config)["K1ABC"][0] > 0,
                           description="history saved while both windows are closed")
            open_watchlist(app)
            wait_status(app, "K1ABC", "Listed")
            # Reading/timers must not advance Last Seen.
            time.sleep(1)
            before = stored(config)["K1ABC"]
            time.sleep(1)
            check_equal(stored(config)["K1ABC"], before, "No manufactured observation timestamps")
            app.close("watchlist")
            app.close("reporter")
            station.set_message("new observation with hidden windows")
            app.wait_until(lambda: stored(config)["K1ABC"][0] > before[0])
            seen = stored(config)["K1ABC"][0]
            station.disconnect()
            open_watchlist(app)
            wait_status(app, "K1ABC", "Not Listed")
            check_equal(stored(config)["K1ABC"][0], seen, "Disconnect preserves Last Seen")
            retained = stored(config)
            quit_and_check_log(app)
        with freedvtest.start(config=config) as app:
            open_watchlist(app)
            wait_status(app, "K1ABC", "Not Listed")
            check_equal(stored(config), retained, "All history survives process restart and absence")
            check_equal(stored(config)["OLD/P"], (315532800000, 315532800000), "Indefinite old history")
            quit_and_check_log(app)


def reconnect_and_filters():
    with ReporterServer() as server:
        first = server.station("K1ABC", "FN42")
        second = server.station("K1ABC", "FN43")
        first.set_frequency(14236000)
        second.set_frequency(14236000)
        settings = reporter_settings(server.hostname)
        settings[RECORDS] = "K1ABC|0|0,W2DEF|0|0"
        # Zero is deliberately seeded to exercise idle hiding without a long sleep.
        settings["/Reporting/FreeDV/EnableMaxIdleFilter"] = True
        settings["/Reporting/FreeDV/MaxIdleMinutes"] = 0
        with freedvtest.start(settings=settings) as app:
            open_watchlist(app)
            wait_status(app, "K1ABC", "Listed")
            app.wait_until(lambda: app.rows(LIST) == [], description="Reporter idle filter to hide sessions")
            check_equal(status(app, "K1ABC"), "Listed", "Idle filtering is not a disconnect")
            normal = app.get(ENTRIES)["background"]
            check_equal(rows(app)["K1ABC"]["background"], normal, "Initial sync has no appearance alert")
            first.disconnect()
            time.sleep(1)
            check_equal(status(app, "K1ABC"), "Listed", "Other session keeps callsign listed")
            server.interrupt_freedv()
            wait_status(app, "K1ABC", "Unknown")
            wait_status(app, "K1ABC", "Listed")
            check_equal(rows(app)["K1ABC"]["background"], normal, "Reconnection has no appearance alert")
            newcomer = server.station("W2DEF", "FN44")
            newcomer.set_frequency(7177000)
            wait_status(app, "W2DEF", "Listed")
            check(rows(app)["W2DEF"]["background"] != normal, "New watched station was not briefly highlighted")
            app.wait_until(lambda: rows(app)["W2DEF"]["background"] == normal, timeout=10,
                           description="transient appearance highlight to expire")
            check(rows(app)["W2DEF"]["bold"], "Listed indicator disappeared with highlight")
            second.disconnect()
            wait_status(app, "K1ABC", "Not Listed")
            quit_and_check_log(app)


def geometry_and_theme():
    with tempfile.TemporaryDirectory(prefix="freedv-watchlist-") as directory:
        config = os.path.join(directory, "freedv.conf")
        base = reporter_settings("127.0.0.1:9")
        base[RECORDS] = "UNSEEN|315532800000|315532800000"
        for mode in (1, 2, 0):  # Light, Dark, System.
            settings = dict(base)
            settings["/Appearance/Mode"] = mode
            settings.update({GEOMETRY + "left": 30000, GEOMETRY + "top": -30000,
                             GEOMETRY + "width": 30000, GEOMETRY + "height": 30000})
            freedvtest.write_config(config, settings)
            with freedvtest.start(config=config) as app:
                open_watchlist(app)
                time.sleep(2.5)  # Allow the existing GTK placement safeguard to finish.
                geometry = app.get("watchlist")["rect"]
                check(-100 <= geometry[0] <= 16000 and -100 <= geometry[1] <= 16000,
                      "Disconnected-monitor position was not recovered: %r" % geometry)
                check(0 < geometry[2] < 30000 and 0 < geometry[3] < 30000, "Oversized window was not constrained")
                colors = app.get(ENTRIES)
                check(colors["background"] != colors["foreground"], "Theme has no readable contrast")
                if mode == 1: check_equal(colors["background"].upper(), "#FFFFFF", "Light surface")
                if mode == 2: check_equal(colors["background"].upper(), "#181A1E", "Dark surface")
                check_equal(rows(app)["UNSEEN"]["image"], 3, "Unknown overrides old age")
                original_indicator = rows(app)["UNSEEN"]["indicatorColor"]
                # Select an application appearance while Watchlist remains open.
                main = "@" + app.get("main")["label"] + "/"
                requested = "Light" if mode == 2 else "Dark"
                app.click(main + requested)
                expected = "#FFFFFF" if requested == "Light" else "#181A1E"
                app.wait_until(lambda: app.get(ENTRIES)["background"].upper() == expected)
                check_equal(rows(app)["UNSEEN"]["image"], 3, "Theme change preserves indicator meaning")
                if mode in (1, 2):
                    check(rows(app)["UNSEEN"]["indicatorColor"] != original_indicator,
                          "Indicator palette did not refresh with appearance")
                app.click(main + {0: "System", 1: "Light", 2: "Dark"}[mode])
                messages = app.message_boxes(clear=True)
                check(all(message["caption"] == "Appearance" for message in messages), "Unexpected theme error dialog")
                app.system_colours_changed()
                footer = app.get("watchlist.legend")
                check(footer["foreground"] != footer["background"], "Footer contrast after theme change")
                check("Unknown" in footer["tooltip"], "Theme change lost footer help")
                check_equal(rows(app)["UNSEEN"]["image"], 3, "System notification preserves Unknown")
                check_equal(rows(app)["UNSEEN"]["background"], app.get(ENTRIES)["background"],
                            "Rows refresh on system appearance notification")
                app.command("window_geometry", name="watchlist", rect=[80, 90, 720, 420])
                time.sleep(0.5)
                left = app.get("watchlist")["rect"]
                app.close("watchlist")
                open_watchlist(app)
                time.sleep(0.5)
                check_equal(app.get("watchlist")["rect"], left, "Close/reopen geometry")
                quit_and_check_log(app)
            with freedvtest.start(config=config) as app:
                open_watchlist(app)
                time.sleep(2.5)
                check_equal(app.get("watchlist")["rect"], left, "Geometry after application restart")
                check_equal(status(app, "UNSEEN"), "Unknown", "Offline status")
                quit_and_check_log(app)


def indicators():
    with ReporterServer() as server:
        now = int(time.time() * 1000)
        hour = 3600000
        # Both boundaries arrive together in a minute, allowing startup time.
        seen = {"RECENT": now - hour + 60000, "AGING": now - 12 * hour + 60000,
                "STALE": now - 13 * hour, "NEVER": 0}
        settings = reporter_settings(server.hostname)
        settings[RECORDS] = ",".join("%s|%d|%d" % (call, stamp, stamp) for call, stamp in seen.items())
        with freedvtest.start(settings=settings) as app:
            open_watchlist(app)
            wait_status(app, "RECENT", "Not Listed")
            for call, image in {"RECENT": 0, "AGING": 1, "STALE": 2, "NEVER": 3}.items():
                check_equal(rows(app)[call]["image"], image, call + " initial indicator")
            app.set_value(ENTRIES, "RECENT")
            selection = app.get(ENTRIES)["selection"]
            history_before = stored(app.config_path)
            app.wait_until(lambda: rows(app)["RECENT"]["image"] == 1 and rows(app)["AGING"]["image"] == 2,
                           timeout=90, description="timer-driven one-hour and twelve-hour transitions")
            check_equal(app.get(ENTRIES)["selection"], selection, "Timer preserves selection")
            check_equal(stored(app.config_path), history_before, "Timer does not mutate history")
            check_equal(status(app, "RECENT"), "Not Listed", "Recent green was not connectivity")
            server.interrupt_freedv()
            wait_status(app, "RECENT", "Unknown")
            check(all(row["image"] == 3 for row in rows(app).values()), "Unknown entries must all be gray")
            quit_and_check_log(app)


def visibility_and_footer():
    """Normal exits retain independent visibility, without starting reception."""
    legend = "Green: Listed/recent | Yellow: 1–12h | Red: >12h | Gray: Unknown/never seen"
    transcript = "/Windows/DecodedSpeech/"

    def check_idle(app):
        state = app.state()
        check(not state["running"] and not state["modemRunning"] and not state["transmitting"]
              and not state["receiveWorkerActive"],
              "Restoring auxiliary windows activated the modem")
        check_equal(app.get("decodedSpeech.text")["value"], "", "No automatic transcription")

    def check_snap(app, name):
        app.wait_until(lambda: app.get(name)["snapEnabled"], description="snapping to be enabled")
        check(app.get(name)["snapRegistered"], name + " lost its snapping registration")

    def check_saved(config, watchlist, decoded):
        saved = freedvtest.read_config(config)
        check_equal(saved[GEOMETRY + "visible"], "1" if watchlist else "0", "Saved watchlist visibility")
        check_equal(saved[transcript + "visible"], "1" if decoded else "0", "Saved transcript visibility")

    with ReporterServer() as server, tempfile.TemporaryDirectory(prefix="freedv-visibility-") as directory:
        config = os.path.join(directory, "freedv.conf")
        settings = reporter_settings(server.hostname)
        settings["/Modem/autoStartOnLaunch"] = False
        # An older configuration with geometry but no visibility preferences.
        settings.update({GEOMETRY + "width": 760, GEOMETRY + "height": 400,
                         transcript + "width": 640, transcript + "height": 360})
        freedvtest.write_config(config, settings)
        with freedvtest.start(config=config) as app:
            app.wait_for_window("main")
            check(not app.window_exists("watchlist"), "First-launch watchlist should be closed")
            check(not app.window_exists("decodedSpeech"), "First-launch transcript should be closed")
            check_idle(app)
            open_watchlist(app)
            app.close("reporter")  # Do not restore another activating window in this scenario.
            app.menu("Tools/Decoded Speech Transcription [Experimental]")
            app.wait_for_window("decodedSpeech")
            footer = app.get("watchlist.legend")
            check_equal(footer["label"], legend, "Compact single-line footer")
            for explanation in ("Not Listed", "Unknown", "Last Seen", "Last Update",
                                "does not mean currently connected", "History is kept"):
                check(explanation in footer["tooltip"], "Tooltip is missing " + explanation)
            check(footer["background"] != footer["foreground"], "Footer text has no contrast")
            time.sleep(2.5)  # Wait out native initial placement before simulating user geometry.
            geometry = {}
            for name, rect in (("watchlist", [80, 90, 760, 420]),
                               ("decodedSpeech", [110, 120, 640, 360])):
                check_snap(app, name)
                app.command("window_geometry", name=name, rect=rect)
                time.sleep(0.5)
                geometry[name] = app.get(name)["rect"]
            # Cover File > Exit as well as the main-window close path below.
            check_equal(app.message_boxes(), [], "Errors before File > Exit")
            app.menu("File/Exit")  # Both auxiliary windows are still open.
            check_equal(app.wait_for_exit(), 0, "File > Exit status")
            check_equal([line for line in app.log().splitlines() if " ERROR " in line or " FATAL " in line],
                        [], "Errors during File > Exit")
        check_saved(config, True, True)

        # Each normal restart uses exactly what the preceding shutdown saved.
        for watchlist, decoded in ((True, True), (False, True), (True, False), (False, False)):
            with freedvtest.start(config=config) as app:
                app.wait_for_window("main")
                for name, visible in (("watchlist", watchlist), ("decodedSpeech", decoded)):
                    if visible:
                        app.wait_for_window(name)
                    else:
                        check(not app.window_exists(name), name + " unexpectedly reopened")
                    check_snap(app, name)
                check_idle(app)
                time.sleep(2.5)
                for name, visible in (("watchlist", watchlist), ("decodedSpeech", decoded)):
                    check_equal(app.window_exists(name), visible, name + " settled startup visibility")
                    if visible:
                        check_equal(app.get(name)["rect"], geometry[name], name + " restart geometry")
                        check(not app.get(name)["active"], name + " stole startup focus")
                if watchlist and decoded:
                    app.close("watchlist")
                    saved_visibility = (False, True)
                elif decoded:
                    app.close("decodedSpeech")
                    open_watchlist(app)
                    app.close("reporter")
                    time.sleep(2.5)
                    check_equal(app.get("watchlist")["rect"], geometry["watchlist"], "Reopened geometry")
                    check_snap(app, "watchlist")
                    saved_visibility = (True, False)
                elif watchlist:
                    app.close("watchlist")
                    saved_visibility = (False, False)
                else:
                    saved_visibility = (False, False)
                quit_and_check_log(app)
            check_saved(config, *saved_visibility)


SCENARIOS = {"management": management, "history": history, "indicators": indicators,
             "reconnect_and_filters": reconnect_and_filters, "geometry_and_theme": geometry_and_theme,
             "visibility_and_footer": visibility_and_footer}

if __name__ == "__main__":
    for name in sys.argv[1:] or SCENARIOS:
        if name not in SCENARIOS:
            sys.exit("Unknown scenario: " + name)
        print("Running " + name, flush=True)
        SCENARIOS[name]()
    print("PASS")
