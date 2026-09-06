#include "main_window.h"
#include "report_markdown.h"

#include "transit_calc_service.h"

#include "../core/formatting.h"

#include <QStringList>
#include <QToolButton>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace dracoved {

using namespace reportmd;

namespace {

const QStringList& balancePlanets() {
    static const QStringList planets = {
        "Sun", "Moon", "Mercury", "Venus", "Mars",
        "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto",
    };
    return planets;
}

void appendBalanceSummary(QStringList& lines, const NatalChart& chart) {
    int fire = 0, earth = 0, air = 0, water = 0;
    int cardinal = 0, fixed = 0, mutableCount = 0;
    int counted = 0;
    for (const auto& name : balancePlanets()) {
        const auto* body = findBody(chart, name);
        if (!body) {
            continue;
        }
        QString sign = body->signName;
        if (sign.isEmpty()) {
            sign = signName(signIndex(body->longitude));
        }
        const QString element = elementForSign(sign);
        if (element == "Fire") {
            ++fire;
        } else if (element == "Earth") {
            ++earth;
        } else if (element == "Air") {
            ++air;
        } else if (element == "Water") {
            ++water;
        }
        const QString mode = modeForSign(sign);
        if (mode == "Cardinal") {
            ++cardinal;
        } else if (mode == "Fixed") {
            ++fixed;
        } else if (mode == "Mutable") {
            ++mutableCount;
        }
        ++counted;
    }
    lines.push_back(QString(
        "- Planets counted: %1 of 10 (Sun through Pluto)")
        .arg(counted));
    lines.push_back(QString(
        "- Elements: Fire %1, Earth %2, Air %3, Water %4")
        .arg(fire).arg(earth).arg(air).arg(water));
    lines.push_back(QString(
        "- Modes: Cardinal %1, Fixed %2, Mutable %3")
        .arg(cardinal).arg(fixed).arg(mutableCount));
    lines.push_back(QString(
        "- Polarity: Active (Fire/Air) %1, Passive (Earth/Water) %2")
        .arg(fire + air).arg(earth + water));
}

}  // namespace

