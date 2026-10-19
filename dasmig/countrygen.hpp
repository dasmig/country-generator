// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_COUNTRYGEN_HPP
#define DASMIG_COUNTRYGEN_HPP

#include "core.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef DASMIG_COUNTRY_EMBED
#include "countrygen_data.hpp"
#endif

/// @file countrygen.hpp
/// @brief Country generator library — procedural country generation for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

DASMIG_CORE_REQUIRE(1, 1);

namespace dasmig
{

/// @brief One language of a country and the share of its population that speaks it.
struct language_share
{
    std::string name;   ///< English name of the language (for example "Portuguese").
    double percent{0.0}; ///< Percent of the population that speaks it, 0 to 100.
};

/// @brief Return type for country generation, holding all data fields.
///
/// Supports implicit conversion to std::string (returns the common name),
/// streaming via operator<< and JSON output via to_json().
class country
{
  public:
    // -- Identification ---------------------------------------------------

    /// @brief ISO 3166-1 alpha-2 code (e.g. "BR").
    std::string cca2;

    /// @brief ISO 3166-1 alpha-3 code (e.g. "BRA").
    std::string cca3;

    /// @brief ISO 3166-1 numeric code (e.g. "076").
    std::string ccn3;

    /// @brief Common English name (e.g. "Brazil").
    std::string name_common;

    /// @brief Official English name (e.g. "Federative Republic of Brazil").
    std::string name_official;

    /// @brief Capital city (semicolon-separated if multiple).
    std::string capital;

    // -- Geography --------------------------------------------------------

    /// @brief UN geoscheme region (e.g. "Americas").
    std::string region;

    /// @brief UN geoscheme subregion (e.g. "South America").
    std::string subregion;

    /// @brief Continent (semicolon-separated if multiple).
    std::string continent;

    /// @brief WGS84 latitude in decimal degrees.
    double latitude{0.0};

    /// @brief WGS84 longitude in decimal degrees.
    double longitude{0.0};

    /// @brief Total land area in km².
    std::uint64_t area{0};

    /// @brief Total population.
    std::uint64_t population{0};

    /// @brief Whether the country is landlocked.
    bool landlocked{false};

    // -- Political --------------------------------------------------------

    /// @brief Whether the country is an independent sovereign state.
    bool independent{false};

    /// @brief Whether the country is a UN member.
    bool un_member{false};

    // -- Culture ----------------------------------------------------------

    /// @brief Semicolon-separated language names (e.g. "Portuguese").
    std::string languages;

    /// @brief Semicolon-separated `Name:percent` pairs, largest first, for the
    ///        languages at least 1% of the population speaks (for example
    ///        "Portuguese:91;English:8"); empty when the data has none.
    ///
    /// Each percent is the share of people who speak that language, so they
    /// overlap and can sum above 100. Use language_distribution() to read them.
    std::string language_shares;

    /// @brief Primary ISO 4217 currency code (e.g. "BRL").
    std::string currency_code;

    /// @brief Primary currency name (e.g. "Brazilian real").
    std::string currency_name;

    /// @brief Primary currency symbol (e.g. "R$").
    std::string currency_symbol;

    // -- Borders & time ---------------------------------------------------

    /// @brief Semicolon-separated ISO alpha-3 border codes.
    std::string borders;

    /// @brief Semicolon-separated UTC offset strings.
    std::string timezones;

    /// @brief Driving side: "right" or "left".
    std::string driving_side;

    // -- Telecom & web ----------------------------------------------------

    /// @brief Country-code top-level domain (e.g. ".br").
    std::string tld;

    /// @brief IDD root (e.g. "+5").
    std::string idd_root;

    /// @brief IDD primary suffix (e.g. "5").
    std::string idd_suffix;

    // -- Demonyms & misc --------------------------------------------------

    /// @brief English male demonym (e.g. "Brazilian").
    std::string demonym_m;

    /// @brief English female demonym (e.g. "Brazilian").
    std::string demonym_f;

    /// @brief Unicode flag emoji.
    std::string flag_emoji;

    /// @brief World Bank income level (e.g. "Upper middle income").
    std::string income_level;

    /// @brief Start of the week: "monday", "sunday", or "saturday".
    std::string start_of_week;

    // -- Methods ----------------------------------------------------------

