#!/usr/bin/env python3
"""Download country data and produce TSV files for country-generator.

Sources:
  - mledoze/countries (ODbL) — names, ISO codes, languages, currencies,
    borders, demonyms, geographic basics
  - REST Countries v3.1 (open) — population, timezones, driving side,
    continents, capital coordinates
  - World Bank API — income level classification

Produces two tiers:
  resources/full/countries.tsv  — all 250 countries/territories
  resources/lite/countries.tsv  — sovereign states only (~195)

Usage:
    python prepare_countries.py              # generate both tiers
    python prepare_countries.py --tier full  # full only
    python prepare_countries.py --tier lite  # lite only
"""

import argparse
import io
import json
import os
import sys
import urllib.request
import zipfile

# ---------------------------------------------------------------------------
# URLs
# ---------------------------------------------------------------------------

_MLEDOZE_URL = (
    "https://raw.githubusercontent.com/mledoze/countries"
    "/master/countries.json"
)

_RESTCOUNTRIES_URL = (
    "https://gitlab.com/restcountries/restcountries"
    "/-/raw/master/src/main/resources/countriesV3.1.json"
)

_WORLDBANK_URL = (
    "https://api.worldbank.org/v2/country"
    "?format=json&per_page=400"
)

# ---------------------------------------------------------------------------
# TSV columns
# ---------------------------------------------------------------------------

_COLUMNS = [
    "cca2",             # ISO 3166-1 alpha-2
    "cca3",             # ISO 3166-1 alpha-3
    "ccn3",             # ISO 3166-1 numeric
    "name_common",      # Common English name
    "name_official",    # Official English name
    "capital",          # Capital city (semicolon-separated if multiple)
    "region",           # UN geoscheme region
    "subregion",        # UN geoscheme subregion
    "continent",        # Continent (semicolon-separated if multiple)
    "latitude",         # WGS84 decimal degrees
    "longitude",        # WGS84 decimal degrees
    "area",             # Total land area (km²)
    "population",       # Total population
    "landlocked",       # 1 or 0
    "independent",      # 1 or 0
    "un_member",        # 1 or 0
    "languages",        # Semicolon-separated language names
    "currency_code",    # Primary ISO 4217 code
    "currency_name",    # Primary currency name
    "currency_symbol",  # Primary currency symbol
    "borders",          # Semicolon-separated alpha-3 codes
    "timezones",        # Semicolon-separated UTC offsets
    "driving_side",     # "right" or "left"
    "tld",              # Country-code TLD (e.g. ".br")
    "idd_root",         # IDD root (e.g. "+5")
    "idd_suffix",       # Primary IDD suffix (e.g. "5")
    "demonym_m",        # English male demonym
    "demonym_f",        # English female demonym
    "flag_emoji",       # Unicode flag emoji
    "income_level",     # World Bank income level
    "start_of_week",    # "monday" or "sunday" or "saturday"
]

_HEADER = "\t".join(_COLUMNS)


# ---------------------------------------------------------------------------
# Download helpers
# ---------------------------------------------------------------------------

def _fetch_json(url: str) -> list | dict:
    """Download JSON from a URL."""
    print(f"  Downloading {url} ...")
    req = urllib.request.Request(url, headers={"User-Agent": "country-generator/1.0"})
    resp = urllib.request.urlopen(req, timeout=60)  # noqa: S310
    raw = resp.read()
    print(f"  Downloaded {len(raw)} bytes.")
    return json.loads(raw)


def _fetch_mledoze() -> dict[str, dict]:
    """Return mledoze data keyed by alpha-3."""
    data = _fetch_json(_MLEDOZE_URL)
    return {c["cca3"]: c for c in data if c.get("cca3")}


def _fetch_restcountries() -> dict[str, dict]:
    """Return REST Countries data keyed by alpha-3."""
    data = _fetch_json(_RESTCOUNTRIES_URL)
    return {c["cca3"]: c for c in data if c.get("cca3")}


def _fetch_worldbank_income() -> dict[str, str]:
    """Return World Bank income levels keyed by alpha-3."""
    # World Bank API returns [metadata, data]
    raw = _fetch_json(_WORLDBANK_URL)
    if not isinstance(raw, list) or len(raw) < 2:
        return {}
    result: dict[str, str] = {}
    for entry in raw[1] or []:
        code = entry.get("id", "").upper()
        income = entry.get("incomeLevel", {})
        level = income.get("value", "") if isinstance(income, dict) else ""
        if code and len(code) == 3 and level:
            result[code] = level
    return result


# ---------------------------------------------------------------------------
# Merge & flatten
# ---------------------------------------------------------------------------

def _first_currency(currencies: dict | None) -> tuple[str, str, str]:
    """Extract the first currency code, name, and symbol."""
    if not currencies:
        return ("", "", "")
    code = next(iter(currencies))
    info = currencies[code]
    return (code, info.get("name", ""), info.get("symbol", ""))


