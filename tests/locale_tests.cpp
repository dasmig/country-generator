#include "catch_amalgamated.hpp"
#include "../dasmig/countrygen.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

std::filesystem::path tier_dir(const char* tier)
{
    for (const auto* root : {"resources", "../resources", "country-generator/resources"})
    {
        const auto dir = std::filesystem::path{root} / tier;
        if (std::filesystem::is_regular_file(dir / "locales.tsv")) return dir;
    }
    FAIL("resources/" << tier << " with the locale tables not found");
    return {};
}

const dasmig::cntg& lite()
{
    static const dasmig::cntg gen = [] {
        dasmig::cntg g;
        g.load(tier_dir("lite"));
        return g;
    }();
    return gen;
}

dasmig::cntg lite_copy()
{
    dasmig::cntg g;
    g.load(tier_dir("lite"));
    return g;
}

dasmig::core::money money(std::int64_t minor, const char* code, unsigned digits)
{
    return dasmig::core::money::from_minor(minor, code, static_cast<std::uint8_t>(digits));
}

std::vector<std::string> keys(const std::vector<dasmig::holiday>& hs)
{
    std::vector<std::string> out;
    for (const auto& h : hs) out.push_back(dasmig::country_detail::iso_date(h.date) + " " + h.key);
    return out;
}

// "2024-04-12>2024-04-14 eid_al_fitr_holiday#4": the date, the observed day, the key and the day
// of a multi-day holiday after the first.
std::vector<std::string> listing(const std::vector<dasmig::holiday>& hs)
{
    std::vector<std::string> out;
    for (const auto& h : hs)
    {
        auto s = dasmig::country_detail::iso_date(h.date);
        if (h.observed) s += ">" + dasmig::country_detail::iso_date(*h.observed);
        s += " " + h.key;
        if (h.day > 1) s += "#" + std::to_string(h.day);
        out.push_back(std::move(s));
    }
    return out;
}

const dasmig::cntg& full()
{
    static const dasmig::cntg gen = [] {
        dasmig::cntg g;
        g.load(tier_dir("full"));
        return g;
    }();
    return gen;
}

dasmig::cntg full_copy()
{
    dasmig::cntg g;
    g.load(tier_dir("full"));
    return g;
}

// The line of a tier's table whose first cells are @p prefix (tab-separated).
std::string table_line(const char* tier, const char* file, const std::string& prefix)
{
    std::ifstream in{tier_dir(tier) / file, std::ios::binary};
    for (std::string line; std::getline(in, line);)
    {
        if (line.starts_with(prefix + "\t")) return line;
    }
    FAIL(file << ": no line starting " << prefix);
    return {};
}

// A tab-separated line with cell @p index (0-based) replaced.
std::string with_cell(const std::string& line, std::size_t index, const std::string& value)
{
    std::size_t start = 0;
    for (std::size_t i = 0; i < index; ++i) start = line.find('\t', start) + 1;
    const auto end = line.find('\t', start);
    return line.substr(0, start) + value + (end == std::string::npos ? "" : line.substr(end));
}

// The text of a table's header line.
std::string table_header(const char* tier, const char* file)
{
    std::ifstream in{tier_dir(tier) / file, std::ios::binary};
    for (std::string line; std::getline(in, line);)
    {
        if (!line.starts_with("#")) return line + "\n";
    }
    return {};
}