QString MainWindow::buildNatalReportMarkdown() const {
    if (!hasCurrentChart_) {
        return QString();
    }

    const NatalChart* chartPtr = &currentChart_;
    const NatalInput* inputPtr = &currentInput_;
    QString reportTitle = "Natal Report";
    QString localTimeLabel = "Birth time (local)";
    QString utcTimeLabel = "Birth time (UTC)";
    QString locationLabel = "Birth location";
    QString modeContext;
    QVector<HouseCusp> placidusCusps = natalPlacidusCusps_;
    bool usingProgressedChart = false;

    if (activeTab_ == AppTab::Progression) {
        reportTitle = "Progression Report";
        localTimeLabel = "Chart time (local)";
        utcTimeLabel = "Chart time (UTC)";
        locationLabel = "Location";
        if (progressionView_ != ProgressionView::NatalOnly && hasProgressionChart_) {
            chartPtr = &currentProgressionChart_;
            inputPtr = &currentProgressionInput_;
            usingProgressedChart = true;
            modeContext = (progressionView_ == ProgressionView::Overlay)
                ? "Progressed (overlay mode)"
                : "Progressed";
            placidusCusps = currentProgressionChart_.cusps;
        } else {
            modeContext = "Natal (progression natal-only)";
        }
    }

    const NatalChart& chart = *chartPtr;
    const NatalInput& input = *inputPtr;
    const QString chartName = input.name.isEmpty() ? QString("Untitled") : input.name;

    const bool includeMinorBodies = natalReportOptions_.includeMinorBodies;
    const int lotScope = static_cast<int>(natalReportOptions_.lotScope);
    auto lotScopeText = [](NatalReportLotScope scope) {
        switch (scope) {
            case NatalReportLotScope::Core:
                return QString("Core: Fortune, Spirit, Eros");
            case NatalReportLotScope::All:
                return QString("All calculated lots");
            case NatalReportLotScope::None:
            default:
                return QString("None");
        }
    };
    double reportOrbLimit = 0.0;
    auto aspectScopeTextFor = [&reportOrbLimit](NatalReportAspectScope scope) {
        switch (scope) {
            case NatalReportAspectScope::Standard:
                reportOrbLimit = 6.0;
                return QString("Standard: maximum 6 degrees");
            case NatalReportAspectScope::Configured:
                reportOrbLimit = 0.0;
                return QString("Configured per-aspect orbs");
            case NatalReportAspectScope::Tight:
            default:
                reportOrbLimit = 3.0;
                return QString("Tight: maximum 3 degrees");
        }
    };
    const QString aspectScopeText = aspectScopeTextFor(natalReportOptions_.aspectScope);
    QString preset = "Custom";
    if (natalReportOptions_.preset == NatalReportPreset::Basic) {
        preset = "Basic";
    } else if (natalReportOptions_.preset == NatalReportPreset::Full) {
        preset = "Full";
    }

    // Basic always shows the house table for the chart's active house system
    // plus the Placidus cusp table, even if the persisted toggle state
    // predates the current chart.
    const bool basicPreset = natalReportOptions_.preset == NatalReportPreset::Basic;
    const bool showWholeSignHouses = basicPreset
        ? input.houseSystem != HouseSystem::Placidus
        : natalReportOptions_.includeWholeSignHouses;
    const bool showPlacidusCusps = basicPreset
        ? true
        : natalReportOptions_.includePlacidusCusps;
    QStringList houseTableNames;
    if (showWholeSignHouses) {
        houseTableNames.push_back("Whole Sign");
    }
    if (showPlacidusCusps) {
        houseTableNames.push_back("Placidus");
    }
    const QString houseTablesText = houseTableNames.isEmpty()
        ? QString("None")
        : houseTableNames.join(" + ");

    const auto chartAspects = internalAspects(
        chart, aspectOrbs_, reportOrbLimit, includeMinorBodies, lotScope);

    const QString positionsHeading = usingProgressedChart
        ? QString("Progressed Positions")
        : QString("Natal Positions");

    QStringList lines;
    lines.push_back(QString("# %1 — %2").arg(reportTitle, chartName));
    lines.push_back("");
    lines.push_back(QString(
        "**Name:** %1  \n"
        "**%2:** %3 (%4)  \n"
        "**%5:** %6")
        .arg(
            markdownCell(chartName),
            markdownCell(localTimeLabel),
            markdownCell(dateTimeText(chart.localDateTime)),
            markdownCell(chart.timezoneLabel),
            markdownCell(locationLabel),
            markdownCell(currentLocation_.isEmpty() ? QString("-") : currentLocation_)));
    lines.push_back("");
    lines.push_back(
        "> Factual chart-data export. No automated astrological interpretation "
        "has been added.");
    lines.push_back("");

    lines.push_back("## Report Configuration");
    lines.push_back("");
    appendTable(lines, {"Setting", "Value"}, {
        {"Preset", preset},
        {"Aspect scope", aspectScopeText},
        {"Conjunction orb", orbText(aspectOrbs_.conjunction)},
        {"Sextile orb", orbText(aspectOrbs_.sextile)},
        {"Square orb", orbText(aspectOrbs_.square)},
        {"Trine orb", orbText(aspectOrbs_.trine)},
        {"Opposition orb", orbText(aspectOrbs_.opposition)},
        {"Arabic Lots", lotScopeText(natalReportOptions_.lotScope)},
        {"Minor bodies", includeMinorBodies ? "Included" : "Excluded"},
        {"Fixed stars", natalReportOptions_.includeFixedStars ? "Included" : "Excluded"},
        {"House tables", houseTablesText},
    });
    lines.push_back("");

    lines.push_back("## Chart Data");
    lines.push_back("");
    QVector<QStringList> chartDataRows = {
        {"Name", chartName},
        {"Gender", genderToString(input.gender)},
        {locationLabel, currentLocation_.isEmpty() ? "-" : currentLocation_},
        {localTimeLabel, dateTimeText(chart.localDateTime)},
        {utcTimeLabel, dateTimeText(chart.utcDateTime.toUTC())},
        {"Timezone", chart.timezoneLabel},
        {"Latitude", coordinateText(input.latitude, true)},
        {"Longitude", coordinateText(input.longitude, false)},
        {"Zodiac", zodiacName(input)},
        {"Ayanamsa", input.zodiacSystem == ZodiacSystem::Sidereal
            ? siderealAyanamsaToString(input.siderealAyanamsa)
            : "Not applicable"},
        {"House system", houseSystemName(input.houseSystem)},
        {"Lunar nodes", lunarNodePolicySummary(chart.lunarNodePolicy)},
        {"Sect", chart.isDayChart ? "Day chart" : "Night chart"},
    };
    if (!modeContext.isEmpty()) {
        chartDataRows.push_back({"Mode context", modeContext});
    }
    if (activeTab_ == AppTab::Progression) {
        chartDataRows.push_back(
            {"Natal birth (local)", dateTimeText(currentChart_.localDateTime)});
        chartDataRows.push_back({"Age at chart time", QString::number(completedYears(
            currentChart_.localDateTime.date(), chart.localDateTime.date()))});
    }
    appendTable(lines, {"Field", "Value"}, chartDataRows);
    lines.push_back("");

    lines.push_back(usingProgressedChart ? "## Progressed Angles" : "## Natal Angles");
    lines.push_back("");
    appendTable(lines, {"Angle", "Position"}, {
        {"Ascendant", positionText(chart.angles.asc)},
        {"Midheaven", positionText(chart.angles.mc)},
        {"Descendant", positionText(chart.angles.desc)},
        {"IC", positionText(chart.angles.ic)},
        {"Vertex", positionText(chart.angles.vertex)},
    });
    lines.push_back("");

    lines.push_back(QString("## %1").arg(positionsHeading));
    lines.push_back("");
    appendTable(lines,
        positionHeaders(natalReportOptions_.includeDailyMotion,
                        natalReportOptions_.includeDignities),
        positionRows(chart, input, placidusCusps,
                     includeMinorBodies, lotScope,
                     natalReportOptions_.includeDailyMotion,
                     natalReportOptions_.includeDignities));
    lines.push_back("");

    lines.push_back("## Balance Summary");
    lines.push_back("");
    appendBalanceSummary(lines, chart);
    lines.push_back("");

    lines.push_back(usingProgressedChart ? "## Progressed Aspects" : "## Natal Aspects");
    lines.push_back("");
    appendAspectTable(lines, chartAspects);
    lines.push_back("");

    if (showWholeSignHouses) {
        lines.push_back("## Houses (Whole Sign)");
        lines.push_back("");
        appendTable(lines, {"House", "Cusp", "Sign", "Traditional Ruler"},
            houseRows(chart, HouseSystem::WholeSign, placidusCusps));
        lines.push_back("");
    }
    if (showPlacidusCusps) {
        lines.push_back("## Houses (Placidus Cusps)");
        lines.push_back("");
        if (placidusCusps.size() == 12) {
            appendTable(lines, {"House", "Cusp", "Sign", "Traditional Ruler"},
                houseRows(chart, HouseSystem::Placidus, placidusCusps));
        } else {
            lines.push_back("Placidus cusps are not available for this chart.");
        }
        lines.push_back("");
    }

    if (natalReportOptions_.includeFixedStars && !chart.fixedStars.isEmpty()) {
        lines.push_back("## Fixed Stars");
        lines.push_back("");
        appendTable(lines, {"Fixed Star", "Position", "House"},
                    fixedStarRows(chart));
        lines.push_back("");
    }

    lines.push_back("## Calculation Warnings");
    lines.push_back("");
    if (chart.warnings.isEmpty()) {
        lines.push_back("- None.");
    } else {
        for (const auto& warning : chart.warnings) {
            const QString cleaned = warning.trimmed();
            if (cleaned.isEmpty()) {
                continue;
            }
            lines.push_back(QString("- %1").arg(markdownCell(cleaned)));
        }
    }
    lines.push_back("");
    lines.push_back("---");
    lines.push_back("Generated by DracoVed.");
    return lines.join('\n');
}

