#!/usr/bin/env python3
"""Write country-generator's locales.tsv and currencies.tsv.

Sources:
  - Unicode CLDR (Unicode License v3), the pinned cldr-json release from npm:
    `cldr-core` (territoryInfo, likelySubtags, defaultContent, weekData,
    measurementData, currencyData), `cldr-dates-full` (Gregorian formats and
    names), `cldr-numbers-full` (Latin-digit symbols, patterns, currency
    symbols) and `cldr-localenames-full` (English language names).
  - ISO 4217 lists one (current) and three (withdrawn), published by SIX.

A country's locales are the languages CLDR's territoryInfo gives it that at
least 1% speak and that are official there in any form or spoken by at least
5%, and that CLDR has locale data for. Each row is complete (cldr-json is
resolved), with the territory's own week data and measurement system. The
tag is the CLDR locale minimised with likelySubtags, keeping the region
(`zh-Hant-TW` is `zh-TW`, `sr-Latn-RS` keeps its script). `basis` is `cldr`
when CLDR has the locale for the territory, `inherited` when only the
language's formats exist, and `fallback` for a country where no language
qualifies (section 4.3 of the design).

The locale's currency is the country's currency_code when it is on ISO list
one; otherwise CLDR's current tender for the territory, or an override below
where CLDR is behind ISO. currencies.tsv holds list one, and the withdrawn
codes of list three that countries.tsv still uses.

Usage:
    python scripts/prepare_locales.py              # both tiers
    python scripts/prepare_locales.py --tier lite

Run it from the repository root. Downloads are cached in build/cache.
"""

import argparse
import hashlib
import io
import json
import re
import sys
import tarfile
import urllib.request
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_language_shares import iso_names, language_name  # noqa: E402  (the same names as language_shares)

CLDR_VERSION = "47.0.0"            # the CLDR of Babel 2.18, which validate_formats.py compares with
_NPM = "https://registry.npmjs.org/{0}/" + CLDR_VERSION
_SIX = "https://www.six-group.com/dam/download/financial-information/data-center/iso-currrency/lists/list-{0}.xml"
_PACKAGES = ("cldr-core", "cldr-dates-full", "cldr-numbers-full", "cldr-localenames-full")

MIN_SHARE, UNOFFICIAL_SHARE = 1.0, 5.0

# Where CLDR 47's current tender is behind ISO 4217 list one.
CURRENCY_OVERRIDES = {"BG": "EUR"}    # euro from 2026-01-01; CLDR 47 still has BGN

WEEKDAYS = ("sun", "mon", "tue", "wed", "thu", "fri", "sat")
DATE_FIELDS = re.compile(r"^(G{1,3}|y{1,2}|yyyy|M{1,4}|L{1,4}|d{1,2}|E{1,4}|c{1,4}|a|h{1,2}|H{1,2}|m{1,2}|s{1,2})$")
NUMBER = re.compile(r"[#0,]*0(\.0*#*)?")
REWRITES = {}                      # CLDR locale -> B fields rewritten to a


# ---------------------------------------------------------------------------
# Downloads
# ---------------------------------------------------------------------------

def download(url, cache, name):
    path = Path(cache) / name
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (country-generator data script)"})
        with urllib.request.urlopen(request, timeout=300) as response:
            path.write_bytes(response.read())
    return path.read_bytes()


class Cldr:
    """The pinned cldr-json packages, read from their npm tarballs."""

    def __init__(self, cache):
        self.files = {}
        for package in _PACKAGES:
            meta = json.loads(download(_NPM.format(package), cache, f"{package}-{CLDR_VERSION}.json"))
            data = download(meta["dist"]["tarball"], cache, f"{package}-{CLDR_VERSION}.tgz")
            if hashlib.sha1(data).hexdigest() != meta["dist"]["shasum"]:
                raise SystemExit(f"{package}: shasum does not match npm")
            with tarfile.open(fileobj=io.BytesIO(data)) as tar:
                for member in tar.getmembers():
                    if member.isfile() and member.name.endswith(".json"):
                        self.files[member.name.removeprefix("package/") + "@" + package] = tar.extractfile(member).read()

    def json(self, package, path):
        return json.loads(self.files[f"{path}@{package}"])

    def has(self, package, path):
        return f"{path}@{package}" in self.files

    def supplemental(self, name):
        return self.json("cldr-core", f"supplemental/{name}.json")["supplemental"]


