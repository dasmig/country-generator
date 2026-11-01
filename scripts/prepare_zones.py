#!/usr/bin/env python3
"""Write country-generator's zones.tsv and zone_links.tsv.

Sources:
  - IANA tzdata (public domain), as compiled by zic into the pinned `tzdata`
    package on PyPI: the TZif file of every zone, `zone.tab` (each zone's
    country) and `tzdata.zi` (the links, old name to current name).
  - GeoNames `cities1000` (CC BY 4.0): the population of each place and its
    time zone, for the share of a country's people in each of its zones.

For each zone, the periods run from the tier's start (2000-01-01 in lite,
1970-01-01 in full) up to the last transition the zone's POSIX rule (the TZif
footer) does not reproduce; the rule computes every transition after that.
The text form is dasmig-core's `tz_rules` (`start/offset/abbreviation/s|d`).
Every zone is checked against Python's zoneinfo, reading the same TZif bytes:
the offset and abbreviation must agree at every transition and every month
from the tier's start to 2060, as dasmig-core's `tz_rules::at` computes them.

A country with no zone.tab row takes the zones GeoNames gives its places
(basis `geonames`, Kosovo's Europe/Belgrade), or, with no places, one fixed
zone from the first UTC offset of countries.tsv's `timezones` column, in
IANA's `Etc` form (basis `fallback`, `Etc/GMT-1` for UTC+01:00).

Usage:
    python scripts/prepare_zones.py              # both tiers
    python scripts/prepare_zones.py --tier lite

Run it from the repository root. Downloads are cached in build/cache.
"""

import argparse
import bisect
import datetime
import hashlib
import io
import json
import re
import struct
import urllib.request
import zipfile
import zoneinfo
from collections import defaultdict
from pathlib import Path

TZDATA_VERSION = "2026.6"          # PyPI package; IANA release in its IANA_VERSION
_PYPI = f"https://pypi.org/pypi/tzdata/{TZDATA_VERSION}/json"
_GEONAMES = "https://download.geonames.org/export/dump/cities1000.zip"

TIER_START = {"lite": 946684800, "full": 0}      # 2000-01-01, 1970-01-01
CHECK_UNTIL = 2060

_DAY = 86400


# ---------------------------------------------------------------------------
# Downloads
# ---------------------------------------------------------------------------

def download(url, cache, name):
    path = Path(cache) / name
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        request = urllib.request.Request(url, headers={"User-Agent": "country-generator data script"})
        with urllib.request.urlopen(request, timeout=300) as response:
            path.write_bytes(response.read())
    return path.read_bytes()


def tzdata_wheel(cache):
    """The pinned tzdata wheel, its sha256 checked against PyPI's."""
    meta = json.loads(download(_PYPI, cache, f"tzdata-{TZDATA_VERSION}.json"))
    wheel = next(u for u in meta["urls"] if u["packagetype"] == "bdist_wheel")
    data = download(wheel["url"], cache, wheel["filename"])
    if hashlib.sha256(data).hexdigest() != wheel["digests"]["sha256"]:
        raise SystemExit(f"{wheel['filename']}: sha256 does not match PyPI")
    return zipfile.ZipFile(io.BytesIO(data))


# ---------------------------------------------------------------------------
# TZif (RFC 8536), version 2 and later
# ---------------------------------------------------------------------------

class TZif:
    def __init__(self, data):
        if data[:4] != b"TZif" or data[4:5] < b"2":
            raise ValueError("not a TZif v2+ file")
        counts = struct.unpack(">6l", data[20:44])
        isut, isstd, leap, time, typ, char = counts
        skip = 44 + time * 5 + typ * 6 + char + leap * 8 + isstd + isut
        isut, isstd, leap, time, typ, char = struct.unpack(">6l", data[skip + 20:skip + 44])
        p = skip + 44
        self.times = list(struct.unpack(f">{time}q", data[p:p + time * 8]))
        p += time * 8
        self.indices = list(data[p:p + time])
        p += time
        self.types = []
        for i in range(typ):
            off, dst, abbr = struct.unpack(">lBB", data[p + i * 6:p + i * 6 + 6])
            self.types.append([off, bool(dst), abbr])
        p += typ * 6
        chars = data[p:p + char]
        for t in self.types:
            t[2] = chars[t[2]:chars.index(b"\0", t[2])].decode("ascii")
        self.types = [tuple(t) for t in self.types]
        p += char + leap * 12 + isstd + isut
        self.footer = data[p:].strip(b"\n").decode("ascii")

    def state_at(self, t):
        """The listed (offset, dst, abbreviation) at t, or None where the footer applies."""
        if self.times and t < self.times[0]:
            return self.types[0]
        if self.footer and (not self.times or t > self.times[-1]):
            return None
        if not self.times:
            return self.types[0]
        return self.types[self.indices[bisect.bisect_right(self.times, t) - 1]]


