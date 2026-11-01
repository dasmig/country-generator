# Data and Method

Where the data in `resources/*/` comes from and how it is built: `countries.tsv`, then the locale tables (locales, time zones, currencies and holidays), below. For using the library, see the [usage guide](usage.md).

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
| `language_shares` | [Unicode CLDR](https://cldr.unicode.org/) `territoryInfo` (Unicode License v3), added by `scripts/prepare_language_shares.py`; names CLDR lacks come from the [SIL ISO 639-3](https://iso639-3.sil.org/) code table |

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
| `region`, `subregion`, `continent` | UN geoscheme region and subregion; continent(s) | `region` drives the `country_request::region` filter |
| `latitude`, `longitude` | Representative point, decimal degrees | |
| `area` | Land area in km² | `-1` in the source means unknown (Svalbard and Jan Mayen) and reads as 0 |
| `population` | Total population | Selection weight; a population of 0 (5 territories in `full`) counts as 1, so every country can be drawn |
| `landlocked`, `independent`, `un_member` | Flags | |
| `language_shares` | `Name:percent` pairs, largest first | Optional. The share of the population that speaks each language (CLDR's estimates, at least 1%); shares overlap and can sum above 100. Each percent is above 0 and at most 100 |
| `languages`, `currency_*`, `borders`, `timezones`, `driving_side`, `tld`, `idd_*`, `demonym_*`, `flag_emoji`, `income_level`, `start_of_week` | As named | May be empty; `income_level` is "Not classified" for some countries (Ethiopia, for example) |

`timezones` (UTC offsets) and `start_of_week` (REST Countries) stay for compatibility. The locale tables have better sources for both: a zone's IANA name and rules in `zones.tsv`, and a locale's `first_day` from CLDR in `locales.tsv`.

## Generation Model

- **Weighted (default):** a country's chance is its population divided by the total population loaded (or of the region specified by `country_request::region`).
- **Uniform:** every loaded country (or every country in the region) is equally likely.

Both draw from the `"country"` stream split from the call seed, with dasmig-core's weighted choice and uniform integer, so the result is the same on every platform.

## Known Limitations

- Population is a single snapshot from REST Countries; it is not tied to a year.
- `latitude` and `longitude` are a representative point, not a centroid or the capital's location.
- Only the first currency is kept for countries with several. That is not always the country's main currency: Palestine has EGP. The locale tables correct it (ILS).

## Locale Tables

Seven tables next to `countries.tsv` in each tier. Each names its sources and versions in its first lines. Loading them does not change anything `get_country` returns or its `data_hash()`.

| Table | Rows in `lite` (`full`) | Key | Source |
| --- | --- | --- | --- |
| `locales.tsv` | 499 (570) | `country`, `tag` | [Unicode CLDR](https://cldr.unicode.org/) 47 (cldr-json) |
| `zones.tsv` | 350 (421) | `country`, `zone` | [IANA tzdata](https://www.iana.org/time-zones) 2026f, as compiled in the PyPI `tzdata` package 2026.6; [GeoNames](https://www.geonames.org/) `cities1000` for the shares |
| `zone_links.tsv` | 140 (131) | `link` | IANA's `backward` file; in `lite` also the zones of territories it leaves out (`Europe/Jersey` is `Europe/London`) |
| `currencies.tsv` | 169 | `code` | ISO 4217 list one (published by SIX), the withdrawn codes `countries.tsv` still uses from list three; cash digits from CLDR |
| `holidays.tsv` | 5,136 (6,611) | `country`, `key`, `from_year` | Inferred from [python-holidays](https://github.com/vacanza/holidays) 0.107 |
| `holiday_events.tsv` | 14,925 (16,108) | `event`, `country`, `date` | python-holidays' calendars, and countries' own dates |
| `holiday_years.tsv` | 195 (250) | `country` | python-holidays' years per country, within 1970 to 2050 |

They are built by three scripts, which download their pinned sources and write both tiers:

```bash
python scripts/prepare_locales.py    # locales.tsv, currencies.tsv
python scripts/prepare_zones.py      # zones.tsv, zone_links.tsv
python scripts/prepare_holidays.py   # holidays.tsv, holiday_events.tsv, holiday_years.tsv
```

Then `make embed` regenerates `dasmig/countrygen_locale_data.hpp`. Each rebuild is checked by the validation scripts below, and changes only the locale, zone and holiday known answers.

### Locales

A country's locales are the languages CLDR's `territoryInfo` gives it that at least 1% of the population speaks, and that are official there in any form or spoken by at least 5%, and that CLDR has locale data for. So Canada has `en-CA` (87) and `fr-CA` (29), not Spanish or Punjabi; the United States has `en-US` and `es-US`.

| Column | Meaning |
| --- | --- |
| `country`, `tag` | ISO alpha-2 code, and the BCP 47 tag, minimised with CLDR's likely subtags (`zh-Hant-TW` is `zh-TW`; `sr-Latn-RS` keeps its script) |
| `language`, `script`, `language_name` | ISO 639 code; ISO 15924 script when the tag has one; the English name, as `language_shares` writes it |
| `share` | Percent of the country that speaks the language (CLDR's estimate). Shares overlap, so they are relative weights: `fr-CA` is drawn 29 / (87 + 29) = 25% of the time |
| `basis` | `cldr`: CLDR has the locale for the country. `inherited`: CLDR has only the language's formats, used with the country's own week and measurement data (`en-AR`). `fallback`: no language qualifies, so one row for the first of `countries.tsv`'s `languages`, with the formats of a language CLDR has (`en-HM`, `full` only) |
| `date_short` … `date_full`, `time_short`, `time_medium` | CLDR's Gregorian patterns (LDML). The long and full time patterns are left out, since they carry a zone name |
| `months`, `months_abbr`, `weekdays`, `weekdays_abbr`, `am`, `pm`, `eras` | Names in the format context, `;`-separated; months from January, weekdays from Sunday; abbreviated eras |
| `decimal`, `group`, `minus`, `number_pattern`, `currency_pattern`, `currency_spacing` | The Latin number system's symbols, standard patterns and the text inserted next to a letter currency symbol |
| `currency_code`, `currency_symbol` | The country's currency and this locale's symbol for it. This is `countries.tsv`'s `currency_code` when it is on ISO 4217 list one, else CLDR's current tender. Palestine is the exception: `countries.tsv` has EGP, the first of the currencies mledoze lists, and the locale has ILS, the currency the country uses and reports its statistics in |
| `first_day`, `weekend_start`, `weekend_end`, `measurement` | CLDR's week data and measurement system for the country |

The build resolves CLDR's inheritance chain (`en-NG` → `en-001` → `en`), so every row is complete. It rewrites the flexible day period `B` to `a` (AM and PM) where CLDR uses it, and fails on any pattern field the formatter does not support. A CLDR locale with no confirmed Gregorian data of its own, which cldr-json fills with the root locale's placeholders (months `M01` to `M12`), is left out; 28 locales in `lite` were, among them `dv-MV`, `la-VA` and `ny-MW`, and every country kept a locale.

### Time Zones

| Column | Meaning |
| --- | --- |
| `country`, `zone` | ISO alpha-2 code and IANA zone name |
| `share` | Percent of the country's population (GeoNames places of at least 1,000 people) in the zone; a zone with no such place gets the country's smallest share, so it can be drawn |
| `basis` | `tzdata`: IANA's `zone.tab` lists it for the country. `geonames`: IANA lists no zone for the country, so its places' zones are used (Kosovo: `Europe/Belgrade`). `fallback`: neither has one, so a fixed `Etc` zone from `countries.tsv`'s first UTC offset (`BV` and `HM` in `full`) |
| `periods`, `posix` | The rules in `core::tz_rules`' text form: each offset change from the tier's start (2000 in `lite`, 1970 in `full`) to the last one the POSIX rule does not give, then that rule for every year after |

The rules are read from the compiled TZif files, not from the operating system, so they are the same on every platform.

### Currencies

| Column | Meaning |
| --- | --- |
| `code`, `numeric` | ISO 4217 alphabetic and numeric codes |
| `digits` | ISO 4217 minor unit (0 for JPY, 3 for KWD). ISO, not CLDR, which deliberately differs for a few currencies (it gives `IQD` 0 digits, ISO 3) |
| `cash_digits` | Optional: CLDR's digits for cash where they differ |

### Holidays

A holiday here is a national day off, or a named day for observances, as python-holidays gives it for the country with no subdivision. Regional holidays (German states, US states) are not included: the UK gives the holidays common to its four nations, Germany its nine national ones.

| Column | Meaning |
| --- | --- |
| `country`, `key` | ISO alpha-2 code, and a stable key in snake case (`christmas_day`) |
| `name`, `name_en` | The name in the country's default language, as python-holidays writes it, and in English. Empty names repeat those of the row above, which has the same country and key |
| `kind` | `public`, `bank`, `optional` (python-holidays' `optional`, `government` and `half_day`) or `observance` (`unofficial`) |
| `rule` | `MM-DD`; `MM:N:DOW`, the N-th weekday (`11:4:thu`; `-1` is the last); `MM-DD>DOW` or `MM-DD<DOW`, that weekday on or after, or before; `easter+N` and `orthodox+N`; `event:KEY+N`, a date of `holiday_events.tsv` |
| `days` | Consecutive days from the date (Saudi Arabia's Eid al-Fitr holiday is 4) |
| `observed` | What a weekend does: `none`; `sun>mon`; `sat<fri,sun>mon` (US federal); `sat>mon,sun>mon` (UK); `next_weekday`; `weekend>next` (a multi-day holiday gains a day); or a map of weekdays (`tue<mon,thu>fri`, Angola's bridge days). An `,overlap` suffix moves onto a taken day too. The weekend is the country's own, from CLDR |
| `from_year`, `to_year` | The years the rule holds, which is how law changes are kept (US Juneteenth from 2021) |

`holiday_events.tsv` holds the dates no rule computes: the Islamic, Chinese, Hebrew, Hindu and Buddhist calendars, Japan's equinoxes, and each country's own announced or decreed days. A row with country `*` is the shared date; a row with a country overrides it for that country, as Islamic dates follow moon sightings. `holiday_years.tsv` gives the span each country is served for: python-holidays' years within 1970 to 2050, so 1991 on for Germany and 2001 to 2035 for India.

**Inference.** `prepare_holidays.py` reads every country and year from python-holidays, groups the dates into one series per holiday, and fits the fewest rules that explain them: fixed dates, weekdays, Easter offsets, then calendar events. It fits the observed policy year by year. Then it evaluates the rules as the library does and compares them with python-holidays for every country, year and kind. A year whose moved days no policy reproduces (a decree) is written with `none`, and python-holidays' moved days become the country's own event. The result has no difference from python-holidays in either tier.

**Countries without holidays.** `BV`, `HM` and `IO` (all in `full` only) have a span but no holidays, which is not the same as not being covered. Ukraine has no public holidays from 2023, under martial law.

### Validation

Run by hand at each rebuild, with the pinned versions of the sources; `make dump` builds the binary they read the library's answers from.

| Script | Compares with | Result (both tiers) |
| --- | --- | --- |
| `scripts/validate_zones.py` | Python's `zoneinfo` on the same IANA release: every transition from the tier's start to 2037, and zoneinfo's own changes, found by sampling every day | No difference |
| `scripts/validate_holidays.py` | python-holidays 0.107, every country, year and kind | No difference |
| `scripts/validate_formats.py` | Babel at the same CLDR version, 1,000 dates, times, numbers and amounts per locale, given the row's own data | No difference, after normalising three things Babel does not do: currency spacing, the locale's minus sign and ISO digits |
| `scripts/validate_locales.py` | `user-generator`'s hand-written locales | A report only |

### Limitations

- National holidays only, as python-holidays defines them; dates python-holidays estimates (some Islamic dates far ahead) are its estimates.
- The Gregorian calendar and Latin digits only in formatted output; other calendars only date holidays.
- No time-zone names in text ("Central European Summer Time"); the abbreviation is given.
- A locale's share is CLDR's estimate of speakers, not of people who use that locale on their devices.
- `zones.tsv` ends with tzdata 2026f's rules; later changes need a rebuild.
