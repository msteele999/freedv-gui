#!/usr/bin/env python3
"""Collect cumulative GitHub release-asset downloads; Python standard library only.

Run from any directory. Generated files are written to this script's repository.
GITHUB_TOKEN (or GH_TOKEN) is optional for public release/engagement access.
RELEASE_STATS_TOKEN supplies read access to repository traffic analytics.
"""

import argparse
import csv
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
import html
import io
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request


REPOSITORY = "msteele999/freedv-gui"
API_ROOT = "https://api.github.com/repos/" + REPOSITORY
ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = (
    "Windows x64", "Windows ARM64", "Linux x64", "Linux ARM64",
    "macOS", "Other / Unknown",
)
PLATFORM_COLORS = ("#2563eb", "#0891b2", "#15803d", "#65a30d", "#7c3aed", "#64748b")
RELEASE_COLORS = {"stable": "#2563eb", "experimental": "#7c3aed", "other": "#64748b"}
HISTORY_FIELDS = (
    "observed_at", "release_id", "tag", "release_name", "published_at",
    "classification", "asset_id", "filename", "download_count", "platform", "asset_type",
)
MAX_ATTEMPTS = 4
MAX_RETRY_WAIT = 120


class StatsError(Exception):
    """An incomplete collection must not replace a previously valid snapshot."""


def safe_error(error):
    message = str(error).replace("\r", " ").replace("\n", " ")
    for name in ("GITHUB_TOKEN", "GH_TOKEN", "RELEASE_STATS_TOKEN"):
        token = os.environ.get(name)
        if token:
            message = message.replace(token, "[redacted]")
    return message[:400]


def retry_delay(headers, attempt, rate_limited=False):
    delay = 60 if rate_limited else 2 ** (attempt + 1)
    retry_after = headers.get("Retry-After")
    if retry_after:
        try:
            delay = max(delay, float(retry_after))
        except ValueError:
            delay = max(delay, parsedate_to_datetime(retry_after).timestamp() - time.time())
    if headers.get("X-RateLimit-Remaining") == "0":
        reset = headers.get("X-RateLimit-Reset")
        if reset:
            delay = max(delay, float(reset) - time.time() + 1)
    return max(1, math.ceil(delay))


def get_json(url, token):
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "FreeDV-W2MWS-Release-Statistics",
    }
    if token:
        headers["Authorization"] = "Bearer " + token
    for attempt in range(MAX_ATTEMPTS):
        try:
            request = urllib.request.Request(url, headers=headers, method="GET")
            with urllib.request.urlopen(request, timeout=30) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            message = error.read(4096).decode("utf-8", "replace")
            try:
                message = json.loads(message).get("message", message)
            except (ValueError, AttributeError):
                pass
            limited = error.code == 429 or (
                error.code == 403 and (
                    error.headers.get("X-RateLimit-Remaining") == "0"
                    or error.headers.get("Retry-After")
                    or "rate limit" in str(message).lower()
                )
            )
            if not limited and error.code not in (500, 502, 503, 504):
                raise StatsError("GitHub API HTTP %s: %s" % (error.code, safe_error(message))) from error
            delay = retry_delay(error.headers, attempt, limited)
            detail = "GitHub API HTTP %s: %s" % (error.code, safe_error(message))
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            delay = 2 ** (attempt + 1)
            detail = "GitHub API network error: %s" % safe_error(error)
        except ValueError as error:
            raise StatsError("GitHub API returned invalid JSON") from error
        if attempt == MAX_ATTEMPTS - 1 or delay > MAX_RETRY_WAIT:
            raise StatsError("%s; retry later (required wait: %ss). No snapshot collected." % (detail, delay))
        print("%s; retrying in %ss" % (detail, delay), file=sys.stderr)
        time.sleep(delay)
    raise StatsError("GitHub API retries exhausted")


def paginate(endpoint, token):
    page = 1
    while True:
        data = get_json("%s/%s?per_page=100&page=%s" % (API_ROOT, endpoint, page), token)
        if not isinstance(data, list):
            raise StatsError("Expected a paginated list for " + endpoint)
        yield from data
        if len(data) < 100:
            return
        page += 1


def classification(tag):
    if re.fullmatch(r"v3\.0-dev-W2MWS\.[0-9]+", tag):
        return "stable"
    if re.fullmatch(r"v3\.0-dev-W2MWS-radev2\.[0-9]+", tag):
        return "experimental"
    return "other"


