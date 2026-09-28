#include "vimshottari.h"
#include "vedic_nakshatra.h"
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace dracoved {
QString dashaLordName(int lord) {
    static const QStringList names = {"Ketu", "Venus", "Sun", "Moon", "Mars", "Rahu", "Jupiter", "Saturn", "Mercury"};
    return names.value(lord);
}
int dashaLordYears(int lord) {
    static const int years[] = {7, 20, 6, 10, 7, 18, 16, 19, 17};
    return lord >= 0 && lord < 9 ? years[lord] : 0;
}
QString dashaLevelName(int level) {
    static const QStringList names = {"Mahadasha", "Antardasha", "Pratyantar", "Sookshma", "Prana"};
    return names.value(level);
}
QString dashaLevelAbbreviation(int level) {
    static const QStringList names = {"MD", "AD", "PD", "SD", "PrD"};
    return names.value(level);
}
bool Vimshottari::initialize(qint64 birthUtcMs, double moonLongitude, double yearDays) {
    valid_ = false; birthMajor_ = {};
    // The UI/ephemeris supports historical dates; bound the pure engine too,
    // keeping all multiplication well within signed 64-bit milliseconds.
    if (birthUtcMs < -62135596800000LL || birthUtcMs > 253402300799999LL
        || (yearDays != 365.25 && yearDays != 360.0)) return false;
    const auto star = classifyVedicNakshatra(moonLongitude);
    if (!star.valid) return false;
    double longitude = std::fmod(moonLongitude, 360.0);
    if (longitude < 0) longitude += 360.0;
    const long double fraction = std::clamp((static_cast<long double>(longitude) - star.index * (40.0L / 3.0L)) / (40.0L / 3.0L), 0.0L, 1.0L);
    yearDays_ = yearDays; yearMs_ = qRound64(yearDays * 86400000.0); birthMs_ = birthUtcMs;
    const int lord = star.index % 9;
    const qint64 duration = dashaLordYears(lord) * yearMs_;
    const qint64 elapsed = std::clamp<qint64>(std::llround(fraction * duration), 0, duration - 1);
    birthMajor_ = {lord, 0, birthMs_ - elapsed, birthMs_ - elapsed + duration};
    valid_ = true;
    return true;
}
QVector<DashaPeriod> Vimshottari::majorCycle(qint64 inspectionUtcMs) const {
    QVector<DashaPeriod> periods;
    if (!valid_ || inspectionUtcMs < -62135596800000LL || inspectionUtcMs > 253402300799999LL) return periods;
    const qint64 cycleMs = 120 * yearMs_;
    const qint64 offset = inspectionUtcMs - birthMajor_.startMs;
    qint64 cycle = offset / cycleMs;
    if (offset < 0 && offset % cycleMs) --cycle;
    qint64 start = birthMajor_.startMs + cycle * cycleMs;
    for (int i = 0; i < 9; ++i) {
        const int lord = (birthMajor_.lord + i) % 9;
        const qint64 end = start + dashaLordYears(lord) * yearMs_;
        periods.push_back({lord, 0, start, end}); start = end;
    }
    return periods;
}
QVector<DashaPeriod> Vimshottari::children(const DashaPeriod& parent) {
    QVector<DashaPeriod> result;
    if (parent.lord < 0 || parent.lord >= 9 || parent.level < 0 || parent.level >= 4 || parent.endMs <= parent.startMs) return result;
    qint64 start = parent.startMs;
    int cumulativeYears = 0;
    for (int i = 0; i < 9; ++i) {
        const int lord = (parent.lord + i) % 9;
        cumulativeYears += dashaLordYears(lord);
        const qint64 end = i == 8 ? parent.endMs : parent.startMs
            + std::llround(static_cast<long double>(parent.endMs - parent.startMs) * cumulativeYears / 120.0L);
        result.push_back({lord, parent.level + 1, start, end}); start = end;
    }
    return result;
}
QVector<DashaPeriod> Vimshottari::activeAt(qint64 inspectionUtcMs) const {
    QVector<DashaPeriod> active;
    auto periods = majorCycle(inspectionUtcMs);
    for (int level = 0; level < 5; ++level) {
        const auto it = std::find_if(periods.cbegin(), periods.cend(), [inspectionUtcMs](const auto& p) { return p.contains(inspectionUtcMs); });
        if (it == periods.cend()) return {};
        active.push_back(*it); periods = children(*it);
    }
    return active;
}
} // namespace dracoved
