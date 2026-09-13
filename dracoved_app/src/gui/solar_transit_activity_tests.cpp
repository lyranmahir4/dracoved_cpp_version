#include "solar_transit_activity.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
using namespace dracoved;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static QDateTime at(int month, int day, int hour = 0) { return QDateTime(QDate(2026, month, day), QTime(hour, 0), QTimeZone::UTC); }
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        SolarTransitResult r; r.startUtc = at(1, 31, 12); r.endUtc = at(4, 1, 12);
        r.query.source.returnChart.localDateTime = r.startUtc;
        auto event = [&](QDateTime utc, int aspect, bool exact, QString kind) {
            SolarTransitEvent e; e.utc = utc; e.body = "Mercury"; e.target = "Venus";
            e.aspect = aspect; e.exact = exact; e.kind = kind; r.events << e;
        };
        event(at(1, 31, 12), 120, true, "Exact at year start");
        event(at(2, 1), 120, true, "Exact contact");
        event(at(2, 1, 1), 120, true, "Exact contact");
        event(at(2, 1, 2), 90, true, "Exact contact");
        event(at(2, 1), 120, false, "Orb entry");
        event(at(2, 1, 3), 120, false, "Orb exit");
        event(r.endUtc, 120, true, "Exact contact");
        event(at(2, 1, 4), 120, true, "House crossing"); r.events.last().houseCrossing = true;
        auto periods = solarActivityPeriods(r, SolarActivityGrouping::Day, false, -1, QDate(2026,1,1), QDate(2026,4,5));
        int sum = 0; for (const auto& p : periods) sum += p.total();
        require(sum == 4, "Count exact passes, exclude entry/exit, houses and next-return boundary");
        require(periods[0].total() == 1 && periods[1].total() == 3, "Half-open daily buckets");
        periods = solarActivityPeriods(r, SolarActivityGrouping::Day, false, 120, QDate(2026,2,1), QDate(2026,2,1));
        require(periods.size() == 1 && periods[0].total() == 2, "Aspect and inclusive local-date filter");
        periods = solarActivityPeriods(r, SolarActivityGrouping::ReturnMonth, false, -1, QDate(2026,1,1), QDate(2026,4,5));
        require(periods.size() == 3 && periods[0].endUtc == at(2,28,12) && periods[1].endUtc == at(3,31,12), "Return months retain original day/time anchor");
        r.windows = {{at(1,31,12),at(2,2),"Pluto","Venus",120}, {at(2,3),at(2,5),"Pluto","Venus",120},
            {at(2,1),at(2,2),"Pluto","Venus",90}, {at(1,31,12),at(2,1),"Uranus","Venus",120}};
        periods = solarActivityPeriods(r, SolarActivityGrouping::CalendarMonth, true, -1, QDate(2026,2,1), QDate(2026,2,28));
        require(periods.size() == 1 && periods[0].total() == 2 && periods[0].windowIndices.size() == 2, "Active contacts deduplicate repeated windows and exclude end-touching intervals");
        periods = solarActivityPeriods(r, SolarActivityGrouping::Day, true, 120, QDate(2026,2,1), QDate(2026,2,2));
        require(periods[0].total() == 1 && periods[1].total() == 0, "Active window exit midnight belongs to neither following day");
        require(solarActivityPeriods(r, SolarActivityGrouping::Day, false, -1, QDate(2026,5,1), QDate(2026,6,1)).isEmpty(), "Out-of-year range empty");
        require(solarActivityPeriods(r, SolarActivityGrouping::Day, false, -1, QDate(2026,3,1), QDate(2026,2,1)).isEmpty(), "Reversed range empty");
        QTimeZone zone("America/New_York"); require(zone.isValid(), "DST zone available");
        r.startUtc = QDate(2026,3,7).startOfDay(zone).toUTC(); r.endUtc = QDate(2026,3,10).startOfDay(zone).toUTC();
        r.query.source.returnChart.localDateTime = r.startUtc.toTimeZone(zone);
        periods = solarActivityPeriods(r, SolarActivityGrouping::Day, false, -1, QDate(2026,3,7), QDate(2026,3,9));
        require(periods.size() == 3 && periods[1].startUtc.secsTo(periods[1].endUtc) == 23*3600, "Local day buckets respect DST");
        periods = solarActivityPeriods(r, SolarActivityGrouping::Week, false, -1, QDate(2026,3,7), QDate(2026,3,9));
        require(periods.size() == 2 && periods[0].endUtc.toTimeZone(zone).date().dayOfWeek() == 1, "Weeks split on local Monday");
        SolarTransitEvent e; e.exact = true; require(solarEventCategory(e) == 1, "Exact checkbox category");
        e.exact = false; e.kind = "Orb exit"; require(solarEventCategory(e) == 4, "Independent exit category");
        e.kind = "Near miss at station"; require(solarEventCategory(e) == 16, "Near miss not double categorized as station");
        std::cout << "PASS: activity exact/active counts, deduplication, aspect filters, year/date bounds, return months, DST, weeks, event categories\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
