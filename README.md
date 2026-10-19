# Country Generator for C++

> **Requires C++23** (e.g., `-std=c++23` for GCC/Clang, `/std:c++latest` for MSVC).

[![Country Generator for C++](https://raw.githubusercontent.com/dasmig/country-generator/master/doc/country-generator.png)](https://github.com/dasmig/country-generator/releases)

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/dasmig/country-generator/master/LICENSE.MIT)
[![CI](https://github.com/dasmig/country-generator/actions/workflows/ci.yml/badge.svg)](https://github.com/dasmig/country-generator/actions/workflows/ci.yml)
[![GitHub Releases](https://img.shields.io/github/release/dasmig/country-generator.svg)](https://github.com/dasmig/country-generator/releases)
[![C++23](https://img.shields.io/badge/standard-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Header-only](https://img.shields.io/badge/type-header--only-green.svg)](https://github.com/dasmig/country-generator#integration)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue.svg)](https://dasmig.github.io/country-generator/)

**[API Reference](https://dasmig.github.io/country-generator/)** · **[Usage Guide](doc/usage.md)** · **[Data and Method](doc/data.md)** · **[Releases](https://github.com/dasmig/country-generator/releases)**

Countries drawn by population, with names, codes, capital, region, languages, currency and other fields.

## Features

- **Population-Weighted Generation**. Countries are selected with probability proportional to their population, mirroring real-world demographics.

- **Rich Country Data**. Every generated country includes 31 fields: names, ISO codes, capital, region, coordinates, area, population, languages, currency, borders, timezones, driving side, demonyms, income level, and more.

- **Region Filtering**. Generate countries from a specific UN geoscheme region with `get_country({.region = "Europe"})`.

- **Open Data Sources**. Ships with two dataset tiers — *full* (~250 countries and territories) and *lite* (~195 sovereign states) — aggregated from mledoze/countries (ODbL), REST Countries, and World Bank.

- **Per-Call Selection**. Use `country_request` with `get_country(request)` or `get_country(request, seed)` to choose a region and population-weighted or uniform selection. Population weighting remains the request default; no mutable generator selection settings are needed.

- **CCA2 Lookup**. Look up a specific country by its ISO 3166-1 alpha-2 code with `find_country("US")`.

- **Deterministic Seeding**. Per-call `get_country(request, seed)` for reproducible results, and `country::seed()` for replaying a previous generation with the same request.

- **Batch Generation**. Generate thousands of results from one seed, on several threads with the same output for any thread count, optionally unique by a key, and stream them as JSON Lines or CSV (see the usage guide).

- **Identical Everywhere**. A seed gives the same country on every compiler, standard library and CPU (GCC, Clang, MSVC, Emscripten; x86-64 and ARM64), checked by known-answer tests.

- **Extensible Data**. Load your own TSV layers on top of the shipped data to add, change or remove countries (`extend()`), checked before they are applied.

- **JSON and Replay**. `to_json()` on every result, and a `signature()` recording the seed and the data it came from.

- **Compiled-in Data**. Define `DASMIG_COUNTRY_EMBED` to build the `lite` tier into the program, for WebAssembly and other builds without files.

- **Multi-Instance Support**. Construct independent `cntg` instances with their own data and random engine — ideal for embedding inside other generators.

## Integration

[`countrygen.hpp`](https://github.com/dasmig/country-generator/blob/master/dasmig/countrygen.hpp) is the library [released here](https://github.com/dasmig/country-generator/releases). Keep [`core.hpp`](https://github.com/dasmig/country-generator/blob/master/dasmig/core.hpp) and the [`core/`](https://github.com/dasmig/country-generator/tree/master/dasmig/core) directory ([dasmig-core](https://github.com/dasmig/dasmig-core)) in the same directory. Add

```cpp
#include <dasmig/countrygen.hpp>
```

to the files you want to use it in, and supply the [`resources`](https://github.com/dasmig/country-generator/tree/master/resources) folder (or define `DASMIG_COUNTRY_EMBED`).

## Usage

```cpp
#include <dasmig/countrygen.hpp>
#include <iostream>

// For convenience.
using cntg = dasmig::cntg;

// Manually load a specific dataset tier if necessary.
cntg::instance().load(dasmig::dataset::lite);  // ~195 sovereign states
// OR
cntg::instance().load(dasmig::dataset::full);  // ~250 countries & territories

// Generate a random country (population-weighted).
auto country = cntg::instance().get_country({});
std::cout << country.name_common << ", " << country.cca2
          << " (pop. " << country.population << ")" << '\n';

// Generate a country from a specific region.
auto eu = cntg::instance().get_country({.region = "Europe"});

// Access all available fields.
std::cout << country.capital << '\n';
std::cout << country.languages << '\n';
std::cout << country.currency_name << " (" << country.currency_symbol << ")"
          << '\n';
std::cout << country.driving_side << '\n';
std::cout << country.income_level << '\n';

// Deterministic generation — same seed always produces the same country.
auto seeded = cntg::instance().get_country({}, 42);

// Replay a previous country using its seed.
auto replay = cntg::instance().get_country({}, seeded.seed());

// Per-call options: uniform selection within Europe.
dasmig::country_request request{.region = "Europe", .weighted = false};
auto random_europe = cntg::instance().get_country(request);
auto requested = cntg::instance().get_country(request, 42);
auto request_replay = cntg::instance().get_country(request, requested.seed());

// Uniform random selection without changing later calls.
auto uniform = cntg::instance().get_country({.weighted = false});

// Independent instance — separate data and random engine.
cntg my_gen;
my_gen.load("path/to/countries.tsv");
auto c = my_gen.get_country({});
```

`country_request{}` selects from all loaded countries with population weighting.
`get_country(request)` draws a seed from the generator's internal engine;
`get_country(request, seed)` does not advance that engine. Replay requires the
same request, seed and data; the result and JSON format are unchanged.

The request-first API replaces the old no-argument, seed-only and region-taking
overloads, and removes generator-level `weighted()`, `seed()` and `unseed()`
controls. Pass `{}` for default options, put filtering and weighting on the
request, and supply per-call seeds for deterministic sequences.

For the complete feature guide — fields, filtering, weighting, and more — see the **[Usage Guide](doc/usage.md)**.

## Data

The country data is aggregated from multiple open-data sources:

| Source | License | Contribution |
|--------|---------|--------------|
| [mledoze/countries](https://github.com/mledoze/countries) | ODbL v1.0 | ISO codes, names, area, flag, and fallbacks for REST Countries' gaps |
| [REST Countries](https://restcountries.com/) | MPL 2.0 | Coordinates, regions, capitals, languages, currencies, borders, demonyms, population, timezones, driving side, continents |
| [World Bank](https://data.worldbank.org/) | CC BY 4.0 | Income level classification |

Because the data is adapted from an ODbL database, the ODbL applies to it. Output generated with the library needs a notice that it uses mledoze/countries data under the ODbL. If you publish the data itself, or a database adapted from it, you must also offer that database under the ODbL (share-alike). `LICENSE_DATA.txt` gives the details. The column-by-column sources are in [`doc/data.md`](doc/data.md).

To regenerate datasets:

```bash
python scripts/prepare_countries.py            # generate both tiers
python scripts/prepare_countries.py --tier lite # lite only
python scripts/prepare_countries.py --tier full # full only
```

## Building

```bash
make            # example
make test       # tests
make bench      # benchmarks, results in build/bench.tsv
make test-embed # tests with the lite tier compiled in
make coverage   # code coverage
make lint       # clang-tidy
make docs       # API docs
make embed      # regenerate dasmig/countrygen_data.hpp from resources/lite
```

## Compiler Support

Tested with:
- Clang 18+ (`-std=c++23`)
- GCC 14+ (`-std=c++23`)
- MSVC 19.38+ (`/std:c++latest`)
- Emscripten 3.1.74

## Dependencies

| Dependency | Version | Bundled | Purpose |
|-----------|---------|---------|---------|
| [dasmig-core](https://github.com/dasmig/dasmig-core) | 1.0 | Yes (`core.hpp`, `core/`) | Platform-independent random engine and distributions, data layers, JSON |
| [Catch2](https://github.com/catchorg/Catch2) | 3.x | Yes (amalgamated) | Unit testing |

## Related Libraries

| Library | Description |
|---------|-------------|
| [address-generator](https://github.com/dasmig/address-generator) | Postal addresses with real streets by place |
| [biodata-generator](https://github.com/dasmig/biodata-generator) | Procedural human physical characteristics |
| [birth-generator](https://github.com/dasmig/birth-generator) | Demographically plausible birthdays |
| [city-generator](https://github.com/dasmig/city-generator) | Weighted city selection by population |
| [email-generator](https://github.com/dasmig/email-generator) | Email addresses in each country's style |
| [entity-generator](https://github.com/dasmig/entity-generator) | ECS-based entity generation |
| [interest-generator](https://github.com/dasmig/interest-generator) | Religion, hobbies and favourite media by country, age and sex |
| [name-generator](https://github.com/dasmig/name-generator) | Culturally appropriate full names |
| [nickname-generator](https://github.com/dasmig/nickname-generator) | Gamer-style nicknames |
| [occupation-generator](https://github.com/dasmig/occupation-generator) | Employment status, education and occupation by country, age and sex |
| [persona-generator](https://github.com/dasmig/persona-generator) | Complete characters composed from the other generators |
| [phone-generator](https://github.com/dasmig/phone-generator) | Phone numbers valid in each country's numbering plan |
| [trait-generator](https://github.com/dasmig/trait-generator) | Personality traits with conflicts, synergies and Big Five axes |
| [user-generator](https://github.com/dasmig/user-generator) | Complete user records: account, username, email, phone and address |

## License

This library is released under the [MIT License](https://opensource.org/licenses/MIT).

```
MIT License

Copyright (c) 2020-2026 Diego Dasso Migotto
```
