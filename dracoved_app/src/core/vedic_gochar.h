#pragma once

#include <array>
#include <limits>
#include <QString>
#include <QStringList>

namespace dracoved {

// The book supplies qualitative results and several exact fractions, but no
// single numerical index. These settings are explicit research encodings.
struct GocharRules {
    double tierPoints = 50.0;          // ordinal house tier -> graph points
    double nakshatraSuppression = .75; // fraction of a positive result suspended
    double aspectShare = .35;          // share of aspect result in the mixed result
    double dignityFloor = .5;          // maps book dignity fraction into [floor, 1]
    double retrogradeShift = .2;       // signed improvement/worsening of magnitude
    double combustionFactor = .8;     // custom potency factor
    double combustionOrb = 8.0;       // degrees, custom research threshold
    double pakshaFactor = 1.1;        // custom potency factor
    std::array<double, 5> avasthaFactors{.5, .75, 1, .25, 0}; // Balya to Mrita
    double samagamamOrb = 1.0;        // degrees, custom research threshold
    double samagamamShare = 0.0;      // optional share in final continuous score
    std::array<bool, 9> useMalefic{true,false,true,false,false,false,true,true,true};
};

struct GocharContext {
    double jd = 0, birthJd = 0;
    // Sun, Moon, Mars, Mercury, Jupiter, Venus, Saturn, Rahu, Ketu.
    std::array<double, 9> natal{};
    std::array<double, 9> transit{};
    std::array<bool, 9> retrograde{};
    GocharContext() {
        natal.fill(std::numeric_limits<double>::quiet_NaN());
        transit.fill(std::numeric_limits<double>::quiet_NaN());
    }
};

struct GocharReading {
    bool valid = false;
    int house = 0, tier = 0, paryaya = 0;
    QString signVedha, paryayaVedha, nakshatraVedha, strengthNotes, aspects, samagamam;
    double base = 0, afterSignVedha = 0, afterNakshatraVedha = 0;
    double afterParyaya = 0, afterStrength = 0, afterAspects = 0;
    double samagamamScore = 0, combined = 0;
};

struct NavamsaTransitRules {
    double contactPoints = 50; // research scale, not a numerical formula in the book
    double blendShare = .5;    // D9 share when both D1 and D9 scores are available
    // The Jupiter paragraph does not specify the reference for "Dusthanas".
    // 0: leave conditional contacts unscored; 1/2: explicit Moon/Lagna research convention.
    int jupiterDusthanaReference = 0;
};
struct NavamsaTransitReading {
    bool valid = false;
    bool supported = false;
    bool incomplete = false; // a supported rule could not be resolved at this moment
    double score = 0;
    QString details;
};
// Ordinary transit longitude -> natal D9 signs; never transit longitude converted to D9.
NavamsaTransitReading calculateNavamsaTransit(int planet, double transitLongitude,
    const std::array<double, 9>& natal, double natalAscendant, const NavamsaTransitRules& rules);

// Mode 0 is the existing Custom Composite; modes 1-8 are book-oriented views.
QStringList gocharViewNames();
double gocharViewScore(const GocharReading& reading, int mode);
std::array<GocharReading, 9> calculateGochar(const GocharContext& context, const GocharRules& rules);
} // namespace dracoved
