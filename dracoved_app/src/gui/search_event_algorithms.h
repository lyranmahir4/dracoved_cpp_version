#pragma once

// Search-only event algorithms. Times are Julian days, angles are degrees.
// The injected ephemeris also makes boundary and retrograde cases testable.
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace dracoved::search_events {

struct Position {
    double longitude = 0.0;
    double speed = 0.0;
};
using Ephemeris = std::function<Position(double)>;
using Check = std::function<void()>; // Throws on cancellation or read failure.
using EventSink = std::function<void(double, double, int)>;

inline double wrap(double angle) {
    angle = std::fmod(angle, 360.0);
    return angle < 0.0 ? angle + 360.0 : angle;
}
inline double signedAngle(double angle) { return wrap(angle + 180.0) - 180.0; }
inline double orb(double relativeLongitude, double aspect) {
    return std::abs(std::abs(signedAngle(relativeLongitude)) - aspect);
}

inline double root(double a, double b, const std::function<double(double)>& value,
                   const Check& check) {
    double fa = value(a);
    for (int i = 0; i < 45 && b - a > 1e-8; ++i) {
        check();
        const double mid = (a + b) * 0.5;
        const double fm = value(mid);
        if ((fa < 0.0) == (fm < 0.0)) { a = mid; fa = fm; }
        else { b = mid; }
    }
    return (a + b) * 0.5;
}

// Refine each directed longitude crossing separately. This catches both edges
// of a narrow orb window even when both samples lie outside that window.
inline void aspects(const Ephemeris& first, const Ephemeris& second,
                    double start, double end, double aspect, double limit,
                    bool exact, const Check& check, const EventSink& sink) {
    std::vector<double> boundaries;
    auto add = [&](double angle) {
        angle = wrap(angle);
        for (double other : boundaries) {
            if (std::abs(signedAngle(angle - other)) < 1e-7) return;
        }
        boundaries.push_back(angle);
    };
    for (double angle : {aspect, -aspect}) {
        if (exact) add(angle);
        else { add(angle - limit); add(angle + limit); }
    }
    auto relative = [&](double t) {
        return signedAngle(first(t).longitude - second(t).longitude);
    };
    auto relativeSpeed = [&](double t) { return first(t).speed - second(t).speed; };
    double previous = relative(start);
    double lastEmitted = -1e100;
    for (double a = start; a < end;) {
        check();
        double b = std::min(end, a + 0.25);
        // Split at a relative-motion reversal so two contacts on opposite
        // sides of the turn cannot disappear between sampling endpoints.
        const double speedA = relativeSpeed(a);
        const double speedB = relativeSpeed(b);
        if (speedA * speedB < 0.0) {
            const double turn = root(a, b, relativeSpeed, check);
            if (turn - a > 1e-7 && b - turn > 1e-7) b = turn;
        }
        const double next = relative(b);
        std::vector<double> hits;
        for (double boundary : boundaries) {
            const double fa = signedAngle(previous - boundary);
            const double fb = signedAngle(next - boundary);
            // Do not mistake the +/-180-degree branch cut for a root.
            if (std::abs(fb - fa) >= 180.0) continue;
            if (std::abs(fa) < 1e-9) hits.push_back(a);
            else if (fa * fb <= 0.0) {
                hits.push_back(root(a, b, [&](double t) {
                    return signedAngle(relative(t) - boundary);
                }, check));
            }
        }
        std::sort(hits.begin(), hits.end());
        for (double t : hits) {
            if (t - lastEmitted < 1e-6) continue;
            const double actualOrb = orb(relative(t), aspect);
            if (std::abs(actualOrb - (exact ? 0.0 : limit)) > 1e-5) continue;
            int kind = 0; // exact / entry / exit
            if (!exact) {
                const double before = orb(relative(t - 1e-4), aspect) - limit;
                const double after = orb(relative(t + 1e-4), aspect) - limit;
                if ((before < 0.0) == (after < 0.0)) continue;
                kind = after < before ? 1 : 2;
            }
            sink(t, actualOrb, kind);
            lastEmitted = t;
        }
        a = b;
        previous = next;
    }
}

struct Station { double time; double longitude; bool direct; };

inline std::vector<Station> stations(const Ephemeris& body, double start, double end,
                                     const Check& check) {
    std::vector<Station> found;
    double speed = body(start).speed;
    for (double a = start; a < end;) {
        check();
        const double b = std::min(end, a + 0.5);
        const double next = body(b).speed;
        if ((speed < 0.0) != (next < 0.0)) {
            const double t = root(a, b, [&](double at) { return body(at).speed; }, check);
            if (found.empty() || t - found.back().time > 1e-5)
                found.push_back({t, wrap(body(t).longitude), next > 0.0});
        }
        a = b;
        speed = next;
    }
    return found;
}

// Near-miss natal contacts occur at longitude extrema. Require a strict local
// minimum of orb; reject exact hits and extrema that turn away from the target.
inline void closest(const Ephemeris& body, const std::vector<Station>& turningPoints,
                    double target, double aspect, double limit,
                    const Check& check, const EventSink& sink) {
    for (const auto& station : turningPoints) {
        check();
        const double t = station.time;
        const double value = orb(station.longitude - target, aspect);
        const double before = orb(body(t - 0.1).longitude - target, aspect);
        const double after = orb(body(t + 0.1).longitude - target, aspect);
        if (value > 1e-5 && value <= limit && value < before && value < after)
            sink(t, value, 0);
    }
}

// A retrograde cycle runs from a retrograde station to the following direct
// station. Pre-shadow crosses the DIRECT station degree before the cycle;
// post-shadow crosses the RETROGRADE station degree after the cycle.
inline void shadows(const Ephemeris& body, double start, double end,
                    const Check& check, const EventSink& sink) {
    constexpr double contextDays = 800.0;
    const double scanStart = start - contextDays;
    const double scanEnd = end + contextDays;
    const auto turns = stations(body, scanStart, scanEnd, check);
    auto crossing = [&](double a, double b, double target, int kind) {
        // Only inspect the adjacent direct-motion leg, and only the requested
        // date interval. Scan from a station offset to avoid its tangent root.
        a = std::max(start, a + 1e-4);
        b = std::min(end, b - 1e-4);
        if (a >= b) return;
        const Ephemeris fixed = [target](double) { return Position{target, 0.0}; };
        aspects(body, fixed, a, b, 0.0, 0.0, true, check,
            [&](double t, double, int) {
                if (body(t).speed > 0.0) sink(t, target, kind);
            });
    };
    for (size_t i = 0; i + 1 < turns.size(); ++i) {
        check();
        if (turns[i].direct || !turns[i + 1].direct) continue;
        crossing(i == 0 ? scanStart : turns[i - 1].time,
                 turns[i].time, turns[i + 1].longitude, 1);
        crossing(turns[i + 1].time,
                 i + 2 < turns.size() ? turns[i + 2].time : scanEnd,
                 turns[i].longitude, 2);
    }
}

} // namespace dracoved::search_events
