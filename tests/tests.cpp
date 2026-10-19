#include "catch_amalgamated.hpp"
#include "../dasmig/countrygen.hpp"

#include <algorithm>
#include <concepts>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

template <typename Generator, typename... Args>
concept country_call = requires(Generator& gen, Args... args) {
    { gen.get_country(args...) } -> std::same_as<dasmig::country>;
};

template <typename Generator>
concept seed_control = requires(Generator& gen) { gen.seed(42); };

template <typename Generator>
concept unseed_control = requires(Generator& gen) { gen.unseed(); };

template <typename Generator>
concept weighting_setter = requires(Generator& gen) { gen.weighted(false); };

template <typename Generator>
concept weighting_getter = requires(const Generator& gen) { gen.weighted(); };

static_assert(country_call<dasmig::cntg, dasmig::country_request>);
static_assert(country_call<const dasmig::cntg, dasmig::country_request, std::uint64_t>);
static_assert(!country_call<const dasmig::cntg, dasmig::country_request>);
static_assert(!country_call<dasmig::cntg>);
static_assert(!country_call<dasmig::cntg, std::uint64_t>);
static_assert(!country_call<dasmig::cntg, std::string>);
static_assert(!country_call<dasmig::cntg, std::string, std::uint64_t>);
static_assert(!country_call<dasmig::cntg, const char*>);
static_assert(!country_call<dasmig::cntg, const char*, std::uint64_t>);
static_assert(!country_call<dasmig::cntg, std::uint64_t, dasmig::country_request>);
static_assert(!seed_control<dasmig::cntg>);
static_assert(!unseed_control<dasmig::cntg>);
static_assert(!weighting_setter<dasmig::cntg>);
static_assert(!weighting_getter<dasmig::cntg>);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static dasmig::cntg make_loaded_generator()
{
    dasmig::cntg gen;
    if (!gen.load(dasmig::dataset::lite) &&
        !gen.load(dasmig::dataset::full))
    {
        for (const auto& candidate :
             {"resources/full/countries.tsv",
              "../resources/full/countries.tsv",
              "country-generator/resources/full/countries.tsv"})
        {
            if (std::filesystem::exists(candidate))
            {
                gen.load(candidate);
                break;
            }
        }
    }
    REQUIRE(gen.has_data());
    return gen;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

TEST_CASE("load - populates country data from TSV", "[loading]")
{
    auto gen = make_loaded_generator();
    // Lite has ~195, full has ~250.
    REQUIRE(gen.country_count() > 100);
}

TEST_CASE("load - non-existent path is a no-op", "[loading]")
{
    dasmig::cntg gen;
    gen.load("does_not_exist.tsv");
    REQUIRE_FALSE(gen.has_data());
    REQUIRE(gen.country_count() == 0);
}

TEST_CASE("load - directory path is a no-op", "[loading]")
{
    dasmig::cntg gen;
    gen.load(std::filesystem::temp_directory_path());
    REQUIRE_FALSE(gen.has_data());
}

TEST_CASE("load - zero-byte file is an error", "[loading]")
{
    auto tmp = std::filesystem::temp_directory_path() / "cntg_empty.tsv";
    { std::ofstream f{tmp}; }
    dasmig::cntg gen;
    REQUIRE_THROWS_AS(gen.load(tmp), dasmig::core::tsv_error); // no header line
    REQUIRE_FALSE(gen.has_data());
    std::filesystem::remove(tmp);
}

TEST_CASE("load - a malformed line is an error with its location", "[loading]")
{
    auto tmp = std::filesystem::temp_directory_path() / "cntg_test.tsv";
    {
        std::ofstream f{tmp};
        // Header
        f << "cca2\tcca3\tccn3\tname_common\tname_official\tcapital\tregion\t"
             "subregion\tcontinent\tlatitude\tlongitude\tarea\tpopulation\t"
             "landlocked\tindependent\tun_member\tlanguages\tcurrency_code\t"
             "currency_name\tcurrency_symbol\tborders\ttimezones\t"
             "driving_side\ttld\tidd_root\tidd_suffix\tdemonym_m\tdemonym_f\t"
             "flag_emoji\tincome_level\tstart_of_week\n";
        // Good line
        f << "XX\tXXX\t999\tTestland\tRepublic of Testland\tTestville\t"
             "Europe\tNorthern Europe\tEurope\t50.0\t10.0\t100000\t5000000\t"
             "0\t1\t1\tEnglish\tXXD\tTestland dollar\tT$\t\tUTC+01:00\t"
             "right\t.xx\t+9\t9\tTestlander\tTestlander\t\tHigh income\t"
             "monday\n";
        // Bad line (too few fields)
        f << "bad_line_too_few_fields\n";
        // Empty cca3
        f << "YY\t\t000\tBadland\tBadland\t\t\t\t\t0\t0\t0\t0\t0\t0\t0\t\t"
             "\t\t\t\t\t\t\t\t\t\t\t\t\t\n";
    }

    dasmig::cntg gen;
    try
    {
        gen.load(tmp);
        FAIL("expected core::tsv_error");
    }
    catch (const dasmig::core::tsv_error& e)
    {
        REQUIRE(e.line() == 3);           // the line with too few fields
        REQUIRE(e.column() == "cca3");    // its missing cells are empty
    }
    REQUIRE_FALSE(gen.has_data());        // nothing was loaded
    std::filesystem::remove(tmp);
}

TEST_CASE("load - skips empty lines", "[loading]")
{
    auto tmp = std::filesystem::temp_directory_path() / "cntg_empty_lines.tsv";
    {
        std::ofstream f{tmp};
        f << "cca2\tcca3\tccn3\tname_common\tname_official\tcapital\tregion\t"
             "subregion\tcontinent\tlatitude\tlongitude\tarea\tpopulation\t"
             "landlocked\tindependent\tun_member\tlanguages\tcurrency_code\t"
             "currency_name\tcurrency_symbol\tborders\ttimezones\t"
             "driving_side\ttld\tidd_root\tidd_suffix\tdemonym_m\tdemonym_f\t"
             "flag_emoji\tincome_level\tstart_of_week\n";
        f << "\n";
        f << "XX\tXXX\t999\tTestland\tRepublic of Testland\tTestville\t"
             "Europe\tNorthern Europe\tEurope\t50.0\t10.0\t100000\t5000000\t"
             "0\t1\t1\tEnglish\tXXD\tTestland dollar\tT$\t\tUTC+01:00\t"
             "right\t.xx\t+9\t9\tTestlander\tTestlander\t\tHigh income\t"
             "monday\n";
        f << "\n\n";
    }

    dasmig::cntg gen;
    gen.load(tmp);
    REQUIRE(gen.country_count() == 1);
    std::filesystem::remove(tmp);
}

TEST_CASE("loading again replaces the data", "[loading]")
{
    auto gen = make_loaded_generator();
    auto count1 = gen.country_count();
    (void)gen.load(dasmig::dataset::lite);
    REQUIRE(gen.country_count() == count1);
    REQUIRE(gen.layers().size() == 1);
}

// ---------------------------------------------------------------------------
// Dataset tier loading
// ---------------------------------------------------------------------------

TEST_CASE("load(dataset::lite) loads fewer than full", "[loading][tier]")
{
    dasmig::cntg gen_lite;
    dasmig::cntg gen_full;

    bool lite_ok = gen_lite.load(dasmig::dataset::lite);
    bool full_ok = gen_full.load(dasmig::dataset::full);

    if (lite_ok && full_ok)
    {
        REQUIRE(gen_lite.country_count() > 0);
        REQUIRE(gen_full.country_count() > 0);
        REQUIRE(gen_full.country_count() > gen_lite.country_count());
    }
}

#ifndef DASMIG_COUNTRY_EMBED // compiled-in data is never missing
TEST_CASE("load(dataset) returns false for missing tier", "[loading][tier]")
{
    dasmig::cntg gen;
    auto old_cwd = std::filesystem::current_path();
    auto tmp = std::filesystem::temp_directory_path() / "cntg_empty_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);

    REQUIRE_FALSE(gen.load(dasmig::dataset::lite));
    REQUIRE_FALSE(gen.load(dasmig::dataset::full));
    REQUIRE_FALSE(gen.has_data());

    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);
}
#endif

