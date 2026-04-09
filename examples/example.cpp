#include <iostream>
#include "../dasmig/countrygen.hpp"

int main()
{
    // Singleton auto-probes for lite first, falls back to full.
    auto& gen = dasmig::cntg::instance();

    // --- Or load a specific tier explicitly: ---
    // dasmig::cntg gen;
    // gen.load(dasmig::dataset::lite);  // ~195 sovereign states
    // gen.load(dasmig::dataset::full);  // ~250 countries & territories

    // Generate random countries (population-weighted).
    std::cout << "=== Random countries ===" << '\n';
    for (int i = 0; i < 10; ++i)
    {
        auto c = gen.get_country();
        std::cout << c.name_common << " (" << c.cca2
                  << ", pop. " << c.population << ")" << '\n';
    }

    // Generate countries from a specific region.
    std::cout << "\n=== European countries ===" << '\n';
    for (int i = 0; i < 5; ++i)
    {
        auto c = gen.get_country("Europe");
        std::cout << c.name_common << ", " << c.subregion
                  << " (pop. " << c.population
                  << ", tz: " << c.timezones << ")" << '\n';
    }

    // Access all available fields.
    std::cout << "\n=== Full country details ===" << '\n';
    auto c = gen.get_country();
    std::cout << "Name:          " << c.name_common << '\n';
    std::cout << "Official:      " << c.name_official << '\n';
    std::cout << "ISO:           " << c.cca2 << " / " << c.cca3
              << " / " << c.ccn3 << '\n';
    std::cout << "Capital:       " << c.capital << '\n';
    std::cout << "Region:        " << c.region << " / "
              << c.subregion << '\n';
    std::cout << "Continent:     " << c.continent << '\n';
    std::cout << "Coordinates:   " << c.latitude << ", "
              << c.longitude << '\n';
    std::cout << "Area:          " << c.area << " km\u00B2" << '\n';
    std::cout << "Population:    " << c.population << '\n';
    std::cout << "Landlocked:    " << (c.landlocked ? "yes" : "no")
              << '\n';
    std::cout << "Independent:   " << (c.independent ? "yes" : "no")
              << '\n';
    std::cout << "UN Member:     " << (c.un_member ? "yes" : "no")
              << '\n';
    std::cout << "Languages:     " << c.languages << '\n';
    std::cout << "Currency:      " << c.currency_name << " ("
              << c.currency_code << " " << c.currency_symbol << ")"
              << '\n';
    std::cout << "Borders:       " << c.borders << '\n';
    std::cout << "Timezones:     " << c.timezones << '\n';
    std::cout << "Driving side:  " << c.driving_side << '\n';
    std::cout << "TLD:           " << c.tld << '\n';
    std::cout << "Phone:         " << c.idd_root << c.idd_suffix
              << '\n';
    std::cout << "Demonym (m/f): " << c.demonym_m << " / "
              << c.demonym_f << '\n';
    std::cout << "Flag:          " << c.flag_emoji << '\n';
    std::cout << "Income level:  " << c.income_level << '\n';
    std::cout << "Start of week: " << c.start_of_week << '\n';

    // Deterministic generation.
    std::cout << "\n=== Seeded (deterministic) ===" << '\n';
    auto seeded = gen.get_country(42);
    std::cout << "seed 42 -> " << seeded.name_common << ", "
              << seeded.cca2 << '\n';

    auto replay = gen.get_country(seeded.seed());
    std::cout << "replay  -> " << replay.name_common << ", "
              << replay.cca2 << '\n';

    // Seed the engine for a deterministic sequence.
    gen.seed(100);
    std::cout << "\n=== Seeded sequence ===" << '\n';
    for (int i = 0; i < 3; ++i)
    {
        std::cout << gen.get_country() << '\n';
    }
    gen.unseed();

    // Uniform random selection.
    gen.weighted(false);
    std::cout << "\n=== Uniform (unweighted) ===" << '\n';
    for (int i = 0; i < 5; ++i)
    {
        std::cout << gen.get_country() << '\n';
    }
    gen.weighted(true);

    // Library info.
    std::cout << "\n=== Info ===" << '\n';
    std::cout << "Countries loaded: " << gen.country_count() << '\n';
}