    /// @brief The language names of `languages` as a list.
    /// @return The official languages in the data's order; empty when none.
    [[nodiscard]] std::vector<std::string> language_list() const
    {
        std::vector<std::string> out;
        std::string_view text{languages};
        while (!text.empty())
        {
            const auto pos = text.find(';');
            out.emplace_back(text.substr(0, pos));
            text = pos == std::string_view::npos ? std::string_view{} : text.substr(pos + 1);
        }
        return out;
    }

    /// @brief The languages and the share of the population that speaks each.
    /// @return `language_shares` parsed, largest first; empty when the data has none.
    [[nodiscard]] std::vector<language_share> language_distribution() const
    {
        return parse_shares_(language_shares).value_or(std::vector<language_share>{});
    }

    /// @brief The language most of the population speaks.
    /// @return The largest share's name, else the first of `languages`, else empty.
    [[nodiscard]] std::string primary_language() const
    {
        const auto shares = language_distribution();
        if (!shares.empty()) return shares.front().name;
        auto list = language_list();
        return list.empty() ? std::string{} : std::move(list.front());
    }

    /// @brief The English demonym for a person of this country.
    /// @param bio_sex The person's sex.
    /// @return `demonym_m` or `demonym_f`, falling back to the other when empty.
    [[nodiscard]] const std::string& demonym(sex bio_sex) const
    {
        const auto& wanted = bio_sex == sex::male ? demonym_m : demonym_f;
        return wanted.empty() ? (bio_sex == sex::male ? demonym_f : demonym_m) : wanted;
    }

    /// @brief Retrieve the random seed used to generate this country.
    /// @return The per-call seed for replay (0 for find_country()).
    /// @see cntg::get_country(const country_request&, std::uint64_t) const
    [[nodiscard]] std::uint64_t seed() const
    {
        return _signature.seed;
    }

    /// @brief The seed and the data this country was drawn from.
    /// @return The replay signature; `data` is cntg::data_hash() at the
    ///         time of generation.
    [[nodiscard]] const core::seed_signature& signature() const
    {
        return _signature;
    }

    /// @brief Write this country as a JSON object.
    ///
    /// Semicolon-separated fields (capital, continent, languages, borders,
    /// timezones) are written as arrays.
    ///
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        auto list = [&w](std::string_view name, std::string_view text) {
            w.key(name).begin_array();
            while (!text.empty())
            {
                const auto pos = text.find(';');
                w.value(text.substr(0, pos));
                text = pos == std::string_view::npos ? std::string_view{} : text.substr(pos + 1);
            }
            w.end_array();
        };
        w.begin_object()
            .field("cca2", cca2)
            .field("cca3", cca3)
            .field("ccn3", ccn3)
            .field("name_common", name_common)
            .field("name_official", name_official);
        list("capital", capital);
        w.field("region", region).field("subregion", subregion);
        list("continent", continent);
        w.field("latitude", latitude)
            .field("longitude", longitude)
            .field("area", area)
            .field("population", population)
            .field("landlocked", landlocked)
            .field("independent", independent)
            .field("un_member", un_member);
        list("languages", languages);
        w.key("language_shares").begin_array();
        (void)for_each_share_(language_shares, [&w](std::string_view name, double percent) {
            w.begin_object().field("name", name).field("percent", percent).end_object();
        });
        w.end_array();
        w.field("currency_code", currency_code)
            .field("currency_name", currency_name)
            .field("currency_symbol", currency_symbol);
        list("borders", borders);
        list("timezones", timezones);
        w.field("driving_side", driving_side)
            .field("tld", tld)
            .field("idd_root", idd_root)
            .field("idd_suffix", idd_suffix)
            .field("demonym_m", demonym_m)
            .field("demonym_f", demonym_f)
            .field("flag_emoji", flag_emoji)
            .field("income_level", income_level)
            .field("start_of_week", start_of_week)
            .key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This country as a JSON document.
    /// @return Compact JSON.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::string.
    /// @return The common English name.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return name_common;
    }

    /// @brief Stream the common name to an output stream.
    /// @param os Output stream.
    /// @param c Country to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const country& c)
    {
        os << c.name_common;
        return os;
    }

  private:
    core::seed_signature _signature;

