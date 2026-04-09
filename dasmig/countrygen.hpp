#ifndef DASMIG_COUNTRYGEN_HPP
#define DASMIG_COUNTRYGEN_HPP

#include "random.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/// @file countrygen.hpp
/// @brief Country generator library — procedural country generation for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

namespace dasmig
{

/// @brief Dataset size tier for resource loading.
enum class dataset : std::uint8_t
{
    lite, ///< ~195 sovereign states only.
    full  ///< ~250 countries and territories.
};

/// @brief Return type for country generation, holding all data fields.
///
/// Supports implicit conversion to std::string (returns the common name)
/// and streaming via operator<<.
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

    /// @brief Retrieve the random seed used to generate this country.
    /// @return The per-call seed for replay.
    /// @see cntg::get_country(std::uint64_t)
    [[nodiscard]] std::uint64_t seed() const
    {
        return _seed;
    }

    /// @brief Implicit conversion to std::string.
    /// @return The common English name.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return name_common;
    }

    /// @brief Stream the common name to an output stream.
    friend std::ostream& operator<<(std::ostream& os, const country& c)
    {
        os << c.name_common;
        return os;
    }

  private:
    std::uint64_t _seed{0};

    friend class cntg;
};

/// @brief Country generator that produces random countries from
///        aggregated open-data sources.
///
/// Generates countries using population-weighted random selection by
/// default. Larger countries are proportionally more likely to be
/// selected, mirroring real-world demographic distributions.
///
/// Can be used as a singleton via instance() or constructed independently.
///
/// @par Thread safety
/// Each instance is independent. Concurrent calls to get_country() on
/// the **same** instance require external synchronization. load() mutates
/// internal state and must not be called concurrently with get_country()
/// on the same instance.
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
    /// The singleton auto-probes common resource paths on first access.
    /// @return Reference to the global cntg instance.
    static cntg& instance()
    {
        static cntg inst{auto_probe_tag{}};
        return inst;
    }

    // -- Generation -------------------------------------------------------

    /// @brief Generate a random country.
    ///
    /// By default, selection is population-weighted. Call weighted(false)
    /// to switch to uniform random selection.
    /// @return A country object with all fields.
    /// @throws std::runtime_error If no data has been loaded.
    [[nodiscard]] country get_country()
    {
        auto call_seed = static_cast<std::uint64_t>(_engine());
        return get_country(call_seed);
    }

    /// @brief Generate a deterministic country using a specific seed.
    /// @param call_seed Seed for reproducible results.
    /// @return A country object with all fields.
    /// @throws std::runtime_error If no data has been loaded.
    [[nodiscard]] country get_country(std::uint64_t call_seed) const
    {
        if (_countries.empty())
        {
            throw std::runtime_error(
                "No country data loaded. Call load() first.");
        }

        effolkronium::random_local call_engine;
        call_engine.seed(static_cast<std::mt19937::result_type>(
            (call_seed ^ (call_seed >> seed_shift_))));

        auto idx = _weighted
                       ? _distribution(call_engine.engine())
                       : _uniform(call_engine.engine());
        country result = _countries[idx]; // NOLINT(cppcoreguidelines-pro-bounds-*)
        result._seed = call_seed;
        return result;
    }

    /// @brief Generate a random country filtered by region.
    /// @param rgn UN geoscheme region (e.g. "Europe", "Asia").
    /// @return A country object from the specified region.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If no countries match the region.
    [[nodiscard]] country get_country(const std::string& rgn)
    {
        auto call_seed = static_cast<std::uint64_t>(_engine());
        return get_country(rgn, call_seed);
    }

    /// @brief Generate a deterministic country filtered by region.
    /// @param rgn UN geoscheme region.
    /// @param call_seed Seed for reproducible results.
    /// @return A country object from the specified region.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If no countries match the region.
    [[nodiscard]] country get_country(const std::string& rgn,
                                      std::uint64_t call_seed) const
    {
        if (_countries.empty())
        {
            throw std::runtime_error(
                "No country data loaded. Call load() first.");
        }

        auto it = _region_index.find(rgn);
        if (it == _region_index.end())
        {
            throw std::invalid_argument(
                "No countries found for region: " + rgn);
        }

        effolkronium::random_local call_engine;
        call_engine.seed(static_cast<std::mt19937::result_type>(
            (call_seed ^ (call_seed >> seed_shift_))));

        const auto& idx = it->second;
        auto selected = _weighted
                            ? idx.distribution(call_engine.engine())
                            : std::uniform_int_distribution<std::size_t>(
                                  0, idx.country_indices.size() - 1)(
                                  call_engine.engine());
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-*)
        country result = _countries[idx.country_indices[selected]];
        result._seed = call_seed;
        return result;
    }

    // -- Seeding ----------------------------------------------------------

    /// @name Seeding
    /// @{

    /// @brief Seed the internal random engine for deterministic sequences.
    /// @param seed_value The seed value.
    /// @return `*this` for chaining.
    cntg& seed(std::uint64_t seed_value)
    {
        _engine.seed(seed_value);
        return *this;
    }

    /// @brief Reseed the engine with a non-deterministic source.
    /// @return `*this` for chaining.
    cntg& unseed()
    {
        _engine.seed(std::random_device{}());
        return *this;
    }

    /// @}

    /// @brief Set whether generation is population-weighted or uniform.
    /// @param enable `true` for weighted (default), `false` for uniform.
    /// @return `*this` for chaining.
    cntg& weighted(bool enable)
    {
        _weighted = enable;
        return *this;
    }

    /// @brief Query whether generation is population-weighted.
    /// @return `true` if weighted (default), `false` if uniform.
    [[nodiscard]] bool weighted() const
    {
        return _weighted;
    }

    // -- Data management --------------------------------------------------

    /// @brief Check whether any data has been loaded.
    [[nodiscard]] bool has_data() const
    {
        return !_countries.empty();
    }

    /// @brief Return the number of loaded countries.
    [[nodiscard]] std::size_t country_count() const
    {
        return _countries.size();
    }

    /// @brief Load country data from a TSV file.
    ///
    /// Expects a tab-delimited file with a header row and 31 columns
    /// matching the schema produced by scripts/prepare_countries.py.
    /// Safe to call multiple times.
    ///
    /// @param tsv_path Path to the TSV file.
    void load(const std::filesystem::path& tsv_path)
    {
        if (!std::filesystem::exists(tsv_path) ||
            !std::filesystem::is_regular_file(tsv_path))
        {
            return;
        }

        std::ifstream file{tsv_path};
        if (!file.is_open())
        {
            return;
        }

        std::string line;

        // Skip header row.
        if (!std::getline(file, line))
        {
            return;
        }

        while (std::getline(file, line))
        {
            if (line.empty())
            {
                continue;
            }

            if (line.back() == '\r')
            {
                line.pop_back();
            }

            auto c = parse_line(line);
            if (!c.cca3.empty())
            {
                _countries.push_back(std::move(c));
            }
        }

        rebuild_indices();
    }

    /// @brief Load a specific dataset tier from a base resources directory.
    /// @param tier The dataset size to load.
    /// @return `true` if a matching directory was found and loaded.
    [[nodiscard]] bool load(dataset tier)
    {
        static constexpr std::array probe_paths = {
            "resources", "../resources", "country-generator/resources"};

        const char* subfolder =
            (tier == dataset::full) ? "full" : "lite";

        auto found = std::ranges::find_if(probe_paths, [&](const char* base) {
            const auto tsv =
                std::filesystem::path{base} / subfolder / "countries.tsv";
            return std::filesystem::is_regular_file(tsv);
        });
        if (found != probe_paths.end())
        {
            load(std::filesystem::path{*found} / subfolder / "countries.tsv");
            return true;
        }
        return false;
    }

  private:
    // All loaded countries.
    std::vector<country> _countries;

    // Population-weighted distribution over _countries indices.
    mutable std::discrete_distribution<std::size_t> _distribution;

    // Uniform distribution over _countries indices.
    mutable std::uniform_int_distribution<std::size_t> _uniform;

    // Whether to use population-weighted (true) or uniform (false) selection.
    bool _weighted{true};

    // Pre-built per-region index for O(1) region-filtered lookups.
    struct region_entry
    {
        std::vector<std::size_t> country_indices;
        mutable std::discrete_distribution<std::size_t> distribution;
    };

    std::unordered_map<std::string, region_entry> _region_index;

    // Bit shift for mixing per-call seeds.
    static constexpr unsigned seed_shift_{32U};

    // Per-instance random engine for seed drawing.
    std::mt19937_64 _engine{std::random_device{}()};

    // Tag type for the auto-probing singleton constructor.
    struct auto_probe_tag {};

    // Singleton constructor: auto-probes common resource locations.
    explicit cntg(auto_probe_tag /*tag*/)
    {
        static constexpr std::array probe_paths = {
            "resources", "../resources", "country-generator/resources"};

        auto found = std::ranges::find_if(probe_paths, [](const char* p) {
            return std::filesystem::exists(p) &&
                   std::filesystem::is_directory(p);
        });
        if (found != probe_paths.end())
        {
            const std::filesystem::path base{*found};
            auto lite_tsv = base / "lite" / "countries.tsv";
            auto full_tsv = base / "full" / "countries.tsv";
            if (std::filesystem::is_regular_file(lite_tsv))
            {
                load(lite_tsv);
            }
            else if (std::filesystem::is_regular_file(full_tsv))
            {
                load(full_tsv);
            }
        }
    }

    // Rebuild the global distribution and region index.
    void rebuild_indices()
    {
        // Global distribution.
        std::vector<double> weights;
        weights.reserve(_countries.size());

        std::ranges::transform(
            _countries, std::back_inserter(weights), [](const country& c) {
                return static_cast<double>(
                    std::max<std::uint64_t>(c.population, 1));
            });

        _distribution = std::discrete_distribution<std::size_t>(
            weights.begin(), weights.end());

        // Uniform distribution.
        if (!_countries.empty())
        {
            _uniform = std::uniform_int_distribution<std::size_t>(
                0, _countries.size() - 1);
        }

        // Region index.
        _region_index.clear();

        for (auto&& [i, c] : _countries | std::views::enumerate)
        {
            auto& entry = _region_index[c.region];
            entry.country_indices.push_back(static_cast<std::size_t>(i));
        }

        // Build per-region distributions.
        for (auto& [rgn, entry] : _region_index)
        {
            std::vector<double> rw;
            rw.reserve(entry.country_indices.size());

            for (auto idx : entry.country_indices)
            {
                rw.push_back(static_cast<double>(
                    std::max<std::uint64_t>(
                        _countries[idx].population, 1))); // NOLINT(cppcoreguidelines-pro-bounds-*)
            }

            entry.distribution = std::discrete_distribution<std::size_t>(
                rw.begin(), rw.end());
        }
    }

    // Parse a single tab-delimited line into a country object.
    // Expected 31 fields matching the TSV header.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    static country parse_line(const std::string& line)
    {
        country c;

        static constexpr std::size_t num_fields{31};
        std::vector<std::string> fields;
        fields.reserve(num_fields);

        for (auto part : line | std::views::split('\t'))
        {
            fields.emplace_back(std::ranges::begin(part),
                                std::ranges::end(part));
        }

        if (fields.size() < num_fields)
        {
            return c; // cca3 stays empty, skipped by caller.
        }

        // Locale-independent numeric parsing via std::from_chars.
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        auto parse_double = [](const std::string& s, double fallback = 0.0) {
            if (s.empty())
            {
                return fallback;
            }
            double val{};
            auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
            return ec == std::errc{} ? val : fallback;
        };

        auto parse_uint64 = [](const std::string& s) -> std::uint64_t {
            if (s.empty())
            {
                return 0ULL;
            }
            std::uint64_t val{};
            auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
            return ec == std::errc{} ? val : 0ULL;
        };
        // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

        c.cca2 = fields[0];
        c.cca3 = fields[1];
        c.ccn3 = fields[2];
        c.name_common = fields[3];
        c.name_official = fields[4];
        c.capital = fields[5];
        c.region = fields[6];
        c.subregion = fields[7];
        c.continent = fields[8];
        c.latitude = parse_double(fields[9]);
        c.longitude = parse_double(fields[10]);
        c.area = parse_uint64(fields[11]);
        c.population = parse_uint64(fields[12]);
        c.landlocked = (fields[13] == "1");
        c.independent = (fields[14] == "1");
        c.un_member = (fields[15] == "1");
        c.languages = fields[16];
        c.currency_code = fields[17];
        c.currency_name = fields[18];
        c.currency_symbol = fields[19];
        c.borders = fields[20];
        c.timezones = fields[21];
        c.driving_side = fields[22];
        c.tld = fields[23];
        c.idd_root = fields[24];
        c.idd_suffix = fields[25];
        c.demonym_m = fields[26];
        c.demonym_f = fields[27];
        c.flag_emoji = fields[28];
        c.income_level = fields[29];
        c.start_of_week = fields[30];

        return c;
    }
};

} // namespace dasmig

#endif // DASMIG_COUNTRYGEN_HPP
