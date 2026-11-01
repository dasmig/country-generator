// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2026 Diego Dasso Migotto

// Prints the library's answers for the validation scripts (doc/data.md):
//
//   dump holidays TIER_DIR    every country's holidays, every year of its span:
//                             country, date, observed, kind, key, name_en
//
// Build: clang++ -std=c++23 -O2 -I. -o build/dump tests/dump.cpp

#include "dasmig/countrygen.hpp"

#include <iostream>
#include <string>

namespace
{

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

} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string{argv[1]} == "holidays") return holidays(argv[2]);
        std::cerr << "usage: dump holidays TIER_DIR\n";
        return 2;
    }
    catch (const std::exception& e)
    {
        std::cerr << "dump: " << e.what() << '\n';
        return 1;
    }
}
