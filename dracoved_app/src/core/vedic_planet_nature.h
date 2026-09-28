#pragma once
#include "chart_types.h"

namespace dracoved {
struct VedicPlanetNature {
    QString planet, natural, naturalReason, houses, functional, functionalReason;
};
// Informational natal classifications; never modifies benchmark Moorthi rules.
QVector<VedicPlanetNature> classifyVedicPlanetNatures(const NatalChart& chart, const LunarNodePolicy& nodes);
} // namespace dracoved
