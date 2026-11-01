# SPDX-License-Identifier: MIT
# Copyright (c) 2020-2026 Diego Dasso Migotto

"""Compare the library's formatting with Babel's, every locale of a tier.

For each row of the tier's locales.tsv:

1. The formatter. The same cases (by default 1,000 dates, times, numbers and
   amounts in the locale's currency, seeded) go through the dump program
   (tests/dump.cpp) and through Babel, given the row's own patterns, names and
   symbols, and the texts must be identical (design 13.2). A pattern field the
   library does not write as Babel does shows here.
2. The data, as a report. Babel's locale for the row is the first of the
   row's CLDR candidates (language-script-region, language-region,
   language-script, language, language-001, en-001) whose names, symbols and
   patterns are the row's, or else the one that differs in the fewest fields;
   the fields that still differ are counted (with --data, listed). Both come
   from CLDR 47, but Babel imports CLDR's XML with every draft level, where
   cldr-json, which the tables are built from, keeps only contributed and
   approved data, as ICU does; so the seed locales of South Africa have
   CLDR's root names (M01, Sun) in the tables and their own in Babel. The
   report does not fail the run.

Babel is given the row's pattern with three documented normalisations, each a
place where Babel does not do what CLDR (UTS #35) and the library do:

- currency spacing (decision 30): Babel inserts no currencySpacing text, so
  it is written into the pattern next to the symbol where the rule applies
  (the symbol touches the number, and its character on that side is neither a
  symbol nor a separator);
- the minus sign: Babel writes the pattern's `-` (and the one it puts before
  a pattern with no negative subpattern) as it is, where UTS #35 replaces it
  with the locale's minus sign (Arabic's is U+200E U+002D, Swedish's U+2212),
  so the negative subpattern is written out with the sign as a literal;
- the amount's digits: the row's currency digits (ISO 4217), where Babel
  would use CLDR's.

Stand-alone month and weekday names are the format names, as the library has
no others (design 5.1). Numbers that round to zero are not generated: the
library writes `0` for them where Babel writes `-0`.

Usage:
    clang++ -std=c++23 -O2 -I. -o build/dump tests/dump.cpp
    python scripts/validate_formats.py build/dump --tier full [--tags pt-BR ar-SA] [--cases 1000] [--data]

Run it from the repository root with Babel at the pinned version
(pip install babel==2.18.0), whose CLDR is the tables'. Exits non-zero when
the formatter differs.
"""

import argparse
import datetime
import decimal
import random
import subprocess
import sys
import tempfile
import unicodedata
from collections import Counter
from pathlib import Path

import babel
from babel import Locale, UnknownLocaleError, dates, localedata, numbers
from babel.core import get_global

BABEL_VERSION = "2.18.0"    # its CLDR is 47, the tables' (scripts/prepare_locales.py)
WEEKDAYS = (6, 0, 1, 2, 3, 4, 5)    # Babel's weekday numbers, Sunday first as the table has them


# ---------------------------------------------------------------------------
# Cases
# ---------------------------------------------------------------------------