    // Calls visit(name, percent) for each "Name:percent" pair of a shares text and returns true;
    // returns false at the first malformed pair (no colon, empty name, or a percent not in (0, 100]).
    template <typename Visit>
    [[nodiscard]] static bool for_each_share_(std::string_view text, Visit&& visit)
    {
        while (!text.empty())
        {
            const auto end = text.find(';');
            const auto pair = text.substr(0, end);
            const auto colon = pair.rfind(':');
            if (colon == std::string_view::npos || colon == 0) return false;
            const auto percent = core::detail::parse_number<double>(pair.substr(colon + 1));
            if (!percent || !(*percent > 0.0) || *percent > 100.0) return false;
            visit(pair.substr(0, colon), *percent);
            text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        }
        return true;
    }

    // "Name:percent;..." -> shares; nullopt when a pair is malformed.
    [[nodiscard]] static std::optional<std::vector<language_share>> parse_shares_(std::string_view text)
    {
        std::vector<language_share> out;
        if (!for_each_share_(text, [&out](std::string_view name, double percent) {
                out.push_back({.name = std::string{name}, .percent = percent});
            }))
        {
            return std::nullopt;
        }
        return out;
    }

    friend class cntg;
};

/// @brief Per-call country selection options.
struct country_request
{
    /// @brief UN geoscheme region; unset selects from all loaded countries.
    /// An empty or unknown region throws std::invalid_argument.
    std::optional<std::string> region{std::nullopt};

    /// @brief Population-weighted selection (default); false selects uniformly.
    bool weighted{true};
};

/// @brief Country generator that produces random countries from
///        aggregated open-data sources.
///
/// Generates countries using population-weighted random selection by
/// default. Larger countries are proportionally more likely to be
/// selected, mirroring real-world demographic distributions.
///
/// Output is identical on every compiler and platform (dasmig-core).
///
/// Can be used as a singleton via instance() or constructed independently.
///
/// @par Thread safety
/// Each instance is independent. Concurrent calls to get_country(request)
/// on the **same** instance require external synchronization. Calls with
/// an explicit seed only read data and may run concurrently. load() and
/// extend() must not run concurrently with either generation overload.
class cntg
{
  public:
    /// @brief Default constructor — creates an empty generator with no data.
    cntg() = default;

    cntg(const cntg&) = delete;            ///< Not copyable.
    cntg& operator=(const cntg&) = delete; ///< Not copyable.
    cntg(cntg&&) noexcept = default;       ///< Move constructor.
    cntg& operator=(cntg&&) noexcept = default; ///< Move assignment.
    ~cntg() = default;                     ///< Default destructor.

    /// @brief Access the global singleton instance.
    ///
    /// Loads the `lite` tier (or `full` if `lite` is missing) on first
    /// access, from compiled-in data or an auto-probed `resources/`.
    /// @return Reference to the global cntg instance.
    static cntg& instance()
    {
        static cntg inst{auto_probe_tag{}};
        return inst;
    }

    // -- Generation -------------------------------------------------------

    /// @brief Generate a random country with per-call selection options.
    ///
    /// Draws a call seed from the internal random engine. Replay with the
    /// same request, the returned country's seed and the same data.
    /// @param request Region filter and selection mode for this call.
    /// @return A country object with all fields.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If no countries match the requested region.
    [[nodiscard]] country get_country(const country_request& request)
    {
        return get_country(request, _engine());
    }

    /// @brief Generate a deterministic country with per-call selection options.
    ///
    /// Does not advance the internal random engine. Replay with the same
    /// request, seed and data.
    /// @param request Region filter and selection mode for this call.
    /// @param call_seed Seed for reproducible results.
    /// @return A country object with all fields.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If no countries match the requested region.
    [[nodiscard]] country get_country(const country_request& request,
                                      std::uint64_t call_seed) const
    {
        require_data_();
        if (!request.region)
        {
            auto r = core::rng{call_seed}.split("country");
            const auto idx = request.weighted
                                 ? _distribution(r)
                                 : static_cast<std::size_t>(core::bounded(r, _countries.size()));
            return result_(_countries[idx], call_seed);
        }

        const auto it = _region_index.find(*request.region);
        if (it == _region_index.end())
        {
            throw std::invalid_argument("No countries found for region: " + *request.region);
        }
        const auto& entry = it->second;
        auto r = core::rng{call_seed}.split("country");
        const auto selected = request.weighted
                                  ? entry.distribution(r)
                                  : static_cast<std::size_t>(core::bounded(r, entry.indices.size()));
        return result_(_countries[entry.indices[selected]], call_seed);
    }

