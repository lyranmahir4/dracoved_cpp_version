// Tajaka (Varshaphala) core calculations per P.V.R. Narasimha Rao's writings:
// - "Re-defining Tajaka Varshaphal Charts" (return-moment methodology)
// - "Vedic Astrology: An Integrated Approach", Part 4: Tajaka Analysis
//   (muntha, Tajaka aspects with deeptamsa orbs, ithasala/eesarpha rules)
//
// The return-moment basis (Sun returning to its natal TROPICAL longitude) is
// implemented in gui/return_calculation_service as tajakaSolarReturnTimeUtc;
// this module works on already-computed charts.
#pragma once

#include "chart_types.h"

#include <QString>
#include <QVector>

namespace dracoved {
namespace tajaka {

// --- Muntha ---------------------------------------------------------------

struct MunthaInfo {
    bool valid = false;
    double longitude = 0.0;   // natal Ascendant advanced by one sign per completed year
    int signIndex = -1;
    QString signName;
    QString lord;             // ruler of the muntha sign (Vedic rulership)
    int houseFromAnnualLagna = 0;  // whole-sign house in the annual chart (1..12)
    QString houseMeaning;     // classical meaning of that muntha house
};

// Natal Ascendant progresses one rasi per completed year of age. The degree
// within the sign is preserved (natal Asc 15 deg Sc -> age 1 -> 15 deg Sg).
MunthaInfo computeMuntha(const NatalChart& natalChart,
                         const NatalChart& annualChart,
                         int completedYears);

// --- Tajaka aspects ---------------------------------------------------------

enum class TajakaAspectKind {
    Conjunction,
    SemiSextile,
    Sextile,
    Square,
    Trine,
    Opposition,
};

enum class TajakaAspectNature {
    StrongBenefic,   // trine (5th/9th)
    WeakBenefic,     // sextile (3rd/11th)
    WeakMalefic,     // square (4th/10th)
    StrongMalefic,   // conjunction and opposition (7th)
    Neutral,         // semi-sextile (2nd/12th)
};

struct TajakaAspect {
    QString firstName;
    QString secondName;
    TajakaAspectKind kind = TajakaAspectKind::Conjunction;
    TajakaAspectNature nature = TajakaAspectNature::Neutral;
    double orb = 0.0;                  // |separation - aspect angle| in degrees
    double firstDeeptamsa = 0.0;
    double secondDeeptamsa = 0.0;
    bool withinFirstDeeptamsa = false;
    bool withinSecondDeeptamsa = false;
    bool mutual = false;               // vartamaana: each within the other's deeptamsa
    bool firstFaster = false;          // speed ordering per Tajaka
    double firstAdvancement = 0.0;     // degree within sign
    double secondAdvancement = 0.0;
    bool firstRetrograde = false;
    bool secondRetrograde = false;
    bool ithasala = false;             // applying per Tajaka advancement rules
    bool eesarpha = false;             // separating
    bool poorna = false;               // ithasala with advancements within 1 degree
    QString summary;
};

// Deeptamsa (aspect orb) of the seven classical planets.
double tajakaDeeptamsa(const QString& planetName);

QString tajakaAspectKindLabel(TajakaAspectKind kind);
QString tajakaAspectNatureLabel(TajakaAspectNature nature);

// Whole-sign Tajaka aspects among the seven classical planets (Sun..Saturn).
// Only pairs whose orbs are mutually within both deeptamsas are returned
// (a vartamaana aspect); the 6th/8th sign distance (quincunx) has no aspect.
QVector<TajakaAspect> computeTajakaAspects(const NatalChart& chart);

// Validates muntha, deeptamsa and the ithasala/eesarpha rules against the
// worked examples in the source material. Returns false with an explanation
// on the first failure.
bool tajakaSelfCheck(QString* error);

// --- Strengths (Harsha, Pancha Vargeeya, Dwadasha Vargeeya) ---------------

// Natural (naisargika) relation of a planet to the lord of a sign. Neutral
// and friend are grouped for the strength tables, which only give values for
// own / friend / enemy placements.
enum class TajakaRelation {
    Own,
    Friend,
    Enemy,
};

TajakaRelation naturalRelation(const QString& planet, int signIndex);

// Sign occupied by a planet's longitude in the given varga (D-2 .. D-12).
// Standard Parashari mappings; D-5 uses the odd/even sign tables and D-11
// progresses from the sign itself (documented conventions).
int divisionSign(int division, double longitude);

// Hadda (D-30-like) lord of a longitude, per the classical Tajaka table.
QString haddaLord(double longitude);

struct TajakaPlanetStrength {
    QString planet;
    // Harsha bala: four sources of 5 units each (max 20).
    bool harshaFavoredHouse = false;
    bool harshaExaltOrOwn = false;
    bool harshaGenderHouse = false;
    bool harshaDayNight = false;
    int harshaTotal = 0;
    // Pancha vargeeya bala components (max 20 overall).
    double kshetraBala = 0.0;
    double uchchaBala = 0.0;
    double haddaBala = 0.0;
    double drekkanaBala = 0.0;
    double navamsaBala = 0.0;
    double panchaVargeeya = 0.0;
    // Dwadasha vargeeya bala: strong minus weak across D-1..D-12.
    int dwadashaStrong = 0;
    int dwadashaWeak = 0;
    int dwadashaVargeeya = 0;
    QString panchaRating;  // weak / ordinary / strong / very strong / extraordinary
};

struct TajakaStrengths {
    bool valid = false;
    QVector<TajakaPlanetStrength> planets;  // the seven classical planets

    const TajakaPlanetStrength* forPlanet(const QString& name) const;
};

// All three strength systems for an annual chart. Houses are whole-sign from
// the annual Ascendant; the day/night source uses the chart's sect.
TajakaStrengths computeTajakaStrengths(const NatalChart& annualChart);

// --- Lord of the Year (Varsheswara) -----------------------------------------

struct TajakaLordOfYear {
    bool valid = false;
    QString planet;
    QStringList candidacy;       // ordered candidates with the reason each is a candidate
    QStringList beneficOnLagna;  // candidates with a benefic Tajaka aspect on lagna
    QString selectionReason;     // explanation of the selection rule applied
};

// The five classical candidates, shortlisted by benefic Tajaka aspect on the
// annual lagna and ranked by pancha vargeeya bala, with the documented
// fallback chain. natalChart must be the sidereal natal reference.
TajakaLordOfYear computeTajakaLordOfYear(const NatalChart& natalChart,
                                         const NatalChart& annualChart,
                                         const MunthaInfo& muntha,
                                         const TajakaStrengths& strengths);

}  // namespace tajaka
}  // namespace dracoved
