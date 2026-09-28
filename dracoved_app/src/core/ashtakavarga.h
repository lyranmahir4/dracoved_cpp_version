#pragma once

#include "chart_types.h"
#include <array>

namespace dracoved {

// Order: Sun, Moon, Mars, Mercury, Jupiter, Venus, Saturn; Lagna is donor 8.
const QStringList& ashtakavargaPlanets();
struct AshtakavargaResult {
    std::array<int, 8> referenceSigns{};
    std::array<std::array<std::array<int, 12>, 8>, 7> contributions{};
    std::array<std::array<int, 12>, 7> bav{};
    std::array<int, 12> sav{};
};

// A sign has eight 3°45' Kakshas, ruled in the book's order:
// Saturn, Jupiter, Mars, Sun, Venus, Mercury, Moon, Lagna.
struct KakshaBindu {
    int sign = -1;
    int section = -1; // 0..7, starting at 0° of the sign
    int donor = -1;   // index in contributions[planet][donor][sign]
    int bindu = -1;   // 0 or 1
};
// Own natal Prastara bindu; transit longitude must use the natal zodiac. Nodes have no BAV.
bool kakshaBinduAt(const AshtakavargaResult& natal, int planet, double longitude,
                   KakshaBindu* out);
QString kakshaDonorName(int donor);

// Unreduced natal BAV and seven-planet SAV. No node BAV or shodhana reductions.
// Uses the supplied D1 zodiac (sidereal or tropical research); failure clears output.
bool computeAshtakavarga(const NatalChart& chart, AshtakavargaResult* out, QString* error);

} // namespace dracoved
