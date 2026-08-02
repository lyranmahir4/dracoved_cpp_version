#include "main_window.h"

#include "return_finder_controller.h"
#include "return_finder_types.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabBar>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <cmath>

namespace dracoved {

namespace {

QTableWidgetItem* finderCell(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return item;
}

void configureFinderTable(QTableWidget* table, const QStringList& headers, int rows) {
    if (!table) return;
    table->clear();
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setRowCount(rows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);
    if (auto* header = table->horizontalHeader()) {
        for (int column = 0; column < headers.size(); ++column) {
            header->setSectionResizeMode(column, column == headers.size() - 1
                ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
        }
    }
}

QString houseModeText(ReturnFinderHouseMode mode) {
    switch (mode) {
        case ReturnFinderHouseMode::Placidus: return "Placidus";
        case ReturnFinderHouseMode::BothOr: return "Both (OR)";
        case ReturnFinderHouseMode::BothAnd: return "Both (AND)";
        case ReturnFinderHouseMode::WholeSign:
        default: return "Whole Sign";
    }
}

QString conditionTypeText(ReturnFinderConditionType type) {
    switch (type) {
        case ReturnFinderConditionType::Aspect: return "Aspect";
        case ReturnFinderConditionType::HouseLordPlacement: return "House Lord Placement";
        case ReturnFinderConditionType::ProfectionLordPlacement: return "Profection Lord Placement";
        case ReturnFinderConditionType::Stellium: return "Stellium";
        case ReturnFinderConditionType::PlanetPlacement:
        default: return "Planet Placement";
    }
}

HouseSystem returnFinderDisplayHouseSystem(ReturnFinderHouseMode mode) {
    return mode == ReturnFinderHouseMode::WholeSign
        ? HouseSystem::WholeSign : HouseSystem::Placidus;
}

}  // namespace

void MainWindow::refreshReturnFinderDocks() {
    if (activeTab_ != AppTab::ReturnFinder || !rightTopTable_ || !rightBottomTable_) return;
    if (rightTopDock_) rightTopDock_->setWindowTitle("Return Finder Details");
    if (rightBottomDock_) rightBottomDock_->setWindowTitle("Search Summary");
    if (!returnFinderController_) {
        configureFinderTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, finderCell("Return Finder is unavailable."));
        configureFinderTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, finderCell("No search summary."));
        return;
    }
    const ReturnFinderQuery query = returnFinderController_->lastQuery();

