# SPDX-License-Identifier: MIT
# Copyright (c) 2020-2026 Diego Dasso Migotto

"""Compare the library's time zones with Python's zoneinfo, every zone of a tier.

The library's answers come from the dump program (tests/dump.cpp), which loads
a tier and prints each country's zones: the rules at the zone's first instant
(the tier's start), then every change before 2038 with the rules one second
before it and at it. zoneinfo reads the pinned tzdata package, the IANA release
the tables were built from (design 6.2 and 13.2). For each zone:

- the rules at the first instant, and one second before and at each of the
  library's changes, must be zoneinfo's (offset, abbreviation and daylight flag);
- zoneinfo's own changes, found by sampling every day and bisecting to the
  second, must be the library's changes, so none is missing.

Usage:
    clang++ -std=c++23 -O2 -I. -o build/dump tests/dump.cpp
    python scripts/validate_zones.py build/dump --tier full [--zones Europe/Berlin]

Run it from the repository root with the pinned tzdata package
(pip install tzdata==2026.6). Exits non-zero when anything differs.
"""

import argparse
import datetime
import subprocess
import sys
import zoneinfo
from pathlib import Path

import tzdata

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_zones import TZDATA_VERSION  # noqa: E402  (the release the tables were built from)

END = 2145916800    # 2038-01-01T00:00:00Z
DAY = 86400


def ours(dump, tier_dir):
    """[(country, zone, first instant, rules there, [(instant, rules before, rules at)])]."""
    text = subprocess.run([str(Path(dump).resolve()), "zones", tier_dir], check=True, capture_output=True,
                          text=True, encoding="utf-8").stdout
    zones = []
    for line in text.splitlines():
        f = line.split("\t")
        if f[0] == "Z":
            zones.append((f[1], f[2], int(f[3]), (int(f[4]), f[5], f[6] == "1"), []))
        else:
            zones[-1][4].append((int(f[1]), (int(f[2]), f[3], f[4] == "1"), (int(f[5]), f[6], f[7] == "1")))
    return zones


class Theirs:
    """zoneinfo's rules for one zone: (offset seconds, abbreviation, daylight)."""

    def __init__(self, name):
        self.zone = zoneinfo.ZoneInfo(name)

    def at(self, t):
        d = datetime.datetime.fromtimestamp(t, tz=self.zone)
        return int(d.utcoffset().total_seconds()), d.tzname(), bool(d.dst())

    def changes(self, start, end):
        """Every change in (start, end): sampled daily, each bisected to the second."""
        out = []
        a, info_a = start, self.at(start)
        while a < end:
            b = min(a + DAY, end)
            info_b = self.at(b)
            while info_a != info_b:
                lo, hi = a, b    # info(lo) == info_a, info(hi) != info_a
                while hi - lo > 1:
                    mid = (lo + hi) // 2
                    if self.at(mid) == info_a:
                        lo = mid
                    else:
                        hi = mid
                after = self.at(hi)
                out.append((hi, info_a, after))
                a, info_a = hi, after
            a, info_a = b, info_b
        return out


def show(info):
    offset, abbreviation, dst = info
    sign = "-" if offset < 0 else "+"
    h, m = divmod(abs(offset) // 60, 60)
    return f"{sign}{h:02}:{m:02} {abbreviation}{' dst' if dst else ''}"


def when(t):
    return datetime.datetime.fromtimestamp(t, tz=datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def compare(theirs, first, info, changes):
    """The differences for one zone, as text."""
    diffs = []
    if theirs.at(first) != info:
        diffs.append(f"at {when(first)}: library {show(info)}, zoneinfo {show(theirs.at(first))}")
    for t, before, after in changes:
        for at, mine in ((t - 1, before), (t, after)):
            if theirs.at(at) != mine:
                diffs.append(f"at {when(at)}: library {show(mine)}, zoneinfo {show(theirs.at(at))}")
    ours_at = {t for t, _, _ in changes}
    for t, before, after in theirs.changes(first, END):
        if t not in ours_at:
            diffs.append(f"change at {when(t)} ({show(before)} to {show(after)}) missing from the library")
    return diffs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", help="the dump program built from tests/dump.cpp")
    ap.add_argument("--tier", default="full")
    ap.add_argument("--resources", default="resources")
    ap.add_argument("--zones", nargs="*", help="only these zones")
    a = ap.parse_args()
    if tzdata.__version__ != TZDATA_VERSION:
        raise SystemExit(f"tzdata {tzdata.__version__} is installed; the tables pin {TZDATA_VERSION}")
    zoneinfo.reset_tzpath(to=())    # the tzdata package, never the system's files
    library = ours(a.dump, str(Path(a.resources) / a.tier))
    total, bad, changes = 0, 0, 0
    for cc, name, first, info, zone_changes in library:
        if a.zones and name not in a.zones:
            continue
        total += 1
        changes += len(zone_changes)
        diffs = compare(Theirs(name), first, info, zone_changes)
        if diffs:
            bad += 1
            print(f"{cc} {name}: {len(diffs)} differences")
            for d in diffs[:10]:
                print(f"    {d}")
    print(f"{total} zones, {changes} changes, {bad} with differences (tzdata {tzdata.IANA_VERSION})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