def iso_list(cache, which):
    root = ET.fromstring(download(_SIX.format(which), cache, f"iso4217-list-{which}.xml"))
    return root.attrib.get("Pblshd", "?"), root.iter("CcyNtry" if which == "one" else "HstrcCcyNtry")


# ---------------------------------------------------------------------------
# Locale ids
# ---------------------------------------------------------------------------

def split_id(locale_id):
    parts = locale_id.replace("_", "-").split("-")
    lang = parts[0]
    script = next((p for p in parts[1:] if len(p) == 4), "")
    region = next((p for p in parts[1:] if len(p) in (2, 3) and (p.isupper() or p.isdigit())), "")
    return lang, script, region


class Ids:
    def __init__(self, cldr):
        self.available = set(cldr.json("cldr-core", "availableLocales.json")["availableLocales"]["full"])
        self.default_content = set(cldr.json("cldr-core", "defaultContent.json")["defaultContent"])
        self.likely = cldr.supplemental("likelySubtags")["likelySubtags"]

    def likely_script(self, lang, region):
        for key in (f"{lang}-{region}", lang, f"und-{region}"):
            if key in self.likely:
                return split_id(self.likely[key])[1]
        return ""

    def folder(self, locale_id):
        """The available locale holding locale_id's data, following default content."""
        while locale_id not in self.available and locale_id in self.default_content:
            locale_id = locale_id.rsplit("-", 1)[0]
        return locale_id if locale_id in self.available else None

    def resolve(self, code, region):
        """(folder, basis, script) for a territoryInfo language code in a region, or None."""
        lang, script, _ = split_id(code)
        script = script or self.likely_script(lang, region)
        for candidate in (f"{lang}-{script}-{region}", f"{lang}-{region}"):
            if (folder := self.folder(candidate)) is not None:
                return folder, "cldr", script
        for candidate in (f"{lang}-{script}", lang):
            folder = self.folder(candidate)
            if folder is not None and (split_id(folder)[1] or self.likely_script(lang, "")) == script:
                return folder, "inherited", script
        return None

    def tag(self, lang, script, region):
        return f"{lang}-{region}" if self.likely_script(lang, region) == script else f"{lang}-{script}-{region}"


# ---------------------------------------------------------------------------
# Formats
# ---------------------------------------------------------------------------

def check_date_pattern(pattern, where):
    """Rewrite B to a; any other field outside section 5.1 is an error."""
    out, i = [], 0
    while i < len(pattern):
        ch = pattern[i]
        if ch == "'":
            j = pattern.find("'", i + 1)
            if j < 0:
                raise SystemExit(f"{where}: unterminated quote in {pattern!r}")
            out.append(pattern[i:j + 1])
            i = j + 1
        elif ch.isascii() and ch.isalpha():
            j = i
            while j < len(pattern) and pattern[j] == ch:
                j += 1
            run = pattern[i:j]
            if ch == "B":
                run = "a"
                REWRITES[where] = REWRITES.get(where, 0) + 1
            if not DATE_FIELDS.match(run):
                raise SystemExit(f"{where}: unsupported field {run!r} in {pattern!r}")
            out.append(run)
            i = j
        else:
            out.append(ch)
            i += 1
    return "".join(out)



def check_number_pattern(pattern, where):
    """positive[;negative], each literals, one number, literals; no quotes or other specials."""
    parts = pattern.split(";")
    if len(parts) > 2:
        raise SystemExit(f"{where}: more than two subpatterns in {pattern!r}")
    for part in parts:
        numbers = list(NUMBER.finditer(part))
        if len(numbers) != 1 or any(c in part[:numbers[0].start()] + part[numbers[0].end():] for c in "#0,.'%‰E@*"):
            raise SystemExit(f"{where}: unsupported number pattern {pattern!r}")
    return pattern


