#include "solar_transit_algorithms.h"
#include <iostream>
#include <stdexcept>
using namespace dracoved::solar_transits;
using dracoved::search_events::Position;
using dracoved::search_events::wrap;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static int count(const Analysis& data, Kind kind) {
    int result = 0; for (const auto& event : data.contacts) if (event.kind == kind) ++result; return result;
}
int main() {
    try {
        const Check check = [] {};
        auto linear = [](double t) { return Position{wrap(359 + 2*t), 2}; };
        auto data = analyze(linear, 0, 0, 0, 1, .25, check);
        require(count(data, Kind::Exact) == 1, "One zodiac-zero crossing");
        require(data.windows.size() == 1 && std::abs(data.windows[0].start - .375) < 1e-6
            && std::abs(data.windows[0].end - .625) < 1e-6, "Refined orb interval");
        require(count(data, Kind::Entry) == 1 && count(data, Kind::Exit) == 1, "Entry and exit dates");
        auto turn = [](double t) { return Position{100 - (t-1)*(t-1), -2*(t-1)}; };
        data = analyze(turn, 100.5, 0, 0, 2, 1, check);
        require(count(data, Kind::NearMiss) == 1 && count(data, Kind::Exact) == 0, "Non-exact turning point");
        require(count(data, Kind::Station) == 1, "Station marked within orb");
        data = analyze(turn, 100, 0, 0, 2, 1, check);
        require(count(data, Kind::Exact) == 1 && count(data, Kind::NearMiss) == 0, "Tangent exact is not a near miss");
        auto slow = [](double t) { return Position{10 + .1*t, .1}; };
        data = analyze(slow, 11, 0, 0, 5, 1.5, check);
        require(count(data, Kind::Exact) == 0 && count(data, Kind::NearMiss) == 0
            && count(data, Kind::EndApproach) == 1, "Year-end approach distinguished from near miss");
        require(data.windows.size() == 1 && data.windows[0].clippedStart && data.windows[0].clippedEnd,
            "Slow planet active all year");
        data = analyze(slow, 30, 0, 0, 5, 1, check);
        require(data.contacts.empty() && data.windows.empty(), "No fabricated dates outside orb");
        data = analyze(linear, 0, 0, 0, .5, 1, check);
        require(count(data, Kind::Exact) == 0, "Next-return boundary excluded");
        auto repeated = [](double t) { return Position{wrap((t-.5)*(t-1.5)*(t-2.5)), 3*t*t-9*t+5.75}; };
        data = analyze(repeated, 0, 0, 0, 3, 2, check);
        require(count(data, Kind::Exact) == 3, "Direct-retrograde-direct passes retained");
        auto opposition = [](double t) { return Position{179 + 2*t, 2}; };
        data = analyze(opposition, 0, 180, 0, 1, .1, check);
        require(count(data, Kind::Exact) == 1 && data.windows.size() == 1, "Opposition and narrow orb");
        bool cancelled = false;
        try { analyze(linear, 0, 0, 0, 1, 1, [] { throw 42; }); } catch (int) { cancelled = true; }
        require(cancelled, "Cancellation propagates");
        std::cout << "PASS: exact/wrap, orb periods, near miss, tangent hit, slow year, boundary, repeated passes, opposition, cancellation\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
