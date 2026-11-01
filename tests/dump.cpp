// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

// Prints the library's answers for the validation scripts (doc/data.md):
//
//   dump holidays TIER_DIR        every country's holidays, every year of its span:
//                                 country, date, observed, kind, key, name_en
//   dump zones TIER_DIR           every country's zones: a Z line with the zone's first
//                                 instant and its rules there, then a T line per change
//                                 before 2038 with the rules one second before and at it
//   dump formats TIER_DIR CASES   every locale (an L line with its tag), then one line per
//                                 case of the CASES file (date, time, number or money)
//
// Build: clang++ -std=c++23 -O2 -I. -o build/dump tests/dump.cpp

#include "dasmig/countrygen.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

// The tier's countries, in countries.tsv order.
std::vector<std::string> country_codes(const std::filesystem::path& dir)
{
    std::vector<std::string> out;
    for (const auto& row : dasmig::core::tsv_document::load(dir / "countries.tsv").rows())
    {
        out.push_back(row.get<std::string>("cca2"));
    }
    return out;
}

int holidays(const std::filesystem::path& dir)
{
    dasmig::cntg gen;
    gen.load(dir);
    const auto years = dasmig::core::tsv_document::load(dir / "holiday_years.tsv");
    std::string out;
    for (const auto& row : years.rows())
    {
        const auto cc = row.get<std::string>("country");
        const auto span = gen.holiday_years(cc);
        for (int y = span.from; y <= span.to; ++y)
        {
            for (const auto& h : gen.holidays({.country = cc, .year = y, .optional = true, .observances = true}))
            {
                out += cc;
                out += '\t';
                out += dasmig::country_detail::iso_date(h.date);
                out += '\t';
                out += h.observed ? dasmig::country_detail::iso_date(*h.observed) : std::string{};
                out += '\t';
                out += dasmig::country_detail::kind_name(h.kind);
                out += '\t';
                out += h.key;
                out += '\t';
                out += h.name_en;
                out += '\n';
            }
        }
    }
    std::cout << out;
    return 0;
}

void put_info(std::string& out, const dasmig::core::tz_info& i)
{
    out += '\t';
    out += std::to_string(i.offset.count());
    out += '\t';
    out += i.abbreviation;
    out += '\t';
    out += i.dst ? '1' : '0';
}

int zones(const std::filesystem::path& dir)
{
    using namespace std::chrono;
    dasmig::cntg gen;
    gen.load(dir);
    const sys_seconds end{sys_days{year{2038} / 1 / 1}};
    std::string out;
    for (const auto& cc : country_codes(dir))
    {
        for (const auto& z : gen.time_zones(cc))
        {
            const auto from = z.rules.valid_from();
            out += "Z\t" + cc + '\t' + z.name + '\t' + std::to_string(from.time_since_epoch().count());
            put_info(out, z.rules.at(from));
            out += '\n';
            for (const auto& [t, after] : z.rules.transitions(from + seconds{1}, end))
            {
                out += "T\t" + std::to_string(t.time_since_epoch().count());
                put_info(out, z.rules.at(t - seconds{1}));
                put_info(out, after);
                out += '\n';
            }
        }
    }
    std::cout << out;
    return 0;
}

// One case of the formats file: kind, then its fields.
struct format_case
{
    std::string kind;
    std::vector<std::string> fields;
};

std::vector<format_case> read_cases(const std::filesystem::path& file)
{
    std::ifstream in{file, std::ios::binary};
    if (!in) throw std::runtime_error("cannot read " + file.string());
    std::vector<format_case> out;
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        format_case c;
        std::size_t start = 0;
        for (std::size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1)
        {
            c.fields.push_back(line.substr(start, tab - start));
        }
        c.fields.push_back(line.substr(start));
        c.kind = c.fields.front();
        c.fields.erase(c.fields.begin());
        out.push_back(std::move(c));
    }
    return out;
}

int number(const std::string& s)
{
    return std::stoi(s);
}

std::string format_one(const dasmig::locale& l, const format_case& c)
{
    using namespace std::chrono;
    if (c.kind == "date")
    {
        // YYYY-MM-DD and a style, 0 (short) to 3 (full).
        const auto& d = c.fields.at(0);
        const year_month_day ymd{year{number(d.substr(0, d.size() - 6))},
                                 month{static_cast<unsigned>(number(d.substr(d.size() - 5, 2)))},
                                 day{static_cast<unsigned>(number(d.substr(d.size() - 2)))}};
        return l.format_date(ymd, static_cast<dasmig::date_style>(number(c.fields.at(1))));
    }
    if (c.kind == "time")
    {
        // HH:MM:SS and a style, 0 (short) or 1 (medium).
        const auto& t = c.fields.at(0);
        const hh_mm_ss<seconds> hms{hours{number(t.substr(0, 2))} + minutes{number(t.substr(3, 2))} +
                                    seconds{number(t.substr(6, 2))}};
        return l.format_time(hms, static_cast<dasmig::time_style>(number(c.fields.at(1))));
    }
    if (c.kind == "number") return l.format_number(std::strtod(c.fields.at(0).c_str(), nullptr));
    if (c.kind == "money")
    {
        // Minor units of the locale's own currency.
        return l.format_money(dasmig::core::money::from_minor(std::stoll(c.fields.at(0)), l.currency_code, l.currency_digits));
    }
    throw std::runtime_error("unknown case kind " + c.kind);
}

int formats(const std::filesystem::path& dir, const std::filesystem::path& cases_file)
{
    dasmig::cntg gen;
    gen.load(dir);
    const auto cases = read_cases(cases_file);
    std::string out;
    for (const auto& cc : country_codes(dir))
    {
        for (const auto& l : gen.locales(cc))
        {
            out += "L\t" + l.tag + '\n';
            for (const auto& c : cases)
            {
                out += format_one(l, c);
                out += '\n';
            }
        }
    }
    std::cout << out;
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (argc == 3 && mode == "holidays") return holidays(argv[2]);
        if (argc == 3 && mode == "zones") return zones(argv[2]);
        if (argc == 4 && mode == "formats") return formats(argv[2], argv[3]);
        std::cerr << "usage: dump holidays TIER_DIR | dump zones TIER_DIR | dump formats TIER_DIR CASES\n";
        return 2;
    }
    catch (const std::exception& e)
    {
        std::cerr << "dump: " << e.what() << '\n';
        return 1;
    }
}
