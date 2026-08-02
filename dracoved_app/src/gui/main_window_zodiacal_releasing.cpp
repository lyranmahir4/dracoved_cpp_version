#include "main_window.h"

#include "zodiacal_releasing_controller.h"

#include <QAbstractItemView>
#include <QDockWidget>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

struct ReleasingInfoRow {
    QString item;
    QString value;
    int signIndex = -1;
};

QTableWidgetItem* releasingCell(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
    return item;
}

void configureReleasingTable(QTableWidget* table,
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
    table->setIconSize(QSize(18, 18));
    if (auto* header = table->horizontalHeader()) {
        header->setStretchLastSection(false);
        for (int column = 0; column < headers.size(); ++column) {
            header->setSectionResizeMode(
                column,
                column == headers.size() - 1
                    ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
        }
    }
}

void fillReleasingRows(QTableWidget* table,
                       const QVector<ReleasingInfoRow>& rows) {
    configureReleasingTable(table, {"Item", "Value"}, rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        table->setItem(row, 0, releasingCell(rows[row].item));
        auto* valueItem = releasingCell(rows[row].value);
        if (rows[row].signIndex >= 0 && rows[row].signIndex < 12) {
            valueItem->setIcon(zodiacalReleasingSignIcon(
                rows[row].signIndex, table->palette().text().color()));
        }
        table->setItem(row, 1, valueItem);
    }
    table->resizeRowsToContents();
}

QString releasingDateTime(const QDateTime& utc, const QString& timezoneLabel) {
    if (!utc.isValid()) return "-";
    const QTimeZone timezone(timezoneLabel.toUtf8());
    return (timezone.isValid() ? utc.toTimeZone(timezone) : utc.toUTC())
        .toString("dddd, d MMMM yyyy, h:mm:ss AP");
}

QString releasingDuration(const ZodiacalReleasingPeriod& period) {
    const qint64 seconds = std::max<qint64>(0, period.startUtc.secsTo(period.endUtc));
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (days >= 365) {
        return QString("%1 years (%2 days)")
            .arg(static_cast<double>(days) / 365.2425, 0, 'f', 2)
            .arg(days);
    }
    if (days > 0) return QString("%1d %2h %3m").arg(days).arg(hours).arg(minutes);
    return QString("%1h %2m").arg(hours).arg(minutes);
}

QString releasingMarkers(const ZodiacalReleasingPeriod& period) {
    QStringList markers;
    if (period.loosingOfBond) markers << "Loosing of the Bond";
    if (period.foreshadowing) markers << "Foreshadowing";
    if (period.fortuneHouse == 1 || period.fortuneHouse == 10) {
        markers << "Major peak";
    } else if (period.fortuneHouse == 4 || period.fortuneHouse == 7) {
        markers << "Fortune angle";
    }
    if (period.truncated) markers << "Truncated at parent boundary";
    return markers.isEmpty() ? "None" : markers.join(" | ");
}

int lotSignIndex(double longitude) {
    double normalized = std::fmod(longitude, 360.0);
    if (normalized < 0.0) normalized += 360.0;
    return qBound(0, static_cast<int>(std::floor(normalized / 30.0)), 11);
}

QString lotPosition(double longitude) {
    double normalized = std::fmod(longitude, 360.0);
    if (normalized < 0.0) normalized += 360.0;
    const int sign = lotSignIndex(longitude);
    const double inSign = normalized - sign * 30.0;
    const int degrees = static_cast<int>(std::floor(inSign));
    const int minutes = static_cast<int>(std::floor((inSign - degrees) * 60.0));
    return QString("%1 %2%3 %4'")
        .arg(zodiacalReleasingSignName(sign))
        .arg(degrees, 2, 10, QChar('0'))
        .arg(QChar(0x00B0))
        .arg(minutes, 2, 10, QChar('0'));
}

QString ageAt(const QDateTime& birthUtc, const QDateTime& momentUtc) {
    if (!birthUtc.isValid() || !momentUtc.isValid()) return "-";
    const long double years = static_cast<long double>(
        birthUtc.msecsTo(momentUtc)) / (365.2425L * 86400000.0L);
    return QString::number(static_cast<double>(years), 'f', 2);
}

QString chainSummary(const QVector<ZodiacalReleasingPeriod>& chain) {
    if (chain.isEmpty()) return "No period contains the research date.";
    QStringList parts;
    for (const auto& period : chain) {
        parts << QString("L%1 %2").arg(period.level).arg(period.signName);
    }
    return parts.join(" -> ");
}

}  // namespace

