#include "arabic_lots.h"

#include "formatting.h"

#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

double signedAngularDiff(double a, double b) {
    return std::fmod((a - b + 540.0), 360.0) - 180.0;
}

double angularDiffAbs(double a, double b) {
    return std::fabs(signedAngularDiff(a, b));
}

double project(double projector, double to, double from) {
    return normalizeDegrees(projector + to - from);
}

bool bodyLon(const LotCalculationContext& ctx, const QString& name, double* outLon) {
    auto it = ctx.bodies.constFind(name);
    if (it == ctx.bodies.constEnd()) {
        return false;
    }
    if (outLon) {
        *outLon = it.value();
    }
    return true;
}

QString traditionalRulerForSign(int signIdx) {
    static const QStringList rulers = {
        "Mars",
        "Venus",
        "Mercury",
        "Moon",
        "Sun",
        "Mercury",
        "Venus",
        "Mars",
        "Jupiter",
        "Saturn",
        "Saturn",
        "Jupiter",
    };
    if (signIdx < 0 || signIdx >= rulers.size()) {
        return QString();
    }
    return rulers[signIdx];
}

bool lordLonForSign(const LotCalculationContext& ctx, int signIdx, double* outLon) {
    const QString lord = traditionalRulerForSign(signIdx);
    if (lord.isEmpty()) {
        return false;
    }
    return bodyLon(ctx, lord, outLon);
}

double wholeSignHouseCusp(double asc, int house) {
    const int ascSign = signIndex(asc);
    return normalizeDegrees(((ascSign + house - 1) % 12) * 30.0);
}

double houseCuspLongitude(const LotCalculationContext& ctx, int house) {
    if (house < 1 || house > 12) {
        return ctx.asc;
    }
    if (ctx.houseSystem == HouseSystem::Placidus && ctx.cusps.size() >= 12) {
        return normalizeDegrees(ctx.cusps[house - 1]);
    }
    return wholeSignHouseCusp(ctx.asc, house);
}