    if (!returnFinderController_->hasSelectedResult()) {
        configureFinderTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, finderCell(hasCurrentChart_
            ? "Run a search and select a matching return."
            : "Load a natal chart to use Return Finder."));
    } else {
        const ReturnFinderResult result = returnFinderController_->selectedResult();
        configureFinderTable(rightTopTable_, {"Status", "Condition", "Explanation", "Orb"},
                             result.evaluations.size());
        for (int row = 0; row < result.evaluations.size(); ++row) {
            const auto& evaluation = result.evaluations[row];
            QString status;
            if (evaluation.excluded) {
                status = evaluation.matched ? "Excluded" : "Not triggered";
            } else {
                status = evaluation.matched ? "Matched" : "Not matched";
            }
            QString conditionLabel = evaluation.conditionId;
            for (int index = 0; index < query.conditions.size(); ++index) {
                if (query.conditions[index].id == evaluation.conditionId) {
                    conditionLabel = QString("%1. %2")
                        .arg(index + 1)
                        .arg(conditionTypeText(query.conditions[index].type));
                    break;
                }
            }
            rightTopTable_->setItem(row, 0, finderCell(status));
            auto* conditionItem = finderCell(conditionLabel);
            conditionItem->setToolTip(QString("Internal condition ID: %1").arg(evaluation.conditionId));
            rightTopTable_->setItem(row, 1, conditionItem);
            rightTopTable_->setItem(row, 2, finderCell(evaluation.description));
            rightTopTable_->setItem(row, 3, finderCell(
                std::isfinite(evaluation.orb) && evaluation.orb >= 0.0
                    ? QString("%1 deg").arg(evaluation.orb, 0, 'f', 2)
                    : QString("N/A")));
        }
        if (auto* header = rightTopTable_->horizontalHeader()) {
            header->setSectionResizeMode(2, QHeaderView::Stretch);
        }
    }

    const ReturnFinderRunSummary summary = returnFinderController_->runSummary();
    QString range = "-";
    if (query.returnType == ReturnFinderType::Solar && query.startYear > 0 && query.endYear > 0) {
        range = QString("%1 - %2").arg(query.startYear).arg(query.endYear);
    } else if (query.startDate.isValid() && query.endDate.isValid()) {
        range = QString("%1 - %2")
            .arg(query.startDate.toString("d MMM yyyy"),
                 query.endDate.toString("d MMM yyyy"));
    }
    QString runNotes = summary.warnings.mid(0, 5).join(" | ");
    if (summary.warnings.size() > 5) {
        runNotes += QString(" (+%1 more)").arg(summary.warnings.size() - 5);
    }
    const QString state = returnFinderController_->isRunning() ? "Running"
        : (!returnFinderController_->hasCompletedRun() ? "Not completed"
            : (returnFinderController_->resultsAreStale() ? "Stale"
                : (summary.cancelled ? "Partial (cancelled)"
                    : (summary.capped ? "Capped" : "Current"))));
    QVector<QPair<QString, QString>> rows = {
        {"Return Type", returnFinderTypeLabel(query.returnType)},
        {"Range", range},
        {"Location", query.locationName.isEmpty() ? "-" : query.locationName},
        {"Timezone", query.timezone.isEmpty() ? "-" : query.timezone},
        {"House System", houseModeText(query.houseMode)},
        {"Rulership", query.modernRulership ? "Modern" : "Traditional"},
        {"Scanned", QString::number(summary.scanned)},
        {"Matched", QString::number(summary.matched)},
        {"Failed", QString::number(summary.failed)},
        {"Result State", state},
    };
    if (!runNotes.isEmpty()) {
        rows.push_back({"Run Notes", runNotes});
    }
    configureFinderTable(rightBottomTable_, {"Item", "Value"}, rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        rightBottomTable_->setItem(row, 0, finderCell(rows[row].first));
        auto* valueItem = finderCell(rows[row].second);
        if (rows[row].first == "Run Notes") {
            valueItem->setToolTip(QString("Non-fatal candidate calculation issues:\n%1")
                .arg(summary.warnings.join('\n')));
        }
        rightBottomTable_->setItem(row, 1, valueItem);
    }
    if (auto* header = rightBottomTable_->horizontalHeader()) {
        header->setSectionResizeMode(1, QHeaderView::Stretch);
    }
}

