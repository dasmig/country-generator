// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

#ifndef DASMIG_COUNTRYGEN_HPP
#define DASMIG_COUNTRYGEN_HPP

#include "core.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#ifdef DASMIG_COUNTRY_EMBED
#include "countrygen_data.hpp"
#endif
#ifdef DASMIG_COUNTRY_EMBED_LOCALE
#include "countrygen_locale_data.hpp"
#endif

/// @file countrygen.hpp
/// @brief Country generator library — procedural country generation for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

DASMIG_CORE_REQUIRE(1, 4);

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
        const bool male = bio_sex == sex::male;
        const auto& wanted = male ? demonym_m : demonym_f;
        const auto& other = male ? demonym_f : demonym_m;
        return wanted.empty() ? other : wanted;
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
    [[nodiscard]] static bool for_each_share_(std::string_view text, const Visit& visit)
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

// -- Locales, time zones, currencies and holidays ------------------------------

/// @brief The length of a date format (CLDR `dateFormats`).
enum class date_style : std::uint8_t
{
    short_, ///< `dd/MM/y` in `pt-BR`.
    medium, ///< `d 'de' MMM 'de' y`.
    long_,  ///< `d 'de' MMMM 'de' y`.
    full    ///< `EEEE, d 'de' MMMM 'de' y`.
};

/// @brief The length of a time format (CLDR `timeFormats`; the long and full
///        ones name a zone and are not stored).
enum class time_style : std::uint8_t
{
    short_, ///< `HH:mm`.
    medium  ///< `HH:mm:ss`.
};

/// @brief A country's measurement system (CLDR `measurementData`).
enum class measurement : std::uint8_t
{
    metric, ///< Everywhere else.
    us,     ///< The United States and Liberia.
    uk      ///< The United Kingdom and, in CLDR 47, Myanmar.
};

/// @brief What kind of day a holiday is (python-holidays' categories).
enum class holiday_kind : std::uint8_t
{
    public_,   ///< A public holiday; always returned.
    bank,      ///< A bank holiday; always returned.
    optional,  ///< A day off for some (Brazil's Carnival, government-only days); on request.
    observance ///< A named day that is not a day off (Mother's Day); on request.
};

/// @brief The tables of the country data, for loading and extending it.
enum class country_table : std::uint8_t
{
    countries,      ///< `countries.tsv`: the countries.
    locales,        ///< `locales.tsv`: each country's locale tags and their CLDR formats.
    zones,          ///< `zones.tsv`: each country's IANA zones and their rules.
    zone_links,     ///< `zone_links.tsv`: old zone names and the zones they now name.
    currencies,     ///< `currencies.tsv`: the ISO 4217 minor unit of each currency.
    holidays,       ///< `holidays.tsv`: each country's holiday rules.
    holiday_events, ///< `holiday_events.tsv`: the dates of lunar and other calendar events.
    holiday_years   ///< `holiday_years.tsv`: the years each country's holidays are served for.
};

/// @brief The years a country's holidays are served for (holiday_years()).
struct year_span
{
    int from{}; ///< First year.
    int to{};   ///< Last year.

    /// @brief Equal when both years are.
    friend bool operator==(const year_span&, const year_span&) = default;
};

