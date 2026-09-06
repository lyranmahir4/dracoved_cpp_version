// Shared Markdown report building blocks used by the report exporters
// (Natal and Solar Return). Kept header-only so every report produces
// identical tables, position formats, and aspect filtering.
#pragma once

#include "../core/chart_types.h"
#include "../core/formatting.h"
#include "transit_calc_service.h"

#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace dracoved {
namespace reportmd {

struct ReportPoint {
    QString name;
    double longitude = 0.0;
    double speed = 0.0;
    bool hasSpeed = false;
    int oppositeIndex = -1;      // index of the point 180 degrees away (Asc/Desc, MC/IC, node pair)
    bool derivedOpposite = false; // true for the derived side (Descendant, IC, South Node)
};

struct ReportAspect {
    QString left;
    QString aspect;
    QString right;
    double orb = 0.0;
    QString motion;
};

inline QString markdownCell(QString value) {
    value.replace('\\', "\\\\");
    value.replace('|', "\\|");
    value.replace('\r', ' ');
    value.replace('\n', ' ');
    return value.trimmed().isEmpty() ? QString("-") : value.trimmed();
}

inline void appendTable(QStringList& lines,
                        const QStringList& headers,
                        const QVector<QStringList>& rows) {
    QStringList escapedHeaders;
    for (const auto& header : headers) {
        escapedHeaders.push_back(markdownCell(header));
    }
    lines.push_back(QString("| %1 |").arg(escapedHeaders.join(" | ")));
    QStringList separators;
    for (int i = 0; i < headers.size(); ++i) {
        separators.push_back("---");
    }
    lines.push_back(QString("| %1 |").arg(separators.join(" | ")));
    if (rows.isEmpty()) {
        QStringList emptyRow{"No data available"};
        while (emptyRow.size() < headers.size()) {
            emptyRow.push_back("-");
        }
        lines.push_back(QString("| %1 |").arg(emptyRow.join(" | ")));
        return;
    }
    for (const auto& row : rows) {
        QStringList cells;
        for (int i = 0; i < headers.size(); ++i) {
            cells.push_back(markdownCell(row.value(i)));
        }
        lines.push_back(QString("| %1 |").arg(cells.join(" | ")));
    }
}

inline QString houseSystemName(HouseSystem system) {
    return system == HouseSystem::Placidus ? "Placidus" : "Whole Sign";
}

inline QString zodiacName(const NatalInput& input) {
    if (input.zodiacSystem == ZodiacSystem::Sidereal) {
        return QString("Sidereal (%1)")
            .arg(siderealAyanamsaToString(input.siderealAyanamsa));
    }
    return "Tropical";
}

inline QString dateTimeText(const QDateTime& value) {
    return value.isValid()
        ? value.toString("dddd, d MMMM yyyy, h:mm:ss AP")
        : QString("-");
}

inline QString coordinateText(double value, bool latitude) {
    const QChar direction = latitude
        ? (value < 0.0 ? QChar('S') : QChar('N'))
        : (value < 0.0 ? QChar('W') : QChar('E'));
    return QString("%1%2 %3")
        .arg(QString::number(std::abs(value), 'f', 6))
        .arg(QChar(0x00B0))
        .arg(direction);
}

inline QString positionText(double longitude) {
    const double position = normalizeDegrees(longitude);
    double value = degInSign(position);
    int degrees = static_cast<int>(value);
    double minuteValue = (value - degrees) * 60.0;
    int minutes = static_cast<int>(minuteValue);
    int seconds = static_cast<int>(std::round((minuteValue - minutes) * 60.0));
    if (seconds >= 60) {
        seconds = 0;
        ++minutes;
    }
    if (minutes >= 60) {
        minutes = 0;
        ++degrees;
    }
    if (degrees >= 30) {
        degrees = 0;
    }
    return QString("%1 %2%3 %4%5 %6%7")
        .arg(signName(signIndex(position)))
        .arg(degrees)
        .arg(QChar(0x00B0))
        .arg(QString::number(minutes).rightJustified(2, '0'))
        .arg(QChar(0x2032))
        .arg(QString::number(seconds).rightJustified(2, '0'))
        .arg(QChar(0x2033));
}

inline QString orbText(double orb) {
    qint64 totalSeconds = qRound64(std::max(0.0, orb) * 3600.0);
    const qint64 degrees = totalSeconds / 3600;
    totalSeconds %= 3600;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QString("%1%2 %3%4 %5%6")
        .arg(degrees)
        .arg(QChar(0x00B0))
        .arg(QString::number(minutes).rightJustified(2, '0'))
        .arg(QChar(0x2032))
        .arg(QString::number(seconds).rightJustified(2, '0'))
        .arg(QChar(0x2033));
}

inline QString dailyMotionText(const BodyPosition& body) {
    if (!body.hasSpeed || !std::isfinite(body.speed)) {
        return "-";
    }
    return QString("%1%2/day")
        .arg(body.speed >= 0.0 ? "+" : "")
        .arg(QString::number(body.speed, 'f', 6));
}

inline QString traditionalRuler(int sign) {
    static const QStringList rulers = {
        "Mars", "Venus", "Mercury", "Moon", "Sun", "Mercury",
        "Venus", "Mars", "Jupiter", "Saturn", "Saturn", "Jupiter",
    };
    return sign >= 0 && sign < rulers.size() ? rulers[sign] : QString("-");
}

inline int completedYears(const QDate& birth, const QDate& reference) {
    if (!birth.isValid() || !reference.isValid()) {
        return 0;
    }
    int years = reference.year() - birth.year();
    if (reference < birth.addYears(years)) {
        --years;
    }
    return std::max(0, years);
}

inline bool isCoreLot(const QString& name) {
    return name == "Part of Fortune"
        || name == "Lot of Spirit"
        || name == "Lot of Eros";
}

inline bool shouldIncludeBody(const QString& name,
                              bool includeMinorBodies,
                              int lotScope) {
    if (name == "Vertex") {
        return false;
    }
    if (isArabicLotName(name)) {
        return lotScope >= 2 || (lotScope == 1 && isCoreLot(name));
    }
    if (isAsteroidBody(name) || name == "Lilith") {
        return includeMinorBodies;
    }
    return true;
}

inline QVector<BodyPosition> orderedBodies(const NatalChart& chart) {
    QMap<QString, BodyPosition> byName;
    for (const auto& body : chart.bodies) {
        byName.insert(body.name, body);
    }
    QVector<BodyPosition> result;
    for (const auto& name : bodyOrderForLunarNodePolicy(chart.lunarNodePolicy)) {
        const auto it = byName.constFind(name);
        if (it == byName.cend()) {
            continue;
        }
        result.push_back(it.value());
        byName.remove(name);
    }
    for (auto it = byName.cbegin(); it != byName.cend(); ++it) {
        result.push_back(it.value());
    }
    return result;
}

inline QString bodyName(const BodyPosition& body, const LunarNodePolicy& policy) {
    return lunarNodeDisplayName(body.name, policy);
}

inline const BodyPosition* findBody(const NatalChart& chart, const QString& name) {
    for (const auto& body : chart.bodies) {
        if (body.name == name) {
            return &body;
        }
    }
    return nullptr;
}

inline QString placementText(const NatalChart& chart, const QString& name) {
    const auto* body = findBody(chart, name);
    if (!body) {
        return "-";
    }
    return QString("%1, House %2%3")
        .arg(positionText(body->longitude))
        .arg(body->house > 0 ? QString::number(body->house) : QString("-"))
        .arg(body->retrograde ? " (retrograde)" : "");
}

inline QVector<ReportPoint> aspectPoints(const NatalChart& chart,
                                         bool includeMinorBodies,
                                         int lotScope) {
    QVector<ReportPoint> points;
    QSet<QString> used;
    QMap<LunarNodeType, QPair<int, int>> nodeIndexes;  // type -> {north index, south index}
    for (const auto& body : orderedBodies(chart)) {
        if (!shouldIncludeBody(body.name, includeMinorBodies, lotScope)) {
            continue;
        }
        points.push_back({
            lunarNodeDisplayName(body.name, chart.lunarNodePolicy),
            body.longitude,
            body.speed,
            body.hasSpeed && !isArabicLotName(body.name),
            -1,
            false,
        });
        if (body.isLunarNode) {
            auto& pair = nodeIndexes[body.lunarNodeType];
            if (body.isNorthLunarNode) {
                pair.first = points.size() - 1;
            } else {
                pair.second = points.size() - 1;
            }
        }
        used.insert(body.name);
    }
    points.push_back({"Ascendant", chart.angles.asc, 0.0, false});
    points.push_back({"Midheaven", chart.angles.mc, 0.0, false});
    points.push_back({"Descendant", chart.angles.desc, 0.0, false});
    points.push_back({"IC", chart.angles.ic, 0.0, false});
    points.push_back({"Vertex", chart.angles.vertex, 0.0, false});
    if (lotScope > 0 && chart.hasPartOfFortune && !used.contains("Part of Fortune")) {
        points.push_back({"Part of Fortune", chart.partOfFortune, 0.0, false});
    }

    const auto linkOpposites = [&points](int primaryIndex, int derivedIndex) {
        if (primaryIndex < 0 || derivedIndex < 0) {
            return;
        }
        points[primaryIndex].oppositeIndex = derivedIndex;
        points[derivedIndex].oppositeIndex = primaryIndex;
        points[derivedIndex].derivedOpposite = true;
    };
    int ascendantIndex = -1, midheavenIndex = -1, descendantIndex = -1, icIndex = -1;
    for (int i = 0; i < points.size(); ++i) {
        if (points[i].name == "Ascendant") {
            ascendantIndex = i;
        } else if (points[i].name == "Midheaven") {
            midheavenIndex = i;
        } else if (points[i].name == "Descendant") {
            descendantIndex = i;
        } else if (points[i].name == "IC") {
            icIndex = i;
        }
    }
    linkOpposites(ascendantIndex, descendantIndex);
    linkOpposites(midheavenIndex, icIndex);
    for (auto it = nodeIndexes.cbegin(); it != nodeIndexes.cend(); ++it) {
        linkOpposites(it.value().first, it.value().second);
    }
    return points;
}

inline double aspectAngle(const QString& label) {
    return transitcalc::aspectAngleForLabel(label);
}

inline bool applyingAspect(double leftLongitude,
                           double leftSpeed,
                           double rightLongitude,
                           double rightSpeed,
                           const QString& label) {
    const double exact = aspectAngle(label);
    const double step = 0.05;
    const double currentOrb =
        std::abs(transitcalc::angularDiffAbs(leftLongitude, rightLongitude) - exact);
    const double futureOrb = std::abs(
        transitcalc::angularDiffAbs(
            leftLongitude + leftSpeed * step,
            rightLongitude + rightSpeed * step)
        - exact);
    return futureOrb < currentOrb;
}

inline bool reportAspectFor(const ReportPoint& left,
                            const ReportPoint& right,
                            const AspectOrbs& orbs,
                            double displayLimit,
                            bool rightIsFixed,
                            ReportAspect* output) {
    QString label;
    double orb = 0.0;
    double maximum = 0.0;
    if (!transitcalc::aspectForDiff(
            transitcalc::angularDiffAbs(left.longitude, right.longitude),
            orbs, &label, &orb, &maximum)) {
        return false;
    }
    if (displayLimit > 0.0 && orb > displayLimit) {
        return false;
    }
    if (!output) {
        return true;
    }
    output->left = left.name;
    output->aspect = label;
    output->right = right.name;
    output->orb = orb;
    constexpr double halfArcSecond = 0.5 / 3600.0;
    if (orb <= halfArcSecond) {
        output->motion = "Exact";
    } else if (left.hasSpeed && (rightIsFixed || right.hasSpeed)) {
        const double rightSpeed = rightIsFixed ? 0.0 : right.speed;
        if (std::abs(left.speed - rightSpeed) < 1.0e-10) {
            output->motion = "Static";
        } else {
            output->motion = applyingAspect(
                left.longitude, left.speed, right.longitude, rightSpeed, label)
                ? "Applying"
                : "Separating";
        }
    } else {
        output->motion = "-";
    }
    return true;
}

inline QVector<ReportAspect> internalAspects(const NatalChart& chart,
                                             const AspectOrbs& orbs,
                                             double displayLimit,
                                             bool includeMinorBodies,
                                             int lotScope) {
    const auto points = aspectPoints(chart, includeMinorBodies, lotScope);
    QVector<ReportAspect> result;
    for (int left = 0; left < points.size(); ++left) {
        for (int right = left + 1; right < points.size(); ++right) {
            // Mutual opposites (Asc/Desc, MC/IC, north/south node pair) are
            // exactly 180 degrees apart by construction: never a real aspect.
            if (points[right].oppositeIndex == left
                || points[left].oppositeIndex == right) {
                continue;
            }
            // An aspect to Descendant/IC/South Node is always mirrored, at the
            // identical orb, by an aspect to Ascendant/MC/North Node (conj<->opp,
            // sextile<->trine, square<->square). Keep only the primary-side row.
            if (points[left].derivedOpposite && points[right].oppositeIndex < 0) {
                continue;
            }
            if (points[right].derivedOpposite && points[left].oppositeIndex < 0) {
                continue;
            }
            ReportAspect aspect;
            if (reportAspectFor(
                    points[left], points[right], orbs, displayLimit, false, &aspect)) {
                result.push_back(aspect);
            }
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.orb < b.orb;
    });
    return result;
}

inline void appendAspectTable(QStringList& lines, const QVector<ReportAspect>& aspects) {
    QVector<QStringList> rows;
    rows.reserve(aspects.size());
    for (const auto& aspect : aspects) {
        rows.push_back({
            aspect.left,
            aspect.aspect,
            aspect.right,
            orbText(aspect.orb),
            aspect.motion,
        });
    }
    appendTable(lines, {"Point A", "Aspect", "Point B", "Orb", "Motion"}, rows);
}

inline QStringList positionHeaders(bool includeDailyMotion, bool includeDignities) {
    QStringList headers{"Body or Point", "Position", "House", "Motion"};
    if (includeDailyMotion) {
        headers.push_back("Daily Motion");
    }
    if (includeDignities) {
        headers.push_back("Dignity");
    }
    return headers;
}

inline QVector<QStringList> positionRows(const NatalChart& chart,
                                         const NatalInput& input,
                                         const QVector<HouseCusp>& placidusCusps,
                                         bool includeMinorBodies,
                                         int lotScope,
                                         bool includeDailyMotion,
                                         bool includeDignities) {
    QVector<QStringList> rows;
    for (const auto& body : orderedBodies(chart)) {
        if (!shouldIncludeBody(body.name, includeMinorBodies, lotScope)) {
            continue;
        }
        int house = body.house;
        if (house <= 0) {
            house = transitcalc::calcHouseForLongitude(
                body.longitude,
                input.houseSystem == HouseSystem::Placidus
                    ? placidusCusps
                    : QVector<HouseCusp>{},
                chart.angles.asc,
                input.houseSystem);
        }
        const bool hasPhysicalMotion = body.hasSpeed
            && std::isfinite(body.speed)
            && !isArabicLotName(body.name);
        QStringList row{
            bodyName(body, chart.lunarNodePolicy),
            positionText(body.longitude),
            house > 0 ? QString::number(house) : "-",
            hasPhysicalMotion ? (body.retrograde ? "Retrograde" : "Direct") : "-",
        };
        if (includeDailyMotion) {
            row.push_back(hasPhysicalMotion ? dailyMotionText(body) : QString("-"));
        }
        if (includeDignities) {
            row.push_back(body.dignity.isEmpty() ? QString("-") : body.dignity);
        }
        rows.push_back(row);
    }
    return rows;
}

inline QVector<QStringList> houseRows(const NatalChart& chart,
                                      HouseSystem system,
                                      const QVector<HouseCusp>& placidusCusps) {
    QVector<QStringList> rows;
    if (system == HouseSystem::Placidus && placidusCusps.size() == 12) {
        QVector<HouseCusp> cusps = placidusCusps;
        std::sort(cusps.begin(), cusps.end(), [](const auto& a, const auto& b) {
            return a.number < b.number;
        });
        for (const auto& cusp : cusps) {
            const int sign = signIndex(cusp.longitude);
            rows.push_back({
                QString::number(cusp.number),
                positionText(cusp.longitude),
                signName(sign),
                traditionalRuler(sign),
            });
        }
        return rows;
    }
    const int ascendantSign = signIndex(chart.angles.asc);
    for (int house = 1; house <= 12; ++house) {
        const int sign = (ascendantSign + house - 1) % 12;
        rows.push_back({
            QString::number(house),
            QString("%1 0%2").arg(signName(sign)).arg(QChar(0x00B0)),
            signName(sign),
            traditionalRuler(sign),
        });
    }
    return rows;
}

inline QVector<QStringList> fixedStarRows(const NatalChart& chart) {
    QVector<QStringList> rows;
    QVector<FixedStarPosition> stars = chart.fixedStars;
    std::sort(stars.begin(), stars.end(), [](const auto& a, const auto& b) {
        return a.longitude < b.longitude;
    });
    for (const auto& star : stars) {
        rows.push_back({
            star.name,
            positionText(star.longitude),
            star.house > 0 ? QString::number(star.house) : "-",
        });
    }
    return rows;
}

}  // namespace reportmd
}  // namespace dracoved
