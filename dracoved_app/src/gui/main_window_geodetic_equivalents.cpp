#include "main_window.h"

#include "geodetic_equivalents_controller.h"

#include "../core/formatting.h"
#include "../core/geodetic_equivalents.h"

#include <QAbstractItemView>
#include <QDockWidget>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

QTableWidgetItem* geodeticCell(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return item;
}

void configureGeodeticTable(
    QTableWidget* table, const QStringList& headers, int rows) {
    if (!table) {
        return;
    }
    table->setSortingEnabled(false);
    table->clearSelection();
    table->clear();
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setRowCount(std::max(0, rows));
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setWordWrap(true);
    if (auto* header = table->horizontalHeader()) {
        header->setStretchLastSection(true);
        for (int column = 0; column < headers.size(); ++column) {
            header->setSectionResizeMode(
                column, column == headers.size() - 1
                    ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
        }
    }
}

QString geodeticLongitudeText(double longitude) {
    const double normalized = normalizeGeodeticDegrees(longitude);
    const int sign = std::clamp(
        static_cast<int>(std::floor(normalized / 30.0)), 0, 11);
    const double inSign = normalized - sign * 30.0;
    int degrees = static_cast<int>(std::floor(inSign));
    int minutes = static_cast<int>(std::floor(
        (inSign - degrees) * 60.0 + 0.5));
    if (minutes >= 60) {
        minutes = 0;
        degrees += 1;
    }
    return QString("%1%2 %3 %4'")
        .arg(degrees, 2, 10, QChar('0'))
        .arg(QChar(0x00B0))
        .arg(signName(sign))
        .arg(minutes, 2, 10, QChar('0'));
}

QString geodeticCoordinates(double latitude, double longitude) {
    return QString("%1%2 %3, %4%5 %6")
        .arg(std::fabs(latitude), 0, 'f', 4)
        .arg(QChar(0x00B0))
        .arg(latitude >= 0.0 ? "N" : "S")
        .arg(std::fabs(longitude), 0, 'f', 4)
        .arg(QChar(0x00B0))
        .arg(longitude >= 0.0 ? "E" : "W");
}

void addGeodeticInfoRow(
    QTableWidget* table, int row,
    const QString& item, const QString& value) {
    table->setItem(row, 0, geodeticCell(item));
    table->setItem(row, 1, geodeticCell(value));
}

}  // namespace

void MainWindow::refreshGeodeticEquivalentsDocks() {
    if (activeTab_ != AppTab::GeodeticEquivalents
        || !rightTopTable_ || !rightBottomTable_) {
        return;
    }
    if (rightTopDock_) {
        rightTopDock_->setWindowTitle("Selected Geodetic Location");
    }
    if (rightBottomDock_) {
        rightBottomDock_->setWindowTitle("Geodetic Transit Summary");
    }

    if (!geodeticEquivalentsController_
        || !geodeticEquivalentsController_->hasResult()) {
        configureGeodeticTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(
            0, 0, geodeticCell(
                "Calculate the map, then click a world location to inspect "
                "its geodetic angles and transit activations."));
        configureGeodeticTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(
            0, 0, geodeticCell(
                "This transit-based workspace works without a natal chart."));
        return;
    }

    const GeodeticMapSnapshot snapshot =
        geodeticEquivalentsController_->snapshot();
    if (!geodeticEquivalentsController_->hasSelectedLocation()) {
        configureGeodeticTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(
            0, 0, geodeticCell(
                snapshot.stale
                    ? "Controls changed. Recalculate before selecting a location."
                    : "Click any location on the map to inspect its geodetic angles."));
    } else {
        const GeodeticAngles angles =
            geodeticEquivalentsController_->selectedAngles();
        const QVector<GeodeticContact> contacts =
            geodeticEquivalentsController_->selectedContacts();
        const int baseRows = 6;
        configureGeodeticTable(
            rightTopTable_, {"Item", "Value"}, baseRows + contacts.size());
        addGeodeticInfoRow(
            rightTopTable_, 0, "Coordinates",
            geodeticCoordinates(
                geodeticEquivalentsController_->selectedLatitude(),
                geodeticEquivalentsController_->selectedLongitude()));
        addGeodeticInfoRow(
            rightTopTable_, 1, "Geodetic ASC",
            geodeticLongitudeText(angles.ascendant));
        addGeodeticInfoRow(
            rightTopTable_, 2, "Geodetic MC",
            geodeticLongitudeText(angles.midheaven));
        addGeodeticInfoRow(
            rightTopTable_, 3, "Geodetic DSC",
            geodeticLongitudeText(angles.descendant));
        addGeodeticInfoRow(
            rightTopTable_, 4, "Geodetic IC",
            geodeticLongitudeText(angles.imumCoeli));
        addGeodeticInfoRow(
            rightTopTable_, 5, "Activations",
            contacts.isEmpty()
                ? "No selected body is within the configured orb."
                : QString("%1 within orb").arg(contacts.size()));
        for (int index = 0; index < contacts.size(); ++index) {
            const GeodeticContact& contact = contacts[index];
            addGeodeticInfoRow(
                rightTopTable_, baseRows + index,
                QString("%1 - %2").arg(contact.body, contact.angle),
                QString("%1%2 - %3")
                    .arg(contact.orb, 0, 'f', 2)
                    .arg(QChar(0x00B0))
                    .arg(contact.motion));
        }
        rightTopTable_->resizeRowsToContents();
    }

    const QString zodiac =
        snapshot.zodiacSystem == ZodiacSystem::Sidereal
            ? QString("Sidereal - %1").arg(
                  siderealAyanamsaToString(snapshot.ayanamsa))
            : QString("Tropical");
    const QString bodyNames = [&snapshot]() {
        QStringList names;
        for (const auto& body : snapshot.bodies) {
            names.push_back(body.name + (body.retrograde ? " R" : ""));
        }
        return names.join(", ");
    }();
    const QVector<QPair<QString, QString>> summary = {
        {"Result State", snapshot.stale ? "Stale - recalculate" : "Current"},
        {"Local Moment",
            snapshot.localMoment.toString("dddd, d MMMM yyyy, h:mm:ss AP")},
        {"Timezone", snapshot.timezone},
        {"UTC Moment",
            snapshot.utcMoment.toString("yyyy-MM-dd HH:mm:ss 'UTC'")},
        {"Method", "Standard / Sepharial"},
        {"Aries Meridian",
            QString("%1%2")
                .arg(snapshot.referenceMeridian, 0, 'f', 2)
                .arg(QChar(0x00B0))},
        {"Transit Zodiac", zodiac},
        {"Bodies", bodyNames},
        {"Visible Lines", QString::number(snapshot.visibleLineCount)},
        {"Warnings",
            snapshot.warnings.isEmpty()
                ? QString("-") : snapshot.warnings.join(" | ")},
    };
    configureGeodeticTable(
        rightBottomTable_, {"Item", "Value"}, summary.size());
    for (int row = 0; row < summary.size(); ++row) {
        addGeodeticInfoRow(
            rightBottomTable_, row,
            summary[row].first, summary[row].second);
    }
    rightBottomTable_->resizeRowsToContents();
}

}  // namespace dracoved