void MainWindow::applyNatalReportBasicPreset() {
    natalReportOptions_ = NatalReportOptions{};
    const bool placidus = currentInput_.houseSystem == HouseSystem::Placidus;
    natalReportOptions_.includeWholeSignHouses = !placidus;
    natalReportOptions_.includePlacidusCusps = true;
    updateNatalReportOptionsUi();
    refreshNatalReport();
    setStatusMessage("Natal report set to Basic.");
}

void MainWindow::applyNatalReportFullPreset() {
    NatalReportOptions options;
    options.preset = NatalReportPreset::Full;
    options.lotScope = NatalReportLotScope::All;
    options.aspectScope = NatalReportAspectScope::Configured;
    options.includeMinorBodies = true;
    options.includeDailyMotion = true;
    options.includeDignities = true;
    options.includeFixedStars = true;
    options.includeWholeSignHouses = true;
    options.includePlacidusCusps = true;
    natalReportOptions_ = options;
    updateNatalReportOptionsUi();
    refreshNatalReport();
    setStatusMessage("Natal report set to Full.");
}

void MainWindow::markNatalReportOptionsCustom() {
    natalReportOptions_.preset = NatalReportPreset::Custom;
    updateNatalReportOptionsUi();
    refreshNatalReport();
    setStatusMessage("Natal report options updated.");
}