std::uint64_t fnv(std::uint64_t h, const std::string& s)
{
    for (const char c : s)
    {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

constexpr std::uint64_t fnv_basis = 0xcbf29ce484222325ULL;

// Draws n results with draw(seed) and checks each value's frequency against
// its weight, normalised, within 1% (absolute).
template <class Draw>
void check_frequencies(const std::map<std::string, double>& weights, std::uint64_t n, Draw draw)
{
    double total = 0;
    for (const auto& [value, w] : weights) total += w;
    std::map<std::string, std::uint64_t> counts;
    for (std::uint64_t seed = 0; seed < n; ++seed) ++counts[draw(seed)];
    for (const auto& [value, count] : counts)
    {
        INFO(value);
        REQUIRE(weights.contains(value));
    }
    for (const auto& [value, w] : weights)
    {
        INFO(value);
        REQUIRE(static_cast<double>(counts[value]) / static_cast<double>(n) == Catch::Approx(w / total).margin(0.01));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Nothing moved (design 1): the locale tables change none of get_country's answers
// ---------------------------------------------------------------------------

TEST_CASE("with the locale tables loaded, get_country's known answers are unchanged", "[locale][kat]")
{
    // The values of tests.cpp's known answers for countries.tsv alone.
    REQUIRE(lite().data_hash() == 0xba0f78d3ad63fb46ULL);
    dasmig::cntg only_countries;
    only_countries.load(tier_dir("lite") / "countries.tsv");
    REQUIRE(lite().get_country({}, std::uint64_t{1}).to_json() == only_countries.get_country({}, std::uint64_t{1}).to_json());
    REQUIRE(lite().get_country(dasmig::country_request{.region = "Europe"}, 3).cca2 == "PL");
    REQUIRE(lite().get_country(dasmig::country_request{.weighted = false}, 2).cca2 == "NO");

    std::uint64_t h = fnv_basis;
    for (std::uint64_t s = 0; s < 10000; ++s) h = fnv(h, lite().get_country({}, s).to_json());
    REQUIRE(h == 0x6f80b11ecf4085e5ULL);

    std::uint64_t batch = fnv_basis;
    lite().for_each_country({}, {.count = 10000, .threads = 4}, 1, [&](std::uint64_t, const dasmig::country& c) {
        batch = dasmig::core::hash_combine(batch, dasmig::core::fnv1a64(c.to_json()));
    });
    REQUIRE(batch == 0xa94ee4e0e96111f4ULL);
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

TEST_CASE("a tier directory loads every table; countries.tsv alone loads none", "[locale][load]")
{
    REQUIRE(lite().has_locale_data());
    REQUIRE(lite().country_count() == 195);

    dasmig::cntg only_countries;
    only_countries.load(tier_dir("lite") / "countries.tsv");
    REQUIRE(only_countries.country_count() == 195);
    REQUIRE_FALSE(only_countries.has_locale_data());
    REQUIRE_THROWS_AS(only_countries.find_locale("pt-BR"), std::runtime_error);
    REQUIRE_THROWS_AS(only_countries.get_time_zone({}, 1), std::runtime_error);
    REQUIRE_THROWS_AS(only_countries.holidays({.country = "BR", .year = 2024}), std::runtime_error);
    REQUIRE_THROWS_AS(only_countries.currency_digits("JPY"), std::runtime_error);

    // Loading a dataset tier finds the locale tables next to countries.tsv.
    dasmig::cntg by_tier;
    REQUIRE(by_tier.load(dasmig::dataset::lite));
    REQUIRE(by_tier.has_locale_data());
    REQUIRE(by_tier.find_locale("ja-JP").to_json() == lite().find_locale("ja-JP").to_json());
}

TEST_CASE("the full tier loads and agrees with lite where both have data", "[locale][load]")
{
    dasmig::cntg full;
    full.load(tier_dir("full"));
    REQUIRE(full.country_count() > lite().country_count());
    REQUIRE(full.find_locale("de-DE").format_date(2024y / 12 / 31, dasmig::date_style::long_) ==
            lite().find_locale("de-DE").format_date(2024y / 12 / 31, dasmig::date_style::long_));
    REQUIRE(keys(full.holidays({.country = "US", .year = 2024})) ==
            keys(lite().holidays({.country = "US", .year = 2024})));
}

#ifdef DASMIG_COUNTRY_EMBED_LOCALE
TEST_CASE("the compiled-in locale tables match the files", "[locale][embed]")
{
    dasmig::cntg embedded;
    REQUIRE(embedded.load(dasmig::dataset::lite));
    REQUIRE(embedded.find_locale("pt-BR").signature().data == lite().find_locale("pt-BR").signature().data);
}
#endif

#if defined(DASMIG_COUNTRY_EMBED) && !defined(DASMIG_COUNTRY_EMBED_LOCALE)
TEST_CASE("compiled-in countries alone take the locale tables from disk, or none", "[locale][embed]")
{
    // From the repository root the tables are on disk.
    dasmig::cntg here;
    REQUIRE(here.load(dasmig::dataset::lite));
    REQUIRE(here.has_locale_data());

    // From a directory with no resources/ near it, only the countries load.
    const auto cwd = std::filesystem::current_path();
    const auto empty = std::filesystem::temp_directory_path() / "cntg_no_resources" / "work";
    std::filesystem::create_directories(empty);
    std::filesystem::current_path(empty);
    dasmig::cntg elsewhere;
    const bool loaded = elsewhere.load(dasmig::dataset::lite);
    std::filesystem::current_path(cwd);
    REQUIRE(loaded);
    REQUIRE(elsewhere.country_count() == 195);
    REQUIRE_FALSE(elsewhere.has_locale_data());
    REQUIRE_THROWS_AS(elsewhere.get_locale({}, 1), std::runtime_error);
    REQUIRE(elsewhere.get_country({}, std::uint64_t{1}).to_json() == lite().get_country({}, std::uint64_t{1}).to_json());
}
#endif

// ---------------------------------------------------------------------------
// Locales
// ---------------------------------------------------------------------------

TEST_CASE("dates and times follow the locale's CLDR patterns", "[locale][format]")
{
    const auto d = 2024y / 12 / 31;
    const hh_mm_ss<seconds> t{15h + 4min + 5s};

    const auto br = lite().find_locale("pt-BR");
    REQUIRE(br.format_date(d) == "31/12/2024");
    REQUIRE(br.format_date(d, dasmig::date_style::medium) == "31 de dez. de 2024");
    REQUIRE(br.format_date(d, dasmig::date_style::full) == "terça-feira, 31 de dezembro de 2024");
    REQUIRE(br.format_time(t) == "15:04");

    const auto us = lite().find_locale("en-US");
    REQUIRE(us.format_date(d) == "12/31/24");
    REQUIRE(us.format_date(d, dasmig::date_style::medium) == "Dec 31, 2024");
    REQUIRE(us.format_date(d, dasmig::date_style::full) == "Tuesday, December 31, 2024");
    REQUIRE(us.format_time(t) == "3:04\xe2\x80\xafPM");

    REQUIRE(lite().find_locale("de-DE").format_date(d, dasmig::date_style::full) == "Dienstag, 31. Dezember 2024");
    REQUIRE(lite().find_locale("ja-JP").format_date(d, dasmig::date_style::full) ==
            "2024\xe5\xb9\xb4" "12\xe6\x9c\x88" "31\xe6\x97\xa5\xe7\x81\xab\xe6\x9b\x9c\xe6\x97\xa5");
    REQUIRE(lite().find_locale("fr-CA").format_time(t, dasmig::time_style::medium) == "15 h 04 min 05 s");
    REQUIRE(lite().find_locale("ja-JP").format_date(d, dasmig::date_style::medium) == "2024/12/31");

    // Arabic's right-to-left marks are kept, and its digits stay Latin (decision 4).
    REQUIRE(lite().find_locale("ar-SA").format_date(d) == "31\xe2\x80\x8f/12\xe2\x80\x8f/2024");

    // Two-digit and padded years, eras, and day periods in the morning.
    REQUIRE(us.format_date(2005y / 1 / 9) == "1/9/05");
    REQUIRE(us.format_time(hh_mm_ss<seconds>{0h + 7min}) == "12:07\xe2\x80\xaf" "AM");
    REQUIRE(lite().find_locale("de-DE").format_time(hh_mm_ss<seconds>{9h + 5min + 7s}, dasmig::time_style::medium) ==
            "09:05:07");

    REQUIRE_THROWS_AS(us.format_time(hh_mm_ss<seconds>{24h}), std::invalid_argument);
}

TEST_CASE("numbers and money follow the locale's symbols and patterns", "[locale][format]")
{
    const auto us = lite().find_locale("en-US");
    REQUIRE(us.format_number(1234567.891) == "1,234,567.891");
    REQUIRE(us.format_number(-2.675, 2) == "-2.68");   // half-even on the shortest text
    REQUIRE(us.format_number(2.665, 2) == "2.66");
    REQUIRE(us.format_number(-0.001, 2) == "0");
    REQUIRE(us.format_money(money(-123450, "USD", 2)) == "-$1,234.50");
    REQUIRE(us.format_money(money(123450, "CHF", 2)) == "CHF\xc2\xa0" "1,234.50");

    // German symbols, Indian grouping, the narrow no-break space of French, the apostrophe of Swiss German.
    REQUIRE(lite().find_locale("de-DE").format_number(1234567.891) == "1.234.567,891");
    REQUIRE(lite().find_locale("de-DE").format_number(0.5) == "0,5");
    REQUIRE(lite().find_locale("en-IN").format_number(1234567.891) == "12,34,567.891");
    REQUIRE(lite().find_locale("en-IN").format_number(999.0) == "999");
    REQUIRE(lite().find_locale("fr-FR").format_number(1234567.891) == "1\xe2\x80\xaf" "234\xe2\x80\xaf" "567,891");
    REQUIRE(lite().find_locale("fr-FR").format_money(money(-123450, "EUR", 2)) ==
            "-1\xe2\x80\xaf" "234,50\xc2\xa0\xe2\x82\xac");
    REQUIRE(lite().find_locale("de-CH").format_money(money(-123450, "CHF", 2)) == "CHF-1\xe2\x80\x99" "234.50");
    REQUIRE(lite().find_locale("de-DE").format_money(money(123450, "CHF", 2)) == "1.234,50\xc2\xa0" "CHF");

    // A negative subpattern with the sign after the number; a currency with no minor unit.
    REQUIRE(lite().find_locale("fy-NL").format_money(money(-123450, "EUR", 2)) == "\xe2\x82\xac\xc2\xa0" "1.234,50-");
    REQUIRE(lite().find_locale("ja-JP").format_money(money(-123450, "JPY", 0)) == "-\xef\xbf\xa5" "123,450");
}

TEST_CASE("find_locale accepts any case, underscores and a redundant script", "[locale][lookup]")
{
    REQUIRE(lite().find_locale("pt_latn_br").tag == "pt-BR");
    REQUIRE(lite().find_locale("PT-br").tag == "pt-BR");
    REQUIRE(lite().find_locale("SR-latn-rs").tag == "sr-Latn-RS");
    REQUIRE(lite().find_locale("pt-BR").signature().seed == 0);
    REQUIRE_THROWS_AS(lite().find_locale("xx-YY"), std::invalid_argument);
    REQUIRE_THROWS_AS(lite().find_locale("zh-TW"), std::invalid_argument);   // Taiwan is not in lite
}

TEST_CASE("locales lists a country's locales by share", "[locale][lookup]")
{
    const auto ch = lite().locales("ch");
    REQUIRE(ch.size() >= 4);
    REQUIRE(ch.front().tag == "de-CH");
    REQUIRE(std::ranges::is_sorted(ch, std::greater<>{}, &dasmig::locale::share));
    for (const auto& l : ch)
    {
        REQUIRE(l.country_code == "CH");
        REQUIRE(l.currency_code == "CHF");
        REQUIRE(l.currency_digits == 2);
    }
    REQUIRE_THROWS_AS(lite().locales("ZZ"), std::invalid_argument);
}

TEST_CASE("get_locale draws the country exactly as get_country does", "[locale][draw]")
{
    for (std::uint64_t seed = 0; seed < 500; ++seed)
    {
        const auto l = lite().get_locale({}, seed);
        REQUIRE(l.country_code == lite().get_country({}, seed).cca2);
        REQUIRE(l.signature().seed == seed);
        REQUIRE(l.to_json() == lite().get_locale({}, seed).to_json());
        REQUIRE(lite().get_locale({.weighted = false}, seed).country_code ==
                lite().get_country({.weighted = false}, seed).cca2);
    }
}

TEST_CASE("get_locale honours the country and the language", "[locale][draw]")
{
    std::set<std::string> tags;
    for (std::uint64_t seed = 0; seed < 400; ++seed)
    {
        const auto ch = lite().get_locale({.country = "CH"}, seed);
        REQUIRE(ch.country_code == "CH");
        tags.insert(ch.tag);
        const auto fr = lite().get_locale({.language = "FR"}, seed);
        REQUIRE(fr.language == "fr");
        const auto ca = lite().get_locale({.country = "CA", .language = "fr"}, seed);
        REQUIRE(ca.tag == "fr-CA");
    }
    REQUIRE(tags.size() >= 3);
    REQUIRE(tags.contains("de-CH"));
    REQUIRE_THROWS_AS(lite().get_locale({.language = "tlh"}, 1), std::invalid_argument);
    REQUIRE_THROWS_AS(lite().get_locale({.country = "JP", .language = "fr"}, 1), std::invalid_argument);
    REQUIRE_THROWS_AS(lite().get_locale({.country = "ZZ"}, 1), std::invalid_argument);
}

TEST_CASE("a drawn country with no locale is replaced by one that has", "[locale][draw][extend]")
{
    auto gen = lite_copy();
    std::string text = "country\ttag\tdisabled\n";
    for (const auto& l : gen.locales("CN")) text += "CN\t" + l.tag + "\t1\n";
    (void)gen.extend_from_string(dasmig::country_table::locales, text, "no-china");
    REQUIRE(gen.locales("CN").empty());
    REQUIRE_THROWS_AS(gen.get_locale({.country = "CN"}, 1), std::invalid_argument);
    bool replaced = false;
    for (std::uint64_t seed = 0; seed < 300; ++seed)
    {
        const auto l = gen.get_locale({}, seed);
        REQUIRE(l.country_code != "CN");
        if (gen.get_country({}, seed).cca2 == "CN") replaced = true;
    }
    REQUIRE(replaced);
}

TEST_CASE("locale batches equal the single calls", "[locale][batch]")
{
    const auto batch = lite().get_locale_batch({}, {.count = 100, .first = 5, .threads = 3}, 9);
    REQUIRE(batch.size() == 100);
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        REQUIRE(batch[i].to_json() == lite().get_locale({}, dasmig::core::item_seed(9, 5 + i)).to_json());
    }
    std::size_t seen = 0;
    lite().for_each_locale({}, {.count = 100, .first = 5, .threads = 3}, 9,
                           [&](std::uint64_t, const dasmig::locale& l) { REQUIRE(l.to_json() == batch[seen++].to_json()); });
    REQUIRE(seen == 100);
}

TEST_CASE("a locale's JSON has its formats, symbols and currency", "[locale][json]")
{
    const auto json = lite().find_locale("pt-BR").to_json();
    for (const auto* part :
         {R"("tag":"pt-BR")", R"("currency":{"code":"BRL")", R"("digits":2)", R"("first_day":"sunday")",
          R"("measurement":"metric")", R"("signature":{"seed":"0000000000000000")"})
    {
        INFO(part);
        REQUIRE(json.find(part) != std::string::npos);
    }
    REQUIRE(std::string{lite().find_locale("pt-BR")} == "pt-BR");
}

// ---------------------------------------------------------------------------
// Time zones
// ---------------------------------------------------------------------------

TEST_CASE("find_time_zone reports the offset at the reference date and follows links", "[zone][lookup]")
{
    const auto kolkata = lite().find_time_zone("Asia/Calcutta");
    REQUIRE(kolkata.name == "Asia/Kolkata");
    REQUIRE(kolkata.country_code == "IN");
    REQUIRE(kolkata.utc_offset == 5h + 30min);
    REQUIRE_FALSE(kolkata.observes_dst);

    const auto summer = lite().find_time_zone("Europe/Berlin");
    REQUIRE(summer.utc_offset == 2h);
    REQUIRE(summer.abbreviation == "CEST");
    REQUIRE(summer.dst);
    const auto winter = lite().find_time_zone("Europe/Berlin", 2024y / 1 / 1);
    REQUIRE(winter.utc_offset == 1h);
    REQUIRE(winter.abbreviation == "CET");
    REQUIRE_FALSE(winter.dst);
    REQUIRE(winter.observes_dst);
    REQUIRE(winter.to_json().find(R"("utc_offset":"+01:00")") != std::string::npos);

    REQUIRE_THROWS_AS(lite().find_time_zone("Mars/Olympus_Mons"), std::invalid_argument);
    REQUIRE_THROWS_AS(lite().find_time_zone("Europe/Berlin", 2024y / 2 / 30), std::invalid_argument);
}

TEST_CASE("a time zone converts between UTC and local time", "[zone]")
{
    const auto berlin = lite().find_time_zone("Europe/Berlin");
    // The spring-forward instant of 2024: 01:00 UTC is 03:00 local.
    const sys_seconds change{sys_days{2024y / 3 / 31} + 1h};
    REQUIRE(berlin.to_local(change).time_since_epoch() - change.time_since_epoch() == 2h);
    REQUIRE(berlin.to_local(change - 1s).time_since_epoch() - (change - 1s).time_since_epoch() == 1h);
    const local_seconds noon{local_days{2024y / 7 / 1} + 12h};
    REQUIRE(berlin.to_utc(noon) == sys_seconds{sys_days{2024y / 7 / 1} + 10h});
}

TEST_CASE("time_zones lists a country's zones by population share", "[zone][lookup]")
{
    const auto br = lite().time_zones("BR");
    REQUIRE(br.size() > 10);
    REQUIRE(br.front().name == "America/Sao_Paulo");
    REQUIRE(std::ranges::is_sorted(br, std::greater<>{}, &dasmig::time_zone::share));
    double total = 0;
    for (const auto& z : br) total += z.share;
    REQUIRE(total == Catch::Approx(100.0).margin(0.5));
    REQUIRE(lite().time_zones("JP").size() == 1);
}

TEST_CASE("get_time_zone draws the country as get_country does, then a zone by share", "[zone][draw]")
{
    std::set<std::string> us;
    for (std::uint64_t seed = 0; seed < 500; ++seed)
    {
        const auto z = lite().get_time_zone({}, seed);
        REQUIRE(z.country_code == lite().get_country({}, seed).cca2);
        REQUIRE(z.to_json() == lite().get_time_zone({}, seed).to_json());
        us.insert(lite().get_time_zone({.country = "us"}, seed).name);
    }
    REQUIRE(us.contains("America/New_York"));
    REQUIRE(us.contains("America/Los_Angeles"));
    const auto batch = lite().get_time_zone_batch({}, {.count = 50}, 3);
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        REQUIRE(batch[i].to_json() == lite().get_time_zone({}, dasmig::core::item_seed(3, i)).to_json());
    }
}

// ---------------------------------------------------------------------------
// Holidays and currencies
// ---------------------------------------------------------------------------

TEST_CASE("holidays of the United States and Germany", "[holiday]")
{
    REQUIRE(keys(lite().holidays({.country = "US", .year = 2024})) ==
            std::vector<std::string>{"2024-01-01 new_year_s_day", "2024-01-15 martin_luther_king_jr_day",
                                     "2024-02-19 washington_s_birthday", "2024-05-27 memorial_day",
                                     "2024-06-19 juneteenth_national_independence_day", "2024-07-04 independence_day",
                                     "2024-09-02 labor_day", "2024-10-14 columbus_day", "2024-11-11 veterans_day",
                                     "2024-11-28 thanksgiving_day", "2024-12-25 christmas_day"});
    REQUIRE(keys(lite().holidays({.country = "DE", .year = 2024})) ==
            std::vector<std::string>{"2024-01-01 new_year_s_day", "2024-03-29 good_friday",
                                     "2024-04-01 easter_monday", "2024-05-01 labor_day",
                                     "2024-05-09 ascension_day", "2024-05-20 pentecost_monday",
                                     "2024-10-03 german_unity_day", "2024-12-25 christmas_day",
                                     "2024-12-26 second_day_of_christmas"});
    const auto us_optional = lite().holidays({.country = "US", .year = 2024, .optional = true});
    REQUIRE(std::ranges::any_of(us_optional, [](const dasmig::holiday& h) {
        return h.key == "christmas_eve" && h.kind == dasmig::holiday_kind::optional;
    }));
}

TEST_CASE("a holiday on a weekend is observed where the country moves it", "[holiday]")
{
    const auto gb = lite().holidays({.country = "GB", .year = 2021});
    const auto christmas = std::ranges::find(gb, std::string{"christmas_day"}, &dasmig::holiday::key);
    const auto boxing = std::ranges::find(gb, std::string{"boxing_day"}, &dasmig::holiday::key);
    REQUIRE(christmas != gb.end());
    REQUIRE(boxing != gb.end());
    REQUIRE(christmas->observed == 2021y / 12 / 27);
    REQUIRE(boxing->observed == 2021y / 12 / 28);

    // New Year's Day 2022 fell on a Saturday; the US observed it on 2021-12-31.
    const auto us = lite().holidays({.country = "US", .year = 2022});
    REQUIRE(us.front().key == "new_year_s_day");
    REQUIRE(us.front().observed == 2021y / 12 / 31);
    REQUIRE(lite().is_holiday("us", 2021y / 12 / 31));
    REQUIRE_FALSE(lite().is_holiday("US", 2021y / 12 / 30));
    REQUIRE_FALSE(lite().is_holiday("US", 2024y / 12 / 24));
    REQUIRE(lite().is_holiday("US", 2024y / 12 / 24, true));
}

TEST_CASE("holidays of several days, lunar dates and substitute days", "[holiday]")
{
    // Carnival is two days in Brazil, an optional day off.
    const auto br = lite().holidays({.country = "BR", .year = 2024, .optional = true});
    std::vector<unsigned> carnival;
    for (const auto& h : br)
    {
        if (h.key == "carnival") carnival.push_back(h.day);
    }
    REQUIRE(carnival == std::vector<unsigned>{1, 2});

    // Saudi Arabia's Eid holidays move off its Friday and Saturday weekend.
    const auto sa = lite().holidays({.country = "SA", .year = 2024});
    const auto eid = std::ranges::find_if(sa, [](const dasmig::holiday& h) {
        return h.key == "eid_al_fitr_holiday" && h.day == 4;
    });
    REQUIRE(eid != sa.end());
    REQUIRE(eid->date == 2024y / 4 / 12);
    REQUIRE(eid->observed == 2024y / 4 / 14);

    // China's days off of the Golden Week and Japan's equinox and substitute days.
    const auto cn = keys(lite().holidays({.country = "CN", .year = 2024}));
    REQUIRE(std::ranges::find(cn, "2024-10-04 day_off") != cn.end());
    const auto jp = keys(lite().holidays({.country = "JP", .year = 2024}));
    REQUIRE(std::ranges::find(jp, "2024-03-20 vernal_equinox_day") != jp.end());
    REQUIRE(std::ranges::find(jp, "2024-09-22 autumnal_equinox_day") != jp.end());
    REQUIRE(std::ranges::find(jp, "2024-05-06 substitute_holiday") != jp.end());

    // India's holidays follow the Hindu, Islamic and Gregorian calendars.
    const auto in = keys(lite().holidays({.country = "IN", .year = 2024}));
    REQUIRE(std::ranges::find(in, "2024-10-31 diwali") != in.end());
    REQUIRE(std::ranges::find(in, "2024-04-11 eid_al_fitr") != in.end());
    REQUIRE(std::ranges::find(in, "2024-01-26 republic_day") != in.end());
}

TEST_CASE("holidays carry the native and English names", "[holiday][json]")
{
    const auto br = lite().holidays({.country = "BR", .year = 2024});
    REQUIRE(br.front().name == "Confraterniza\xc3\xa7\xc3\xa3o Universal");
    REQUIRE(br.front().name_en == "Universal Fraternization Day");
    REQUIRE(br.front().to_json() ==
            R"({"date":"2024-01-01","observed":null,"key":"universal_fraternization_day",)"
            R"("name":"Confraterniza)" "\xc3\xa7\xc3\xa3" R"(o Universal","name_en":"Universal Fraternization Day",)"
            R"("kind":"public","day":1})");
}

TEST_CASE("holidays outside a country's span or data are errors", "[holiday][errors]")
{
    const auto span = lite().holiday_years("US");
    REQUIRE(span.from == 1970);
    REQUIRE(span.to == 2050);
    REQUIRE_THROWS_AS(lite().holidays({.country = "US", .year = 1969}), std::out_of_range);
    REQUIRE_THROWS_AS(lite().holidays({.country = "US", .year = 2051}), std::out_of_range);
    REQUIRE_THROWS_AS(lite().is_holiday("US", 2051y / 1 / 1), std::out_of_range);
    REQUIRE_THROWS_AS(lite().holidays({.country = "ZZ", .year = 2024}), std::invalid_argument);
}

TEST_CASE("currency minor units", "[currency]")
{
    REQUIRE(lite().currency_digits("JPY") == 0);
    REQUIRE(lite().currency_digits("usd") == 2);
    REQUIRE(lite().currency_digits("KWD") == 3);
    REQUIRE_THROWS_AS(lite().currency_digits("XYZ"), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

TEST_CASE("a holiday layer adds, overrides and removes holidays", "[holiday][extend]")
{
    auto gen = lite_copy();
    const auto before = gen.layers(dasmig::country_table::holidays).size();
    const auto& info = gen.extend_from_string(
        dasmig::country_table::holidays,
        "country\tkey\tname\tname_en\tkind\trule\tdays\tobserved\tfrom_year\tto_year\n"
        "BR\tcompany_day\tDia da Empresa\tCompany Day\tpublic\t08-13\t1\tnone\t2000\t2050\n"
        "BR\tcarnival\tCarnaval\tCarnival\tpublic\teaster-48\t2\tnone\t1970\t2050\n",
        "company");
    REQUIRE(gen.layers(dasmig::country_table::holidays).size() == before + 1);
    REQUIRE(info.source == "company");
    const auto br = keys(gen.holidays({.country = "BR", .year = 2024}));
    REQUIRE(std::ranges::find(br, "2024-08-13 company_day") != br.end());
    REQUIRE(std::ranges::find(br, "2024-02-12 carnival") != br.end());   // now public
    // The override replaces the carnival row from 1970 on: public in 1999 too.
    const auto br1999 = gen.holidays({.country = "BR", .year = 1999});
    REQUIRE(std::ranges::count(br1999, std::string{"carnival"}, &dasmig::holiday::key) == 2);

    (void)gen.extend_from_string(dasmig::country_table::holidays,
                                 "country\tkey\tfrom_year\tdisabled\nBR\tcompany_day\t2000\t1\n", "undo");
    const auto undone = keys(gen.holidays({.country = "BR", .year = 2024}));
    REQUIRE(std::ranges::find(undone, "2024-08-13 company_day") == undone.end());
}

TEST_CASE("a bad locale-table layer is rejected and leaves the data unchanged", "[extend][errors]")
{
    auto gen = lite_copy();
    const auto json = gen.find_locale("pt-BR").to_json();
    const auto layers = gen.layers(dasmig::country_table::locales).size();

    // A row of a country that is not loaded.
    REQUIRE_THROWS_AS(gen.extend_from_string(dasmig::country_table::holidays,
                                             "country\tkey\tname\tname_en\tkind\trule\tdays\tobserved\tfrom_year\tto_year\n"
                                             "TW\tx\tX\tX\tpublic\t01-01\t1\tnone\t2000\t2050\n",
                                             "bad"),
                      dasmig::core::tsv_error);
    // A malformed rule, a bad pattern, a currency with no currencies.tsv row.
    REQUIRE_THROWS_AS(gen.extend_from_string(dasmig::country_table::holidays,
                                             "country\tkey\tname\tname_en\tkind\trule\tdays\tobserved\tfrom_year\tto_year\n"
                                             "BR\tx\tX\tX\tpublic\t13-01\t1\tnone\t2000\t2050\n",
                                             "bad"),
                      dasmig::core::tsv_error);
    // Continuation names need a row above with the same key.
    REQUIRE_THROWS_AS(gen.extend_from_string(dasmig::country_table::holidays,
                                             "country\tkey\tname\tname_en\tkind\trule\tdays\tobserved\tfrom_year\tto_year\n"
                                             "BR\tnew_key\t\t\tpublic\t01-02\t1\tnone\t2000\t2050\n",
                                             "bad"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(dasmig::country_table::zone_links, "link\tzone\nMars/Base\tMars/Olympus\n",
                                             "bad"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(dasmig::country_table::currencies, "code\tnumeric\tdigits\nBRL\t986\t7\n",
                                             "bad"),
                      dasmig::core::tsv_error);

    REQUIRE(gen.find_locale("pt-BR").to_json() == json);
    REQUIRE(gen.layers(dasmig::country_table::locales).size() == layers);
    REQUIRE(keys(gen.holidays({.country = "BR", .year = 2024})) == keys(lite().holidays({.country = "BR", .year = 2024})));
}

TEST_CASE("removing a country leaves out its locale rows", "[extend]")
{
    auto gen = lite_copy();
    (void)gen.extend_from_string("cca2\tdisabled\nBR\t1\n", "no-brazil");
    REQUIRE_THROWS_AS(gen.find_country("BR"), std::invalid_argument);
    REQUIRE_THROWS_AS(gen.find_locale("pt-BR"), std::invalid_argument);
    REQUIRE_THROWS_AS(gen.holidays({.country = "BR", .year = 2024}), std::invalid_argument);
    REQUIRE(gen.find_locale("pt-PT").tag == "pt-PT");
}

TEST_CASE("locale and zone layers add, replace and remove rows", "[extend]")
{
    auto gen = lite_copy();

    // A locale added to a country, from another country's row; one replaced; one removed.
    auto en_br = table_line("lite", "locales.tsv", "GB\ten-GB");
    en_br.replace(0, 8, "BR\ten-BR");
    const auto pt_br = with_cell(table_line("lite", "locales.tsv", "BR\tpt-BR"), 5, "99.5");   // share
    (void)gen.extend_from_string(dasmig::country_table::locales,
                                 table_header("lite", "locales.tsv") + en_br + "\n" + pt_br + "\n", "locales");
    const auto br = gen.locales("BR");
    REQUIRE(br.size() == 2);
    REQUIRE(br.front().tag == "pt-BR");
    REQUIRE(br.front().share == 99.5);
    REQUIRE(br.back().tag == "en-BR");
    REQUIRE(br.back().currency_code == "GBP");   // the copied row's
    REQUIRE(gen.find_locale("en-BR").format_date(2024y / 12 / 31) == "31/12/2024");
    (void)gen.extend_from_string(dasmig::country_table::locales, "country\ttag\tdisabled\nBR\ten-BR\t1\n", "undo");
    REQUIRE_THROWS_AS(gen.find_locale("en-BR"), std::invalid_argument);
    REQUIRE(gen.locales("BR").size() == 1);

    // A zone added to another country; a zone's rules replaced; a link added and one removed.
    const auto header = table_header("lite", "zones.tsv");
    auto seoul = table_line("lite", "zones.tsv", "JP\tAsia/Tokyo");
    seoul.replace(0, 2, "KR");
    (void)gen.extend_from_string(dasmig::country_table::zones,
                                 header + seoul + "\nJP\tAsia/Tokyo\t100\ttzdata\t946684800/36000/JST/s\tJST-10\n",
                                 "zones");
    REQUIRE(gen.time_zones("KR").size() == 2);
    REQUIRE(gen.find_time_zone("Asia/Tokyo").utc_offset == 10h);
    REQUIRE(gen.find_time_zone("Asia/Tokyo").country_code == "JP");
    (void)gen.extend_from_string(dasmig::country_table::zone_links,
                                 "link\tzone\tdisabled\nAsia/Edo\tAsia/Tokyo\t\nAsia/Calcutta\tAsia/Kolkata\t1\n", "links");
    REQUIRE(gen.find_time_zone("Asia/Edo").name == "Asia/Tokyo");
    REQUIRE_THROWS_AS(gen.find_time_zone("Asia/Calcutta"), std::invalid_argument);
    (void)gen.extend_from_string(dasmig::country_table::zones, "country\tzone\tdisabled\nKR\tAsia/Tokyo\t1\n", "undo");
    REQUIRE(gen.time_zones("KR").size() == 1);

    // A currency added, one replaced and the added one removed.
    (void)gen.extend_from_string(dasmig::country_table::currencies,
                                 "code\tnumeric\tdigits\tcash_digits\nXTS\t963\t2\t\nKWD\t414\t2\t\n", "currencies");
    REQUIRE(gen.currency_digits("XTS") == 2);
    REQUIRE(gen.currency_digits("KWD") == 2);
    REQUIRE(gen.find_locale("ar-KW").currency_digits == 2);
    (void)gen.extend_from_string(dasmig::country_table::currencies, "code\tdisabled\nXTS\t1\n", "undo");
    REQUIRE_THROWS_AS(gen.currency_digits("XTS"), std::invalid_argument);
    // A currency a locale uses cannot go.
    REQUIRE_THROWS_AS(gen.extend_from_string(dasmig::country_table::currencies, "code\tdisabled\nUSD\t1\n", "bad"),
                      dasmig::core::tsv_error);
    REQUIRE(gen.currency_digits("USD") == 2);
}

TEST_CASE("event and span layers correct one country's holidays", "[holiday][extend]")
{
    auto gen = lite_copy();
    const auto eid = [&](const char* cc) {
        for (const auto& h : gen.holidays({.country = cc, .year = 2024}))
        {
            if (h.key == "eid_al_fitr") return dasmig::country_detail::iso_date(h.date);
        }
        return std::string{};
    };
    REQUIRE(eid("IN") == "2024-04-11");

    // An override of the shared date for one country moves only that country's holiday. India's
    // rule is the day after the shared date (event:eid_al_fitr+1).
    (void)gen.extend_from_string(dasmig::country_table::holiday_events, "event\tcountry\tdate\neid_al_fitr\tIN\t2024-04-09\n",
                                 "sighting");
    const auto in = keys(gen.holidays({.country = "IN", .year = 2024}));
    REQUIRE(std::ranges::find(in, "2024-04-10 eid_al_fitr") != in.end());
    REQUIRE(std::ranges::find(in, "2024-04-11 eid_al_fitr") == in.end());
    for (const char* cc : {"PK", "BD", "EG"})
    {
        REQUIRE(keys(gen.holidays({.country = cc, .year = 2024})) == keys(lite().holidays({.country = cc, .year = 2024})));
    }
    (void)gen.extend_from_string(dasmig::country_table::holiday_events,
                                 "event\tcountry\tdate\tdisabled\neid_al_fitr\tIN\t2024-04-09\t1\n", "undo");
    REQUIRE(eid("IN") == "2024-04-11");

    // A shorter span.
    (void)gen.extend_from_string(dasmig::country_table::holiday_years, "country\tfrom_year\tto_year\nIN\t2010\t2030\n",
                                 "span");
    REQUIRE(gen.holiday_years("IN").from == 2010);
    REQUIRE_THROWS_AS(gen.holidays({.country = "IN", .year = 2031}), std::out_of_range);
    REQUIRE_NOTHROW(gen.holidays({.country = "IN", .year = 2030}));
}

TEST_CASE("a bad row is rejected with its line and column", "[extend][errors]")
{
    auto gen = lite_copy();
    const auto position = [&](dasmig::country_table t, const std::string& text) {
        try
        {
            (void)gen.extend_from_string(t, text, "bad");
        }
        catch (const dasmig::core::tsv_error& e)
        {
            return std::to_string(e.line()) + ":" + e.column();
        }
        return std::string{"accepted"};
    };
    const std::string holidays = "country\tkey\tname\tname_en\tkind\trule\tdays\tobserved\tfrom_year\tto_year\n";
    const std::string good = "BR\tok\tOk\tOk\tpublic\t01-02\t1\tnone\t2000\t2050\n";
    REQUIRE(position(dasmig::country_table::holidays, holidays + good + "BR\tx\tX\tX\tpublic\teaster+x\t1\tnone\t2000\t2050\n") ==
            "3:rule");
    REQUIRE(position(dasmig::country_table::holidays, holidays + good + "BR\tx\tX\tX\tpublic\t01-02\t1\tsat>xyz\t2000\t2050\n") ==
            "3:observed");
    REQUIRE(position(dasmig::country_table::holidays, holidays + good + "BR\tx\tX\tX\tpublic\t01-02\t11\tnone\t2000\t2050\n") ==
            "3:days");
    REQUIRE(position(dasmig::country_table::holidays, holidays + good + "BR\tx\tX\tX\tpublic\tevent:nope\t1\tnone\t2000\t2050\n") ==
            "3:rule");
    REQUIRE(position(dasmig::country_table::holidays, holidays + "BR\tX-Y\tX\tX\tpublic\t01-02\t1\tnone\t2000\t2050\n") ==
            "2:key");
    REQUIRE(position(dasmig::country_table::zones,
                     "country\tzone\tshare\tbasis\tperiods\tposix\nJP\tAsia/Tokyo\t100\ttzdata\t946684800/32400/JST/s\tJST\n") ==
            "2:periods");   // the rules are parsed together and reported on periods
    REQUIRE(position(dasmig::country_table::holiday_events, "event\tcountry\tdate\neid_al_fitr\tIN\t2024-02-30\n") == "2:date");
    REQUIRE(position(dasmig::country_table::holiday_years, "country\tfrom_year\tto_year\nIN\t2030\t2010\n") == "2:to_year");
    const auto pt_br = with_cell(table_line("lite", "locales.tsv", "BR\tpt-BR"), 7, "dd/MM/Q");   // date_short
    REQUIRE(position(dasmig::country_table::locales, table_header("lite", "locales.tsv") + pt_br + "\n") == "2:date_short");
    REQUIRE(keys(gen.holidays({.country = "BR", .year = 2024})) == keys(lite().holidays({.country = "BR", .year = 2024})));
}

// ---------------------------------------------------------------------------
// Known answers (design 13.1)
// ---------------------------------------------------------------------------

TEST_CASE("known answers of get_locale", "[locale][kat]")
{
    REQUIRE(lite().get_locale({}, 1).to_json() ==
            R"({"tag":"ti-ET","language":"ti","script":"","language_name":"Tigrinya","country":"ET",)"
            R"("share":6,"basis":"cldr","formats":{"date":{"short":"M/d/yy","medium":"MMM d, y",)"
            R"("long":"MMMM d, y","full":"EEEE)" "\xe1\x8d\xa3" R"( d MMMM y"},"time":{"short":"h:mm a",)"
            R"("medium":"h:mm:ss a"},"number":"#,##0.###","currency":")" "\xc2\xa4" R"(#,##0.00"},)"
            R"("symbols":{"decimal":".","group":",","minus":"-"},"currency":{"code":"ETB","symbol":"Br","digits":2},)"
            R"("first_day":"sunday","weekend":["saturday","sunday"],"measurement":"metric",)"
            R"("signature":{"seed":"0000000000000001","data":"7e3c1fb013e6e881"}})");
    REQUIRE(lite().get_locale({}, 2).to_json() ==
            R"({"tag":"fr-ML","language":"fr","script":"","language_name":"French","country":"ML",)"
            R"("share":17,"basis":"cldr","formats":{"date":{"short":"dd/MM/y","medium":"d MMM y",)"
            R"("long":"d MMMM y","full":"EEEE d MMMM y"},"time":{"short":"HH:mm","medium":"HH:mm:ss"},)"
            R"("number":"#,##0.###","currency":"#,##0.00)" "\xc2\xa0\xc2\xa4" R"("},"symbols":{"decimal":",",)"
            R"("group":")" "\xe2\x80\xaf" R"(","minus":"-"},"currency":{"code":"XOF","symbol":"F)" "\xe2\x80\xaf" R"(CFA",)"
            R"("digits":0},"first_day":"monday","weekend":["saturday","sunday"],"measurement":"metric",)"
            R"("signature":{"seed":"0000000000000002","data":"7e3c1fb013e6e881"}})");
    REQUIRE(lite().get_locale({}, 3).to_json() ==
            R"({"tag":"hi-IN","language":"hi","script":"","language_name":"Hindi","country":"IN",)"
            R"("share":41,"basis":"cldr","formats":{"date":{"short":"d/M/yy","medium":"d MMM y",)"
            R"("long":"d MMMM y","full":"EEEE, d MMMM y"},"time":{"short":"h:mm a","medium":"h:mm:ss a"},)"
            R"("number":"#,##,##0.###","currency":")" "\xc2\xa4" R"(#,##,##0.00"},"symbols":{"decimal":".",)"
            R"("group":",","minus":"-"},"currency":{"code":"INR","symbol":")" "\xe2\x82\xb9" R"(","digits":2},)"
            R"("first_day":"sunday","weekend":["sunday","sunday"],"measurement":"metric",)"
            R"("signature":{"seed":"0000000000000003","data":"7e3c1fb013e6e881"}})");
    REQUIRE(lite().get_locale({}, 4).to_json() ==
            R"({"tag":"ru-KZ","language":"ru","script":"","language_name":"Russian","country":"KZ",)"
            R"("share":72,"basis":"cldr","formats":{"date":{"short":"dd.MM.y",)"
            R"("medium":"d MMM y)" "\xe2\x80\xaf" R"(')" "\xd0\xb3" R"('.",)"
            R"("long":"d MMMM y)" "\xe2\x80\xaf" R"(')" "\xd0\xb3" R"('.",)"
            R"("full":"EEEE, d MMMM y)" "\xe2\x80\xaf" R"(')" "\xd0\xb3" R"('."},"time":{"short":"HH:mm",)"
            R"("medium":"HH:mm:ss"},"number":"#,##0.###","currency":"#,##0.00)" "\xc2\xa0\xc2\xa4" R"("},)"
            R"("symbols":{"decimal":",","group":")" "\xc2\xa0" R"(","minus":"-"},)"
            R"("currency":{"code":"KZT","symbol":")" "\xe2\x82\xb8" R"(","digits":2},)"
            R"("first_day":"monday","weekend":["saturday","sunday"],"measurement":"metric",)"
            R"("signature":{"seed":"0000000000000004","data":"7e3c1fb013e6e881"}})");
    REQUIRE(lite().get_locale({}, 5).to_json() ==
            R"({"tag":"ff-GN","language":"ff","script":"","language_name":"Fula","country":"GN",)"
            R"("share":26,"basis":"cldr","formats":{"date":{"short":"d/M/y","medium":"d MMM, y",)"
            R"("long":"d MMMM y","full":"EEEE d MMMM y"},"time":{"short":"HH:mm","medium":"HH:mm:ss"},)"
            R"("number":"#,##0.###","currency":"#,##0.00)" "\xc2\xa0\xc2\xa4" R"("},"symbols":{"decimal":",",)"
            R"("group":")" "\xc2\xa0" R"(","minus":"-"},"currency":{"code":"GNF","symbol":"FG","digits":0},)"
            R"("first_day":"monday","weekend":["saturday","sunday"],"measurement":"metric",)"
            R"("signature":{"seed":"0000000000000005","data":"7e3c1fb013e6e881"}})");

    std::uint64_t h = fnv_basis;
    for (std::uint64_t s = 0; s < 10000; ++s) h = fnv(h, lite().get_locale({}, s).to_json());
    REQUIRE(h == 0xbaf42467e956b7aeULL);
}

TEST_CASE("known answers of get_time_zone", "[zone][kat]")
{
    REQUIRE(lite().get_time_zone({}, 1).to_json() ==
            R"({"name":"Africa/Addis_Ababa","country":"ET","share":100,"basis":"tzdata",)"
            R"("utc_offset":"+03:00","abbreviation":"EAT","dst":false,"observes_dst":false,)"
            R"("signature":{"seed":"0000000000000001","data":"7e3c1fb013e6e881"}})");
    REQUIRE(lite().get_time_zone({}, 2).to_json() ==
            R"({"name":"Africa/Bamako","country":"ML","share":100,"basis":"tzdata","utc_offset":"+00:00",)"
            R"("abbreviation":"GMT","dst":false,"observes_dst":false,"signature":{"seed":"0000000000000002",)"
            R"("data":"7e3c1fb013e6e881"}})");
    REQUIRE(lite().get_time_zone({}, 3).to_json() ==
            R"({"name":"Asia/Kolkata","country":"IN","share":100,"basis":"tzdata","utc_offset":"+05:30",)"
            R"("abbreviation":"IST","dst":false,"observes_dst":false,"signature":{"seed":"0000000000000003",)"
            R"("data":"7e3c1fb013e6e881"}})");
    REQUIRE(lite().get_time_zone({}, 4).to_json() ==
            R"({"name":"Asia/Almaty","country":"KZ","share":76.3,"basis":"tzdata","utc_offset":"+06:00",)"
            R"("abbreviation":"+06","dst":false,"observes_dst":false,"signature":{"seed":"0000000000000004",)"
            R"("data":"7e3c1fb013e6e881"}})");
    REQUIRE(lite().get_time_zone({}, 5).to_json() ==
            R"({"name":"Africa/Conakry","country":"GN","share":100,"basis":"tzdata","utc_offset":"+00:00",)"
            R"("abbreviation":"GMT","dst":false,"observes_dst":false,"signature":{"seed":"0000000000000005",)"
            R"("data":"7e3c1fb013e6e881"}})");

    std::uint64_t h = fnv_basis;
    for (std::uint64_t s = 0; s < 10000; ++s) h = fnv(h, lite().get_time_zone({}, s).to_json());
    REQUIRE(h == 0x84dc11ac4b5b3f98ULL);
}

TEST_CASE("known answers of holidays: eight countries in 2024", "[holiday][kat]")
{
    const auto year = [](const char* cc) { return listing(lite().holidays({.country = cc, .year = 2024})); };
    using list = std::vector<std::string>;
    REQUIRE(year("BR") == list{"2024-01-01 universal_fraternization_day", "2024-03-29 good_friday",
                               "2024-04-21 tiradentes_day", "2024-05-01 worker_s_day", "2024-09-07 independence_day",
                               "2024-10-12 our_lady_of_aparecida", "2024-11-02 all_souls_day",
                               "2024-11-15 republic_proclamation_day",
                               "2024-11-20 national_day_of_zumbi_and_black_awareness", "2024-12-25 christmas_day"});
    REQUIRE(year("US") == list{"2024-01-01 new_year_s_day", "2024-01-15 martin_luther_king_jr_day",
                               "2024-02-19 washington_s_birthday", "2024-05-27 memorial_day",
                               "2024-06-19 juneteenth_national_independence_day", "2024-07-04 independence_day",
                               "2024-09-02 labor_day", "2024-10-14 columbus_day", "2024-11-11 veterans_day",
                               "2024-11-28 thanksgiving_day", "2024-12-25 christmas_day"});
    REQUIRE(year("GB") == list{"2024-01-01 new_year_s_day", "2024-03-29 good_friday", "2024-05-06 may_day",
                               "2024-05-27 spring_bank_holiday", "2024-12-25 christmas_day", "2024-12-26 boxing_day"});
    REQUIRE(year("DE") == list{"2024-01-01 new_year_s_day", "2024-03-29 good_friday", "2024-04-01 easter_monday",
                               "2024-05-01 labor_day", "2024-05-09 ascension_day", "2024-05-20 pentecost_monday",
                               "2024-10-03 german_unity_day", "2024-12-25 christmas_day",
                               "2024-12-26 second_day_of_christmas"});
    REQUIRE(year("SA") == list{"2024-02-22 founding_day_holiday", "2024-04-09 eid_al_fitr_holiday",
                               "2024-04-10 eid_al_fitr_holiday#2", "2024-04-11 eid_al_fitr_holiday#3",
                               "2024-04-12>2024-04-14 eid_al_fitr_holiday#4", "2024-06-15 day_of_arafah",
                               "2024-06-16>2024-06-19 eid_al_adha_holiday", "2024-06-17 eid_al_adha_holiday#2",
                               "2024-06-18 eid_al_adha_holiday#3", "2024-09-23 national_day_holiday"});
    REQUIRE(year("CN") == list{"2024-01-01 new_year_s_day", "2024-02-10>2024-02-13 chinese_new_year_spring_festival",
                               "2024-02-11>2024-02-14 chinese_new_year_spring_festival#2",
                               "2024-02-12 chinese_new_year_spring_festival#3", "2024-02-15 day_off",
                               "2024-02-16 day_off", "2024-04-04 tomb_sweeping_day", "2024-04-05 day_off",
                               "2024-05-01 labor_day", "2024-05-02 day_off", "2024-05-03 day_off",
                               "2024-06-10 dragon_boat_festival", "2024-09-16 day_off", "2024-09-17 mid_autumn_festival",
                               "2024-10-01 national_day", "2024-10-02 national_day#2", "2024-10-03 national_day#3",
                               "2024-10-04 day_off", "2024-10-07 day_off"});
    REQUIRE(year("IN") == list{"2024-01-26 republic_day", "2024-03-25 holi", "2024-03-29 good_friday",
                               "2024-04-11 eid_al_fitr", "2024-04-14 dr_b_r_ambedkar_s_birthday", "2024-04-17 ram_navami",
                               "2024-04-21 mahavira_s_birthday", "2024-05-23 buddha_purnima", "2024-06-17 eid_al_adha",
                               "2024-07-17 ashura", "2024-08-15 independence_day", "2024-08-26 janmashtami_vaishnava",
                               "2024-09-16 prophet_s_birthday", "2024-10-02 mahatma_gandhi_s_birthday",
                               "2024-10-12 dussehra", "2024-10-31 diwali", "2024-11-15 guru_nanak_s_birthday",
                               "2024-12-25 christmas"});
    REQUIRE(year("JP") == list{"2024-01-01 bank_holiday", "2024-01-01 new_year_s_day", "2024-01-02 bank_holiday",
                               "2024-01-03 bank_holiday", "2024-01-08 coming_of_age_day", "2024-02-11 foundation_day",
                               "2024-02-12 substitute_holiday", "2024-02-23 emperor_s_birthday",
                               "2024-03-20 vernal_equinox_day", "2024-04-29 showa_day", "2024-05-03 constitution_day",
                               "2024-05-04 greenery_day", "2024-05-05 children_s_day", "2024-05-06 substitute_holiday",
                               "2024-07-15 marine_day", "2024-08-11 mountain_day", "2024-08-12 substitute_holiday",
                               "2024-09-16 respect_for_the_aged_day", "2024-09-22 autumnal_equinox_day",
                               "2024-09-23 substitute_holiday", "2024-10-14 sports_day", "2024-11-03 culture_day",
                               "2024-11-04 substitute_holiday", "2024-11-23 labor_thanksgiving_day",
                               "2024-12-31 bank_holiday"});

    // The JSON of every entry, names and kinds included, with optional days and observances.
    std::uint64_t h = fnv_basis;
    for (const char* cc : {"BR", "US", "GB", "DE", "SA", "CN", "IN", "JP"})
    {
        for (const auto& x : lite().holidays({.country = cc, .year = 2024, .optional = true, .observances = true}))
        {
            h = fnv(h, x.to_json());
        }
    }
    REQUIRE(h == 0x92f69afd982aa9ffULL);
}

// ---------------------------------------------------------------------------
// Draws: agreement with get_country, shares, language narrowing, batches
// ---------------------------------------------------------------------------

TEST_CASE("over 10,000 seeds, the drawn country is get_country's", "[locale][zone][draw]")
{
    for (const bool weighted : {true, false})
    {
        std::uint64_t locale_differs = 0, zone_differs = 0;
        for (std::uint64_t seed = 0; seed < 10000; ++seed)
        {
            const auto cca2 = lite().get_country({.weighted = weighted}, seed).cca2;
            if (lite().get_locale({.weighted = weighted}, seed).country_code != cca2) ++locale_differs;
            if (lite().get_time_zone({.weighted = weighted}, seed).country_code != cca2) ++zone_differs;
        }
        INFO("weighted " << weighted);
        REQUIRE(locale_differs == 0);
        REQUIRE(zone_differs == 0);
    }
}

TEST_CASE("tags and zones are drawn by their shares, or uniformly", "[locale][zone][draw][shares]")
{
    constexpr std::uint64_t n = 100000;
    for (const char* cc : {"CA", "CH", "IN", "US"})
    {
        INFO(cc);
        std::map<std::string, double> tags, zones, every_tag, every_zone;
        for (const auto& l : lite().locales(cc))
        {
            tags[l.tag] = l.share;
            every_tag[l.tag] = 1;
        }
        for (const auto& z : lite().time_zones(cc))
        {
            zones[z.name] = z.share;
            every_zone[z.name] = 1;
        }
        check_frequencies(tags, n, [&](std::uint64_t s) { return lite().get_locale({.country = cc}, s).tag; });
        check_frequencies(every_tag, n,
                          [&](std::uint64_t s) { return lite().get_locale({.country = cc, .weighted = false}, s).tag; });
        check_frequencies(zones, n, [&](std::uint64_t s) { return lite().get_time_zone({.country = cc}, s).name; });
        check_frequencies(every_zone, n, [&](std::uint64_t s) {
            return lite().get_time_zone({.country = cc, .weighted = false}, s).name;
        });
    }
    // Shares overlap, so they are relative weights: fr-CA is 29 / (87 + 29) of Canada's draws, not 29%.
    std::uint64_t french = 0;
    for (std::uint64_t seed = 0; seed < n; ++seed) french += lite().get_locale({.country = "CA"}, seed).tag == "fr-CA";
    REQUIRE(static_cast<double>(french) / n == Catch::Approx(29.0 / 116.0).margin(0.01));
}

TEST_CASE("a language narrows the draw to its tags, and its countries by speakers", "[locale][draw][shares]")
{
    // Speakers of French by country: population times the share of its French locale.
    const auto doc = dasmig::core::tsv_document::load(tier_dir("lite") / "countries.tsv");
    std::map<std::string, double> speakers, countries;
    for (const auto& row : doc.rows())
    {
        const auto cc = row.get<std::string>("cca2");
        for (const auto& l : lite().locales(cc))
        {
            if (l.language != "fr") continue;
            const auto population = static_cast<double>(lite().find_country(cc).population);
            speakers[cc] = std::max(speakers[cc], population * l.share / 100.0);
            countries[cc] = 1;
        }
    }
    REQUIRE(speakers.size() > 20);
    check_frequencies(speakers, 100000, [&](std::uint64_t s) {
        const auto l = lite().get_locale({.language = "fr"}, s);
        if (l.language != "fr") return std::string{"not French: "} + l.tag;
        return l.country_code;
    });
    check_frequencies(countries, 100000,
                      [&](std::uint64_t s) { return lite().get_locale({.language = "fr", .weighted = false}, s).country_code; });
}

TEST_CASE("locale and zone batches: any thread count, and batches concatenate", "[locale][zone][batch]")
{
    const auto json = [](const auto& items) {
        std::vector<std::string> out;
        for (const auto& x : items) out.push_back(x.to_json());
        return out;
    };
    const dasmig::locale_request lr{.country = "CH"};
    const auto one = json(lite().get_locale_batch(lr, {.count = 200, .threads = 1}, 11));
    REQUIRE(json(lite().get_locale_batch(lr, {.count = 200, .threads = 4}, 11)) == one);
    REQUIRE(json(lite().get_locale_batch(lr, {.count = 200, .threads = 0}, 11)) == one);
    auto halves = json(lite().get_locale_batch(lr, {.count = 120, .threads = 2}, 11));
    const auto rest = json(lite().get_locale_batch(lr, {.count = 80, .first = 120, .threads = 3}, 11));
    halves.insert(halves.end(), rest.begin(), rest.end());
    REQUIRE(halves == one);

    const dasmig::time_zone_request zr{.country = "US"};
    const auto zones = json(lite().get_time_zone_batch(zr, {.count = 200, .threads = 1}, 12));
    REQUIRE(json(lite().get_time_zone_batch(zr, {.count = 200, .threads = 4}, 12)) == zones);
    auto zone_halves = json(lite().get_time_zone_batch(zr, {.count = 50, .threads = 2}, 12));
    const auto zone_rest = json(lite().get_time_zone_batch(zr, {.count = 150, .first = 50, .threads = 3}, 12));
    zone_halves.insert(zone_halves.end(), zone_rest.begin(), zone_rest.end());
    REQUIRE(zone_halves == zones);
    std::vector<std::string> streamed;
    lite().for_each_time_zone(zr, {.count = 200, .threads = 4}, 12,
                              [&](std::uint64_t, const dasmig::time_zone& z) { streamed.push_back(z.to_json()); });
    REQUIRE(streamed == zones);
}

// ---------------------------------------------------------------------------
// Zones (design 6 and 13.1)
// ---------------------------------------------------------------------------

TEST_CASE("offsets one second either side of the 2024 transitions", "[zone]")
{
    struct change
    {
        const char* zone;
        std::int64_t at;   // the instant, seconds since 1970 UTC
        seconds before, after;
    };
    for (const auto& c : {change{"Europe/Berlin", 1711846800, 1h, 2h}, change{"Europe/Berlin", 1729990800, 2h, 1h},
                          change{"America/New_York", 1710054000, -5h, -4h},
                          change{"America/New_York", 1730613600, -4h, -5h},
                          change{"Australia/Sydney", 1712419200, 11h, 10h},
                          change{"Australia/Sydney", 1728144000, 10h, 11h}})
    {
        for (const auto* gen : {&lite(), &full()})
        {
            INFO(c.zone << " at " << c.at);
            const auto z = gen->find_time_zone(c.zone);
            const sys_seconds t{seconds{c.at}};
            REQUIRE(z.to_local(t - 1s).time_since_epoch() - (t - 1s).time_since_epoch() == c.before);
            REQUIRE(z.to_local(t).time_since_epoch() - t.time_since_epoch() == c.after);
            REQUIRE(z.to_local(t + 1s).time_since_epoch() - (t + 1s).time_since_epoch() == c.after);
        }
    }
    for (const auto* gen : {&lite(), &full()})
    {
        const auto z = gen->find_time_zone("Australia/Sydney");
        REQUIRE(z.rules.transitions(sys_days{2024y / 1 / 1}, sys_days{2025y / 1 / 1}).size() == 2);
        // São Paulo has had no daylight time since 2019.
        const auto sp = gen->find_time_zone("America/Sao_Paulo");
        const auto since = sp.rules.transitions(sys_days{2019y / 1 / 1}, sys_days{2051y / 1 / 1});
        REQUIRE(since.size() == 1);
        REQUIRE(since.front().first == sys_seconds{sys_days{2019y / 2 / 17} + 2h});
        REQUIRE(since.front().second.offset == -3h);
        REQUIRE_FALSE(sp.observes_dst);
        REQUIRE(sp.to_local(sys_seconds{sys_days{2040y / 1 / 15}}).time_since_epoch() -
                    sys_seconds{sys_days{2040y / 1 / 15}}.time_since_epoch() ==
                -3h);
    }
}

TEST_CASE("lite's zones start in 2000 and full's in 1970", "[zone][tier]")
{
    const auto lite_berlin = lite().find_time_zone("Europe/Berlin");
    const auto full_berlin = full().find_time_zone("Europe/Berlin");
    REQUIRE(lite_berlin.rules.valid_from() == sys_seconds{sys_days{2000y / 1 / 1}});
    REQUIRE(full_berlin.rules.valid_from() == sys_seconds{sys_days{1970y / 1 / 1}});
    REQUIRE_THROWS_AS(lite_berlin.to_local(sys_seconds{sys_days{1999y / 12 / 31}}), std::out_of_range);
    // Germany had no summer time in 1979 and has had it every year since 1980.
    REQUIRE(full_berlin.to_local(sys_seconds{sys_days{1979y / 7 / 1}}).time_since_epoch() -
                sys_seconds{sys_days{1979y / 7 / 1}}.time_since_epoch() ==
            1h);
    REQUIRE(full_berlin.to_local(sys_seconds{sys_days{1980y / 7 / 1}}).time_since_epoch() -
                sys_seconds{sys_days{1980y / 7 / 1}}.time_since_epoch() ==
            2h);
    // From 2000 on, both tiers agree.
    for (int y = 2000; y <= 2037; ++y)
    {
        const sys_days from{year{y} / 1 / 1};
        const sys_days to{year{y + 1} / 1 / 1};
        REQUIRE(lite_berlin.rules.transitions(from, to) == full_berlin.rules.transitions(from, to));
    }
}

TEST_CASE("countries without IANA zones: GeoNames, then a fixed offset", "[zone][tier]")
{
    // Kosovo takes its places' zone (decision 13); Bouvet Island and Heard Island a fixed offset.
    const auto xk = full().time_zones("XK");
    REQUIRE(xk.size() == 1);
    REQUIRE(xk.front().name == "Europe/Belgrade");
    REQUIRE(xk.front().basis == "geonames");
    REQUIRE(full().find_time_zone("Europe/Belgrade").country_code == "RS");

    const auto bv = full().time_zones("BV");
    REQUIRE(bv.size() == 1);
    REQUIRE(bv.front().name == "Etc/GMT-1");   // IANA's inverted sign: one hour ahead of UTC
    REQUIRE(bv.front().basis == "fallback");
    REQUIRE(bv.front().utc_offset == 1h);
    REQUIRE_FALSE(bv.front().observes_dst);
    REQUIRE(full().time_zones("HM").front().name == "Etc/GMT-5");
    REQUIRE(full().get_time_zone({.country = "BV"}, 7).name == "Etc/GMT-1");
}

TEST_CASE("local times that repeat or are skipped", "[zone]")
{
    const auto berlin = lite().find_time_zone("Europe/Berlin");
    // 02:30 on 2024-10-27 happened twice: 00:30Z (CEST) and 01:30Z (CET).
    const local_seconds repeated{local_days{2024y / 10 / 27} + 2h + 30min};
    REQUIRE(berlin.to_utc(repeated) == sys_seconds{sys_days{2024y / 10 / 27} + 30min});
    REQUIRE(berlin.to_utc(repeated, dasmig::core::tz_choose::latest) == sys_seconds{sys_days{2024y / 10 / 27} + 1h + 30min});
    // 02:30 on 2024-03-31 never happened; it is read with the offset before the change.
    const local_seconds skipped{local_days{2024y / 3 / 31} + 2h + 30min};
    REQUIRE(berlin.to_utc(skipped) == sys_seconds{sys_days{2024y / 3 / 31} + 1h + 30min});
    REQUIRE(berlin.to_local(berlin.to_utc(skipped)) == local_seconds{local_days{2024y / 3 / 31} + 3h + 30min});
}

// ---------------------------------------------------------------------------
// Holidays (design 8 and 13.1)
// ---------------------------------------------------------------------------

namespace
{

// A full-tier generator whose British Indian Ocean Territory (a covered country with no
// holidays, a Saturday and Sunday weekend) has the given rows.
dasmig::cntg with_io_holidays(const std::string& rows)
{
    auto gen = full_copy();
    (void)gen.extend_from_string(dasmig::country_table::holidays,
                                 "country\tkey\tname\tname_en\tkind\trule\tdays\tobserved\tfrom_year\tto_year\n" + rows,
                                 "test");
    return gen;
}

std::string io_row(const char* key, const char* rule, const char* observed = "none", unsigned days = 1,
                   const char* kind = "public")
{
    return std::string{"IO\t"} + key + "\t" + key + "\t" + key + "\t" + kind + "\t" + rule + "\t" + std::to_string(days) +
           "\t" + observed + "\t1970\t2050\n";
}

} // namespace

TEST_CASE("every rule form", "[holiday][rules]")
{
    REQUIRE(full().holiday_years("IO").from == 1970);
    REQUIRE(full().holidays({.country = "IO", .year = 2024, .optional = true, .observances = true}).empty());

    const auto gen = with_io_holidays(io_row("a_fixed", "07-04") + io_row("b_nth", "11:4:thu") +
                                      io_row("c_last", "05:-1:mon") + io_row("d_on_or_after", "09-02>mon") +
                                      io_row("e_after", "09-03>mon") + io_row("f_on_or_before", "05-24<mon") +
                                      io_row("g_good_friday", "easter-2") + io_row("h_corpus_christi", "easter+60") +
                                      io_row("i_orthodox_monday", "orthodox+1") + io_row("j_eid_eve", "event:eid_al_fitr-1") +
                                      io_row("k_three_days", "12-24", "none", 3) + io_row("l_leap_day", "02-29"));
    REQUIRE(listing(gen.holidays({.country = "IO", .year = 2024})) ==
            std::vector<std::string>{"2024-02-29 l_leap_day", "2024-03-29 g_good_friday", "2024-04-09 j_eid_eve",
                                     "2024-05-06 i_orthodox_monday", "2024-05-20 f_on_or_before", "2024-05-27 c_last",
                                     "2024-05-30 h_corpus_christi", "2024-07-04 a_fixed", "2024-09-02 d_on_or_after",
                                     "2024-09-09 e_after", "2024-11-28 b_nth", "2024-12-24 k_three_days",
                                     "2024-12-25 k_three_days#2", "2024-12-26 k_three_days#3"});
    // No 29 February in 2023; Eid al-Fitr fell twice in 2000.
    REQUIRE(std::ranges::count(gen.holidays({.country = "IO", .year = 2023}), std::string{"l_leap_day"},
                               &dasmig::holiday::key) == 0);
    REQUIRE(keys(gen.holidays({.country = "IO", .year = 2000})) ==
            std::vector<std::string>{"2000-01-07 j_eid_eve", "2000-02-29 l_leap_day", "2000-04-21 g_good_friday",
                                     "2000-05-01 i_orthodox_monday", "2000-05-22 f_on_or_before", "2000-05-29 c_last",
                                     "2000-06-22 h_corpus_christi", "2000-07-04 a_fixed", "2000-09-04 d_on_or_after",
                                     "2000-09-04 e_after", "2000-11-23 b_nth", "2000-12-24 k_three_days",
                                     "2000-12-25 k_three_days", "2000-12-26 j_eid_eve", "2000-12-26 k_three_days"});
}

TEST_CASE("Western and Orthodox Easter, checked against design 8.3", "[holiday][rules]")
{
    const auto gen = with_io_holidays(io_row("easter", "easter") + io_row("orthodox", "orthodox"));
    const auto dates = [&](int y) { return keys(gen.holidays({.country = "IO", .year = y})); };
    REQUIRE(dates(1970) == std::vector<std::string>{"1970-03-29 easter", "1970-04-26 orthodox"});
    REQUIRE(dates(2000) == std::vector<std::string>{"2000-04-23 easter", "2000-04-30 orthodox"});
    REQUIRE(dates(2024) == std::vector<std::string>{"2024-03-31 easter", "2024-05-05 orthodox"});
    REQUIRE(dates(2025) == std::vector<std::string>{"2025-04-20 easter", "2025-04-20 orthodox"});
    REQUIRE(dates(2050) == std::vector<std::string>{"2050-04-10 easter", "2050-04-17 orthodox"});
}

TEST_CASE("every observed policy", "[holiday][observed]")
{
    // June to September 2024; the weekend is Saturday and Sunday.
    const auto gen = with_io_holidays(
        io_row("a_us_saturday", "06-01", "sat<fri,sun>mon") + io_row("b_sunday", "06-09", "sun>mon") +
        io_row("c_none", "06-15") + io_row("d_uk_saturday", "06-22", "sat>mon,sun>mon") +
        io_row("e_next_weekday", "06-29", "next_weekday") + io_row("f_weekend_next", "07-12", "weekend>next", 3) +
        io_row("g_tuesday", "07-23", "tue<mon,thu>fri") + io_row("h_thursday", "07-25", "tue<mon,thu>fri") +
        io_row("i_taken", "08-05") + io_row("j_moves_past", "08-04", "sun>mon") + io_row("k_taken", "08-12") +
        io_row("l_overlaps", "08-11", "sun>mon,overlap") + io_row("m_same_sunday", "08-18", "sun>mon") +
        io_row("n_same_sunday", "08-18", "sun>mon") + io_row("o_saturday", "08-24", "sat>mon,sun>mon") +
        io_row("p_sunday", "08-25", "sat>mon,sun>mon") + io_row("q_bank", "09-02", "none", 1, "bank") +
        io_row("r_other_kind", "09-01", "sun>mon"));
    REQUIRE(listing(gen.holidays({.country = "IO", .year = 2024})) ==
            std::vector<std::string>{
                "2024-06-01>2024-05-31 a_us_saturday", "2024-06-09>2024-06-10 b_sunday", "2024-06-15 c_none",
                "2024-06-22>2024-06-24 d_uk_saturday", "2024-06-29>2024-07-01 e_next_weekday",
                // A day of the span on the weekend adds a day after the span.
                "2024-07-12 f_weekend_next", "2024-07-13>2024-07-15 f_weekend_next#2",
                "2024-07-14>2024-07-16 f_weekend_next#3",
                // Bridge days, which need not be weekend days.
                "2024-07-23>2024-07-22 g_tuesday", "2024-07-25>2024-07-26 h_thursday",
                // A taken target moves on; with overlap it does not.
                "2024-08-04>2024-08-06 j_moves_past", "2024-08-05 i_taken", "2024-08-11>2024-08-12 l_overlaps",
                "2024-08-12 k_taken",
                // Two holidays on one Sunday share their day off (Albania 1994).
                "2024-08-18>2024-08-19 m_same_sunday", "2024-08-18>2024-08-19 n_same_sunday",
                // Saturday and Sunday both to Monday: the second takes Tuesday (UK 2021).
                "2024-08-24>2024-08-26 o_saturday", "2024-08-25>2024-08-27 p_sunday",
                // Kinds are moved separately: a bank holiday does not take a public day off.
                "2024-09-01>2024-09-02 r_other_kind", "2024-09-02 q_bank"});
}

TEST_CASE("the measured observed days of design 8.4", "[holiday][observed]")
{
    const auto us = listing(lite().holidays({.country = "US", .year = 2022}));
    REQUIRE(us.front() == "2022-01-01>2021-12-31 new_year_s_day");
    REQUIRE(std::ranges::find(us, "2022-06-19>2022-06-20 juneteenth_national_independence_day") != us.end());
    REQUIRE(std::ranges::find(us, "2022-12-25>2022-12-26 christmas_day") != us.end());
    // The 2022 holiday's observed day is in 2021, and 2021's list does not hold it.
    REQUIRE(listing(lite().holidays({.country = "US", .year = 2021})).back() == "2021-12-25>2021-12-24 christmas_day");
    REQUIRE(lite().is_holiday("US", 2021y / 12 / 24));
    REQUIRE(lite().is_holiday("US", 2021y / 12 / 31));
    REQUIRE(lite().is_holiday("US", 2022y / 1 / 1));
    // An observed day across New Year at the end of the span is not an error.
    REQUIRE_NOTHROW(lite().is_holiday("US", 2050y / 12 / 31));
    REQUIRE_NOTHROW(lite().is_holiday("US", 1970y / 1 / 1));

    // Albania 1994: Workers' Day and Orthodox Easter Sunday, both on Sunday 05-01, are both off on 05-02.
    REQUIRE(lite().is_holiday("AL", 1994y / 5 / 2));
    const auto al = lite().holidays({.country = "AL", .year = 1994});
    REQUIRE(std::ranges::count_if(al, [](const dasmig::holiday& h) {
                return h.date == 1994y / 5 / 2 || h.observed == 1994y / 5 / 2;
            }) == 2);
}

TEST_CASE("year spans and the years a row holds", "[holiday][years]")
{
    REQUIRE(lite().holiday_years("DE").from == 1991);
    REQUIRE(lite().holiday_years("IN").from == 2001);
    REQUIRE(lite().holiday_years("IN").to == 2035);
    REQUIRE_THROWS_AS(lite().holidays({.country = "DE", .year = 1990}), std::out_of_range);
    REQUIRE_THROWS_AS(lite().holidays({.country = "IN", .year = 2036}), std::out_of_range);
    // Ukraine is covered, with no holidays from 2023, under martial law.
    REQUIRE(lite().holiday_years("UA").to == 2050);
    REQUIRE(lite().holidays({.country = "UA", .year = 2024}).empty());
    REQUIRE_FALSE(lite().holidays({.country = "UA", .year = 2022}).empty());
    // Law changes: Juneteenth from 2021, the Day of Black Consciousness in Brazil from 2024.
    const auto has = [](const char* cc, int y, const char* key) {
        return std::ranges::count(lite().holidays({.country = cc, .year = y}), std::string{key}, &dasmig::holiday::key) > 0;
    };
    REQUIRE_FALSE(has("US", 2020, "juneteenth_national_independence_day"));
    REQUIRE(has("US", 2021, "juneteenth_national_independence_day"));
    REQUIRE_FALSE(has("BR", 2023, "national_day_of_zumbi_and_black_awareness"));
    REQUIRE(has("BR", 2024, "national_day_of_zumbi_and_black_awareness"));
}

TEST_CASE("optional days and observances are asked for", "[holiday][kinds]")
{
    const auto count = [](const std::vector<dasmig::holiday>& hs, dasmig::holiday_kind k) {
        return std::ranges::count(hs, k, &dasmig::holiday::kind);
    };
    const auto plain = lite().holidays({.country = "US", .year = 2024});
    const auto optional = lite().holidays({.country = "US", .year = 2024, .optional = true});
    const auto observances = lite().holidays({.country = "US", .year = 2024, .observances = true});
    const auto all = lite().holidays({.country = "US", .year = 2024, .optional = true, .observances = true});
    REQUIRE(count(plain, dasmig::holiday_kind::optional) == 0);
    REQUIRE(count(plain, dasmig::holiday_kind::observance) == 0);
    REQUIRE(count(optional, dasmig::holiday_kind::optional) > 0);
    REQUIRE(count(optional, dasmig::holiday_kind::observance) == 0);
    REQUIRE(count(observances, dasmig::holiday_kind::optional) == 0);
    const auto halloween = keys(observances);
    REQUIRE(std::ranges::find(halloween, "2024-10-31 halloween") != halloween.end());
    REQUIRE(std::ranges::find(halloween, "2024-05-12 mother_s_day") != halloween.end());
    REQUIRE(all.size() == plain.size() + static_cast<std::size_t>(count(optional, dasmig::holiday_kind::optional)) +
                              static_cast<std::size_t>(count(observances, dasmig::holiday_kind::observance)));
    // is_holiday never counts observances.
    REQUIRE_FALSE(lite().is_holiday("US", 2024y / 10 / 31, true));
    REQUIRE_THROWS_AS(lite().is_holiday("US", 2024y / 2 / 30), std::invalid_argument);
}
