#include "moorthi.h"
#include "formatting.h"
#include <cmath>
#include <functional>

namespace dracoved {
Moorthi moorthiForCount(int count) {
    switch (count) {
        case 1: case 6: case 11: return Moorthi::Swarna;
        case 2: case 5: case 9: return Moorthi::Rajata;
        case 3: case 7: case 10: return Moorthi::Tamra;
        default: return Moorthi::Loha;
    }
}
QString moorthiName(Moorthi value) {
    switch (value) {
        case Moorthi::Swarna: return "Swarna";
        case Moorthi::Rajata: return "Rajata";
        case Moorthi::Tamra: return "Tamra";
        default: return "Loha";
    }
}

bool findMoorthiEntries(SwissEph& swe, int body, double offset, double from,
                       double to, int natalMoonSign, QVector<MoorthiEntry>* out,
                       QString* error) {
    if (!out || natalMoonSign < 0 || natalMoonSign > 11 || !std::isfinite(from)
        || !std::isfinite(to) || to <= from || to - from > 0.251) {
        if (error) *error = "Invalid Moorthi search interval.";
        return false;
    }
    struct Position { double lon; double speed; };
    auto position = [&](double jd, Position* p) {
        double values[6]{};
        if (!swe.calcUtFull(jd, body, SEFLG_SIDEREAL | SEFLG_SPEED, values, error)) return false;
        p->lon = normalizeDegrees(values[0] + offset);
        p->speed = values[3];
        if (std::isfinite(p->lon) && std::isfinite(p->speed)) return true;
        if (error) *error = "Ephemeris returned an invalid position.";
        return false;
    };
    auto distance = [](double a, double b) { return std::remainder(a - b, 360.0); };
    std::function<bool(double, double, Position, Position, int)> scan;
    scan = [&](double a, double b, Position pa, Position pb, int depth) {
        // Split at a station before testing sign endpoints: a short excursion
        // across a boundary and back must produce two entries, not disappear.
        if (pa.speed * pb.speed < 0.0 && depth < 4) {
            double lo = a, hi = b;
            Position pm{};
            for (int i = 0; i < 40 && hi - lo > 0.01 / 86400.0; ++i) {
                const double mid = (lo + hi) * 0.5;
                if (!position(mid, &pm)) return false;
                if ((pm.speed < 0) == (pa.speed < 0)) lo = mid; else hi = mid;
            }
            const double station = (lo + hi) * 0.5;
            if (!position(station, &pm)) return false;
            return scan(a, station, pa, pm, depth + 1) && scan(station, b, pm, pb, depth + 1);
        }
        const int sa = signIndex(pa.lon), sb = signIndex(pb.lon);
        if (sa == sb) return true;
        const bool retrograde = distance(pb.lon, pa.lon) < 0.0;
        const double boundary = retrograde ? sa * 30.0 : ((sa + 1) % 12) * 30.0;
        double lo = a, hi = b;
        for (int i = 0; i < 40 && hi - lo > 0.01 / 86400.0; ++i) {
            const double mid = (lo + hi) * 0.5;
            Position pm{};
            if (!position(mid, &pm)) return false;
            if ((distance(pm.lon, boundary) < 0.0) != retrograde) lo = mid; else hi = mid;
        }
        MoorthiEntry entry;
        entry.jd = (lo + hi) * 0.5;
        entry.newSign = sb;
        entry.retrograde = retrograde;
        if (!swe.calcUt(entry.jd, SE_MOON, SEFLG_SIDEREAL, &entry.moonLongitude, error)) return false;
        entry.moonSign = signIndex(entry.moonLongitude);
        if (body == SE_MOON && offset == 0.0) {
            entry.moonLongitude = boundary;
            entry.moonSign = sb;
        }
        entry.count = (entry.moonSign - natalMoonSign + 12) % 12 + 1;
        entry.moorthi = moorthiForCount(entry.count);
        out->push_back(entry);
        return true;
    };
    Position first{}, last{};
    return position(from, &first) && position(to, &last) && scan(from, to, first, last, 0);
}
}  // namespace dracoved
