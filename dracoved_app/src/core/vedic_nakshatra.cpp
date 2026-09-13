#include "vedic_nakshatra.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace dracoved {
namespace {

constexpr double kFullCircle = 360.0;
constexpr double kPadaSpan = 10.0 / 3.0;

const std::array<double, 108>& padaBoundaries() {
    static const std::array<double, 108> value = [] {
        std::array<double, 108> result{};
        for (int i = 0; i < static_cast<int>(result.size()); ++i) {
            result[static_cast<size_t>(i)] = (i * 10.0) / 3.0;
        }
        return result;
    }();
    return value;
}

const QStringList& names() {
    static const QStringList value = {
        "Ashwini", "Bharani", "Krittika", "Rohini", "Mrigashira", "Ardra",
        "Punarvasu", "Pushya", "Ashlesha", "Magha", "Purva Phalguni",
        "Uttara Phalguni", "Hasta", "Chitra", "Swati", "Vishakha",
        "Anuradha", "Jyeshtha", "Mula", "Purva Ashadha", "Uttara Ashadha",
        "Shravana", "Dhanishtha", "Shatabhisha", "Purva Bhadrapada",
        "Uttara Bhadrapada", "Revati",
    };
    return value;
}

const QStringList& lords() {
    static const QStringList value = {
        "Ketu", "Venus", "Sun", "Moon", "Mars", "Rahu", "Jupiter", "Saturn", "Mercury",
        "Ketu", "Venus", "Sun", "Moon", "Mars", "Rahu", "Jupiter", "Saturn", "Mercury",
        "Ketu", "Venus", "Sun", "Moon", "Mars", "Rahu", "Jupiter", "Saturn", "Mercury",
    };
    return value;
}

double normalizeFinite(double value) {
    if (!std::isfinite(value)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double normalized = std::fmod(value, kFullCircle);
    if (normalized < 0.0) {
        normalized += kFullCircle;
    }
    // fmod(-0, 360) is -0; keeping the canonical zero makes boundary tests
    // and display deterministic.
    if (normalized >= kFullCircle) {
        return std::nextafter(kFullCircle, 0.0);
    }
    return normalized == 0.0 ? 0.0 : normalized;
}

}  // namespace

TaraPlacement classifyVedicTara(int natalNakshatra, int transitNakshatra) {
    if (natalNakshatra < 0 || natalNakshatra >= 27 || transitNakshatra < 0 || transitNakshatra >= 27)
        return {};
    static const QStringList taraNames = {"Janma", "Sampat", "Vipat", "Kshema", "Pratyak",
        "Daivanukula", "Naidhana", "Mitra", "Parama Maitra"};
    const int count = (transitNakshatra - natalNakshatra + 27) % 27 + 1;
    const int number = (count - 1) % 9 + 1;
    return {count, number, taraNames[number - 1]};
}

QStringList vedicNakshatraNames() {
    return names();
}

QString vedicNakshatraLord(int index) {
    return index >= 0 && index < lords().size() ? lords().at(index) : QString();
}

NakshatraPlacement classifyVedicNakshatra(double siderealLongitude) {
    NakshatraPlacement result;
    const double longitude = normalizeFinite(siderealLongitude);
    if (!std::isfinite(longitude)) {
        return result;
    }

    // Canonical boundaries are 10/3 degrees apart. upper_bound gives an
    // exact boundary to the pada beginning at that boundary, while a value
    // one representable step below it remains in the preceding pada.
    const auto& boundaries = padaBoundaries();
    const int globalPada = std::clamp(
        static_cast<int>(std::upper_bound(boundaries.begin(), boundaries.end(), longitude)
                         - boundaries.begin()) - 1,
        0, 107);
    const int index = globalPada / 4;
    const int pada = globalPada % 4 + 1;
    result.valid = true;
    result.index = index;
    result.pada = pada;
    result.name = names().at(index);
    result.lord = lords().at(index);
    return result;
}

}  // namespace dracoved
