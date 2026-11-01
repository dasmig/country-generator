# Usage Guide

This guide covers every feature of the country-generator library in detail. For a quick overview, see the [README](../README.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating-countries">Generating Countries</a></li>
<li><a href="#per-call-requests">Per-Call Requests</a></li>
<li><a href="#country-fields">Country Fields</a></li>
<li><a href="#languages-and-demonyms">Languages and demonyms</a></li>
<li><a href="#locales">Locales</a></li>
<li><a href="#time-zones">Time Zones</a></li>
<li><a href="#holidays">Holidays</a></li>
<li><a href="#currencies">Currencies</a></li>
<li><a href="#region-filtering">Region Filtering</a></li>
<li><a href="#cca2-lookup">CCA2 Lookup</a></li>
<li><a href="#population-weighting">Population Weighting</a></li>
<li><a href="#uniform-selection">Uniform Selection</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
<li><a href="#multi-instance-support">Multi-Instance Support</a></li>
<li><a href="#data-pipeline">Data Pipeline</a></li>
<li><a href="#extending-data">Extending Data</a></li>
<li><a href="#replay-and-json">Replay and JSON</a></li>
<li><a href="#compiled-in-data">Compiled-in Data</a></li>
<li><a href="#migrating-to-request-first-generation">Migrating to Request-First Generation</a></li>
<li><a href="#migrating-from-v1">Migrating from v1</a></li>
<li><a href="#thread-safety">Thread Safety</a></li>
<li><a href="#batch-generation">Batch Generation</a></li>
<li><a href="#error-reference">Error Reference</a></li>
</ul>

## Quick Start

```cpp
#include <dasmig/countrygen.hpp>
#include <iostream>

int main()
{
    auto& gen = dasmig::cntg::instance();

    // Random country (population-weighted).
    auto c = gen.get_country({});
    std::cout << c.name_common << ", " << c.cca2 << "\n";

    // Country from a specific region.
    auto eu = gen.get_country({.region = "Europe"});
    std::cout << eu.name_common << " (pop. " << eu.population << ")\n";
}
```

## Installation

1. Copy `dasmig/countrygen.hpp`, `dasmig/core.hpp` and the `dasmig/core/` directory into your include path, keeping them side by side.
2. Copy the `resources/` folder (containing `full/` and/or `lite/` subdirectories) so it is accessible at runtime. Each tier holds `countries.tsv` and the locale tables; leave the locale tables out if you need only countries.
3. Compile with C++23 enabled: `-std=c++23`.

## Loading Resources

The library ships two dataset tiers:

| Tier | Enum | Countries | Description |
|------|------|-----------|-------------|
| lite | `dasmig::dataset::lite` | ~195 | Sovereign states only |
| full | `dasmig::dataset::full` | ~250 | All countries and territories |

Resources are stored as `resources/lite/countries.tsv` and `resources/full/countries.tsv`. Next to each are the locale tables: `locales.tsv`, `zones.tsv`, `zone_links.tsv`, `currencies.tsv`, `holidays.tsv`, `holiday_events.tsv` and `holiday_years.tsv` (see [Data and Method](data.md#locale-tables)). Loading a tier loads every one of them it finds; a missing table is not an error, and only the getters that need it throw.

### Automatic loading (singleton)

On first access the singleton loads the `lite` tier (or `full` if `lite` is missing). It uses the compiled-in copy when built with `DASMIG_COUNTRY_EMBED` (see Compiled-in Data), and otherwise looks for `lite/countries.tsv` in these directories, relative to the working directory:

| Priority | Base path |
|----------|-----------|
| 1 | `resources/` |
| 2 | `../resources/` |
| 3 | `country-generator/resources/` |
| 4 | `../country-generator/resources/` |

### Explicit tier loading

```cpp
dasmig::cntg gen;
gen.load(dasmig::dataset::lite);  // ~195 sovereign states
gen.load(dasmig::dataset::full);  // ~250 countries and territories
```

If your resources are elsewhere, call `load()` with a direct path:

```cpp
dasmig::cntg::instance().load("/opt/data/lite");           // a tier directory: every table in it
dasmig::cntg::instance().load("/opt/data/countries.tsv");  // a file: the countries only
```

Each `load()` replaces the data loaded before (use `extend()` to add on top; see Extending Data).
`load(dataset)` returns `bool` (`true` if the tier's `countries.tsv` was found and loaded).
`load(path)` returns `void` and does nothing if the path is neither a file nor a directory holding `countries.tsv`. A directory's missing locale tables are cleared; a file leaves the locale tables as they are. A malformed file throws `dasmig::core::tsv_error` with its file, line and column, and nothing changes.
`has_locale_data()` says whether any locale table is loaded.

## Generating Countries

### Random (population-weighted)

```cpp
auto c = dasmig::cntg::instance().get_country({});
std::string text = c;  // implicit conversion — returns the common name
```

### Region-filtered

```cpp
auto c = dasmig::cntg::instance().get_country({.region = "Asia"});
// c.region == "Asia"
```

## Per-Call Requests

Every generation call takes a `dasmig::country_request` first. Use
`get_country(request)` for a random call, or `get_country(request, seed)` for
a deterministic call:

```cpp
dasmig::country_request request{.region = "Europe", .weighted = false};
auto random = gen.get_country(request); // uniform within Europe
auto c = gen.get_country(request, 42);   // deterministic uniform within Europe
auto replay = gen.get_country(request, c.seed());

auto any = gen.get_country(dasmig::country_request{}, 42); // population-weighted
auto uniform = gen.get_country(dasmig::country_request{.weighted = false}, 42);
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `region` | `std::optional<std::string>` | Unset | All loaded countries; set to a case-sensitive UN geoscheme region to filter |
| `weighted` | `bool` | `true` | Population weighting; `false` gives every matching country the same probability |

An explicitly empty or unknown region throws `std::invalid_argument`.
No loaded data throws `std::runtime_error` before region validation.

All selection choices come from the request, with no mutable generator
defaults. Random calls draw a call seed from the internal engine; explicit
seed calls do not advance it and can be made on a const generator. The only
generation overloads are `get_country(request)` and `get_country(request, seed)`.

The returned `country` and its JSON format are unchanged. To replay a
request-based result, retain the request as well as the seed and use the same
data. The result signature records the seed and data hash, not the request.

## Country Fields

Every generated country includes all 32 data fields:

| Field | Type | Description |
|-------|------|-------------|
| `cca2` | `string` | ISO 3166-1 alpha-2 code |
| `cca3` | `string` | ISO 3166-1 alpha-3 code |
| `ccn3` | `string` | ISO 3166-1 numeric code |
| `name_common` | `string` | Common English name |
| `name_official` | `string` | Official English name |
| `capital` | `string` | Capital city (`;`-separated if multiple) |
| `region` | `string` | UN geoscheme region |
| `subregion` | `string` | UN geoscheme subregion |
| `continent` | `string` | Continent (`;`-separated if multiple) |
| `latitude` | `double` | WGS84 decimal degrees |
| `longitude` | `double` | WGS84 decimal degrees |
| `area` | `uint64_t` | Total land area in km² |
| `population` | `uint64_t` | Total population |
| `landlocked` | `bool` | Whether landlocked |
| `independent` | `bool` | Sovereign state flag |
| `un_member` | `bool` | UN membership |
| `languages` | `string` | `;`-separated language names |
| `language_shares` | `string` | `;`-separated `Name:percent` pairs, largest first (see Languages and demonyms) |
| `currency_code` | `string` | Primary ISO 4217 code |
| `currency_name` | `string` | Primary currency name |
| `currency_symbol` | `string` | Primary currency symbol |
| `borders` | `string` | `;`-separated alpha-3 border codes |
| `timezones` | `string` | `;`-separated UTC offsets |
| `driving_side` | `string` | `"right"` or `"left"` |
| `tld` | `string` | Country-code TLD |
| `idd_root` | `string` | IDD root (e.g. `"+5"`) |
| `idd_suffix` | `string` | IDD primary suffix |
| `demonym_m` | `string` | English male demonym |
| `demonym_f` | `string` | English female demonym |
| `flag_emoji` | `string` | Unicode flag emoji |
| `income_level` | `string` | World Bank income level |
| `start_of_week` | `string` | `"monday"`, `"sunday"`, or `"saturday"` |

```cpp
auto c = gen.get_country({});
std::cout << c.name_common << " (" << c.cca2 << ")\n";
std::cout << "Capital: " << c.capital << "\n";
std::cout << "Population: " << c.population << "\n";
std::cout << "Currency: " << c.currency_name << " ("
          << c.currency_symbol << ")\n";
std::cout << "Driving side: " << c.driving_side << "\n";
```

## Languages and demonyms

`languages` lists a country's official languages. `language_shares` adds how many people speak each, from Unicode CLDR: the languages at least 1% of the population speaks, as `Name:percent` pairs. The shares overlap (a country where many people speak two languages sums above 100), because each is the share of people who speak that language.

```cpp
auto ch = gen.find_country("CH");
ch.language_list();          // {"French", "Italian", "Romansh", "Swiss German"}
ch.language_distribution();  // {{"German", 76}, {"Swiss German", 66}, {"English", 45}, ...}
ch.primary_language();       // "German" (the largest share, not an official-list entry)
ch.demonym(dasmig::sex::female);   // "Swiss"

// A language a person of the country speaks, in proportion to the shares.
std::string language = gen.get_language("CH", seed);
```

`get_language(cca2, seed)` is a pure function of the data, the country and the seed. A country with no shares falls back to the first of its `languages`; one with no language data at all throws `std::invalid_argument`. `primary_language()` does the same fallback. `demonym(sex)` returns `demonym_m` or `demonym_f`, and the other one when the wanted form is empty.

Layers can set `language_shares` like any column; a malformed value (a pair without a percent, or a percent that is not above 0 and at most 100) is rejected with its row and column. `to_json()` writes the shares as an array of `{"name", "percent"}`.

## Locales

A country's locales are the languages its people speak, as BCP 47 tags (`en-CA`, `fr-CA`), each with the date, time, number and currency formats of [Unicode CLDR](https://cldr.unicode.org/). `get_locale` draws one by speakers:

```cpp
auto& gen = dasmig::cntg::instance();

auto loc = gen.get_locale({.country = "CA"}, 42);   // fr-CA (en-CA has 87, fr-CA 29)
auto any = gen.get_locale({}, 5);                   // country drawn as get_country({}, 5) draws it
auto fr = gen.get_locale({.language = "fr"}, 7);    // a French tag, its country by French speakers

gen.locales("CH");           // de-CH 76, gsw-CH 66, en-CH 45, fr-CH 39, it-CH 15
gen.find_locale("pt_br");    // pt-BR: any case, "_" for "-", and "pt-Latn-BR" also work
```

| `locale_request` field | Default | Meaning |
|-------|---------|---------|
| `country` | Unset | ISO alpha-2 code, any case. Unset draws the country exactly as `get_country` does, so `get_locale({}, s).country_code == get_country({}, s).cca2` |
| `language` | Unset | ISO 639 code (`"fr"`), any case. Only that language's tags; with no country, the country is drawn by that language's speakers |
| `weighted` | `true` | Weighted by `share`, the percent of the country that speaks the language; `false` makes every eligible tag, and country, equally likely |

A drawn country with no locale (Antarctica in `full`) is replaced by a draw among the countries that have one. `locales(country)` lists a country's locales by share, and `find_locale(tag)` looks one up; both give seed 0.

**Formatting.** A `locale` formats with its own patterns, names and symbols:

```cpp
using namespace std::chrono;
auto br = gen.find_locale("pt-BR");
br.format_date(2024y / December / 31);                     // "31/12/2024"
br.format_date(2024y / December / 31, dasmig::date_style::long_);   // "31 de dezembro de 2024"
br.format_date(2024y / December / 31, dasmig::date_style::full);    // "terça-feira, 31 de dezembro de 2024"
br.format_time(hh_mm_ss<seconds>{15h + 4min});            // "15:04"
br.format_number(1234567.891);                             // "1.234.567,891"
br.format_number(2.675, 2);                                // "2,68" (half-even on the decimal text)

auto brl = dasmig::core::money::from_minor(123450, "BRL", gen.currency_digits("BRL"));
br.format_money(brl);                                      // "R$ 1.234,50"
br.format_money(dasmig::core::money::from_minor(123450, "USD", 2));   // "USD 1.234,50"

gen.find_locale("en-US").format_time(hh_mm_ss<seconds>{15h + 4min}, dasmig::time_style::medium);  // "3:04:00 PM"
gen.find_locale("en-IN").format_number(1234567.891);       // "12,34,567.891"
```

- `date_style` is `short_`, `medium`, `long_` or `full`; `time_style` is `short_` or `medium`. Digits are always Latin.
- `format_money` uses the locale's symbol for the locale's own currency (`R$` in `pt-BR`) and the ISO code for any other, so an amount is never ambiguous.
- The fields are public: the patterns (`date_short`, `number_pattern`, `currency_pattern`), `months`, `weekdays` (Sunday first), `am`, `pm`, `eras`, the symbols (`decimal`, `group`, `minus`), `currency_code`, `currency_symbol` and `currency_digits`, `first_day`, `weekend_start` and `weekend_end`, and `measurement` (`metric`, `us` or `uk`).
- `basis` says where the row came from: `cldr` (CLDR has the locale for the country), `inherited` (CLDR has only the language's formats, so `en-AR` writes as `en` does, with Argentina's week and currency) or `fallback` (no language qualifies, so the country's first language is used: `en-HM` in `full`).

`to_json()` writes the tag, the formats, the symbols, the currency and the week; the month and weekday names are left out to keep records short:

```json
{"tag":"pt-BR","language":"pt","script":"","language_name":"Portuguese","country":"BR","share":91,"basis":"cldr",
 "formats":{"date":{"short":"dd/MM/y","medium":"d 'de' MMM 'de' y","long":"d 'de' MMMM 'de' y","full":"EEEE, d 'de' MMMM 'de' y"},
            "time":{"short":"HH:mm","medium":"HH:mm:ss"},"number":"#,##0.###","currency":"¤ #,##0.00"},
 "symbols":{"decimal":",","group":".","minus":"-"},"currency":{"code":"BRL","symbol":"R$","digits":2},
 "first_day":"sunday","weekend":["saturday","sunday"],"measurement":"metric","signature":{"seed":"...","data":"..."}}
```

## Time Zones

A country's time zones are its [IANA](https://www.iana.org/time-zones) zones, each with the share of the country's population that lives in it (from GeoNames) and its rules as a `core::tz_rules`:

```cpp
auto tz = gen.get_time_zone({.country = "BR"}, 42);   // America/Sao_Paulo (63.6%)
gen.time_zones("US");                                 // America/New_York 42.6, America/Chicago 26.4, ...

auto sp = gen.find_time_zone("America/Sao_Paulo");
sp.utc_offset;                                        // -3h, "-03", no daylight time since 2019
sp.to_local(sys_days{2024y / March / 1} + 15h);       // 2024-03-01 12:00:00 local

gen.find_time_zone("Asia/Calcutta").name;             // "Asia/Kolkata": old names are followed
```

| `time_zone_request` field | Default | Meaning |
|-------|---------|---------|
| `country` | Unset | ISO alpha-2 code, any case; unset draws the country exactly as `get_country` does |
| `reference_date` | 2023-07-01 | The date whose offset, abbreviation and daylight flag the result reports, at 12:00 UTC |
| `weighted` | `true` | Weighted by population share; `false` makes every zone equally likely |

- `utc_offset`, `abbreviation` and `dst` are the rules at the reference date; `observes_dst` is whether the zone has daylight time from then on. `find_time_zone(name, date)` takes the reference date too: Berlin is `CEST` on the default date and `CET` on 2024-01-15.
- `to_local(sys_seconds)` and `to_utc(local_seconds, choose)` convert at any instant. A local time that happens twice when clocks go back is resolved by `core::tz_choose::earliest` (the default) or `latest`; one that never happens when clocks go forward is read with the offset before the change (02:30 on Berlin's spring-forward night is 01:30 UTC).
- `find_time_zone` follows IANA's `backward` links, the old names GeoNames and older systems still write; `city-generator`'s cities carry such names, so pass a city's zone to `find_time_zone` rather than drawing one.
- `basis` is `tzdata` (IANA lists the zone for the country), `geonames` (IANA lists none, so the zones of the country's places are used: Kosovo gets `Europe/Belgrade`), or `fallback` (a fixed `Etc` zone from `countries.tsv`'s first UTC offset, for the two uninhabited territories of `full` with no place).
- The rules start in 2000 in `lite` and 1970 in `full`; an earlier instant throws `std::out_of_range`.

The rules come from the data (IANA 2026f), not from `std::chrono::tzdb`. That database is the operating system's, it differs by platform and update level, and it is missing from Emscripten and older libc++, so the same seed would give different local times on different machines.

## Holidays

`holidays(request)` lists a country's national holidays in a year. It is a lookup, not a draw, so it takes no seed:

```cpp
for (const auto& h : gen.holidays({.country = "BR", .year = 2025}))
    std::cout << h.date << ' ' << h.name << " / " << h.name_en << '\n';
// 2025-01-01 Confraternização Universal / Universal Fraternization Day
// 2025-04-18 Sexta-feira Santa / Good Friday
// ...
// 2025-12-25 Natal / Christmas Day

gen.is_holiday("US", 2021y / December / 31);       // true: New Year's Day 2022, observed
gen.is_holiday("BR", 2025y / March / 4);           // false: Carnival is optional
gen.is_holiday("BR", 2025y / March / 4, true);     // true
gen.holiday_years("BR");                           // {1970, 2050}
```

| `holiday_request` field | Default | Meaning |
|-------|---------|---------|
| `country` | Required | ISO alpha-2 code, any case |
| `year` | Required | Within `holiday_years(country)` |
| `optional` | `false` | Include days off for some only (Brazil's Carnival, government-only days) |
| `observances` | `false` | Include days that are not days off (Mother's Day) |

- Each `holiday` has a `date`, a stable `key` (`christmas_day`), a `name` in the country's language and `name_en` in English, a `kind` (`holiday_kind::public_`, `bank`, `optional` or `observance`) and, for a multi-day holiday, its 1-based `day` (Saudi Arabia's Eid al-Fitr holiday of 2024 is four days, 9 to 12 April).
- `observed` is set when a weekend rule moves the day off: the US's Christmas Day 2022, a Sunday, is observed on Monday 26 December. `is_holiday` counts both dates, and looks at the years either side, since an observed day can cross New Year.
- The list is sorted by date, then key, then day. Regional holidays (German states, US states) are not included.
- A year outside `holiday_years` throws `std::out_of_range`. The span is python-holidays' for that country, within 1970 to 2050.

The holidays are stored as rules (fixed dates, n-th weekdays, Easter offsets, dated events such as Eid) inferred from [python-holidays](https://github.com/vacanza/holidays) 0.107 and checked against it for every country and year; see [Data and Method](data.md#holidays). Where the law changes, a layer corrects a country at once (see Extending Data). `to_json()` writes a holiday with its dates as `YYYY-MM-DD`:

```json
{"date":"2025-01-01","observed":null,"key":"universal_fraternization_day","name":"Confraternização Universal",
 "name_en":"Universal Fraternization Day","kind":"public","day":1}
```

## Currencies

`currency_digits(code)` is the ISO 4217 minor unit of a currency, any case, which is what `core::money` needs:

```cpp
gen.currency_digits("JPY");   // 0
gen.currency_digits("kwd");   // 3
auto price = dasmig::core::money::from_minor(1999, "BRL", gen.currency_digits("BRL"));   // 19.99 BRL
```

A locale's `currency_code` is its country's `currency_code` when that is a current ISO 4217 code; otherwise it is CLDR's current tender for the country, or, where CLDR is behind ISO, a code the build sets.

## Region Filtering

Set a UN geoscheme region on the request to restrict generation:

```cpp
auto eu = gen.get_country({.region = "Europe"});
auto af = gen.get_country({.region = "Africa"});
```

Available regions: `Africa`, `Americas`, `Antarctic`, `Asia`, `Europe`, `Oceania`.

Region-filtered selection is also population-weighted within the region.
Use `country_request{.region = "Europe", .weighted = false}` for uniform
selection within a region.
Throws `std::invalid_argument` if no countries match the region; region names
are case-sensitive.

## CCA2 Lookup

Look up a specific country by its ISO 3166-1 alpha-2 code:

```cpp
auto us = gen.find_country("US");
std::cout << us.name_common << " — " << us.capital << "\n";
// "United States — Washington, D.C."
```

Throws `std::invalid_argument` if no country matches the code, or
`std::runtime_error` if no data has been loaded. Codes are case-sensitive
(uppercase only).

## Population Weighting

By default, countries are selected with probability proportional to their population.
China and India are far more likely to appear than Liechtenstein. Countries
with zero population are assigned a minimum weight of 1.

## Uniform Selection

Switch to equal-probability selection where every loaded country has the
same chance of being picked, regardless of population:

```cpp
auto c = gen.get_country(dasmig::country_request{.weighted = false});
auto e = gen.get_country(
    dasmig::country_request{.region = "Europe", .weighted = false}, 42);
```

Selection mode applies only to the current request. A subsequent
`gen.get_country({})` call is population-weighted, regardless of earlier calls.

## Seeding and Deterministic Generation

### Per-call seeding

Pass a request followed by an explicit seed to produce the same country every time:

```cpp
auto c = dasmig::cntg::instance().get_country({}, 42);
// Always produces the same country for the same request + data + seed.
```

### Seed replay

Every country records its seed. Retrieve it with `country::seed()` and pass
it back with the original request to reproduce the exact same result:

```cpp
dasmig::country_request request{.region = "Europe", .weighted = false};
auto c = dasmig::cntg::instance().get_country(request);
auto saved_seed = c.seed();

// Later, replay:
auto replay = dasmig::cntg::instance().get_country(request, saved_seed);
// replay.cca3 == c.cca3
```

### Deterministic sequences

Supply an explicit seed for each call. For a repeatable sequence, keep the
same seed sequence, requests and data:

```cpp
auto& gen = dasmig::cntg::instance();

auto a = gen.get_country({}, 100);
auto b = gen.get_country({}, 101);
auto a2 = gen.get_country({}, 100); // identical to a
auto b2 = gen.get_country({}, 101); // identical to b
```

The generator has no public `seed()` or `unseed()` controls.

### Region filter with seed

```cpp
dasmig::country_request request{.region = "Americas"};
auto c = gen.get_country(request, 42);
// Deterministic within American countries.
```

> **Note:** Replay with `gen.get_country(request, c.seed())` using the original
> request. Omitting the region or changing the weighting mode may produce a
> different country. The result signature records the seed and data, not
> these request options.

## Multi-Instance Support

Construct independent generators with their own data and engine:

```cpp
dasmig::cntg my_gen;
my_gen.load("path/to/countries.tsv");
auto c = my_gen.get_country({});
```

## Data Pipeline

The shipped datasets are generated from open data sources by `scripts/prepare_countries.py`. [Data and Method](data.md) describes the sources, every column and how selection works.

## Extending Data

The shipped data is a base layer. Files with the same columns as `countries.tsv` can be loaded on top of it to add countries, change them or remove them, without editing the shipped file. Rows are keyed by `cca2`.

```cpp
auto& gen = dasmig::cntg::instance();

// Override mode (the default): a row replaces the loaded country with the
// same code; new codes are added.
gen.extend("mods/fictional_nations.tsv");

// From text, for example supplied by a WebAssembly host or a game scenario.
gen.extend_from_string(text, "scenario: new republic");

// Remove countries: a layer needs only the key and a disabled column.
gen.extend_from_string("cca2\tdisabled\nVA\t1\n", "cleanup");

// Add mode refuses to touch countries that are already loaded.
gen.extend("mods/new_countries.tsv", dasmig::core::layer_mode::add);
```

- A layer must have the columns `cca2`, `cca3`, `name_common`, `region`, `latitude`, `longitude`, `area`, `population`, `landlocked`, `independent` and `un_member`; the other columns are optional and default to empty. Numbers and flags (`0`/`1`) are checked, and latitude and longitude must be in range, before anything changes. A bad layer throws `dasmig::core::tsv_error` with the file, line and column, and the data stays as it was.
- New regions become available to `get_country({.region = region})` at once.
- `layers()` lists what is loaded: source, mode, rows added, replaced and removed, and warnings such as unknown columns.
- `load(...)` replaces the base and drops every layer.

**Locale tables.** Every table takes layers the same way, named by a `dasmig::country_table` (`countries`, `locales`, `zones`, `zone_links`, `currencies`, `holidays`, `holiday_events`, `holiday_years`). The overloads without one mean `countries`. A layer can add a country to `countries.tsv` and its locales, zones and holidays in the other tables, or correct a country's holidays without waiting for a release:

```cpp
// Brazil adds a national day off from 2025 on.
gen.extend_from_string(dasmig::country_table::holidays,
    "country\tkey\tname\tname_en\tkind\trule\tdays\tobserved\tfrom_year\tto_year\n"
    "BR\tnew_day\tDia Novo\tNew Day\tpublic\t06-15\t1\tnone\t2025\t2050\n",
    "law of 2025");
gen.is_holiday("BR", 2025y / June / 15);   // true
```

| Table | Key | Checked before a layer is applied |
|-------|-----|-----------------------------------|
| `locales` | `country`, `tag` | The country is loaded; the tag is well formed; the share is above 0 and at most 100; patterns hold only supported fields; 12 months, 7 weekdays and 2 eras; the week and `measurement` are known values |
| `zones` | `country`, `zone` | The country is loaded; the share is above 0; `periods` and `posix` parse as `core::tz_rules` |
| `zone_links` | `link` | The target zone is loaded |
| `currencies` | `code` | Three capitals, a three-digit `numeric`, `digits` and `cash_digits` from 0 to 4 |
| `holidays` | `country`, `key`, `from_year` | The country is loaded; the key is `[a-z0-9_]+`; the kind, rule and observed policy parse; `days` is 1 to 10; an `event:` rule names an event in `holiday_events` |
| `holiday_events` | `event`, `country`, `date` | A valid date; the country is `*` (every country) or a loaded one |
| `holiday_years` | `country` | The country is loaded; `from_year` is not after `to_year` |

The columns and rule forms are in [Data and Method](data.md#locale-tables). A bad row throws `dasmig::core::tsv_error` with its file, line and column, and the data stays as it was.

## Replay and JSON

Every result records the seed and the data it came from:

```cpp
dasmig::country_request request{};
auto c = gen.get_country(request);
c.seed();                    // the call seed
c.signature().data;          // gen.data_hash() when it was generated

// Same request, same seed, same data: the same country on every
// compiler and platform.
auto again = gen.get_country(request, c.seed());
```

`to_json()` gives the result as JSON, the form the WebAssembly layer and the game consume. Semicolon-separated fields (`capital`, `continent`, `languages`, `borders`, `timezones`) become arrays:

```json
{"cca2":"ET","cca3":"ETH","ccn3":"231","name_common":"Ethiopia",...,"languages":["Amharic"],
 "language_shares":[{"name":"English","percent":43},...],"borders":["DJI","ERI","KEN","SOM","SSD","SDN"],...,"signature":{"seed":"0000000000000001","data":"ba0f78d3ad63fb46"}}
```

Seeds are strings because JavaScript numbers cannot hold every 64-bit value. `to_json(writer)` writes into an existing `dasmig::core::json_writer`. `find_country()` results have seed 0.

## Compiled-in Data

Define `DASMIG_COUNTRY_EMBED` before including the header to compile the `lite` tier's countries into the program (about 70 KB of source). `load(dataset::lite)` and `instance()` then use it and need no files, which is what a WebAssembly build wants. The compiled-in data has the same `data_hash()` as the file, so seeds replay the same either way.

Define `DASMIG_COUNTRY_EMBED_LOCALE` as well to compile in the `lite` locale tables (about 1.9 MB of source). They have their own macro because most programs that embed the countries use none of them; with `DASMIG_COUNTRY_EMBED` alone, the locale tables are read from disk if they are there, and the program is the same as before they existed.

```cpp
#define DASMIG_COUNTRY_EMBED
#define DASMIG_COUNTRY_EMBED_LOCALE   // optional: locales, zones, currencies and holidays
#include <dasmig/countrygen.hpp>
```

`dasmig/countrygen_data.hpp` and `dasmig/countrygen_locale_data.hpp` are generated from `resources/lite` by `make embed`; CI checks they are up to date.

## Migrating to Request-First Generation

Generation now uses exactly `get_country(request)` and
`get_country(request, seed)`, where `request` is a `country_request`.
The former no-argument, seed-only and region-taking overloads and generator-level
`weighted()`, `seed()` and `unseed()` controls have been removed.

| Former call | Request-first replacement |
|-------------|---------------------------|
| `get_country()` | `get_country({})` |
| `get_country(seed)` | `get_country({}, seed)` |
| `get_country("Europe")` | `get_country({.region = "Europe"})` |
| `get_country("Europe", seed)` | `get_country({.region = "Europe"}, seed)` |
| `weighted(false)` followed by generation | Set `.weighted = false` on each applicable request |
| `seed()` / `unseed()` on a generator | Pass explicit per-call seeds for repeatable output; omit the seed for normal random generation |

The request defaults remain all loaded countries with population weighting.
Matching requests, seeds and data preserve the established deterministic
results, including zero-population minimum weights, region validation, JSON
and signatures. Data loading, extensions and `find_country()` are unchanged.

## Migrating from v1

Version 2 moves the library onto [dasmig-core](https://github.com/dasmig/dasmig-core).

- **Every seeded result changes.** Selection now uses the core's engine and weighted choice, so a seed gives the same country on every compiler, standard library and CPU, which the `std::` distributions could not promise. Seeds saved with v1 do not reproduce their v1 results. The selection probabilities are unchanged (checked against v1 over 20,000 draws per scenario).
- **`load(path)` replaces the data.** In v1 each call added to what was loaded. Use `extend()` to add data on top.
- **Malformed data is an error.** v1 skipped lines with too few fields and read non-numeric numbers as 0. v2 throws `dasmig::core::tsv_error` with the file, line and column and loads nothing. A path that does not exist is still ignored by `load(path)`.
- **Files:** replace `random.hpp` with `core.hpp` and the `core/` directory (see Installation).
- **New:** `extend()`, `extend_from_string()`, `layers()`, `data_hash()`, `country::signature()`, `country::to_json()`, `DASMIG_COUNTRY_EMBED`.

## Thread Safety

Each `cntg` instance is independent. The static `instance()` singleton uses a
local static for safe initialization.

| Operation | Thread-safe? |
|-----------|-------------|
| `instance()` | Yes (static local) |
| `get_country(request)` on **different** instances | Yes |
| `get_country(request)` on the **same** instance | **No** — requires external synchronization |
| `get_country(request, seed)` | Yes for concurrent reads, provided no data mutation occurs |
| `get_locale(request, seed)`, `get_time_zone(request, seed)` | Yes, as `get_country(request, seed)` |
| Lookups: `find_*`, `locales`, `time_zones`, `holidays`, `is_holiday`, `currency_digits` | Yes, as `get_country(request, seed)` |
| `load()`, `extend()` | **No** — must not be called concurrently with generation on the same instance |

Call `load()` once during initialization before spawning threads. For
concurrent generation, give each thread its own `cntg` instance.

## Batch Generation

Every seeded getter has batch forms: `get_country_batch`, `get_locale_batch` and `get_time_zone_batch` return a `std::vector`, and `for_each_country`, `for_each_locale` and `for_each_time_zone` stream the items to a sink in index order without holding the batch.

```cpp
auto& gen = dasmig::cntg::instance();
const auto request = dasmig::country_request{};

// 1,000 results from one batch seed, on four threads.
auto items = gen.get_country_batch(request, {.count = 1000, .threads = 4}, 42);

// Item 1 replays on its own: the batch seed and the index give its seed.
auto again = gen.get_country(request, dasmig::core::item_seed(42, 1));

// A million results to a file as JSON Lines, never all in memory.
std::ofstream out{"items.jsonl"};
dasmig::core::jsonl_writer lines{out};
gen.for_each_country(request, {.count = 1'000'000, .threads = 0}, 42, lines);
```

- **Item seeds.** Item `i` is exactly `get_country(request, core::item_seed(batch_seed, first + i))`. A range can be generated directly (`first = 1000` starts at item 1000), two batches that follow each other equal one, and the output is the same for any `threads` (0 means the hardware count). Without a `batch_seed` one is drawn like the single getter draws its own.
- **Options.** `core::batch_options` has `count`, `first`, `threads` and `unique`. The request describes one item; the options describe the batch.
- **Uniqueness.** `.unique = core::unique_rule::by<country>(key)` regenerates an item whose key already appeared in the batch, with a deterministic retry seed, and throws `std::runtime_error` naming the item when the attempts run out.
- **Sinks.** `core::jsonl_writer` writes one `to_json()` per line, `core::csv_writer` flattens each result's JSON into dotted columns. Any callable `(std::uint64_t index, country&)` works.
- Batch calls use the same const getters from several threads, which the thread-safety rules above allow; the CI runs the batch tests under ThreadSanitizer.

## Error Reference

| Exception | Condition |
|-----------|-----------|
| `std::runtime_error` | Either `get_country` overload or `find_country()` called with no data loaded |
| `std::invalid_argument` | Either `get_country` overload with an empty or unknown request region |
| `std::invalid_argument` | `find_country(cca2)` with unknown CCA2 code |
| `std::invalid_argument` | `get_language(cca2, seed)` with an unknown code, or a country with no language data |
| `std::runtime_error` | A locale, zone, holiday or currency getter when its table is not loaded ("No locale data loaded. Load a tier with the locale tables, or define DASMIG_COUNTRY_EMBED_LOCALE.") |
| `std::invalid_argument` | An unknown country, locale tag, zone name or currency code; a language no country (or not the requested one) has a locale of; a country with no holiday data; an invalid date |
| `std::out_of_range` | `holidays` or `is_holiday` for a year outside `holiday_years(country)`; a zone's reference date or instant before its data (2000 in `lite`, 1970 in `full`) |
| `dasmig::core::tsv_error` | A malformed table or layer (message gives file, line and column); the data is unchanged |
