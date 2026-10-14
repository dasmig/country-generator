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
dasmig::cntg::instance().load("/opt/data/countries.tsv");
```

Each `load()` replaces the data loaded before (use `extend()` to add on top; see Extending Data).
`load(dataset)` returns `bool` (`true` if a matching file was found and loaded).
`load(path)` returns `void` and does nothing if the path is not a file; a malformed file throws `dasmig::core::tsv_error` with its file, line and column.

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
auto c = gen.get_country({});
std::cout << c.name_common << " (" << c.cca2 << ")\n";
std::cout << "Capital: " << c.capital << "\n";
std::cout << "Population: " << c.population << "\n";
std::cout << "Currency: " << c.currency_name << " ("
          << c.currency_symbol << ")\n";
std::cout << "Driving side: " << c.driving_side << "\n";
```

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
 "borders":["DJI","ERI","KEN","SOM","SSD","SDN"],...,"signature":{"seed":"0000000000000001","data":"25ba572e6c3d6838"}}
```

Seeds are strings because JavaScript numbers cannot hold every 64-bit value. `to_json(writer)` writes into an existing `dasmig::core::json_writer`. `find_country()` results have seed 0.

## Compiled-in Data

Define `DASMIG_COUNTRY_EMBED` before including the header to compile the `lite` tier into the program (about 60 KB of source). `load(dataset::lite)` and `instance()` then use it and need no files, which is what a WebAssembly build wants. The compiled-in data has the same `data_hash()` as the file, so seeds replay the same either way.

```cpp
#define DASMIG_COUNTRY_EMBED
#include <dasmig/countrygen.hpp>
```

`dasmig/countrygen_data.hpp` is generated from `resources/lite` by `make embed`; CI checks it is up to date.

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
| `load()`, `extend()` | **No** — must not be called concurrently with generation on the same instance |

Call `load()` once during initialization before spawning threads. For
concurrent generation, give each thread its own `cntg` instance.

## Error Reference

| Exception | Condition |
|-----------|-----------|
| `std::runtime_error` | Either `get_country` overload or `find_country()` called with no data loaded |
| `std::invalid_argument` | Either `get_country` overload with an empty or unknown request region |
| `std::invalid_argument` | `find_country(cca2)` with unknown CCA2 code |
| `dasmig::core::tsv_error` | A malformed `countries.tsv` or layer (message gives file, line and column); the data is unchanged |