/// @cond INTERNAL
namespace country_detail
{

[[nodiscard]] inline std::string upper(std::string_view s)
{
    std::string out{s};
    for (auto& c : out)
    {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

[[nodiscard]] inline std::string lower(std::string_view s)
{
    std::string out{s};
    for (auto& c : out)
    {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

[[nodiscard]] constexpr bool is_letter(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// Weekday codes and names in std::chrono::weekday's c_encoding (Sunday 0).
inline constexpr std::array<std::string_view, 7> weekday_codes{"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
inline constexpr std::array<std::string_view, 7> weekday_names{"sunday",   "monday", "tuesday", "wednesday",
                                                               "thursday", "friday", "saturday"};

// Measurement system names, by dasmig::measurement.
inline constexpr std::array<std::string_view, 3> measurement_names{"metric", "us", "uk"};

[[nodiscard]] inline std::optional<std::chrono::weekday> parse_weekday(std::string_view code)
{
    for (unsigned i = 0; i < weekday_codes.size(); ++i)
    {
        if (weekday_codes.at(i) == code) return std::chrono::weekday{i};
    }
    return std::nullopt;
}

// Appends v (not negative) in base 10, padded with zeros to width digits.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline void append_padded(std::string& out, std::int64_t v, std::size_t width)
{
    std::array<char, 24> buf{};
    const auto res = std::to_chars(buf.data(), std::to_address(buf.end()), v);
    const auto n = static_cast<std::size_t>(res.ptr - buf.data());
    if (n < width) out.append(width - n, '0');
    out.append(buf.data(), n);
}

// "2024-04-10".
[[nodiscard]] inline std::string iso_date(std::chrono::year_month_day d)
{
    std::string out;
    append_padded(out, static_cast<int>(d.year()), 4);
    out += '-';
    append_padded(out, static_cast<unsigned>(d.month()), 2);
    out += '-';
    append_padded(out, static_cast<unsigned>(d.day()), 2);
    return out;
}

// "2024-04-10" -> the date; nullopt unless the text is exactly that form and a valid date.
[[nodiscard]] inline std::optional<std::chrono::year_month_day> parse_iso_date(std::string_view t)
{
    if (t.size() != 10 || t[4] != '-' || t[7] != '-') return std::nullopt;
    const auto y = core::detail::parse_number<int>(t.substr(0, 4));
    const auto m = core::detail::parse_number<unsigned>(t.substr(5, 2));
    const auto d = core::detail::parse_number<unsigned>(t.substr(8, 2));
    if (!y || !m || !d) return std::nullopt;
    const std::chrono::year_month_day ymd{std::chrono::year{*y}, std::chrono::month{*m}, std::chrono::day{*d}};
    if (!ymd.ok()) return std::nullopt;
    return ymd;
}

// -- LDML date and time patterns (design 5.1) ----------------------------------

// Walks a pattern: literal(text) for literal text (quotes resolved), field(letter, count) for each run
// of one letter. Returns false for an unterminated quote.
template <typename Literal, typename Field>
// NOLINTNEXTLINE(readability-function-cognitive-complexity): one pass over the pattern
bool walk_pattern(std::string_view p, const Literal& literal, const Field& field)
{
    std::size_t i = 0;
    while (i < p.size())
    {
        if (p[i] == '\'')
        {
            if (i + 1 < p.size() && p[i + 1] == '\'')
            {
                literal(std::string_view{"'"});
                i += 2;
                continue;
            }
            std::string text;
            std::size_t j = i + 1;
            for (;; ++j)
            {
                if (j == p.size()) return false;
                if (p[j] != '\'')
                {
                    text += p[j];
                    continue;
                }
                if (j + 1 < p.size() && p[j + 1] == '\'')
                {
                    text += '\'';
                    ++j;
                    continue;
                }
                break;
            }
            literal(std::string_view{text});
            i = j + 1;
            continue;
        }
        if (is_letter(p[i]))
        {
            std::size_t j = i;
            while (j < p.size() && p[j] == p[i]) ++j;
            field(p[i], static_cast<unsigned>(j - i));
            i = j;
            continue;
        }
        std::size_t j = i;
        while (j < p.size() && p[j] != '\'' && !is_letter(p[j])) ++j;
        literal(p.substr(i, j - i));
        i = j;
    }
    return true;
}

// Whether a field is one the formatter writes, for a date or a time pattern.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] constexpr bool supported_field(char c, unsigned n, bool time) noexcept
{
    if (time)
    {
        switch (c)
        {
        case 'a': return n <= 3;
        case 'h':
        case 'H':
        case 'm':
        case 's': return n <= 2;
        default: return false;
        }
    }
    switch (c)
    {
    case 'G': return n <= 3;
    case 'y':
    case 'M':
    case 'L':
    case 'E':
    case 'c': return n <= 4;
    case 'd': return n <= 2;
    default: return false;
    }
}

// Empty when the pattern is one the formatter writes; else what is wrong.
[[nodiscard]] inline std::string check_pattern(std::string_view p, bool time)
{
    std::string error;
    const bool closed = walk_pattern(
        p, [](std::string_view) {},
        [&](char c, unsigned n) {
            if (error.empty() && !supported_field(c, n, time))
            {
                error = "unsupported field '" + std::string(n, c) + "'";
            }
        });
    if (!closed) return "unterminated quote";
    return error;
}

// -- CLDR number patterns (design 5.2, 5.3) ------------------------------------

// Affix text keeps these marks where the pattern has ¤ and -.
inline constexpr char currency_mark = '\x01';
inline constexpr char minus_mark = '\x02';

struct number_format
{
    std::string positive_prefix, positive_suffix, negative_prefix, negative_suffix;
    std::size_t primary{0}, secondary{0};   // grouping sizes; 0 = no grouping
    std::size_t min_integer{1}, min_fraction{0}, max_fraction{0};
};

[[nodiscard]] constexpr bool number_char(char c) noexcept
{
    return c == '#' || c == '0' || c == ',' || c == '.';
}

// One subpattern into prefix, number body and suffix; false on an unsupported character.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters,readability-function-cognitive-complexity): one pass
inline bool split_subpattern(std::string_view p, std::string& prefix, std::string& body, std::string& suffix)
{
    constexpr std::string_view currency_sign = "\xC2\xA4";   // ¤
    int part = 0;   // 0 prefix, 1 body, 2 suffix
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        const char c = p[i];
        if (part == 1 && !number_char(c)) part = 2;
        if (part == 0 && number_char(c)) part = 1;
        if (part == 1)
        {
            body += c;
            continue;
        }
        auto& affix = part == 0 ? prefix : suffix;
        if (c == '\'')
        {
            if (i + 1 < p.size() && p[i + 1] == '\'')
            {
                affix += '\'';
                ++i;
                continue;
            }
            const auto end = p.find('\'', i + 1);
            if (end == std::string_view::npos) return false;
            affix += p.substr(i + 1, end - i - 1);
            i = end;
            continue;
        }
        if (p.substr(i, currency_sign.size()) == currency_sign)
        {
            affix += currency_mark;
            i += currency_sign.size() - 1;
            continue;
        }
        if (c == '-')
        {
            affix += minus_mark;
            continue;
        }
        if (c == '%' || c == '+' || c == '*' || c == '@' || c == ';' || c == currency_mark || c == minus_mark)
        {
            return false;
        }
        affix += c;
    }
    return !body.empty();
}

// Parses a pattern; empty error when it is one the formatter writes.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): one check per pattern part
[[nodiscard]] inline number_format parse_number_pattern(std::string_view text, bool currency, std::string& error)
{
    number_format f;
    error.clear();
    // The subpatterns split at a ';' outside quotes.
    std::size_t semi = std::string_view::npos;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\'') quoted = !quoted;
        if (text[i] == ';' && !quoted)
        {
            semi = i;
            break;
        }
    }
    std::string body;
    if (!split_subpattern(text.substr(0, semi), f.positive_prefix, body, f.positive_suffix))
    {
        error = "unsupported number pattern";
        return f;
    }
    const auto dot = body.find('.');
    const std::string_view integer = std::string_view{body}.substr(0, dot);
    const std::string_view fraction =
        dot == std::string::npos ? std::string_view{} : std::string_view{body}.substr(dot + 1);
    // The fraction is zeros then hashes ("00", "###", "0##"); the integer does not end in a comma.
    const auto hash = fraction.find('#');
    if (integer.empty() || integer.back() == ',' || fraction.find_first_not_of("0#") != std::string_view::npos ||
        (hash != std::string_view::npos && fraction.find('0', hash) != std::string_view::npos))
    {
        error = "unsupported number pattern";
        return f;
    }
    f.min_integer = static_cast<std::size_t>(std::ranges::count(integer, '0'));
    f.min_fraction = static_cast<std::size_t>(std::ranges::count(fraction, '0'));
    f.max_fraction = fraction.size();
    const auto last_comma = integer.rfind(',');
    if (last_comma != std::string_view::npos)
    {
        f.primary = integer.size() - last_comma - 1;
        const auto before = last_comma == 0 ? std::string_view::npos : integer.rfind(',', last_comma - 1);
        f.secondary = before == std::string_view::npos ? f.primary : last_comma - before - 1;
        if (f.primary == 0 || f.secondary == 0)
        {
            error = "unsupported number pattern";
            return f;
        }
    }
    if (semi != std::string_view::npos)
    {
        std::string negative_body;
        if (!split_subpattern(text.substr(semi + 1), f.negative_prefix, negative_body, f.negative_suffix))
        {
            error = "unsupported negative subpattern";
            return f;
        }
    }
    else
    {
        f.negative_prefix = minus_mark + f.positive_prefix;
        f.negative_suffix = f.positive_suffix;
    }
    const auto has_currency = [](const std::string& a, const std::string& b) {
        return a.contains(currency_mark) || b.contains(currency_mark);
    };
    if (currency != has_currency(f.positive_prefix, f.positive_suffix) ||
        currency != has_currency(f.negative_prefix, f.negative_suffix))
    {
        error = currency ? "a currency pattern needs ¤" : "a number pattern cannot have ¤";
    }
    return f;
}

// The first and last code points of UTF-8 text (0 for empty text).
[[nodiscard]] inline char32_t first_code_point(std::string_view s) noexcept
{
    if (s.empty()) return 0;
    const auto b = static_cast<unsigned char>(s[0]);
    std::size_t n = 4;
    if (b < 0x80) n = 1;
    else if (b < 0xE0) n = 2;
    else if (b < 0xF0) n = 3;
    if (s.size() < n) return 0;
    constexpr std::array<unsigned, 5> lead_bits{0, 0x7FU, 0x1FU, 0x0FU, 0x07U};   // by sequence length
    char32_t cp = b & lead_bits.at(n);
    for (std::size_t i = 1; i < n; ++i) cp = (cp << 6U) | (static_cast<char32_t>(static_cast<unsigned char>(s[i])) & 0x3FU);
    return cp;
}

[[nodiscard]] inline char32_t last_code_point(std::string_view s) noexcept
{
    std::size_t i = s.size();
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0U) == 0x80U) --i;
    return i == 0 ? 0 : first_code_point(s.substr(i - 1));
}

// Whether a code point is a symbol or a separator ([:S:] or [:Z:]) for CLDR's currencySpacing,
// for the characters currency symbols are made of: the currency symbols (Sc), the ASCII and
// Latin-1 symbols and the spaces. Letters, marks, digits and punctuation (the '.' of "лв.")
// are not, so a symbol that ends in one is spaced from the number.
[[nodiscard]] constexpr bool symbol_or_space(char32_t c) noexcept
{
    if (c < 0x80)
    {
        return c == U' ' || c == U'$' || c == U'+' || c == U'<' || c == U'=' || c == U'>' || c == U'^' ||
               c == U'`' || c == U'|' || c == U'~';
    }
    if (c <= 0xFF)
    {
        return c == 0xA0 || (c >= 0xA2 && c <= 0xA6) || c == 0xA8 || c == 0xA9 || c == 0xAC ||
               (c >= 0xAE && c <= 0xB1) || c == 0xB4 || c == 0xB8 || c == 0xD7 || c == 0xF7;
    }
    constexpr std::array<char32_t, 25> currency{0x058F, 0x060B, 0x07FE, 0x07FF, 0x09F2, 0x09F3, 0x09FB,
                                                0x0AF1, 0x0BF9, 0x0E3F, 0x17DB, 0xA838, 0xFDFC, 0xFE69,
                                                0xFF04, 0xFFE0, 0xFFE1, 0xFFE5, 0xFFE6, 0x11FDD, 0x11FDE,
                                                0x11FDF, 0x11FE0, 0x1E2FF, 0x1ECB0};
    if (std::ranges::find(currency, c) != currency.end()) return true;
    return (c >= 0x20A0 && c <= 0x20C0) || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 ||
           c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

// The integer and fraction digits of |v|: the shortest decimal that reads back to v, rounded
// half-even to at most max_fraction digits, on the text (design 5.2).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] inline std::pair<std::string, std::string> decimal_digits(double v, std::size_t max_fraction)
{
    std::array<char, 400> buf{};
    const auto res = std::to_chars(buf.data(), std::to_address(buf.end()), std::fabs(v), std::chars_format::fixed);
    const std::string_view text{buf.data(), static_cast<std::size_t>(res.ptr - buf.data())};
    const auto dot = text.find('.');
    std::string integer{text.substr(0, dot)};
    std::string fraction{dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1)};
    if (fraction.size() <= max_fraction) return {integer, fraction};
    const char first = fraction[max_fraction];
    const bool rest = fraction.find_first_not_of('0', max_fraction + 1) != std::string::npos;
    fraction.resize(max_fraction);
    const char kept = max_fraction > 0 ? fraction.back() : integer.back();
    const bool up = first > '5' || (first == '5' && (rest || (kept - '0') % 2 == 1));
    if (!up) return {integer, fraction};
    for (auto* digits : {&fraction, &integer})
    {
        for (char& c : std::views::reverse(*digits))
        {
            if (c != '9')
            {
                ++c;
                return {integer, fraction};
            }
            c = '0';
        }
    }
    integer.insert(integer.begin(), '1');
    return {integer, fraction};
}

// -- Holidays (design 8) ---------------------------------------------------------

enum class rule_form : std::uint8_t
{
    fixed,
    nth,
    after,
    before,
    easter,
    orthodox,
    event
};

struct holiday_rule
{
    rule_form form{rule_form::fixed};
    unsigned month{1}, day{1};
    int n{0};   // the N-th weekday (-1 the last), or the days added to Easter or the event
    std::chrono::weekday weekday{};
    std::string event;
};

[[nodiscard]] constexpr bool key_char(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

[[nodiscard]] constexpr bool valid_key(std::string_view k) noexcept
{
    return !k.empty() && std::ranges::all_of(k, key_char);
}

// "+3", "-48" or empty -> the offset; nullopt when malformed or beyond a year.
[[nodiscard]] inline std::optional<int> parse_offset(std::string_view t)
{
    if (t.empty()) return 0;
    if (t.size() < 2 || (t[0] != '+' && t[0] != '-')) return std::nullopt;
    const auto v = core::detail::parse_number<int>(t.substr(1));
    if (!v || *v > 366 || t.substr(1).find_first_not_of("0123456789") != std::string_view::npos)
    {
        return std::nullopt;
    }
    return t[0] == '-' ? -*v : *v;
}

// "MM-DD" -> month and day, valid in a leap year.
[[nodiscard]] inline bool parse_month_day(std::string_view t, unsigned& month, unsigned& day)
{
    if (t.size() != 5 || t[2] != '-') return false;
    const auto m = core::detail::parse_number<unsigned>(t.substr(0, 2));
    const auto d = core::detail::parse_number<unsigned>(t.substr(3, 2));
    if (!m || !d) return false;
    month = *m;
    day = *d;
    return std::chrono::year_month_day{std::chrono::year{2024}, std::chrono::month{month}, std::chrono::day{day}}.ok();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): one branch per rule form
[[nodiscard]] inline std::optional<holiday_rule> parse_rule(std::string_view t)
{
    holiday_rule r;
    if (t.starts_with("event:"))
    {
        const auto rest = t.substr(6);
        const auto sign = rest.find_first_of("+-");
        r.form = rule_form::event;
        r.event = std::string{rest.substr(0, sign)};
        const auto n = parse_offset(sign == std::string_view::npos ? std::string_view{} : rest.substr(sign));
        if (!valid_key(r.event) || !n) return std::nullopt;
        r.n = *n;
        return r;
    }
    for (const auto& [name, form] : {std::pair{std::string_view{"easter"}, rule_form::easter},
                                     std::pair{std::string_view{"orthodox"}, rule_form::orthodox}})
    {
        if (t.starts_with(name))
        {
            const auto n = parse_offset(t.substr(name.size()));
            if (!n) return std::nullopt;
            r.form = form;
            r.n = *n;
            return r;
        }
    }
    if (t.size() > 3 && t[2] == ':')
    {
        // MM:N:dow
        const auto second = t.find(':', 3);
        if (second == std::string_view::npos) return std::nullopt;
        const auto m = core::detail::parse_number<unsigned>(t.substr(0, 2));
        const auto n = core::detail::parse_number<int>(t.substr(3, second - 3));
        const auto dow = parse_weekday(t.substr(second + 1));
        if (!m || *m < 1 || *m > 12 || !n || (*n != -1 && (*n < 1 || *n > 5)) || !dow) return std::nullopt;
        r.form = rule_form::nth;
        r.month = *m;
        r.n = *n;
        r.weekday = *dow;
        return r;
    }
    if (t.size() == 9 && (t[5] == '>' || t[5] == '<'))
    {
        const auto dow = parse_weekday(t.substr(6));
        if (!parse_month_day(t.substr(0, 5), r.month, r.day) || !dow) return std::nullopt;
        r.form = t[5] == '>' ? rule_form::after : rule_form::before;
        r.weekday = *dow;
        return r;
    }
    if (!parse_month_day(t, r.month, r.day)) return std::nullopt;
    r.form = rule_form::fixed;
    return r;
}

// Western Easter Sunday (the Anonymous Gregorian algorithm, Meeus).
[[nodiscard]] constexpr std::chrono::sys_days easter(int y) noexcept
{
    const int a = y % 19;
    const int b = y / 100;
    const int c = y % 100;
    const int d = b / 4;
    const int e = b % 4;
    const int f = (b + 8) / 25;
    const int g = (b - f + 1) / 3;
    const int h = (19 * a + b - d - g + 15) % 30;
    const int i = c / 4;
    const int k = c % 4;
    const int l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451;
    const int n = h + l - 7 * m + 114;
    return std::chrono::sys_days{std::chrono::year{y} / std::chrono::month{static_cast<unsigned>(n / 31)} /
                                 std::chrono::day{static_cast<unsigned>(n % 31 + 1)}};
}

// Orthodox Easter Sunday (the Julian computus, Meeus), as a Gregorian date.
[[nodiscard]] constexpr std::chrono::sys_days orthodox_easter(int y) noexcept
{
    const int a = y % 4;
    const int b = y % 7;
    const int c = y % 19;
    const int d = (19 * c + 15) % 30;
    const int e = (2 * a + 4 * b - d + 34) % 7;
    const int n = d + e + 114;
    return std::chrono::sys_days{std::chrono::year{y} / std::chrono::month{static_cast<unsigned>(n / 31)} /
                                 std::chrono::day{static_cast<unsigned>(n % 31 + 1)}} +
           std::chrono::days{y / 100 - y / 400 - 2};
}

[[nodiscard]] inline int year_of(std::chrono::sys_days d)
{
    return static_cast<int>(std::chrono::year_month_day{d}.year());
}

// The start dates a rule gives in year y; events: the event's dates for the country, sorted.
inline void rule_dates(const holiday_rule& r, int y, const std::vector<std::chrono::sys_days>* events,
                       std::vector<std::chrono::sys_days>& out)
{
    using namespace std::chrono;
    out.clear();
    switch (r.form)
    {
    case rule_form::fixed:
    {
        const year_month_day d{year{y}, month{r.month}, day{r.day}};
        if (d.ok()) out.push_back(sys_days{d});
        return;
    }
    case rule_form::nth:
    {
        if (r.n > 0)
        {
            const sys_days first{year{y} / month{r.month} / 1};
            out.push_back(first + (r.weekday - weekday{first}) + days{7 * (r.n - 1)});
        }
        else
        {
            const sys_days final_day{year{y} / month{r.month} / std::chrono::last};
            out.push_back(final_day - (weekday{final_day} - r.weekday));
        }
        return;
    }
    case rule_form::after:
    case rule_form::before:
    {
        const sys_days base{year{y} / month{r.month} / day{r.day}};
        out.push_back(r.form == rule_form::after ? base + (r.weekday - weekday{base})
                                                 : base - (weekday{base} - r.weekday));
        return;
    }
    case rule_form::easter: out.push_back(easter(y) + days{r.n}); return;
    case rule_form::orthodox: out.push_back(orthodox_easter(y) + days{r.n}); return;
    case rule_form::event:
    {
        if (events == nullptr) return;
        const sys_days lo = sys_days{year{y} / 1 / 1} - days{r.n};
        const sys_days hi = sys_days{year{y + 1} / 1 / 1} - days{r.n};
        for (auto it = std::ranges::lower_bound(*events, lo); it != events->end() && *it < hi; ++it)
        {
            out.push_back(*it + days{r.n});
        }
        return;
    }
    }
}

// An observed policy (design 8.4): none, a weekday map, next_weekday or weekend>next, with overlap.
struct observed_policy
{
    enum class type : std::uint8_t
    {
        none,
        map,
        next_weekday,
        weekend_next
    };
    type kind{type::none};
    // By c_encoding of the day that moves: the weekday it moves to, and +1 (after) or -1 (before).
    std::array<std::optional<std::pair<std::chrono::weekday, int>>, 7> moves{};
    bool overlap{false};
    std::string text;
};

[[nodiscard]] inline std::optional<observed_policy> parse_policy(std::string_view t)
{
    observed_policy p;
    p.text = std::string{t};
    if (t == "none") return p;
    if (t.ends_with(",overlap"))
    {
        p.overlap = true;
        t.remove_suffix(8);
    }
    if (t == "next_weekday")
    {
        p.kind = observed_policy::type::next_weekday;
        return p;
    }
    if (t == "weekend>next")
    {
        p.kind = observed_policy::type::weekend_next;
        return p;
    }
    p.kind = observed_policy::type::map;
    while (!t.empty())
    {
        const auto comma = t.find(',');
        const auto item = t.substr(0, comma);
        t = comma == std::string_view::npos ? std::string_view{} : t.substr(comma + 1);
        if (item.size() != 7 || (item[3] != '<' && item[3] != '>')) return std::nullopt;
        const auto from = parse_weekday(item.substr(0, 3));
        const auto to = parse_weekday(item.substr(4));
        if (!from || !to || *from == *to || p.moves.at(from->c_encoding())) return std::nullopt;
        p.moves.at(from->c_encoding()) = std::pair{*to, item[3] == '>' ? 1 : -1};
    }
    return p;
}

// What the library needs of one holiday row.
struct holiday_row
{
    std::string key, name, name_en;
    holiday_kind kind{holiday_kind::public_};
    holiday_rule rule;
    unsigned days{1};
    observed_policy observed;
    int from{}, to{};
    const std::vector<std::chrono::sys_days>* events{nullptr};   // the country's dates of rule.event
};

[[nodiscard]] inline std::optional<holiday_kind> parse_kind(std::string_view t)
{
    if (t == "public") return holiday_kind::public_;
    if (t == "bank") return holiday_kind::bank;
    if (t == "optional") return holiday_kind::optional;
    if (t == "observance") return holiday_kind::observance;
    return std::nullopt;
}

[[nodiscard]] constexpr std::string_view kind_name(holiday_kind k) noexcept
{
    switch (k)
    {
    case holiday_kind::public_: return "public";
    case holiday_kind::bank: return "bank";
    case holiday_kind::optional: return "optional";
    case holiday_kind::observance: return "observance";
    }
    return "public";
}

// Moves one occurrence's days by its policy (design 8.4). taken: the days of the kind's holidays
// and the moves so far; shared: the days moved under each policy, so holidays on one date move
// together. moved[i] is the observed day of the occurrence's day i.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): the observed-day algorithm of design 8.4
inline void simulate(const observed_policy& p, std::chrono::sys_days start, unsigned days, std::set<int>& taken,
                     unsigned weekend, std::map<std::pair<int, std::string_view>, int>& shared,
                     std::vector<std::optional<std::chrono::sys_days>>& moved)
{
    using std::chrono::sys_days;
    moved.assign(days, std::nullopt);
    if (p.kind == observed_policy::type::none) return;
    const auto day_of = [](sys_days d) { return static_cast<int>(d.time_since_epoch().count()); };
    const auto is_weekend = [weekend](sys_days d) {
        return ((weekend >> std::chrono::weekday{d}.c_encoding()) & 1U) != 0;
    };
    const auto free = [&](sys_days t, int step) {
        while (!p.overlap && (taken.contains(day_of(t)) || is_weekend(t))) t += std::chrono::days{step};
        return t;
    };
    if (p.kind == observed_policy::type::weekend_next)
    {
        sys_days t = start + std::chrono::days{days};
        for (unsigned i = 0; i < days; ++i)
        {
            if (!is_weekend(start + std::chrono::days{i})) continue;
            t = free(t, 1);
            moved[i] = t;
            taken.insert(day_of(t));
            t += std::chrono::days{1};
        }
        return;
    }
    for (unsigned i = 0; i < days; ++i)
    {
        const sys_days d = start + std::chrono::days{i};
        sys_days t = d;
        int step = 1;
        if (p.kind == observed_policy::type::next_weekday)
        {
            if (!is_weekend(d)) continue;
            t = d + std::chrono::days{1};
        }
        else
        {
            const auto& move = p.moves.at(std::chrono::weekday{d}.c_encoding());
            if (!move) continue;
            step = move->second;
            t = d + std::chrono::days{step};
            while (std::chrono::weekday{t} != move->first) t += std::chrono::days{step};
        }
        const std::pair<int, std::string_view> id{day_of(d), p.text};
        if (const auto it = shared.find(id); it != shared.end())
        {
            moved[i] = sys_days{std::chrono::days{it->second}};
            continue;
        }
        t = free(t, step);
        moved[i] = t;
        shared.emplace(id, day_of(t));
        taken.insert(day_of(t));
    }
}

} // namespace country_detail
/// @endcond

/// @brief Per-call locale selection options (cntg::get_locale()).
struct locale_request
{
    /// @brief ISO 3166-1 alpha-2 code, any case; unset draws the country as
    ///        get_country() does (or by speakers of `language`, when set).
    std::optional<std::string> country{std::nullopt};

    /// @brief ISO 639 code (`fr`), any case; narrows the draw to that language.
    std::optional<std::string> language{std::nullopt};

    /// @brief Weighted by speakers (default); false makes every eligible tag,
    ///        and every eligible country, equally likely.
    bool weighted{true};
};

/// @brief A locale of a country: its tag, CLDR formats and calendar data.
///
/// Every row is complete: the build resolved CLDR's inheritance, so the
/// formatting functions read only these fields.
class locale
{
  public:
    std::string tag;            ///< BCP 47, minimised: "pt-BR", "zh-TW", "sr-Latn-RS".
    std::string language;       ///< ISO 639 code: "pt".
    std::string script;         ///< ISO 15924 code when the tag needs one ("Latn"); else empty.
    std::string language_name;  ///< English, as country::language_shares writes it: "Portuguese".
    std::string country_code;   ///< ISO 3166-1 alpha-2: "BR".
    double share{0.0};          ///< Percent of the country that speaks the language (CLDR).
    std::string basis;          ///< "cldr", "inherited" or "fallback" (doc/data.md).

    std::string date_short;     ///< LDML short date pattern: "dd/MM/y".
    std::string date_medium;    ///< LDML medium date pattern.
    std::string date_long;      ///< LDML long date pattern.
    std::string date_full;      ///< LDML full date pattern.
    std::string time_short;     ///< LDML short time pattern: "HH:mm".
    std::string time_medium;    ///< LDML medium time pattern: "HH:mm:ss".
    std::array<std::string, 12> months{};       ///< Wide month names, format context, January first.
    std::array<std::string, 12> months_abbr{};  ///< Abbreviated month names.
    std::array<std::string, 7> weekdays{};      ///< Wide weekday names, format context, Sunday first.
    std::array<std::string, 7> weekdays_abbr{}; ///< Abbreviated weekday names, Sunday first.
    std::string am;                             ///< Day period before noon.
    std::string pm;                             ///< Day period after noon.
    std::array<std::string, 2> eras{};          ///< Abbreviated eras: BC, AD.

    std::string decimal;          ///< Decimal symbol: ",".
    std::string group;            ///< Grouping symbol: ".".
    std::string minus;            ///< Minus sign: "-".
    std::string number_pattern;   ///< CLDR decimal pattern: "#,##0.###".
    std::string currency_pattern; ///< CLDR currency pattern: "¤ #,##0.00".
    std::string currency_spacing; ///< Put between a letter symbol and the number; usually U+00A0.
    std::string currency_code;    ///< The country's ISO 4217 code: "BRL".
    std::string currency_symbol;  ///< This locale's symbol for it: "R$".
    std::uint8_t currency_digits{2}; ///< Its ISO 4217 minor unit.

    std::chrono::weekday first_day{std::chrono::Monday};      ///< First day of the week.
    std::chrono::weekday weekend_start{std::chrono::Saturday}; ///< First day of the weekend.
    std::chrono::weekday weekend_end{std::chrono::Sunday};     ///< Last day of the weekend.
    dasmig::measurement measurement{dasmig::measurement::metric}; ///< Measurement system.

    /// @brief Write a date with one of the locale's date patterns.
    /// @param d The date.
    /// @param style Which pattern.
    /// @return The text, in UTF-8.
    /// @throws std::invalid_argument If @p d is not a valid date, or the
    ///         pattern holds a field the formatter does not write.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity): one case per field
    [[nodiscard]] std::string format_date(std::chrono::year_month_day d, date_style style = date_style::short_) const
    {
        if (!d.ok()) throw std::invalid_argument("format_date: not a valid date");
        const std::array<const std::string*, 4> patterns{&date_short, &date_medium, &date_long, &date_full};
        const std::string& pattern = *patterns.at(static_cast<std::size_t>(style));
        const int y = static_cast<int>(d.year());
        const auto month = static_cast<unsigned>(d.month());
        const unsigned wd = std::chrono::weekday{std::chrono::sys_days{d}}.c_encoding();
        std::string out;
        write_pattern_(pattern, false, "format_date", out, [&](char c, unsigned n) {
            const int era_year = y > 0 ? y : 1 - y;
            switch (c)
            {
            case 'G': out += eras.at(y > 0 ? 1 : 0); break;
            case 'y':
                if (n == 2) country_detail::append_padded(out, era_year % 100, 2);
                else country_detail::append_padded(out, era_year, n);
                break;
            case 'M':
            case 'L':
                if (n >= 3) out += (n == 3 ? months_abbr : months).at(month - 1);
                else country_detail::append_padded(out, month, n);
                break;
            case 'd': country_detail::append_padded(out, static_cast<unsigned>(d.day()), n); break;
            default: out += (n == 4 ? weekdays : weekdays_abbr).at(wd); break;   // E and c
            }
        });
        return out;
    }

    /// @brief Write a time of day with one of the locale's time patterns.
    /// @param t The time (0 to 24 hours).
    /// @param style Which pattern.
    /// @return The text, in UTF-8.
    /// @throws std::invalid_argument If @p t is negative or 24 hours or more,
    ///         or the pattern holds a field the formatter does not write.
    [[nodiscard]] std::string format_time(std::chrono::hh_mm_ss<std::chrono::seconds> t,
                                          time_style style = time_style::short_) const
    {
        if (t.is_negative() || t.hours().count() > 23)
        {
            throw std::invalid_argument("format_time: the time must be from 00:00:00 to 23:59:59");
        }
        const auto h = t.hours().count();
        std::string out;
        write_pattern_(style == time_style::short_ ? time_short : time_medium, true, "format_time", out,
                       [&](char c, unsigned n) {
                           switch (c)
                           {
                           case 'a': out += h < 12 ? am : pm; break;
                           case 'h': country_detail::append_padded(out, h % 12 == 0 ? 12 : h % 12, n); break;
                           case 'H': country_detail::append_padded(out, h, n); break;
                           case 'm': country_detail::append_padded(out, t.minutes().count(), n); break;
                           default: country_detail::append_padded(out, t.seconds().count(), n); break;
                           }
                       });
        return out;
    }

    /// @brief Write a number with the locale's decimal pattern.
    ///
    /// The value's shortest round-trip digits are rounded half-even, as
    /// text, to at most @p max_fraction fraction digits (and the pattern's
    /// maximum), so 2.675 at two digits is `2.68` on every platform.
    ///
    /// @param value The number.
    /// @param max_fraction Most fraction digits.
    /// @return The text, in UTF-8.
    /// @throws std::invalid_argument If @p value is not finite or the pattern
    ///         is not one the formatter writes.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] std::string format_number(double value, unsigned max_fraction = 3) const
    {
        if (!std::isfinite(value)) throw std::invalid_argument("format_number: the value is not finite");
        const auto f = number_format_(number_pattern, false, "format_number");
        const std::size_t digits = std::min<std::size_t>(max_fraction, f.max_fraction);
        auto [integer, fraction] = country_detail::decimal_digits(value, digits);
        const std::size_t min_fraction = std::min(f.min_fraction, digits);
        while (fraction.size() > min_fraction && fraction.back() == '0') fraction.pop_back();
        if (fraction.size() < min_fraction) fraction.append(min_fraction - fraction.size(), '0');
        const bool zero = integer.find_first_not_of('0') == std::string::npos &&
                          fraction.find_first_not_of('0') == std::string::npos;
        return render_(f, value < 0 && !zero, integer, fraction, {});
    }

    /// @brief Write an amount with the locale's currency pattern.
    ///
    /// Exactly `m.digits` fraction digits, from the integer minor units, so
    /// nothing is rounded. The symbol is the locale's own for its currency
    /// and the ISO code for any other; CLDR's currency spacing puts
    /// currency_spacing between a symbol that ends (or starts) with a letter
    /// and the number: `CHF 1,234.50` but `$1,234.50` in `en-US`.
    ///
    /// @param m The amount.
    /// @return The text, in UTF-8.
    /// @throws std::invalid_argument If the pattern is not one the formatter writes.
    [[nodiscard]] std::string format_money(const core::money& m) const
    {
        const auto f = number_format_(currency_pattern, true, "format_money");
        const std::uint64_t magnitude =
            m.minor < 0 ? static_cast<std::uint64_t>(-(m.minor + 1)) + 1 : static_cast<std::uint64_t>(m.minor);
        std::string digits = std::to_string(magnitude);
        if (digits.size() <= m.digits) digits.insert(0, m.digits + 1 - digits.size(), '0');
        const auto split = digits.size() - m.digits;
        return render_(f, m.minor < 0, digits.substr(0, split), digits.substr(split),
                       m.currency == currency_code ? std::string_view{currency_symbol} : std::string_view{m.currency});
    }

    /// @brief The seed this locale was drawn with.
    /// @return The per-call seed (0 for find_locale() and locales()).
    [[nodiscard]] std::uint64_t seed() const { return _signature.seed; }

    /// @brief The seed and the data this locale was drawn from.
    /// @return The replay signature.
    [[nodiscard]] const core::seed_signature& signature() const { return _signature; }

    /// @brief Write this locale as a JSON object (doc/usage.md, "Locales").
    ///
    /// The month, weekday, era and day-period names are left out: they are
    /// data, reachable through cntg::find_locale().
    ///
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        w.begin_object()
            .field("tag", tag)
            .field("language", language)
            .field("script", script)
            .field("language_name", language_name)
            .field("country", country_code)
            .field("share", share)
            .field("basis", basis)
            .key("formats")
            .begin_object()
            .key("date")
            .begin_object()
            .field("short", date_short)
            .field("medium", date_medium)
            .field("long", date_long)
            .field("full", date_full)
            .end_object()
            .key("time")
            .begin_object()
            .field("short", time_short)
            .field("medium", time_medium)
            .end_object()
            .field("number", number_pattern)
            .field("currency", currency_pattern)
            .end_object()
            .key("symbols")
            .begin_object()
            .field("decimal", decimal)
            .field("group", group)
            .field("minus", minus)
            .end_object()
            .key("currency")
            .begin_object()
            .field("code", currency_code)
            .field("symbol", currency_symbol)
            .field("digits", static_cast<unsigned>(currency_digits))
            .end_object()
            .field("first_day", country_detail::weekday_names.at(first_day.c_encoding()))
            .key("weekend")
            .begin_array()
            .value(country_detail::weekday_names.at(weekend_start.c_encoding()))
            .value(country_detail::weekday_names.at(weekend_end.c_encoding()))
            .end_array()
            .field("measurement", country_detail::measurement_names.at(static_cast<std::size_t>(measurement)))
            .key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This locale as a JSON document.
    /// @return Compact JSON.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::string.
    /// @return The tag.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return tag;
    }

    /// @brief Stream the tag.
    /// @param os Output stream.
    /// @param l Locale to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const locale& l)
    {
        os << l.tag;
        return os;
    }

  private:
    core::seed_signature _signature;

    template <typename Field>
    static void write_pattern_(std::string_view pattern, bool time, std::string_view caller, std::string& out,
                               Field&& field)
    {
        if (const auto error = country_detail::check_pattern(pattern, time); !error.empty())
        {
            throw std::invalid_argument(std::string{caller} + ": " + error + " in '" + std::string{pattern} + "'");
        }
        (void)country_detail::walk_pattern(
            pattern, [&out](std::string_view text) { out += text; }, std::forward<Field>(field));
    }

    static country_detail::number_format number_format_(std::string_view pattern, bool currency,
                                                        std::string_view caller)
    {
        std::string error;
        auto f = country_detail::parse_number_pattern(pattern, currency, error);
        if (!error.empty())
        {
            throw std::invalid_argument(std::string{caller} + ": " + error + " in '" + std::string{pattern} + "'");
        }
        return f;
    }

    // The number with its affixes, the symbols in place of the marks and the currency spacing.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity): one pass over the affixes
    [[nodiscard]] std::string render_(const country_detail::number_format& f, bool negative, std::string integer,
                                      std::string_view fraction, std::string_view symbol) const
    {
        const auto start = integer.find_first_not_of('0');
        integer.erase(0, start == std::string::npos ? integer.size() : start);
        if (integer.size() < f.min_integer) integer.insert(0, f.min_integer - integer.size(), '0');
        std::string number;
        if (f.primary == 0 || integer.size() <= f.primary)
        {
            number = integer;
        }
        else
        {
            std::vector<std::string_view> groups;
            std::string_view rest{integer};
            groups.push_back(rest.substr(rest.size() - f.primary));
            rest.remove_suffix(f.primary);
            while (rest.size() > f.secondary)
            {
                groups.push_back(rest.substr(rest.size() - f.secondary));
                rest.remove_suffix(f.secondary);
            }
            groups.push_back(rest);
            for (auto it = groups.rbegin(); it != groups.rend(); ++it)
            {
                if (it != groups.rbegin()) number += group;
                number += *it;
            }
        }
        if (!fraction.empty())
        {
            number += decimal;
            number += fraction;
        }
        const auto& prefix = negative ? f.negative_prefix : f.positive_prefix;
        const auto& suffix = negative ? f.negative_suffix : f.positive_suffix;
        const auto expand = [&](const std::string& affix, std::string& out) {
            for (const char c : affix)
            {
                if (c == country_detail::currency_mark) out += symbol;
                else if (c == country_detail::minus_mark) out += minus;
                else out += c;
            }
        };
        std::string out;
        expand(prefix, out);
        if (!prefix.empty() && prefix.back() == country_detail::currency_mark &&
            !country_detail::symbol_or_space(country_detail::last_code_point(symbol)))
        {
            out += currency_spacing;
        }
        out += number;
        if (!suffix.empty() && suffix.front() == country_detail::currency_mark &&
            !country_detail::symbol_or_space(country_detail::first_code_point(symbol)))
        {
            out += currency_spacing;
        }
        expand(suffix, out);
        return out;
    }

    friend class cntg;
};

/// @brief Per-call time-zone selection options (cntg::get_time_zone()).
struct time_zone_request
{
    /// @brief The date whose offset a result reports, at 12:00 UTC, unless set.
    static constexpr std::chrono::year_month_day default_reference_date{std::chrono::year{2023} /
                                                                        std::chrono::July / 1};

