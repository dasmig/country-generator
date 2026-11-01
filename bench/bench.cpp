// Benchmarks for country-generator; see bench.hpp for the options.

#include "../dasmig/countrygen.hpp"
#include "bench.hpp"

int main(int argc, char** argv)
{
    dasmig::bench::runner run{"country-generator", argc, argv};

    run.load("load", [&] {
        dasmig::cntg gen;
        return gen.load(run.tier());
    });

    dasmig::cntg gen;
    if (!gen.load(run.tier())) return run.missing_tier();

    run.calls("get_country", [&](std::uint64_t seed) { return gen.get_country({}, seed); });
    run.calls("get_country Europe uniform", [&](std::uint64_t seed) {
        return gen.get_country({.region = "Europe", .weighted = false}, seed);
    });
    run.calls("get_country to_json", [&](std::uint64_t seed) {
        return gen.get_country({}, seed).to_json();
    });

    run.calls("get_locale", [&](std::uint64_t seed) { return gen.get_locale({}, seed); });
    run.calls("get_locale CA", [&](std::uint64_t seed) { return gen.get_locale({.country = "CA"}, seed); });
    run.calls("get_time_zone", [&](std::uint64_t seed) { return gen.get_time_zone({}, seed); });
    const auto pt_br = gen.find_locale("pt-BR");
    run.calls("format_date pt-BR long", [&](std::uint64_t seed) {
        const auto day = std::chrono::sys_days{std::chrono::year{2024} / 1 / 1} + std::chrono::days{seed % 366};
        return pt_br.format_date(day, dasmig::date_style::long_);
    });
    run.calls("format_money pt-BR", [&](std::uint64_t seed) {
        return pt_br.format_money(dasmig::core::money::from_minor(static_cast<std::int64_t>(seed % 10'000'000), "BRL", 2));
    });
    run.calls("holidays BR year", [&](std::uint64_t seed) {
        return gen.holidays({.country = "BR", .year = 1970 + static_cast<int>(seed % 81)});
    });

    run.batches("get_country_batch", [&](std::size_t count, std::size_t threads, std::uint64_t seed) {
        return gen.get_country_batch({}, {.count = count, .threads = threads}, seed);
    });

    return run.finish();
}
