#include "tajaka.h"

#include "formatting.h"

#include <QHash>
#include <QPair>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace dracoved {
namespace tajaka {

namespace {

const QStringList& classicalPlanets() {
    static const QStringList planets = {
        "Sun", "Moon", "Mars", "Mercury", "Jupiter", "Venus", "Saturn",
    };
    return planets;
}

QString vedicRulerForSign(int sign) {
    static const QStringList rulers = {
        "Mars",     // Aries
        "Venus",    // Taurus
        "Mercury",  // Gemini
        "Moon",     // Cancer
        "Sun",      // Leo
        "Mercury",  // Virgo
        "Venus",    // Libra
        "Mars",     // Scorpio
        "Jupiter",  // Sagittarius
        "Saturn",   // Capricorn
        "Saturn",   // Aquarius
        "Jupiter",  // Pisces
    };
    if (sign < 0 || sign >= 12) {
        return QString();
    }
    return rulers[sign];
}

QString munthaHouseMeaning(int house) {
    switch (house) {
        case 1: return "Health";
        case 2: return "Wealth";
        case 3: return "Success and fame";
        case 4: return "Disputes, loss of position";
        case 5: return "Good results";
        case 6: return "Illness";
        case 7: return "Troubles in marriage, hardships";
        case 8: return "Troubles";
        case 9: return "Prosperity";
        case 10: return "Status";
        case 11: return "Gains";
        case 12: return "Expenditures";
        default: return QString();
    }
}

double deepExaltationLongitude(const QString& planet) {
    static const QHash<QString, double> points = {
        {"Sun", 10.0},      // 10 Ar
        {"Moon", 33.0},     // 3 Ta
        {"Mars", 298.0},    // 28 Cp
        {"Mercury", 165.0}, // 15 Vi
        {"Jupiter", 95.0},  // 5 Cn
        {"Venus", 357.0},   // 27 Pi
        {"Saturn", 200.0},  // 20 Li
    };
    return points.value(planet, -1.0);
}

int exaltationSign(const QString& planet) {
    static const QHash<QString, int> signs = {
        {"Sun", 0}, {"Moon", 1}, {"Mars", 9}, {"Mercury", 5},
        {"Jupiter", 3}, {"Venus", 11}, {"Saturn", 6},
    };
    return signs.value(planet, -1);
}

bool isOwnSign(const QString& planet, int sign) {
    static const QHash<QString, QVector<int>> own = {
        {"Sun", {4}}, {"Moon", {3}}, {"Mars", {0, 7}}, {"Mercury", {2, 5}},
        {"Jupiter", {8, 11}}, {"Venus", {1, 6}}, {"Saturn", {9, 10}},
    };
    return own.value(planet, {}).contains(sign);
}

// Naisargika maitri: relation of `planet` to `other`.
QString relationTo(const QString& planet, const QString& other) {
    static const QHash<QString, QPair<QStringList, QStringList>> table = {
        // {friends, enemies}; everyone not listed is neutral.
        {"Sun", {{"Moon", "Mars", "Jupiter"}, {"Venus", "Saturn"}}},
        {"Moon", {{"Sun", "Mercury"}, {}}},
        {"Mars", {{"Sun", "Moon", "Jupiter"}, {"Mercury"}}},
        {"Mercury", {{"Sun", "Venus"}, {"Moon"}}},
        {"Jupiter", {{"Sun", "Moon", "Mars"}, {"Mercury", "Venus"}}},
        {"Venus", {{"Mercury", "Saturn"}, {"Sun", "Moon"}}},
        {"Saturn", {{"Mercury", "Venus"}, {"Sun", "Moon", "Mars"}}},
    };
    const auto& entry = table.value(planet);
    if (entry.first.contains(other)) {
        return "friend";
    }
    if (entry.second.contains(other)) {
        return "enemy";
    }
    return "neutral";
}

// Hadda lords per sign: five degree ranges each (classical Tajaka table).
struct HaddaRange {
    double from;
    double to;
    QString lord;
};

QVector<HaddaRange> haddaRangesForSign(int sign) {
    static const QVector<QVector<HaddaRange>> table = {
        // Aries
        {{0, 6, "Jupiter"}, {6, 12, "Venus"}, {12, 20, "Mercury"}, {20, 25, "Mars"}, {25, 30, "Saturn"}},
        // Taurus
        {{0, 8, "Venus"}, {8, 14, "Mercury"}, {14, 22, "Jupiter"}, {22, 27, "Saturn"}, {27, 30, "Mars"}},
        // Gemini
        {{0, 6, "Mercury"}, {6, 12, "Venus"}, {12, 17, "Jupiter"}, {17, 24, "Mars"}, {24, 30, "Saturn"}},
        // Cancer
        {{0, 7, "Mars"}, {7, 13, "Venus"}, {13, 19, "Mercury"}, {19, 26, "Jupiter"}, {26, 30, "Saturn"}},
        // Leo
        {{0, 6, "Jupiter"}, {6, 11, "Venus"}, {11, 18, "Saturn"}, {18, 24, "Mercury"}, {24, 30, "Mars"}},
        // Virgo
        {{0, 7, "Mercury"}, {7, 17, "Venus"}, {17, 21, "Jupiter"}, {21, 28, "Mars"}, {28, 30, "Saturn"}},
        // Libra
        {{0, 6, "Saturn"}, {6, 14, "Mercury"}, {14, 21, "Jupiter"}, {21, 28, "Venus"}, {28, 30, "Mars"}},
        // Scorpio
        {{0, 7, "Mars"}, {7, 11, "Venus"}, {11, 19, "Mercury"}, {19, 24, "Jupiter"}, {24, 30, "Saturn"}},
        // Sagittarius
        {{0, 12, "Jupiter"}, {12, 17, "Venus"}, {17, 21, "Mercury"}, {21, 26, "Mars"}, {26, 30, "Saturn"}},
        // Capricorn
        {{0, 7, "Mercury"}, {7, 14, "Jupiter"}, {14, 22, "Venus"}, {22, 26, "Saturn"}, {26, 30, "Mars"}},
        // Aquarius
        {{0, 7, "Mercury"}, {7, 13, "Venus"}, {13, 20, "Jupiter"}, {20, 25, "Mars"}, {25, 30, "Saturn"}},
        // Pisces
        {{0, 12, "Venus"}, {12, 16, "Jupiter"}, {16, 19, "Mercury"}, {19, 28, "Mars"}, {28, 30, "Saturn"}},
    };
    if (sign < 0 || sign >= 12) {
        return {};
    }
    return table[sign];
}

// Triraasi lords of the annual lagna, by day/night (classical table).
QString triraasiLord(int lagnaSign, bool dayChart) {
    static const QStringList dayLords = {
        "Sun", "Venus", "Saturn", "Venus", "Jupiter", "Moon",
        "Mercury", "Mars", "Saturn", "Mars", "Jupiter", "Moon",
    };
    static const QStringList nightLords = {
        "Jupiter", "Moon", "Mercury", "Mars", "Sun", "Venus",
        "Saturn", "Venus", "Saturn", "Mars", "Jupiter", "Moon",
    };
    if (lagnaSign < 0 || lagnaSign >= 12) {
        return QString();
    }
    return dayChart ? dayLords[lagnaSign] : nightLords[lagnaSign];
}

int harshaFavoredHouse(const QString& planet) {
    static const QHash<QString, int> houses = {
        {"Sun", 9}, {"Moon", 3}, {"Mars", 6}, {"Mercury", 1},
        {"Jupiter", 11}, {"Venus", 5}, {"Saturn", 12},
    };
    return houses.value(planet, 0);
}

bool isFemininePlanet(const QString& planet) {
    return planet == "Moon" || planet == "Mercury" || planet == "Venus" || planet == "Saturn";
}

int wholeSignHouse(double longitude, double ascendant) {
    return ((signIndex(longitude) - signIndex(ascendant) + 12) % 12) + 1;
}

double normalize360(double value) {
    double result = std::fmod(value, 360.0);
    if (result < 0.0) {
        result += 360.0;
    }
    return result;
}

double angularDiffAbs(double a, double b) {
    const double diff = std::fmod(a - b, 360.0);
    const double wrapped = std::fabs(diff < 0.0 ? diff + 360.0 : diff);
    return wrapped > 180.0 ? 360.0 - wrapped : wrapped;
}

// Tajaka speed ordering, slowest to fastest (nodes excluded from aspects):
// Saturn, Jupiter, Mars, Sun, Venus, Mercury, Moon.
int planetSpeedRank(const QString& name) {
    static const QHash<QString, int> ranks = {
        {"Saturn", 0}, {"Jupiter", 1}, {"Mars", 2},
        {"Sun", 3}, {"Venus", 4}, {"Mercury", 5}, {"Moon", 6},
    };
    return ranks.value(name, -1);
}

const BodyPosition* findClassicalBody(const NatalChart& chart, const QString& name) {
    for (const auto& body : chart.bodies) {
        if (body.name == name) {
            return &body;
        }
    }
    return nullptr;
}

}  // namespace

MunthaInfo computeMuntha(const NatalChart& natalChart,
                         const NatalChart& annualChart,
                         int completedYears) {
    MunthaInfo info;
    const int age = std::max(0, completedYears);
    info.longitude = normalize360(natalChart.angles.asc + 30.0 * age);
    info.signIndex = signIndex(info.longitude);
    info.signName = signName(info.signIndex);
    info.lord = vedicRulerForSign(info.signIndex);
    const int annualAscSign = signIndex(annualChart.angles.asc);
    info.houseFromAnnualLagna = ((info.signIndex - annualAscSign + 12) % 12) + 1;
    info.houseMeaning = munthaHouseMeaning(info.houseFromAnnualLagna);
    info.valid = info.signIndex >= 0 && !info.lord.isEmpty();
    return info;
}

double tajakaDeeptamsa(const QString& planetName) {
    static const QHash<QString, double> deeptamsas = {
        {"Sun", 15.0}, {"Moon", 12.0}, {"Mars", 8.0}, {"Mercury", 7.0},
        {"Jupiter", 9.0}, {"Venus", 7.0}, {"Saturn", 9.0},
    };
    return deeptamsas.value(planetName, 0.0);
}

QString tajakaAspectKindLabel(TajakaAspectKind kind) {
    switch (kind) {
        case TajakaAspectKind::Conjunction: return "Conjunction";
        case TajakaAspectKind::SemiSextile: return "Semi-sextile";
        case TajakaAspectKind::Sextile: return "Sextile";
        case TajakaAspectKind::Square: return "Square";
        case TajakaAspectKind::Trine: return "Trine";
        case TajakaAspectKind::Opposition: return "Opposition";
    }
    return QString();
}

QString tajakaAspectNatureLabel(TajakaAspectNature nature) {
    switch (nature) {
        case TajakaAspectNature::StrongBenefic: return "Strong benefic";
        case TajakaAspectNature::WeakBenefic: return "Weak benefic";
        case TajakaAspectNature::WeakMalefic: return "Weak malefic";
        case TajakaAspectNature::StrongMalefic: return "Strong malefic";
        case TajakaAspectNature::Neutral: return "Neutral";
    }
    return QString();
}

QVector<TajakaAspect> computeTajakaAspects(const NatalChart& chart) {
    QVector<TajakaAspect> aspects;
    const QStringList& planets = classicalPlanets();
    for (int i = 0; i < planets.size(); ++i) {
        const BodyPosition* first = findClassicalBody(chart, planets[i]);
        if (!first) {
            continue;
        }
        for (int j = i + 1; j < planets.size(); ++j) {
            const BodyPosition* second = findClassicalBody(chart, planets[j]);
            if (!second) {
                continue;
            }
            const int signDistance = (signIndex(second->longitude)
                                      - signIndex(first->longitude) + 12) % 12;
            const int distance = std::min(signDistance, 12 - signDistance);
            TajakaAspectKind kind;
            TajakaAspectNature nature;
            switch (distance) {
                case 0:
                    kind = TajakaAspectKind::Conjunction;
                    nature = TajakaAspectNature::StrongMalefic;
                    break;
                case 1:
                    kind = TajakaAspectKind::SemiSextile;
                    nature = TajakaAspectNature::Neutral;
                    break;
                case 2:
                    kind = TajakaAspectKind::Sextile;
                    nature = TajakaAspectNature::WeakBenefic;
                    break;
                case 3:
                    kind = TajakaAspectKind::Square;
                    nature = TajakaAspectNature::WeakMalefic;
                    break;
                case 4:
                    kind = TajakaAspectKind::Trine;
                    nature = TajakaAspectNature::StrongBenefic;
                    break;
                case 6:
                    kind = TajakaAspectKind::Opposition;
                    nature = TajakaAspectNature::StrongMalefic;
                    break;
                default:
                    continue;  // 6th/8th sign distance (quincunx): no Tajaka aspect
            }
            const double separation = angularDiffAbs(first->longitude, second->longitude);
            const double aspectAngle = 30.0 * distance;
            const double orb = std::fabs(separation - aspectAngle);

            TajakaAspect aspect;
            aspect.firstName = first->name;
            aspect.secondName = second->name;
            aspect.kind = kind;
            aspect.nature = nature;
            aspect.orb = orb;
            aspect.firstDeeptamsa = tajakaDeeptamsa(first->name);
            aspect.secondDeeptamsa = tajakaDeeptamsa(second->name);
            aspect.withinFirstDeeptamsa = orb <= aspect.firstDeeptamsa + 1e-9;
            aspect.withinSecondDeeptamsa = orb <= aspect.secondDeeptamsa + 1e-9;
            aspect.mutual = aspect.withinFirstDeeptamsa && aspect.withinSecondDeeptamsa;
            if (!aspect.mutual) {
                continue;  // only vartamaana (mutually within orb) aspects are kept
            }
            aspect.firstAdvancement = degInSign(first->longitude);
            aspect.secondAdvancement = degInSign(second->longitude);
            aspect.firstRetrograde = first->retrograde;
            aspect.secondRetrograde = second->retrograde;
            const int firstRank = planetSpeedRank(first->name);
            const int secondRank = planetSpeedRank(second->name);
            aspect.firstFaster = firstRank > secondRank;
            const BodyPosition& faster = aspect.firstFaster ? *first : *second;
            const BodyPosition& slower = aspect.firstFaster ? *second : *first;
            const double fasterAdvancement = aspect.firstFaster
                ? aspect.firstAdvancement : aspect.secondAdvancement;
            const double slowerAdvancement = aspect.firstFaster
                ? aspect.secondAdvancement : aspect.firstAdvancement;

            // Ithasala (applying) / Eesarpha (separating) with the classical
            // retrogression refinements. The "about to station retrograde"
            // refinement requires ephemeris sampling and is not applied here.
            if (faster.retrograde) {
                if (fasterAdvancement < slowerAdvancement) {
                    aspect.eesarpha = true;  // faster retrograde moving away
                } else {
                    aspect.ithasala = true;  // both move toward the same advancement
                }
            } else {
                if (fasterAdvancement < slowerAdvancement) {
                    aspect.ithasala = true;
                } else if (fasterAdvancement > slowerAdvancement) {
                    aspect.eesarpha = true;
                }
            }
            if (aspect.ithasala
                && std::fabs(fasterAdvancement - slowerAdvancement) < 1.0) {
                aspect.poorna = true;
            }

            QStringList motionParts;
            if (aspect.ithasala) {
                motionParts.push_back(aspect.poorna ? "Poorna ithasala" : "Ithasala");
            } else if (aspect.eesarpha) {
                motionParts.push_back("Eesarpha");
            }
            if (faster.retrograde) {
                motionParts.push_back(QString("%1 retrograde").arg(faster.name));
            } else if (slower.retrograde) {
                motionParts.push_back(QString("%1 retrograde").arg(slower.name));
            }
            aspect.summary = QString("%1 %2 %3 - orb %4 deg (%5)")
                .arg(first->name,
                     tajakaAspectKindLabel(kind).toLower(),
                     second->name)
                .arg(orb, 0, 'f', 2)
                .arg(tajakaAspectNatureLabel(nature).toLower());
            if (!motionParts.isEmpty()) {
                aspect.summary += QString("; %1").arg(motionParts.join(", "));
            }
            aspects.push_back(aspect);
        }
    }
    std::stable_sort(aspects.begin(), aspects.end(),
                     [](const TajakaAspect& a, const TajakaAspect& b) {
                         return a.orb < b.orb;
                     });
    return aspects;
}

TajakaRelation naturalRelation(const QString& planet, int signIndex) {
    const QString lord = vedicRulerForSign(signIndex);
    if (lord.isEmpty() || lord == planet) {
        return TajakaRelation::Own;
    }
    const QString relation = relationTo(planet, lord);
    if (relation == "enemy") {
        return TajakaRelation::Enemy;
    }
    // The strength tables only value own / friend / enemy placements; a
    // neutral lord is grouped with friend.
    return TajakaRelation::Friend;
}

int divisionSign(int division, double longitude) {
    const double normalized = normalize360(longitude);
    const int sign = static_cast<int>(normalized / 30.0) % 12;
    const double deg = normalized - sign * 30.0;
    const bool oddSign = (sign % 2) == 0;  // Ar, Ge, Le, Li, Sg, Aq
    const auto part = [&deg](double width) {
        return static_cast<int>(std::min(deg / width, 30.0 / width - 1.0));
    };
    switch (division) {
        case 2: {
            // Hora: odd signs Sun/Saturn halves map to Leo/Cancer; even reversed.
            const bool firstHalf = deg < 15.0;
            const bool sunHora = oddSign ? firstHalf : !firstHalf;
            return sunHora ? 4 : 3;
        }
        case 3:
            return (sign + 4 * part(10.0)) % 12;
        case 4:
            return (sign + part(7.5)) % 12;
        case 5: {
            // BPHS panchamsa: odd signs Ar/Aq/Sg/Ge/Li; even signs Ta/Vi/Pi/Cp/Sc.
            static const int oddMap[5] = {0, 10, 8, 3, 6};
            static const int evenMap[5] = {1, 5, 11, 9, 7};
            return oddSign ? oddMap[part(6.0)] : evenMap[part(6.0)];
        }
        case 6:
            return (sign + part(5.0)) % 12;
        case 7:
            return oddSign ? (sign + part(30.0 / 7.0)) % 12
                           : (sign + 6 + part(30.0 / 7.0)) % 12;
        case 8:
            return (sign + part(3.75)) % 12;
        case 9:
            return static_cast<int>(normalized / (10.0 / 3.0)) % 12;
        case 10:
            return oddSign ? (sign + part(3.0)) % 12
                           : (sign + 8 + part(3.0)) % 12;
        case 11:
            // Ekadasamsa convention: eleven parts from the sign itself.
            return (sign + part(30.0 / 11.0)) % 12;
        case 12:
            return (sign + part(2.5)) % 12;
        default:
            return sign;
    }
}

QString haddaLord(double longitude) {
    const double normalized = normalize360(longitude);
    const int sign = static_cast<int>(normalized / 30.0) % 12;
    const double deg = normalized - sign * 30.0;
    for (const auto& range : haddaRangesForSign(sign)) {
        if (deg >= range.from && deg < range.to) {
            return range.lord;
        }
    }
    return QString();
}

const TajakaPlanetStrength* TajakaStrengths::forPlanet(const QString& name) const {
    for (const auto& entry : planets) {
        if (entry.planet == name) {
            return &entry;
        }
    }
    return nullptr;
}

TajakaStrengths computeTajakaStrengths(const NatalChart& annualChart) {
    TajakaStrengths strengths;
    const double ascendant = annualChart.angles.asc;
    const bool dayChart = annualChart.isDayChart;
    for (const QString& name : classicalPlanets()) {
        const BodyPosition* body = findClassicalBody(annualChart, name);
        if (!body) {
            continue;
        }
        TajakaPlanetStrength entry;
        entry.planet = name;
        const int house = wholeSignHouse(body->longitude, ascendant);
        const int sign = signIndex(body->longitude);

        // --- Harsha bala ---
        entry.harshaFavoredHouse = house == harshaFavoredHouse(name);
        entry.harshaExaltOrOwn = sign == exaltationSign(name) || isOwnSign(name, sign);
        const bool feminine = isFemininePlanet(name);
        if (feminine) {
            static const QSet<int> houses = {1, 2, 3, 7, 8, 9};
            entry.harshaGenderHouse = houses.contains(house);
        } else {
            static const QSet<int> houses = {4, 5, 6, 10, 11, 12};
            entry.harshaGenderHouse = houses.contains(house);
        }
        entry.harshaDayNight = dayChart ? !feminine : feminine;
        entry.harshaTotal = (entry.harshaFavoredHouse ? 5 : 0)
            + (entry.harshaExaltOrOwn ? 5 : 0)
            + (entry.harshaGenderHouse ? 5 : 0)
            + (entry.harshaDayNight ? 5 : 0);

        // --- Pancha vargeeya bala ---
        const TajakaRelation kshetraRelation = naturalRelation(name, sign);
        entry.kshetraBala = kshetraRelation == TajakaRelation::Own ? 30.0
            : kshetraRelation == TajakaRelation::Enemy ? 7.5 : 15.0;
        const double exaltation = deepExaltationLongitude(name);
        if (exaltation >= 0.0) {
            // Uchcha bala grows with distance from the deep DEBILITATION point
            // (exaltation + 180) toward the deep exaltation point.
            const double debilitation = normalize360(exaltation + 180.0);
            entry.uchchaBala = 20.0 * angularDiffAbs(body->longitude, debilitation) / 180.0;
        }
        const QString hadda = haddaLord(body->longitude);
        if (!hadda.isEmpty()) {
            if (hadda == name) {
                entry.haddaBala = 15.0;
            } else {
                entry.haddaBala = relationTo(name, hadda) == "enemy" ? 3.75 : 7.5;
            }
        }
        auto vargaBala = [&name](int divisionSignIndex, double own, double friendValue, double enemyValue) {
            if (divisionSignIndex < 0) {
                return 0.0;
            }
            if (isOwnSign(name, divisionSignIndex) || exaltationSign(name) == divisionSignIndex) {
                return own;
            }
            const TajakaRelation relation = naturalRelation(name, divisionSignIndex);
            if (relation == TajakaRelation::Enemy) {
                return enemyValue;
            }
            if (relation == TajakaRelation::Own) {
                return own;
            }
            return friendValue;
        };
        entry.drekkanaBala = vargaBala(divisionSign(3, body->longitude), 10.0, 5.0, 2.5);
        entry.navamsaBala = vargaBala(divisionSign(9, body->longitude), 5.0, 2.5, 1.25);
        entry.panchaVargeeya = (entry.kshetraBala + entry.uchchaBala + entry.haddaBala
                                + entry.drekkanaBala + entry.navamsaBala) / 4.0;
        if (entry.panchaVargeeya < 5.0) {
            entry.panchaRating = "weak";
        } else if (entry.panchaVargeeya < 10.0) {
            entry.panchaRating = "ordinary";
        } else if (entry.panchaVargeeya < 15.0) {
            entry.panchaRating = "strong";
        } else if (entry.panchaVargeeya <= 20.0) {
            entry.panchaRating = "very strong";
        } else {
            entry.panchaRating = "extraordinary";
        }

        // --- Dwadasha vargeeya bala ---
        const int debilitationSign = exaltationSign(name) >= 0 ? (exaltationSign(name) + 6) % 12 : -1;
        for (int division = 1; division <= 12; ++division) {
            const int divisionSignIndex = division == 1 ? sign : divisionSign(division, body->longitude);
            if (divisionSignIndex < 0) {
                continue;
            }
            if (isOwnSign(name, divisionSignIndex)
                || exaltationSign(name) == divisionSignIndex
                || naturalRelation(name, divisionSignIndex) == TajakaRelation::Friend) {
                ++entry.dwadashaStrong;
            } else if (debilitationSign == divisionSignIndex
                       || naturalRelation(name, divisionSignIndex) == TajakaRelation::Enemy) {
                ++entry.dwadashaWeak;
            }
        }
        entry.dwadashaVargeeya = entry.dwadashaStrong - entry.dwadashaWeak;
        strengths.planets.push_back(entry);
    }
    strengths.valid = !strengths.planets.isEmpty();
    return strengths;
}

TajakaLordOfYear computeTajakaLordOfYear(const NatalChart& natalChart,
                                         const NatalChart& annualChart,
                                         const MunthaInfo& muntha,
                                         const TajakaStrengths& strengths) {
    TajakaLordOfYear result;
    const bool dayChart = annualChart.isDayChart;
    const int annualAscSign = signIndex(annualChart.angles.asc);
    if (annualAscSign < 0) {
        return result;
    }

    QVector<QPair<QString, QString>> candidates;  // {planet, reason}
    auto addCandidate = [&candidates](const QString& planet, const QString& reason) {
        if (planet.isEmpty()) {
            return;
        }
        candidates.push_back({planet, reason});
    };
    const QString sectPlanet = dayChart ? "Sun" : "Moon";
    const auto* sectBody = findClassicalBody(annualChart, sectPlanet);
    if (sectBody) {
        addCandidate(vedicRulerForSign(signIndex(sectBody->longitude)),
                     QString("lord of the %1's sign in the annual chart").arg(sectPlanet));
    }
    addCandidate(vedicRulerForSign(signIndex(natalChart.angles.asc)),
                 "natal lagna lord");
    if (muntha.valid) {
        addCandidate(muntha.lord, "muntha lord");
    }
    addCandidate(vedicRulerForSign(annualAscSign), "annual lagna lord");
    addCandidate(triraasiLord(annualAscSign, dayChart),
                 QString("triraasi lord of the annual lagna (%1)").arg(dayChart ? "day" : "night"));
    if (candidates.isEmpty()) {
        return result;
    }

    QStringList candidacy;
    QHash<QString, int> candidacyCounts;
    for (const auto& candidate : candidates) {
        candidacy.push_back(QString("%1 (%2)").arg(candidate.first, candidate.second));
        ++candidacyCounts[candidate.first];
    }
    result.candidacy = candidacy;

    const double ascendant = annualChart.angles.asc;
    auto aspectOnLagna = [&](const QString& planet) {
        // Returns 1 for benefic (trine/sextile), -1 for malefic
        // (conjunction/square/opposition), 0 for none; deeptamsa orb.
        const auto* body = findClassicalBody(annualChart, planet);
        if (!body) {
            return 0;
        }
        const double orb = tajakaDeeptamsa(planet);
        const double separation = angularDiffAbs(body->longitude, ascendant);
        const auto within = [separation, orb](double angle) {
            return std::fabs(separation - angle) <= orb + 1e-9;
        };
        if (within(60.0) || within(120.0)) {
            return 1;
        }
        if (within(0.0) || within(90.0) || within(180.0)) {
            return -1;
        }
        return 0;
    };
    auto panchaOf = [&strengths](const QString& planet) {
        const auto* entry = strengths.forPlanet(planet);
        return entry ? entry->panchaVargeeya : -1.0;
    };
    // Deduplicated candidate order.
    QStringList unique;
    for (const auto& candidate : candidates) {
        if (!unique.contains(candidate.first)) {
            unique.push_back(candidate.first);
        }
    }
    for (const QString& planet : unique) {
        if (aspectOnLagna(planet) > 0) {
            result.beneficOnLagna.push_back(planet);
        }
    }

    auto selectFrom = [&](const QStringList& pool, const QString& reason) {
        QString best;
        double bestPancha = -1.0;
        int bestCount = -1;
        for (const QString& planet : pool) {
            const double pancha = panchaOf(planet);
            const int count = candidacyCounts.value(planet, 0);
            // "Similar" pancha vargeeya (within half a unit) is a tie;
            // more candidacy categories then win, and among candidates that
            // are still tied, the higher pancha vargeeya wins.
            const bool tie = bestPancha >= 0.0 && std::fabs(pancha - bestPancha) <= 0.5;
            if (best.isEmpty()
                || pancha > bestPancha + 0.5
                || (tie && count > bestCount)
                || (tie && count == bestCount && pancha > bestPancha)) {
                best = planet;
                bestPancha = pancha;
                bestCount = count;
            }
        }
        result.planet = best;
        result.selectionReason = reason;
    };

    if (!result.beneficOnLagna.isEmpty()) {
        QString list = result.beneficOnLagna.join(", ");
        selectFrom(result.beneficOnLagna,
                   QString("Shortlisted by benefic aspect on lagna (%1); highest pancha vargeeya bala.")
                       .arg(list));
    } else {
        QStringList malefic;
        for (const QString& planet : unique) {
            if (aspectOnLagna(planet) < 0) {
                malefic.push_back(planet);
            }
        }
        if (!malefic.isEmpty()) {
            selectFrom(malefic,
                       QString("No benefic aspect on lagna; accepted malefic aspect (%1); highest pancha vargeeya bala.")
                           .arg(malefic.join(", ")));
        } else {
            QStringList veryStrong;
            for (const QString& planet : unique) {
                if (panchaOf(planet) >= 15.0) {
                    veryStrong.push_back(planet);
                }
            }
            if (!veryStrong.isEmpty()) {
                selectFrom(veryStrong, "No aspect on lagna; very strong pancha vargeeya bala.");
            } else {
                result.planet = candidates.first().first;
                result.selectionReason = "No aspect on lagna and no very strong candidate; first candidate taken.";
            }
        }
    }
    result.valid = !result.planet.isEmpty();
    return result;
}

bool tajakaSelfCheck(QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    // Muntha: natal Ascendant in Scorpio, 32nd year (31 completed) must land
    // in Gemini per the book's worked example.
    {
        NatalChart natal;
        natal.angles.asc = 210.0;  // 0 deg Scorpio
        NatalChart annual;
        annual.angles.asc = 280.0;  // Capricorn, as in the book's running example
        const MunthaInfo muntha = computeMuntha(natal, annual, 31);
        if (!muntha.valid || muntha.signName != "Gemini" || muntha.lord != "Mercury") {
            return fail("Muntha self-check failed: expected Gemini ruled by Mercury.");
        }
        if (muntha.houseFromAnnualLagna != 6) {
            return fail("Muntha self-check failed: expected 6th house from a Capricorn annual lagna.");
        }
    }

    // Deeptamsa table.
    if (tajakaDeeptamsa("Sun") != 15.0 || tajakaDeeptamsa("Moon") != 12.0
        || tajakaDeeptamsa("Mars") != 8.0 || tajakaDeeptamsa("Mercury") != 7.0
        || tajakaDeeptamsa("Jupiter") != 9.0 || tajakaDeeptamsa("Venus") != 7.0
        || tajakaDeeptamsa("Saturn") != 9.0) {
        return fail("Deeptamsa self-check failed.");
    }

    // Ithasala example: Moon 14 Leo vs Venus 19 Libra -> mutual sextile,
    // faster Moon less advanced -> ithasala (not poorna, 5 deg apart).
    // Eesarpha example: Moon 23 Leo vs Venus 19 Libra -> eesarpha.
    {
        auto makeChart = [](double moonLon, double venusLon) {
            NatalChart chart;
            auto addBody = [&chart](const QString& name, double longitude) {
                BodyPosition body;
                body.name = name;
                body.longitude = longitude;
                body.signIndex = signIndex(longitude);
                body.signName = signName(body.signIndex);
                body.degInSign = degInSign(longitude);
                body.hasSpeed = true;
                body.speed = name == "Moon" ? 13.0 : 1.2;
                chart.bodies.push_back(body);
            };
            addBody("Moon", moonLon);
            addBody("Venus", venusLon);
            return chart;
        };
        const QVector<TajakaAspect> ithasalaAspects =
            computeTajakaAspects(makeChart(134.0, 199.0));   // 14 Leo, 19 Libra
        if (ithasalaAspects.size() != 1 || !ithasalaAspects[0].ithasala
            || ithasalaAspects[0].eesarpha || ithasalaAspects[0].poorna
            || ithasalaAspects[0].kind != TajakaAspectKind::Sextile) {
            return fail("Ithasala self-check failed for Moon 14 Leo / Venus 19 Libra.");
        }
        const QVector<TajakaAspect> eesarphaAspects =
            computeTajakaAspects(makeChart(143.0, 199.0));   // 23 Leo, 19 Libra
        if (eesarphaAspects.size() != 1 || !eesarphaAspects[0].eesarpha
            || eesarphaAspects[0].ithasala) {
            return fail("Eesarpha self-check failed for Moon 23 Leo / Venus 19 Libra.");
        }
    }

    // Venus at 13 Libra trines 6-20 degrees of Gemini (deeptamsa 7):
    // Mars at 10 Gemini is inside the window, Mars at 4 Gemini is outside.
    {
        auto makeTrineChart = [](double marsLongitude) {
            NatalChart chart;
            auto addBody = [&chart](const QString& name, double longitude, double speed) {
                BodyPosition body;
                body.name = name;
                body.longitude = longitude;
                body.signIndex = signIndex(longitude);
                body.signName = signName(body.signIndex);
                body.degInSign = degInSign(longitude);
                body.hasSpeed = true;
                body.speed = speed;
                chart.bodies.push_back(body);
            };
            addBody("Venus", 193.0, 1.2);  // 13 Libra
            addBody("Mars", marsLongitude, 0.6);
            return chart;
        };
        const QVector<TajakaAspect> inside = computeTajakaAspects(makeTrineChart(70.0));   // 10 Gemini
        if (inside.size() != 1 || inside[0].kind != TajakaAspectKind::Trine
            || inside[0].firstName != "Mars" || inside[0].secondName != "Venus"
            || !inside[0].mutual) {
            return fail("Deeptamsa coverage self-check failed for Venus 13 Libra trine Mars 10 Gemini.");
        }
        const QVector<TajakaAspect> outside = computeTajakaAspects(makeTrineChart(64.0));  // 4 Gemini
        if (!outside.isEmpty()) {
            return fail("Deeptamsa rejection self-check failed for Mars 4 Gemini outside 6-20 window.");
        }
    }

    // Uchcha bala worked example: Jupiter at 8Vi30 scores 12.94 of 20.
    {
        NatalChart chart;
        BodyPosition jupiter;
        jupiter.name = "Jupiter";
        jupiter.longitude = 158.5;  // 8 Vi 30
        jupiter.signIndex = signIndex(jupiter.longitude);
        jupiter.signName = signName(jupiter.signIndex);
        jupiter.degInSign = degInSign(jupiter.longitude);
        jupiter.hasSpeed = true;
        jupiter.speed = 0.1;
        chart.bodies.push_back(jupiter);
        chart.angles.asc = 0.0;
        const TajakaStrengths strengths = computeTajakaStrengths(chart);
        const auto* entry = strengths.forPlanet("Jupiter");
        if (!entry || std::fabs(entry->uchchaBala - 12.94) > 0.01) {
            return fail("Uchcha bala self-check failed: expected 12.94 for Jupiter at 8Vi30.");
        }
        if (haddaLord(158.5) != "Venus") {
            return fail("Hadda lord self-check failed: expected Venus for 8Vi30.");
        }
    }

    // Harsha bala worked example (annual chart of Example 118, night year):
    // Moon 15, Mercury 10, Venus 10, Jupiter 5, Saturn 5, Sun 0, Mars 0.
    {
        auto addBody = [](NatalChart& chart, const QString& name, double longitude) {
            BodyPosition body;
            body.name = name;
            body.longitude = longitude;
            body.signIndex = signIndex(longitude);
            body.signName = signName(body.signIndex);
            body.degInSign = degInSign(longitude);
            body.hasSpeed = true;
            body.speed = 1.0;
            chart.bodies.push_back(body);
        };
        NatalChart chart;
        chart.angles.asc = 280.0 + 50.0 / 60.0;  // 10 Cp 50
        chart.isDayChart = false;               // year started at ~4:42 am
        addBody(chart, "Sun", 314.0);      // ~23 Aq (2nd)
        addBody(chart, "Moon", 345.2);     // 15 Pi (3rd)
        addBody(chart, "Mars", 355.0);     // 24 Pi (3rd)
        addBody(chart, "Mercury", 311.5);  // 11 Aq (2nd)
        addBody(chart, "Jupiter", 25.0);   // Ar (4th)
        addBody(chart, "Venus", 282.0);    // Cp (1st)
        addBody(chart, "Saturn", 19.2);    // 19 Ar (4th)
        const TajakaStrengths strengths = computeTajakaStrengths(chart);
        const QHash<QString, int> expected = {
            {"Moon", 15}, {"Mercury", 10}, {"Venus", 10},
            {"Jupiter", 5}, {"Saturn", 5}, {"Sun", 0}, {"Mars", 0},
        };
        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            const auto* entry = strengths.forPlanet(it.key());
            if (!entry || entry->harshaTotal != it.value()) {
                return fail(QString("Harsha bala self-check failed for %1: expected %2.")
                                .arg(it.key()).arg(it.value()));
            }
        }
    }

    return true;
}

}  // namespace tajaka
}  // namespace dracoved