def clean(text, where):
    if any(c in text for c in "\t\n;"):
        raise SystemExit(f"{where}: {text!r} holds a tab, newline or ';'")
    return text


def own_names(cldr, folder):
    """Whether a CLDR locale has month names of its own, not the root's M01 to M12.

    cldr-json gives a locale with no confirmed Gregorian data the root's
    values (M01, Sun, y-MM-dd); such a locale is treated as having no CLDR
    data (decision 36 of the design).
    """
    g = cldr.json("cldr-dates-full", f"main/{folder}/ca-gregorian.json")["main"][folder]["dates"]["calendars"]["gregorian"]
    return [g["months"]["format"]["wide"][str(m)] for m in range(1, 13)] != [f"M{m:02}" for m in range(1, 13)]


def formats(cldr, folder, currency):
    where = f"CLDR {folder}"
    g = cldr.json("cldr-dates-full", f"main/{folder}/ca-gregorian.json")["main"][folder]["dates"]["calendars"]["gregorian"]
    n = cldr.json("cldr-numbers-full", f"main/{folder}/numbers.json")["main"][folder]["numbers"]
    cur = cldr.json("cldr-numbers-full", f"main/{folder}/currencies.json")["main"][folder]["numbers"]["currencies"]
    symbols = n["symbols-numberSystem-latn"]
    money = n["currencyFormats-numberSystem-latn"]
    spacing = money["currencySpacing"]
    if spacing["beforeCurrency"]["insertBetween"] != spacing["afterCurrency"]["insertBetween"]:
        raise SystemExit(f"{where}: currency spacing differs before and after the symbol")
    months, days = g["months"]["format"], g["days"]["format"]
    names = lambda d, keys: ";".join(clean(d[k], where) for k in keys)
    row = {
        **{f"date_{s}": check_date_pattern(g["dateFormats"][s], where) for s in ("short", "medium", "long", "full")},
        **{f"time_{s}": check_date_pattern(g["timeFormats"][s], where) for s in ("short", "medium")},
        "months": names(months["wide"], [str(m) for m in range(1, 13)]),
        "months_abbr": names(months["abbreviated"], [str(m) for m in range(1, 13)]),
        "weekdays": names(days["wide"], WEEKDAYS),
        "weekdays_abbr": names(days["abbreviated"], WEEKDAYS),
        "am": clean(g["dayPeriods"]["format"]["abbreviated"]["am"], where),
        "pm": clean(g["dayPeriods"]["format"]["abbreviated"]["pm"], where),
        "eras": names(g["eras"]["eraAbbr"], ("0", "1")),
        "decimal": clean(symbols["decimal"], where),
        "group": clean(symbols["group"], where),
        "minus": clean(symbols["minusSign"], where),
        "number_pattern": check_number_pattern(n["decimalFormats-numberSystem-latn"]["standard"], where),
        "currency_pattern": check_number_pattern(money["standard"], where),
        "currency_spacing": spacing["beforeCurrency"]["insertBetween"],
        "currency_code": currency,
        "currency_symbol": clean(cur.get(currency, {}).get("symbol", currency), where),
    }
    if "¤" not in row["currency_pattern"]:
        raise SystemExit(f"{where}: currency pattern without ¤")
    return row


# ---------------------------------------------------------------------------
# Countries
# ---------------------------------------------------------------------------

