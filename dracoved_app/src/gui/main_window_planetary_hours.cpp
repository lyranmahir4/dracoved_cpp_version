#include "main_window.h"

#include "planetary_hours_controller.h"

#include <QAbstractItemView>
#include <QDockWidget>
#include <QFont>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <algorithm>

namespace dracoved {

namespace {

QTableWidgetItem* planetaryDetailCell(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
    return item;
}

void configurePlanetaryDetailTable(QTableWidget* table,
                                   const QStringList& headers,
                                   int rows) {
    if (!table) return;
    table->setSortingEnabled(false);
    table->clearSelection();
    table->clear();
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setRowCount(std::max(0, rows));
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setWordWrap(true);
    table->setTextElideMode(Qt::ElideNone);
    table->setShowGrid(true);
    if (auto* header = table->horizontalHeader()) {
        header->setStretchLastSection(false);
        for (int column = 0; column < headers.size(); ++column) {
            header->setSectionResizeMode(column,
                column == headers.size() - 1 ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
        }
    }
}

QString detailDuration(qint64 milliseconds) {
    const qint64 totalSeconds = std::max<qint64>(0, milliseconds / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QString("%1h %2m %3s")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }
    return QString("%1m %2s").arg(minutes).arg(seconds, 2, 10, QChar('0'));
}

QString coordinateText(double latitude, double longitude) {
    return QString("%1, %2")
        .arg(latitude, 0, 'f', 6)
        .arg(longitude, 0, 'f', 6);
}

QString adjacentRuler(const QString& ruler, int direction) {
    static const QStringList chaldeanOrder = {
        "Saturn", "Jupiter", "Mars", "Sun", "Venus", "Mercury", "Moon",
    };
    const int index = chaldeanOrder.indexOf(ruler);
    if (index < 0) return "-";
    return chaldeanOrder[(index + direction + chaldeanOrder.size()) % chaldeanOrder.size()];
}

QString relationToReference(const PlanetaryHoursResult& result,
                            const PlanetaryHourInterval& selected,
                            int selectedIndex,
                            bool live) {
    if (selectedIndex == result.currentIndex) {
        return live ? "Live current hour" : "Contains the selected snapshot time";
    }
    if (selected.endLocal <= result.momentLocal) {
        return live ? "Past relative to now" : "Earlier than the selected snapshot";
    }
    return live ? "Upcoming relative to now" : "Later than the selected snapshot";
}

QString pressureText(const PlanetaryHoursCalculationOptions& options) {
    return options.pressureHPa > 0.0
        ? QString("%1 hPa").arg(options.pressureHPa, 0, 'f', 1)
        : QString("Auto (estimated from elevation)");
}

void fillDetailRows(QTableWidget* table,
                    const QVector<QPair<QString, QString>>& rows) {
    configurePlanetaryDetailTable(table, {"Item", "Value"}, rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        table->setItem(row, 0, planetaryDetailCell(rows[row].first));
        table->setItem(row, 1, planetaryDetailCell(rows[row].second));
    }
    table->resizeRowsToContents();
}

}  // namespace

void MainWindow::refreshPlanetaryHoursDocks() {
    if (activeTab_ != AppTab::PlanetaryHours || !rightTopTable_ || !rightBottomTable_) return;
    if (rightTopDock_) rightTopDock_->setWindowTitle("Selected Planetary Hour");
    if (rightBottomDock_) rightBottomDock_->setWindowTitle("Planetary Day Summary");

    if (!planetaryHoursController_ || !planetaryHoursController_->hasResult()) {
        configurePlanetaryDetailTable(rightTopTable_, {"Info"}, 1);
        QString message = "Choose a location to calculate planetary hours.";
        if (planetaryHoursController_ && !planetaryHoursController_->result().error.isEmpty()) {
            message = planetaryHoursController_->result().error;
        }
        rightTopTable_->setItem(0, 0, planetaryDetailCell(message));
        rightTopTable_->resizeRowsToContents();
        configurePlanetaryDetailTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, planetaryDetailCell(
            "Sunrise, sunset, calculation assumptions and temporal-hour lengths will appear here."));
        rightBottomTable_->resizeRowsToContents();
        return;
    }

    const PlanetaryHoursResult& result = planetaryHoursController_->result();
    const bool live = planetaryHoursController_->isLiveMode();
    const int selectedIndex = planetaryHoursController_->selectedHourIndex();
    const PlanetaryHourInterval selected = planetaryHoursController_->selectedHour();
    if (selectedIndex < 0 || selectedIndex >= result.hours.size()) return;

    const QString period = selected.daytime
        ? QString("Day hour %1").arg(selected.periodHour)
        : QString("Night hour %1").arg(selected.periodHour);
    const QVector<QPair<QString, QString>> detailRows = {
        {"Relation to Reference", relationToReference(result, selected, selectedIndex, live)},
        {"Ruler", selected.ruler},
        {"Planetary Hour", QString("%1 (#%2 of 24)").arg(period).arg(selected.sequence)},
        {"Starts", selected.startLocal.toString("dddd, d MMMM yyyy h:mm:ss AP")},
        {"Ends", selected.endLocal.toString("dddd, d MMMM yyyy h:mm:ss AP")},
        {"Duration", detailDuration(selected.startLocal.msecsTo(selected.endLocal))},
        {"Previous Ruler", adjacentRuler(selected.ruler, -1)},
        {"Next Ruler", adjacentRuler(selected.ruler, 1)},
        {"Planetary Day Ruler", result.dayRuler},
        {"Interpretation", planetaryRulerMeaning(selected.ruler)},
    };
    fillDetailRows(rightTopTable_, detailRows);

    const qint64 dayLength = result.sunrise.msecsTo(result.sunset);
    const qint64 nightLength = result.sunset.msecsTo(result.nextSunrise);
    const QVector<QPair<QString, QString>> summaryRows = {
        {"Location", planetaryHoursController_->locationName()},
        {"Coordinates", coordinateText(planetaryHoursController_->latitude(),
                                         planetaryHoursController_->longitude())},
        {"Timezone", planetaryHoursController_->timezoneLabel()},
        {"Reference Mode", live ? "Live current time" : "Fixed date/time snapshot"},
        {"Reference Moment", result.momentLocal.toString("dddd, d MMMM yyyy h:mm:ss AP")},
        {"Planetary Date", result.planetaryDate.toString("dddd, d MMMM yyyy")},
        {"Planetary Day Ruler", result.dayRuler},
        {"Sunrise", result.sunrise.toString("dddd, d MMM h:mm:ss AP")},
        {"Sunset", result.sunset.toString("dddd, d MMM h:mm:ss AP")},
        {"Next Sunrise", result.nextSunrise.toString("dddd, d MMM h:mm:ss AP")},
        {"Daylight", detailDuration(dayLength)},
        {"Night", detailDuration(nightLength)},
        {"One Day Hour", detailDuration(result.dayHourMilliseconds)},
        {"One Night Hour", detailDuration(result.nightHourMilliseconds)},
        {"Elevation", QString("%1 m").arg(result.options.elevationMeters, 0, 'f', 0)},
        {"Pressure", pressureText(result.options)},
        {"Temperature", QString("%1 C").arg(result.options.temperatureC, 0, 'f', 1)},
        {"Method", "Apparent upper solar limb with atmospheric refraction"},
        {"Horizon", "Standard mathematical horizon; local terrain/obstructions excluded"},
        {"Warnings", result.warnings.isEmpty() ? "None" : result.warnings.join(" | ")},
    };
    fillDetailRows(rightBottomTable_, summaryRows);
}

}  // namespace dracoved