# ---------------------------------------------------------------------------
# POSIX TZ rules, evaluated as dasmig-core's tz_rules does (core design 5.3-5.5)
# ---------------------------------------------------------------------------

_NAME = r"(<[A-Za-z0-9+-]{3,}>|[A-Za-z]{3,})"
_OFFSET = r"([+-]?\d{1,3}(?::\d{1,2}){0,2})"
_POSIX = re.compile(rf"^{_NAME}{_OFFSET}(?:{_NAME}{_OFFSET}?(?:,([^,]+),([^,]+))?)?$")


def _hms(text):
    sign = -1 if text.startswith("-") else 1
    parts = [int(x) for x in text.lstrip("+-").split(":")] + [0, 0]
    return sign * (parts[0] * 3600 + parts[1] * 60 + parts[2])


def _rule_date(spec, year):
    """Days since 1970-01-01 of a POSIX rule date in a year."""
    jan1 = datetime.date(year, 1, 1).toordinal() - datetime.date(1970, 1, 1).toordinal()
    if spec[0] == "J":
        n = int(spec[1:])
        leap = year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)
        return jan1 + n - 1 + (1 if leap and n >= 60 else 0)
    if spec[0] == "M":
        m, w, d = (int(x) for x in spec[1:].split("."))
        first = datetime.date(year, m, 1)
        day = 1 + (d - first.isoweekday() % 7) % 7 + 7 * (w - 1)
        last = (datetime.date(year + (m == 12), m % 12 + 1, 1) - datetime.timedelta(days=1)).day
        while day > last:
            day -= 7
        return datetime.date(year, m, day).toordinal() - datetime.date(1970, 1, 1).toordinal()
    return jan1 + int(spec)


class Posix:
    def __init__(self, text):
        self.text = text
        m = _POSIX.match(text)
        if not m:
            raise ValueError(f"bad POSIX rule {text!r}")
        std, std_off, dst, dst_off, start, end = m.groups()
        self.std = (-_hms(std_off), False, std.strip("<>"))
        self.dst = None
        if dst:
            off = -_hms(dst_off) if dst_off else self.std[0] + 3600
            self.dst = (off, True, dst.strip("<>"))
            self.start, self.end = self._rule(start), self._rule(end)

    @staticmethod
    def _rule(spec):
        date, _, time = spec.partition("/")
        return date, _hms(time) if time else 7200

    def transitions(self, year):
        """[(utc, state)] for one year, in order."""
        if not self.dst:
            return []
        s = _rule_date(self.start[0], year) * _DAY + self.start[1] - self.std[0]
        e = _rule_date(self.end[0], year) * _DAY + self.end[1] - self.dst[0]
        return sorted([(s, self.dst), (e, self.std)], key=lambda x: x[0])


def _year(t):
    return datetime.datetime.fromtimestamp(t, datetime.timezone.utc).year if t >= -62135596800 else 1


def core_at(periods, starts, posix, t):
    """dasmig-core's tz_rules::at over (start, state) periods and a Posix rule."""
    last_start = starts[-1]
    if t < last_start or posix is None or posix.dst is None:
        return periods[bisect.bisect_right(starts, t) - 1][1]
    y = _year(t)
    best = None
    for year in (y - 1, y, y + 1):
        for when, state in posix.transitions(year):
            if last_start < when <= t and (best is None or when >= best[0]):
                best = (when, state)
    return best[1] if best else periods[-1][1]


# ---------------------------------------------------------------------------
# Periods
# ---------------------------------------------------------------------------

def check_points(tzif, posix, start):
    points = set()
    for t in tzif.times:
        if t >= start:
            points.update((t - 1, t))
    first = _year(start)
    for year in range(first, CHECK_UNTIL + 1):
        for month in range(1, 13):
            points.add(int(datetime.datetime(year, month, 15, tzinfo=datetime.timezone.utc).timestamp()))
        if posix and posix.dst:
            for when, _ in posix.transitions(year):
                points.update((when - 1, when))
    return sorted(p for p in points if p >= start)


