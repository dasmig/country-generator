#include "catch_amalgamated.hpp"
#include "../dasmig/countrygen.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
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

} // namespace

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

    // Indian grouping, the narrow no-break space of French, the apostrophe of Swiss German.
    REQUIRE(lite().find_locale("en-IN").format_number(1234567.891) == "12,34,567.891");
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
