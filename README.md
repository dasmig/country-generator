# Country Generator for C++

> **Requires C++23** (e.g., `-std=c++23` for GCC/Clang, `/std:c++latest` for MSVC).

[![Country Generator for C++](https://raw.githubusercontent.com/dasmig/country-generator/master/doc/country-generator.png)](https://github.com/dasmig/country-generator/releases)

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/dasmig/country-generator/master/LICENSE.MIT)
[![CI](https://github.com/dasmig/country-generator/actions/workflows/ci.yml/badge.svg)](https://github.com/dasmig/country-generator/actions/workflows/ci.yml)
[![GitHub Releases](https://img.shields.io/github/release/dasmig/country-generator.svg)](https://github.com/dasmig/country-generator/releases)
[![GitHub Issues](https://img.shields.io/github/issues/dasmig/country-generator.svg)](https://github.com/dasmig/country-generator/issues)
[![C++23](https://img.shields.io/badge/standard-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Header-only](https://img.shields.io/badge/type-header--only-green.svg)](https://github.com/dasmig/country-generator#integration)
[![Platform](https://img.shields.io/badge/platform-linux%20|%20windows%20|%20macos-lightgrey.svg)](https://github.com/dasmig/country-generator)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue.svg)](https://dasmig.github.io/country-generator/)

**[API Reference](https://dasmig.github.io/country-generator/)** · **[Usage Guide](doc/usage.md)** · **[Data and Method](doc/data.md)** · **[Releases](https://github.com/dasmig/country-generator/releases)**

## Features

- **Population-Weighted Generation**. Countries are selected with probability proportional to their population, mirroring real-world demographics.

- **Rich Country Data**. Every generated country includes 31 fields: names, ISO codes, capital, region, coordinates, area, population, languages, currency, borders, timezones, driving side, demonyms, income level, and more.

- **Region Filtering**. Generate countries from a specific UN geoscheme region with `get_country("Europe")`.

- **Open Data Sources**. Ships with two dataset tiers — *full* (~250 countries and territories) and *lite* (~195 sovereign states) — aggregated from mledoze/countries (ODbL), REST Countries, and World Bank.

- **Deterministic Seeding**. Per-call `get_country(seed)` for reproducible results, generator-level `seed()` / `unseed()` for deterministic sequences, and `country::seed()` for replaying a previous generation.

- **Per-Call Selection**. Use `country_request` with `get_country(request, seed)` to choose a region and population-weighted or uniform selection without changing generator settings. Population-weighted selection remains the default; `weighted(false)` is still supported for the simple overloads.

- **Identical Everywhere**. A seed gives the same country on every compiler, standard library and CPU (GCC, Clang, MSVC, Emscripten; x86-64 and ARM64), checked by known-answer tests.

- **Extensible Data**. Load your own TSV layers on top of the shipped data to add, change or remove countries (`extend()`), checked before they are applied.

- **JSON and Replay**. `to_json()` on every result, and a `signature()` recording the seed and the data it came from.

- **Compiled-in Data**. Define `DASMIG_COUNTRY_EMBED` to build the `lite` tier into the program, for WebAssembly and other builds without files.

- **Multi-Instance Support**. Construct independent `cntg` instances with their own data and random engine — ideal for embedding inside other generators.

- **CCA2 Lookup**. Look up a specific country by its ISO 3166-1 alpha-2 code with `find_country("US")`.

## Integration

[`countrygen.hpp`](https://github.com/dasmig/country-generator/blob/master/dasmig/countrygen.hpp) is the library [released here](https://github.com/dasmig/country-generator/releases). Keep [`core.hpp`](https://github.com/dasmig/country-generator/blob/master/dasmig/core.hpp) and the [`core/`](https://github.com/dasmig/country-generator/tree/master/dasmig/core) directory ([dasmig-core](https://github.com/dasmig/dasmig-core)) in the same directory. Add

```cpp
#include <dasmig/countrygen.hpp>

// For convenience.
using cntg = dasmig::cntg;
```

to the files you want to generate countries and set the necessary switches to enable C++23 (e.g., `-std=c++23` for GCC and Clang).

Additionally you must supply the country generator with the [`resources`](https://github.com/dasmig/country-generator/tree/master/resources) folder containing `full/countries.tsv` and/or `lite/countries.tsv`, also available in the [release](https://github.com/dasmig/country-generator/releases).

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
auto country = cntg::instance().get_country();
std::cout << country.name_common << ", " << country.cca2
          << " (pop. " << country.population << ")" << '\n';

// Generate a country from a specific region.
auto eu = cntg::instance().get_country("Europe");

// Access all available fields.
std::cout << country.capital << '\n';
std::cout << country.languages << '\n';
std::cout << country.currency_name << " (" << country.currency_symbol << ")"
          << '\n';
std::cout << country.driving_side << '\n';
std::cout << country.income_level << '\n';

// Deterministic generation — same seed always produces the same country.
auto seeded = cntg::instance().get_country(42);

// Replay a previous country using its seed.
auto replay = cntg::instance().get_country(seeded.seed());

// Per-call options: uniform selection within Europe, without changing settings.
dasmig::country_request request{.region = "Europe", .weighted = false};
auto requested = cntg::instance().get_country(request, 42);
auto request_replay = cntg::instance().get_country(request, requested.seed());

// Seed the engine for a deterministic sequence.
cntg::instance().seed(100);
// ... generate countries ...
cntg::instance().unseed(); // restore non-deterministic state

// Compatibility API: change the default for the simple overloads.
cntg::instance().weighted(false);
auto uniform = cntg::instance().get_country();
cntg::instance().weighted(true);

// Independent instance — separate data and random engine.
cntg my_gen;
my_gen.load("path/to/countries.tsv");
auto c = my_gen.get_country();
```

`country_request{}` selects from all loaded countries with population weighting,
even after `weighted(false)`. Request-based calls use an explicit seed and do
not advance the generator's engine or affect subsequent calls. Replay requires
the same request, seed and data; the result and JSON format are unchanged.

For the complete feature guide — fields, filtering, weighting, and more — see the **[Usage Guide](doc/usage.md)**.

## Build and Test

The library is header-only. To build the example and run the tests, use GNU Make with a POSIX shell and a C++23-capable compiler and standard library. The Makefile defaults to `clang++`; CI uses LLVM 18 with libstdc++ 14.

Run these commands from the repository root:

```bash
make all        # build the example in build/example
./build/example # run the example
make test       # build and run the tests
make test-embed # build and run the tests with the lite tier compiled in
```

For an LLVM coverage report, run `make coverage` with `llvm-profdata` and `llvm-cov` on your `PATH`. The HTML report is written to `coverage/index.html`.

## Data

The country data is aggregated from multiple open-data sources:

| Source | License | Contribution |
|--------|---------|--------------|
| [mledoze/countries](https://github.com/mledoze/countries) | ODbL v1.0 | Names, ISO codes, borders, demonyms, languages, currencies |
| [REST Countries](https://restcountries.com/) | Open Source | Population, timezones, driving side, continents |
| [World Bank](https://data.worldbank.org/) | CC BY 4.0 | Income level classification |

See `LICENSE_DATA.txt` for details.

To regenerate datasets:

```bash
python scripts/prepare_countries.py            # generate both tiers
python scripts/prepare_countries.py --tier lite # lite only
python scripts/prepare_countries.py --tier full # full only
```

## Related Libraries

| Library | Description |
|---------|-------------|
| [name-generator](https://github.com/dasmig/name-generator) | Culturally appropriate full names |
| [nickname-generator](https://github.com/dasmig/nickname-generator) | Gamer-style nicknames |
| [birth-generator](https://github.com/dasmig/birth-generator) | Demographically plausible birthdays |
| [biodata-generator](https://github.com/dasmig/biodata-generator) | Procedural human physical characteristics |
| [city-generator](https://github.com/dasmig/city-generator) | Weighted city selection by population |
| [entity-generator](https://github.com/dasmig/entity-generator) | ECS-based entity generation |