def zone_periods(name, tzif_bytes, start):
    """The shortest period list from `start` that, with the footer, matches zoneinfo."""
    tzif = TZif(tzif_bytes)
    posix = Posix(tzif.footer) if tzif.footer else None
    zi = zoneinfo.ZoneInfo.from_file(io.BytesIO(tzif_bytes), key=name)

    def expected(t):
        """zoneinfo's offset and abbreviation; the dst flag from the TZif type, or
        in the footer's region from zoneinfo's dst()."""
        local = datetime.datetime.fromtimestamp(t, zi)
        off, abbr = int(local.utcoffset().total_seconds()), local.tzname()
        state = tzif.state_at(t)
        return (off, state[1] if state is not None else bool(local.dst()), abbr)

    # Every transition from `start` to the end of the TZif data. A transition that
    # changes nothing can still matter as the last one, where the footer starts
    # (Macquarie's 2010-12-31), so they are dropped only after the cut.
    full = [(start, expected(start))] + [(t, expected(t)) for t in tzif.times if t > start]
    points = check_points(tzif, posix, start)
    truth = {t: expected(t) for t in points}

    def good(periods):
        starts = [p[0] for p in periods]
        return all(core_at(periods, starts, posix, t) == truth[t] for t in points)

    lo, hi = 0, len(full) - 1
    if not good(full):
        raise SystemExit(f"{name}: the TZif periods and footer do not reproduce zoneinfo")
    while lo < hi:
        mid = (lo + hi) // 2
        if good(full[:mid + 1]):
            hi = mid
        else:
            lo = mid + 1
    periods = [p for i, p in enumerate(full[:lo + 1]) if i == 0 or i == lo or p[1] != full[i - 1][1]]
    if not good(periods):
        raise SystemExit(f"{name}: dropping the no-op transitions changed the zone")
    for when, (off, _, abbr) in periods:
        if not re.fullmatch(r"[A-Za-z0-9+-]{3,6}", abbr) or abs(off) >= _DAY:
            raise SystemExit(f"{name}: period {when} has {off} {abbr!r}, outside tz_rules' limits")
    text = " ".join(f"{when}/{off}/{abbr}/{'d' if dst else 's'}" for when, (off, dst, abbr) in periods)
    return text, tzif.footer


def fixed_zone(offset_text):
    """UTC+03:00 -> Etc/GMT-3 (IANA's Etc names invert the sign)."""
    m = re.fullmatch(r"UTC([+-])(\d\d):(\d\d)", offset_text.strip())
    if not m or m[3] != "00":
        raise SystemExit(f"no Etc zone for UTC offset {offset_text!r}")
    hours = int(m[2])
    return f"Etc/GMT{'-' if m[1] == '+' else '+'}{hours}" if hours else "Etc/GMT"


# ---------------------------------------------------------------------------
# Countries, zones, links, shares
# ---------------------------------------------------------------------------

