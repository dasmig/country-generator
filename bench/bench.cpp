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

    return run.finish();
}