void MainWindow::handleReturnFinderOpen(const ReturnFinderResult& result,
                                        const ReturnFinderQuery& query) {
    if (!hasCurrentChart_) {
        setStatusMessage("Load the natal chart used by this search before opening a return.");
        return;
    }
    auto switchTo = [this](AppTab tab) {
        for (int i = 0; mainTabBar_ && i < mainTabBar_->count(); ++i) {
            if (mainTabBar_->tabData(i).toInt() == static_cast<int>(tab)) {
                mainTabBar_->setCurrentIndex(i);
                return;
            }
        }
    };
    auto computeExactReturn = [this, &result, &query](const QString& name,
                                                      NatalChart* chart,
                                                      NatalInput* input,
                                                      QString* error) {
        if (!chart || !input || !result.localDateTime.isValid()
            || !result.utcDateTime.isValid()) {
            if (error) *error = "The selected Return Finder result has an invalid timestamp.";
            return false;
        }
        if (!swe_.isLoaded()) {
            if (error) *error = "Swiss Ephemeris is not loaded.";
            return false;
        }
        if (ephePath_.isEmpty()) {
            if (error) *error = "Ephemeris folder is not available.";
            return false;
        }
        swe_.setEphePath(ephePath_);
        NatalInput exactInput = currentInput_;
        exactInput.name = name;
        exactInput.date = result.localDateTime.date();
        exactInput.time = result.localDateTime.time();
        exactInput.timezone = query.timezone;
        exactInput.latitude = query.latitude;
        exactInput.longitude = query.longitude;
        exactInput.houseSystem = returnFinderDisplayHouseSystem(query.houseMode);
        exactInput.aspectOrbs = aspectOrbs_;
        if (!engine_.compute(exactInput, chart, error)) {
            return false;
        }
        if (!chart->utcDateTime.isValid()
            || qAbs(chart->utcDateTime.secsTo(result.utcDateTime)) > 1) {
            if (error) {
                *error = "The selected return timestamp could not be reproduced exactly; the current chart was left unchanged.";
            }
            return false;
        }
        *input = exactInput;
        return true;
    };

    if (result.returnType == ReturnFinderType::Solar) {
        NatalChart exactChart;
        NatalInput exactInput;
        QString error;
        if (!computeExactReturn(QString("Solar Return %1").arg(result.year),
                                &exactChart, &exactInput, &error)) {
            setStatusMessage(error);
            return;
        }
        if (solarYearSpin_) solarYearSpin_->setValue(result.year);
        if (solarTimezoneEdit_) solarTimezoneEdit_->setText(query.timezone);
        if (query.useNatalLocation) {
            if (solarUseNatalRadio_) solarUseNatalRadio_->setChecked(true);
        } else {
            if (solarUseCustomRadio_) solarUseCustomRadio_->setChecked(true);
            if (solarLocationEdit_) solarLocationEdit_->setText(query.locationName);
            if (solarLatSpin_) solarLatSpin_->setValue(query.latitude);
            if (solarLonSpin_) solarLonSpin_->setValue(query.longitude);
        }
        switchTo(AppTab::SolarReturn);
        currentSolarChart_ = exactChart;
        currentSolarInput_ = exactInput;
        currentSolarLocation_ = query.locationName;
        hasSolarChart_ = true;
        solarPending_ = false;
        lastSolarCalculated_ = QDateTime::currentDateTime();
        updateSolarStatusLabels();
        refreshSolarReturnView();
        refreshSolarTechniqueView();
        refreshSolarPlacementFinderView();
        setStatusMessage(QString("Opened Solar Return %1 from Return Finder.").arg(result.year));
        return;
    }

    NatalChart exactChart;
    NatalInput exactInput;
    QString error;
    if (!computeExactReturn(
            QString("Lunar Return %1").arg(result.localDateTime.toString("yyyy-MM-dd")),
            &exactChart, &exactInput, &error)) {
        setStatusMessage(error);
        return;
    }
    if (lunarAnchorDateEdit_) lunarAnchorDateEdit_->setDate(result.localDateTime.date());
    if (lunarTimezoneEdit_) lunarTimezoneEdit_->setText(query.timezone);
    if (query.useNatalLocation) {
        if (lunarUseNatalRadio_) lunarUseNatalRadio_->setChecked(true);
    } else {
        if (lunarUseCustomRadio_) lunarUseCustomRadio_->setChecked(true);
        if (lunarLocationEdit_) lunarLocationEdit_->setText(query.locationName);
        if (lunarLatSpin_) lunarLatSpin_->setValue(query.latitude);
        if (lunarLonSpin_) lunarLonSpin_->setValue(query.longitude);
    }
    switchTo(AppTab::LunarReturn);
    currentLunarChart_ = exactChart;
    currentLunarInput_ = exactInput;
    currentLunarLocation_ = query.locationName;
    currentLunarReturnUtc_ = result.utcDateTime;
    hasLunarChart_ = true;
    lunarPending_ = false;
    lastLunarCalculated_ = QDateTime::currentDateTime();
    if (lunarAnchorDateEdit_) {
        const QSignalBlocker blocker(lunarAnchorDateEdit_);
        lunarAnchorDateEdit_->setDate(result.localDateTime.date());
    }
    updateLunarStatusLabels();
    refreshLunarReturnView();
    setStatusMessage(QString("Opened Lunar Return %1 from Return Finder.")
                         .arg(result.localDateTime.toString("d MMM yyyy, h:mm AP")));
}

}  // namespace dracoved
