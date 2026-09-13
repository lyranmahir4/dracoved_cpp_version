#pragma once
#include <cmath>
#include <vector>

namespace dracoved::lunation_degrees {

inline long long roundedArcsecond(double degree) { return std::llround(degree * 3600.0); }

// Input is sorted degrees within a sign (0 <= degree < 30). Each event
// belongs to one group. Start at the lowest unused degree and include all
// subsequent degrees within the maximum total spread. No chain linking or
// circular 29/0 merging: these are distinct degree-in-sign coordinates.
inline std::vector<int> groupEnds(const std::vector<double>& degrees, double spread) {
    std::vector<int> ends;
    for (int start = 0; start < static_cast<int>(degrees.size());) {
        int end = start + 1;
        while (end < static_cast<int>(degrees.size()) && degrees[end] - degrees[start] <= spread + 1e-10) ++end;
        ends.push_back(end); // exclusive
        start = end;
    }
    return ends;
}
}
