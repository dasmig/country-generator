# Usage Guide

This guide covers every feature of the country-generator library in detail. For a quick overview, see the [README](../README.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating-countries">Generating Countries</a></li>
<li><a href="#country-fields">Country Fields</a></li>
<li><a href="#region-filtering">Region Filtering</a></li>
<li><a href="#population-weighting">Population Weighting</a></li>
<li><a href="#uniform-selection">Uniform Selection</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
<li><a href="#multi-instance-support">Multi-Instance Support</a></li>
<li><a href="#data-pipeline">Data Pipeline</a></li>
<li><a href="#thread-safety">Thread Safety</a></li>
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
    auto c = gen.get_country();
    std::cout << c.name_common << ", " << c.cca2 << "\n";

    // Country from a specific region.
    auto eu = gen.get_country("Europe");
    std::cout << eu.name_common << " (pop. " << eu.population << ")\n";
}
```

## Installation

1. Copy `dasmig/countrygen.hpp` and `dasmig/random.hpp` into your include path.
2. Copy the `resources/` folder (containing `full/` and/or `lite/` subdirectories) so it is accessible at runtime.
3. Compile with C++23 enabled: `-std=c++23`.

## Loading Resources

The library ships two dataset tiers:

| Tier | Enum | Countries | Description |
|------|------|-----------|-------------|
| lite | `dasmig::dataset::lite` | ~195 | Sovereign states only |
| full | `dasmig::dataset::full` | ~250 | All countries and territories |

Resources are stored as `resources/lite/countries.tsv` and `resources/full/countries.tsv`.

### Automatic loading (singleton)

On first access the singleton constructor probes these base paths:

| Priority | Base path |
|----------|-----------|
| 1 | `resources/` |
| 2 | `../resources/` |
| 3 | `country-generator/resources/` |

It loads `lite/countries.tsv` if found, otherwise falls back to `full/countries.tsv`.

### Explicit tier loading

```cpp
dasmig::cntg gen;
gen.load(dasmig::dataset::lite);  // ~195 sovereign states
gen.load(dasmig::dataset::full);  // ~250 countries (can combine)
```

If your resources are elsewhere, call `load()` with a direct path:

```cpp
dasmig::cntg::instance().load("/opt/data/countries.tsv");
```

Calling `load()` multiple times is safe — each call adds to the existing data.
`load(dataset)` returns `bool` (`true` if a matching file was found and loaded).
`load(path)` returns `void` and silently does nothing if the path does not exist.

## Generating Countries

### Random (population-weighted)

```cpp
auto c = dasmig::cntg::instance().get_country();
std::string text = c;  // implicit conversion — returns the common name
```

### Region-filtered

```cpp
auto c = dasmig::cntg::instance().get_country("Asia");
// c.region == "Asia"
```

## Country Fields

Every generated country includes all 31 data fields:

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
auto c = gen.get_country();
std::cout << c.name_common << " (" << c.cca2 << ")\n";
std::cout << "Capital: " << c.capital << "\n";
std::cout << "Population: " << c.population << "\n";
std::cout << "Currency: " << c.currency_name << " ("
          << c.currency_symbol << ")\n";
std::cout << "Driving side: " << c.driving_side << "\n";
```

## Region Filtering

Pass a UN geoscheme region to restrict generation:

```cpp
auto eu = gen.get_country("Europe");
auto af = gen.get_country("Africa");
```

Available regions: `Africa`, `Americas`, `Antarctic`, `Asia`, `Europe`, `Oceania`.

Region-filtered selection is also population-weighted within the region.
Throws `std::invalid_argument` if no countries match the region.

## Population Weighting

By default, countries are selected with probability proportional to their population.
China and India are far more likely to appear than Liechtenstein. Countries
with zero population are assigned a minimum weight of 1.

## Uniform Selection

Switch to equal-probability selection where every loaded country has the
same chance of being picked, regardless of population:

```cpp
auto& gen = dasmig::cntg::instance();

gen.weighted(false);             // uniform random
auto c = gen.get_country();      // any country equally likely
auto e = gen.get_country("Europe"); // uniform within Europe

gen.weighted(true);              // restore population-weighted (default)
```

The `weighted()` setter returns `*this` for chaining:

```cpp
gen.weighted(false).seed(42);  // uniform + deterministic
```

Query the current mode with:

```cpp
bool is_weighted = gen.weighted(); // true by default
```

## Seeding and Deterministic Generation

### Per-call seeding

Pass an explicit seed to `get_country()` to produce the same country every time:

```cpp
auto c = dasmig::cntg::instance().get_country(42);
// Always produces the same country for the same data + seed.
```

### Seed replay

Every country records its seed. Retrieve it with `country::seed()` and pass
it back to reproduce the exact same result:

```cpp
auto c = dasmig::cntg::instance().get_country();
auto saved_seed = c.seed();

// Later, replay:
auto replay = dasmig::cntg::instance().get_country(saved_seed);
// replay.cca3 == c.cca3
```

### Sequence seeding

Seed the generator's engine for a reproducible sequence:

```cpp
auto& gen = dasmig::cntg::instance();

gen.seed(100);
auto a = gen.get_country();
auto b = gen.get_country();

gen.seed(100);                   // reset
auto a2 = gen.get_country();     // identical to a
auto b2 = gen.get_country();     // identical to b

gen.unseed(); // restore non-deterministic behavior
```

### Region filter with seed

```cpp
auto c = gen.get_country("Americas", 42);
// Deterministic within American countries.
```

> **Note:** To replay a region-filtered country, pass both the region
> and the seed: `gen.get_country("Americas", c.seed())`. Using just `c.seed()`
> without the region filter will select from the full dataset and likely
> produce a different country.

## Multi-Instance Support

Construct independent generators with their own data and engine:

```cpp
dasmig::cntg my_gen;
my_gen.load("path/to/countries.tsv");
auto c = my_gen.get_country();
```

## Data Pipeline

The shipped datasets are generated from open data sources using the included Python script:

```bash
python scripts/prepare_countries.py            # generate both tiers
python scripts/prepare_countries.py --tier full # full only (~250 countries)
python scripts/prepare_countries.py --tier lite # lite only (~195 sovereign states)
```

This produces `resources/full/countries.tsv` and `resources/lite/countries.tsv`.

Data sources are licensed under ODbL, CC BY 4.0, and open source licenses.
See `LICENSE_DATA.txt` for details.

## Thread Safety

Each `cntg` instance is independent. The static `instance()` singleton uses a
local static for safe initialization.

| Operation | Thread-safe? |
|-----------|-------------|
| `instance()` | Yes (static local) |
| `get_country()` on **different** instances | Yes |
| `get_country()` on the **same** instance | **No** — requires external synchronization |
| `load()` | **No** — must not be called concurrently with `get_country()` on the same instance |

Call `load()` once during initialization before spawning threads. For
concurrent generation, give each thread its own `cntg` instance.

## Error Reference

| Exception | Condition |
|-----------|-----------|
| `std::runtime_error` | `get_country()` called with no data loaded |
| `std::invalid_argument` | `get_country(region)` with unknown region |