    // -- Batch generation -------------------------------------------------

    /// @brief Generate a batch of country results from a seed drawn like
    ///        get_country() draws its own.
    ///
    /// Item `i` is `get_country(request, core::item_seed(batch_seed, options.first + i))`
    /// for the drawn batch seed; see the seeded overload.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<country> get_country_batch(const country_request& request,
                                                  const core::batch_options& options)
    {
        return get_country_batch(request, options, _engine());
    }

    /// @brief Generate a batch of country results, deterministically.
    ///
    /// Item `i` is exactly `get_country(request, core::item_seed(batch_seed,
    /// options.first + i))`, so an item replays on its own, a range can be
    /// generated directly, two batches that follow each other equal one, and
    /// the output does not depend on `options.threads`.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness (see
    ///        core::unique_rule; the rule must be made for `country`).
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    /// @throws std::invalid_argument If the request or options are invalid.
    /// @throws std::runtime_error If no data has been loaded, or a unique
    ///         batch runs out of attempts.
    [[nodiscard]] std::vector<country> get_country_batch(const country_request& request,
                                                  const core::batch_options& options,
                                                  std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed, [&](std::uint64_t item_seed)
                               { return get_country(request, item_seed); });
    }

    /// @brief Stream a batch of country results to a sink without holding the
    ///        whole batch.
    ///
    /// Generates the same items as get_country_batch() and calls
    /// `sink(index, item)` in index order on the calling thread; `index`
    /// includes `options.first`.
    ///
    /// @tparam Sink Callable taking `(std::uint64_t, country&)`; for example
    ///         core::jsonl_writer or core::csv_writer.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_country(const country_request& request, const core::batch_options& options,
                        std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_country(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of country results with a batch seed drawn like
    ///        get_country() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, country&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_country(const country_request& request, const core::batch_options& options,
                        Sink&& sink)
    {
        for_each_country(request, options, _engine(), std::forward<Sink>(sink));
    }


    /// @brief Look up a country by its ISO 3166-1 alpha-2 code.
    /// @param cca2 Two-letter country code (e.g. "US", "BR").
    /// @return The matching country object (seed 0).
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If no country matches the code.
    [[nodiscard]] country find_country(const std::string& cca2) const
    {
        require_data_();
        const auto it = _cca2_index.find(cca2);
        if (it == _cca2_index.end())
        {
            throw std::invalid_argument("No country found for CCA2 code: " + cca2);
        }
        return result_(_countries[it->second], 0);
    }

    /// @brief Draw a language a person of a country speaks, in proportion to
    ///        the share of the population that speaks it.
    ///
    /// Falls back to the first of the country's `languages` when the data has
    /// no shares for it. A pure function of the data, the country and the seed.
    /// @param cca2 ISO 3166-1 alpha-2 code.
    /// @param call_seed The call seed.
    /// @return The language's English name.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If no country matches the code, or the
    ///         country has no language data.
    [[nodiscard]] std::string get_language(const std::string& cca2, std::uint64_t call_seed) const
    {
        const auto c = find_country(cca2);
        const auto shares = c.language_distribution();
        if (shares.empty())
        {
            auto list = c.language_list();
            if (list.empty()) throw std::invalid_argument("No language data for CCA2 code: " + cca2);
            return std::move(list.front());
        }
        std::vector<double> weights;
        weights.reserve(shares.size());
        for (const auto& share : shares) weights.push_back(share.percent);
        auto r = core::rng{call_seed}.split("language");
        return shares[core::discrete{weights}(r)].name;
    }

    // -- Data management --------------------------------------------------

    /// @brief Check whether any data has been loaded.
    /// @return True when at least one country is loaded.
    [[nodiscard]] bool has_data() const
    {
        return !_countries.empty();
    }

    /// @brief Return the number of loaded countries.
    /// @return The count.
    [[nodiscard]] std::size_t country_count() const
    {
        return _countries.size();
    }

    /// @brief Load country data from a TSV file, replacing all data and
    ///        layers loaded before.
    ///
    /// Expects the `countries.tsv` columns (see doc/data.md). A path that
    /// is not a regular file is ignored.
    ///
    /// @param tsv_path Path to the TSV file.
    /// @throws core::tsv_error If the file is malformed; the data is then
    ///         unchanged.
    void load(const std::filesystem::path& tsv_path)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(tsv_path, ec)) return;
        (void)extend(core::tsv_document::load(tsv_path), core::layer_mode::replace);
    }

    /// @brief Load a dataset tier, replacing all data and layers loaded
    ///        before.
    ///
    /// Uses the compiled-in copy when the library is built with
    /// `DASMIG_COUNTRY_EMBED`, otherwise searches `resources/`,
    /// `../resources`, `country-generator/resources` and
    /// `../country-generator/resources`.
    ///
    /// @param tier The dataset size to load.
    /// @return `true` if the tier was found and loaded.
    /// @throws core::tsv_error If the file is malformed.
    [[nodiscard]] bool load(dataset tier)
    {
        const std::string path = std::string{core::tier_name(tier)} + "/" + std::string{data_file_};
        auto doc = core::load_resource(path, embedded_files_(),
                                       core::find_resources(library_name_, path));
        if (!doc) return false;
        (void)extend(std::move(*doc), core::layer_mode::replace);
        return true;
    }

    /// @brief Add a data layer from a TSV file with the `countries.tsv`
    ///        columns.
    ///
    /// Rows are keyed by `cca2`. In the default override mode a row
    /// replaces the loaded country with the same code and new codes are
    /// added; a row with `disabled` set to 1 removes its country.
    ///
    /// @param file The layer file.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If the file cannot be read or a row is
    ///         invalid; the data is then unchanged.
    const core::layer_info& extend(const std::filesystem::path& file,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        return extend(core::tsv_document::load(file), mode);
    }

    /// @brief Add a data layer from TSV text.
    /// @param text TSV with the `countries.tsv` columns.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend_from_string(std::string_view text, std::string source,
                                               core::layer_mode mode = core::layer_mode::override)
    {
        return extend(core::tsv_document::parse(text, std::move(source)), mode);
    }

    /// @brief Add a data layer.
    /// @param doc TSV with the `countries.tsv` columns.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend(core::tsv_document doc,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        const auto& info = _table.extend(std::move(doc), mode, [](const core::tsv_row& row) {
            (void)parse_(row);
        });
        rebuild_();
        return info;
    }

    /// @brief The data layers in effect, in order.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& layers() const
    {
        return _table.layers();
    }

    /// @brief Hash of the loaded data, recorded in each result's
    ///        signature().
    /// @return The hash; 0 when nothing is loaded.
    [[nodiscard]] std::uint64_t data_hash() const { return _table.data_hash(); }

  private:
    static constexpr std::string_view library_name_ = "country-generator";
    static constexpr std::string_view data_file_ = "countries.tsv";

    static core::table_schema schema_()
    {
        return {.key = {"cca2"},
                .required = {"cca3", "name_common", "region", "latitude", "longitude",
                             "area", "population", "landlocked", "independent", "un_member"},
                .optional = {"ccn3", "name_official", "capital", "subregion", "continent",
                             "languages", "language_shares", "currency_code", "currency_name",
                             "currency_symbol", "borders", "timezones", "driving_side", "tld", "idd_root",
                             "idd_suffix", "demonym_m", "demonym_f", "flag_emoji",
                             "income_level", "start_of_week"}};
    }

    // Parse and check a row; throws core::tsv_error located at the row.
    static country parse_(const core::tsv_row& row)
    {
        country c;
        c.cca2 = row.get<std::string>("cca2");
        c.cca3 = row.get<std::string>("cca3");
        c.ccn3 = row.get_or<std::string>("ccn3", "");
        c.name_common = row.get<std::string>("name_common");
        c.name_official = row.get_or<std::string>("name_official", "");
        c.capital = row.get_or<std::string>("capital", "");
        c.region = row.get<std::string>("region");
        c.subregion = row.get_or<std::string>("subregion", "");
        c.continent = row.get_or<std::string>("continent", "");
        c.latitude = row.get<double>("latitude");
        c.longitude = row.get<double>("longitude");
        if (c.latitude < -90.0 || c.latitude > 90.0)
        {
            throw row.error("latitude", "must be between -90 and 90");
        }
        if (c.longitude < -180.0 || c.longitude > 180.0)
        {
            throw row.error("longitude", "must be between -180 and 180");
        }
        // The source marks an unknown area as -1 (Svalbard and Jan Mayen);
        // it reads as 0, as in v1.
        c.area = static_cast<std::uint64_t>(std::max<std::int64_t>(row.get<std::int64_t>("area"), 0));
        c.population = row.get<std::uint64_t>("population");
        c.landlocked = row.get<bool>("landlocked");
        c.independent = row.get<bool>("independent");
        c.un_member = row.get<bool>("un_member");
        c.languages = row.get_or<std::string>("languages", "");
        c.language_shares = row.get_or<std::string>("language_shares", "");
        if (!country::for_each_share_(c.language_shares, [](std::string_view, double) {}))
        {
            throw row.error("language_shares", "must be Name:percent pairs separated by ';', each percent above 0 and at most 100");
        }
        c.currency_code = row.get_or<std::string>("currency_code", "");
        c.currency_name = row.get_or<std::string>("currency_name", "");
        c.currency_symbol = row.get_or<std::string>("currency_symbol", "");
        c.borders = row.get_or<std::string>("borders", "");
        c.timezones = row.get_or<std::string>("timezones", "");
        c.driving_side = row.get_or<std::string>("driving_side", "");
        c.tld = row.get_or<std::string>("tld", "");
        c.idd_root = row.get_or<std::string>("idd_root", "");
        c.idd_suffix = row.get_or<std::string>("idd_suffix", "");
        c.demonym_m = row.get_or<std::string>("demonym_m", "");
        c.demonym_f = row.get_or<std::string>("demonym_f", "");
        c.flag_emoji = row.get_or<std::string>("flag_emoji", "");
        c.income_level = row.get_or<std::string>("income_level", "");
        c.start_of_week = row.get_or<std::string>("start_of_week", "");
        return c;
    }

    // Population weight; every country keeps a chance even with population 0.
    static double weight_(const country& c)
    {
        return static_cast<double>(std::max<std::uint64_t>(c.population, 1));
    }

    struct region_entry
    {
        std::vector<std::size_t> indices;
        core::discrete distribution{1.0};
    };

    core::layered_table _table{schema_()};
    std::vector<country> _countries; // in table row order
    core::discrete _distribution{1.0}; // replaced in rebuild_() when data is loaded
    std::map<std::string, region_entry, std::less<>> _region_index;
    std::map<std::string, std::size_t, std::less<>> _cca2_index;
    core::rng _engine{core::rng::from_entropy()};

    struct auto_probe_tag {};

    explicit cntg(auto_probe_tag /*tag*/)
    {
        if (!load(dataset::lite)) (void)load(dataset::full);
    }

    static std::span<const core::embedded_file> embedded_files_()
    {
#ifdef DASMIG_COUNTRY_EMBED
        return embedded::countrygen::files;
#else
        return {};
#endif
    }

    void require_data_() const
    {
        if (_countries.empty())
        {
            throw std::runtime_error("No country data loaded. Call load() first.");
        }
    }

    [[nodiscard]] country result_(const country& c, std::uint64_t call_seed) const
    {
        country result = c;
        result._signature = {.seed = call_seed, .data = data_hash()};
        return result;
    }

    void rebuild_()
    {
        std::vector<country> countries;
        countries.reserve(_table.size());
        for (const auto& row : _table.rows()) countries.push_back(parse_(row));

        core::discrete distribution{1.0};
        std::map<std::string, region_entry, std::less<>> regions;
        std::map<std::string, std::size_t, std::less<>> cca2_index;
        if (!countries.empty())
        {
            std::vector<double> weights;
            weights.reserve(countries.size());
            std::map<std::string, std::vector<double>, std::less<>> region_weights;
            for (std::size_t i = 0; i < countries.size(); ++i)
            {
                const auto& c = countries[i];
                weights.push_back(weight_(c));
                regions[c.region].indices.push_back(i);
                region_weights[c.region].push_back(weight_(c));
                cca2_index.emplace(c.cca2, i);
            }
            distribution = core::discrete{weights};
            for (auto& [name, entry] : regions)
            {
                entry.distribution = core::discrete{region_weights[name]};
            }
        }
        _countries = std::move(countries);
        _distribution = std::move(distribution);
        _region_index = std::move(regions);
        _cca2_index = std::move(cca2_index);
    }
};

} // namespace dasmig

#endif // DASMIG_COUNTRYGEN_HPP
