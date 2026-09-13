#pragma once

#include "search_event_algorithms.h"
#include <limits>

namespace dracoved::solar_transits {

using search_events::Ephemeris;
using search_events::Check;

enum class Kind { Exact, Entry, Exit, NearMiss, StartProximity, EndApproach, Station };
struct Contact { double time; double orb; Kind kind; double speed; };
struct Window { double start; double end; bool clippedStart; bool clippedEnd; };
struct Analysis {
    std::vector<Contact> contacts;
    std::vector<Window> windows;
};

// Fixed return coordinates: half-open year [start,end). EndApproach is an
// explicitly labelled boundary measurement at end, not an exact hit in-year.
inline Analysis analyze(const Ephemeris& body, double target, double aspect,
                        double start, double end, double limit, const Check& check) {
    Analysis result;
    if (!(start < end) || limit <= 0) return result;
    constexpr double exactTolerance = 1e-6;
    constexpr double timeTolerance = 1e-6; // 0.0864 seconds
    auto value = [&](double t) { return search_events::orb(body(t).longitude - target, aspect); };
    auto add = [&](double t, Kind kind) {
        for (const auto& hit : result.contacts)
            if (hit.kind == kind && std::abs(hit.time - t) < timeTolerance) return;
        result.contacts.push_back({t, value(t), kind, body(t).speed});
    };
    const Ephemeris fixed = [target](double) { return search_events::Position{target, 0}; };
    search_events::aspects(body, fixed, start, end, aspect, 0, true, check,
        [&](double t, double, int) {
            if (t < end - timeTolerance) add(std::max(start, t), Kind::Exact);
        });
    if (value(start) <= exactTolerance) add(start, Kind::Exact);

    // All boundaries are refined independently, even for very short windows.
    std::vector<double> cuts{start, end};
    search_events::aspects(body, fixed, start, end, aspect, limit, false, check,
        [&](double t, double, int kind) {
            if (t > start + timeTolerance && t < end - timeTolerance) cuts.push_back(t);
            if (t >= start && t < end - timeTolerance) add(t, kind == 1 ? Kind::Entry : Kind::Exit);
        });
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double a, double b) {
        return std::abs(a - b) < timeTolerance;
    }), cuts.end());
    for (size_t i = 1; i < cuts.size(); ++i) {
        check();
        if (value((cuts[i - 1] + cuts[i]) / 2) <= limit) {
            if (!result.windows.empty() && std::abs(result.windows.back().end - cuts[i - 1]) < timeTolerance)
                result.windows.back().end = cuts[i];
            else result.windows.push_back({cuts[i - 1], cuts[i], false, false});
        }
    }
    for (auto& window : result.windows) {
        window.clippedStart = std::abs(window.start - start) < timeTolerance && value(start - 1e-4) <= limit;
        window.clippedEnd = std::abs(window.end - end) < timeTolerance && value(end + 1e-4) <= limit;
    }

    const auto turns = search_events::stations(body, start, end, check);
    for (const auto& station : turns) {
        check();
        if (station.time >= end - timeTolerance) continue;
        const double orb = value(station.time);
        if (orb > limit) continue;
        add(station.time, Kind::Station);
        if (orb <= exactTolerance) {
            // A tangent exact contact need not change the sign of longitude.
            add(station.time, Kind::Exact);
        } else if (orb < value(station.time - 0.01) && orb < value(station.time + 0.01)) {
            add(station.time, Kind::NearMiss);
        }
    }
    // An endpoint is not a near miss: the turn could lie outside this year.
    if (value(start) > exactTolerance && value(start) <= limit)
        add(start, Kind::StartProximity);
    if (value(end) <= limit && value(end) > exactTolerance
        && value(end) < value(end - 0.01)) add(end, Kind::EndApproach);

    std::sort(result.contacts.begin(), result.contacts.end(), [](const Contact& a, const Contact& b) {
        if (a.time != b.time) return a.time < b.time;
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    return result;
}

} // namespace dracoved::solar_transits