def asset_type(filename):
    name = filename.lower()
    if name.endswith((".exe", ".appimage", ".dmg")):
        return "installer"
    if name.endswith((".zip", ".7z", ".tar", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tbz2", ".gz", ".xz")):
        return "archive"
    return "other"


def platform(filename):
    name = filename.lower()
    # Do not infer an architecture from a version number or a generic ZIP name.
    arm = bool(re.search(r"(?<![a-z0-9])(?:arm64|aarch64)(?![a-z0-9])", name))
    x64 = bool(re.search(r"(?<![a-z0-9])(?:x86_64|x86-64|amd64|x64|win64)(?![a-z0-9])", name))
    mac = name.endswith(".dmg") or bool(re.search(r"(?<![a-z0-9])(?:macos|mac|osx|darwin)(?![a-z0-9])", name))
    windows = name.endswith(".exe") or bool(re.search(r"(?<![a-z0-9])(?:windows|win|win32|win64)(?![a-z0-9])", name))
    linux = ".appimage" in name or bool(re.search(r"(?<![a-z0-9])linux(?![a-z0-9])", name))
    if mac and not windows and not linux:
        return "macOS"
    if arm == x64 or windows == linux or mac:
        return "Other / Unknown"
    if windows:
        return "Windows ARM64" if arm else "Windows x64"
    return "Linux ARM64" if arm else "Linux x64"


def integer(value, field):
    if type(value) is not int or value < 0:
        raise StatsError("Invalid or missing integer: " + field)
    return value


def collect(token):
    releases = []
    seen_releases = set()
    seen_assets = set()
    for release in paginate("releases", token):
        if release.get("draft") or not release.get("published_at"):
            continue
        release_id = integer(release.get("id"), "release id")
        if release_id in seen_releases:
            raise StatsError("Release list changed during pagination; retry collection")
        seen_releases.add(release_id)
        tag = release["tag_name"]
        assets = []
        # Fetch assets separately so releases with over 100 assets are complete.
        # GitHub's automatic tarball_url/zipball_url source archives are not assets.
        for asset in paginate("releases/%s/assets" % release_id, token):
            asset_id = integer(asset.get("id"), "asset id")
            if asset_id in seen_assets:
                raise StatsError("Asset list changed during pagination; retry collection")
            seen_assets.add(asset_id)
            filename = asset["name"]
            assets.append({
                "id": asset_id, "filename": filename,
                "download_count": integer(asset.get("download_count"), "download_count"),
                "platform": platform(filename), "asset_type": asset_type(filename),
                "is_zip": filename.lower().endswith(".zip"),
            })
        assets.sort(key=lambda item: (item["filename"].casefold(), item["id"]))
        releases.append({
            "id": release_id, "tag": tag, "name": release.get("name") or tag,
            "published_at": release["published_at"], "classification": classification(tag),
            "prerelease": bool(release.get("prerelease")),
            "is_w2mws": "W2MWS" in tag.upper(), "assets": assets,
            "download_count": sum(item["download_count"] for item in assets),
        })
    return sorted(releases, key=lambda item: (item["published_at"], item["tag"], item["id"]))


def summarize(releases):
    totals = {"downloads": 0, "releases": len(releases), "assets": 0,
              "by_classification": {key: 0 for key in RELEASE_COLORS},
              "by_platform": {key: 0 for key in PLATFORMS},
              "by_asset_type": {key: 0 for key in ("installer", "archive", "other")},
              "zip_downloads": 0}
    for release in releases:
        totals["by_classification"][release["classification"]] += release["download_count"]
        for asset in release["assets"]:
            count = asset["download_count"]
            totals["downloads"] += count
            totals["assets"] += 1
            totals["by_platform"][asset["platform"]] += count
            totals["by_asset_type"][asset["asset_type"]] += count
            if asset["is_zip"]:
                totals["zip_downloads"] += count
    return totals


def svg_start(width, height, title, description):
    return [
        '<svg xmlns="http://www.w3.org/2000/svg" width="%s" height="%s" viewBox="0 0 %s %s" role="img" aria-labelledby="title desc">' % (width, height, width, height),
        '<title id="title">%s</title><desc id="desc">%s</desc>' % (html.escape(title), html.escape(description)),
        '<rect width="100%" height="100%" rx="12" fill="#ffffff"/>',
        '<g font-family="Segoe UI, Arial, sans-serif" fill="#0f172a">',
    ]


def text(x, y, value, size=14, color="#0f172a", anchor="start"):
    return '<text x="%s" y="%s" font-size="%s" fill="%s" text-anchor="%s">%s</text>' % (x, y, size, color, anchor, html.escape(str(value)))


def version_label(release):
    tag = release["tag"]
    if release["classification"] == "stable":
        return "W2MWS." + tag.rsplit(".", 1)[1]
    if release["classification"] == "experimental":
        return "RADEV2." + tag.rsplit(".", 1)[1]
    return tag if len(tag) <= 30 else tag[:27] + "..."


def bar_chart(releases):
    shown = sorted((r for r in releases if r["is_w2mws"]),
                   key=lambda r: (r["published_at"], r["tag"], r["id"]), reverse=True)[:10]
    height = max(240, 130 + len(shown) * 35)
    svg = svg_start(920, height, "Recent W2MWS release downloads",
                    "The ten most recently published W2MWS releases; cumulative asset downloads. Complete release data remains in latest.json.")
    svg += [text(20, 30, "Downloads by release", 20),
            text(20, 51, "10 most recent W2MWS releases · newest first · cumulative downloads", 12, "#475569")]
    for i, (key, label) in enumerate((("stable", "Stable"), ("experimental", "Experimental"), ("other", "Other W2MWS"))):
        x = 20 + i * 170
        svg += ['<rect x="%s" y="66" width="11" height="11" rx="2" fill="%s"/>' % (x, RELEASE_COLORS[key]), text(x + 18, 76, label, 12)]
    if not shown:
        svg.append(text(20, 140, "No published W2MWS releases.", 15, "#475569"))
    else:
        maximum = max(r["download_count"] for r in shown) or 1
        ticks = min(4, maximum)
        axis_max = math.ceil(maximum / ticks) * ticks
        for i in range(ticks + 1):
            x = 370 + i * 450 / ticks
            svg += ['<path d="M %.1f 95 V %s" stroke="#e2e8f0"/>' % (x, height - 30),
                    text(round(x, 1), height - 10, "{:,}".format(axis_max * i // ticks), 11, "#64748b", "middle")]
        for i, release in enumerate(shown):
            y = 100 + i * 35
            count = release["download_count"]
            width = 450 * count / axis_max
            name = str(release["name"]).replace("\n", " ").replace("\r", " ")
            label = name if len(name) <= 53 else name[:50] + "..."
            svg += [text(20, y + 11, version_label(release), 13), text(20, y + 25, label, 11, "#475569"),
                    '<rect x="370" y="%s" width="%.2f" height="23" rx="3" fill="%s"><title>%s</title></rect>' %
                    (y + 2, width, RELEASE_COLORS[release["classification"]], html.escape(name + " — " + release["tag"] + ": " + str(count))),
                    text(round(830 if width == 450 else 380 + width, 2), y + 18, "{:,}".format(count), 13)]
    return "\n".join(svg + ["</g></svg>", ""])


def pie_chart(totals):
    total = totals["downloads"]
    svg = svg_start(720, 270, "Downloads by platform", "Donut chart of cumulative downloads, including ZIP archives, across all six platform categories.")
    svg += [text(20, 29, "Downloads by platform", 20), text(20, 49, "All published assets · cumulative downloads", 12, "#475569")]
    angle = -math.pi / 2
    if not total:
        svg.append('<circle cx="120" cy="158" r="83" fill="#e2e8f0"/>')
    for i, name in enumerate(PLATFORMS):
        count = totals["by_platform"][name]
        fraction = count / total if total else 0
        end = angle + 2 * math.pi * fraction
        color = PLATFORM_COLORS[i]
        if count == total and total:
            svg.append('<circle cx="120" cy="158" r="83" fill="%s"/>' % color)
        elif count:
            x1, y1 = 120 + 83 * math.cos(angle), 158 + 83 * math.sin(angle)
            x2, y2 = 120 + 83 * math.cos(end), 158 + 83 * math.sin(end)
            svg.append('<path d="M 120 158 L %.3f %.3f A 83 83 0 %s 1 %.3f %.3f Z" fill="%s" stroke="#ffffff" stroke-width="2"/>' % (x1, y1, int(fraction > 0.5), x2, y2, color))
        angle = end
        y = 85 + i * 29
        svg += ['<rect x="250" y="%s" width="12" height="12" rx="2" fill="%s"/>' % (y - 10, color),
                text(274, y, name, 13), text(698, y, "{:,} ({:.1%})".format(count, fraction), 13, anchor="end")]
    svg += ['<circle cx="120" cy="158" r="55" fill="#ffffff"/>',
            text(120, 156, "{:,}".format(total), 19, anchor="middle"), text(120, 176, "downloads", 11, "#475569", "middle")]
    return "\n".join(svg + ["</g></svg>", ""])


def traffic_chart(traffic):
    days = (traffic.get("data") or {}).get("views", {}).get("days", [])
    svg = svg_start(800, 250, "Daily repository traffic", "Daily views and unique visitors from GitHub's rolling traffic window. Missing days are not fabricated as zero.")
    svg += [text(20, 28, "Repository traffic trends", 20), text(20, 49, component_label(traffic), 11, "#475569")]
    if not days:
        if traffic.get("data") is not None:
            message = "No daily traffic points returned for this window."
            detail = "Reported zero totals remain valid; missing days are not synthesized."
        else:
            message = "Daily traffic data is not available yet."
            detail = "A successful traffic API collection is required."
        svg += [text(20, 120, message, 15, "#475569"), text(20, 146, detail, 12, "#64748b")]
        return "\n".join(svg + ["</g></svg>", ""])
    dates = [datetime.fromisoformat(day["date"]).date().toordinal() for day in days]
    start, span = min(dates), max(1, max(dates) - min(dates))
    ceiling = max(1, max(day["count"] for day in days))
    ticks = min(3, ceiling)
    ceiling = math.ceil(ceiling / ticks) * ticks
    for i in range(ticks + 1):
        y = 195 - i * 110 / ticks
        svg += ['<path d="M 52 %.2f H 775" stroke="#e2e8f0"/>' % y,
                text(43, round(y + 4, 2), str(ceiling * i // ticks), 10, "#64748b", "end")]
    for series, color, label in (("count", "#2563eb", "Views"), ("uniques", "#0891b2", "Unique visitors")):
        commands = []
        previous = None
        for day, ordinal in zip(days, dates):
            x, y = 52 + 723 * (ordinal - start) / span, 195 - 110 * day[series] / ceiling
            commands.append(("M" if previous is None or ordinal != previous + 1 else "L") + " %.2f %.2f" % (x, y))
            previous = ordinal
            svg.append('<circle cx="%.2f" cy="%.2f" r="2.3" fill="%s"><title>%s</title></circle>' % (x, y, color, html.escape("%s %s: %s" % (day["date"], label, day[series]))))
        svg.append('<path d="%s" fill="none" stroke="%s" stroke-width="2.2"/>' % (" ".join(commands), color))
    for i in sorted({round(i * (len(days) - 1) / 4) for i in range(5)}):
        x = 52 + 723 * (dates[i] - start) / span
        svg.append(text(round(x, 2), 218, datetime.fromisoformat(days[i]["date"]).strftime("%b %d"), 10, "#64748b", "middle"))
    svg += [text(52, 242, "UTC dates · views", 11, "#2563eb"), text(200, 242, "Unique visitors", 11, "#0891b2")]
    return "\n".join(svg + ["</g></svg>", ""])


def markdown(value):
    value = html.escape(str(value).replace("\r", " ").replace("\n", " "))
    return re.sub(r"([\\`*_{}\[\]()#+.!|>~-])", r"\\\1", value)


def component_label(component):
    observed = component.get("observed_at")
    status = component.get("status", "unavailable")
    if not observed:
        return "Not yet collected / unavailable"
    return "%s · last successful collection %s UTC" % (status.title(), observed)


def metric(value):
    return "—" if value is None else "{:,}".format(value)


def statistics_page(snapshot):
    totals = snapshot.get("totals")
    repository = snapshot.get("repository_metrics", {})
    traffic = snapshot.get("traffic", {})
    engagement = repository.get("data") or {}
    traffic_data = traffic.get("data") or {}
    views = traffic_data.get("views", {})
    clones = traffic_data.get("clones", {})
    lines = ["# GitHub Release Statistics", "",
             "[msteele999/freedv-gui](https://github.com/msteele999/freedv-gui/releases) · downloads, engagement and repository traffic", "",
             "**Most recent successful collection: %s UTC.**" % (snapshot.get("last_successful_collection_at") or "not yet collected"), "",
             "| Release downloads (cumulative) | Published releases | Stars |", "| ---: | ---: | ---: |",
             "| **%s** | **%s** | **%s** |" % (metric(totals["downloads"] if totals else None), metric(totals["releases"] if totals else None), metric(engagement.get("stars"))), "",
             "| Subscribers / watchers | Forks | Unique visitors (14 days) |", "| ---: | ---: | ---: |",
             "| **%s** | **%s** | **%s** |" % (metric(engagement.get("subscribers")), metric(engagement.get("forks")), metric(views.get("uniques"))), ""]
    if totals:
        classes = totals["by_classification"]
        lines += ["Stable W2MWS: **%s** · Experimental RADEV2: **%s** · Other: **%s** downloads." % tuple(metric(classes[key]) for key in ("stable", "experimental", "other")), ""]
    lines += ["Releases: %s. Engagement: %s. Traffic: %s." % tuple(markdown(component_label(snapshot.get(key, {}))) for key in ("release_collection", "repository_metrics", "traffic")), ""]
    for label, component in (("Releases", snapshot.get("release_collection", {})), ("Engagement", repository), ("Traffic", traffic)):
        if component.get("error"):
            lines += ["> **%s %s:** %s. Last successful values are retained; unavailable values are shown as —." % (label, component.get("status", "unavailable"), markdown(component["error"])), ""]
    lines += ["## Downloads by release", "", "![Ten most recent W2MWS releases](docs/release-stats/downloads-by-release.svg)", "",
              "<details>", "<summary>Complete W2MWS release table</summary>", "",
              "| Release | Name | Family | Published (UTC) | Downloads |", "| --- | --- | --- | --- | ---: |"]
    shown = sorted((r for r in snapshot.get("releases", []) if r["is_w2mws"]), key=lambda r: (r["published_at"], r["tag"], r["id"]), reverse=True)
    for release in shown:
        url = "https://github.com/%s/releases/tag/%s" % (REPOSITORY, urllib.parse.quote(release["tag"], safe=""))
        lines.append("| [%s](%s) | %s | %s | %s | %s |" % (markdown(release["tag"]), url, markdown(release["name"]), release["classification"].title(), markdown(release["published_at"][:10]), metric(release["download_count"])))
    if not shown:
        lines.append("| No published W2MWS releases collected | — | — | — | — |")
    lines += ["", "</details>", "", "## Downloads by platform", "",
              "![Release downloads by platform](docs/release-stats/downloads-by-platform.svg)", "",
              "<details>", "<summary>Platform and asset-format breakdown</summary>", "",
              "| Platform | Downloads | Share |", "| --- | ---: | ---: |"]
    for name in PLATFORMS:
        count = totals["by_platform"][name] if totals else None
        percent = ("%.1f%%" % (100 * count / totals["downloads"] if totals["downloads"] else 0)) if totals else "—"
        lines.append("| %s | %s | %s |" % (name, metric(count), percent))
    if totals:
        types = totals["by_asset_type"]
        lines += ["", "| Asset format | Downloads |", "| --- | ---: |",
                  "| Installers / distributions | %s |" % metric(types["installer"]),
                  "| ZIP archives | %s |" % metric(totals["zip_downloads"]),
                  "| Other archives | %s |" % metric(types["archive"] - totals["zip_downloads"]),
                  "| Other files | %s |" % metric(types["other"])]
    lines += ["", "</details>", "", "## Repository traffic", "",
              "**Rolling 14-day window; these are repository visits and clones, not release downloads.**", "",
              "Traffic snapshot: %s." % markdown(component_label(traffic)), "",
              "| Views | Unique visitors | Clones | Unique cloners |", "| ---: | ---: | ---: | ---: |",
              "| %s | %s | %s | %s |" % tuple(metric(value) for value in (views.get("count"), views.get("uniques"), clones.get("count"), clones.get("uniques"))), "",
              "![Daily repository views and unique visitors](docs/release-stats/traffic-trend.svg)", "",
              "## Top referrers", "", "Top external referring websites in the traffic snapshot. Referrals do **not** establish where release downloads originated.", "",
              "| Referrer | Views | Unique visitors |", "| --- | ---: | ---: |"]
    referrers = traffic_data.get("referrers", [])
    external = [r for r in referrers if not r["self_referral"]][:10]
    for referrer in external:
        lines.append("| %s | %s | %s |" % (markdown(referrer["referrer"]), metric(referrer["count"]), metric(referrer["uniques"])))
    if not external:
        lines.append("| %s | — | — |" % ("No external referrers returned" if traffic_data else "Not yet collected / unavailable"))
    internal = [r for r in referrers if r["self_referral"]]
    if internal:
        lines += ["", "<details>", "<summary>GitHub self-referrals (excluded above)</summary>", "", "| Referrer | Views | Unique visitors |", "| --- | ---: | ---: |"]
        for referrer in internal:
            lines.append("| %s | %s | %s |" % (markdown(referrer["referrer"]), metric(referrer["count"]), metric(referrer["uniques"])))
        lines += ["", "</details>"]
    lines += ["", "## Popular pages", "", "Top repository pages in the same traffic snapshot.", "",
              "| Repository page | Views | Unique visitors |", "| --- | ---: | ---: |"]
    pages = traffic_data.get("popular_pages", [])[:10]
    for page in pages:
        label = page.get("title") or page["path"]
        label = label if len(label) <= 75 else label[:72] + "..."
        url = "https://github.com" + urllib.parse.quote(page["path"], safe="/")
        lines.append("| [%s](%s) | %s | %s |" % (markdown(label), url, metric(page["count"]), metric(page["uniques"])))
    if not pages:
        lines.append("| %s | — | — |" % ("No popular pages returned" if traffic_data else "Not yet collected / unavailable"))
    lines += ["", "## Data notes", "",
              "- Downloads are cumulative uploaded-asset counts, not unique users, installations or historical daily activity. Automatic GitHub source archives are excluded; manually uploaded assets remain included.",
              "- Stable tags match v3.0-dev-W2MWS.N; experimental tags match v3.0-dev-W2MWS-radev2.N. ZIP bundles are kept distinct and are not directly comparable to per-platform installers. Overview/platform totals cover all published releases; the release chart shows only the latest ten W2MWS releases.",
              "- Stars measure interest, subscribers are actual repository watchers, and forks are repository copies. Subscribers use subscribers_count, never watchers_count (which generally mirrors stars).",
              "- GitHub traffic covers a rolling 14-day window. Window uniques come directly from GitHub and must not be calculated by summing daily uniques. Missing data is not zero; the current day may be incomplete and later revised.",
              "- Traffic failures retain the last successful snapshot, marked stale with its original timestamp. Top lists are GitHub's limited results; after excluding self-referrals, fewer than ten external referrers may remain. Referrals do not attribute release downloads.",
              "- Historical download observations remain append-only. Daily traffic rows are upserted by UTC date from real API observations; overlaps replace older observations, without inventing earlier history. Deleted/replaced assets or counter revisions can affect apparent changes.",
              "- APIs are collected sequentially, not atomically. Each section displays its own successful collection time. An unavailable or stale section may coexist with fresh release statistics.", "",
              "[Current JSON](docs/release-stats/latest.json) · [Download history](docs/release-stats/history.csv) · [Repository history](docs/release-stats/repository-history.csv) · [Daily traffic](docs/release-stats/traffic-daily.csv) · [Referrers JSON](docs/release-stats/referrers-latest.json) · [Popular pages JSON](docs/release-stats/popular-pages-latest.json)", "",
              "Updated daily at 06:23 UTC or by manual workflow dispatch. Python 3 standard library only. GITHUB_TOKEN (or GH_TOKEN) reads releases/engagement; RELEASE_STATS_TOKEN reads traffic. Local collection: python scripts/release_stats.py.", ""]
    return "\n".join(lines)


def history_additions(path, snapshot):
    existing = set()
    has_content = path.exists() and path.stat().st_size > 0
    if has_content:
        with path.open(encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != list(HISTORY_FIELDS):
                raise StatsError("Unrecognized history.csv schema; existing history was not modified")
            for row in reader:
                if None in row or any(value is None for value in row.values()):
                    raise StatsError("Malformed history.csv; existing history was not modified")
                existing.add((row["observed_at"], row["release_id"], row["asset_id"]))
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, HISTORY_FIELDS, lineterminator="\n")
    if not has_content:
        writer.writeheader()
    for release in snapshot["releases"]:
        for asset in release["assets"]:
            key = (snapshot["observed_at"], str(release["id"]), str(asset["id"]))
            if key in existing:
                continue
            existing.add(key)
            writer.writerow({
                "observed_at": snapshot["observed_at"], "release_id": release["id"],
                "tag": release["tag"], "release_name": release["name"],
                "published_at": release["published_at"], "classification": release["classification"],
                "asset_id": asset["id"], "filename": asset["filename"],
                "download_count": asset["download_count"], "platform": asset["platform"],
                "asset_type": asset["asset_type"],
            })
    addition = output.getvalue()
    # Do not merge a row into an existing CSV that lacks a final newline.
    if addition and has_content:
        with path.open("rb") as stream:
            stream.seek(-1, os.SEEK_END)
            if stream.read(1) not in (b"\n", b"\r"):
                addition = "\n" + addition
    return addition


def write_if_changed(path, contents):
    data = contents.encode("utf-8")
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


REPOSITORY_HISTORY_FIELDS = (
    "observed_at", "stars", "subscribers", "forks", "views_14d", "unique_visitors_14d",
    "clones_14d", "unique_cloners_14d", "repository_observed_at", "traffic_observed_at",
)
DAILY_FIELDS = (
    "date", "views", "unique_visitors", "views_observed_at",
    "clones", "unique_cloners", "clones_observed_at",
)


def utc_time(value):
    if not isinstance(value, str):
        raise StatsError("Missing UTC timestamp")
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None or parsed.utcoffset().total_seconds() != 0:
        raise StatsError("Expected a UTC timestamp")
    return parsed


def collect_repository(token):
    data = get_json(API_ROOT, token)
    if not isinstance(data, dict) or data.get("full_name") != REPOSITORY:
        raise StatsError("Missing or unexpected repository metadata")
    return {"stars": integer(data.get("stargazers_count"), "stargazers_count"),
            "subscribers": integer(data.get("subscribers_count"), "subscribers_count"),
            "forks": integer(data.get("forks_count"), "forks_count")}


def traffic_counts(data, name):
    if not isinstance(data, dict):
        raise StatsError("Malformed " + name)
    count = integer(data.get("count"), name + ".count")
    uniques = integer(data.get("uniques"), name + ".uniques")
    if uniques > count:
        raise StatsError("Unique counts exceed totals in " + name)
    return {"count": count, "uniques": uniques}


def traffic_days(data, name):
    result = traffic_counts(data, name)
    if not isinstance(data.get(name), list):
        raise StatsError("Missing daily " + name + " data")
    days = []
    seen = set()
    for record in data[name]:
        item = traffic_counts(record, name + " day")
        day = utc_time(record.get("timestamp")).date().isoformat()
        if day in seen:
            raise StatsError("Duplicate daily " + name + " date")
        seen.add(day)
        item["date"] = day
        days.append(item)
    if not days and result["count"]:
        raise StatsError("Insufficient daily " + name + " data for the returned totals")
    result["days"] = sorted(days, key=lambda item: item["date"])
    return result


def collect_traffic(token):
    if not token:
        raise StatsError("RELEASE_STATS_TOKEN is not configured; traffic needs repository analytics access")
    views = traffic_days(get_json(API_ROOT + "/traffic/views?per=day", token), "views")
    clones = traffic_days(get_json(API_ROOT + "/traffic/clones?per=day", token), "clones")
    referrers = get_json(API_ROOT + "/traffic/popular/referrers", token)
    paths = get_json(API_ROOT + "/traffic/popular/paths", token)
    if not isinstance(referrers, list) or not isinstance(paths, list):
        raise StatsError("Missing or malformed popular traffic lists")
    normalized_referrers = []
    for record in referrers:
        item = traffic_counts(record, "referrer")
        name = record.get("referrer")
        if not isinstance(name, str) or not name.strip():
            raise StatsError("Missing referrer name")
        host = urllib.parse.urlsplit(name if "://" in name else "//" + name).hostname
        host = (host or "").lower().rstrip(".")
        item.update(referrer=name, self_referral=host == "github.com" or host.endswith(".github.com"))
        normalized_referrers.append(item)
    normalized_paths = []
    for record in paths:
        item = traffic_counts(record, "popular page")
        path = record.get("path")
        title = record.get("title") or path
        if (not isinstance(path, str) or not re.match(r"^/msteele999/freedv-gui(?:/|$)", path)
                or not isinstance(title, str)):
            raise StatsError("Unexpected repository page path/title")
        item.update(path=path, title=title)
        normalized_paths.append(item)
    return {"window_days": 14, "views": views, "clones": clones,
            "referrers": sorted(normalized_referrers, key=lambda item: (-item["count"], item["referrer"].casefold())),
            "popular_pages": sorted(normalized_paths, key=lambda item: (-item["count"], item["path"]))}


def load_previous(path):
    if not path.exists():
        return {"releases": [], "totals": None, "observed_at": None}
    with path.open(encoding="utf-8") as stream:
        previous = json.load(stream)
    if not isinstance(previous, dict) or previous.get("repository") != REPOSITORY:
        raise StatsError("Unrecognized latest.json; refusing to overwrite existing snapshots")
    return previous


def attempt_component(collector, previous, observed_at, name):
    try:
        data = collector()
        return {"status": "fresh", "observed_at": observed_at,
                "last_attempt_at": observed_at, "error": None, "data": data}
    except (StatsError, OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        message = safe_error(error)
        print("%s collection failed: %s" % (name, message), file=sys.stderr)
        # Preserve the successful data and its original time, never relabel it fresh.
        result = dict(previous or {})
        result.update(status="stale" if result.get("data") is not None else "unavailable",
                      observed_at=result.get("observed_at"), last_attempt_at=observed_at, error=message)
        result.setdefault("data", None)
        return result


def read_history(path, key, fields):
    if not path.exists() or not path.stat().st_size:
        return list(fields), []
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or key not in reader.fieldnames:
            raise StatsError("Unknown schema in %s; preserving existing history" % path.name)
        names = list(reader.fieldnames)
        rows = list(reader)
    if len(names) != len(set(names)) or any(None in row or any(v is None for v in row.values()) for row in rows):
        raise StatsError("Malformed %s; preserving existing history" % path.name)
    # Retain unknown columns and old rows when adding schema fields.
    names += [name for name in fields if name not in names]
    return names, rows


def csv_contents(fields, rows):
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fields, lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
    return output.getvalue()


def repository_history(path, observed_at, repository, traffic):
    fields, rows = read_history(path, "observed_at", REPOSITORY_HISTORY_FIELDS)
    if repository["status"] != "fresh" and traffic["status"] != "fresh":
        return None if path.exists() else csv_contents(fields, rows)
    addition = {name: "" for name in fields}
    addition["observed_at"] = observed_at
    if repository["status"] == "fresh":
        addition.update(repository["data"])
        addition["repository_observed_at"] = repository["observed_at"]
    if traffic["status"] == "fresh":
        data = traffic["data"]
        addition.update(views_14d=data["views"]["count"], unique_visitors_14d=data["views"]["uniques"],
                        clones_14d=data["clones"]["count"], unique_cloners_14d=data["clones"]["uniques"],
                        traffic_observed_at=traffic["observed_at"])
    existing = next((row for row in rows if row["observed_at"] == observed_at), None)
    if existing is None:
        rows.append(addition)
    else:
        # Same-timestamp retries fill fresh fields without replacing good data
        # with blanks when one independent collection is unavailable.
        existing.update({key: value for key, value in addition.items() if value != ""})
    return csv_contents(fields, rows)


def daily_history(path, traffic):
    fields, rows = read_history(path, "date", DAILY_FIELDS)
    if traffic["status"] != "fresh":
        return None if path.exists() else csv_contents(fields, rows)
    by_date = {}
    for row in rows:
        datetime.fromisoformat(row["date"])  # Reject malformed keys before writing.
        if row["date"] in by_date:
            raise StatsError("Duplicate dates in existing traffic history; preserving the file")
        by_date[row["date"]] = row
    for series, unique_key in (("views", "unique_visitors"), ("clones", "unique_cloners")):
        observed_key = series + "_observed_at"
        for day in traffic["data"][series]["days"]:
            row = by_date.setdefault(day["date"], dict.fromkeys(fields, ""))
            row["date"] = day["date"]
            previous_time = row.get(observed_key) or row.get("observed_at")
            if previous_time and utc_time(previous_time) > utc_time(traffic["observed_at"]):
                continue
            row.update({series: day["count"], unique_key: day["uniques"], observed_key: traffic["observed_at"]})
    return csv_contents(fields, [by_date[day] for day in sorted(by_date)])


def json_contents(data):
    return json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def unavailable_chart(title, explanation):
    svg = svg_start(800, 180, title, explanation)
    svg += [text(20, 35, title, 20), text(20, 100, explanation, 14, "#475569")]
    return "\n".join(svg + ["</g></svg>", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timestamp", help="Optional observation time in ISO 8601 UTC (ending Z); useful for repeatable imports")
    args = parser.parse_args()
    observed = datetime.now(timezone.utc)
    if args.timestamp:
        if not args.timestamp.endswith("Z"):
            raise StatsError("--timestamp must use UTC and end with Z")
        observed = utc_time(args.timestamp)
    observed_at = observed.strftime("%Y-%m-%dT%H:%M:%SZ")
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    directory = ROOT / "docs" / "release-stats"
    previous = load_previous(directory / "latest.json")
    old_releases = previous.get("releases", [])
    old_release_state = dict(previous.get("release_collection") or {})
    old_release_state["observed_at"] = previous.get("observed_at")
    old_release_state["data"] = old_releases if previous.get("totals") is not None else None
    # Release, repository and traffic requests are independent. Fetch and render
    # all components before writing any data; traffic is published as one unit.
    release_collection = attempt_component(lambda: collect(token), old_release_state, observed_at, "Release")
    releases = release_collection["data"]
    if releases is None:
        releases = old_releases
    repository = attempt_component(lambda: collect_repository(token), previous.get("repository_metrics"), observed_at, "Repository")
    traffic = attempt_component(lambda: collect_traffic(os.environ.get("RELEASE_STATS_TOKEN")), previous.get("traffic"), observed_at, "Traffic")
    snapshot = dict(previous)
    release_time = release_collection.get("observed_at")
    successful_times = [component["observed_at"] for component in (release_collection, repository, traffic) if component.get("observed_at")]
    snapshot.update(schema_version=2, repository=REPOSITORY,
                    status="collected" if release_time else "not_collected",
                    observed_at=release_time, last_attempt_at=observed_at,
                    last_successful_collection_at=max(successful_times, default=None),
                    releases=releases, totals=summarize(releases) if release_time else None,
                    repository_metrics=repository, traffic=traffic)
    # Avoid a second complete per-asset copy in the collection metadata.
    snapshot["release_collection"] = {key: value for key, value in release_collection.items() if key != "data"}
    history = directory / "history.csv"
    addition = history_additions(history, snapshot) if release_collection["status"] == "fresh" else ""
    outputs = {
        directory / "latest.json": json_contents(snapshot),
        directory / "downloads-by-release.svg": bar_chart(releases) if release_time else unavailable_chart("Downloads by release", "Release data unavailable; no counts were fabricated."),
        directory / "downloads-by-platform.svg": pie_chart(snapshot["totals"]) if snapshot["totals"] else unavailable_chart("Downloads by platform", "Release data unavailable; no counts were fabricated."),
        directory / "traffic-trend.svg": traffic_chart(traffic),
        ROOT / "RELEASE-STATISTICS.md": statistics_page(snapshot),
    }
    for filename, key in (("referrers-latest.json", "referrers"), ("popular-pages-latest.json", "popular_pages")):
        outputs[directory / filename] = json_contents({
            "schema_version": 1, "repository": REPOSITORY, "window_days": 14,
            "status": traffic["status"], "observed_at": traffic.get("observed_at"),
            "last_attempt_at": observed_at, "error": traffic.get("error"),
            "data": traffic["data"][key] if traffic.get("data") is not None else None,
        })
    for path, contents in (
        (directory / "repository-history.csv", repository_history(directory / "repository-history.csv", observed_at, repository, traffic)),
        (directory / "traffic-daily.csv", daily_history(directory / "traffic-daily.csv", traffic)),
    ):
        if contents is not None:
            outputs[path] = contents
    # Preserve release CSV bytes exactly and append only new download rows.
    # New CSVs and snapshot/visual files use atomic per-file replacements.
    directory.mkdir(parents=True, exist_ok=True)
    if addition:
        with history.open("a", encoding="utf-8", newline="") as stream:
            stream.write(addition)
    for path, contents in outputs.items():
        write_if_changed(path, contents)
    print("Collection status: releases=%s, engagement=%s, traffic=%s; attempt %s" % (
        release_collection["status"], repository["status"], traffic["status"], observed_at))


if __name__ == "__main__":
    try:
        main()
    except (StatsError, OSError, ValueError, KeyError, TypeError) as error:
        print("release_stats: %s" % safe_error(error), file=sys.stderr)
        sys.exit(1)
