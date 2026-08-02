#include "main_window.h"

#include "transit_calc_service.h"

#include "../core/formatting.h"

#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

struct ReportPoint {
    QString name;
    double longitude = 0.0;
    double speed = 0.0;
    bool hasSpeed = false;
};

struct ReportAspect {
    QString left;
    QString aspect;
    QString right;
    double orb = 0.0;
    QString motion;
};

QString markdownCell(QString value) {
    value.replace('\\', "\\\\");
    value.replace('|', "\\|");
    value.replace('\r', ' ');
    value.replace('\n', ' ');
    return value.trimmed().isEmpty() ? QString("-") : value.trimmed();
}
void appendTable(QStringList& lines,
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

QString houseSystemName(HouseSystem system) {
    return system == HouseSystem::Placidus ? "Placidus" : "Whole Sign";
}

QString zodiacName(const NatalInput& input) {
    if (input.zodiacSystem == ZodiacSystem::Sidereal) {
        return QString("Sidereal (%1)")
            .arg(siderealAyanamsaToString(input.siderealAyanamsa));
    }
    return "Tropical";
}

QString dateTimeText(const QDateTime& value) {
    return value.isValid()
        ? value.toString("dddd, d MMMM yyyy, h:mm:ss AP")
        : QString("-");
}

QString coordinateText(double value, bool latitude) {
    const QChar direction = latitude
        ? (value < 0.0 ? QChar('S') : QChar('N'))
        : (value < 0.0 ? QChar('W') : QChar('E'));
    return QString("%1%2 %3")
        .arg(QString::number(std::abs(value), 'f', 6))
        .arg(QChar(0x00B0))
        .arg(direction);
}

QString positionText(double longitude) {
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

QString orbText(double orb) {
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

QString dailyMotionText(const BodyPosition& body) {
    if (!body.hasSpeed || !std::isfinite(body.speed)) {
        return "-";
    }
    return QString("%1%2/day")
        .arg(body.speed >= 0.0 ? "+" : "")
        .arg(QString::number(body.speed, 'f', 6));
}

QString traditionalRuler(int sign) {
    static const QStringList rulers = {
        "Mars", "Venus", "Mercury", "Moon", "Sun", "Mercury",
        "Venus", "Mars", "Jupiter", "Saturn", "Saturn", "Jupiter",
    };
    return sign >= 0 && sign < rulers.size() ? rulers[sign] : QString("-");
}

int completedYears(const QDate& birth, const QDate& reference) {
    if (!birth.isValid() || !reference.isValid()) {
        return 0;
    }
    int years = reference.year() - birth.year();
    if (reference < birth.addYears(years)) {
        --years;
    }
    return std::max(0, years);
}

bool isCoreLot(const QString& name) {
    return name == "Part of Fortune"
        || name == "Lot of Spirit"
        || name == "Lot of Eros";
}

bool shouldIncludeBody(const QString& name,
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

bool isStructuralAspectPair(const QString& left, const QString& right) {
    const auto matches = [&](const QString& first, const QString& second) {
        return (left == first && right == second)
            || (left == second && right == first);
    };
    if (matches("Ascendant", "Descendant") || matches("Midheaven", "IC")) {
        return true;
    }
    return isLunarNodeName(left)
        && isLunarNodeName(right)
        && isNorthLunarNodeName(left) != isNorthLunarNodeName(right);
}

QVector<BodyPosition> orderedBodies(const NatalChart& chart) {
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

QString bodyName(const BodyPosition& body, const LunarNodePolicy& policy) {
    return lunarNodeDisplayName(body.name, policy);
}

const BodyPosition* findBody(const NatalChart& chart, const QString& name) {
    for (const auto& body : chart.bodies) {
        if (body.name == name) {
            return &body;
        }
    }
    return nullptr;
}

QString placementText(const NatalChart& chart, const QString& name) {
    const auto* body = findBody(chart, name);
    if (!body) {
        return "-";
    }
    return QString("%1, House %2%3")
        .arg(positionText(body->longitude))
        .arg(body->house > 0 ? QString::number(body->house) : QString("-"))
        .arg(body->retrograde ? " (retrograde)" : "");
}

QVector<ReportPoint> aspectPoints(const NatalChart& chart,
                                  bool includeMinorBodies,
                                  int lotScope) {
    QVector<ReportPoint> points;
    QSet<QString> used;
    for (const auto& body : orderedBodies(chart)) {
        if (!shouldIncludeBody(body.name, includeMinorBodies, lotScope)) {
            continue;
        }
        points.push_back({
            lunarNodeDisplayName(body.name, chart.lunarNodePolicy),
            body.longitude,
            body.speed,
            body.hasSpeed && !isArabicLotName(body.name),
        });
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
    return points;
}

double aspectAngle(const QString& label) {
    return transitcalc::aspectAngleForLabel(label);
}

bool applyingAspect(double leftLongitude,
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

bool reportAspectFor(const ReportPoint& left,
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

QVector<ReportAspect> internalAspects(const NatalChart& chart,
                                      const AspectOrbs& orbs,
                                      double displayLimit,
                                      bool includeMinorBodies,
                                      int lotScope) {
    const auto points = aspectPoints(chart, includeMinorBodies, lotScope);
    QVector<ReportAspect> result;
    for (int left = 0; left < points.size(); ++left) {
        for (int right = left + 1; right < points.size(); ++right) {
            if (isStructuralAspectPair(points[left].name, points[right].name)) {
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

QVector<ReportAspect> crossAspects(const NatalChart& solar,
                                   const NatalChart& natal,
                                   const AspectOrbs& orbs,
                                   double displayLimit,
                                   bool includeMinorBodies,
                                   int lotScope) {
    const auto solarPoints = aspectPoints(solar, includeMinorBodies, lotScope);
    const auto natalPoints = aspectPoints(natal, includeMinorBodies, lotScope);
    QVector<ReportAspect> result;
    for (const auto& solarPoint : solarPoints) {
        for (const auto& natalPoint : natalPoints) {
            ReportAspect aspect;
            if (!reportAspectFor(
                    solarPoint, natalPoint, orbs, displayLimit, true, &aspect)) {
                continue;
            }
            aspect.left.prepend("SR ");
            aspect.right.prepend("Natal ");
            result.push_back(aspect);
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.orb < b.orb;
    });
    return result;
}

void appendAspectTable(QStringList& lines, const QVector<ReportAspect>& aspects) {
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

QStringList positionHeaders(bool includeDailyMotion, bool includeDignities) {
    QStringList headers{"Body or Point", "Position", "House", "Motion"};
    if (includeDailyMotion) {
        headers.push_back("Daily Motion");
    }
    if (includeDignities) {
        headers.push_back("Dignity");
    }
    return headers;
}

QVector<QStringList> positionRows(const NatalChart& chart,
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

QVector<QStringList> houseRows(const NatalChart& chart,
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

QVector<QStringList> overlayRows(const NatalChart& moving,
                                 const NatalChart& reference,
                                 HouseSystem referenceSystem,
                                 const QVector<HouseCusp>& referenceCusps,
                                 bool includeMinorBodies,
                                 int lotScope) {
    QVector<QStringList> rows;
    for (const auto& body : orderedBodies(moving)) {
        if (!shouldIncludeBody(body.name, includeMinorBodies, lotScope)) {
            continue;
        }
        const int house = transitcalc::calcHouseForLongitude(
            body.longitude,
            referenceSystem == HouseSystem::Placidus
                ? referenceCusps
                : QVector<HouseCusp>{},
            reference.angles.asc,
            referenceSystem);
        rows.push_back({
            bodyName(body, moving.lunarNodePolicy),
            positionText(body.longitude),
            house > 0 ? QString::number(house) : "-",
        });
    }
    return rows;
}

QVector<QStringList> fixedStarRows(const NatalChart& chart) {
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

}  // namespace

QString MainWindow::buildSolarReturnReportMarkdown() const {
    if (!hasCurrentChart_ || !hasSolarChart_) {
        return QString();
    }

    const int returnYear = currentSolarChart_.localDateTime.isValid()
        ? currentSolarChart_.localDateTime.date().year()
        : currentSolarInput_.date.year();
    const int age = completedYears(
        currentChart_.localDateTime.date(),
        currentSolarChart_.localDateTime.date());
    const int profectedHouse = (age % 12) + 1;
    const int natalAscendantSign = signIndex(currentChart_.angles.asc);
    const int profectedSign = (natalAscendantSign + age) % 12;
    const QString lordOfYear = traditionalRuler(profectedSign);
    const bool includeMinorBodies = solarReportOptions_.includeMinorBodies;
    const int lotScope = static_cast<int>(solarReportOptions_.lotScope);
    double reportOrbLimit = 0.0;
    QString aspectScope = "Configured per-aspect orbs";
    if (solarReportOptions_.aspectScope == SolarReportAspectScope::Tight) {
        reportOrbLimit = 3.0;
        aspectScope = "Tight: maximum 3 degrees";
    } else if (solarReportOptions_.aspectScope == SolarReportAspectScope::Standard) {
        reportOrbLimit = 6.0;
        aspectScope = "Standard: maximum 6 degrees";
    }
    QString preset = "Custom";
    if (solarReportOptions_.preset == SolarReportPreset::Basic) {
        preset = "Basic";
    } else if (solarReportOptions_.preset == SolarReportPreset::Full) {
        preset = "Full";
    }
    QString lotScopeText = "None";
    if (solarReportOptions_.lotScope == SolarReportLotScope::Core) {
        lotScopeText = "Core: Fortune, Spirit, Eros";
    } else if (solarReportOptions_.lotScope == SolarReportLotScope::All) {
        lotScopeText = "All calculated lots";
    }

    const auto solarSolar = internalAspects(
        currentSolarChart_, aspectOrbs_, reportOrbLimit, includeMinorBodies, lotScope);
    const auto natalNatal = internalAspects(
        currentChart_, aspectOrbs_, reportOrbLimit, includeMinorBodies, lotScope);
    const auto solarNatal = crossAspects(
        currentSolarChart_, currentChart_, aspectOrbs_, reportOrbLimit,
        includeMinorBodies, lotScope);

    QStringList lines;
    lines.push_back(QString("# Solar Return Report — %1").arg(returnYear));
    lines.push_back("");
    lines.push_back(QString(
        "**Natal chart:** %1  \n"
        "**Exact Solar Return:** %2 (%3)  \n"
        "**Return location:** %4")
        .arg(
            markdownCell(currentInput_.name.isEmpty() ? "Untitled" : currentInput_.name),
            markdownCell(dateTimeText(currentSolarChart_.localDateTime)),
            markdownCell(currentSolarChart_.timezoneLabel),
            markdownCell(currentSolarLocation_.isEmpty() ? QString("-") : currentSolarLocation_)));
    lines.push_back("");
    lines.push_back(
        "> Factual chart-data export. No automated astrological interpretation "
        "has been added.");
    lines.push_back("");

    lines.push_back("## Report Configuration");
    lines.push_back("");
    appendTable(lines, {"Setting", "Value"}, {
        {"Preset", preset},
        {"Aspect scope", aspectScope},
        {"Arabic Lots", lotScopeText},
        {"Minor bodies", includeMinorBodies ? "Included" : "Excluded"},
        {"Fixed stars", solarReportOptions_.includeFixedStars ? "Included" : "Excluded"},
    });
    lines.push_back("");

    lines.push_back("## Calculation Settings");
    lines.push_back("");
    appendTable(lines, {"Setting", "Value"}, {
        {"Zodiac", zodiacName(currentSolarInput_)},
        {"Ayanamsa", currentSolarInput_.zodiacSystem == ZodiacSystem::Sidereal
            ? siderealAyanamsaToString(currentSolarInput_.siderealAyanamsa)
            : "Not applicable"},
        {"Lunar nodes", lunarNodePolicySummary(currentSolarChart_.lunarNodePolicy)},
        {"Natal house system", houseSystemName(currentInput_.houseSystem)},
        {"Solar Return house system", houseSystemName(currentSolarInput_.houseSystem)},
        {"Conjunction orb", orbText(aspectOrbs_.conjunction)},
        {"Sextile orb", orbText(aspectOrbs_.sextile)},
        {"Square orb", orbText(aspectOrbs_.square)},
        {"Trine orb", orbText(aspectOrbs_.trine)},
        {"Opposition orb", orbText(aspectOrbs_.opposition)},
        {"Chart display max orb", aspectDisplayMaxOrb_ > 0.0
            ? orbText(aspectDisplayMaxOrb_)
            : "No additional display filter"},
        {"Report aspect limit", aspectScope},
    });
    lines.push_back("");

    lines.push_back("## Natal Birth Data");
    lines.push_back("");
    appendTable(lines, {"Field", "Value"}, {
        {"Name", currentInput_.name.isEmpty() ? "Untitled" : currentInput_.name},
        {"Gender", genderToString(currentInput_.gender)},
        {"Birth location", currentLocation_.isEmpty() ? "-" : currentLocation_},
        {"Birth time (local)", dateTimeText(currentChart_.localDateTime)},
        {"Birth time (UTC)", dateTimeText(currentChart_.utcDateTime.toUTC())},
        {"Timezone", currentChart_.timezoneLabel},
        {"Latitude", coordinateText(currentInput_.latitude, true)},
        {"Longitude", coordinateText(currentInput_.longitude, false)},
        {"Zodiac", zodiacName(currentInput_)},
        {"House system", houseSystemName(currentInput_.houseSystem)},
        {"Lunar nodes", lunarNodePolicySummary(currentChart_.lunarNodePolicy)},
        {"Sect", currentChart_.isDayChart ? "Day chart" : "Night chart"},
    });
    lines.push_back("");

    lines.push_back("## Solar Return Event");
    lines.push_back("");
    appendTable(lines, {"Field", "Value"}, {
        {"Solar Return year", QString::number(returnYear)},
        {"Exact return (local)", dateTimeText(currentSolarChart_.localDateTime)},
        {"Exact return (UTC)", dateTimeText(currentSolarChart_.utcDateTime.toUTC())},
        {"Timezone", currentSolarChart_.timezoneLabel},
        {"Return location", currentSolarLocation_.isEmpty() ? "-" : currentSolarLocation_},
        {"Latitude", coordinateText(currentSolarInput_.latitude, true)},
        {"Longitude", coordinateText(currentSolarInput_.longitude, false)},
        {"Sect", currentSolarChart_.isDayChart ? "Day chart" : "Night chart"},
        {"Ascendant", positionText(currentSolarChart_.angles.asc)},
        {"Midheaven", positionText(currentSolarChart_.angles.mc)},
        {"Descendant", positionText(currentSolarChart_.angles.desc)},
        {"IC", positionText(currentSolarChart_.angles.ic)},
        {"Vertex", positionText(currentSolarChart_.angles.vertex)},
    });
    lines.push_back("");

    if (solarReportOptions_.includeAnnualProfection) {
        lines.push_back("## Annual Profection");
        lines.push_back("");
        appendTable(lines, {"Field", "Value"}, {
            {"Age at Solar Return", QString::number(age)},
            {"Profected house", QString("House %1").arg(profectedHouse)},
            {"Profected sign", signName(profectedSign)},
            {"Lord of the Year", lordOfYear},
            {"Lord in natal chart", placementText(currentChart_, lordOfYear)},
            {"Lord in Solar Return", placementText(currentSolarChart_, lordOfYear)},
        });
        QVector<ReportAspect> lordAspects;
        for (const auto& aspect : solarSolar) {
            if (aspect.left == lordOfYear || aspect.right == lordOfYear) {
                lordAspects.push_back(aspect);
            }
        }
        lines.push_back("");
        lines.push_back("### Lord of the Year Aspects in the Solar Return");
        lines.push_back("");
        appendAspectTable(lines, lordAspects);
        lines.push_back("");
    }

    lines.push_back("## Natal Angles");
    lines.push_back("");
    appendTable(lines, {"Angle", "Position"}, {
        {"Ascendant", positionText(currentChart_.angles.asc)},
        {"Midheaven", positionText(currentChart_.angles.mc)},
        {"Descendant", positionText(currentChart_.angles.desc)},
        {"IC", positionText(currentChart_.angles.ic)},
        {"Vertex", positionText(currentChart_.angles.vertex)},
    });
    lines.push_back("");

    if (solarReportOptions_.includeNatalPositions) {
        lines.push_back("## Natal Positions");
        lines.push_back("");
        appendTable(lines,
            positionHeaders(solarReportOptions_.includeDailyMotion,
                            solarReportOptions_.includeDignities),
            positionRows(currentChart_, currentInput_, natalPlacidusCusps_,
                         includeMinorBodies, lotScope,
                         solarReportOptions_.includeDailyMotion,
                         solarReportOptions_.includeDignities));
        lines.push_back("");
    }

    if (solarReportOptions_.includeSolarPositions) {
        lines.push_back("## Solar Return Positions");
        lines.push_back("");
        appendTable(lines,
            positionHeaders(solarReportOptions_.includeDailyMotion,
                            solarReportOptions_.includeDignities),
            positionRows(currentSolarChart_, currentSolarInput_, currentSolarChart_.cusps,
                         includeMinorBodies, lotScope,
                         solarReportOptions_.includeDailyMotion,
                         solarReportOptions_.includeDignities));
        lines.push_back("");
    }

    if (solarReportOptions_.includeHouseCusps) {
        lines.push_back("## Natal Houses");
        lines.push_back("");
        appendTable(lines, {"House", "Cusp", "Sign", "Traditional Ruler"},
            houseRows(currentChart_, currentInput_.houseSystem, natalPlacidusCusps_));
        lines.push_back("");
        lines.push_back("## Solar Return Houses");
        lines.push_back("");
        appendTable(lines, {"House", "Cusp", "Sign", "Traditional Ruler"},
            houseRows(currentSolarChart_, currentSolarInput_.houseSystem,
                      currentSolarChart_.cusps));
        lines.push_back("");
    }

    if (solarReportOptions_.includeHouseOverlays) {
        lines.push_back("## Solar Return Bodies and Points in Natal Houses");
        lines.push_back("");
        appendTable(lines,
            {"Solar Return Body or Point", "Solar Return Position", "Natal House"},
            overlayRows(currentSolarChart_, currentChart_, currentInput_.houseSystem,
                        natalPlacidusCusps_, includeMinorBodies, lotScope));
        lines.push_back("");
        lines.push_back("## Natal Bodies and Points in Solar Return Houses");
        lines.push_back("");
        appendTable(lines,
            {"Natal Body or Point", "Natal Position", "Solar Return House"},
            overlayRows(currentChart_, currentSolarChart_, currentSolarInput_.houseSystem,
                        currentSolarChart_.cusps, includeMinorBodies, lotScope));
        lines.push_back("");
    }

    if (solarReportOptions_.includeSolarNatalAspects) {
        lines.push_back("## Solar Return–Natal Aspects");
        lines.push_back("");
        appendAspectTable(lines, solarNatal);
        lines.push_back("");
    }
    if (solarReportOptions_.includeSolarSolarAspects) {
        lines.push_back("## Solar Return–Solar Return Aspects");
        lines.push_back("");
        appendAspectTable(lines, solarSolar);
        lines.push_back("");
    }
    if (solarReportOptions_.includeNatalNatalAspects) {
        lines.push_back("## Natal–Natal Aspects");
        lines.push_back("");
        appendAspectTable(lines, natalNatal);
        lines.push_back("");
    }

    if (solarReportOptions_.includeFixedStars && !currentChart_.fixedStars.isEmpty()) {
        lines.push_back("## Natal Fixed Stars");
        lines.push_back("");
        appendTable(lines, {"Fixed Star", "Position", "House"},
                    fixedStarRows(currentChart_));
        lines.push_back("");
    }
    if (solarReportOptions_.includeFixedStars && !currentSolarChart_.fixedStars.isEmpty()) {
        lines.push_back("## Solar Return Fixed Stars");
        lines.push_back("");
        appendTable(lines, {"Fixed Star", "Position", "House"},
                    fixedStarRows(currentSolarChart_));
        lines.push_back("");
    }

    const QStringList warnings = currentChart_.warnings + currentSolarChart_.warnings;
    lines.push_back("## Calculation Warnings");
    lines.push_back("");
    if (warnings.isEmpty()) {
        lines.push_back("- None.");
    } else {
        QSet<QString> seen;
        for (const auto& warning : warnings) {
            const QString cleaned = warning.trimmed();
            if (cleaned.isEmpty() || seen.contains(cleaned)) {
                continue;
            }
            seen.insert(cleaned);
            lines.push_back(QString("- %1").arg(markdownCell(cleaned)));
        }
    }
    lines.push_back("");
    lines.push_back("---");
    lines.push_back("Generated by DracoVed.");
    return lines.join('\n');
}

}  // namespace dracoved