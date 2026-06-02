#include "tropical_natal.h"

#include "arabic_lots.h"
#include "fixed_stars.h"
#include "formatting.h"
#include "timezone_utils.h"

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

double angularDiff(double a, double b) {
    double d = std::fmod((a - b + 540.0), 360.0) - 180.0;
    return std::fabs(d);
}

QString aspectSymbol(const QString& label) {
    if (label == "Conjunction") return QString(QChar(0x260C));
    if (label == "Sextile") return QString(QChar(0x2736));
    if (label == "Square") return QString(QChar(0x25A1));
    if (label == "Trine") return QString(QChar(0x25B3));
    if (label == "Opposition") return QString(QChar(0x260D));
    return "";
}

AspectOrbs normalizedOrbs(const AspectOrbs& input) {
    AspectOrbs out = input;
    auto clamp = [](double v) { return v < 0.0 ? 0.0 : v; };
    out.conjunction = clamp(out.conjunction);
    out.sextile = clamp(out.sextile);
    out.square = clamp(out.square);
    out.trine = clamp(out.trine);
    out.opposition = clamp(out.opposition);
    return out;
}

bool aspectFor(double diff, const AspectOrbs& orbs, QString* outLabel, double* outOrb, double* outMaxOrb) {
    struct AspectDef {
        const char* name;
        double exact;
        double orb;
    };
    const AspectDef aspects[] = {
        {"Conjunction", 0.0, orbs.conjunction},
        {"Sextile", 60.0, orbs.sextile},
        {"Square", 90.0, orbs.square},
        {"Trine", 120.0, orbs.trine},
        {"Opposition", 180.0, orbs.opposition},
    };
    for (const auto& asp : aspects) {
        double delta = std::fabs(diff - asp.exact);
        if (delta <= asp.orb) {
            if (outLabel) {
                *outLabel = asp.name;
            }
            if (outOrb) {
                *outOrb = delta;
            }
            if (outMaxOrb) {
                *outMaxOrb = asp.orb;
            }
            return true;
        }
    }
    return false;
}

