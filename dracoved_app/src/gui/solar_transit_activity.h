#pragma once
#include "solar_transit_types.h"
#include <QSet>
#include <QTimeZone>
#include <array>
#include <algorithm>

namespace dracoved {
inline unsigned solarEventCategory(const SolarTransitEvent& event) {
    if (event.houseCrossing) return 128;
    if (event.exact) return 1;
    if (event.kind == "Orb entry") return 2;
    if (event.kind == "Orb exit") return 4;
    if (event.kind.startsWith("Near miss")) return 16;
    if (event.kind.startsWith("Approaching")) return 32;
    if (event.kind.startsWith("Station")) return 64;
    return 8;
}
enum class SolarActivityGrouping { Day, Week, CalendarMonth, ReturnMonth };
struct SolarActivityPeriod {
    QDateTime startUtc;
    QDateTime endUtc; // Half-open, including for active-window intersections.
    std::array<int, 5> counts{};
    QVector<int> eventIndices;
    QVector<int> windowIndices; // One representative per distinct contact.
    int total() const { int n = 0; for (int count : counts) n += count; return n; }
};
inline int solarActivityAspectIndex(int angle) {
    const std::array<int, 5> angles{0, 60, 90, 120, 180};
    for (int i = 0; i < 5; ++i) if (angles[i] == angle) return i;
    return -1;
}
inline QVector<SolarActivityPeriod> solarActivityPeriods(const SolarTransitResult& result,
        SolarActivityGrouping grouping, bool activeWithinOrb, int aspect,
        QDate from, QDate through) {
    QVector<SolarActivityPeriod> periods;
    if (!result.startUtc.isValid() || !result.endUtc.isValid() || from > through) return periods;
    const QTimeZone zone = result.query.source.returnChart.localDateTime.timeZone();
    const auto start = std::max(result.startUtc, from.startOfDay(zone).toUTC());
    const auto end = std::min(result.endUtc, through.addDays(1).startOfDay(zone).toUTC());
    if (start >= end) return periods;
    const auto anchor = result.startUtc.toTimeZone(zone);
    QDateTime cursor;
    if (grouping == SolarActivityGrouping::ReturnMonth) cursor = anchor;
    else {
        QDate date = start.toTimeZone(zone).date();
        if (grouping == SolarActivityGrouping::Week) date = date.addDays(1 - date.dayOfWeek());
        if (grouping == SolarActivityGrouping::CalendarMonth) date = QDate(date.year(), date.month(), 1);
        cursor = date.startOfDay(zone);
    }
    int returnMonth = 0;
    while (cursor.toUTC() < end) {
        QDateTime next;
        if (grouping == SolarActivityGrouping::ReturnMonth) next = anchor.addMonths(++returnMonth);
        else if (grouping == SolarActivityGrouping::CalendarMonth) next = cursor.date().addMonths(1).startOfDay(zone);
        else next = cursor.date().addDays(grouping == SolarActivityGrouping::Week ? 7 : 1).startOfDay(zone);
        if (!next.isValid() || next <= cursor) break;
        SolarActivityPeriod period;
        period.startUtc = std::max(start, cursor.toUTC());
        period.endUtc = std::min(end, next.toUTC());
        if (period.startUtc < period.endUtc) {
            if (!activeWithinOrb) {
                for (int i = 0; i < result.events.size(); ++i) {
                    const auto& event = result.events[i];
                    const int col = solarActivityAspectIndex(event.aspect);
                    if (!event.exact || event.houseCrossing || col < 0 || (aspect >= 0 && event.aspect != aspect)) continue;
                    if (event.utc >= period.startUtc && event.utc < period.endUtc) {
                        ++period.counts[col]; period.eventIndices.push_back(i);
                    }
                }
            } else {
                QSet<QString> contacts;
                for (int i = 0; i < result.windows.size(); ++i) {
                    const auto& window = result.windows[i];
                    const int col = solarActivityAspectIndex(window.aspect);
                    if (col < 0 || (aspect >= 0 && window.aspect != aspect)) continue;
                    if (window.startUtc >= window.endUtc || window.startUtc >= period.endUtc || window.endUtc <= period.startUtc) continue;
                    const QString key = window.body + QChar(0x1f) + window.target + QChar(0x1f) + QString::number(window.aspect);
                    if (contacts.contains(key)) continue;
                    contacts.insert(key); ++period.counts[col]; period.windowIndices.push_back(i);
                }
            }
            periods.push_back(period);
        }
        cursor = next;
    }
    return periods;
}
} // namespace dracoved