def read_countries(path):
    lines = [l for l in Path(path).read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    header = lines[0].split("\t")
    rows = [dict(zip(header, l.split("\t"))) for l in lines[1:]]
    return {r["cca2"]: r for r in rows}


def read_links(zi_text):
    links = {}
    for line in zi_text.splitlines():
        if line.startswith("L "):
            _, target, name = line.split()
            links[name] = target
    for name in links:                          # resolve chains
        seen = {name}
        while links[name] in links and links[name] not in seen:
            seen.add(links[name])
            links[name] = links[links[name]]
    return links


def geonames_population(data):
    """(country, timezone) -> population of the GeoNames places there."""
    pops = defaultdict(int)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        with z.open("cities1000.txt") as f:
            for line in io.TextIOWrapper(f, encoding="utf-8"):
                cells = line.rstrip("\n").split("\t")
                if len(cells) > 17 and cells[17]:
                    pops[(cells[8], cells[17])] += int(cells[14] or 0)
    return pops


def share_text(x):
    return f"{round(x, 1):g}" if x >= 1 else f"{x:.2g}"


def build(countries, zone_tab, links, pops, tzfiles, start):
    """Rows (country, zone, share, basis, periods, posix) and a report."""
    by_country = defaultdict(list)
    for zone, cc in zone_tab.items():
        by_country[cc].append(zone)
    canonical = lambda z: z if z in zone_tab else links.get(z, z)
    per_zone = defaultdict(lambda: defaultdict(int))
    foreign = defaultdict(int)
    for (cc, tz), pop in pops.items():
        if cc not in countries:
            continue
        zones = by_country.get(cc)
        if zones is None:                       # no zone.tab row: GeoNames decides, links resolved
            per_zone[cc][links.get(tz, tz)] += pop
            continue
        if tz in zones:
            per_zone[cc][tz] += pop
        elif canonical(tz) in zones:
            per_zone[cc][canonical(tz)] += pop
        elif pop:
            foreign[cc] += pop
    rows, report = [], []
    for cc in sorted(countries):
        zones, basis = by_country.get(cc), "tzdata"
        if zones is None:
            zones = sorted(z for z, p in per_zone[cc].items() if p) if per_zone[cc] else []
            basis = "geonames"
        if not zones:
            name = fixed_zone(countries[cc]["timezones"].split(";")[0])
            report.append(f"{cc}: no zone.tab row and no GeoNames place; fallback {name}")
            zones, basis = [name], "fallback"
        elif basis == "geonames":
            report.append(f"{cc}: no zone.tab row; GeoNames places it in {', '.join(zones)}")
        total = sum(per_zone[cc].get(z, 0) for z in zones)
        shares = {z: 100.0 * per_zone[cc].get(z, 0) / total for z in zones} if total else {}
        texts = {z: share_text(s) for z, s in shares.items() if s > 0}
        smallest = min(texts.values(), key=float) if texts else share_text(100.0 / len(zones))
        for zone in sorted(zones, key=lambda z: (-shares.get(z, 0), z)):
            periods, posix = zone_periods(zone, tzfiles(zone), start)
            rows.append((cc, zone, texts.get(zone, smallest), basis, periods, posix))
        if foreign.get(cc):
            report.append(f"{cc}: {foreign[cc]} people in GeoNames places with another country's zone, left out")
    return rows, report


def write(path, header_comment, columns, rows):
    out = [f"# {line}" for line in header_comment] + ["\t".join(columns)]
    out += ["\t".join(str(c) for c in row) for row in rows]
    Path(path).write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    return Path(path).stat().st_size


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tier", choices=["lite", "full", "both"], default="both")
    ap.add_argument("--resources", default="resources")
    ap.add_argument("--cache", default="build/cache")
    a = ap.parse_args()

    wheel = tzdata_wheel(a.cache)
    root = "tzdata/zoneinfo/"
    iana = re.search(r'IANA_VERSION\s*=\s*"([^"]+)"', wheel.read("tzdata/__init__.py").decode())[1]
    zone_tab = {}
    for line in wheel.read(root + "zone.tab").decode("utf-8").splitlines():
        if line and not line.startswith("#"):
            cells = line.split("\t")
            zone_tab[cells[2]] = cells[0]
    links = read_links(wheel.read(root + "tzdata.zi").decode("utf-8"))
    pops = geonames_population(download(_GEONAMES, a.cache, "cities1000.zip"))
    geonames_date = datetime.datetime.fromtimestamp(
        (Path(a.cache) / "cities1000.zip").stat().st_mtime, datetime.timezone.utc).date()
    tzfiles = lambda zone: wheel.read(root + zone)

    for tier in (["lite", "full"] if a.tier == "both" else [a.tier]):
        countries = read_countries(Path(a.resources) / tier / "countries.tsv")
        rows, report = build(countries, zone_tab, links, pops, tzfiles, TIER_START[tier])
        start = datetime.datetime.fromtimestamp(TIER_START[tier], datetime.timezone.utc).date()
        source = [f"source: IANA tzdata {iana} (PyPI tzdata {TZDATA_VERSION}), "
                  f"GeoNames cities1000 of {geonames_date} (CC BY 4.0)",
                  "written by scripts/prepare_zones.py"]
        size = write(Path(a.resources) / tier / "zones.tsv", source + [f"periods from {start}"],
                     ["country", "zone", "share", "basis", "periods", "posix"], rows)
        loaded = {r[1] for r in rows}
        link_rows = sorted((name, target) for name, target in links.items()
                           if name not in loaded and target in loaded)
        link_size = write(Path(a.resources) / tier / "zone_links.tsv", [f"source: IANA tzdata {iana}"] + source[1:],
                          ["link", "zone"], link_rows)
        periods = sum(len(r[4].split()) for r in rows)
        print(f"{tier}: {len(rows)} zones in {len({r[0] for r in rows})} countries, {periods} periods, "
              f"zones.tsv {size / 1024:.0f} KB; {len(link_rows)} links, {link_size / 1024:.0f} KB")
        for line in report:
            print(f"  {line}")


if __name__ == "__main__":
    main()
