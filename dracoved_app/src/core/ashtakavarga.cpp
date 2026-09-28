#include "ashtakavarga.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace dracoved {
namespace {
constexpr uint16_t houses(std::initializer_list<int> values) {
    uint16_t mask = 0;
    for (int house : values) mask |= uint16_t(1 << (house - 1));
    return mask;
}

// Benefic houses counted inclusively from each natal donor, in the order above.
// P.V.R. Narasimha Rao, Vedic Astrology: An Integrated Approach, ch. 12,
// tables 19–25 (printed pp. 146–149). In particular Venus-from-Mars uses 4, not 5.
// https://vedicastrologer.org/articles/vedic_astro_textbook.pdf
constexpr uint16_t rules[7][8] = {
    {houses({1,2,4,7,8,9,10,11}), houses({3,6,10,11}), houses({1,2,4,7,8,9,10,11}), houses({3,5,6,9,10,11,12}),
     houses({5,6,9,11}), houses({6,7,12}), houses({1,2,4,7,8,9,10,11}), houses({3,4,6,10,11,12})},
    {houses({3,6,7,8,10,11}), houses({1,3,6,7,9,10,11}), houses({2,3,5,6,10,11}), houses({1,3,4,5,7,8,10,11}),
     houses({1,2,4,7,8,10,11}), houses({3,4,5,7,9,10,11}), houses({3,5,6,11}), houses({3,6,10,11})},
    {houses({3,5,6,10,11}), houses({3,6,11}), houses({1,2,4,7,8,10,11}), houses({3,5,6,11}),
     houses({6,10,11,12}), houses({6,8,11,12}), houses({1,4,7,8,9,10,11}), houses({1,3,6,10,11})},
    {houses({5,6,9,11,12}), houses({2,4,6,8,10,11}), houses({1,2,4,7,8,9,10,11}), houses({1,3,5,6,9,10,11,12}),
     houses({6,8,11,12}), houses({1,2,3,4,5,8,9,11}), houses({1,2,4,7,8,9,10,11}), houses({1,2,4,6,8,10,11})},
    {houses({1,2,3,4,7,8,9,10,11}), houses({2,5,7,9,11}), houses({1,2,4,7,8,10,11}), houses({1,2,4,5,6,9,10,11}),
     houses({1,2,3,4,7,8,10,11}), houses({2,5,6,9,10,11}), houses({3,5,6,12}), houses({1,2,4,5,6,7,9,10,11})},
    {houses({8,11,12}), houses({1,2,3,4,5,8,9,11,12}), houses({3,4,6,9,11,12}), houses({3,5,6,9,11}),
     houses({5,8,9,10,11}), houses({1,2,3,4,5,8,9,10,11}), houses({3,4,5,8,9,10,11}), houses({1,2,3,4,5,8,9,11})},
    {houses({1,2,4,7,8,10,11}), houses({3,6,11}), houses({3,5,6,10,11,12}), houses({6,8,9,10,11,12}),
     houses({5,6,11,12}), houses({6,11,12}), houses({3,5,6,11}), houses({1,3,4,6,10,11})}
};
constexpr bool validTotals() {
    constexpr int totals[] = {48,49,39,54,56,52,39};
    for (int planet = 0; planet < 7; ++planet) {
        int total = 0;
        for (auto mask : rules[planet]) total += std::popcount(mask);
        if (total != totals[planet]) return false;
    }
    return true;
}
static_assert(validTotals());

int zodiacSign(double longitude) {
    double normalized = std::fmod(longitude, 360.0);
    if (normalized < 0) normalized += 360.0;
    return int(normalized / 30.0) % 12;
}
}

const QStringList& ashtakavargaPlanets() {
    static const QStringList names = {"Sun", "Moon", "Mars", "Mercury", "Jupiter", "Venus", "Saturn"};
    return names;
}

bool kakshaBinduAt(const AshtakavargaResult& natal, int planet, double longitude,
                   KakshaBindu* out) {
    if (out) *out = {};
    if (!out || planet < 0 || planet >= 7 || !std::isfinite(longitude)) return false;
    double normalized = std::fmod(longitude, 360.0);
    if (normalized < 0) normalized += 360.0;
    const int sign = int(normalized / 30.0);
    const double within = normalized - sign * 30.0;
    const int section = std::min(7, int(within / 3.75));
    constexpr int donors[] = {6, 4, 2, 0, 5, 3, 1, 7};
    const int donor = donors[section];
    *out = {sign, section, donor, natal.contributions[planet][donor][sign]};
    return true;
}

QString kakshaDonorName(int donor) {
    return donor == 7 ? "Lagna" : ashtakavargaPlanets().value(donor);
}

bool computeAshtakavarga(const NatalChart& chart, AshtakavargaResult* out, QString* error) {
    if (!out) return false;
    *out = {};
    if (error) error->clear();
    auto fail = [&](const QString& message) { if (error) *error = message; return false; };
    // Use the supplied D1 zodiac consistently for every donor and transit lookup.
    AshtakavargaResult result;
    for (int planet = 0; planet < 7; ++planet) {
        const BodyPosition* found = nullptr;
        for (const auto& body : chart.bodies) if (body.name == ashtakavargaPlanets()[planet]) { found = &body; break; }
        if (!found || !std::isfinite(found->longitude))
            return fail("Natal position unavailable: " + ashtakavargaPlanets()[planet]);
        result.referenceSigns[planet] = zodiacSign(found->longitude);
    }
    if (!std::isfinite(chart.angles.asc)) return fail("Natal Ascendant unavailable.");
    result.referenceSigns[7] = zodiacSign(chart.angles.asc);
    for (int planet = 0; planet < 7; ++planet) for (int donor = 0; donor < 8; ++donor) {
        for (int house = 0; house < 12; ++house) {
            if (!(rules[planet][donor] & (1 << house))) continue;
            const int sign = (result.referenceSigns[donor] + house) % 12;
            result.contributions[planet][donor][sign] = 1;
            ++result.bav[planet][sign];
            ++result.sav[sign];
        }
    }
    *out = result;
    return true;
}

} // namespace dracoved
