#include "lunation_degree_groups.h"
#include <iostream>
#include <stdexcept>
using namespace dracoved::lunation_degrees;
int main() {
    auto check = [](bool ok) { if (!ok) throw std::runtime_error("Grouping regression"); };
    check(groupEnds({}, 1).empty());
    check(groupEnds({1, 1, 1}, 0) == std::vector<int>{3});
    check(groupEnds({1, 1.5, 2, 2.5}, 1) == std::vector<int>({3, 4}));
    check(groupEnds({0, 0.9, 1.8}, 1) == std::vector<int>({2, 3}));
    check(groupEnds({0.1, 29.9}, 1) == std::vector<int>({1, 2}));
    check(groupEnds({1, 1.25, 1.250001}, .25) == std::vector<int>({2, 3}));
    check(roundedArcsecond(1 + .49/3600) == 3600);
    check(roundedArcsecond(1 + .51/3600) == 3601);
    std::cout << "PASS: group boundaries, no chaining, no wrap, duplicates, arcsecond rounding\n";
}
