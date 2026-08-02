#pragma once

#include "chart_types.h"

#include <QVector>

namespace dracoved {

class SwissEph;

struct CalculatedLunarNode {
    LunarNodeType type = LunarNodeType::Mean;
    double northLongitude = 0.0;
    double speed = 0.0;
    bool hasSpeed = false;
    bool retrograde = false;
};

int swissBodyIdForLunarNode(LunarNodeType type);

bool calculateLunarNodes(const SwissEph& swe,
                         double julianDay,
                         int calculationFlags,
                         const LunarNodePolicy& policy,
                         QVector<CalculatedLunarNode>* outNodes,
                         QString* error);

QString internalNorthNodeName(LunarNodeType type, const LunarNodePolicy& policy);
QString internalSouthNodeName(LunarNodeType type, const LunarNodePolicy& policy);

}  // namespace dracoved
