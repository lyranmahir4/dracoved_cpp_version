#include "lunar_nodes.h"

#include "formatting.h"
#include "swiss_eph.h"

#include <cmath>

namespace dracoved {

int swissBodyIdForLunarNode(LunarNodeType type) {
    return type == LunarNodeType::True ? SE_TRUE_NODE : SE_MEAN_NODE;
}

QString internalNorthNodeName(LunarNodeType type, const LunarNodePolicy& policy) {
    return type == effectivePrimaryNodeType(policy)
        ? QString("North Node")
        : explicitLunarNodeName(true, type);
}

QString internalSouthNodeName(LunarNodeType type, const LunarNodePolicy& policy) {
    return type == effectivePrimaryNodeType(policy)
        ? QString("South Node")
        : explicitLunarNodeName(false, type);
}

bool calculateLunarNodes(const SwissEph& swe,
                         double julianDay,
                         int calculationFlags,
                         const LunarNodePolicy& policy,
                         QVector<CalculatedLunarNode>* outNodes,
                         QString* error) {
    if (!outNodes) {
        if (error) {
            *error = "Node calculation output is unavailable.";
        }
        return false;
    }

    outNodes->clear();
    const LunarNodeType primary = effectivePrimaryNodeType(policy);
    QVector<LunarNodeType> requested;
    requested.push_back(primary);
    if (policy.mode == LunarNodeMode::Both) {
        requested.push_back(primary == LunarNodeType::Mean
                                ? LunarNodeType::True
                                : LunarNodeType::Mean);
    }

    for (const LunarNodeType type : requested) {
        double values[6] = {0.0};
        QString calculationError;
        if (!swe.calcUtFull(julianDay, swissBodyIdForLunarNode(type),
                            calculationFlags | SEFLG_SPEED, values, &calculationError)) {
            if (error) {
                *error = QString("Failed to compute %1 North Node: %2")
                    .arg(lunarNodeTypeToString(type), calculationError);
            }
            outNodes->clear();
            return false;
        }

        CalculatedLunarNode node;
        node.type = type;
        node.northLongitude = normalizeDegrees(values[0]);
        node.speed = values[3];
        node.hasSpeed = true;
        if (type == LunarNodeType::Mean) {
            node.speed = -std::abs(node.speed);
            node.retrograde = true;
        } else {
            node.retrograde = node.speed < 0.0;
        }
        outNodes->push_back(node);
    }
    return true;
}

}  // namespace dracoved