// ---------------------------------------------------------------------------
// Generation — basic
// ---------------------------------------------------------------------------

TEST_CASE("get_country returns a valid country", "[generation]")
{
    auto gen = make_loaded_generator();
    auto c = gen.get_country({});
    REQUIRE_FALSE(c.name_common.empty());
    REQUIRE_FALSE(c.cca2.empty());
    REQUIRE_FALSE(c.cca3.empty());
    REQUIRE(c.seed() != 0);
}

TEST_CASE("get_country with seed is deterministic", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto c1 = gen.get_country({}, 42);
    auto c2 = gen.get_country({}, 42);
    REQUIRE(c1.cca3 == c2.cca3);
    REQUIRE(c1.name_common == c2.name_common);
}

TEST_CASE("get_country seed replay", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto c1 = gen.get_country({});
    auto c2 = gen.get_country({}, c1.seed());
    REQUIRE(c1.cca3 == c2.cca3);
}

TEST_CASE("get_country throws when empty", "[generation]")
{
    dasmig::cntg gen;
    REQUIRE_THROWS_AS(gen.get_country({}), std::runtime_error);
}

TEST_CASE("get_country(request, seed) throws when empty", "[generation]")
{
    const dasmig::cntg gen;
    REQUIRE_THROWS_AS(gen.get_country({}, 42), std::runtime_error);
}

TEST_CASE("implicit string conversion returns common name", "[generation]")
{
    auto gen = make_loaded_generator();
    auto c = gen.get_country({});
    std::string name = c;
    REQUIRE(name == c.name_common);
}

TEST_CASE("ostream operator outputs common name", "[generation]")
{
    auto gen = make_loaded_generator();
    auto c = gen.get_country({});
    std::ostringstream oss;
    oss << c;
    REQUIRE(oss.str() == c.name_common);
}

// ---------------------------------------------------------------------------
// Region filtering
// ---------------------------------------------------------------------------

TEST_CASE("get_country(request) filters correctly", "[region]")
{
    auto gen = make_loaded_generator();

    for (int i = 0; i < 20; ++i)
    {
        auto c = gen.get_country({.region = "Europe"});
        REQUIRE(c.region == "Europe");
    }
}

TEST_CASE("get_country(request, seed) filters deterministically", "[region][seed]")
{
    auto gen = make_loaded_generator();
    auto c1 = gen.get_country({.region = "Asia"}, 7777);
    auto c2 = gen.get_country({.region = "Asia"}, 7777);
    REQUIRE(c1.cca3 == c2.cca3);
    REQUIRE(c1.region == "Asia");
}

TEST_CASE("get_country(request) throws for unknown region", "[region]")
{
    auto gen = make_loaded_generator();
    REQUIRE_THROWS_AS(gen.get_country({.region = "Atlantis"}), std::invalid_argument);
}

