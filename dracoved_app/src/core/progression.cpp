#include "progression.h"

#include "arabic_lots.h"
#include "fixed_stars.h"
#include "formatting.h"
#include "lunar_nodes.h"
#include "timezone_utils.h"

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

constexpr double kDaysPerYear = 365.2425;
constexpr double kNaibodKey = 0.98564733;

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

double aspectExactAngle(const QString& label) {
    if (label == "Conjunction") return 0.0;
    if (label == "Sextile") return 60.0;
    if (label == "Square") return 90.0;
    if (label == "Trine") return 120.0;
    if (label == "Opposition") return 180.0;
    return 0.0;
}

bool aspectApplying(double lonA, double speedA, double lonB, double speedB, double exact) {
    const double dt = 0.05;  // days
    const double cur = std::fabs(angularDiff(lonA, lonB) - exact);
    const double fut = std::fabs(angularDiff(lonA + speedA * dt, lonB + speedB * dt) - exact);
    return fut < cur;
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

double julianFromDateTime(const SwissEph* swe, const QDateTime& utc) {
    const QTime time = utc.time();
    const double hourDec = time.hour() + time.minute() / 60.0 + time.second() / 3600.0 + time.msec() / 3600000.0;
    return swe->julianDay(utc.date().year(), utc.date().month(), utc.date().day(), hourDec, SE_GREG_CAL);
}

}  // namespace

SecondaryProgressionEngine::SecondaryProgressionEngine(SwissEph* swe, const QString& ephePath)
    : swe_(swe), ephePath_(ephePath) {}

void SecondaryProgressionEngine::setEphePath(const QString& path) {
    ephePath_ = path;
}

