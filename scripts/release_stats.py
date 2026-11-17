#!/usr/bin/env python3
"""Collect cumulative GitHub release-asset downloads; Python standard library only.

Run from any directory. Generated files are written to this script's repository.
GITHUB_TOKEN (or GH_TOKEN) is optional for local public-API access.
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
                raise StatsError("GitHub API HTTP %s: %s" % (error.code, message)) from error
            delay = retry_delay(error.headers, attempt, limited)
            detail = "GitHub API HTTP %s: %s" % (error.code, message)
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            delay = 2 ** (attempt + 1)
            detail = "GitHub API network error: %s" % error
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
    shown = [release for release in releases if release["is_w2mws"]]
    height = max(240, 155 + len(shown) * 40)
    svg = svg_start(1000, height, "Downloads by W2MWS release", "Cumulative uploaded-asset downloads; stable releases blue, experimental releases violet.")
    svg += [text(28, 36, "Downloads by W2MWS release", 22),
            text(28, 60, "Cumulative asset downloads · oldest to newest · ZIPs and installers included", 13, "#475569")]
    for index, (key, label) in enumerate((("stable", "Stable"), ("experimental", "Experimental"), ("other", "Other W2MWS tags"))):
        x = 28 + index * 230
        svg += ['<rect x="%s" y="79" width="12" height="12" rx="2" fill="%s"/>' % (x, RELEASE_COLORS[key]), text(x + 20, 90, label, 13)]
    if not shown:
        svg.append(text(28, 150, "No published W2MWS releases.", 16, "#475569"))
    else:
        maximum = max(release["download_count"] for release in shown) or 1
        ticks = min(4, maximum)
        axis_max = math.ceil(maximum / ticks) * ticks
        for index in range(ticks + 1):
            x = 240 + index * 620 / ticks
            svg += ['<path d="M %.1f 108 V %s" stroke="#e2e8f0"/>' % (x, height - 42),
                    text(round(x, 1), height - 18, "{:,}".format(axis_max * index // ticks), 12, "#64748b", "middle")]
        for index, release in enumerate(shown):
            y = 118 + index * 40
            count = release["download_count"]
            width = 620 * count / axis_max
            svg += [text(28, y + 19, version_label(release)),
                    '<rect x="240" y="%s" width="%.2f" height="26" rx="4" fill="%s"><title>%s</title></rect>' % (y, width, RELEASE_COLORS[release["classification"]], html.escape(release["tag"] + ": " + str(count))),
                    text(round(250 + width, 2), y + 19, "{:,}".format(count), 14)]
    return "\n".join(svg + ["</g></svg>", ""])


def pie_chart(totals):
    total = totals["downloads"]
    svg = svg_start(1000, 420, "Downloads by platform", "Distribution of cumulative downloads across six platform categories. Counts include uploaded ZIP archives.")
    svg += [text(28, 36, "Downloads by platform", 22), text(28, 60, "All published release assets · cumulative counts", 13, "#475569")]
    angle = -math.pi / 2
    if not total:
        svg += ['<circle cx="190" cy="235" r="130" fill="#f1f5f9"/>', text(190, 240, "No downloads yet", 16, "#475569", "middle")]
    for index, name in enumerate(PLATFORMS):
        count = totals["by_platform"][name]
        fraction = count / total if total else 0
        end = angle + 2 * math.pi * fraction
        color = PLATFORM_COLORS[index]
        if count == total and total:
            svg.append('<circle cx="190" cy="235" r="130" fill="%s"/>' % color)
        elif count:
            x1, y1 = 190 + 130 * math.cos(angle), 235 + 130 * math.sin(angle)
            x2, y2 = 190 + 130 * math.cos(end), 235 + 130 * math.sin(end)
            svg.append('<path d="M 190 235 L %.3f %.3f A 130 130 0 %s 1 %.3f %.3f Z" fill="%s" stroke="#ffffff" stroke-width="2"/>' % (x1, y1, int(fraction > 0.5), x2, y2, color))
        angle = end
        y = 115 + index * 40
        svg += ['<rect x="380" y="%s" width="14" height="14" rx="3" fill="%s"/>' % (y - 12, color),
                text(405, y, name, 15), text(950, y, "{:,}  ({:.1%})".format(count, fraction), 15, anchor="end")]
    svg.append(text(380, 380, "Total asset downloads: {:,}".format(total), 17))
    return "\n".join(svg + ["</g></svg>", ""])


def markdown(value):
    value = html.escape(str(value).replace("\r", " ").replace("\n", " "))
    return re.sub(r"([\\`*_{}\[\]()#+.!|>~-])", r"\\\1", value)


def statistics_page(snapshot):
    totals = snapshot["totals"]
    classes = totals["by_classification"]
    lines = [
        "# GitHub Release Statistics", "",
        "Cumulative downloads of uploaded release assets for [msteele999/freedv-gui](https://github.com/msteele999/freedv-gui/releases).",
        "", "Last updated: **%s (UTC)**." % snapshot["observed_at"], "",
        "## Overview", "",
        "**Total asset downloads: {:,}** across {:,} published releases and {:,} uploaded assets.".format(totals["downloads"], totals["releases"], totals["assets"]), "",
        "| Release family | Downloads |", "| --- | ---: |",
        "| Stable W2MWS | {:,} |".format(classes["stable"]),
        "| Experimental RADEV2 | {:,} |".format(classes["experimental"]),
        "| Other / unclassified releases | {:,} |".format(classes["other"]), "",
        "Stable tags match v3.0-dev-W2MWS.N; experimental tags match v3.0-dev-W2MWS-radev2.N. Classification follows tags, not GitHub's prerelease flag.", "",
        "## Downloads by release", "",
        "![Cumulative downloads for every published W2MWS release](docs/release-stats/downloads-by-release.svg)", "",
        "| Release | Name | Family | Published (UTC) | Downloads |", "| --- | --- | --- | --- | ---: |",
    ]
    shown = [release for release in reversed(snapshot["releases"]) if release["is_w2mws"]]
    for release in shown:
        url = "https://github.com/%s/releases/tag/%s" % (REPOSITORY, urllib.parse.quote(release["tag"], safe=""))
        lines.append("| [%s](%s) | %s | %s | %s | %s |" % (markdown(release["tag"]), url, markdown(release["name"]), release["classification"].title(), markdown(release["published_at"]), "{:,}".format(release["download_count"])))
    if not shown:
        lines.append("| No published W2MWS releases | — | — | — | 0 |")
    lines += ["", "The bar chart includes every published W2MWS tag. Overview and platform totals include all published releases in this repository, including unclassified tags.", "",
              "## Downloads by platform", "",
              "![Cumulative release asset downloads by platform](docs/release-stats/downloads-by-platform.svg)", "",
              "| Platform | Downloads | Share |", "| --- | ---: | ---: |"]
    for name in PLATFORMS:
        count = totals["by_platform"][name]
        lines.append("| %s | %s | %.1f%% |" % (name, "{:,}".format(count), 100 * count / totals["downloads"] if totals["downloads"] else 0))
    types = totals["by_asset_type"]
    lines += ["", "## Asset formats", "", "| Asset type | Downloads |", "| --- | ---: |",
              "| Installers / distributions (EXE, AppImage, DMG) | {:,} |".format(types["installer"]),
              "| ZIP archives | {:,} |".format(totals["zip_downloads"]),
              "| Other archives | {:,} |".format(types["archive"] - totals["zip_downloads"]),
              "| Other files | {:,} |".format(types["other"]), "",
              "## Interpreting these numbers", "",
              "- GitHub provides cumulative downloads per uploaded release asset, not historical daily download activity. These counts are not unique users or confirmed installations; repeat or automated downloads can contribute.",
              "- Older ZIP releases are not directly comparable to installer-based releases: one ZIP may bundle several platforms or files, while an installer download usually selects one platform. ZIPs remain separately identified in the data and format totals.",
              "- GitHub's automatically generated Source code (zip) and Source code (tar.gz) links are excluded. Manually uploaded source ZIPs, checksums and other files remain uploaded assets and are included.",
              "- Platforms are inferred from filenames. Missing or ambiguous platform/architecture markers go into Other / Unknown rather than guessing. macOS assets share one category, including universal binaries.",
              "- The collector appends UTC observations to [history.csv](docs/release-stats/history.csv). History begins with the first collection; it cannot reconstruct earlier daily activity. Differences between observations may be affected by deleted/replaced assets and counter changes.",
              "- Collection is paginated and sequential, not an atomic GitHub-wide snapshot: counts can change while the API is being read. Failed API collections leave existing generated data untouched.", "",
              "## Data and updates", "",
              "[Current JSON snapshot](docs/release-stats/latest.json) · [Observation history](docs/release-stats/history.csv) · [GitHub API documentation](https://docs.github.com/en/rest/releases/assets)", "",
              "The scheduled workflow refreshes this page daily at 06:23 UTC and supports manual dispatch. Run locally from the repository root with:", "",
              "    python scripts/release_stats.py", "",
              "Only Python 3's standard library is required. GITHUB_TOKEN (or GH_TOKEN) is optional locally and recommended for the higher authenticated API rate limit.", ""]
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timestamp", help="Optional observation time in ISO 8601 UTC (ending Z); useful for repeatable imports")
    args = parser.parse_args()
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    # Complete every API request before creating output or appending history.
    releases = collect(token)
    observed = datetime.now(timezone.utc)
    if args.timestamp:
        if not args.timestamp.endswith("Z"):
            raise StatsError("--timestamp must use UTC and end with Z")
        observed = datetime.fromisoformat(args.timestamp.replace("Z", "+00:00"))
    observed_at = observed.strftime("%Y-%m-%dT%H:%M:%SZ")
    snapshot = {"schema_version": 1, "repository": REPOSITORY, "status": "collected",
                "observed_at": observed_at, "totals": summarize(releases), "releases": releases}
    directory = ROOT / "docs" / "release-stats"
    history = directory / "history.csv"
    addition = history_additions(history, snapshot)
    outputs = {
        directory / "latest.json": json.dumps(snapshot, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        directory / "downloads-by-release.svg": bar_chart(releases),
        directory / "downloads-by-platform.svg": pie_chart(snapshot["totals"]),
        ROOT / "RELEASE-STATISTICS.md": statistics_page(snapshot),
    }
    # Render everything before any writes. Keep old CSV bytes and append only.
    directory.mkdir(parents=True, exist_ok=True)
    if addition:
        with history.open("a", encoding="utf-8", newline="") as stream:
            stream.write(addition)
    for path, contents in outputs.items():
        write_if_changed(path, contents)
    print("Collected %s releases, %s assets, %s cumulative downloads at %s" % (
        snapshot["totals"]["releases"], snapshot["totals"]["assets"],
        snapshot["totals"]["downloads"], observed_at))


if __name__ == "__main__":
    try:
        main()
    except (StatsError, OSError, ValueError, KeyError, TypeError) as error:
        print("release_stats: %s" % error, file=sys.stderr)
        sys.exit(1)