int houseOfLongitude(double lon, const QVector<double>& cusps) {
    if (cusps.size() < 12) {
        return 0;
    }
    double c1 = cusps[0];
    QVector<QPair<double, int>> rel;
    rel.reserve(cusps.size());
    for (int i = 0; i < cusps.size(); ++i) {
        double v = normalizeDegrees(cusps[i] - c1);
        rel.push_back({v, i});
    }
    std::sort(rel.begin(), rel.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    double x = normalizeDegrees(lon - c1);
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

}  // namespace

TropicalNatalEngine::TropicalNatalEngine(SwissEph* swe, const QString& ephePath)
    : swe_(swe), ephePath_(ephePath) {}

void TropicalNatalEngine::setEphePath(const QString& path) {
    ephePath_ = path;
}

bool TropicalNatalEngine::compute(const NatalInput& input, NatalChart* out, QString* error) {
    if (!swe_ || !out) {
        return false;
    }
    if (!swe_->isLoaded()) {
        if (error) {
            *error = "Swiss Ephemeris is not loaded.";
        }
        return false;
    }
    swe_->setEphePath(ephePath_);
    const bool siderealMode = (input.zodiacSystem == ZodiacSystem::Sidereal);
    if (siderealMode) {
        swe_->setSidMode(siderealAyanamsaSwissMode(input.siderealAyanamsa));
    }
    const int calcFlags = siderealMode ? SEFLG_SIDEREAL : 0;

    QTimeZone tz;
    QString tzLabel;
    QString tzErr;
    if (!parseTimezoneInput(input.timezone, &tz, &tzLabel, &tzErr)) {
        if (error) {
            *error = tzErr;
        }
        return false;
    }

    QDateTime local(input.date, input.time, tz);
    if (!local.isValid()) {
        if (error) {
            *error = "Invalid date/time.";
        }
        return false;
    }
    QDateTime utc = local.toUTC();

    double hourDec = utc.time().hour() + utc.time().minute() / 60.0 + utc.time().second() / 3600.0;
    double jd = swe_->julianDay(utc.date().year(), utc.date().month(), utc.date().day(), hourDec, SE_GREG_CAL);

    double cuspsRaw[13] = {0};
    double ascmc[10] = {0};
    QString houseErr;
    if (!swe_->housesEx(jd, calcFlags, input.latitude, input.longitude, 'P', cuspsRaw, ascmc, &houseErr)) {
        if (error) {
            *error = houseErr;
        }
        return false;
    }

    AnglePositions angles;
    angles.asc = normalizeDegrees(ascmc[0]);
    angles.mc = normalizeDegrees(ascmc[1]);
    angles.vertex = normalizeDegrees(ascmc[3]);
    angles.desc = normalizeDegrees(angles.asc + 180.0);
    angles.ic = normalizeDegrees(angles.mc + 180.0);

    QVector<double> cusps;
    cusps.reserve(12);
    for (int i = 1; i <= 12; ++i) {
        cusps.push_back(normalizeDegrees(cuspsRaw[i]));
    }

    struct BodySpec {
        const char* name;
        int sweId;
    };
    const BodySpec bodies[] = {
        {"Sun", SE_SUN},
        {"Moon", SE_MOON},
        {"Mercury", SE_MERCURY},
        {"Venus", SE_VENUS},
        {"Mars", SE_MARS},
        {"Jupiter", SE_JUPITER},
        {"Saturn", SE_SATURN},
        {"Uranus", SE_URANUS},
        {"Neptune", SE_NEPTUNE},
        {"Pluto", SE_PLUTO},
        {"Chiron", SE_CHIRON},
        {"Ceres", SE_CERES},
        {"Pallas", SE_PALLAS},
        {"Juno", SE_JUNO},
        {"Vesta", SE_VESTA},
        {"Pholus", SE_PHOLUS},
        {"North Node", SE_MEAN_NODE},
        {"Lilith", SE_MEAN_APOG},
    };

    QVector<BodyPosition> positions;
    positions.reserve(128);
    QStringList warnings;
    QMap<QString, double> bodyLongitudes;

    int ascSignIdx = signIndex(angles.asc);

    for (const auto& body : bodies) {
        double lon = 0.0;
        QString calcErr;
        if (!swe_->calcUt(jd, body.sweId, calcFlags, &lon, &calcErr)) {
            if (isAsteroidBody(body.name)) {
                warnings.push_back(QString("Skipped %1: %2").arg(body.name, calcErr));
                continue;
            }
            if (error) {
                *error = QString("Failed to compute %1: %2").arg(body.name).arg(calcErr);
            }
            return false;
        }
        lon = normalizeDegrees(lon);
        int sidx = signIndex(lon);
        QString sname = signName(sidx);
        int house = 0;
        if (input.houseSystem == HouseSystem::Placidus) {
            house = houseOfLongitude(lon, cusps);
        } else {
            house = ((sidx - ascSignIdx + 12) % 12) + 1;
        }

        BodyPosition pos;
        pos.name = body.name;
        pos.longitude = lon;
        pos.signIndex = sidx;
        pos.signName = sname;
        pos.degInSign = degInSign(lon);
        pos.house = house;
        pos.element = elementForSign(sname);
        pos.mode = modeForSign(sname);
        pos.dignity = dignityLabel(body.name, sname);

        // Retrograde check: compare with next day.
        double lonNext = 0.0;
        if (swe_->calcUt(jd + 1.0, body.sweId, calcFlags, &lonNext, nullptr)) {
            double delta = std::fmod((lonNext - lon + 540.0), 360.0) - 180.0;
            pos.retrograde = (delta < 0.0);
        }

        bodyLongitudes.insert(pos.name, lon);
        positions.push_back(pos);
    }

    // South Node = North Node + 180.
    BodyPosition south;
    for (const auto& pos : positions) {
        if (pos.name == "North Node") {
            double lon = normalizeDegrees(pos.longitude + 180.0);
            int sidx = signIndex(lon);
            QString sname = signName(sidx);
            int house = 0;
            if (input.houseSystem == HouseSystem::Placidus) {
                house = houseOfLongitude(lon, cusps);
            } else {
                house = ((sidx - ascSignIdx + 12) % 12) + 1;
            }
            south.name = "South Node";
            south.longitude = lon;
            south.signIndex = sidx;
            south.signName = sname;
            south.degInSign = degInSign(lon);
            south.house = house;
            south.element = elementForSign(sname);
            south.mode = modeForSign(sname);
            south.dignity = dignityLabel("South Node", sname);
            south.retrograde = true;
            positions.push_back(south);
            break;
        }
    }

    // Add Vertex
    {
        double vLon = angles.vertex;
        int sidx = signIndex(vLon);
        QString sname = signName(sidx);
        int house = 0;
        if (input.houseSystem == HouseSystem::Placidus) {
            house = houseOfLongitude(vLon, cusps);
        } else {
            house = ((sidx - ascSignIdx + 12) % 12) + 1;
        }
        BodyPosition vx;
        vx.name = "Vertex";
        vx.longitude = vLon;
        vx.signIndex = sidx;
        vx.signName = sname;
        vx.degInSign = degInSign(vLon);
        vx.house = house;
        vx.element = elementForSign(sname);
        vx.mode = modeForSign(sname);
        vx.dignity = "-";
        positions.push_back(vx);
    }

    // Determine day/night by Sun's house position.
    int sunHouse = 0;
    for (const auto& pos : positions) {
        if (pos.name == "Sun") {
            sunHouse = pos.house;
            break;
        }
    }
    bool isDay = (sunHouse >= 7 && sunHouse <= 12);

    LotCalculationContext lotCtx;
    lotCtx.asc = angles.asc;
    lotCtx.mc = angles.mc;
    lotCtx.cusps = cusps;
    lotCtx.houseSystem = input.houseSystem;
    lotCtx.isDay = isDay;
    lotCtx.gender = input.gender;
    lotCtx.bodies = bodyLongitudes;

    PrenatalSyzygy syzygy;
    QString syzygyErr;
    if (findPrenatalSyzygy(swe_, jd, calcFlags, &syzygy, &syzygyErr)) {
        lotCtx.hasPrenatalSyzygy = syzygy.valid;
        lotCtx.prenatalConjunctional = syzygy.conjunctional;
        lotCtx.prenatalSyzygyLongitude = syzygy.longitude;
    } else {
        warnings.push_back(QString("Skipped prenatal-syzygy dependent Lots: %1").arg(syzygyErr));
    }

    double pof = 0.0;
    bool hasPartOfFortune = false;
    const QVector<BodyPosition> lotPositions = calculateArabicLots(lotCtx);
    for (const auto& lot : lotPositions) {
        if (lot.name == "Part of Fortune") {
            pof = lot.longitude;
            hasPartOfFortune = true;
        }
        positions.push_back(lot);
    }

    // Build cusps for Placidus only.
    QVector<HouseCusp> cuspRows;
    if (input.houseSystem == HouseSystem::Placidus) {
        cuspRows.reserve(12);
        for (int i = 0; i < cusps.size(); ++i) {
            HouseCusp c;
            c.number = i + 1;
            c.longitude = cusps[i];
            c.signName = signName(signIndex(cusps[i]));
            cuspRows.push_back(c);
        }
    }

    QVector<FixedStarPosition> fixedStars;
    QStringList fixedStarNames = input.fixedStars;
    if (fixedStarNames.isEmpty()) {
        fixedStarNames = defaultFixedStars();
    }
    QSet<QString> seenStars;
    for (const auto& requestedName : fixedStarNames) {
        const QString starName = requestedName.trimmed();
        const QString starKey = starName.toCaseFolded();
        if (starName.isEmpty() || seenStars.contains(starKey)) {
            continue;
        }
        seenStars.insert(starKey);

        double starLon = 0.0;
        QString resolvedName;
        QString starErr;
        if (!swe_->fixstarUt(starName, jd, calcFlags, &starLon, &resolvedName, &starErr)) {
            warnings.push_back(QString("Skipped fixed star %1: %2").arg(starName, starErr));
            continue;
        }

        starLon = normalizeDegrees(starLon);
        const int sidx = signIndex(starLon);
        const QString sname = signName(sidx);
        int house = 0;
        if (input.houseSystem == HouseSystem::Placidus) {
            house = houseOfLongitude(starLon, cusps);
        } else {
            house = ((sidx - ascSignIdx + 12) % 12) + 1;
        }

        const QString displayName = resolvedName.section(',', 0, 0).trimmed();
        FixedStarPosition star;
        star.name = displayName.isEmpty() ? starName : displayName;
        star.longitude = starLon;
        star.signIndex = sidx;
        star.signName = sname;
        star.degInSign = degInSign(starLon);
        star.house = house;
        fixedStars.push_back(star);
    }

    // Build aspects grid.
    const AspectOrbs orbs = normalizedOrbs(input.aspectOrbs);
    QStringList order = tropicalBodyOrder();
    QStringList abbrev = tropicalBodyAbbrev();
    QStringList glyphs = tropicalBodyGlyphs();
    QMap<QString, double> bodyMap;
    for (const auto& pos : positions) {
        bodyMap.insert(pos.name, pos.longitude);
    }
    bodyMap.insert("Ascendant", angles.asc);
    bodyMap.insert("Midheaven", angles.mc);
    bodyMap.insert("Descendant", angles.desc);
    bodyMap.insert("IC", angles.ic);

    QVector<QVector<AspectGrid::Cell>> grid;
    grid.resize(order.size());
    for (int i = 0; i < order.size(); ++i) {
        grid[i].resize(order.size());
    }

    for (int i = 0; i < order.size(); ++i) {
        for (int j = i + 1; j < order.size(); ++j) {
            const QString& aName = order[i];
            const QString& bName = order[j];
            if (!bodyMap.contains(aName) || !bodyMap.contains(bName)) {
                continue;
            }
            double diff = angularDiff(bodyMap.value(aName), bodyMap.value(bName));
            QString label;
            double orb = 0.0;
            double maxOrb = 0.0;
            if (aspectFor(diff, orbs, &label, &orb, &maxOrb)) {
                AspectGrid::Cell cell;
                cell.label = label;
                cell.symbol = aspectSymbol(label);
                cell.orb = orb;
                cell.maxOrb = maxOrb;
                cell.hasAspect = true;
                grid[i][j] = cell;
                grid[j][i] = cell;
            }
        }
    }

    out->localDateTime = local;
    out->utcDateTime = utc;
    out->timezoneLabel = tzLabel;
    out->zodiacSystem = input.zodiacSystem;
    out->siderealAyanamsa = input.siderealAyanamsa;
    out->angles = angles;
    out->bodies = positions;
    out->fixedStars = fixedStars;
    out->cusps = cuspRows;
    out->warnings = warnings;
    out->isDayChart = isDay;
    out->partOfFortune = pof;
    out->hasPartOfFortune = hasPartOfFortune;
    out->aspects.bodyOrder = order.toVector();
    out->aspects.bodyAbbrev = abbrev.toVector();
    out->aspects.bodyGlyphs = glyphs.toVector();
    out->aspects.cells = grid;

    return true;
}

}  // namespace dracoved
