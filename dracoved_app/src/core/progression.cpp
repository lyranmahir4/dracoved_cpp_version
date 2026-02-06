#include "progression.h"

#include "formatting.h"
#include "timezone_utils.h"

#include <QDateTime>
#include <QMap>
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
                                        NatalChart* out, QString* error) {
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
    positions.reserve(24);
    QStringList warnings;

    int ascSignIdx = signIndex(angles.asc);
    double sunLon = 0.0;
    double moonLon = 0.0;

    for (const auto& body : bodies) {
        double lon = 0.0;
        QString calcErr;
        if (!swe_->calcUt(jdProg, body.sweId, 0, &lon, &calcErr)) {
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

        double lonNext = 0.0;
        if (swe_->calcUt(jdProg + 1.0, body.sweId, 0, &lonNext, nullptr)) {
            double delta = std::fmod((lonNext - lon + 540.0), 360.0) - 180.0;
            pos.retrograde = (delta < 0.0);
        }

        if (pos.name == "Sun") {
            sunLon = lon;
        } else if (pos.name == "Moon") {
            moonLon = lon;
        }

        positions.push_back(pos);
    }

    BodyPosition south;
    for (const auto& pos : positions) {
        if (pos.name == "North Node") {
            double lon = normalizeDegrees(pos.longitude + 180.0);
            int sidx = signIndex(lon);
            QString sname = signName(sidx);
            int house = 0;
            if (natalInput.houseSystem == HouseSystem::Placidus) {
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
    if (sunLon > 0.0 || moonLon > 0.0) {
        if (isDay) {
            pof = normalizeDegrees(angles.asc + moonLon - sunLon);
        } else {
            pof = normalizeDegrees(angles.asc + sunLon - moonLon);
        }
    }

    {
        int sidx = signIndex(pof);
        QString sname = signName(sidx);
        int house = 0;
        if (natalInput.houseSystem == HouseSystem::Placidus) {
            house = houseOfLongitude(pof, cusps);
        } else {
            house = ((sidx - ascSignIdx + 12) % 12) + 1;
        }
        BodyPosition pf;
        pf.name = "Part of Fortune";
        pf.longitude = pof;
        pf.signIndex = sidx;
        pf.signName = sname;
        pf.degInSign = degInSign(pof);
        pf.house = house;
        pf.element = elementForSign(sname);
        pf.mode = modeForSign(sname);
        pf.dignity = "-";
        positions.push_back(pf);
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

    const AspectOrbs orbs = normalizedOrbs(natalInput.aspectOrbs);
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

    out->localDateTime = progLocal;
    out->utcDateTime = progUtc;
    out->timezoneLabel = natalTzLabel;
    out->angles = angles;
    out->bodies = positions;
    out->cusps = cuspRows;
    out->warnings = warnings;
    out->isDayChart = isDay;
    out->partOfFortune = pof;
    out->hasPartOfFortune = true;
    out->aspects.bodyOrder = order.toVector();
    out->aspects.bodyAbbrev = abbrev.toVector();
    out->aspects.bodyGlyphs = glyphs.toVector();
    out->aspects.cells = grid;

    return true;
}

}  // namespace dracoved