TEST_CASE("get_country(request) with region throws when empty", "[region]")
{
    dasmig::cntg gen;
    REQUIRE_THROWS_AS(gen.get_country({.region = "Europe"}), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Per-call requests
// ---------------------------------------------------------------------------

TEST_CASE("country_request defaults to all countries with population weighting",
          "[request][generation]")
{
    auto gen = make_loaded_generator();
    const dasmig::country_request request;
    REQUIRE_FALSE(request.region.has_value());
    REQUIRE(request.weighted);

    REQUIRE(gen.get_country(request, 42).to_json() ==
            gen.get_country(dasmig::country_request{.weighted = true}, 42).to_json());
}

TEST_CASE("country_request selects every major region in either mode",
          "[request][region][weighting]")
{
    auto gen = make_loaded_generator();
    const auto& const_gen = gen;
    for (const bool weighted : {true, false})
    {
        for (std::uint64_t seed = 0; seed < 64; ++seed)
        {
            for (const char* region : {"Africa", "Americas", "Asia", "Europe", "Oceania"})
            {
                const dasmig::country_request request{.region = region, .weighted = weighted};
                const auto c = const_gen.get_country(request, seed);
                REQUIRE(c.region == region);
                REQUIRE(c.to_json() == const_gen.get_country(request, seed).to_json());
            }
        }
    }
}

TEST_CASE("country_request replays with the same options seed and data",
          "[request][seed][json]")
{
    auto gen = make_loaded_generator();
    for (const bool weighted : {true, false})
    {
        for (const auto& request : {dasmig::country_request{.weighted = weighted},
                                   dasmig::country_request{.region = "Asia", .weighted = weighted}})
        {
            for (const auto seed : {std::uint64_t{0}, std::uint64_t{42},
                                    std::numeric_limits<std::uint64_t>::max()})
            {
                const auto c = gen.get_country(request, seed);
                REQUIRE(c.seed() == seed);
                REQUIRE(c.signature().data == gen.data_hash());
                (void)gen.get_country({.region = "Europe", .weighted = !weighted}, 123);
                (void)gen.get_country({.weighted = !weighted});
                REQUIRE(gen.get_country(request, c.seed()).to_json() == c.to_json());
            }
        }
    }
}

TEST_CASE("country_request preserves validation and errors", "[request][errors]")
{
    dasmig::cntg empty;
    for (const auto& request : {dasmig::country_request{},
                               dasmig::country_request{.weighted = false},
                               dasmig::country_request{.region = "Atlantis"}})
    {
        REQUIRE_THROWS_AS(empty.get_country(request, 42), std::runtime_error);
        REQUIRE_THROWS_WITH(empty.get_country(request, 42),
                            "No country data loaded. Call load() first.");
        REQUIRE_THROWS_AS(empty.get_country(request), std::runtime_error);
        REQUIRE_THROWS_WITH(empty.get_country(request),
                            "No country data loaded. Call load() first.");
    }

    auto gen = make_loaded_generator();
    for (const bool weighted : {true, false})
    {
        for (const char* region : {"Atlantis", "", "europe"})
        {
            const dasmig::country_request request{.region = region, .weighted = weighted};
            REQUIRE_THROWS_AS(gen.get_country(request, 42), std::invalid_argument);
            const auto error = std::string{"No countries found for region: "} + region;
            REQUIRE_THROWS_WITH(gen.get_country(request, 42), error);
            REQUIRE_THROWS_AS(gen.get_country(request), std::invalid_argument);
            REQUIRE_THROWS_WITH(gen.get_country(request), error);
        }
    }
}

TEST_CASE("country_request options do not leak between calls or instances",
          "[request][seed][weighting]")
{
    auto gen = make_loaded_generator();
    auto control = make_loaded_generator();
    const auto hash = gen.data_hash();
    const auto count = gen.country_count();
    const auto layers = gen.layers().size();
    for (const bool weighted : {true, false})
    {
        const dasmig::country_request request{.region = "Asia", .weighted = weighted};
        for (std::uint64_t seed = 0; seed < 20; ++seed)
        {
            const auto expected = control.get_country(request, seed).to_json();
            (void)gen.get_country({.region = "Europe", .weighted = !weighted});
            (void)gen.get_country(dasmig::country_request{.weighted = !weighted}, seed);
            REQUIRE_THROWS_AS(
                gen.get_country(dasmig::country_request{.region = "Atlantis",
                                                       .weighted = !weighted}, seed),
                std::invalid_argument);
            REQUIRE(gen.get_country(request, seed).to_json() == expected);
            REQUIRE(gen.get_country({}, seed).to_json() == control.get_country({}, seed).to_json());
        }
    }
    REQUIRE(gen.data_hash() == hash);
    REQUIRE(gen.country_count() == count);
    REQUIRE(gen.layers().size() == layers);
}

// ---------------------------------------------------------------------------
// Unseeded request replay
// ---------------------------------------------------------------------------

TEST_CASE("unseeded requests replay with the same options seed and data", "[request][seed]")
{
    auto gen = make_loaded_generator();
    std::set<std::uint64_t> seeds;
    for (const bool weighted : {true, false})
    {
        for (const auto& request : {dasmig::country_request{.weighted = weighted},
                                   dasmig::country_request{.region = "Asia", .weighted = weighted}})
        {
            for (int i = 0; i < 20; ++i)
            {
                const auto c = gen.get_country(request);
                if (request.region) REQUIRE(c.region == *request.region);
                REQUIRE(c.signature().data == gen.data_hash());
                REQUIRE(gen.get_country(request, c.seed()).to_json() == c.to_json());
                seeds.insert(c.seed());
            }
        }
    }
    REQUIRE(seeds.size() == 80);
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

TEST_CASE("singleton returns same instance", "[singleton]")
{
    auto& a = dasmig::cntg::instance();
    auto& b = dasmig::cntg::instance();
    REQUIRE(&a == &b);
}

// ---------------------------------------------------------------------------
// Weighted / uniform
// ---------------------------------------------------------------------------

TEST_CASE("country_request weighted false enables uniform selection", "[weighting]")
{
    auto gen = make_loaded_generator();
    auto seeds = dasmig::core::rng{99};

    // With uniform selection, small countries should appear more often.
    int below_10m = 0;
    static constexpr int draws = 200;

    for (int i = 0; i < draws; ++i)
    {
        auto c = gen.get_country({.weighted = false}, seeds());
        if (c.population < 10'000'000)
        {
            ++below_10m;
        }
    }

    // Most countries have pop < 10M. Uniform should produce many of them.
    REQUIRE(below_10m > draws / 3);
}

TEST_CASE("country_request weighted false with region filter", "[weighting][region]")
{
    auto gen = make_loaded_generator();

    for (int i = 0; i < 20; ++i)
    {
        auto c = gen.get_country({.region = "Americas", .weighted = false});
        REQUIRE(c.region == "Americas");
    }
}

// ---------------------------------------------------------------------------
// Population weighting
// ---------------------------------------------------------------------------

TEST_CASE("population weighting favors large countries", "[weighting]")
{
    auto gen = make_loaded_generator();
    auto seeds = dasmig::core::rng{12345};

    int large_count = 0;
    static constexpr int draws = 200;

    for (int i = 0; i < draws; ++i)
    {
        auto c = gen.get_country({}, seeds());
        if (c.population > 50'000'000)
        {
            ++large_count;
        }
    }

    // With population weighting, most draws should be large countries.
    REQUIRE(large_count > draws / 2);
}

// ---------------------------------------------------------------------------
// Data integrity — spot check fields on generated countries
// ---------------------------------------------------------------------------

TEST_CASE("generated countries have valid field ranges", "[integrity]")
{
    auto gen = make_loaded_generator();
    auto seeds = dasmig::core::rng{42};

    for (int i = 0; i < 50; ++i)
    {
        auto c = gen.get_country({}, seeds());
        REQUIRE_FALSE(c.name_common.empty());
        REQUIRE_FALSE(c.cca2.empty());
        REQUIRE(c.cca2.size() == 2);
        REQUIRE_FALSE(c.cca3.empty());
        REQUIRE(c.cca3.size() == 3);
        REQUIRE(c.latitude >= -90.0);
        REQUIRE(c.latitude <= 90.0);
        REQUIRE(c.longitude >= -180.0);
        REQUIRE(c.longitude <= 180.0);
        REQUIRE_FALSE(c.region.empty());
        REQUIRE(c.seed() != 0);

        // Validate enum-like fields.
        REQUIRE((c.driving_side == "right" || c.driving_side == "left" ||
                 c.driving_side.empty()));
        REQUIRE((c.start_of_week == "monday" || c.start_of_week == "sunday" ||
                 c.start_of_week == "saturday" || c.start_of_week.empty()));
    }
}

// ---------------------------------------------------------------------------
// Missing coverage: has_data before load
// ---------------------------------------------------------------------------

TEST_CASE("has_data is false before load", "[loading]")
{
    const dasmig::cntg gen;
    REQUIRE_FALSE(gen.has_data());
    REQUIRE(gen.country_count() == 0);
}

// ---------------------------------------------------------------------------
// Missing coverage: header-only file (no data rows)
// ---------------------------------------------------------------------------

TEST_CASE("load - header-only file yields no data", "[loading]")
{
    auto tmp = std::filesystem::temp_directory_path() / "cntg_header_only.tsv";
    {
        std::ofstream f{tmp};
        f << "cca2\tcca3\tccn3\tname_common\tname_official\tcapital\tregion\t"
             "subregion\tcontinent\tlatitude\tlongitude\tarea\tpopulation\t"
             "landlocked\tindependent\tun_member\tlanguages\tcurrency_code\t"
             "currency_name\tcurrency_symbol\tborders\ttimezones\t"
             "driving_side\ttld\tidd_root\tidd_suffix\tdemonym_m\tdemonym_f\t"
             "flag_emoji\tincome_level\tstart_of_week\n";
    }

    dasmig::cntg gen;
    gen.load(tmp);
    REQUIRE_FALSE(gen.has_data());
    REQUIRE(gen.country_count() == 0);
    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Missing coverage: CRLF line endings
// ---------------------------------------------------------------------------

TEST_CASE("load - handles CRLF line endings", "[loading]")
{
    auto tmp = std::filesystem::temp_directory_path() / "cntg_crlf.tsv";
    {
        std::ofstream f{tmp, std::ios::binary};
        f << "cca2\tcca3\tccn3\tname_common\tname_official\tcapital\tregion\t"
             "subregion\tcontinent\tlatitude\tlongitude\tarea\tpopulation\t"
             "landlocked\tindependent\tun_member\tlanguages\tcurrency_code\t"
             "currency_name\tcurrency_symbol\tborders\ttimezones\t"
             "driving_side\ttld\tidd_root\tidd_suffix\tdemonym_m\tdemonym_f\t"
             "flag_emoji\tincome_level\tstart_of_week\r\n";
        f << "XX\tXXX\t999\tCRLFland\tRepublic of CRLFland\tCRLFville\t"
             "Europe\tNorthern Europe\tEurope\t60.0\t25.0\t50000\t1000000\t"
             "0\t1\t1\tEnglish\tXXD\tCRLF dollar\tC$\t\tUTC+02:00\t"
             "right\t.xx\t+9\t9\tCRLFlander\tCRLFlander\t\tHigh income\t"
             "monday\r\n";
    }

    dasmig::cntg gen;
    gen.load(tmp);
    REQUIRE(gen.country_count() == 1);
    auto c = gen.get_country({});
    REQUIRE(c.name_common == "CRLFland");
    // Verify the trailing \r was stripped, not embedded in start_of_week.
    REQUIRE(c.start_of_week == "monday");
    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Non-numeric fields are an error
// ---------------------------------------------------------------------------

TEST_CASE("load - non-numeric coordinate is an error", "[loading]")
{
    auto tmp = std::filesystem::temp_directory_path() / "cntg_bad_num.tsv";
    {
        std::ofstream f{tmp};
        f << "cca2\tcca3\tccn3\tname_common\tname_official\tcapital\tregion\t"
             "subregion\tcontinent\tlatitude\tlongitude\tarea\tpopulation\t"
             "landlocked\tindependent\tun_member\tlanguages\tcurrency_code\t"
             "currency_name\tcurrency_symbol\tborders\ttimezones\t"
             "driving_side\ttld\tidd_root\tidd_suffix\tdemonym_m\tdemonym_f\t"
             "flag_emoji\tincome_level\tstart_of_week\n";
        // latitude and longitude are non-numeric "abc" / "xyz"
        f << "ZZ\tZZZ\t000\tBadCoords\tBadCoords\tBadCity\tAfrica\t"
             "Northern Africa\tAfrica\tabc\txyz\t999\t100\t"
             "0\t1\t1\t\t\t\t\t\t\t"
             "right\t\t\t\t\t\t\t\t"
             "monday\n";
    }

    dasmig::cntg gen;
    try
    {
        gen.load(tmp);
        FAIL("expected core::tsv_error");
    }
    catch (const dasmig::core::tsv_error& e)
    {
        REQUIRE(e.line() == 2);
        REQUIRE(e.column() == "latitude");
    }
    REQUIRE_FALSE(gen.has_data());
    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Missing coverage: get_country(request, seed) with region throws when empty
// ---------------------------------------------------------------------------

TEST_CASE("get_country(request, seed) with region throws when empty", "[region]")
{
    const dasmig::cntg gen;
    REQUIRE_THROWS_AS(gen.get_country({.region = "Europe"}, 42), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Missing coverage: different seeds produce different countries
// ---------------------------------------------------------------------------

TEST_CASE("different seeds produce different countries", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    std::set<std::string> seen;

    for (std::uint64_t s = 1; s <= 50; ++s)
    {
        seen.insert(gen.get_country({}, s).cca3);
    }

    // 50 different seeds on ~195 countries should produce at least 5 distinct.
    REQUIRE(seen.size() >= 5);
}

// ---------------------------------------------------------------------------
// Missing coverage: region index covers all expected regions
// ---------------------------------------------------------------------------

TEST_CASE("every generated country has a known region", "[region][integrity]")
{
    auto gen = make_loaded_generator();
    auto seeds = dasmig::core::rng{1};

    static const std::set<std::string> valid_regions = {
        "Africa", "Americas", "Antarctic", "Asia", "Europe", "Oceania"};

    for (int i = 0; i < 100; ++i)
    {
        auto c = gen.get_country({}, seeds());
        REQUIRE(valid_regions.count(c.region) == 1);
    }
}

// ---------------------------------------------------------------------------
// Missing coverage: all six regions are reachable
// ---------------------------------------------------------------------------

TEST_CASE("all major regions are reachable", "[region]")
{
    auto gen = make_loaded_generator();

    for (const auto* rgn : {"Africa", "Americas", "Asia", "Europe", "Oceania"})
    {
        auto c = gen.get_country({.region = rgn});
        REQUIRE(c.region == std::string{rgn});
    }
}

// ---------------------------------------------------------------------------
// Deeper data integrity: all 31 fields populated for known country
// ---------------------------------------------------------------------------

TEST_CASE("known country has all fields populated", "[integrity]")
{
    auto gen = make_loaded_generator();
    auto seeds = dasmig::core::rng{42};

    bool found = false;
    for (int i = 0; i < 500; ++i)
    {
        auto c = gen.get_country({}, seeds());
        // Brazil is in both tiers, has all fields, and is likely with
        // population weighting.
        if (c.cca3 == "BRA")
        {
            REQUIRE(c.cca2 == "BR");
            REQUIRE(c.ccn3 == "076");
            REQUIRE(c.name_common == "Brazil");
            REQUIRE_FALSE(c.name_official.empty());
            REQUIRE_FALSE(c.capital.empty());
            REQUIRE(c.region == "Americas");
            REQUIRE(c.subregion == "South America");
            REQUIRE_FALSE(c.continent.empty());
            REQUIRE(c.latitude != Catch::Approx(0.0));
            REQUIRE(c.longitude != Catch::Approx(0.0));
            REQUIRE(c.area > 0);
            REQUIRE(c.population > 100'000'000);
            REQUIRE_FALSE(c.landlocked);
            REQUIRE(c.independent);
            REQUIRE(c.un_member);
            REQUIRE_FALSE(c.languages.empty());
            REQUIRE(c.currency_code == "BRL");
            REQUIRE_FALSE(c.currency_name.empty());
            REQUIRE_FALSE(c.currency_symbol.empty());
            REQUIRE_FALSE(c.borders.empty());
            REQUIRE_FALSE(c.timezones.empty());
            REQUIRE(c.driving_side == "right");
            REQUIRE(c.tld == ".br");
            REQUIRE_FALSE(c.idd_root.empty());
            REQUIRE_FALSE(c.idd_suffix.empty());
            REQUIRE_FALSE(c.demonym_m.empty());
            REQUIRE_FALSE(c.demonym_f.empty());
            REQUIRE_FALSE(c.income_level.empty());
            REQUIRE_FALSE(c.start_of_week.empty());
            found = true;
            break;
        }
    }

    // Brazil should appear within 500 pop-weighted draws.
    REQUIRE(found);
}

// ---------------------------------------------------------------------------
// Stronger uniform test: compare distribution shape
// ---------------------------------------------------------------------------

TEST_CASE("uniform selection distributes evenly", "[weighting]")
{
    auto gen = make_loaded_generator();
    auto seeds = dasmig::core::rng{123};

    std::unordered_map<std::string, int> counts;
    static constexpr int draws = 2000;

    for (int i = 0; i < draws; ++i)
    {
        counts[gen.get_country({.weighted = false}, seeds()).cca3]++;
    }

    // Uniform over ~195 countries, 2000 draws → expect ~10 per country.
    // No single country should dominate (>5% of draws, i.e., >100).
    for (const auto& [code, cnt] : counts)
    {
        REQUIRE(cnt < draws / 10);
    }
}

// ---------------------------------------------------------------------------
// load(dataset) return value is usable (not discarded)
// ---------------------------------------------------------------------------

TEST_CASE("load(dataset) return value is meaningful", "[loading][tier]")
{
    dasmig::cntg gen;
    // At least one tier should work from the test directory.
    bool lite_ok = gen.load(dasmig::dataset::lite);
    bool full_ok = gen.load(dasmig::dataset::full);
    REQUIRE((lite_ok || full_ok));
    REQUIRE(gen.has_data());
}
// ---------------------------------------------------------------------------
// CCA2 lookup — find_country()
// ---------------------------------------------------------------------------

TEST_CASE("find_country - returns matching country by CCA2", "[lookup]")
{
    auto gen = make_loaded_generator();
    auto us = gen.find_country("US");
    REQUIRE(us.cca2 == "US");
    REQUIRE_FALSE(us.name_common.empty());
}

TEST_CASE("find_country - returns correct fields", "[lookup]")
{
    auto gen = make_loaded_generator();
    auto jp = gen.find_country("JP");
    REQUIRE(jp.cca2 == "JP");
    REQUIRE_FALSE(jp.capital.empty());
    REQUIRE_FALSE(jp.region.empty());
}

TEST_CASE("find_country - throws for unknown CCA2", "[lookup]")
{
    auto gen = make_loaded_generator();
    REQUIRE_THROWS_AS(gen.find_country("ZZ"), std::invalid_argument);
}

TEST_CASE("find_country - throws when no data loaded", "[lookup]")
{
    const dasmig::cntg gen;
    REQUIRE_THROWS_AS(gen.find_country("US"), std::runtime_error);
}

TEST_CASE("find_country - case sensitive", "[lookup]")
{
    auto gen = make_loaded_generator();
    // CCA2 codes are uppercase; lowercase should not match.
    REQUIRE_THROWS_AS(gen.find_country("us"), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Known answers (rule 2): the same seed and data give these exact results on
// every compiler and platform. A change here is a change of output and needs
// a major version.
// ---------------------------------------------------------------------------

namespace
{

dasmig::cntg load_lite_file()
{
    dasmig::cntg gen;
    for (const char* path : {"resources/lite/countries.tsv", "../resources/lite/countries.tsv"})
    {
        if (std::filesystem::is_regular_file(path))
        {
            gen.load(std::filesystem::path{path});
            break;
        }
    }
    REQUIRE(gen.has_data());
    return gen;
}

} // namespace

TEST_CASE("known answers for fixed seeds", "[kat]")
{
    auto gen = load_lite_file();
    REQUIRE(gen.data_hash() == 0xba0f78d3ad63fb46ULL);

    REQUIRE(gen.get_country({}, std::uint64_t{1}).to_json() ==
            R"({"cca2":"ET","cca3":"ETH","ccn3":"231","name_common":"Ethiopia",)"
            R"("name_official":"Federal Democratic Republic of Ethiopia","capital":["Addis Ababa"],)"
            R"("region":"Africa","subregion":"Eastern Africa","continent":["Africa"],"latitude":8,)"
            R"("longitude":38,"area":1104300,"population":111652998,"landlocked":true,"independent":true,)"
            R"("un_member":true,"languages":["Amharic"],)"
            R"("language_shares":[{"name":"English","percent":43},{"name":"Amharic","percent":33},)"
            R"({"name":"Oromo","percent":32},{"name":"Somali","percent":6},{"name":"Tigrinya","percent":6},)"
            R"({"name":"Sidamo","percent":3.5},{"name":"Wolaytta","percent":1.8},{"name":"Afar","percent":1.4}],)"
            R"("currency_code":"ETB","currency_name":"Ethiopian birr",)"
            R"("currency_symbol":"Br","borders":["DJI","ERI","KEN","SOM","SSD","SDN"],"timezones":["UTC+03:00"],)"
            R"("driving_side":"right","tld":".et","idd_root":"+2","idd_suffix":"51","demonym_m":"Ethiopian",)"
            R"("demonym_f":"Ethiopian","flag_emoji":")" "\xF0\x9F\x87\xAA\xF0\x9F\x87\xB9" R"(","income_level":"Not classified",)"
            R"("start_of_week":"monday","signature":{"seed":"0000000000000001","data":"ba0f78d3ad63fb46"}})");
    REQUIRE(gen.get_country(dasmig::country_request{.region = "Europe"}, 3).cca2 == "PL");
    REQUIRE(gen.get_country(dasmig::country_request{.weighted = false}, 2).cca2 == "NO");
    REQUIRE(gen.get_country(dasmig::country_request{.region = "Asia", .weighted = false}, 4).cca2 ==
            "PK");

    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint64_t s = 0; s < 10000; ++s)
    {
        for (const char c : gen.get_country({}, s).to_json())
        {
            h ^= static_cast<unsigned char>(c);
            h *= 0x100000001b3ULL;
        }
    }
    REQUIRE(h == 0x6f80b11ecf4085e5ULL);
}

// ---------------------------------------------------------------------------
// Languages and demonyms
// ---------------------------------------------------------------------------

TEST_CASE("languages are listed, shared out and drawn", "[languages]")
{
    auto gen = make_loaded_generator();

    const auto br = gen.find_country("BR");
    REQUIRE(br.language_list() == std::vector<std::string>{"Portuguese"});
    REQUIRE(br.primary_language() == "Portuguese");
    const auto shares = br.language_distribution();
    REQUIRE(shares.size() >= 2);
    REQUIRE(shares.front().name == "Portuguese");
    REQUIRE(shares.front().percent > 80.0);
    REQUIRE(std::is_sorted(shares.begin(), shares.end(),
                           [](const auto& a, const auto& b) { return a.percent > b.percent; }));

    // A country with several official languages lists them all.
    REQUIRE(gen.find_country("CH").language_list().size() >= 3);

    // Every country in the data: shares parse, are in range and sorted, and the primary language is one of them.
    std::size_t with_shares = 0;
    for (const auto& c : gen.get_country_batch({}, dasmig::core::batch_options{.count = 2000}, std::uint64_t{7}))
    {
        const auto list = c.language_distribution();
        if (list.empty()) continue;
        ++with_shares;
        REQUIRE(c.primary_language() == list.front().name);
        for (const auto& share : list)
        {
            REQUIRE(!share.name.empty());
            REQUIRE(share.percent > 0.0);
            REQUIRE(share.percent <= 100.0);
        }
    }
    REQUIRE(with_shares > 1900);

    // Drawn in proportion to the shares, and a pure function of the seed.
    std::map<std::string, int> counts;
    for (std::uint64_t seed = 0; seed < 4000; ++seed) ++counts[gen.get_language("CH", seed)];
    REQUIRE(counts.size() >= 3);
    REQUIRE(counts["German"] > counts["Italian"]);
    REQUIRE(gen.get_language("CH", 42) == gen.get_language("CH", 42));
    REQUIRE_THROWS_AS(gen.get_language("XX", 1), std::invalid_argument);
}

TEST_CASE("a country without shares falls back to its languages", "[languages][extend]")
{
    auto gen = make_loaded_generator();
    const std::string header =
        "cca2\tcca3\tname_common\tregion\tlatitude\tlongitude\tarea\tpopulation\tlandlocked\tindependent"
        "\tun_member\tlanguages\tlanguage_shares\tdemonym_m\tdemonym_f\n";
    gen.extend_from_string(header + "ZZ\tZZZ\tZedland\tEurope\t0\t0\t1\t1000\t0\t1\t0\tZedish;Zorp\t\t\tZedlander\n",
                           "test:fallback", dasmig::core::layer_mode::override);
    const auto zz = gen.find_country("ZZ");
    REQUIRE(zz.language_distribution().empty());
    REQUIRE(zz.primary_language() == "Zedish");
    REQUIRE(gen.get_language("ZZ", 5) == "Zedish");
    REQUIRE(zz.demonym(dasmig::sex::male) == "Zedlander"); // no male form: the female one
    REQUIRE(zz.demonym(dasmig::sex::female) == "Zedlander");

    gen.extend_from_string(header + "ZY\tZYY\tZyland\tEurope\t0\t0\t1\t1000\t0\t1\t0\t\t\t\t\n",
                           "test:none", dasmig::core::layer_mode::override);
    REQUIRE(gen.find_country("ZY").primary_language().empty());
    REQUIRE_THROWS_AS(gen.get_language("ZY", 1), std::invalid_argument);
}

TEST_CASE("malformed language shares are rejected with their location", "[languages][errors]")
{
    auto gen = make_loaded_generator();
    const std::string header =
        "cca2\tcca3\tname_common\tregion\tlatitude\tlongitude\tarea\tpopulation\tlandlocked\tindependent"
        "\tun_member\tlanguage_shares\n";
    for (const char* bad : {"Zedish", "Zedish:0", "Zedish:101", "Zedish:x", ":50", "Zedish:50;Zorp"})
    {
        const auto row = std::string{"ZZ\tZZZ\tZedland\tEurope\t0\t0\t1\t1000\t0\t1\t0\t"} + bad + "\n";
        REQUIRE_THROWS_AS(gen.extend_from_string(header + row, "test:bad", dasmig::core::layer_mode::override),
                          dasmig::core::tsv_error);
    }
    REQUIRE_NOTHROW(gen.extend_from_string(header + "ZZ\tZZZ\tZedland\tEurope\t0\t0\t1\t1000\t0\t1\t0\tZedish:60;Zorp:5.5\n",
                                           "test:good", dasmig::core::layer_mode::override));
    REQUIRE(gen.find_country("ZZ").language_distribution().size() == 2);
}

TEST_CASE("demonyms follow the sex, falling back to the other form", "[languages]")
{
    auto gen = make_loaded_generator();
    const auto fr = gen.find_country("FR");
    REQUIRE(fr.demonym(dasmig::sex::male) == fr.demonym_m);
    REQUIRE(fr.demonym(dasmig::sex::female) == fr.demonym_f);
    REQUIRE(!fr.demonym(dasmig::sex::male).empty());
}

// ---------------------------------------------------------------------------
// JSON and signatures
// ---------------------------------------------------------------------------

TEST_CASE("to_json writes lists as arrays and the signature", "[json]")
{
    auto gen = make_loaded_generator();
    const auto br = gen.find_country("BR");
    const auto json = br.to_json();
    REQUIRE(json.find(R"("languages":["Portuguese"])") != std::string::npos);
    REQUIRE(json.find(R"("borders":["ARG","BOL",)") != std::string::npos);
    REQUIRE(br.signature().seed == 0);
    REQUIRE(br.signature().data == gen.data_hash());

    const auto c = gen.get_country({}, std::uint64_t{77});
    REQUIRE(c.seed() == 77);
    REQUIRE(c.signature().data == gen.data_hash());
    REQUIRE(c.to_json().find(R"("signature":{"seed":"000000000000004d")") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Extending data (rule 8)
// ---------------------------------------------------------------------------

namespace
{

const std::string country_header =
    "cca2\tcca3\tccn3\tname_common\tname_official\tcapital\tregion\tsubregion\tcontinent\t"
    "latitude\tlongitude\tarea\tpopulation\tlandlocked\tindependent\tun_member\tlanguages\t"
    "currency_code\tcurrency_name\tcurrency_symbol\tborders\ttimezones\tdriving_side\ttld\t"
    "idd_root\tidd_suffix\tdemonym_m\tdemonym_f\tflag_emoji\tincome_level\tstart_of_week\n";

std::string country_row(std::string_view cca2, std::string_view name, std::string_view region,
                        std::string_view population)
{
    return std::string{cca2} + "\tX" + std::string{cca2} + "\t999\t" + std::string{name} + "\t" +
           std::string{name} + "\tCapital\t" + std::string{region} + "\t\t" + std::string{region} +
           "\t10\t20\t1000\t" + std::string{population} +
           "\t0\t1\t0\tEnglish\t\t\t\t\tUTC\tright\t\t\t\t\t\t\t\tmonday\n";
}

} // namespace

TEST_CASE("country_request controls population weighting and uniform selection per call",
          "[request][weighting][region]")
{
    dasmig::cntg gen;
    gen.extend_from_string(
        country_header + country_row("AA", "Small A", "North", "0") +
            country_row("AB", "Large A", "North", "1000000") +
            country_row("BA", "Small B", "South", "0") +
            country_row("BB", "Large B", "South", "1000000"),
        "request.tsv");

    for (const auto& region : {std::optional<std::string>{},
                               std::optional<std::string>{"North"},
                               std::optional<std::string>{"South"}})
    {
        int weighted_large = 0;
        int uniform_large = 0;
        constexpr int draws = 2000;
        for (std::uint64_t seed = 0; seed < draws; ++seed)
        {
            const auto weighted = gen.get_country(
                dasmig::country_request{.region = region}, seed);
            const auto uniform = gen.get_country(
                dasmig::country_request{.region = region, .weighted = false}, seed);
            if (region)
            {
                REQUIRE(weighted.region == *region);
                REQUIRE(uniform.region == *region);
            }
            if (weighted.population > 0) ++weighted_large;
            if (uniform.population > 0) ++uniform_large;
        }
        REQUIRE(weighted_large > draws * 99 / 100);
        REQUIRE(uniform_large > draws * 40 / 100);
        REQUIRE(uniform_large < draws * 60 / 100);
    }

    gen.extend_from_string("cca2\tdisabled\nAB\t1\nBB\t1\n", "zero-population.tsv");
    REQUIRE(gen.get_country(dasmig::country_request{}, 42).population == 0);
    REQUIRE(gen.get_country(dasmig::country_request{.region = "North"}, 42).cca2 == "AA");
}

TEST_CASE("extend adds, overrides and removes countries", "[extend]")
{
    auto gen = make_loaded_generator();
    const auto count = gen.country_count();
    const auto base_hash = gen.data_hash();

    // A new country in a new region, and Brazil renamed.
    const auto& info = gen.extend_from_string(
        country_header + country_row("QQ", "Atlantis", "Ocean", "1000000") +
            country_row("BR", "Pindorama", "Americas", "200000000"),
        "atlantis.tsv");
    REQUIRE(info.added == 1);
    REQUIRE(info.replaced == 1);
    REQUIRE(gen.country_count() == count + 1);
    REQUIRE(gen.find_country("BR").name_common == "Pindorama");
    REQUIRE(gen.get_country({.region = "Ocean"}).cca2 == "QQ");
    REQUIRE(gen.data_hash() != base_hash);

    // A layer that only removes needs just the key.
    gen.extend_from_string("cca2\tdisabled\nQQ\t1\n", "sink.tsv");
    REQUIRE(gen.country_count() == count);
    REQUIRE_THROWS_AS(gen.get_country({.region = "Ocean"}), std::invalid_argument);
    REQUIRE(gen.layers().size() == 3);

    REQUIRE(gen.load(dasmig::dataset::lite));
    REQUIRE(gen.data_hash() == base_hash);
}

TEST_CASE("a bad layer is rejected and leaves the data unchanged", "[extend][errors]")
{
    auto gen = make_loaded_generator();
    const auto hash = gen.data_hash();
    const auto count = gen.country_count();

    REQUIRE_THROWS_WITH(gen.extend_from_string(country_header + country_row("QQ", "Atlantis", "Ocean", "many"),
                                               "bad.tsv"),
                        "bad.tsv:2: column 'population': expected a number, got 'many'");
    std::string bad_lat = country_header + country_row("QQ", "Atlantis", "Ocean", "5");
    bad_lat.replace(bad_lat.find("\t10\t20\t"), 7, "\t95\t20\t");
    REQUIRE_THROWS_WITH(gen.extend_from_string(bad_lat, "lat.tsv"),
                        "lat.tsv:2: column 'latitude': must be between -90 and 90");
    REQUIRE_THROWS_AS(gen.extend_from_string(country_header + country_row("BR", "Brazil", "Americas", "5"),
                                             "clash.tsv", dasmig::core::layer_mode::add),
                      dasmig::core::tsv_error);

    REQUIRE(gen.data_hash() == hash);
    REQUIRE(gen.country_count() == count);
    REQUIRE(gen.layers().size() == 1);
}

#ifdef DASMIG_COUNTRY_EMBED
TEST_CASE("compiled-in data needs no files and matches them", "[embed]")
{
    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "cntg_embed_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    dasmig::cntg gen;
    const bool loaded = gen.load(dasmig::dataset::lite);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);

    REQUIRE(loaded);
    REQUIRE(gen.layers().front().source == "embedded:lite/countries.tsv");
    REQUIRE(gen.data_hash() == 0xba0f78d3ad63fb46ULL); // same as the file
    REQUIRE(gen.get_country({}, std::uint64_t{1}).cca2 == "ET");
}
#endif

// ---------------------------------------------------------------------------
// Batch generation
// ---------------------------------------------------------------------------

TEST_CASE("country batch: items equal the single calls for any thread count", "[batch]")
{
    auto gen = load_lite_file();
    const auto request = dasmig::country_request{};
    const auto batch = gen.get_country_batch(request, {.count = 200, .threads = 1}, 77);
    REQUIRE(batch.size() == 200);
    for (std::uint64_t i = 0; i < batch.size(); ++i)
    {
        REQUIRE(batch[i].to_json() == gen.get_country(request, dasmig::core::item_seed(77, i)).to_json());
    }

    // A range is computed directly, on any number of threads.
    const auto tail = gen.get_country_batch(request, {.count = 100, .first = 100, .threads = 4}, 77);
    REQUIRE(tail.size() == 100);
    for (std::size_t i = 0; i < tail.size(); ++i)
    {
        REQUIRE(tail[i].to_json() == batch[100 + i].to_json());
    }
    for (const std::size_t threads : {2U, 7U, 0U})
    {
        const auto other = gen.get_country_batch(request, {.count = 200, .threads = threads}, 77);
        REQUIRE(other.size() == batch.size());
        for (std::size_t i = 0; i < other.size(); ++i)
        {
            REQUIRE(other[i].to_json() == batch[i].to_json());
        }
    }

    // Without a seed the batch still has the requested size.
    REQUIRE(gen.get_country_batch(request, {.count = 5}).size() == 5);
}

TEST_CASE("country batch: streaming gives the same items in order", "[batch]")
{
    auto gen = load_lite_file();
    const auto request = dasmig::country_request{};
    const auto batch = gen.get_country_batch(request, {.count = 300, .first = 10, .threads = 3}, 5);
    std::uint64_t next = 10;
    std::size_t seen = 0;
    gen.for_each_country(request, {.count = 300, .first = 10, .threads = 3}, 5,
                        [&](std::uint64_t index, const dasmig::country& item)
                        {
                            REQUIRE(index == next);
                            REQUIRE(item.to_json() == batch[seen].to_json());
                            ++next;
                            ++seen;
                        });
    REQUIRE(seen == batch.size());
}

TEST_CASE("country batch: known answer of 10,000 items", "[batch][kat]")
{
    auto gen = load_lite_file();
    const auto request = dasmig::country_request{};
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    gen.for_each_country(request, {.count = 10000, .threads = 4}, 1,
                        [&](std::uint64_t, const dasmig::country& item)
                        { hash = dasmig::core::hash_combine(hash, dasmig::core::fnv1a64(item.to_json())); });
    REQUIRE(hash == 0xa94ee4e0e96111f4ULL);
}
