# SPDX-License-Identifier: MIT
# Copyright (c) 2020-2026 Diego Dasso Migotto

"""Compare the library's holidays with python-holidays, every country and year.

The library's answers come from the dump program (tests/dump.cpp), which loads
a tier and prints every country's holidays for every year of its span. For each
country, kind and day, the count of holidays on that date and the count of days
observed on it must equal python-holidays', read the way prepare_holidays.py
reads it (design 11.3 and 13.2). A day the library lists as its own entry (a
move no rule gives, design 13.2) counts as an observed day when python-holidays
has a day of that kind observed on that date, and the entry has that day's
English name or a key prepare_holidays.py gives moved days (..._observed).

Usage:
    clang++ -std=c++23 -O2 -I. -o build/dump tests/dump.cpp
    python scripts/validate_holidays.py build/dump --tier full [--countries US GB]

Run it from the repository root with python-holidays at the pinned version
(pip install holidays==0.107). Exits non-zero when anything differs.
"""

import argparse
import datetime
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_holidays as ph  # noqa: E402  (the same reading of python-holidays)


def ours(dump, tier_dir):
    """{country: [(date, observed or None, kind, key, name_en)]}."""
    text = subprocess.run([str(Path(dump).resolve()), "holidays", tier_dir], check=True, capture_output=True,
                          text=True, encoding="utf-8").stdout
    out = defaultdict(list)
    for line in text.splitlines():
        cc, date, observed, kind, key, name_en = line.split("\t")
        out[cc].append((datetime.date.fromisoformat(date),
                        datetime.date.fromisoformat(observed) if observed else None, kind, key, name_en))
    return out


# The keys prepare_holidays.py gives moved days: <key>_observed, then _2 or _<kind> on a clash.
MOVED_KEY = re.compile(r"_observed(_2)*(_[a-z]+)?(_2)*$")


def count(entries, moved_names):
    """(dates, observed), each a {kind: Counter(date)}; an entry that is a moved day
    python-holidays has (see above) counts as observed."""
    dates, moved = defaultdict(Counter), defaultdict(Counter)
    for day, observed, kind, key, name_en in entries:
        if observed is None and ((day, kind, name_en) in moved_names or
                                 ((day, kind, None) in moved_names and MOVED_KEY.search(key))):
            moved[kind][day] += 1
        else:
            dates[kind][day] += 1
        if observed:
            moved[kind][observed] += 1
    return dates, moved


def theirs(code):
    """python-holidays' (country, dates, observed, observed names)."""
    c = ph.Country(code)
    dates, moved = defaultdict(Counter), defaultdict(Counter)
    for kind, days in c.real.items():
        dates[kind].update(days)
    names = set()
    for day, kind, base, name in c.observed:
        moved[kind][day] += 1
        names.update({(day, kind, name), (day, kind, f"{base} (observed)"), (day, kind, None)})
    return c, dates, moved, names


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", help="the dump program built from tests/dump.cpp")
    ap.add_argument("--tier", default="full")
    ap.add_argument("--resources", default="resources")
    ap.add_argument("--countries", nargs="*")
    a = ap.parse_args()
    if ph.holidays.__version__ != ph.HOLIDAYS_VERSION:
        raise SystemExit(f"python-holidays {ph.holidays.__version__} is installed; the tables pin {ph.HOLIDAYS_VERSION}")
    tier_dir = str(Path(a.resources) / a.tier)
    library = ours(a.dump, tier_dir)
    spans = {r["country"]: (int(r["from_year"]), int(r["to_year"]))
             for r in ph.read_tsv(Path(tier_dir) / "holiday_years.tsv")}
    total, bad = 0, 0
    for code in a.countries or sorted(spans):
        c, dates, moved, names = theirs(code)
        if (c.first, c.last) != spans[code]:
            print(f"{code}: span {spans[code]}, python-holidays {(c.first, c.last)}")
            bad += 1
        first, last = datetime.date(c.first, 1, 1), datetime.date(c.last, 12, 31)
        our_dates, our_moved = count(library.get(code, []), names)
        diffs = []
        for what, mine, other in (("date", our_dates, dates), ("observed", our_moved, moved)):
            for kind in set(mine) | set(other):
                x = Counter({d: n for d, n in mine[kind].items() if first <= d <= last})
                y = other[kind]
                diffs += [(d, kind, what, x[d], y[d]) for d in set(x) | set(y) if x[d] != y[d]]
        total += 1
        if diffs:
            bad += 1
            print(f"{code}: {len(diffs)} differences")
            for d, kind, what, x, y in sorted(diffs)[:10]:
                print(f"    {kind} {what} {d}: library {x}, python-holidays {y}")
    print(f"{total} countries, {bad} with differences")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