    /// @brief ISO 3166-1 alpha-2 code, any case; unset draws the country as
    ///        get_country() does.
    std::optional<std::string> country{std::nullopt};

    /// @brief The date whose offset, abbreviation and daylight flag are reported.
    std::chrono::year_month_day reference_date{default_reference_date};

    /// @brief Weighted by population (default); false makes every zone of the
    ///        country, and every country, equally likely.
    bool weighted{true};
};

/// @brief An IANA time zone of a country, with its rules.
class time_zone
{
  public:
    std::string name;          ///< IANA name: "America/Sao_Paulo".
    std::string country_code;  ///< ISO 3166-1 alpha-2: "BR".
    double share{0.0};         ///< Percent of the country's population in the zone (GeoNames).
    std::string basis;         ///< "tzdata", "geonames" or "fallback" (doc/data.md).
    core::tz_rules rules;      ///< The zone's rules, from the tier's first year on.

    std::chrono::seconds utc_offset{}; ///< At the reference date, 12:00 UTC: east of UTC.
    std::string abbreviation;          ///< At the reference date: "-03", "CEST".
    bool dst{false};                   ///< At the reference date: daylight time.
    bool observes_dst{false};          ///< Any daylight time from the reference date on.

    /// @brief Local time at a UTC instant.
    /// @param t The instant.
    /// @return The local time.
    /// @throws std::out_of_range Before the zone's data (core::tz_rules).
    [[nodiscard]] std::chrono::local_seconds to_local(std::chrono::sys_seconds t) const { return rules.to_local(t); }