def _build_row(m: dict, rc: dict, income: str) -> dict[str, str]:
    """Merge mledoze + REST Countries + World Bank into a flat row."""
    # Prefer REST Countries for fields it has that mledoze doesn't
    name = m.get("name", {})
    latlng = rc.get("latlng") or m.get("latlng") or [0, 0]
    car = rc.get("car", {}) or {}
    idd = rc.get("idd") or m.get("idd") or {}
    demonyms = (rc.get("demonyms") or m.get("demonyms") or {}).get("eng", {})
    currencies = rc.get("currencies") or m.get("currencies")
    cc, cn, cs = _first_currency(currencies)

    # Languages: collect values
    langs = rc.get("languages") or m.get("languages") or {}
    lang_list = sorted(set(langs.values())) if isinstance(langs, dict) else []

    capitals = rc.get("capital") or m.get("capital") or []
    borders = rc.get("borders") or m.get("borders") or []
    timezones = rc.get("timezones") or []
    continents = rc.get("continents") or []
    tlds = rc.get("tld") or m.get("tld") or []

    suffixes = idd.get("suffixes") or []

    return {
        "cca2": m.get("cca2", ""),
        "cca3": m.get("cca3", ""),
        "ccn3": m.get("ccn3", ""),
        "name_common": name.get("common", ""),
        "name_official": name.get("official", ""),
        "capital": ";".join(capitals),
        "region": rc.get("region") or m.get("region", ""),
        "subregion": rc.get("subregion") or m.get("subregion", ""),
        "continent": ";".join(continents),
        "latitude": str(latlng[0]) if len(latlng) > 0 else "0",
        "longitude": str(latlng[1]) if len(latlng) > 1 else "0",
        "area": str(int(m.get("area", 0) or rc.get("area", 0) or 0)),
        "population": str(rc.get("population") or 0),
        "landlocked": "1" if (rc.get("landlocked") or m.get("landlocked")) else "0",
        "independent": "1" if (rc.get("independent") or m.get("independent")) else "0",
        "un_member": "1" if (rc.get("unMember") or m.get("unMember")) else "0",
        "languages": ";".join(lang_list),
        "currency_code": cc,
        "currency_name": cn,
        "currency_symbol": cs,
        "borders": ";".join(borders),
        "timezones": ";".join(timezones),
        "driving_side": car.get("side", "right"),
        "tld": tlds[0] if tlds else "",
        "idd_root": idd.get("root", ""),
        "idd_suffix": suffixes[0] if suffixes else "",
        "demonym_m": demonyms.get("m", ""),
        "demonym_f": demonyms.get("f", ""),
        "flag_emoji": m.get("flag", ""),
        "income_level": income,
        "start_of_week": rc.get("startOfWeek", "monday"),
    }


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def _write_tsv(rows: list[dict[str, str]], output: str) -> None:
    """Write rows to a TSV file."""
    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
    with open(output, "w", encoding="utf-8", newline="\n") as f:
        f.write(_HEADER + "\n")
        for row in rows:
            line = "\t".join(row.get(col, "") for col in _COLUMNS)
            f.write(line + "\n")
    print(f"  Wrote {len(rows)} countries to {output}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Prepare country TSV files for country-generator."
    )
    parser.add_argument(
        "--tier",
        default="both",
        choices=["full", "lite", "both"],
        help="Which dataset tier to generate (default: both).",
    )
    args = parser.parse_args()

    # Download sources
    print("=== Downloading data sources ===")
    mledoze = _fetch_mledoze()
    restcountries = _fetch_restcountries()
    worldbank = _fetch_worldbank_income()

    print(f"\n  mledoze:        {len(mledoze)} entries")
    print(f"  REST Countries: {len(restcountries)} entries")
    print(f"  World Bank:     {len(worldbank)} entries")

    # Merge: use mledoze as the primary key set
    all_codes = sorted(set(mledoze.keys()) | set(restcountries.keys()))

    rows_full: list[dict[str, str]] = []
    rows_lite: list[dict[str, str]] = []

    for code in all_codes:
        m = mledoze.get(code, {})
        rc = restcountries.get(code, {})
        income = worldbank.get(code, "")

        if not m and not rc:
            continue

        # Ensure cca3 is set
        if not m.get("cca3"):
            m["cca3"] = code
        if not m.get("name"):
            m["name"] = rc.get("name", {})

        row = _build_row(m, rc, income)
        rows_full.append(row)

        # Lite: sovereign states only
        if row["independent"] == "1":
            rows_lite.append(row)

    # Sort by common name
    rows_full.sort(key=lambda r: r["name_common"])
    rows_lite.sort(key=lambda r: r["name_common"])

    base_dir = os.path.join(os.path.dirname(__file__), "..", "resources")
    tiers = ["full", "lite"] if args.tier == "both" else [args.tier]

    for tier in tiers:
        rows = rows_full if tier == "full" else rows_lite
        output = os.path.join(base_dir, tier, "countries.tsv")
        print(f"\n=== {tier} ===")
        _write_tsv(rows, output)


if __name__ == "__main__":
    main()
