# Data and Method

Where the data in `resources/*/countries.tsv` comes from and how it is built. For using the library, see the [usage guide](usage.md).

[TOC]

## Tiers

| Tier | Countries | Which |
| --- | --- | --- |
| `lite` | 195 | Independent (sovereign) states |
| `full` | 250 | Every country and territory in the sources |

## Sources

| Columns | Source |
| --- | --- |
| ISO codes, names, area, flag | [mledoze/countries](https://github.com/mledoze/countries) |
| Coordinates, region and subregion, capital, languages, currency, borders, demonyms, dialling codes, TLD, landlocked, independent, UN member | [REST Countries](https://restcountries.com/) v3.1, falling back to mledoze where REST Countries has no value |
| Population, timezones, continents, driving side, start of week | REST Countries only |
| `income_level` | [World Bank](https://data.worldbank.org/) income classification |

`scripts/prepare_countries.py` downloads all three, merges them by ISO alpha-3 code and writes both tiers:

```bash
python scripts/prepare_countries.py              # both tiers
python scripts/prepare_countries.py --tier full  # one tier
```

After changing the data, run `make embed` to regenerate the compiled-in copy, and update the known-answer tests (the data hash and results change).

## Columns

Lists (`capital`, `continent`, `languages`, `borders`, `timezones`) are separated by `;`. Flags are `1` or `0`.

| Column | Meaning | Notes |
| --- | --- | --- |
| `cca2`, `cca3`, `ccn3` | ISO 3166-1 alpha-2, alpha-3 and numeric codes | `cca2` is the key of extension layers |
| `name_common`, `name_official` | English names | |
| `capital` | Capital city or cities | |
| `region`, `subregion`, `continent` | UN geoscheme region and subregion; continent(s) | `region` drives `get_country(region)` |
| `latitude`, `longitude` | Representative point, decimal degrees | |
| `area` | Land area in km² | `-1` in the source means unknown (Svalbard and Jan Mayen) and reads as 0 |
| `population` | Total population | Selection weight; a population of 0 (5 territories in `full`) counts as 1, so every country can be drawn |
| `landlocked`, `independent`, `un_member` | Flags | |
| `languages`, `currency_*`, `borders`, `timezones`, `driving_side`, `tld`, `idd_*`, `demonym_*`, `flag_emoji`, `income_level`, `start_of_week` | As named | May be empty; `income_level` is "Not classified" for some countries (Ethiopia, for example) |

## Generation Model

- **Weighted (default):** a country's chance is its population divided by the total population loaded (or of its region, for `get_country(region)`).
- **Uniform:** every loaded country (or every country in the region) is equally likely.

Both draw from the `"country"` stream split from the call seed, with dasmig-core's weighted choice and uniform integer, so the result is the same on every platform.

## Known Limitations

- Population is a single snapshot from REST Countries; it is not tied to a year.
- `latitude` and `longitude` are a representative point, not a centroid or the capital's location.
- Only the first currency is kept for countries with several.
