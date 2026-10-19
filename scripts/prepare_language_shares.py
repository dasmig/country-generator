#!/usr/bin/env python3
"""Add the `language_shares` column to country-generator's countries.tsv.

Sources: Unicode CLDR (Unicode License v3), supplemental `territoryInfo`: for
each territory, the percentage of its population that speaks each language.
Language names are CLDR's English display names; a language CLDR has no name
for (Levantine Arabic, Hassaniya, and many smaller ones) takes the reference
name of the SIL ISO 639-3 code table.

The column holds `Name:percent` pairs separated by semicolons, largest
first, for the languages spoken by at least 1% of the population, for
example `Portuguese:91;English:8`. The shares overlap (a country where many people speak two
languages sums above 100) because each is the share of people who speak
that language. A language written in more than one script (Serbian in
Cyrillic and Latin) is listed once, with the larger share.

Usage:
    python scripts/prepare_language_shares.py            # both tiers
    python scripts/prepare_language_shares.py --tier lite
    python scripts/prepare_language_shares.py --min 1.0  # smallest share kept

Run it from the repository root. It rewrites resources/<tier>/countries.tsv in
place and is safe to run again.
"""

import argparse
import json
import urllib.request
from pathlib import Path

_BASE = "https://raw.githubusercontent.com/unicode-org/cldr-json/main/cldr-json"
_TERRITORIES = _BASE + "/cldr-core/supplemental/territoryInfo.json"
_LANGUAGES = _BASE + "/cldr-localenames-full/main/en/languages.json"
_ISO_639_3 = "https://iso639-3.sil.org/sites/iso639-3/files/downloads/iso-639-3.tab"

COLUMN = "language_shares"


def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as response:
        return json.load(response)


def iso_names():
    """ISO 639-3 code -> reference name."""
    request = urllib.request.Request(_ISO_639_3, headers={"User-Agent": "Mozilla/5.0 (country-generator data script)"})
    with urllib.request.urlopen(request, timeout=60) as response:
        rows = response.read().decode("utf-8").splitlines()
    header = rows[0].split("	")
    code, name = header.index("Id"), header.index("Ref_Name")
    return {cells[code]: cells[name] for cells in (row.split("	") for row in rows[1:]) if len(cells) > name}


def language_name(names, fallback, code):
    """CLDR's English name for a language id, falling back to its base language,
    then to the ISO 639-3 reference name."""
    for key in (code, code.replace("-", "_"), code.split("_")[0], code.split("-")[0]):
        if key in names:
            return names[key]
    return fallback.get(code.split("_")[0].split("-")[0])


def shares_by_country(minimum):
    territories = fetch(_TERRITORIES)["supplemental"]["territoryInfo"]
    names = fetch(_LANGUAGES)["main"]["en"]["localeDisplayNames"]["languages"]
    fallback = iso_names()
    result, unnamed = {}, {}
    for cca2, info in territories.items():
        best = {}
        for code, data in info.get("languagePopulation", {}).items():
            percent = float(data["_populationPercent"])
            if percent < minimum:
                continue
            name = language_name(names, fallback, code)
            if name is None:
                unnamed.setdefault(cca2, []).append(f"{code} {percent}")
                continue
            best[name] = max(best.get(name, 0.0), percent)
        ordered = sorted(best.items(), key=lambda item: (-item[1], item[0]))
        result[cca2] = ";".join(f"{name}:{round(percent, 1):g}" for name, percent in ordered)
    return result, unnamed


def rewrite(path, shares):
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    header = lines[0].split("\t")
    column = header.index(COLUMN) if COLUMN in header else None
    if column is None:
        header.append(COLUMN)
        column = len(header) - 1
    out = ["\t".join(header)]
    filled = 0
    for line in lines[1:]:
        cells = line.split("\t")
        cells += [""] * (len(header) - len(cells))
        cells[column] = shares.get(cells[0], "")
        filled += bool(cells[column])
        out.append("\t".join(cells))
    Path(path).write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    return len(lines) - 1, filled


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tier", choices=["lite", "full", "both"], default="both")
    ap.add_argument("--min", type=float, default=1.0, help="smallest share kept, in percent")
    ap.add_argument("--resources", default="resources")
    a = ap.parse_args()
    shares, unnamed = shares_by_country(a.min)
    for tier in (["lite", "full"] if a.tier == "both" else [a.tier]):
        rows, filled = rewrite(Path(a.resources) / tier / "countries.tsv", shares)
        print(f"{tier}: {filled} of {rows} countries have language shares")
    if unnamed:
        print("languages with no name in CLDR or ISO 639-3, left out:")
        for cca2, items in sorted(unnamed.items()):
            print(f"  {cca2}: {', '.join(items)}")


if __name__ == "__main__":
    main()
