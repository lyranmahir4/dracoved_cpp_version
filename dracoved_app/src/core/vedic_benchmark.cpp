#include "vedic_benchmark.h"
#include <cmath>

namespace dracoved {
double vedicPlanetWeight(int planet, unsigned roleMask, const VedicBenchmarkRules& rules) {
    if(planet<0 || planet>=9) return 0;
    const double manual=rules.usePlanetWeights?rules.planetWeights[planet]:1;
    if(!std::isfinite(manual) || manual<0) return 0;
    return manual*vedicDashaWeight(roleMask,rules);
}
double vedicDashaWeight(unsigned roleMask, const VedicBenchmarkRules& rules) {
    if (!rules.weightByDasha || !roleMask) return 1;
    for (int level=0; level<5; ++level) if (roleMask & (1u<<level)) {
        const double weight=rules.dashaWeights[level];
        return std::isfinite(weight) && weight>=0 ? weight : 0;
    }
    return 0;
}
VedicBenchmarkScore scoreVedicBenchmark(int planet, int metal, int tara, int bav,
                                      const VedicBenchmarkRules& rules, int kaksha) {
    VedicBenchmarkScore score;
    if (planet < 0 || planet > 8 || (planet != 1 && (metal < 0 || metal > 3)) || tara < 1 || tara > 9 || bav < -1 || bav > 8 || kaksha < -1 || kaksha > 1) return score;
    const std::array<bool, 4> available{planet != 1, true, bav >= 0, planet < 7 && kaksha >= 0};
    const std::array<double, 4> values{
        available[0] ? (rules.useMalefic[planet] ? rules.malefic : rules.benefic)[metal] : 0,
        rules.tara[tara - 1], bav < 0 ? 0 : (bav - 4) / 4.0, kaksha < 0 ? 0 : kaksha ? 1.0 : -1.0
    };
    double denominator = 0;
    for (int i = 0; i < 4; ++i) {
        if (!available[i]) continue;
        if (!std::isfinite(rules.weights[i]) || rules.weights[i] < 0 || !std::isfinite(values[i]) || std::abs(values[i]) > 1) return score;
        denominator += rules.weights[i];
    }
    if (denominator <= 0) return score;
    for (int i = 0; i < 4; ++i) {
        if (!available[i]) continue;
        score.points[i] = 100 * rules.weights[i] * values[i] / denominator;
        score.net += score.points[i];
    }
    score.valid = true;
    return score;
}
} // namespace dracoved
