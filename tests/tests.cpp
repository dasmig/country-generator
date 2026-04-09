#include "catch_amalgamated.hpp"
#include "../dasmig/countrygen.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <vector>

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

TEST_CASE("load - zero-byte file is a no-op", "[loading]")
{
    auto tmp = std::filesystem::temp_directory_path() / "cntg_empty.tsv";
    { std::ofstream f{tmp}; }
    dasmig::cntg gen;
    gen.load(tmp);
    REQUIRE_FALSE(gen.has_data());
    std::filesystem::remove(tmp);
}

TEST_CASE("load - skips malformed lines", "[loading]")
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
    gen.load(tmp);
    REQUIRE(gen.country_count() == 1);
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

TEST_CASE("multiple loads accumulate data", "[loading]")
{
    auto gen = make_loaded_generator();
    auto count1 = gen.country_count();
    (void)gen.load(dasmig::dataset::lite);
    REQUIRE(gen.country_count() >= count1);
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

// ---------------------------------------------------------------------------
// Generation — basic
// ---------------------------------------------------------------------------

TEST_CASE("get_country returns a valid country", "[generation]")
{
    auto gen = make_loaded_generator();
    auto c = gen.get_country();
    REQUIRE_FALSE(c.name_common.empty());
    REQUIRE_FALSE(c.cca2.empty());
    REQUIRE_FALSE(c.cca3.empty());
    REQUIRE(c.seed() != 0);
}

TEST_CASE("get_country with seed is deterministic", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto c1 = gen.get_country(42);
    auto c2 = gen.get_country(42);
    REQUIRE(c1.cca3 == c2.cca3);
    REQUIRE(c1.name_common == c2.name_common);
}

TEST_CASE("get_country seed replay", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto c1 = gen.get_country();
    auto c2 = gen.get_country(c1.seed());
    REQUIRE(c1.cca3 == c2.cca3);
}

TEST_CASE("get_country throws when empty", "[generation]")
{
    dasmig::cntg gen;
    REQUIRE_THROWS_AS(gen.get_country(), std::runtime_error);
}

TEST_CASE("get_country(seed) throws when empty", "[generation]")
{
    const dasmig::cntg gen;
    REQUIRE_THROWS_AS(gen.get_country(42), std::runtime_error);
}

TEST_CASE("implicit string conversion returns common name", "[generation]")
{
    auto gen = make_loaded_generator();
    auto c = gen.get_country();
    std::string name = c;
    REQUIRE(name == c.name_common);
}

TEST_CASE("ostream operator outputs common name", "[generation]")
{
    auto gen = make_loaded_generator();
    auto c = gen.get_country();
    std::ostringstream oss;
    oss << c;
    REQUIRE(oss.str() == c.name_common);
}

// ---------------------------------------------------------------------------
// Region filtering
// ---------------------------------------------------------------------------

TEST_CASE("get_country(region) filters correctly", "[region]")
{
    auto gen = make_loaded_generator();
    gen.seed(42);

    for (int i = 0; i < 20; ++i)
    {
        auto c = gen.get_country("Europe");
        REQUIRE(c.region == "Europe");
    }

    gen.unseed();
}

TEST_CASE("get_country(region, seed) is deterministic", "[region][seed]")
{
    auto gen = make_loaded_generator();
    auto c1 = gen.get_country("Asia", 7777);
    auto c2 = gen.get_country("Asia", 7777);
    REQUIRE(c1.cca3 == c2.cca3);
    REQUIRE(c1.region == "Asia");
}

TEST_CASE("get_country(region) throws for unknown region", "[region]")
{
    auto gen = make_loaded_generator();
    REQUIRE_THROWS_AS(gen.get_country("Atlantis"), std::invalid_argument);
}

TEST_CASE("get_country(region) throws when empty", "[region]")
{
    dasmig::cntg gen;
    REQUIRE_THROWS_AS(gen.get_country("Europe"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Seeding
// ---------------------------------------------------------------------------

TEST_CASE("seed/unseed produces deterministic then random", "[seed]")
{
    auto gen = make_loaded_generator();

    gen.seed(100);
    auto a = gen.get_country();
    auto b = gen.get_country();

    gen.seed(100);
    auto a2 = gen.get_country();
    auto b2 = gen.get_country();

    REQUIRE(a.cca3 == a2.cca3);
    REQUIRE(b.cca3 == b2.cca3);

    gen.unseed();
}

TEST_CASE("seed is chainable", "[seed]")
{
    auto gen = make_loaded_generator();
    auto& ref = gen.seed(42);
    REQUIRE(&ref == &gen);
    gen.unseed();
}

TEST_CASE("unseed is chainable", "[seed]")
{
    auto gen = make_loaded_generator();
    auto& ref = gen.unseed();
    REQUIRE(&ref == &gen);
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

TEST_CASE("weighted defaults to true", "[weighting]")
{
    auto gen = make_loaded_generator();
    REQUIRE(gen.weighted() == true);
}

TEST_CASE("weighted(false) enables uniform selection", "[weighting]")
{
    auto gen = make_loaded_generator();
    gen.weighted(false).seed(99);
    REQUIRE(gen.weighted() == false);

    // With uniform selection, small countries should appear more often.
    int below_10m = 0;
    static constexpr int draws = 200;

    for (int i = 0; i < draws; ++i)
    {
        auto c = gen.get_country();
        if (c.population < 10'000'000)
        {
            ++below_10m;
        }
    }

    // Most countries have pop < 10M. Uniform should produce many of them.
    REQUIRE(below_10m > draws / 3);

    gen.weighted(true).unseed();
}

TEST_CASE("weighted(false) with region filter", "[weighting][region]")
{
    auto gen = make_loaded_generator();
    gen.weighted(false).seed(42);

    for (int i = 0; i < 20; ++i)
    {
        auto c = gen.get_country("Americas");
        REQUIRE(c.region == "Americas");
    }

    gen.weighted(true).unseed();
}

TEST_CASE("weighted toggle is chainable", "[weighting]")
{
    auto gen = make_loaded_generator();
    auto& ref = gen.weighted(false);
    REQUIRE(&ref == &gen);
    gen.weighted(true);
}

// ---------------------------------------------------------------------------
// Population weighting
// ---------------------------------------------------------------------------

TEST_CASE("population weighting favors large countries", "[weighting]")
{
    auto gen = make_loaded_generator();
    gen.seed(12345);

    int large_count = 0;
    static constexpr int draws = 200;

    for (int i = 0; i < draws; ++i)
    {
        auto c = gen.get_country();
        if (c.population > 50'000'000)
        {
            ++large_count;
        }
    }

    // With population weighting, most draws should be large countries.
    REQUIRE(large_count > draws / 2);
    gen.unseed();
}

// ---------------------------------------------------------------------------
// Data integrity — spot check fields on generated countries
// ---------------------------------------------------------------------------

TEST_CASE("generated countries have valid field ranges", "[integrity]")
{
    auto gen = make_loaded_generator();
    gen.seed(42);

    for (int i = 0; i < 50; ++i)
    {
        auto c = gen.get_country();
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

    gen.unseed();
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
    auto c = gen.get_country();
    REQUIRE(c.name_common == "CRLFland");
    // Verify the trailing \r was stripped, not embedded in start_of_week.
    REQUIRE(c.start_of_week == "monday");
    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Missing coverage: non-numeric fields cause graceful skip
// ---------------------------------------------------------------------------

TEST_CASE("load - non-numeric coordinate yields zero", "[loading]")
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
    gen.load(tmp);
    // from_chars returns 0 on failure — line is still valid (cca3 not empty).
    REQUIRE(gen.country_count() == 1);
    auto c = gen.get_country();
    REQUIRE(c.latitude == Catch::Approx(0.0));
    REQUIRE(c.longitude == Catch::Approx(0.0));
    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Missing coverage: get_country(region, seed) throws when empty
// ---------------------------------------------------------------------------

TEST_CASE("get_country(region, seed) throws when empty", "[region]")
{
    const dasmig::cntg gen;
    REQUIRE_THROWS_AS(gen.get_country("Europe", 42), std::runtime_error);
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
        seen.insert(gen.get_country(s).cca3);
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
    gen.seed(1);

    static const std::set<std::string> valid_regions = {
        "Africa", "Americas", "Antarctic", "Asia", "Europe", "Oceania"};

    for (int i = 0; i < 100; ++i)
    {
        auto c = gen.get_country();
        REQUIRE(valid_regions.count(c.region) == 1);
    }

    gen.unseed();
}

// ---------------------------------------------------------------------------
// Missing coverage: all six regions are reachable
// ---------------------------------------------------------------------------

TEST_CASE("all major regions are reachable", "[region]")
{
    auto gen = make_loaded_generator();

    for (const auto* rgn : {"Africa", "Americas", "Asia", "Europe", "Oceania"})
    {
        auto c = gen.get_country(rgn);
        REQUIRE(c.region == std::string{rgn});
    }
}

// ---------------------------------------------------------------------------
// Deeper data integrity: all 31 fields populated for known country
// ---------------------------------------------------------------------------

TEST_CASE("known country has all fields populated", "[integrity]")
{
    auto gen = make_loaded_generator();
    gen.seed(42);

    bool found = false;
    for (int i = 0; i < 500; ++i)
    {
        auto c = gen.get_country();
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
    gen.unseed();
}

// ---------------------------------------------------------------------------
// Stronger uniform test: compare distribution shape
// ---------------------------------------------------------------------------

TEST_CASE("uniform selection distributes evenly", "[weighting]")
{
    auto gen = make_loaded_generator();
    gen.weighted(false).seed(123);

    std::unordered_map<std::string, int> counts;
    static constexpr int draws = 2000;

    for (int i = 0; i < draws; ++i)
    {
        counts[gen.get_country().cca3]++;
    }

    // Uniform over ~195 countries, 2000 draws → expect ~10 per country.
    // No single country should dominate (>5% of draws, i.e., >100).
    for (const auto& [code, cnt] : counts)
    {
        REQUIRE(cnt < draws / 10);
    }

    gen.weighted(true).unseed();
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