    /// @brief The UTC instant of a local time.
    /// @param t The local time.
    /// @param choose Which instant, for a local time that occurs twice.
    /// @return The instant (a local time in a gap is moved forward by the gap).
    /// @throws std::out_of_range Before the zone's data (core::tz_rules).
    [[nodiscard]] std::chrono::sys_seconds to_utc(std::chrono::local_seconds t,
                                                  core::tz_choose choose = core::tz_choose::earliest) const
    {
        return rules.to_utc(t, choose);
    }

    /// @brief The seed this zone was drawn with.
    /// @return The per-call seed (0 for find_time_zone() and time_zones()).
    [[nodiscard]] std::uint64_t seed() const { return _signature.seed; }

    /// @brief The seed and the data this zone was drawn from.
    /// @return The replay signature.
    [[nodiscard]] const core::seed_signature& signature() const { return _signature; }

    /// @brief Write this zone as a JSON object; the offset as `±HH:MM`.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        std::string offset{utc_offset.count() < 0 ? "-" : "+"};
        const auto total = utc_offset.count() < 0 ? -utc_offset.count() : utc_offset.count();
        country_detail::append_padded(offset, total / 3600, 2);
        offset += ':';
        country_detail::append_padded(offset, total / 60 % 60, 2);
        if (total % 60 != 0)
        {
            offset += ':';
            country_detail::append_padded(offset, total % 60, 2);
        }
        w.begin_object()
            .field("name", name)
            .field("country", country_code)
            .field("share", share)
            .field("basis", basis)
            .field("utc_offset", offset)
            .field("abbreviation", abbreviation)
            .field("dst", dst)
            .field("observes_dst", observes_dst)
            .key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This zone as a JSON document.
    /// @return Compact JSON.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::string.
    /// @return The IANA name.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return name;
    }

    /// @brief Stream the IANA name.
    /// @param os Output stream.
    /// @param z Zone to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const time_zone& z)
    {
        os << z.name;
        return os;
    }

  private:
    core::seed_signature _signature;
    friend class cntg;
};

/// @brief What cntg::holidays() returns.
struct holiday_request
{
    std::string country;       ///< ISO 3166-1 alpha-2 code, any case; required.
    int year{};                ///< The year; required.
    bool optional{false};      ///< Include the days off for some (holiday_kind::optional).
    bool observances{false};   ///< Include the days that are not days off (holiday_kind::observance).
};

/// @brief One day of a holiday in a year.
struct holiday
{
    std::chrono::year_month_day date{};                  ///< The day itself.
    std::optional<std::chrono::year_month_day> observed; ///< Only when a weekend rule moves the day off.
    std::string key;                                     ///< Stable: "christmas_day", "eid_al_fitr_holiday".
    std::string name;                                    ///< In the country's language, as python-holidays writes it: "Natal".
    std::string name_en;                                 ///< In English: "Christmas Day".
    holiday_kind kind{holiday_kind::public_};            ///< The kind of day.
    unsigned day{1};                                     ///< 1-based day of a multi-day holiday (Eid's second day is 2).

    /// @brief Write this holiday as a JSON object, dates as `YYYY-MM-DD`.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        w.begin_object().field("date", country_detail::iso_date(date)).key("observed");
        if (observed) w.value(country_detail::iso_date(*observed));
        else w.value(nullptr);
        w.field("key", key)
            .field("name", name)
            .field("name_en", name_en)
            .field("kind", country_detail::kind_name(kind))
            .field("day", day)
            .end_object();
    }

    /// @brief This holiday as a JSON document.
    /// @return Compact JSON.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }
};