int houseOfLongitude(double lon, const QVector<double>& cusps) {
    if (cusps.size() < 12) {
        return 0;
    }
    const double c1 = cusps[0];
    QVector<QPair<double, int>> rel;
    rel.reserve(cusps.size());
    for (int i = 0; i < cusps.size(); ++i) {
        rel.push_back({normalizeDegrees(cusps[i] - c1), i});
    }
    std::sort(rel.begin(), rel.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    const double x = normalizeDegrees(lon - c1);
    int lastIdx = rel[0].second;
    for (const auto& pair : rel) {
        if (x >= pair.first) {
            lastIdx = pair.second;
        } else {
            break;
        }
    }
    return lastIdx + 1;
}

int lotHouse(double lon, const LotCalculationContext& ctx) {
    const int sidx = signIndex(lon);
    if (ctx.houseSystem == HouseSystem::Placidus) {
        return houseOfLongitude(lon, ctx.cusps);
    }
    const int ascSign = signIndex(ctx.asc);
    return ((sidx - ascSign + 12) % 12) + 1;
}

BodyPosition makeLot(const QString& name, double lon, const LotCalculationContext& ctx) {
    const int sidx = signIndex(lon);
    const QString sname = signName(sidx);

    BodyPosition lot;
    lot.name = name;
    lot.longitude = normalizeDegrees(lon);
    lot.signIndex = sidx;
    lot.signName = sname;
    lot.degInSign = degInSign(lon);
    lot.house = lotHouse(lon, ctx);
    lot.retrograde = false;
    lot.element = elementForSign(sname);
    lot.mode = modeForSign(sname);
    lot.dignity = "-";
    return lot;
}

bool calcSolarLunar(const SwissEph* swe, double jdUt, int calcFlags,
                    double* outSunLon, double* outMoonLon, QString* error) {
    double sun = 0.0;
    double moon = 0.0;
    QString calcErr;
    if (!swe || !swe->calcUt(jdUt, SE_SUN, calcFlags, &sun, &calcErr)) {
        if (error) {
            *error = calcErr.isEmpty() ? "failed to compute Sun" : calcErr;
        }
        return false;
    }
    if (!swe->calcUt(jdUt, SE_MOON, calcFlags, &moon, &calcErr)) {
        if (error) {
            *error = calcErr.isEmpty() ? "failed to compute Moon" : calcErr;
        }
        return false;
    }
    if (outSunLon) {
        *outSunLon = normalizeDegrees(sun);
    }
    if (outMoonLon) {
        *outMoonLon = normalizeDegrees(moon);
    }
    return true;
}

bool phaseValue(const SwissEph* swe, double jdUt, int calcFlags, double target,
                double* outValue, QString* error) {
    double sun = 0.0;
    double moon = 0.0;
    if (!calcSolarLunar(swe, jdUt, calcFlags, &sun, &moon, error)) {
        return false;
    }
    const double phase = normalizeDegrees(moon - sun);
    if (outValue) {
        *outValue = signedAngularDiff(phase, target);
    }
    return true;
}

bool findPreviousPhase(const SwissEph* swe, double jdUt, int calcFlags, double target,
                       double* outJd, QString* error) {
    constexpr double kStep = 0.25;
    constexpr double kLookbackDays = 33.0;
    constexpr double kExactTolerance = 0.0001;

    double newerJd = jdUt;
    double newerVal = 0.0;
    if (!phaseValue(swe, newerJd, calcFlags, target, &newerVal, error)) {
        return false;
    }
    if (std::fabs(newerVal) < kExactTolerance) {
        if (outJd) {
            *outJd = newerJd;
        }
        return true;
    }

    for (double olderJd = jdUt - kStep; olderJd >= jdUt - kLookbackDays; olderJd -= kStep) {
        double olderVal = 0.0;
        if (!phaseValue(swe, olderJd, calcFlags, target, &olderVal, error)) {
            return false;
        }

        const bool discontinuity = std::fabs(olderVal - newerVal) > 180.0;
        const bool bracketed = !discontinuity && ((olderVal <= 0.0 && newerVal >= 0.0)
            || (olderVal >= 0.0 && newerVal <= 0.0));
        if (bracketed) {
            double lo = olderJd;
            double hi = newerJd;
            double loVal = olderVal;
            for (int i = 0; i < 48; ++i) {
                const double mid = (lo + hi) * 0.5;
                double midVal = 0.0;
                if (!phaseValue(swe, mid, calcFlags, target, &midVal, error)) {
                    return false;
                }
                if ((loVal <= 0.0 && midVal >= 0.0) || (loVal >= 0.0 && midVal <= 0.0)) {
                    hi = mid;
                } else {
                    lo = mid;
                    loVal = midVal;
                }
            }
            if (outJd) {
                *outJd = (lo + hi) * 0.5;
            }
            return true;
        }

        newerJd = olderJd;
        newerVal = olderVal;
    }

    if (error) {
        *error = "no prenatal syzygy found in the 33-day search window";
    }
    return false;
}

void appendLot(QVector<BodyPosition>& lots, const LotCalculationContext& ctx, const QString& name, double lon) {
    lots.push_back(makeLot(name, lon, ctx));
}

}  // namespace

double calculatePartOfFortuneLongitude(double ascendant, double sun, double moon, bool isDay) {
    // Day: Ascendant + Moon - Sun. Night: the Sun and Moon swap.
    return isDay ? project(ascendant, moon, sun) : project(ascendant, sun, moon);
}

BodyPosition makePartOfFortune(const LotCalculationContext& ctx) {
    double sun = 0.0;
    double moon = 0.0;
    if (!bodyLon(ctx, "Sun", &sun) || !bodyLon(ctx, "Moon", &moon)) {
        return BodyPosition{};
    }
    return makeLot("Part of Fortune",
                   calculatePartOfFortuneLongitude(ctx.asc, sun, moon, ctx.isDay), ctx);
}

namespace {

}  // namespace

bool findPrenatalSyzygy(const SwissEph* swe, double jdUt, int calcFlags, PrenatalSyzygy* out, QString* error) {
    if (!swe || !out) {
        if (error) {
            *error = "Swiss Ephemeris is unavailable";
        }
        return false;
    }

    double newMoonJd = 0.0;
    double fullMoonJd = 0.0;
    if (!findPreviousPhase(swe, jdUt, calcFlags, 0.0, &newMoonJd, error)) {
        return false;
    }
    if (!findPreviousPhase(swe, jdUt, calcFlags, 180.0, &fullMoonJd, error)) {
        return false;
    }

    const bool conjunctional = newMoonJd >= fullMoonJd;
    const double syzygyJd = conjunctional ? newMoonJd : fullMoonJd;
    double sun = 0.0;
    double moon = 0.0;
    if (!calcSolarLunar(swe, syzygyJd, calcFlags, &sun, &moon, error)) {
        return false;
    }

    out->valid = true;
    out->conjunctional = conjunctional;
    out->longitude = moon;
    return true;
}