def make_cases(n, seed):
    """[(kind, fields)]: n dates, n times, n numbers and n amounts in minor units."""
    rng = random.Random(seed)
    cases = []
    fixed_dates = [(2024, 12, 31), (2005, 1, 9), (2000, 2, 29), (1999, 1, 1), (999, 6, 15), (1, 1, 1)]
    for i in range(n):
        if i < len(fixed_dates):
            y, m, d = fixed_dates[i]
        else:
            y, m = rng.randint(1000, 2999), rng.randint(1, 12)
            d = rng.randint(1, (datetime.date(y + m // 12, m % 12 + 1, 1) - datetime.timedelta(days=1)).day)
        cases.append(("date", (f"{y:04}-{m:02}-{d:02}", str(i % 4))))
    fixed_times = [(0, 0, 0), (12, 0, 0), (23, 59, 59), (0, 7, 0), (11, 59, 59), (13, 4, 5)]
    for i in range(n):
        h, mi, s = fixed_times[i] if i < len(fixed_times) else (rng.randint(0, 23), rng.randint(0, 59), rng.randint(0, 59))
        cases.append(("time", (f"{h:02}:{mi:02}:{s:02}", str(i % 2))))
    fixed_numbers = ["2.675", "2.665", "0.0015", "-1234567.891", "1000", "999.9995", "12345678.5", "0.001"]
    for i in range(n):
        if i < len(fixed_numbers):
            text = fixed_numbers[i]
        else:
            while True:
                digits = rng.randint(1, 15)
                scale = rng.randint(-6, 15 - digits) if digits < 15 else 0
                value = decimal.Decimal(rng.randint(1, 10 ** digits - 1)).scaleb(scale)
                if rng.random() < 0.3:
                    value = value.quantize(decimal.Decimal("0.001")) + decimal.Decimal("0.0005")   # a tie
                if rng.random() < 0.5:
                    value = -value
                text = repr(float(value))
                if abs(decimal.Decimal(text)) >= decimal.Decimal("0.001") and abs(float(text)) < 1e15:
                    break
        cases.append(("number", (repr(float(text)),)))
    fixed_minor = [0, 1, -1, 99, -100, 123450, -123450]
    for i in range(n):
        minor = fixed_minor[i] if i < len(fixed_minor) else rng.choice((1, -1)) * rng.randint(0, 10 ** rng.randint(1, 13))
        cases.append(("money", (str(minor),)))
    return cases


# ---------------------------------------------------------------------------
# The library
# ---------------------------------------------------------------------------

def ours(dump, tier_dir, cases):
    """{tag: [text per case]}."""
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "cases.tsv"
        path.write_text("".join("\t".join((kind, *fields)) + "\n" for kind, fields in cases), encoding="utf-8")
        text = subprocess.run([str(Path(dump).resolve()), "formats", tier_dir, str(path)], check=True,
                              capture_output=True, encoding="utf-8").stdout
    out, tag = {}, None
    for line in text.split("\n")[:-1]:
        line = line.removesuffix("\r")
        if line.startswith("L\t"):
            tag = line[2:]
            out[tag] = []
        else:
            out[tag].append(line)
    return out


def read_table(path):
    lines = [l for l in Path(path).read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    header = lines[0].split("\t")
    return [dict(zip(header, l.split("\t"))) for l in lines[1:]]


# ---------------------------------------------------------------------------
# Babel's data (the report)
# ---------------------------------------------------------------------------

def rewrite_b(pattern):
    """The library's rewrite of CLDR's flexible day period B to a (design 5.1)."""
    out, quoted, previous = [], False, ""
    for ch in pattern:
        if ch == "'":
            quoted = not quoted
        if ch == "B" and not quoted:
            if previous != "B":
                out.append("a")
        else:
            out.append(ch)
        previous = ch if not quoted else ""
    return "".join(out)


def babel_fields(loc, currency):
    """The table's columns as Babel's locale has them."""
    latn = loc.number_symbols["latn"]
    months, days = loc.months["format"], loc.days["format"]
    return {
        **{f"date_{s}": rewrite_b(loc.date_formats[s].pattern) for s in ("short", "medium", "long", "full")},
        **{f"time_{s}": rewrite_b(loc.time_formats[s].pattern) for s in ("short", "medium")},
        "months": ";".join(months["wide"][m] for m in range(1, 13)),
        "months_abbr": ";".join(months["abbreviated"][m] for m in range(1, 13)),
        "weekdays": ";".join(days["wide"][d] for d in WEEKDAYS),
        "weekdays_abbr": ";".join(days["abbreviated"][d] for d in WEEKDAYS),
        "am": loc.day_periods["format"]["abbreviated"]["am"],
        "pm": loc.day_periods["format"]["abbreviated"]["pm"],
        "eras": ";".join(loc.eras["abbreviated"][e] for e in (0, 1)),
        "decimal": latn["decimal"],
        "group": latn["group"],
        "minus": latn["minusSign"],
        "number_pattern": loc.decimal_formats[None].pattern,
        "currency_pattern": loc.currency_formats["standard"].pattern,
        "currency_symbol": loc.currency_symbols.get(currency, currency),
    }


_LOCALES = {}


def babel_locale(identifier):
    if identifier not in _LOCALES:
        try:
            _LOCALES[identifier] = Locale.parse(identifier) if localedata.exists(identifier) else None
        except (UnknownLocaleError, ValueError):
            _LOCALES[identifier] = None
    return _LOCALES[identifier]


def candidates(row):
    parts = row["tag"].split("-")
    lang, region = parts[0], parts[-1]
    likely = get_global("likely_subtags")
    full = likely.get(f"{lang}_{region}") or likely.get(lang) or ""
    script = row["script"] or next((p for p in full.split("_")[1:] if len(p) == 4), "")
    ids = [f"{lang}_{script}_{region}" if script else "", f"{lang}_{region}", f"{lang}_{script}" if script else "",
           lang, f"{lang}_001", "en_001"]
    return [i for i in dict.fromkeys(ids) if i]


def match(row):
    """(Babel locale, [fields that differ], Babel's fields) for a row."""
    best = None
    for identifier in candidates(row):
        loc = babel_locale(identifier)
        if loc is None:
            continue
        theirs = babel_fields(loc, row["currency_code"])
        differ = [k for k, v in theirs.items() if row[k] != v]
        if best is None or len(differ) < len(best[1]):
            best = (loc, differ, theirs)
        if not differ:
            break
    return best


# ---------------------------------------------------------------------------
# Babel's text (the check)
# ---------------------------------------------------------------------------

class RowLocale(Locale):
    """A Babel locale with the row's names and symbols, so that only the formatters are compared.

    Only the widths the library has are given (wide and abbreviated names,
    abbreviated eras, am and pm): a pattern asking for another is an error.
    """

    def __init__(self, base, row):
        super().__init__(base.language, territory=base.territory, script=base.script, variant=base.variant)
        names = lambda column: row[column].split(";")
        month = lambda column: {m + 1: n for m, n in enumerate(names(column))}
        day = lambda column: {WEEKDAYS[i]: n for i, n in enumerate(names(column))}
        months = {"wide": month("months"), "abbreviated": month("months_abbr")}
        days = {"wide": day("weekdays"), "abbreviated": day("weekdays_abbr")}
        self._row_months = {"format": months, "stand-alone": months}
        self._row_days = {"format": days, "stand-alone": days}
        self._row_eras = {"abbreviated": dict(enumerate(names("eras")))}
        periods = {"abbreviated": {"am": row["am"], "pm": row["pm"]}}
        self._row_periods = {"format": periods, "stand-alone": periods}
        symbols = dict(base.number_symbols["latn"])
        symbols.update(decimal=row["decimal"], group=row["group"], minusSign=row["minus"])
        self._row_symbols = {"latn": symbols}

    months = property(lambda self: self._row_months)
    days = property(lambda self: self._row_days)
    eras = property(lambda self: self._row_eras)
    day_periods = property(lambda self: self._row_periods)
    number_symbols = property(lambda self: self._row_symbols)


def quote(text):
    return "'" + text.replace("'", "''") + "'"


def split_number(sub):
    """(prefix, number, suffix) of a subpattern: the number is the run from the first to the last # or 0."""
    start = next(i for i, c in enumerate(sub) if c in "#0")
    end = len(sub) - next(i for i, c in enumerate(reversed(sub)) if c in "#0")
    return sub[:start], sub[start:end], sub[end:]


def symbol_or_separator(ch):
    return unicodedata.category(ch)[0] in "SZ"


def babel_pattern(row, pattern, digits=None, symbol=None):
    """The row's pattern for Babel: its minus sign, and for an amount the symbol as a literal,
    the currency spacing and the currency's digits (see the module's description)."""
    subs = pattern.split(";")
    if len(subs) == 1:
        subs.append("-" + subs[0])    # UTS #35's implicit negative subpattern
    out = []
    for n, sub in enumerate(subs):
        prefix, number, suffix = split_number(sub)
        if digits is not None:
            number = number.split(".")[0] + ("." + "0" * digits if digits else "")
        affixes = []
        for side, affix in (("prefix", prefix), ("suffix", suffix)):
            text = ""
            for ch in affix:
                if ch == "¤":
                    text += quote(symbol)
                elif ch == "-" and n == 1:
                    text += quote(row["minus"])
                else:
                    text += quote(ch)
            if symbol is not None:
                touches = affix.endswith("¤") if side == "prefix" else affix.startswith("¤")
                edge = symbol[-1] if side == "prefix" else symbol[0]
                if touches and not symbol_or_separator(edge):
                    spacing = quote(row["currency_spacing"])
                    text = text + spacing if side == "prefix" else spacing + text
            affixes.append(text)
        out.append(affixes[0] + number + affixes[1])
    return ";".join(out)


def theirs(row, loc, kind, fields):
    try:
        if kind == "date":
            y, m, d = (int(x) for x in fields[0].split("-"))
            style = ("short", "medium", "long", "full")[int(fields[1])]
            return dates.format_date(datetime.date(y, m, d), format=row[f"date_{style}"], locale=loc)
        if kind == "time":
            h, mi, s = (int(x) for x in fields[0].split(":"))
            style = ("short", "medium")[int(fields[1])]
            return dates.format_time(datetime.time(h, mi, s), format=row[f"time_{style}"], locale=loc)
        if kind == "number":
            return numbers.format_decimal(decimal.Decimal(fields[0]), format=babel_pattern(row, row["number_pattern"]),
                                          locale=loc, numbering_system="latn")
        digits = int(row["currency_digits"])
        amount = decimal.Decimal(int(fields[0])).scaleb(-digits)
        pattern = babel_pattern(row, row["currency_pattern"], digits, row["currency_symbol"])
        return numbers.format_currency(amount, row["currency_code"], format=pattern, locale=loc, currency_digits=False,
                                       numbering_system="latn")
    except KeyError as e:
        return f"(Babel needs {e}, which the table does not have)"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", help="the dump program built from tests/dump.cpp")
    ap.add_argument("--tier", default="full")
    ap.add_argument("--resources", default="resources")
    ap.add_argument("--tags", nargs="*", help="only these locales")
    ap.add_argument("--cases", type=int, default=1000, help="cases of each kind (default 1000)")
    ap.add_argument("--seed", type=int, default=2026)
    ap.add_argument("--data", action="store_true", help="list each locale's data differences from Babel")
    a = ap.parse_args()
    if babel.__version__ != BABEL_VERSION:
        raise SystemExit(f"Babel {babel.__version__} is installed; the tables pin {BABEL_VERSION}")
    tier_dir = Path(a.resources) / a.tier
    rows = [r for r in read_table(tier_dir / "locales.tsv") if not a.tags or r["tag"] in a.tags]
    digits = {r["code"]: r["digits"] for r in read_table(tier_dir / "currencies.tsv")}
    cases = make_cases(a.cases, a.seed)
    library = ours(a.dump, str(tier_dir), cases)
    bad, fields, placeholders, data_locales = 0, Counter(), 0, 0
    for row in rows:
        row["currency_digits"] = digits[row["currency_code"]]
        loc, differ, babel_row = match(row)
        fields.update(differ)
        data_locales += bool(differ)
        placeholders += row["months"].startswith("M01;")
        if differ and a.data:
            print(f"{row['tag']} ({row['basis']}, Babel {loc}): data differs in {len(differ)} fields")
            for k in differ:
                print(f"    {k}: table {row[k]!r}, Babel {babel_row[k]!r}")
        mine_loc = RowLocale(loc, row)
        diffs = []
        for (kind, fields_), mine in zip(cases, library[row["tag"]]):
            other = theirs(row, mine_loc, kind, fields_)
            if mine != other:
                diffs.append(f"{kind} {' '.join(fields_)}: library {mine!r}, Babel {other!r}")
        if diffs:
            bad += 1
            print(f"{row['tag']} ({row['basis']}): {len(diffs)} formatting differences")
            for d in diffs[:10]:
                print(f"    {d}")
    print(f"data report: {data_locales} locales differ from Babel's CLDR import; by field: "
          + (", ".join(f"{k} {n}" for k, n in fields.most_common()) or "none")
          + f"; {placeholders} locales have CLDR's root month names (M01)")
    print(f"{len(rows)} locales, {4 * a.cases} cases each, {bad} with formatting differences")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