void MainWindow::refreshZodiacalReleasingDocks() {
    if (activeTab_ != AppTab::ZodiacalReleasing
        || !rightTopTable_ || !rightBottomTable_) {
        return;
    }
    if (rightTopDock_) rightTopDock_->setWindowTitle("Releasing Period Details");
    if (rightBottomDock_) rightBottomDock_->setWindowTitle("Releasing Method Summary");

    if (!zodiacalReleasingController_
        || !zodiacalReleasingController_->hasTimeline()) {
        configureReleasingTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, releasingCell(
            hasCurrentChart_
                ? "Calculate the Zodiacal Releasing timeline, then select a period."
                : "Load a natal chart to use Zodiacal Releasing."));
        configureReleasingTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, releasingCell(
            "The active Lot, time key, start sign and reference chain will appear here."));
        return;
    }

    const ZodiacalReleasingTimeline timeline =
        zodiacalReleasingController_->timeline();
    const QString timezone = currentInput_.timezone;

    if (!zodiacalReleasingController_->hasSelectedPeriod()) {
        configureReleasingTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, releasingCell(
            "Select a period in the timeline to inspect it."));
    } else {
        const ZodiacalReleasingPeriod period =
            zodiacalReleasingController_->selectedPeriod();
        QString natalRulerPlacement = "Not found in natal chart";
        for (const auto& body : currentChart_.bodies) {
            if (body.name == period.ruler) {
                natalRulerPlacement = QString("%1 %2%3 %4' - House %5%6")
                    .arg(body.signName)
                    .arg(static_cast<int>(std::floor(body.degInSign)), 2, 10, QChar('0'))
                    .arg(QChar(0x00B0))
                    .arg(static_cast<int>(std::floor(
                             (body.degInSign - std::floor(body.degInSign)) * 60.0)),
                         2, 10, QChar('0'))
                    .arg(body.house)
                    .arg(body.retrograde ? " - retrograde" : "");
                break;
            }
        }
        const QVector<ReleasingInfoRow> details = {
            {"Level", QString("L%1").arg(period.level)},
            {"Sign", period.signName, period.signIndex},
            {"Time Lord", period.ruler},
            {"Starts", releasingDateTime(period.startUtc, timezone)},
            {"Ends", releasingDateTime(period.endUtc, timezone)},
            {"Duration", releasingDuration(period)},
            {"Age at Start", ageAt(timeline.context.birthUtc, period.startUtc)},
            {"Sequence in Parent", QString::number(period.sequenceIndex + 1)},
            {"Parent Sign", period.parentSignIndex >= 0
                ? zodiacalReleasingSignName(period.parentSignIndex) : "Release point",
                period.parentSignIndex},
            {"Relation to Fortune", period.fortuneRelationship},
            {"Special Markers", releasingMarkers(period)},
            {"Natal Ruler Placement", natalRulerPlacement},
            {"Boundary Rule", "[start, end) - exact end belongs to the next period"},
        };
        fillReleasingRows(rightTopTable_, details);
    }

    const QString zodiac = currentInput_.zodiacSystem == ZodiacSystem::Sidereal
        ? QString("Sidereal - %1").arg(
              siderealAyanamsaToString(currentInput_.siderealAyanamsa))
        : QString("Tropical");
    const QString adjustment = timeline.sameSignSpiritAdjustmentApplied
        ? QString("Applied - Spirit moved from %1 to %2")
            .arg(zodiacalReleasingSignName(timeline.lotSignIndex),
                 zodiacalReleasingSignName(timeline.startSignIndex))
        : "Not required";
    const QVector<ReleasingInfoRow> summary = {
        {"Natal Chart", zodiacalReleasingController_->natalName()},
        {"Birth", releasingDateTime(timeline.context.birthUtc, timezone)},
        {"Location", zodiacalReleasingController_->locationName()},
        {"Sect", timeline.context.isDayChart ? "Day chart" : "Night chart"},
        {"Zodiac", zodiac},
        {"Release Point", zodiacalReleasingPointName(timeline.settings.releasePoint)},
        {"Release Lot Position", lotPosition(timeline.releaseLongitude),
            lotSignIndex(timeline.releaseLongitude)},
        {"Fortune Position", lotPosition(timeline.context.fortuneLongitude),
            lotSignIndex(timeline.context.fortuneLongitude)},
        {"Effective Start Sign", zodiacalReleasingSignName(timeline.startSignIndex),
            timeline.startSignIndex},
        {"Same-Sign Spirit Rule", adjustment},
        {"Time Key", zodiacalReleasingTimeKeyName(timeline.settings.timeKey)},
        {"Capricorn Period", QString("%1 years").arg(timeline.settings.capricornYears)},
        {"Maximum Detail", QString("L1-L%1").arg(timeline.settings.maximumLevel)},
        {"Calculated Through", releasingDateTime(timeline.rangeEndUtc, timezone)},
        {"Research Moment", releasingDateTime(
             zodiacalReleasingController_->referenceMomentUtc(), timezone)},
        {"Research Chain", chainSummary(
             zodiacalReleasingController_->referenceChain())},
        {"Result State", zodiacalReleasingController_->isStale()
             ? "Stale - recalculate" : "Current"},
    };
    fillReleasingRows(rightBottomTable_, summary);
}

}  // namespace dracoved