double calculateLotOfErosLongitude(
    double ascendant,
    double spirit,
    double venus,
    bool isDay) {
    // Paulus/Olympiodorus Hermetic lot used by Astrodienst's
    // Hellenistic Zodiacal Releasing calculator:
    // day = Ascendant + Venus - Spirit; night = Ascendant + Spirit - Venus.
    return isDay
        ? project(ascendant, venus, spirit)
        : project(ascendant, spirit, venus);
}

QVector<BodyPosition> calculateArabicLots(const LotCalculationContext& ctx) {
    QVector<BodyPosition> lots;
    lots.reserve(96);

    double sun = 0.0;
    double moon = 0.0;
    double mercury = 0.0;
    double venus = 0.0;
    double mars = 0.0;
    double jupiter = 0.0;
    double saturn = 0.0;
    if (!bodyLon(ctx, "Sun", &sun)
        || !bodyLon(ctx, "Moon", &moon)
        || !bodyLon(ctx, "Mercury", &mercury)
        || !bodyLon(ctx, "Venus", &venus)
        || !bodyLon(ctx, "Mars", &mars)
        || !bodyLon(ctx, "Jupiter", &jupiter)
        || !bodyLon(ctx, "Saturn", &saturn)) {
        return lots;
    }

    double northNode = 0.0;
    const bool hasNorthNode = bodyLon(ctx, "North Node", &northNode);

    const double fortune = calculatePartOfFortuneLongitude(ctx.asc, sun, moon, ctx.isDay);
    const double spirit = ctx.isDay ? project(ctx.asc, sun, moon) : project(ctx.asc, moon, sun);
    const double eros = calculateLotOfErosLongitude(ctx.asc, spirit, venus, ctx.isDay);
    const double basis = ctx.isDay ? project(ctx.asc, spirit, fortune) : project(ctx.asc, fortune, spirit);
    const double necessity = ctx.isDay ? project(ctx.asc, fortune, spirit) : project(ctx.asc, spirit, fortune);
    const double courage = ctx.isDay ? project(ctx.asc, fortune, mars) : project(ctx.asc, mars, fortune);
    const double victory = ctx.isDay ? project(ctx.asc, jupiter, spirit) : project(ctx.asc, spirit, jupiter);
    const double nemesis = ctx.isDay ? project(ctx.asc, fortune, saturn) : project(ctx.asc, saturn, fortune);

    appendLot(lots, ctx, "Part of Fortune", fortune);
    appendLot(lots, ctx, "Lot of Spirit", spirit);
    appendLot(lots, ctx, "Lot of Eros", eros);
    appendLot(lots, ctx, "Lot of Basis", basis);
    appendLot(lots, ctx, "Lot of Necessity", necessity);
    appendLot(lots, ctx, "Lot of Courage", courage);
    appendLot(lots, ctx, "Lot of Victory", victory);
    appendLot(lots, ctx, "Lot of Nemesis", nemesis);

    appendLot(lots, ctx, "Lot of Life", ctx.isDay ? project(ctx.asc, saturn, jupiter) : project(ctx.asc, jupiter, saturn));
    if (ctx.hasPrenatalSyzygy) {
        appendLot(lots, ctx, "Lot of Releaser",
                  ctx.prenatalConjunctional
                      ? project(ctx.asc, moon, ctx.prenatalSyzygyLongitude)
                      : project(ctx.asc, moon, ctx.prenatalSyzygyLongitude));
    }
    appendLot(lots, ctx, "Lot of Origins", ctx.isDay ? project(mercury, mars, saturn) : project(mercury, saturn, mars));

    const double secondCusp = houseCuspLongitude(ctx, 2);
    const int secondSign = signIndex(secondCusp);
    double lord2 = 0.0;
    if (lordLonForSign(ctx, secondSign, &lord2)) {
        appendLot(lots, ctx, "Lot of Assets", project(ctx.asc, secondCusp, lord2));
    }
    appendLot(lots, ctx, "Lot of Brothers", project(ctx.asc, jupiter, saturn));
    appendLot(lots, ctx, "Lot of Brothers (Sahl Variant)",
              ctx.isDay ? project(ctx.asc, jupiter, saturn) : project(ctx.asc, saturn, jupiter));
    appendLot(lots, ctx, "Lot of Death of Brothers",
              ctx.isDay ? project(ctx.asc, ctx.mc, sun) : project(ctx.asc, sun, ctx.mc));

    const bool saturnUnderRays = angularDiffAbs(saturn, sun) <= 15.0;
    appendLot(lots, ctx, "Lot of Father",
              saturnUnderRays
                  ? (ctx.isDay ? project(ctx.asc, jupiter, sun) : project(ctx.asc, sun, jupiter))
                  : (ctx.isDay ? project(ctx.asc, saturn, sun) : project(ctx.asc, sun, saturn)));
    appendLot(lots, ctx, "Lot of Death of Father",
              ctx.isDay ? project(ctx.asc, jupiter, saturn) : project(ctx.asc, saturn, jupiter));

    const int sunSign = signIndex(sun);
    double grandfather = 0.0;
    if (sunSign == 4) {
        const double leoStart = 120.0;
        grandfather = ctx.isDay ? project(ctx.asc, saturn, leoStart) : project(ctx.asc, leoStart, saturn);
        appendLot(lots, ctx, "Lot of Grandfathers", grandfather);
    } else if (sunSign == 9 || sunSign == 10) {
        grandfather = ctx.isDay ? project(ctx.asc, saturn, sun) : project(ctx.asc, sun, saturn);
        appendLot(lots, ctx, "Lot of Grandfathers", grandfather);
    } else {
        double sunLord = 0.0;
        if (lordLonForSign(ctx, sunSign, &sunLord)) {
            grandfather = ctx.isDay ? project(ctx.asc, saturn, sunLord) : project(ctx.asc, sunLord, saturn);
            appendLot(lots, ctx, "Lot of Grandfathers", grandfather);
        }
    }

    appendLot(lots, ctx, "Lot of Real Estate", project(ctx.asc, moon, saturn));
    appendLot(lots, ctx, "Lot of Cultivation", project(ctx.asc, saturn, venus));
    if (ctx.hasPrenatalSyzygy) {
        double syzygyLord = 0.0;
        if (lordLonForSign(ctx, signIndex(ctx.prenatalSyzygyLongitude), &syzygyLord)) {
            const double endMatters = project(ctx.asc, syzygyLord, saturn);
            appendLot(lots, ctx, "Lot of End of Matters", endMatters);
            appendLot(lots, ctx, "Lot of Suspected Year", endMatters);
        }
    }

    appendLot(lots, ctx, "Lot of Children", ctx.isDay ? project(ctx.asc, saturn, jupiter) : project(ctx.asc, jupiter, saturn));
    appendLot(lots, ctx, "Lot of Child Timing", project(ctx.asc, jupiter, mars));
    appendLot(lots, ctx, "Lot of Male Children", project(ctx.asc, jupiter, moon));
    appendLot(lots, ctx, "Lot of Female Children", project(ctx.asc, venus, moon));
    double moonLord = 0.0;
    if (lordLonForSign(ctx, signIndex(moon), &moonLord)) {
        appendLot(lots, ctx, "Lot of Child's Sex",
                  ctx.isDay ? project(ctx.asc, moon, moonLord) : project(ctx.asc, moonLord, moon));
    }

    appendLot(lots, ctx, "Lot of Delight", project(ctx.asc, saturn, venus));
    appendLot(lots, ctx, "Lot of Chronic Illness", ctx.isDay ? project(ctx.asc, mars, saturn) : project(ctx.asc, saturn, mars));
    appendLot(lots, ctx, "Lot of Slaves", project(ctx.asc, moon, mercury));
    appendLot(lots, ctx, "Lot of Slaves Variant",
              ctx.isDay ? project(ctx.asc, fortune, mercury) : project(ctx.asc, mercury, fortune));

    const double menMarriageHermes = project(ctx.asc, venus, saturn);
    const double womenMarriageHermes = project(ctx.asc, saturn, venus);
    if (ctx.gender == Gender::Male) {
        appendLot(lots, ctx, "Lot of Marriage", menMarriageHermes);
    } else if (ctx.gender == Gender::Female) {
        appendLot(lots, ctx, "Lot of Marriage", womenMarriageHermes);
    }
    appendLot(lots, ctx, "Lot of Marriage (Men Hermes)", menMarriageHermes);
    appendLot(lots, ctx, "Lot of Marriage (Men Valens)", project(ctx.asc, venus, sun));
    appendLot(lots, ctx, "Lot of Marriage (Women Hermes)", womenMarriageHermes);
    appendLot(lots, ctx, "Lot of Marriage (Women Valens)", project(ctx.asc, mars, moon));
    appendLot(lots, ctx, "Lot of Marriage (Time)", project(ctx.asc, moon, sun));
    appendLot(lots, ctx, "Lot of Delight and Pleasure", project(ctx.asc, houseCuspLongitude(ctx, 7), venus));

    appendLot(lots, ctx, "Lot of Death", project(saturn, houseCuspLongitude(ctx, 8), moon));
    double ascLord = 0.0;
    if (lordLonForSign(ctx, signIndex(ctx.asc), &ascLord)) {
        appendLot(lots, ctx, "Lot of Killing Planet",
                  ctx.isDay ? project(ctx.asc, moon, ascLord) : project(ctx.asc, ascLord, moon));
    }
    appendLot(lots, ctx, "Lot of Oppressive Place", ctx.isDay ? project(mercury, mars, saturn) : project(mercury, saturn, mars));

    const double ninthCusp = houseCuspLongitude(ctx, 9);
    double lord9 = 0.0;
    if (lordLonForSign(ctx, signIndex(ninthCusp), &lord9)) {
        appendLot(lots, ctx, "Lot of Travel", project(ctx.asc, ninthCusp, lord9));
    }
    constexpr double kFifteenCancer = 105.0;
    appendLot(lots, ctx, "Lot of Navigation",
              ctx.isDay ? project(ctx.asc, kFifteenCancer, saturn) : project(ctx.asc, saturn, kFifteenCancer));
    appendLot(lots, ctx, "Lot of Intellect", ctx.isDay ? project(ctx.asc, moon, saturn) : project(ctx.asc, saturn, moon));
    appendLot(lots, ctx, "Lot of Wisdom", ctx.isDay ? project(mercury, jupiter, saturn) : project(mercury, saturn, jupiter));
    appendLot(lots, ctx, "Lot of Rumors", project(ctx.asc, moon, mercury));
    appendLot(lots, ctx, "Lot of Rumors (Night Reversed)",
              ctx.isDay ? project(ctx.asc, moon, mercury) : project(ctx.asc, mercury, moon));
    appendLot(lots, ctx, "Lot of Religion", ctx.isDay ? project(ctx.asc, mercury, moon) : project(ctx.asc, moon, mercury));

    appendLot(lots, ctx, "Lot of Nobility",
              ctx.isDay ? project(ctx.asc, 19.0, sun) : project(ctx.asc, 33.0, moon));
    appendLot(lots, ctx, "Lot of Kingdom and Authority",
              ctx.isDay ? project(ctx.asc, moon, mars) : project(ctx.asc, mars, moon));
    appendLot(lots, ctx, "Lot of Power", ctx.isDay ? project(ctx.asc, saturn, sun) : project(ctx.asc, sun, saturn));
    appendLot(lots, ctx, "Lot of Authority and Work", project(ctx.asc, moon, saturn));
    appendLot(lots, ctx, "Lot of Action", ctx.isDay ? project(ctx.asc, mars, mercury) : project(ctx.asc, mercury, mars));
    appendLot(lots, ctx, "Lot of Mother", ctx.isDay ? project(ctx.asc, moon, venus) : project(ctx.asc, venus, moon));
    appendLot(lots, ctx, "Lot of Job and Authority", project(ctx.asc, ctx.mc, sun));
    appendLot(lots, ctx, "Lot of Cause of Kingdom", project(jupiter, ctx.mc, sun));

    appendLot(lots, ctx, "Lot of Hope", ctx.isDay ? project(ctx.asc, venus, saturn) : project(ctx.asc, saturn, venus));
    appendLot(lots, ctx, "Lot of Friends", project(ctx.asc, mercury, moon));
    appendLot(lots, ctx, "Lot of Friends (Night Reversed)",
              ctx.isDay ? project(ctx.asc, mercury, moon) : project(ctx.asc, moon, mercury));
    appendLot(lots, ctx, "Lot of Friends and Enemies",
              ctx.isDay ? project(ctx.asc, spirit, fortune) : project(ctx.asc, fortune, spirit));
    appendLot(lots, ctx, "Lot of Enemies", project(ctx.asc, mars, saturn));
    const double twelfthCusp = houseCuspLongitude(ctx, 12);
    double lord12 = 0.0;
    if (lordLonForSign(ctx, signIndex(twelfthCusp), &lord12)) {
        appendLot(lots, ctx, "Lot of Enemies (Hermes)", project(ctx.asc, twelfthCusp, lord12));
    }
    appendLot(lots, ctx, "Lot of Enemies (Sahl 1)",
              ctx.isDay ? project(ctx.asc, fortune, mercury) : project(ctx.asc, mercury, fortune));
    appendLot(lots, ctx, "Lot of Enemies (Sahl 2)",
              ctx.isDay ? project(ctx.asc, moon, mercury) : project(ctx.asc, mercury, moon));

    appendLot(lots, ctx, "Lot of Knowledge", ctx.isDay ? project(ctx.asc, jupiter, moon) : project(ctx.asc, moon, jupiter));
    appendLot(lots, ctx, "Lot of War", project(ctx.asc, moon, saturn));
    appendLot(lots, ctx, "Lot of Peace Among Soldiers", project(ctx.asc, mercury, moon));
    appendLot(lots, ctx, "Lot of Revolution Year", ctx.isDay ? project(sun, venus, moon) : project(sun, moon, venus));

    appendLot(lots, ctx, "Lot of Food/Wheat", project(ctx.asc, mars, sun));
    appendLot(lots, ctx, "Lot of Water", project(ctx.asc, venus, moon));
    appendLot(lots, ctx, "Lot of Barley", project(ctx.asc, jupiter, moon));
    appendLot(lots, ctx, "Lot of Chickpeas", project(ctx.asc, sun, venus));
    appendLot(lots, ctx, "Lot of Lentils", project(ctx.asc, saturn, mars));
    appendLot(lots, ctx, "Lot of Egyptian Beans", project(ctx.asc, mars, saturn));
    appendLot(lots, ctx, "Lot of Indian Peas", project(ctx.asc, mars, saturn));
    appendLot(lots, ctx, "Lot of Dates", project(ctx.asc, venus, sun));
    appendLot(lots, ctx, "Lot of Honey", project(ctx.asc, sun, moon));
    appendLot(lots, ctx, "Lot of Rice", project(ctx.asc, saturn, jupiter));
    appendLot(lots, ctx, "Lot of Olives", project(ctx.asc, moon, mercury));
    appendLot(lots, ctx, "Lot of Grapes", project(ctx.asc, venus, saturn));
    appendLot(lots, ctx, "Lot of Cotton", project(ctx.asc, venus, mercury));
    appendLot(lots, ctx, "Lot of Sesame (Jupiter-Saturn)", project(ctx.asc, jupiter, saturn));
    appendLot(lots, ctx, "Lot of Sesame (Venus-Saturn)", project(ctx.asc, venus, saturn));
    appendLot(lots, ctx, "Lot of Watermelons", project(ctx.asc, saturn, mercury));
    appendLot(lots, ctx, "Lot of Acidic Foods", project(ctx.asc, mars, saturn));
    appendLot(lots, ctx, "Lot of Sweet Foods", project(ctx.asc, venus, sun));
    appendLot(lots, ctx, "Lot of Pungent Foods", project(ctx.asc, saturn, mars));
    appendLot(lots, ctx, "Lot of Bitter Foods", project(ctx.asc, saturn, mercury));
    appendLot(lots, ctx, "Lot of Purgative Sweet Medicines", project(ctx.asc, moon, sun));
    appendLot(lots, ctx, "Lot of Purgative Acidic Medicines", project(ctx.asc, jupiter, saturn));
    appendLot(lots, ctx, "Lot of Purgative Salty Medicines", project(ctx.asc, moon, mars));
    if (hasNorthNode) {
        appendLot(lots, ctx, "Lot of Poisons", project(ctx.asc, saturn, northNode));
    }

    appendLot(lots, ctx, "Lot of Duration of Kingdom", ctx.isDay ? project(moon, 135.0, sun) : project(sun, 105.0, moon));

    return lots;
}

}  // namespace dracoved
