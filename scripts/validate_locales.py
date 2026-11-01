# SPDX-License-Identifier: MIT
# Copyright (c) 2020-2026 Diego Dasso Migotto

"""Report how a tier's locales differ from user-generator's hand-written table.

user-generator's resources/lite/locales.tsv gives tags by country with a
speaker weight; it is to be replaced by get_locale (design 15). For each
country of either table this lists the tags only one has and, for the tags
both have, each table's share of the country's draws (weights normalised
within the country). A country that user-generator does not list draws
<first language>-<country> there; those are counted, not listed.

A report to review before user-generator switches (design 13.2): it always
exits 0.

Usage:
    python scripts/validate_locales.py --tier lite [--user ../user-generator/resources/lite/locales.tsv]
"""

import argparse
import sys
from collections import defaultdict
from pathlib import Path


def read_table(path):
    lines = [l for l in Path(path).read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    header = lines[0].split("\t")
    return [dict(zip(header, l.split("\t"))) for l in lines[1:]]


def by_country(rows, tag, weight):
    """{country: {tag: share of the country's draws}}."""
    out = defaultdict(dict)
    for r in rows:
        out[r["country"].upper()][r[tag]] = float(r[weight])
    for tags in out.values():
        total = sum(tags.values())
        for t in tags:
            tags[t] /= total
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tier", default="lite")
    ap.add_argument("--resources", default="resources")
    ap.add_argument("--user", default="../user-generator/resources/lite/locales.tsv",
                    help="user-generator's locales.tsv")
    ap.add_argument("--threshold", type=float, default=5.0,
                    help="list a shared tag whose shares differ by more than this many points (default 5)")
    a = ap.parse_args()
    ours = by_country(read_table(Path(a.resources) / a.tier / "locales.tsv"), "tag", "share")
    theirs = by_country(read_table(a.user), "locale", "weight")
    same = moved = 0
    only_ours_countries = sorted(set(ours) - set(theirs))
    for cc in sorted(set(ours) | set(theirs)):
        if cc not in theirs:
            continue
        mine, other = ours.get(cc, {}), theirs[cc]
        lines = []
        if not mine:
            lines.append("no locale in the tier")
        for t in sorted(set(mine) - set(other), key=lambda t: -mine[t]):
            lines.append(f"only here: {t} ({100 * mine[t]:.0f}%)")
        for t in sorted(set(other) - set(mine), key=lambda t: -other[t]):
            lines.append(f"only in user-generator: {t} ({100 * other[t]:.0f}%)")
        for t in sorted(set(mine) & set(other), key=lambda t: -mine[t]):
            if abs(mine[t] - other[t]) * 100 > a.threshold:
                lines.append(f"{t}: {100 * mine[t]:.0f}% here, {100 * other[t]:.0f}% in user-generator")
        if lines:
            moved += 1
            print(f"{cc}:")
            for line in lines:
                print(f"    {line}")
        else:
            same += 1
    print(f"{len(theirs)} countries in user-generator's table: {same} the same (shares within {a.threshold:g} points), "
          f"{moved} different; {len(only_ours_countries)} countries only here, where user-generator draws "
          f"<first language>-<country>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