/// @brief Country generator that produces random countries from
///        aggregated open-data sources.
///
/// Generates countries using population-weighted random selection by
/// default. Larger countries are proportionally more likely to be
/// selected, mirroring real-world demographic distributions.
///
/// Next to the countries it holds each country's locales (with their CLDR
/// formats), IANA time zones, currency minor units and national holidays,
/// in their own tables (country_table), which load with the tier when they
/// are there.
///
/// Output is identical on every compiler and platform (dasmig-core).
///
/// Can be used as a singleton via instance() or constructed independently.
///
/// @par Thread safety
/// Each instance is independent. Concurrent calls to the unseeded getters
/// on the **same** instance require external synchronization. Calls with
/// an explicit seed and the lookups (find_*, locales(), time_zones(),
/// holidays(), is_holiday()) only read data and may run concurrently.
/// load() and extend() must not run concurrently with any of them.
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

    // -- Locales ------------------------------------------------------------

    /// @brief Draw a locale of a country, by speakers.
    ///
    /// Draws a call seed from the internal random engine. Replay with the
    /// same request, the returned locale's seed and the same data.
    /// @param request The country, the language and the selection mode.
    /// @return The locale.
    /// @throws std::runtime_error If the locale tables are not loaded.
    /// @throws std::invalid_argument As the seeded overload.
    [[nodiscard]] locale get_locale(const locale_request& request) { return get_locale(request, _engine()); }

    /// @brief Draw a locale of a country, by speakers, deterministically.
    ///
    /// The country is the request's, or else drawn from the `"country"`
    /// stream exactly as get_country() draws it, so
    /// `get_locale({}, s).country_code == get_country({}, s).cca2` for every
    /// country with a locale (a drawn country with none, Antarctica in
    /// `full`, is replaced by a draw among the countries that have one). With
    /// only `language` set, the country is drawn among those with a locale of
    /// that language, by speakers. The tag is drawn from the `"locale"`
    /// stream, weighted by its share.
    ///
    /// @param request The country, the language and the selection mode.
    /// @param call_seed Seed for reproducible results.
    /// @return The locale.
    /// @throws std::runtime_error If no countries or no locale tables are loaded.
    /// @throws std::invalid_argument For an unknown country, a language no
    ///         country has a locale of, or a country with no locale (of the
    ///         language, when set).
    [[nodiscard]] locale get_locale(const locale_request& request, std::uint64_t call_seed) const
    {
        require_locale_table_(country_table::locales);
        const auto& m = _locale;
        std::optional<std::string> language;
        if (request.language) language = country_detail::lower(*request.language);
        bool drawn = false;
        std::string cc;
        if (request.country)
        {
            cc = known_country_(*request.country);
        }
        else if (!language)
        {
            cc = draw_country_(call_seed, request.weighted, m.locale_countries, m.locales_by_country, "locale_country");
            drawn = true;
        }
        else
        {
            const auto it = m.locale_languages.find(*language);
            if (it == m.locale_languages.end()) throw std::invalid_argument("No locale for language: " + *language);
            require_data_();
            auto r = core::rng{call_seed}.split("country");
            const auto j = request.weighted ? it->second.weighted(r)
                                            : static_cast<std::size_t>(core::bounded(r, it->second.items.size()));
            cc = _countries[it->second.items[j]].cca2;
            drawn = true;
        }
        const auto it = m.locales_by_country.find(cc);
        if (it == m.locales_by_country.end()) throw std::invalid_argument("No locale data for country: " + cc);
        auto r = core::rng{call_seed}.split("locale");
        std::size_t index = 0;
        if (!language)
        {
            const auto& items = it->second.items;
            index = items[request.weighted ? it->second.weighted(r)
                                           : static_cast<std::size_t>(core::bounded(r, items.size()))];
        }
        else
        {
            std::vector<std::size_t> items;
            std::vector<double> weights;
            for (const auto i : it->second.items)
            {
                if (m.locales[i].language != *language) continue;
                items.push_back(i);
                weights.push_back(m.locales[i].share);
            }
            if (items.empty())
            {
                throw std::invalid_argument("No locale of language " + *language + " for country: " + cc);
            }
            index = items[request.weighted ? core::discrete{weights}(r)
                                           : static_cast<std::size_t>(core::bounded(r, items.size()))];
        }
        locale out = m.locales[index];
        out._signature = {.seed = call_seed,
                          .data = drawn ? core::hash_combine(locale_hash_(), data_hash()) : locale_hash_()};
        return out;
    }

    /// @brief Generate a batch of locales from a seed drawn like get_locale()
    ///        draws its own.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<locale> get_locale_batch(const locale_request& request,
                                                       const core::batch_options& options)
    {
        return get_locale_batch(request, options, _engine());
    }

    /// @brief Generate a batch of locales, deterministically: item `i` is
    ///        `get_locale(request, core::item_seed(batch_seed, options.first + i))`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    /// @throws std::invalid_argument If the request or options are invalid.
    /// @throws std::runtime_error If no data is loaded, or a unique batch runs
    ///         out of attempts.
    [[nodiscard]] std::vector<locale> get_locale_batch(const locale_request& request,
                                                       const core::batch_options& options,
                                                       std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed,
                               [&](std::uint64_t item_seed) { return get_locale(request, item_seed); });
    }

    /// @brief Stream a batch of locales to a sink without holding the batch.
    /// @tparam Sink Callable taking `(std::uint64_t, locale&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items, in index order.
    template <typename Sink>
    void for_each_locale(const locale_request& request, const core::batch_options& options,
                         std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_locale(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of locales with a batch seed drawn like
    ///        get_locale() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, locale&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items, in index order.
    template <typename Sink>
    void for_each_locale(const locale_request& request, const core::batch_options& options, Sink&& sink)
    {
        for_each_locale(request, options, _engine(), std::forward<Sink>(sink));
    }

    /// @brief Look up a locale by its tag.
    ///
    /// Accepts any case, `_` for `-`, and a script where the tag has none
    /// (`pt-Latn-BR` is `pt-BR`): a script the table has no row of for that
    /// language and country is taken as the language's likely one.
    ///
    /// @param tag BCP 47 tag.
    /// @return The locale (seed 0).
    /// @throws std::runtime_error If the locale tables are not loaded.
    /// @throws std::invalid_argument If no locale has the tag.
    [[nodiscard]] locale find_locale(std::string_view tag) const
    {
        require_locale_table_(country_table::locales);
        std::string key = country_detail::lower(tag);
        std::ranges::replace(key, '_', '-');
        auto it = _locale.locale_by_tag.find(key);
        if (it == _locale.locale_by_tag.end())
        {
            // language-Script-REGION -> language-REGION, when that row has no script.
            const auto first = key.find('-');
            const auto second = first == std::string::npos ? std::string::npos : key.find('-', first + 1);
            if (second == first + 5 && key.find('-', second + 1) == std::string::npos)
            {
                const auto shorter = _locale.locale_by_tag.find(key.substr(0, first) + key.substr(second));
                if (shorter != _locale.locale_by_tag.end() && _locale.locales[shorter->second].script.empty())
                {
                    it = shorter;
                }
            }
        }
        if (it == _locale.locale_by_tag.end()) throw std::invalid_argument("Unknown locale tag: " + std::string{tag});
        locale out = _locale.locales[it->second];
        out._signature = {.seed = 0, .data = locale_hash_()};
        return out;
    }

    /// @brief Every locale of a country, largest share first, then by tag.
    /// @param country ISO 3166-1 alpha-2 code, any case.
    /// @return The locales (seed 0); empty for a country with none.
    /// @throws std::runtime_error If no countries or no locale tables are loaded.
    /// @throws std::invalid_argument For an unknown country.
    [[nodiscard]] std::vector<locale> locales(std::string_view country) const
    {
        require_locale_table_(country_table::locales);
        const auto cc = known_country_(country);
        std::vector<locale> out;
        if (const auto it = _locale.locales_by_country.find(cc); it != _locale.locales_by_country.end())
        {
            for (const auto i : it->second.items)
            {
                out.push_back(_locale.locales[i]);
                out.back()._signature = {.seed = 0, .data = locale_hash_()};
            }
        }
        return out;
    }

    // -- Time zones -----------------------------------------------------------

    /// @brief Draw a time zone of a country, by population.
    /// @param request The country, the reference date and the selection mode.
    /// @return The zone.
    /// @throws std::runtime_error If the zone tables are not loaded.
    /// @throws std::invalid_argument As the seeded overload.
    [[nodiscard]] time_zone get_time_zone(const time_zone_request& request)
    {
        return get_time_zone(request, _engine());
    }

    /// @brief Draw a time zone of a country, by population, deterministically.
    ///
    /// The country is the request's, or else drawn from the `"country"`
    /// stream as get_country() draws it; the zone is drawn from the
    /// `"time_zone"` stream, weighted by its share of the population. The
    /// offset, abbreviation and daylight flag are those at the reference
    /// date, 12:00 UTC.
    ///
    /// @param request The country, the reference date and the selection mode.
    /// @param call_seed Seed for reproducible results.
    /// @return The zone.
    /// @throws std::runtime_error If no countries or no zone tables are loaded.
    /// @throws std::invalid_argument For an unknown country, one with no zone,
    ///         or an invalid reference date.
    /// @throws std::out_of_range For a reference date before the zone's data.
    [[nodiscard]] time_zone get_time_zone(const time_zone_request& request, std::uint64_t call_seed) const
    {
        require_locale_table_(country_table::zones);
        const auto& m = _locale;
        bool drawn = false;
        std::string cc;
        if (request.country)
        {
            cc = known_country_(*request.country);
        }
        else
        {
            cc = draw_country_(call_seed, request.weighted, m.zone_countries, m.zones_by_country, "time_zone_country");
            drawn = true;
        }
        const auto it = m.zones_by_country.find(cc);
        if (it == m.zones_by_country.end()) throw std::invalid_argument("No time zone data for country: " + cc);
        auto r = core::rng{call_seed}.split("time_zone");
        const auto& items = it->second.items;
        const auto index = items[request.weighted ? it->second.weighted(r)
                                                  : static_cast<std::size_t>(core::bounded(r, items.size()))];
        return zone_at_(m.zones[index], request.reference_date,
                        {.seed = call_seed,
                         .data = drawn ? core::hash_combine(locale_hash_(), data_hash()) : locale_hash_()});
    }

    /// @brief Generate a batch of time zones from a seed drawn like
    ///        get_time_zone() draws its own.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<time_zone> get_time_zone_batch(const time_zone_request& request,
                                                             const core::batch_options& options)
    {
        return get_time_zone_batch(request, options, _engine());
    }

    /// @brief Generate a batch of time zones, deterministically: item `i` is
    ///        `get_time_zone(request, core::item_seed(batch_seed, options.first + i))`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    /// @throws std::invalid_argument If the request or options are invalid.
    /// @throws std::runtime_error If no data is loaded, or a unique batch runs
    ///         out of attempts.
    [[nodiscard]] std::vector<time_zone> get_time_zone_batch(const time_zone_request& request,
                                                             const core::batch_options& options,
                                                             std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed,
                               [&](std::uint64_t item_seed) { return get_time_zone(request, item_seed); });
    }

    /// @brief Stream a batch of time zones to a sink without holding the batch.
    /// @tparam Sink Callable taking `(std::uint64_t, time_zone&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items, in index order.
    template <typename Sink>
    void for_each_time_zone(const time_zone_request& request, const core::batch_options& options,
                            std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_time_zone(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of time zones with a batch seed drawn like
    ///        get_time_zone() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, time_zone&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items, in index order.
    template <typename Sink>
    void for_each_time_zone(const time_zone_request& request, const core::batch_options& options, Sink&& sink)
    {
        for_each_time_zone(request, options, _engine(), std::forward<Sink>(sink));
    }

    /// @brief Look up a time zone by its IANA name.
    ///
    /// Accepts IANA's `backward` links (`Asia/Calcutta` is `Asia/Kolkata`),
    /// which GeoNames and older systems write; the result's name is then the
    /// current one. A zone two countries share is returned with the country
    /// IANA lists it for.
    ///
    /// @param name IANA name.
    /// @param reference_date The date whose offset is reported, at 12:00 UTC.
    /// @return The zone (seed 0).
    /// @throws std::runtime_error If the zone tables are not loaded.
    /// @throws std::invalid_argument For an unknown name or an invalid date.
    /// @throws std::out_of_range For a reference date before the zone's data.
    [[nodiscard]] time_zone find_time_zone(std::string_view name,
                                           std::chrono::year_month_day reference_date =
                                               time_zone_request::default_reference_date) const
    {
        require_locale_table_(country_table::zones);
        auto it = _locale.zone_by_name.find(name);
        if (it == _locale.zone_by_name.end())
        {
            if (const auto link = _locale.zone_links.find(name); link != _locale.zone_links.end())
            {
                it = _locale.zone_by_name.find(link->second);
            }
        }
        if (it == _locale.zone_by_name.end()) throw std::invalid_argument("Unknown time zone: " + std::string{name});
        return zone_at_(_locale.zones[it->second], reference_date, {.seed = 0, .data = locale_hash_()});
    }

    /// @brief Every time zone of a country, largest share first, then by name,
    ///        at the default reference date.
    /// @param country ISO 3166-1 alpha-2 code, any case.
    /// @return The zones (seed 0); empty for a country with none.
    /// @throws std::runtime_error If no countries or no zone tables are loaded.
    /// @throws std::invalid_argument For an unknown country.
    [[nodiscard]] std::vector<time_zone> time_zones(std::string_view country) const
    {
        require_locale_table_(country_table::zones);
        const auto cc = known_country_(country);
        std::vector<time_zone> out;
        if (const auto it = _locale.zones_by_country.find(cc); it != _locale.zones_by_country.end())
        {
            for (const auto i : it->second.items)
            {
                out.push_back(zone_at_(_locale.zones[i], time_zone_request::default_reference_date,
                                       {.seed = 0, .data = locale_hash_()}));
            }
        }
        return out;
    }

    // -- Holidays and currencies ----------------------------------------------

    /// @brief A country's holidays in a year.
    ///
    /// A lookup, not a draw: no seed. Sorted by date, then key, then day.
    /// Public and bank holidays always; optional days and observances when
    /// asked. A day off a weekend rule moves has `observed` set (design 8.4);
    /// a move python-holidays records but no rule gives is its own entry,
    /// named as python-holidays names it ("Christmas Day (observed)").
    ///
    /// @param request The country, the year and the kinds.
    /// @return The holidays.
    /// @throws std::runtime_error If no countries or no holiday tables are loaded.
    /// @throws std::invalid_argument For an unknown country, or one with no
    ///         holiday data.
    /// @throws std::out_of_range For a year outside holiday_years().
    [[nodiscard]] std::vector<holiday> holidays(const holiday_request& request) const
    {
        const auto& hc = holiday_country_(request.country);
        check_holiday_year_(hc, request.year);
        return evaluate_(hc, request.year, request.optional, request.observances);
    }

    /// @brief Whether a date is a holiday's date or its observed date.
    ///
    /// Looks at the holidays of the year before and after too, since an
    /// observed day can cross New Year (the US's New Year's Day 2022 was
    /// observed on 2021-12-31).
    ///
    /// @param country ISO 3166-1 alpha-2 code, any case.
    /// @param date The date.
    /// @param optional Count holiday_kind::optional days too.
    /// @return True for a public or bank holiday (or optional, if asked).
    /// @throws std::runtime_error If no countries or no holiday tables are loaded.
    /// @throws std::invalid_argument For an unknown country, one with no
    ///         holiday data, or an invalid date.
    /// @throws std::out_of_range For a date whose year is outside holiday_years().
    [[nodiscard]] bool is_holiday(std::string_view country, std::chrono::year_month_day date,
                                  bool optional = false) const
    {
        if (!date.ok()) throw std::invalid_argument("is_holiday: not a valid date");
        const auto& hc = holiday_country_(country);
        const int y = static_cast<int>(date.year());
        check_holiday_year_(hc, y);
        for (int year = std::max(y - 1, hc.years.from); year <= std::min(y + 1, hc.years.to); ++year)
        {
            for (const auto& h : evaluate_(hc, year, optional, false))
            {
                if (h.date == date || h.observed == date) return true;
            }
        }
        return false;
    }

    /// @brief The years holidays() serves for a country.
    /// @param country ISO 3166-1 alpha-2 code, any case.
    /// @return The span (python-holidays' years for the country, within 1970 to 2050).
    /// @throws std::runtime_error If no countries or no holiday tables are loaded.
    /// @throws std::invalid_argument For an unknown country, or one with no
    ///         holiday data.
    [[nodiscard]] year_span holiday_years(std::string_view country) const
    {
        return holiday_country_(country).years;
    }

    /// @brief The ISO 4217 minor unit of a currency (0 for JPY, 3 for KWD).
    /// @param code ISO 4217 alphabetic code, any case.
    /// @return The digits, 0 to 4.
    /// @throws std::runtime_error If the currency table is not loaded.
    /// @throws std::invalid_argument For an unknown code.
    [[nodiscard]] std::uint8_t currency_digits(std::string_view code) const
    {
        require_locale_table_(country_table::currencies);
        const auto it = _locale.currencies.find(country_detail::upper(code));
        if (it == _locale.currencies.end()) throw std::invalid_argument("Unknown currency code: " + std::string{code});
        return it->second.digits;
    }

    /// @brief Whether any locale table (locales, zones, currencies, holidays)
    ///        is loaded.
    /// @return False when only `countries.tsv` is.
    [[nodiscard]] bool has_locale_data() const
    {
        return std::ranges::any_of(_locale_tables, [](const core::layered_table& t) { return !t.empty(); });
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

    /// @brief Load country data from a TSV file or a tier directory,
    ///        replacing all data and layers loaded before.
    ///
    /// A file is `countries.tsv` (see doc/data.md); the locale tables are
    /// then left as they are. A directory such as `resources/lite` must hold
    /// `countries.tsv` and loads every other table it holds; the ones it does
    /// not hold are cleared. Any other path is ignored.
    ///
    /// @param path Path to the TSV file or the directory.
    /// @throws core::tsv_error If a file is malformed; the data is then
    ///         unchanged.
    void load(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
        {
            const auto countries = path / data_file_;
            if (!std::filesystem::is_regular_file(countries, ec)) return;
            replace_all_(core::tsv_document::load(countries), read_locale_directory_(path));
            return;
        }
        if (!std::filesystem::is_regular_file(path, ec)) return;
        (void)extend(core::tsv_document::load(path), core::layer_mode::replace);
    }

    /// @brief Load a dataset tier, replacing all data and layers loaded
    ///        before.
    ///
    /// Loads `countries.tsv`, then every locale table of the tier it finds
    /// (a missing one is cleared). Each uses the compiled-in copy when the
    /// library is built with `DASMIG_COUNTRY_EMBED` (the countries) or
    /// `DASMIG_COUNTRY_EMBED_LOCALE` (the locale tables), otherwise searches
    /// `resources/`, `../resources`, `country-generator/resources` and
    /// `../country-generator/resources`.
    ///
    /// @param tier The dataset size to load.
    /// @return `true` if the tier's `countries.tsv` was found and loaded.
    /// @throws core::tsv_error If a file is malformed; the data is then
    ///         unchanged.
    [[nodiscard]] bool load(dataset tier)
    {
        const std::string dir{core::tier_name(tier)};
        const std::string path = dir + "/" + std::string{data_file_};
        auto doc = core::load_resource(path, embedded_files_(),
                                       core::find_resources(library_name_, path));
        if (!doc) return false;
        const auto root = core::find_resources(library_name_, dir + "/" + std::string{file_name_(country_table::locales)});
        locale_documents docs;
        for (const auto t : locale_tables_)
        {
            docs.at(index_(t)) = core::load_resource(dir + "/" + std::string{file_name_(t)}, embedded_locale_files_(), root);
        }
        replace_all_(std::move(*doc), std::move(docs));
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

    /// @brief Add a data layer to one table.
    ///
    /// Rows are keyed by the table's key columns (doc/data.md). In the
    /// default override mode a row replaces the loaded row with the same key
    /// and new keys are added; a row with `disabled` set to 1 removes its key.
    /// A locale table's rows must name loaded countries, and refer to loaded
    /// rows of the other tables (a locale's currency, a link's zone, a
    /// holiday's event dates and span).
    ///
    /// @param which The table.
    /// @param doc TSV with that table's columns.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend(country_table which, core::tsv_document doc,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        if (which == country_table::countries) return extend(std::move(doc), mode);
        locale_documents docs;
        docs.at(index_(which)).emplace(std::move(doc));
        apply_locale_(std::move(docs), mode, false);
        return _locale_tables.at(index_(which)).layers().back();
    }

    /// @brief Add a data layer to one table from a TSV file.
    /// @param which The table.
    /// @param file The layer file.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If the file cannot be read or a row is
    ///         invalid; the data is then unchanged.
    const core::layer_info& extend(country_table which, const std::filesystem::path& file,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        return extend(which, core::tsv_document::load(file), mode);
    }

    /// @brief Add a data layer to one table from TSV text.
    /// @param which The table.
    /// @param text TSV with that table's columns.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend_from_string(country_table which, std::string_view text, std::string source,
                                               core::layer_mode mode = core::layer_mode::override)
    {
        return extend(which, core::tsv_document::parse(text, std::move(source)), mode);
    }

    /// @brief The data layers of `countries.tsv` in effect, in order.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& layers() const
    {
        return _table.layers();
    }

    /// @brief The data layers of one table in effect, in order.
    /// @param which The table.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& layers(country_table which) const
    {
        if (which == country_table::countries) return _table.layers();
        return _locale_tables.at(index_(which)).layers();
    }

    /// @brief Hash of the loaded `countries.tsv`, recorded in each country's
    ///        signature() (the locale tables have their own, which locales,
    ///        zones and holidays record).
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

    // Rebuilds the countries' indexes and, unless with_locale is false, the locale model on them.
    void rebuild_(bool with_locale = true)
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
        if (with_locale) _locale = build_locale_model_(_locale_tables, false);
    }

    // -- Locale tables ----------------------------------------------------------

    static constexpr std::size_t locale_table_count_ = 7;
    static constexpr std::array<country_table, locale_table_count_> locale_tables_{
        country_table::locales,    country_table::zones,    country_table::zone_links,    country_table::currencies,
        country_table::holidays,   country_table::holiday_events, country_table::holiday_years};

    using locale_documents = std::array<std::optional<core::tsv_document>, locale_table_count_>;
    using locale_table_set = std::array<core::layered_table, locale_table_count_>;

    static constexpr std::size_t index_(country_table t) { return static_cast<std::size_t>(t) - 1; }

    static constexpr std::string_view file_name_(country_table t)
    {
        switch (t)
        {
        case country_table::countries: return "countries.tsv";
        case country_table::locales: return "locales.tsv";
        case country_table::zones: return "zones.tsv";
        case country_table::zone_links: return "zone_links.tsv";
        case country_table::currencies: return "currencies.tsv";
        case country_table::holidays: return "holidays.tsv";
        case country_table::holiday_events: return "holiday_events.tsv";
        case country_table::holiday_years: return "holiday_years.tsv";
        }
        return "";
    }

    static core::table_schema locale_schema_(country_table t)
    {
        switch (t)
        {
        case country_table::locales:
            return {.key = {"country", "tag"},
                    .required = {"language", "language_name", "share", "basis", "date_short", "date_medium",
                                 "date_long", "date_full", "time_short", "time_medium", "months", "months_abbr",
                                 "weekdays", "weekdays_abbr", "am", "pm", "eras", "decimal", "group", "minus",
                                 "number_pattern", "currency_pattern", "currency_code", "currency_symbol",
                                 "first_day", "weekend_start", "weekend_end", "measurement"},
                    .optional = {"script", "currency_spacing"}};
        case country_table::zones:
            return {.key = {"country", "zone"}, .required = {"share", "basis", "periods"}, .optional = {"posix"}};
        case country_table::zone_links: return {.key = {"link"}, .required = {"zone"}};
        case country_table::currencies:
            return {.key = {"code"}, .required = {"numeric", "digits"}, .optional = {"cash_digits"}};
        case country_table::holidays:
            return {.key = {"country", "key", "from_year"},
                    .required = {"kind", "rule", "days", "observed", "to_year"},
                    .optional = {"name", "name_en"}};
        case country_table::holiday_events: return {.key = {"event", "country", "date"}};
        case country_table::holiday_years: return {.key = {"country"}, .required = {"from_year", "to_year"}};
        case country_table::countries: break;
        }
        return schema_();
    }

    static locale_table_set make_locale_tables_()
    {
        return {core::layered_table{locale_schema_(country_table::locales)},
                core::layered_table{locale_schema_(country_table::zones)},
                core::layered_table{locale_schema_(country_table::zone_links)},
                core::layered_table{locale_schema_(country_table::currencies)},
                core::layered_table{locale_schema_(country_table::holidays)},
                core::layered_table{locale_schema_(country_table::holiday_events)},
                core::layered_table{locale_schema_(country_table::holiday_years)}};
    }

    // -- Row parsing (throws core::tsv_error located at the row) ----------------

    static std::chrono::weekday weekday_cell_(const core::tsv_row& row, std::string_view column)
    {
        const auto d = country_detail::parse_weekday(row.get<std::string_view>(column));
        if (!d) throw row.error(column, "expected sun, mon, tue, wed, thu, fri or sat");
        return *d;
    }

    template <std::size_t N>
    static void list_cell_(const core::tsv_row& row, std::string_view column, std::array<std::string, N>& out)
    {
        const auto items = row.list(column);
        if (items.size() != N || std::ranges::any_of(items, [](std::string_view s) { return s.empty(); }))
        {
            throw row.error(column, "expected " + std::to_string(N) + " names separated by ';'");
        }
        for (std::size_t i = 0; i < N; ++i) out.at(i) = std::string{items[i]};
    }

    static locale parse_locale_(const core::tsv_row& row)
    {
        locale l;
        l.country_code = row.get<std::string>("country");
        l.tag = row.get<std::string>("tag");
        l.language = row.get<std::string>("language");
        l.script = row.get_or<std::string>("script", "");
        l.language_name = row.get<std::string>("language_name");
        const auto lowercase = [](std::string_view s) {
            return std::ranges::all_of(s, [](char c) { return c >= 'a' && c <= 'z'; });
        };
        const bool script_ok = l.script.empty() || (l.script.size() == 4 && l.script[0] >= 'A' &&
                                                    l.script[0] <= 'Z' && lowercase(l.script.substr(1)));
        if (l.language.size() < 2 || l.language.size() > 3 || !lowercase(l.language))
        {
            throw row.error("language", "expected an ISO 639 code of 2 or 3 lowercase letters");
        }
        if (!script_ok) throw row.error("script", "expected an ISO 15924 code such as Latn");
        if (!core::is_alpha2(l.country_code)) throw row.error("country", "expected an ISO 3166-1 alpha-2 code");
        if (l.tag != l.language + (l.script.empty() ? "" : "-" + l.script) + "-" + l.country_code)
        {
            throw row.error("tag", "expected language[-Script]-REGION from the language, script and country columns");
        }
        l.share = row.get<double>("share");
        if (!(l.share > 0.0) || l.share > 100.0) throw row.error("share", "must be above 0 and at most 100");
        l.basis = row.get<std::string>("basis");
        if (l.basis != "cldr" && l.basis != "inherited" && l.basis != "fallback")
        {
            throw row.error("basis", "expected cldr, inherited or fallback");
        }
        for (const auto& [column, field, time] :
             {std::tuple{"date_short", &locale::date_short, false}, std::tuple{"date_medium", &locale::date_medium, false},
              std::tuple{"date_long", &locale::date_long, false}, std::tuple{"date_full", &locale::date_full, false},
              std::tuple{"time_short", &locale::time_short, true}, std::tuple{"time_medium", &locale::time_medium, true}})
        {
            l.*field = row.get<std::string>(column);
            if (const auto error = country_detail::check_pattern(l.*field, time); !error.empty())
            {
                throw row.error(column, error);
            }
        }
        list_cell_(row, "months", l.months);
        list_cell_(row, "months_abbr", l.months_abbr);
        list_cell_(row, "weekdays", l.weekdays);
        list_cell_(row, "weekdays_abbr", l.weekdays_abbr);
        list_cell_(row, "eras", l.eras);
        l.am = row.get<std::string>("am");
        l.pm = row.get<std::string>("pm");
        l.decimal = row.get<std::string>("decimal");
        l.group = row.get<std::string>("group");
        l.minus = row.get<std::string>("minus");
        for (const auto& [column, field, currency] : {std::tuple{"number_pattern", &locale::number_pattern, false},
                                               std::tuple{"currency_pattern", &locale::currency_pattern, true}})
        {
            l.*field = row.get<std::string>(column);
            std::string error;
            (void)country_detail::parse_number_pattern(l.*field, currency, error);
            if (!error.empty()) throw row.error(column, error);
        }
        l.currency_spacing = row.get_or<std::string>("currency_spacing", "");
        l.currency_code = row.get<std::string>("currency_code");
        if (!is_currency_code_(l.currency_code)) throw row.error("currency_code", "expected three capital letters");
        l.currency_symbol = row.get<std::string>("currency_symbol");
        l.first_day = weekday_cell_(row, "first_day");
        l.weekend_start = weekday_cell_(row, "weekend_start");
        l.weekend_end = weekday_cell_(row, "weekend_end");
        const auto m = row.get<std::string_view>("measurement");
        if (m == "metric") l.measurement = measurement::metric;
        else if (m == "us") l.measurement = measurement::us;
        else if (m == "uk") l.measurement = measurement::uk;
        else throw row.error("measurement", "expected metric, us or uk");
        return l;
    }

    static bool is_currency_code_(std::string_view c)
    {
        return c.size() == 3 && std::ranges::all_of(c, [](char ch) { return ch >= 'A' && ch <= 'Z'; });
    }

    static time_zone parse_zone_(const core::tsv_row& row)
    {
        time_zone z;
        z.country_code = row.get<std::string>("country");
        if (!core::is_alpha2(z.country_code)) throw row.error("country", "expected an ISO 3166-1 alpha-2 code");
        z.name = row.get<std::string>("zone");
        z.share = row.get<double>("share");
        if (!(z.share > 0.0) || z.share > 100.0) throw row.error("share", "must be above 0 and at most 100");
        z.basis = row.get<std::string>("basis");
        if (z.basis != "tzdata" && z.basis != "geonames" && z.basis != "fallback")
        {
            throw row.error("basis", "expected tzdata, geonames or fallback");
        }
        try
        {
            z.rules = core::tz_rules::parse(row.get<std::string_view>("periods"), row.get_or<std::string_view>("posix", ""));
        }
        catch (const std::invalid_argument& e)
        {
            throw row.error("periods", e.what());
        }
        return z;
    }

    struct currency_entry
    {
        std::string numeric;
        std::uint8_t digits{2};
        std::optional<std::uint8_t> cash_digits;
    };

    static currency_entry parse_currency_(const core::tsv_row& row)
    {
        if (!is_currency_code_(row.get<std::string_view>("code"))) throw row.error("code", "expected three capital letters");
        currency_entry c;
        c.numeric = row.get<std::string>("numeric");
        if (c.numeric.size() != 3 || c.numeric.find_first_not_of("0123456789") != std::string::npos)
        {
            throw row.error("numeric", "expected three digits");
        }
        const auto digits = row.get<unsigned>("digits");
        if (digits > 4) throw row.error("digits", "must be from 0 to 4");
        c.digits = static_cast<std::uint8_t>(digits);
        if (row.has("cash_digits"))
        {
            const auto cash = row.get<unsigned>("cash_digits");
            if (cash > 4) throw row.error("cash_digits", "must be from 0 to 4");
            c.cash_digits = static_cast<std::uint8_t>(cash);
        }
        return c;
    }

    static country_detail::holiday_row parse_holiday_(const core::tsv_row& row)
    {
        if (!core::is_alpha2(row.get<std::string_view>("country")))
        {
            throw row.error("country", "expected an ISO 3166-1 alpha-2 code");
        }
        country_detail::holiday_row h;
        h.key = row.get<std::string>("key");
        if (!country_detail::valid_key(h.key)) throw row.error("key", "expected [a-z0-9_]+");
        h.name = row.get_or<std::string>("name", "");
        h.name_en = row.get_or<std::string>("name_en", "");
        const auto kind = country_detail::parse_kind(row.get<std::string_view>("kind"));
        if (!kind) throw row.error("kind", "expected public, bank, optional or observance");
        h.kind = *kind;
        auto rule = country_detail::parse_rule(row.get<std::string_view>("rule"));
        if (!rule) throw row.error("rule", "expected MM-DD, MM:N:dow, MM-DD>dow, MM-DD<dow, easter+N, orthodox+N or event:key+N");
        h.rule = std::move(*rule);
        h.days = row.get<unsigned>("days");
        if (h.days < 1 || h.days > 10) throw row.error("days", "must be from 1 to 10");
        auto observed = country_detail::parse_policy(row.get<std::string_view>("observed"));
        if (!observed) throw row.error("observed", "expected none, next_weekday, weekend>next or a weekday map such as sat<fri,sun>mon");
        h.observed = std::move(*observed);
        h.from = row.get<int>("from_year");
        h.to = row.get<int>("to_year");
        if (h.from < 1 || h.to > 9998 || h.from > h.to) throw row.error("to_year", "must be from from_year to 9998");
        return h;
    }

    struct event_row
    {
        std::string_view event;     // views into the row
        std::string_view country;   // "*" for the shared date
        std::chrono::sys_days date;
    };

    static event_row parse_event_(const core::tsv_row& row)
    {
        event_row e;
        e.event = row.get<std::string_view>("event");
        if (!country_detail::valid_key(e.event)) throw row.error("event", "expected [a-z0-9_]+");
        e.country = row.get<std::string_view>("country");
        if (e.country != "*" && !core::is_alpha2(e.country))
        {
            throw row.error("country", "expected * (every country) or an ISO 3166-1 alpha-2 code");
        }
        const auto d = country_detail::parse_iso_date(row.get<std::string_view>("date"));
        if (!d) throw row.error("date", "expected a valid YYYY-MM-DD date");
        e.date = std::chrono::sys_days{*d};
        return e;
    }

    static year_span parse_years_(const core::tsv_row& row)
    {
        if (!core::is_alpha2(row.get<std::string_view>("country")))
        {
            throw row.error("country", "expected an ISO 3166-1 alpha-2 code");
        }
        const year_span s{.from = row.get<int>("from_year"), .to = row.get<int>("to_year")};
        if (s.from < 1 || s.to > 9998 || s.from > s.to) throw row.error("to_year", "must be from from_year to 9998");
        return s;
    }

    // -- The model the getters read ---------------------------------------------

    struct pick_list
    {
        std::vector<std::size_t> items;   // in draw order
        core::discrete weighted{1.0};
    };

    struct holiday_country
    {
        year_span years;
        unsigned weekend{0b1000001U};   // bits by c_encoding: Saturday and Sunday
        std::vector<country_detail::holiday_row> rows;
        std::map<std::string, std::vector<std::chrono::sys_days>, std::less<>> events;
    };

    struct locale_model
    {
        std::vector<locale> locales;
        std::map<std::string, std::size_t, std::less<>> locale_by_tag;           // lowercase tag
        std::map<std::string, pick_list, std::less<>> locales_by_country;        // share desc, then tag
        std::map<std::string, pick_list, std::less<>> locale_languages;          // countries, by speakers
        pick_list locale_countries;                                              // countries with a locale
        std::vector<time_zone> zones;
        std::map<std::string, std::size_t, std::less<>> zone_by_name;
        std::map<std::string, std::string, std::less<>> zone_links;
        std::map<std::string, pick_list, std::less<>> zones_by_country;          // share desc, then name
        pick_list zone_countries;                                                // countries with a zone
        std::map<std::string, currency_entry, std::less<>> currencies;
        std::map<std::string, holiday_country, std::less<>> holidays;
    };

    // Builds the model from locale tables and the loaded countries. strict: every row is parsed, and
    // a bad row, a row of a country that is not loaded, or a reference to a missing row of another
    // table, is an error located at the row; so the strict build that applies a layer is its
    // validation. Otherwise (after the countries change) a row of a missing country is left out.
    [[nodiscard]] locale_model build_locale_model_(const locale_table_set& t, bool strict) const
    {
        locale_model m;
        build_locales_(m, t, strict);
        build_zones_(m, t, strict);
        build_holidays_(m, t, strict);
        return m;
    }

    // Whether a row's country is loaded; when strict, a row of one that is not is an error.
    [[nodiscard]] bool row_loaded_(const core::tsv_row& row, bool strict) const
    {
        const auto cc = row["country"];
        if (_cca2_index.contains(cc)) return true;
        if (strict) throw row.error("country", "country " + std::string{cc} + " is not loaded");
        return false;
    }

    static void weigh_(pick_list& p, const std::vector<double>& weights)
    {
        if (!weights.empty()) p.weighted = core::discrete{weights};
    }

    // Sorts a country's rows by share, largest first, then by name, and weights the draw by share.
    template <typename Row, typename Name>
    static void order_by_share_(pick_list& p, const std::vector<Row>& rows, Name name)
    {
        std::ranges::sort(p.items, [&](std::size_t a, std::size_t b) {
            if (rows[a].share != rows[b].share) return rows[a].share > rows[b].share;
            return name(rows[a]) < name(rows[b]);
        });
        std::vector<double> weights;
        weights.reserve(p.items.size());
        for (const auto i : p.items) weights.push_back(rows[i].share);
        weigh_(p, weights);
    }

    // The loaded countries that by_country has, weighted as get_country() weighs them.
    template <typename ByCountry>
    [[nodiscard]] pick_list countries_with_(const ByCountry& by_country) const
    {
        pick_list p;
        std::vector<double> weights;
        for (std::size_t i = 0; i < _countries.size(); ++i)
        {
            if (!by_country.contains(_countries[i].cca2)) continue;
            p.items.push_back(i);
            weights.push_back(weight_(_countries[i]));
        }
        weigh_(p, weights);
        return p;
    }

    // Currencies, then locales, their indexes and their draws.
    void build_locales_(locale_model& m, const locale_table_set& t, bool strict) const
    {
        for (const auto& row : t.at(index_(country_table::currencies)).rows())
        {
            m.currencies.emplace(std::string{row["code"]}, parse_currency_(row));
        }
        for (const auto& row : t.at(index_(country_table::locales)).rows())
        {
            if (!row_loaded_(row, strict)) continue;
            auto l = parse_locale_(row);
            const auto currency = m.currencies.find(l.currency_code);
            if (currency == m.currencies.end())
            {
                if (strict) throw row.error("currency_code", "no currencies.tsv row for " + l.currency_code);
                continue;
            }
            l.currency_digits = currency->second.digits;
            m.locales.push_back(std::move(l));
        }
        std::map<std::string, std::map<std::string, double>, std::less<>> language_shares;   // language -> cc -> share
        for (std::size_t i = 0; i < m.locales.size(); ++i)
        {
            const auto& l = m.locales[i];
            m.locale_by_tag.emplace(country_detail::lower(l.tag), i);
            m.locales_by_country[l.country_code].items.push_back(i);
            auto& share = language_shares[l.language][l.country_code];
            share = std::max(share, l.share);
        }
        for (auto& [cc, p] : m.locales_by_country)
        {
            order_by_share_(p, m.locales, [](const locale& l) -> const std::string& { return l.tag; });
        }
        for (const auto& [language, shares] : language_shares)
        {
            pick_list p;
            std::vector<double> weights;
            for (std::size_t i = 0; i < _countries.size(); ++i)
            {
                const auto it = shares.find(_countries[i].cca2);
                if (it == shares.end()) continue;
                p.items.push_back(i);
                weights.push_back(weight_(_countries[i]) * it->second / 100.0);
            }
            if (p.items.empty()) continue;
            weigh_(p, weights);
            m.locale_languages.emplace(language, std::move(p));
        }
        m.locale_countries = countries_with_(m.locales_by_country);
    }

    // Zones, their indexes and draws, then the links to them.
    void build_zones_(locale_model& m, const locale_table_set& t, bool strict) const
    {
        for (const auto& row : t.at(index_(country_table::zones)).rows())
        {
            if (row_loaded_(row, strict)) m.zones.push_back(parse_zone_(row));
        }
        for (std::size_t i = 0; i < m.zones.size(); ++i)
        {
            const auto& z = m.zones[i];
            m.zones_by_country[z.country_code].items.push_back(i);
            const auto [it, added] = m.zone_by_name.emplace(z.name, i);
            // A zone two countries share answers for the one IANA lists it for.
            if (!added && m.zones[it->second].basis == "geonames" && z.basis != "geonames") it->second = i;
        }
        for (auto& [cc, p] : m.zones_by_country)
        {
            order_by_share_(p, m.zones, [](const time_zone& z) -> const std::string& { return z.name; });
        }
        m.zone_countries = countries_with_(m.zones_by_country);
        for (const auto& row : t.at(index_(country_table::zone_links)).rows())
        {
            const auto target = row.get<std::string_view>("zone");
            if (!m.zone_by_name.contains(target))
            {
                if (strict) throw row.error("zone", "zone " + std::string{target} + " is not loaded");
                continue;
            }
            m.zone_links.emplace(std::string{row["link"]}, std::string{target});
        }
    }

    // Event dates, keyed by views into the tables, which outlive the build: the shared dates of
    // each event, and each country's own and overriding dates with their rows.
    using shared_events_ = std::map<std::string_view, std::vector<std::chrono::sys_days>, std::less<>>;
    using own_events_ = std::map<std::pair<std::string_view, std::string_view>,
                                 std::vector<std::pair<std::chrono::sys_days, const core::tsv_row*>>>;

    // Replaces the shared date nearest each of a country's own dates, which must be within three days.
    static void override_dates_(std::vector<std::chrono::sys_days>& dates,
                                const std::vector<std::pair<std::chrono::sys_days, const core::tsv_row*>>& mine,
                                const std::string& ev)
    {
        for (const auto& [date, row] : mine)
        {
            auto near = std::ranges::min_element(dates, {}, [date](std::chrono::sys_days d) {
                return d > date ? (d - date).count() : (date - d).count();
            });
            const auto gap = near == dates.end() ? 4 : std::abs((*near - date).count());
            if (gap > 3)
            {
                throw row->error("date", "no shared date of " + ev + " within three days");
            }
            *near = date;
        }
    }

    // An event's dates for a country: the shared dates with the country's own replacing the one
    // within three days of each; or, for an event with no shared dates, the country's own. Null
    // when there are none (an error at the rule's row when strict).
    [[nodiscard]] static const std::vector<std::chrono::sys_days>*
    event_dates_(holiday_country& hc, const std::string& cc, const std::string& ev, const core::tsv_row& at,
                 const shared_events_& shared, const own_events_& own, bool strict)
    {
        if (const auto it = hc.events.find(ev); it != hc.events.end()) return &it->second;
        const auto mine = own.find({cc, ev});
        const auto base = shared.find(ev);
        std::vector<std::chrono::sys_days> dates;
        if (base != shared.end())
        {
            dates = base->second;
            if (mine != own.end()) override_dates_(dates, mine->second, ev);
        }
        else if (mine != own.end())
        {
            for (const auto& [date, row] : mine->second) dates.push_back(date);
        }
        else
        {
            if (strict) throw at.error("rule", "no holiday_events.tsv dates for event " + ev);
            return nullptr;
        }
        std::ranges::sort(dates);
        return &hc.events.emplace(ev, std::move(dates)).first->second;
    }

    // The weekend of a locale (CLDR weekData) as bits by c_encoding.
    [[nodiscard]] static unsigned weekend_mask_(const locale& l)
    {
        unsigned mask = 0;
        for (unsigned d = l.weekend_start.c_encoding();; d = (d + 1) % 7)
        {
            mask |= 1U << d;
            if (d == l.weekend_end.c_encoding()) break;
        }
        return mask;
    }

    // A holiday row's names. A continuation row (both empty) takes those of the nearest row above
    // with its country and key; names holds the last row's, as views into the table.
    static void resolve_names_(country_detail::holiday_row& h, const core::tsv_row& row,
                               std::pair<std::string_view, std::string_view>& names)
    {
        if (h.name.empty() && h.name_en.empty())
        {
            if (names.second.empty())
            {
                throw row.error("name_en", "empty names need a row above with the same country and key");
            }
            h.name = names.first;
            h.name_en = names.second;
            return;
        }
        if (h.name_en.empty()) throw row.error("name_en", "missing value");
        names = {row[h.name.empty() ? "name_en" : "name"], row["name_en"]};
        if (h.name.empty()) h.name = h.name_en;
    }

    // A country's holiday entry, made on first use with its span and the weekend of its first
    // locale (else Saturday and Sunday).
    static holiday_country& holiday_entry_(locale_model& m, const std::map<std::string, year_span, std::less<>>& spans,
                                           const std::string& cc)
    {
        auto [it, added] = m.holidays.try_emplace(cc);
        if (added)
        {
            it->second.years = spans.at(cc);
            const auto l = std::ranges::find(m.locales, cc, &locale::country_code);
            if (l != m.locales.end()) it->second.weekend = weekend_mask_(*l);
        }
        return it->second;
    }

    // Holidays: the event dates, the spans, then each country's rows, with its weekend.
    void build_holidays_(locale_model& m, const locale_table_set& t, bool strict) const
    {
        shared_events_ shared;
        own_events_ own;
        for (const auto& row : t.at(index_(country_table::holiday_events)).rows())
        {
            const auto e = parse_event_(row);
            if (e.country == "*")
            {
                shared[e.event].push_back(e.date);
            }
            else if (row_loaded_(row, strict))
            {
                own[{e.country, e.event}].emplace_back(e.date, &row);
            }
        }
        for (auto& [k, dates] : shared) std::ranges::sort(dates);
        std::map<std::string, year_span, std::less<>> spans;
        for (const auto& row : t.at(index_(country_table::holiday_years)).rows())
        {
            if (row_loaded_(row, strict)) spans.emplace(std::string{row["country"]}, parse_years_(row));
        }
        // (country, key) -> the names of the last row with them.
        std::map<std::pair<std::string_view, std::string_view>, std::pair<std::string_view, std::string_view>>
            last_names;
        for (const auto& row : t.at(index_(country_table::holidays)).rows())
        {
            if (!row_loaded_(row, strict)) continue;
            const std::string cc{row["country"]};
            if (!spans.contains(cc))
            {
                if (strict) throw row.error("country", "no holiday_years.tsv row for " + cc);
                continue;
            }
            auto h = parse_holiday_(row);
            resolve_names_(h, row, last_names[{row["country"], row["key"]}]);
            auto& hc = holiday_entry_(m, spans, cc);
            if (h.rule.form == country_detail::rule_form::event)
            {
                h.events = event_dates_(hc, cc, h.rule.event, row, shared, own, strict);
                if (h.events == nullptr) continue;
            }
            hc.rows.push_back(std::move(h));
        }
        for (const auto& [cc, span] : spans) (void)holiday_entry_(m, spans, cc);
    }

    locale_table_set _locale_tables{make_locale_tables_()};
    locale_model _locale;

    // Applies locale-table documents together: all or, if any fails, none.
    void apply_locale_(locale_documents docs, core::layer_mode mode, bool reset)
    {
        auto next = _locale_tables;
        for (const auto t : locale_tables_)
        {
            auto& doc = docs.at(index_(t));
            auto& tbl = next.at(index_(t));
            if (!doc)
            {
                if (reset) tbl.clear();
                continue;
            }
            (void)tbl.extend(std::move(*doc), mode);   // the strict build below validates every row
        }
        auto built = build_locale_model_(next, true);
        _locale_tables = std::move(next);
        _locale = std::move(built);
    }

    // Replaces every table: the countries, then the locale tables (a missing one is cleared).
    void replace_all_(core::tsv_document countries, locale_documents docs)
    {
        auto saved = _table;
        (void)_table.extend(std::move(countries), core::layer_mode::replace,
                            [](const core::tsv_row& row) { (void)parse_(row); });
        try
        {
            rebuild_(false);
            apply_locale_(std::move(docs), core::layer_mode::replace, true);
        }
        catch (...)
        {
            _table = std::move(saved);
            rebuild_(false);
            throw;
        }
    }

    static locale_documents read_locale_directory_(const std::filesystem::path& dir)
    {
        locale_documents docs;
        for (const auto t : locale_tables_)
        {
            const auto file = dir / file_name_(t);
            std::error_code ec;
            if (std::filesystem::is_regular_file(file, ec)) docs.at(index_(t)) = core::tsv_document::load(file);
        }
        return docs;
    }

    static std::span<const core::embedded_file> embedded_locale_files_()
    {
#ifdef DASMIG_COUNTRY_EMBED_LOCALE
        return embedded::countrygen_locale::files;
#else
        return {};
#endif
    }

    [[nodiscard]] std::uint64_t locale_hash_() const
    {
        std::uint64_t h = 0;
        for (const auto& t : _locale_tables) h = core::hash_combine(h, t.data_hash());
        return h;
    }

    void require_locale_table_(country_table t) const
    {
        if (_locale_tables.at(index_(t)).empty())
        {
            throw std::runtime_error(
                "No locale data loaded. Load a tier with the locale tables, or define DASMIG_COUNTRY_EMBED_LOCALE.");
        }
    }

    // The code in capitals, when that country is loaded.
    [[nodiscard]] std::string known_country_(std::string_view code) const
    {
        require_data_();
        auto cc = country_detail::upper(code);
        if (!_cca2_index.contains(cc)) throw core::unknown_country_error(code);
        return cc;
    }

    // Draws a country exactly as get_country() does; one with no rows in by_country is replaced by a
    // draw, from the fallback stream, among those that have some.
    template <typename ByCountry>
    [[nodiscard]] std::string draw_country_(std::uint64_t call_seed, bool weighted, const pick_list& with,
                                            const ByCountry& by_country, std::string_view fallback) const
    {
        require_data_();
        auto r = core::rng{call_seed}.split("country");
        const auto idx = weighted ? _distribution(r) : static_cast<std::size_t>(core::bounded(r, _countries.size()));
        if (by_country.contains(_countries[idx].cca2)) return _countries[idx].cca2;
        if (with.items.empty()) throw std::invalid_argument("No loaded country has this data");
        auto again = core::rng{call_seed}.split(fallback);
        const auto j = weighted ? with.weighted(again) : static_cast<std::size_t>(core::bounded(again, with.items.size()));
        return _countries[with.items[j]].cca2;
    }

    [[nodiscard]] static time_zone zone_at_(const time_zone& base, std::chrono::year_month_day reference,
                                            core::seed_signature signature)
    {
        if (!reference.ok()) throw std::invalid_argument("time zone: the reference date is not valid");
        time_zone z = base;
        const std::chrono::sys_seconds t{std::chrono::sys_days{reference} + std::chrono::hours{12}};
        const auto info = z.rules.at(t);
        z.utc_offset = info.offset;
        z.abbreviation = std::string{info.abbreviation};
        z.dst = info.dst;
        z.observes_dst = z.rules.observes_dst_after(t);
        z._signature = signature;
        return z;
    }

    [[nodiscard]] const holiday_country& holiday_country_(std::string_view country) const
    {
        require_locale_table_(country_table::holiday_years);
        const auto cc = known_country_(country);
        const auto it = _locale.holidays.find(cc);
        if (it == _locale.holidays.end()) throw std::invalid_argument("No holiday data for country " + cc);
        return it->second;
    }

    static void check_holiday_year_(const holiday_country& hc, int year)
    {
        if (year < hc.years.from || year > hc.years.to)
        {
            throw std::out_of_range("Year " + std::to_string(year) + " is outside the holiday years, " +
                                    std::to_string(hc.years.from) + " to " + std::to_string(hc.years.to));
        }
    }

    // A year's holidays as design 8.3 and 8.4 compute them: every rule's days in the year and the years
    // either side, sorted by date, key and day; then, kind by kind, each occurrence's days moved by
    // its policy; the occurrences that start in the year, sorted by date, key and day.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity): the year's evaluation of design 8.3 and 8.4
    [[nodiscard]] static std::vector<holiday> evaluate_(const holiday_country& hc, int year, bool optional,
                                                        bool observances)
    {
        using std::chrono::sys_days;
        struct entry
        {
            sys_days date;
            const country_detail::holiday_row* row;
            unsigned day;
            sys_days start;
        };
        const auto wanted = [&](holiday_kind k) {
            return (k != holiday_kind::optional || optional) && (k != holiday_kind::observance || observances);
        };
        std::vector<entry> entries;
        std::vector<sys_days> starts;
        for (int y = year - 1; y <= year + 1; ++y)
        {
            for (const auto& row : hc.rows)
            {
                if (y < row.from || y > row.to || !wanted(row.kind)) continue;
                country_detail::rule_dates(row.rule, y, row.events, starts);
                for (const auto s : starts)
                {
                    for (unsigned i = 0; i < row.days; ++i)
                    {
                        entries.push_back({.date = s + std::chrono::days{i}, .row = &row, .day = i + 1, .start = s});
                    }
                }
            }
        }
        std::ranges::stable_sort(entries, [](const entry& a, const entry& b) {
            if (a.date != b.date) return a.date < b.date;
            if (a.row->key != b.row->key) return a.row->key < b.row->key;
            return a.day < b.day;
        });
        const auto day_of = [](sys_days d) { return static_cast<int>(d.time_since_epoch().count()); };
        std::vector<holiday> out;
        std::vector<std::optional<sys_days>> moved;
        for (const auto kind : {holiday_kind::public_, holiday_kind::bank, holiday_kind::optional, holiday_kind::observance})
        {
            std::set<int> taken;
            for (const auto& e : entries)
            {
                if (e.row->kind == kind) taken.insert(day_of(e.date));
            }
            if (taken.empty()) continue;
            std::set<std::pair<const country_detail::holiday_row*, int>> done;
            std::map<std::pair<int, std::string_view>, int> shared;
            for (const auto& e : entries)
            {
                if (e.row->kind != kind || !done.emplace(e.row, day_of(e.start)).second) continue;
                country_detail::simulate(e.row->observed, e.start, e.row->days, taken, hc.weekend, shared, moved);
                if (country_detail::year_of(e.start) != year) continue;
                for (unsigned i = 0; i < e.row->days; ++i)
                {
                    holiday h;
                    h.date = std::chrono::year_month_day{e.start + std::chrono::days{i}};
                    if (moved[i]) h.observed = std::chrono::year_month_day{*moved[i]};
                    h.key = e.row->key;
                    h.name = e.row->name;
                    h.name_en = e.row->name_en;
                    h.kind = kind;
                    h.day = i + 1;
                    out.push_back(std::move(h));
                }
            }
        }
        std::ranges::sort(out, [](const holiday& a, const holiday& b) {
            if (a.date != b.date) return a.date < b.date;
            if (a.key != b.key) return a.key < b.key;
            return a.day < b.day;
        });
        return out;
    }
};

} // namespace dasmig

#endif // DASMIG_COUNTRYGEN_HPP