def read_countries(path):
    lines = [l for l in Path(path).read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    header = lines[0].split("\t")
    return {r["cca2"]: r for r in (dict(zip(header, l.split("\t"))) for l in lines[1:])}


def current_tender(region_data, cc):
    out = []
    for entry in region_data.get(cc, []):
        for code, d in entry.items():
            if "_to" not in d and d.get("_tender", "true") != "false":
                out.append(code)
    return out


def country_currency(cc, country, region_data, iso_current, report):
    code = country["currency_code"]
    if code in iso_current:
        return code
    if cc in CURRENCY_OVERRIDES:
        chosen = CURRENCY_OVERRIDES[cc]
    else:
        chosen = next((c for c in current_tender(region_data, cc) if c in iso_current), None)
    if chosen is None:
        return None
    report.append(f"{cc}: currency_code {code or '(none)'} is not on ISO list one; the locale uses {chosen}")
    return chosen


def measurement(data, cc):
    system = data["measurementSystem"].get(cc, data["measurementSystem"]["001"])
    return {"metric": "metric", "US": "us", "UK": "uk"}[system]


def week(data, cc):
    get = lambda key: data[key].get(cc, data[key]["001"])
    return {"first_day": get("firstDay"), "weekend_start": get("weekendStart"), "weekend_end": get("weekendEnd")}


def build(cldr, ids, countries, iso_current, report):
    territories = cldr.supplemental("territoryInfo")["territoryInfo"]
    names = cldr.json("cldr-localenames-full", "main/en/languages.json")["main"]["en"]["localeDisplayNames"]["languages"]
    week_data, measure = cldr.supplemental("weekData")["weekData"], cldr.supplemental("measurementData")["measurementData"]
    region_data = cldr.supplemental("currencyData")["currencyData"]["region"]
    english = {v: k for k, v in names.items() if "-" not in k and "_" not in k}
    fallback_names = {}
    rows = []
    for cc in sorted(countries):
        country = countries[cc]
        currency = country_currency(cc, country, region_data, iso_current, report)
        candidates = []
        for code, data in territories.get(cc, {}).get("languagePopulation", {}).items():
            share = float(data["_populationPercent"])
            official = "_officialStatus" in data
            if code == "und" or share < MIN_SHARE or not (official or share >= UNOFFICIAL_SHARE):
                continue
            resolved = ids.resolve(code, cc)
            if resolved is None:
                report.append(f"{cc}: {code} ({share:g}%) qualifies but CLDR has no locale data for it")
                continue
            if not own_names(cldr, resolved[0]):
                report.append(f"{cc}: {code} ({share:g}%) qualifies but CLDR {resolved[0]} has only the root's names")
                continue
            candidates.append((code, share, *resolved))
        if not candidates:
            # Fallback: the primary language of countries.tsv, with the formats of the
            # most spoken language CLDR has names for, else en-001.
            primary = country["languages"].split(";")[0]
            lang = english.get(primary)
            spoken = sorted(((float(d["_populationPercent"]), c) for c, d in
                             territories.get(cc, {}).get("languagePopulation", {}).items()), reverse=True)
            source = next((r[0] for _, c in spoken if c != "und" and (r := ids.resolve(c, cc)) and own_names(cldr, r[0])),
                          "en-001")
            if lang is None or currency is None:
                report.append(f"{cc}: no locale (primary language {primary or '(none)'}, currency {currency or '(none)'})")
                continue
            script = ids.likely_script(lang, cc)
            share = next((p for p, c in spoken if split_id(c)[0] == lang), 100.0) or 100.0
            report.append(f"{cc}: fallback {lang}-{cc} with the formats of {source}")
            candidates = [(lang, share, source, "fallback", script)]
        if currency is None:
            raise SystemExit(f"{cc}: no ISO 4217 currency for the locale")
        seen = {}
        for code, share, folder, basis, script in sorted(candidates, key=lambda c: (-c[1], c[0])):
            lang = split_id(code)[0]
            tag = ids.tag(lang, script, cc)
            if tag in seen:
                report.append(f"{cc}: {code} gives {tag} again; kept the larger share")
                continue
            name = language_name(names, fallback_names, code)
            if name is None:
                fallback_names.update(iso_names())
                name = language_name(names, fallback_names, code)
            seen[tag] = True
            rows.append({
                "country": cc, "tag": tag, "language": lang,
                "script": "" if tag.count("-") == 1 else script,
                "language_name": name, "share": f"{share:g}", "basis": basis,
                **formats(cldr, folder, currency),
                **week(week_data, cc), "measurement": measurement(measure, cc),
            })
            shares = dict(item.split(":") for item in country["language_shares"].split(";") if item)
            if basis != "fallback" and name not in shares:
                report.append(f"{cc}: {tag}'s language_name {name!r} is not in countries.tsv's language_shares")
    return rows


COLUMNS = ["country", "tag", "language", "script", "language_name", "share", "basis",
           "date_short", "date_medium", "date_long", "date_full", "time_short", "time_medium",
           "months", "months_abbr", "weekdays", "weekdays_abbr", "am", "pm", "eras",
           "decimal", "group", "minus", "number_pattern", "currency_pattern", "currency_spacing",
           "currency_code", "currency_symbol", "first_day", "weekend_start", "weekend_end", "measurement"]


def currencies(cache, cldr, used):
    """ISO 4217 list one, plus the withdrawn codes in `used`; cash digits from CLDR."""
    published, entries = iso_list(cache, "one")
    rows = {}
    for e in entries:
        code, numeric, digits = (e.findtext(t) for t in ("Ccy", "CcyNbr", "CcyMnrUnts"))
        if code and digits and digits.isdigit():
            rows[code] = [code, numeric, digits, ""]
    current = set(rows)
    published_three, entries = iso_list(cache, "three")
    withdrawn = {}
    for e in entries:
        code, numeric = e.findtext("Ccy"), e.findtext("CcyNbr")
        if code in used and code not in current and code not in withdrawn:
            withdrawn[code] = [code, numeric, None, ""]
    fractions = cldr.supplemental("currencyData")["currencyData"]["fractions"]
    for code, row in withdrawn.items():
        # List three has no minor units; CLDR's (its default is 2).
        row[2] = fractions.get(code, fractions["DEFAULT"])["_digits"]
        rows[code] = row
    for code, row in rows.items():
        cash = fractions.get(code, {}).get("_cashDigits")
        if cash is not None and cash != row[2]:
            row[3] = cash
    return [rows[c] for c in sorted(rows)], current, published, published_three, sorted(withdrawn)


def write(path, comment, columns, rows):
    out = [f"# {line}" for line in comment] + ["\t".join(columns)]
    out += ["\t".join(row) for row in rows]
    Path(path).write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    return Path(path).stat().st_size


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tier", choices=["lite", "full", "both"], default="both")
    ap.add_argument("--resources", default="resources")
    ap.add_argument("--cache", default="build/cache")
    a = ap.parse_args()

    cldr = Cldr(a.cache)
    ids = Ids(cldr)
    used = {r["currency_code"] for r in read_countries(Path(a.resources) / "full" / "countries.tsv").values()}
    currency_rows, iso_current, one, three, withdrawn = currencies(a.cache, cldr, used)
    for tier in (["lite", "full"] if a.tier == "both" else [a.tier]):
        report = []
        REWRITES.clear()
        countries = read_countries(Path(a.resources) / tier / "countries.tsv")
        rows = build(cldr, ids, countries, iso_current, report)
        comment = [f"source: CLDR {CLDR_VERSION} (cldr-json, Unicode License v3)",
                   "written by scripts/prepare_locales.py"]
        size = write(Path(a.resources) / tier / "locales.tsv", comment, COLUMNS,
                     [[r[c] for c in COLUMNS] for r in rows])
        cur_size = write(Path(a.resources) / tier / "currencies.tsv",
                         [f"source: ISO 4217 list one of {one}, withdrawn codes from list three of {three}, "
                          f"cash digits from CLDR {CLDR_VERSION}", "written by scripts/prepare_locales.py"],
                         ["code", "numeric", "digits", "cash_digits"], currency_rows)
        bases = defaultdict(int)
        for r in rows:
            bases[r["basis"]] += 1
        covered = {r["country"] for r in rows}
        print(f"{tier}: {len(rows)} locales in {len(covered)} of {len(countries)} countries "
              f"({', '.join(f'{k} {v}' for k, v in sorted(bases.items()))}), locales.tsv {size / 1024:.0f} KB; "
              f"{len(currency_rows)} currencies ({', '.join(withdrawn)} withdrawn), {cur_size / 1024:.0f} KB; "
              f"B rewritten to a in {sum(REWRITES.values())} patterns of {', '.join(sorted(REWRITES)) or 'no locale'}")
        for line in report:
            print(f"  {line}")


if __name__ == "__main__":
    main()
