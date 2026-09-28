#pragma once

#include <array>

namespace dracoved {

struct VedicBenchmarkRules {
    // Moorthi, Tara, own natal BAV, own Prastara Kaksha bindu.
    // Kaksha starts at zero so existing saved/default scores are unchanged.
    std::array<double, 4> weights{1, 1, 1, 0};
    // Book's good fractions mapped by 2*f-1; 50% is our chosen zero benchmark.
    // Gold, Silver, Copper, Iron.
    std::array<double, 4> benefic{1, .5, 0, -.5};
    std::array<double, 4> malefic{-.5, 1, .5, 0};
    // Book: medium / very good / bad / good / bad / good / bad / good / moderate.
    // These numerical encodings are editable research choices, not textual rules.
    std::array<double, 9> tara{0, 1, -1, .5, -1, .5, -1, .5, 0};
    // Sun Moon Mars Mercury Jupiter Venus Saturn Rahu Ketu. Moon's slot is unused:
    // it has no Moorthi. Mercury defaults to benefic; the choice is editable.
    std::array<bool, 9> useMalefic{true, false, true, false, false, false, true, true, true};
    bool weightByDasha = false;
    // Custom research weights, MD / AD / PD / SD / PrD; not a classical formula.
    std::array<double, 5> dashaWeights{3, 2, 1, .5, .25};
    bool usePlanetWeights = false;
    std::array<double, 9> planetWeights{1, 1, 1, 1, 1, 1, 1, 1, 1};
};

struct VedicBenchmarkScore {
    bool valid = false;
    double net = 0;
    // Weighted contributions sum to net. Unavailable components have no weight.
    std::array<double, 4> points{};
};

// planet 0–8; metal 0–3 (ignored for Moon); tara 1–9; BAV -1 = unavailable, else 0–8.
VedicBenchmarkScore scoreVedicBenchmark(int planet, int metal, int tara, int bav,
                                      const VedicBenchmarkRules& rules, int kaksha = -1);
// The highest enabled role (lowest level index) wins; repeated lords count once.
double vedicDashaWeight(unsigned roleMask, const VedicBenchmarkRules& rules);
// Manual importance multiplies any active dasha weighting; zero excludes.
double vedicPlanetWeight(int planet, unsigned roleMask, const VedicBenchmarkRules& rules);
} // namespace dracoved
