#include "vedic_nakshatra.h"

#include <cassert>
#include <cmath>
#include <limits>

using namespace dracoved;

int main() {
    assert(vedicNakshatraNames().size() == 27);
    assert(classifyVedicNakshatra(std::numeric_limits<double>::quiet_NaN()).valid == false);
    assert(classifyVedicNakshatra(std::numeric_limits<double>::infinity()).valid == false);
    assert(classifyVedicNakshatra(-0.0).name == "Ashwini");
    assert(classifyVedicNakshatra(-360.0).name == "Ashwini");
    assert(classifyVedicNakshatra(360.0).name == "Ashwini");
    assert(classifyVedicNakshatra(std::nextafter(0.0, -std::numeric_limits<double>::infinity())).name == "Revati");
    assert(classifyVedicNakshatra(std::nextafter(360.0, 0.0)).name == "Revati");

    // Exercise every canonical pada boundary and the representable values on
    // either side. Expected indices come from the boundary ordinal, rather
    // than a second classifier implementation.
    for (int ordinal = 0; ordinal < 108; ++ordinal) {
        const double boundary = (ordinal * 10.0) / 3.0;
        const auto exact = classifyVedicNakshatra(boundary);
        assert(exact.valid);
        assert(exact.index == ordinal / 4);
        assert(exact.pada == ordinal % 4 + 1);
        const auto above = classifyVedicNakshatra(std::nextafter(boundary, std::numeric_limits<double>::infinity()));
        assert(above.valid);
        assert(above.index == ordinal / 4);
        assert(above.pada == ordinal % 4 + 1);
        if (ordinal > 0) {
            const auto below = classifyVedicNakshatra(std::nextafter(boundary, -std::numeric_limits<double>::infinity()));
            assert(below.valid);
            assert((below.index * 4 + below.pada - 1) == ordinal - 1);
        }
    }

    // Independent Lahiri fixture from sweph/bin/swetest64.exe, Dhaka
    // 1999-01-04 16:01 local (see vedic_nakshatra_test.ps1).
    const auto sun = classifyVedicNakshatra(259.7469235);
    assert(sun.name == "Purva Ashadha" && sun.pada == 2 && sun.lord == "Venus");
    const auto moon = classifyVedicNakshatra(109.6409818);
    assert(moon.name == "Ashlesha" && moon.pada == 1 && moon.lord == "Mercury");
    const auto asc = classifyVedicNakshatra(61.7315428);
    assert(asc.name == "Mrigashira" && asc.pada == 3 && asc.lord == "Mars");
    const auto mercury = classifyVedicNakshatra(242.2778507);
    assert(mercury.name == "Mula" && mercury.pada == 1 && mercury.lord == "Ketu");
    const auto venus = classifyVedicNakshatra(275.8250891);
    assert(venus.name == "Uttara Ashadha" && venus.pada == 3 && venus.lord == "Sun");
    const auto mars = classifyVedicNakshatra(176.2187029);
    assert(mars.name == "Chitra" && mars.pada == 1 && mars.lord == "Mars");
    const auto jupiter = classifyVedicNakshatra(328.6135904);
    assert(jupiter.name == "Purva Bhadrapada" && jupiter.pada == 3 && jupiter.lord == "Jupiter");
    const auto saturn = classifyVedicNakshatra(2.9580111);
    assert(saturn.name == "Ashwini" && saturn.pada == 1 && saturn.lord == "Ketu");
    const auto meanNode = classifyVedicNakshatra(120.3749112);
    assert(meanNode.name == "Magha" && meanNode.pada == 1 && meanNode.lord == "Ketu");
    const auto trueNode = classifyVedicNakshatra(118.7714546);
    assert(trueNode.name == "Ashlesha" && trueNode.pada == 4 && trueNode.lord == "Mercury");
    return 0;
}
