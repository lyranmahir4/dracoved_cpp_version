#include "progression_events.h"
#include "progression.h"
#include "formatting.h"

#include <QMap>
#include <QTimeZone>
#include <algorithm>
#include <cmath>

namespace dracoved {
namespace {
double signedArc(double v) { return normalizeDegrees(v + 180.0) - 180.0; }
struct Snapshot {
    QMap<QString, double> longitudes;
    QMap<QString, double> speeds;
    QVector<double> cusps;
    double asc = 0.0;
};
struct Track {
    QString body;
    int cusp = -1; // -1: zodiac longitude; 0..11: relative to a house cusp.
};
}

QVector<ProgressionEvent> findProgressionEvents(SwissEph& swe, const QString& ephePath,
    const ProgressionEventQuery& q, const std::function<bool()>& cancelled,
    const std::function<void(int)>& progress, QString* error) {
    QVector<ProgressionEvent> events;
    if (error) error->clear();
    const qint64 first = q.start.toMSecsSinceEpoch();
    const qint64 last = q.end.toMSecsSinceEpoch();
    if (!q.start.isValid() || !q.end.isValid() || first >= last) {
        if (error) *error = "Choose a valid start and end date.";
        return events;
    }
    SecondaryProgressionEngine engine(&swe, ephePath);
    const bool whole = q.input.houseSystem == HouseSystem::WholeSign;
    const bool movingWhole = whole && q.progressedHouses;
    const QString houseReference = q.progressedHouses ? "Progressed" : "Natal";
    QVector<double> natalCusps;
    if (whole) {
        for (int i = 0; i < 12; ++i) natalCusps << normalizeDegrees(signIndex(q.natal.angles.asc) * 30.0 + i * 30.0);
    } else {
        for (const auto& cusp : q.natal.cusps) natalCusps << cusp.longitude;
    }
    if (q.houses && !q.progressedHouses && natalCusps.size() != 12) {
        if (error) *error = "Natal house cusps are unavailable. Recalculate the natal chart first.";
        return events;
    }
    auto check = [&]() { if (cancelled()) throw QString(); };
    QMap<qint64, Snapshot> cache;
    auto sample = [&](qint64 ms) -> Snapshot {
        check();
        // The progression engine resolves the target to seconds.
        ms = (ms / 1000) * 1000;
        const auto found = cache.constFind(ms);
        if (found != cache.cend()) return found.value();
        NatalChart chart;
        QString message;
        const QDateTime local = QDateTime::fromMSecsSinceEpoch(ms, q.start.timeZone());
        if (!engine.compute(q.input, local, q.timezone, &chart, &message, false, false)) {
            throw message.isEmpty() ? QString("Unable to calculate progressed positions.") : message;
        }
        Snapshot s;
        for (const auto& body : chart.bodies) {
            s.longitudes.insert(body.name, body.longitude);
            if (body.hasSpeed) s.speeds.insert(body.name, body.speed);
        }
        s.longitudes.insert("Ascendant", chart.angles.asc);
        s.longitudes.insert("Midheaven", chart.angles.mc);
        s.asc = chart.angles.asc;
        for (const auto& cusp : chart.cusps) s.cusps << cusp.longitude;
        cache.insert(ms, s);
        return s;
    };
    auto longitude = [&](const Track& track, qint64 ms) {
        const Snapshot s = sample(ms);
        if (!s.longitudes.contains(track.body)) throw QString("No progressed position for %1.").arg(track.body);
        double value = s.longitudes.value(track.body);
        if (track.cusp >= 0) {
            const auto& cusps = q.progressedHouses ? s.cusps : natalCusps;
            if (cusps.size() != 12) throw QString("Progressed house cusps are unavailable.");
            value -= cusps[track.cusp];
        }
        return normalizeDegrees(value);
    };
    auto velocity = [&](const Track& track, qint64 ms) {
        if (track.cusp < 0) {
            const Snapshot s = sample(ms);
            const auto speed = s.speeds.constFind(track.body);
            if (speed != s.speeds.cend()) return speed.value();
        }
        // Central difference over two target hours (about 20 ephemeris seconds).
        return signedArc(longitude(track, ms + 3600000) - longitude(track, ms - 3600000));
    };
    auto root = [&](qint64 a, qint64 b, const std::function<double(qint64)>& f) {
        double fa = f(a);
        while (b - a > 1000) {
            check();
            const qint64 mid = a + (b - a) / 2;
            const double fm = f(mid);
            if ((fa < 0.0) == (fm < 0.0)) { a = mid; fa = fm; }
            else b = mid;
        }
        return b; // First second on the new side of the boundary.
    };
    auto add = [&](qint64 ms, const QString& body, const QString& kind,
                   const QString& transition, const QString& motion, const QString& detail) {
        if (ms < first || ms >= last) return;
        for (auto it = events.crbegin(); it != events.crend(); ++it) {
            if (it->body == body && it->kind == kind && it->transition == transition
                && std::abs(it->time.toMSecsSinceEpoch() - ms) < 2000) return;
        }
        events.push_back({QDateTime::fromMSecsSinceEpoch(ms, q.start.timeZone()), body, kind,
            transition, motion, sample(ms).longitudes.value(body), detail});
    };
    auto wholeHouse = [](const Snapshot& s, const QString& body) {
        return (signIndex(s.longitudes.value(body)) - signIndex(s.asc) + 12) % 12 + 1;
    };
    QVector<Track> tracks;
    for (const auto& body : q.bodies) {
        if (q.signs || q.stations || (q.houses && movingWhole)) tracks.push_back({body, -1});
        if (q.houses && !movingWhole) {
            for (int cusp = 0; cusp < 12; ++cusp) tracks.push_back({body, cusp});
        }
    }
    if (q.angles || (q.houses && movingWhole)) tracks.push_back({"Ascendant", -1});
    if (q.angles) tracks.push_back({"Midheaven", -1});
    try {
        // Thirty target days are only ~0.082 ephemeris days. Split at motion
        // reversals so retrograde exits and re-entries are both retained.
        constexpr qint64 step = 30LL * 86400000;
        for (qint64 a = first; a < last; a += step) {
            const qint64 b = std::min(last, a + step);
            cache.clear();
            for (const auto& track : tracks) {
                check();
                const bool angle = track.body == "Ascendant" || track.body == "Midheaven";
                QVector<qint64> edges{a, b};
                const double va = velocity(track, a), vb = velocity(track, b);
                if (va * vb < 0.0) {
                    const qint64 turn = root(a, b, [&](qint64 t) { return velocity(track, t); });
                    edges.insert(1, turn);
                    if (q.stations && track.cusp < 0 && !angle) {
                        add(turn, track.body, "Station", vb > 0 ? "Retrograde → Direct" : "Direct → Retrograde",
                            vb > 0 ? "Direct" : "Retrograde", "Direction change in the progressed zodiac longitude.");
                    }
                }
                if (track.cusp < 0 && !angle && !q.signs && !(q.houses && movingWhole)) continue;
                for (int part = 1; part < edges.size(); ++part) {
                    const qint64 lo = edges[part - 1], hi = edges[part];
                    for (int boundary = 0; boundary < (track.cusp < 0 ? 12 : 1); ++boundary) {
                        const double target = boundary * 30.0;
                        auto f = [&](qint64 t) { return signedArc(longitude(track, t) - target); };
                        const double fa = f(lo), fb = f(hi);
                        if (std::abs(fb - fa) >= 180.0 || fa * fb > 0.0 || fa == fb) continue;
                        const qint64 at = fa == 0.0 ? lo : root(lo, hi, f);
                        const double before = f(at - 60000), after = f(at + 60000);
                        if ((before < 0.0) == (after < 0.0)) continue; // Touch, not a crossing.
                        const bool forward = after > before;
                        const QString motion = velocity({track.body, -1}, at) >= 0 ? "Direct" : "Retrograde";
                        if (track.cusp >= 0) {
                            const int from = forward ? (track.cusp + 11) % 12 + 1 : track.cusp + 1;
                            const int to = forward ? track.cusp + 1 : (track.cusp + 11) % 12 + 1;
                            add(at, track.body, houseReference + " cusp", QString("H%1 → H%2").arg(from).arg(to), motion,
                                QString("Crosses %1 cusp %2 (%3 houses). Direction is relative to the cusp.")
                                    .arg(houseReference.toLower()).arg(track.cusp + 1).arg(whole ? "Whole Sign" : "Placidus"));
                        } else {
                            const int from = forward ? (boundary + 11) % 12 : boundary;
                            const int to = forward ? boundary : (boundary + 11) % 12;
                            if ((angle && q.angles) || (!angle && q.signs)) {
                                add(at, track.body, "Sign entry", signName(from) + " → " + signName(to), motion,
                                    QString("Crosses the %1 boundary.").arg(signName(boundary)));
                            }
                            if (q.houses && movingWhole && track.body != "Midheaven") {
                                const Snapshot pre = sample(at - 60000), post = sample(at + 60000);
                                const QStringList bodies = angle ? q.bodies : QStringList{track.body};
                                for (const auto& body : bodies) {
                                    const int h1 = wholeHouse(pre, body), h2 = wholeHouse(post, body);
                                    if (h1 == h2) continue;
                                    add(at, body, angle ? "House shift (ASC)" : "Progressed cusp",
                                        QString("H%1 → H%2").arg(h1).arg(h2),
                                        velocity({body, -1}, at) >= 0 ? "Direct" : "Retrograde",
                                        angle ? "Progressed Whole Sign house changes because the Ascendant enters a new sign."
                                              : "Planet crosses a progressed Whole Sign house boundary.");
                                }
                            }
                        }
                    }
                }
            }
            progress(static_cast<int>(100.0 * (b - first) / (last - first)));
        }
    } catch (const QString& message) {
        if (error) *error = message;
    }
    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        if (a.time != b.time) return a.time < b.time;
        if (a.body != b.body) return a.body < b.body;
        return a.kind < b.kind;
    });
    return events;
}
} // namespace dracoved
