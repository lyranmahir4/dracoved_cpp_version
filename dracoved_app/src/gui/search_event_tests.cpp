// Standalone tests: see SEARCH_TESTING.md. Does not start the application.
#include "search_event_algorithms.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace dracoved::search_events;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Hit { double time; double value; int kind; };

int main() {
    try {
        const Check check = [] {};
        std::vector<Hit> hits;
        const EventSink collect = [&](double t, double v, int k) { hits.push_back({t, v, k}); };
        const Ephemeris fixed = [](double) { return Position{0, 0}; };
        const Ephemeris moving = [](double t) { return Position{wrap(359.0 + 20.0 * t), 20}; };
        aspects(moving, fixed, 0, 1, 0, 0, true, check, collect);
        require(hits.size() == 1 && std::abs(hits[0].time - 0.05) < 1e-6, "Conjunction across zodiac zero");
        hits.clear();
        aspects(moving, fixed, 0, 0.1, 0, 0.1, false, check, collect);
        require(hits.size() == 2 && hits[0].kind == 1 && hits[1].kind == 2,
                "Both boundaries of narrow orb window");
        hits.clear();
        const Ephemeris opposition = [](double t) { return Position{179 + 2*t, 2}; };
        aspects(opposition, fixed, 0, 1, 180, 0, true, check, collect);
        require(hits.size() == 1 && std::abs(hits[0].time - 0.5) < 1e-6, "Opposition root");
        hits.clear();
        aspects(opposition, fixed, 0, 1, 0, 0, true, check, collect);
        require(hits.empty(), "Reject branch-cut false conjunction");
        const Ephemeris closeTurn = [](double t) {
            return Position{wrap(0.0025 - (t - 0.125)*(t - 0.125)), -2*(t - 0.125)};
        };
        aspects(closeTurn, fixed, 0, 0.25, 0, 0, true, check, collect);
        require(hits.size() == 2, "Two exact contacts around a relative-motion reversal");
        hits.clear();
        const Ephemeris turn = [](double t) { return Position{100 - (t - 1)*(t - 1), -2*(t - 1)}; };
        const auto turns = stations(turn, 0, 2, check);
        require(turns.size() == 1 && !turns[0].direct, "Retrograde turning point");
        closest(turn, turns, 100.5, 0, 1, check, collect);
        require(hits.size() == 1 && std::abs(hits[0].value - 0.5) < 1e-6, "Near miss found");
        hits.clear();
        closest(turn, turns, 100, 0, 1, check, collect);
        closest(turn, turns, 99.5, 0, 1, check, collect);
        closest(turn, turns, 102, 0, 1, check, collect);
        require(hits.empty(), "Reject exact, local maximum, and out-of-orb contacts");
        bool cancelled = false;
        try { aspects(moving, fixed, 0, 100, 0, 0, true, [] { throw 42; }, collect); }
        catch (int) { cancelled = true; }
        require(cancelled, "Cancellation propagates");

        // Validate actual planetary cycles against the packaged ephemeris.
        HMODULE dll = LoadLibraryW(L"swedll64.dll");
        require(dll != nullptr, "Swiss Ephemeris DLL unavailable (run from project root)");
        const auto setPath = reinterpret_cast<void (*)(const char*)>(GetProcAddress(dll, "swe_set_ephe_path"));
        const auto calc = reinterpret_cast<int (*)(double,int,int,double*,char*)>(GetProcAddress(dll, "swe_calc_ut"));
        const auto setSidereal = reinterpret_cast<void (*)(int,double,double)>(GetProcAddress(dll, "swe_set_sid_mode"));
        require(setPath && calc && setSidereal, "Swiss API available");
        setPath("ephe");
        auto body = [&](int id, int flags = 0) -> Ephemeris {
            return [=](double t) {
                double values[6] = {}; char error[256] = {};
                if (calc(t,id,flags | 256,values,error) < 0) throw std::runtime_error(error);
                return Position{values[0],values[3]};
            };
        };
        const double start = 2461041.5; // 2026-01-01 UTC
        const double end = start + 365;
        const auto mercury = body(2);
        hits.clear();
        shadows(mercury, start, end, check, collect);
        require(hits.size() == 6, "2026 Mercury has three shadow entries and three exits");
        const auto actualTurns = stations(mercury, start - 800, end + 800, check);
        for (const auto& hit : hits) {
            require(hit.time >= start && hit.time <= end, "Shadow respects requested range");
            require(mercury(hit.time).speed > 0, "Shadow crossing is direct");
            require(std::abs(signedAngle(mercury(hit.time).longitude - hit.value)) < 1e-5, "Shadow degree refined");
            bool paired = false;
            for (size_t i = 0; i + 1 < actualTurns.size(); ++i) {
                const auto& r = actualTurns[i]; const auto& d = actualTurns[i+1];
                if (r.direct || !d.direct) continue;
                if (hit.kind == 1 && hit.time < r.time && r.time - hit.time < 100
                    && std::abs(signedAngle(hit.value - d.longitude)) < 1e-5) paired = true;
                if (hit.kind == 2 && hit.time > d.time && hit.time - d.time < 100
                    && std::abs(signedAngle(hit.value - r.longitude)) < 1e-5) paired = true;
            }
            require(paired, "Correct station defines each shadow boundary");
        }
        const auto firstShadow = hits.front();
        hits.clear();
        shadows(mercury, firstShadow.time - 0.1, firstShadow.time + 0.1, check, collect);
        require(hits.size() == 1 && std::abs(hits[0].time - firstShadow.time) < 1e-5,
                "Narrow range finds shadow with stations outside range");
        for (int flags : {0, 65536}) {
            setSidereal(1, 0, 0);
            hits.clear();
            const auto moon = body(1, flags); const auto sun = body(0, flags);
            aspects(moon, sun, start, start + 40, 0, 0, true, check, collect);
            require(!hits.empty(), "Moving Moon-Sun conjunctions");
            for (const auto& hit : hits)
                require(orb(moon(hit.time).longitude-sun(hit.time).longitude,0) < 1e-5,
                        "Both moving positions refined in selected zodiac");
        }
        std::cout << "PASS: synthetic boundaries, near misses, cancellation, Mercury shadows, tropical/sidereal moving aspects\n";
        FreeLibrary(dll);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