void MainWindow::updateNatalReportOptionsUi() {
    if (natalReportOptionsButton_) {
        QString label = "Custom";
        if (natalReportOptions_.preset == NatalReportPreset::Basic) {
            label = "Basic";
        } else if (natalReportOptions_.preset == NatalReportPreset::Full) {
            label = "Full";
        }
        natalReportOptionsButton_->setText(QString("Report: %1").arg(label));
    }
    if (natalReportMinorBodiesAction_) {
        natalReportMinorBodiesAction_->setChecked(natalReportOptions_.includeMinorBodies);
    }
    if (natalReportDailyMotionAction_) {
        natalReportDailyMotionAction_->setChecked(natalReportOptions_.includeDailyMotion);
    }
    if (natalReportDignitiesAction_) {
        natalReportDignitiesAction_->setChecked(natalReportOptions_.includeDignities);
    }
    if (natalReportFixedStarsAction_) {
        natalReportFixedStarsAction_->setChecked(natalReportOptions_.includeFixedStars);
    }
    if (natalReportWholeSignHousesAction_) {
        natalReportWholeSignHousesAction_->setChecked(
            natalReportOptions_.includeWholeSignHouses);
    }
    if (natalReportPlacidusCuspsAction_) {
        natalReportPlacidusCuspsAction_->setChecked(
            natalReportOptions_.includePlacidusCusps);
    }
    if (natalReportNoLotsAction_) {
        natalReportNoLotsAction_->setChecked(
            natalReportOptions_.lotScope == NatalReportLotScope::None);
    }
    if (natalReportCoreLotsAction_) {
        natalReportCoreLotsAction_->setChecked(
            natalReportOptions_.lotScope == NatalReportLotScope::Core);
    }
    if (natalReportAllLotsAction_) {
        natalReportAllLotsAction_->setChecked(
            natalReportOptions_.lotScope == NatalReportLotScope::All);
    }
    if (natalReportTightAspectsAction_) {
        natalReportTightAspectsAction_->setChecked(
            natalReportOptions_.aspectScope == NatalReportAspectScope::Tight);
    }
    if (natalReportStandardAspectsAction_) {
        natalReportStandardAspectsAction_->setChecked(
            natalReportOptions_.aspectScope == NatalReportAspectScope::Standard);
    }
    if (natalReportConfiguredAspectsAction_) {
        natalReportConfiguredAspectsAction_->setChecked(
            natalReportOptions_.aspectScope == NatalReportAspectScope::Configured);
    }
}

}  // namespace dracoved