bool SecondaryProgressionEngine::compute(const NatalInput& natalInput, const QDateTime& targetLocal, const QString& targetTzLabel,
                                        NatalChart* out, QString* error, bool includeDisplayData, bool includeLots) {
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
    const bool siderealMode = (natalInput.zodiacSystem == ZodiacSystem::Sidereal);
    if (siderealMode) {
        swe_->setSidMode(siderealAyanamsaSwissMode(natalInput.siderealAyanamsa));
    }
    const int calcFlags = siderealMode ? SEFLG_SIDEREAL : 0;

    QTimeZone natalTz;
    QString natalTzLabel;
    QString tzErr;
    if (!parseTimezoneInput(natalInput.timezone, &natalTz, &natalTzLabel, &tzErr)) {
        if (error) {
            *error = tzErr;
        }
        return false;
    }

    QDateTime natalLocal(natalInput.date, natalInput.time, natalTz);
    if (!natalLocal.isValid()) {
        if (error) {
            *error = "Invalid natal date/time.";
        }
        return false;
    }
    const QDateTime natalUtc = natalLocal.toUTC();

    QTimeZone targetTz;
    QString targetNormLabel;
    QString targetErr;
    if (!parseTimezoneInput(targetTzLabel, &targetTz, &targetNormLabel, &targetErr)) {
        if (error) {
            *error = targetErr;
        }
        return false;
    }
    QDateTime targetLocalClean(targetLocal.date(), targetLocal.time(), targetTz);
    if (!targetLocalClean.isValid()) {
        if (error) {
            *error = "Invalid progression target date/time.";
        }
        return false;
    }
    const QDateTime targetUtc = targetLocalClean.toUTC();

    const double ageDays = natalUtc.secsTo(targetUtc) / 86400.0;
    const double ageYears = ageDays / kDaysPerYear;

    const double jdNatal = julianFromDateTime(swe_, natalUtc);
    const double jdProg = jdNatal + ageYears;

    const qint64 progMsecs = static_cast<qint64>(std::llround(ageYears * 86400.0 * 1000.0));
    const QDateTime progUtc = natalUtc.addMSecs(progMsecs);
    const QDateTime progLocal = progUtc.toTimeZone(natalTz);

    double natalCuspsRaw[13] = {0};
    double natalAscmc[10] = {0};
    QString houseErr;
    if (!swe_->houses(jdNatal, natalInput.latitude, natalInput.longitude, 'P', natalCuspsRaw, natalAscmc, &houseErr)) {
        if (error) {
            *error = houseErr;
        }
        return false;
    }
    const double armcNatal = normalizeDegrees(natalAscmc[2]);
    const double armcProg = normalizeDegrees(armcNatal + ageYears * kNaibodKey);

    double eps = 0.0;
    QString epsErr;
    if (!swe_->calcUt(jdProg, SE_ECL_NUT, 0, &eps, &epsErr)) {
        if (error) {
            *error = epsErr.isEmpty() ? "Failed to compute obliquity." : epsErr;
        }
        return false;
    }

    double cuspsRaw[13] = {0};
    double ascmc[10] = {0};
    if (!swe_->housesArmc(armcProg, natalInput.latitude, eps, 'P', cuspsRaw, ascmc, &houseErr)) {
        if (error) {
            *error = houseErr;
        }
        return false;
    }
    if (siderealMode) {
        double ayanamsa = 0.0;
        QString ayanErr;
        if (!swe_->getAyanamsaUt(jdProg, &ayanamsa, &ayanErr)) {
            if (error) {
                *error = ayanErr;
            }
            return false;
        }
        for (int i = 1; i <= 12; ++i) {
            cuspsRaw[i] = normalizeDegrees(cuspsRaw[i] - ayanamsa);
        }
        ascmc[0] = normalizeDegrees(ascmc[0] - ayanamsa);
        ascmc[1] = normalizeDegrees(ascmc[1] - ayanamsa);
        ascmc[3] = normalizeDegrees(ascmc[3] - ayanamsa);
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
        {"Lilith", SE_MEAN_APOG},
    };

    QVector<BodyPosition> positions;
    positions.reserve(128);
    QStringList warnings;
    QMap<QString, double> bodyLongitudes;

    int ascSignIdx = signIndex(angles.asc);

    for (const auto& body : bodies) {
        double values[6] = {0};
        QString calcErr;
        if (!swe_->calcUtFull(jdProg, body.sweId, calcFlags | SEFLG_SPEED, values, &calcErr)) {
            if (isAsteroidBody(body.name)) {
                warnings.push_back(QString("Skipped %1: %2").arg(body.name, calcErr));
                continue;
            }
            if (error) {
                *error = QString("Failed to compute %1: %2").arg(body.name).arg(calcErr);
            }
            return false;
        }
        const double lon = normalizeDegrees(values[0]);
        int sidx = signIndex(lon);
        QString sname = signName(sidx);
        int house = 0;
        if (natalInput.houseSystem == HouseSystem::Placidus) {
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

        // Instantaneous ephemeris motion, so stations agree with the event finder.
        pos.speed = values[3]; // Degrees per ephemeris day, signed.
        pos.hasSpeed = true;
        pos.retrograde = pos.speed < 0.0;

        bodyLongitudes.insert(pos.name, lon);
        positions.push_back(pos);
    }

    QVector<CalculatedLunarNode> calculatedNodes;
    QString nodeError;
    if (!calculateLunarNodes(*swe_, jdProg, calcFlags, natalInput.lunarNodePolicy,
                             &calculatedNodes, &nodeError)) {
        if (error) {
            *error = nodeError;
        }
        return false;
    }
    for (const auto& node : calculatedNodes) {
        auto appendNode = [&](bool north) {
            const double lon = normalizeDegrees(node.northLongitude + (north ? 0.0 : 180.0));
            int sidx = signIndex(lon);
            QString sname = signName(sidx);
            int house = 0;
            if (natalInput.houseSystem == HouseSystem::Placidus) {
                house = houseOfLongitude(lon, cusps);
            } else {
                house = ((sidx - ascSignIdx + 12) % 12) + 1;
            }
            BodyPosition position;
            position.name = north
                ? internalNorthNodeName(node.type, natalInput.lunarNodePolicy)
                : internalSouthNodeName(node.type, natalInput.lunarNodePolicy);
            position.longitude = lon;
            position.signIndex = sidx;
            position.signName = sname;
            position.degInSign = degInSign(lon);
            position.house = house;
            position.element = elementForSign(sname);
            position.mode = modeForSign(sname);
            position.dignity = dignityLabel(north ? "North Node" : "South Node", sname);
            position.retrograde = node.retrograde;
            position.speed = node.speed;
            position.hasSpeed = node.hasSpeed;
            position.isLunarNode = true;
            position.isNorthLunarNode = north;
            position.lunarNodeType = node.type;
            positions.push_back(position);
            bodyLongitudes.insert(position.name, lon);
        };
        appendNode(true);
        appendNode(false);
    }

    {
        double vLon = angles.vertex;
        int sidx = signIndex(vLon);
        QString sname = signName(sidx);
        int house = 0;
        if (natalInput.houseSystem == HouseSystem::Placidus) {
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

    int sunHouse = 0;
    for (const auto& pos : positions) {
        if (pos.name == "Sun") {
            sunHouse = pos.house;
            break;
        }
    }
    const bool isDay = (sunHouse >= 7 && sunHouse <= 12);

    double pof = 0.0;
    bool hasPartOfFortune = false;
    if (includeLots) {
        LotCalculationContext lotCtx;
        lotCtx.asc = angles.asc;
        lotCtx.mc = angles.mc;
        lotCtx.cusps = cusps;
        lotCtx.houseSystem = natalInput.houseSystem;
        lotCtx.isDay = isDay;
        lotCtx.gender = natalInput.gender;
        lotCtx.bodies = bodyLongitudes;

        PrenatalSyzygy syzygy;
        QString syzygyErr;
        if (findPrenatalSyzygy(swe_, jdProg, calcFlags, &syzygy, &syzygyErr)) {
            lotCtx.hasPrenatalSyzygy = syzygy.valid;
            lotCtx.prenatalConjunctional = syzygy.conjunctional;
            lotCtx.prenatalSyzygyLongitude = syzygy.longitude;
        } else {
            warnings.push_back(QString("Skipped prenatal-syzygy dependent Lots: %1").arg(syzygyErr));
        }

        const QVector<BodyPosition> lotPositions = calculateArabicLots(lotCtx);
        for (const auto& lot : lotPositions) {
            if (lot.name == "Part of Fortune") {
                pof = lot.longitude;
                hasPartOfFortune = true;
            }
            positions.push_back(lot);
        }
    }

    QVector<HouseCusp> cuspRows;
    if (natalInput.houseSystem == HouseSystem::Placidus) {
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
    if (includeDisplayData) {
        QStringList fixedStarNames = natalInput.fixedStars;
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
            if (!swe_->fixstarUt(starName, jdProg, calcFlags, &starLon, &resolvedName, &starErr)) {
                warnings.push_back(QString("Skipped fixed star %1: %2").arg(starName, starErr));
                continue;
            }

            starLon = normalizeDegrees(starLon);
            const int sidx = signIndex(starLon);
            const QString sname = signName(sidx);
            int house = 0;
            if (natalInput.houseSystem == HouseSystem::Placidus) {
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

    }

    QStringList order, abbrev, glyphs;
    QVector<QVector<AspectGrid::Cell>> grid;
    if (includeDisplayData) {
        const AspectOrbs orbs = normalizedOrbs(natalInput.aspectOrbs);
        order = tropicalBodyOrder();
        abbrev = tropicalBodyAbbrev();
        glyphs = tropicalBodyGlyphs();
        if (natalInput.lunarNodePolicy.mode == LunarNodeMode::Both) {
            const LunarNodeType secondary = effectivePrimaryNodeType(natalInput.lunarNodePolicy) == LunarNodeType::Mean
                ? LunarNodeType::True
                : LunarNodeType::Mean;
            const QString secondaryNorth = internalNorthNodeName(secondary, natalInput.lunarNodePolicy);
            const QString secondarySouth = internalSouthNodeName(secondary, natalInput.lunarNodePolicy);
            int insertAt = order.indexOf("South Node") + 1;
            if (insertAt <= 0) insertAt = order.size();
            order.insert(insertAt, secondaryNorth);
            abbrev.insert(insertAt, secondary == LunarNodeType::Mean ? "mNN" : "tNN");
            glyphs.insert(insertAt, bodyGlyph("North Node"));
            ++insertAt;
            order.insert(insertAt, secondarySouth);
            abbrev.insert(insertAt, secondary == LunarNodeType::Mean ? "mSN" : "tSN");
            glyphs.insert(insertAt, bodyGlyph("South Node"));
        }
        QMap<QString, double> bodyMap;
        QMap<QString, double> speedMap;
        for (const auto& pos : positions) {
            bodyMap.insert(pos.name, pos.longitude);
            if (pos.hasSpeed) {
                speedMap.insert(pos.name, pos.speed);
            }
        }
        bodyMap.insert("Ascendant", angles.asc);
        bodyMap.insert("Midheaven", angles.mc);
        bodyMap.insert("Descendant", angles.desc);
        bodyMap.insert("IC", angles.ic);

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
                    if (speedMap.contains(aName) && speedMap.contains(bName)) {
                        cell.hasMotion = true;
                        cell.applying = aspectApplying(bodyMap.value(aName), speedMap.value(aName),
                                                       bodyMap.value(bName), speedMap.value(bName),
                                                       aspectExactAngle(label));
                    }
                    grid[i][j] = cell;
                    grid[j][i] = cell;
                }
            }
        }

    }

    out->localDateTime = progLocal;
    out->utcDateTime = progUtc;
    out->timezoneLabel = natalTzLabel;
    out->zodiacSystem = natalInput.zodiacSystem;
    out->siderealAyanamsa = natalInput.siderealAyanamsa;
    out->lunarNodePolicy = natalInput.lunarNodePolicy;
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
