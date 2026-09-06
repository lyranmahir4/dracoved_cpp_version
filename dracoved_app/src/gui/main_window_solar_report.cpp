#include "main_window.h"
#include "report_markdown.h"

#include "transit_calc_service.h"

#include "../core/formatting.h"

#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace dracoved {

using namespace reportmd;

namespace {

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

    // Under Tajaka the SR chart is sidereal; every SR-vs-natal comparison in
    // this report uses the sidereal natal reference so the two charts stay in
    // one zodiac even when the app-level chart is tropical.
    const bool tajakaCompare = solarChartMethod() == SolarChartMethod::Tajaka
        && hasTajakaNatalChart_;
    const NatalChart& natalRef = tajakaCompare ? currentTajakaNatalChart_ : currentChart_;
    const HouseSystem natalHouses = tajakaCompare ? HouseSystem::WholeSign
                                                  : currentInput_.houseSystem;

    const auto solarSolar = internalAspects(
        currentSolarChart_, aspectOrbs_, reportOrbLimit, includeMinorBodies, lotScope);
    const auto natalNatal = internalAspects(
        natalRef, aspectOrbs_, reportOrbLimit, includeMinorBodies, lotScope);
    const auto solarNatal = crossAspects(
        currentSolarChart_, natalRef, aspectOrbs_, reportOrbLimit,
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

    if (solarChartMethod() == SolarChartMethod::Tajaka) {
        lines.push_back("## Tajaka Varshaphala");
        lines.push_back("");
        appendTable(lines, {"Field", "Value"}, {
            {"Method", "Tajaka (P.V.R. Rao): tropical Sun return, sidereal chart"},
            {"Judging zodiac", zodiacName(currentSolarInput_)},
            {"Chart location", "Natal (birthplace) coordinates"},
            {"Muntha", hasTajakaMuntha_ ? positionText(currentTajakaMuntha_.longitude) : QString("-")},
            {"Muntha sign", hasTajakaMuntha_ ? currentTajakaMuntha_.signName : QString("-")},
            {"Muntha lord", hasTajakaMuntha_ ? currentTajakaMuntha_.lord : QString("-")},
            {"Muntha house", hasTajakaMuntha_
                ? QString("House %1 (%2)")
                    .arg(currentTajakaMuntha_.houseFromAnnualLagna)
                    .arg(currentTajakaMuntha_.houseMeaning)
                : QString("-")},
        });
        lines.push_back("");
        if (currentTajakaLordOfYear_.valid) {
            lines.push_back("### Lord of the Year");
            lines.push_back("");
            QVector<QStringList> candidacyRows;
            candidacyRows.reserve(currentTajakaLordOfYear_.candidacy.size());
            for (const QString& entry : currentTajakaLordOfYear_.candidacy) {
                candidacyRows.push_back({entry});
            }
            appendTable(lines, {"Planet", "Pancha Vargeeya"}, [&]() {
                QVector<QStringList> rows;
                for (const auto& entry : currentTajakaStrengths_.planets) {
                    rows.push_back({
                        entry.planet,
                        QString("%1 (%2)").arg(entry.panchaVargeeya, 0, 'f', 2).arg(entry.panchaRating),
                    });
                }
                return rows;
            }());
            lines.push_back("");
            const QString loyBenefic = currentTajakaLordOfYear_.beneficOnLagna.isEmpty()
                ? QString("None") : currentTajakaLordOfYear_.beneficOnLagna.join(", ");
            appendTable(lines, {"Field", "Value"}, {
                {"Lord of the Year", currentTajakaLordOfYear_.planet},
                {"Selection", currentTajakaLordOfYear_.selectionReason},
                {"Benefic on lagna", loyBenefic},
            });
            lines.push_back("");
        }
        if (currentTajakaStrengths_.valid) {
            lines.push_back("### Tajaka Strengths");
            lines.push_back("");
            QVector<QStringList> strengthRows;
            strengthRows.reserve(currentTajakaStrengths_.planets.size());
            for (const auto& entry : currentTajakaStrengths_.planets) {
                strengthRows.push_back({
                    entry.planet,
                    QString::number(entry.harshaTotal),
                    QString::number(entry.kshetraBala, 'f', 2),
                    QString::number(entry.uchchaBala, 'f', 2),
                    QString::number(entry.haddaBala, 'f', 2),
                    QString::number(entry.drekkanaBala, 'f', 2),
                    QString::number(entry.navamsaBala, 'f', 2),
                    QString("%1 (%2)").arg(entry.panchaVargeeya, 0, 'f', 2).arg(entry.panchaRating),
                    QString("%1 (S%2/W%3)").arg(entry.dwadashaVargeeya)
                        .arg(entry.dwadashaStrong).arg(entry.dwadashaWeak),
                });
            }
            appendTable(lines,
                {"Planet", "Harsha", "Kshetra", "Uchcha", "Hadda", "Drekkana",
                 "Navamsa", "Pancha Vargeeya", "Dwadasha"},
                strengthRows);
            lines.push_back("");
        }
        lines.push_back("### Tajaka Aspects");
        lines.push_back("");
        QVector<QStringList> tajakaRows;
        tajakaRows.reserve(currentTajakaAspects_.size());
        for (const auto& aspect : currentTajakaAspects_) {
            QStringList motion;
            if (aspect.ithasala) {
                motion.push_back(aspect.poorna ? "Poorna ithasala" : "Ithasala");
            } else if (aspect.eesarpha) {
                motion.push_back("Eesarpha");
            }
            if (aspect.firstRetrograde) {
                motion.push_back(QString("%1 R").arg(aspect.firstName));
            } else if (aspect.secondRetrograde) {
                motion.push_back(QString("%1 R").arg(aspect.secondName));
            }
            tajakaRows.push_back({
                aspect.firstName,
                tajaka::tajakaAspectKindLabel(aspect.kind),
                aspect.secondName,
                orbText(aspect.orb),
                tajaka::tajakaAspectNatureLabel(aspect.nature),
                motion.join(", "),
            });
        }
        appendTable(lines,
            {"Point A", "Aspect", "Point B", "Orb", "Nature", "Motion"},
            tajakaRows);
        lines.push_back("");
    }

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
        {"Ascendant", positionText(natalRef.angles.asc)},
        {"Midheaven", positionText(natalRef.angles.mc)},
        {"Descendant", positionText(natalRef.angles.desc)},
        {"IC", positionText(natalRef.angles.ic)},
        {"Vertex", positionText(natalRef.angles.vertex)},
    });
    lines.push_back("");

    if (solarReportOptions_.includeNatalPositions) {
        lines.push_back("## Natal Positions");
        lines.push_back("");
        NatalInput natalInputRef = currentInput_;
        natalInputRef.houseSystem = natalHouses;
        appendTable(lines,
            positionHeaders(solarReportOptions_.includeDailyMotion,
                            solarReportOptions_.includeDignities),
            positionRows(natalRef, natalInputRef,
                         tajakaCompare ? natalRef.cusps : natalPlacidusCusps_,
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
            houseRows(natalRef, natalHouses,
                      tajakaCompare ? natalRef.cusps : natalPlacidusCusps_));
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
            overlayRows(currentSolarChart_, natalRef, natalHouses,
                        tajakaCompare ? natalRef.cusps : natalPlacidusCusps_,
                        includeMinorBodies, lotScope));
        lines.push_back("");
        lines.push_back("## Natal Bodies and Points in Solar Return Houses");
        lines.push_back("");
        appendTable(lines,
            {"Natal Body or Point", "Natal Position", "Solar Return House"},
            overlayRows(natalRef, currentSolarChart_, currentSolarInput_.houseSystem,
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
