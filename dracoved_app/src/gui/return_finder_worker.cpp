#include "return_finder_worker.h"

#include "../core/formatting.h"
#include "../core/timezone_utils.h"
#include "../core/tropical_natal.h"
#include "return_calculation_service.h"
#include "transit_calc_service.h"

#include <QHash>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>

namespace dracoved {

namespace {

struct ResolvedPoint {
    QString label;
    QString name;
    double longitude = 0.0;
    int sourceHouse = 0;
};

QString houseRulerForSign(int sign, bool modern) {
    static const QStringList traditional = {
        "Mars", "Venus", "Mercury", "Moon", "Sun", "Mercury",
        "Venus", "Mars", "Jupiter", "Saturn", "Saturn", "Jupiter",
    };
    static const QStringList contemporary = {
        "Mars", "Venus", "Mercury", "Moon", "Sun", "Mercury",
        "Venus", "Pluto", "Jupiter", "Saturn", "Uranus", "Neptune",
    };
    if (sign < 0 || sign >= 12) {
        return {};
    }
    return modern ? contemporary[sign] : traditional[sign];
}

bool bodyLongitude(const NatalChart& chart, const QString& name, double* longitude) {
    return transitcalc::findBodyLongitude(chart, name, longitude);
}

QVector<HouseSystem> systemsForMode(ReturnFinderHouseMode mode) {
    if (mode == ReturnFinderHouseMode::WholeSign) {
        return {HouseSystem::WholeSign};
    }
    if (mode == ReturnFinderHouseMode::Placidus) {
        return {HouseSystem::Placidus};
    }
    return {HouseSystem::WholeSign, HouseSystem::Placidus};
}

bool combineSystemMatches(ReturnFinderHouseMode mode, const QVector<bool>& matches) {
    if (matches.isEmpty()) {
        return false;
    }
    if (mode == ReturnFinderHouseMode::BothAnd) {
        return std::all_of(matches.begin(), matches.end(), [](bool value) { return value; });
    }
    return std::any_of(matches.begin(), matches.end(), [](bool value) { return value; });
}

QString systemMatchLabel(ReturnFinderHouseMode mode, const QVector<bool>& matches) {
    const bool whole = !matches.isEmpty() && matches[0];
    if (mode == ReturnFinderHouseMode::WholeSign) {
        return whole ? "Whole Sign" : "Whole Sign did not match";
    }
    if (mode == ReturnFinderHouseMode::Placidus) {
        return whole ? "Placidus" : "Placidus did not match";
    }
    const bool placidus = matches.size() > 1 && matches[1];
    if (whole && placidus) return "Whole Sign and Placidus matched";
    if (whole) return "Whole Sign matched; Placidus did not match";
    if (placidus) return "Placidus matched; Whole Sign did not match";
    return "Whole Sign and Placidus did not match";
}

int houseForLongitude(const NatalChart& chart, double longitude, HouseSystem system) {
    if (system == HouseSystem::Placidus && chart.cusps.size() != 12) {
        return 0;
    }
    return transitcalc::calcHouseForLongitude(longitude, chart.cusps, chart.angles.asc, system);
}

QVector<int> normalizedHouses(const QVector<int>& houses) {
    QVector<int> normalized;
    QSet<int> seen;
    for (int house : houses) {
        if (house >= 1 && house <= 12 && !seen.contains(house)) {
            seen.insert(house);
            normalized.push_back(house);
        }
    }
    return normalized;
}

QVector<ResolvedPoint> resolveHouseLords(const NatalChart& chart,
                                         const QVector<int>& requestedHouses,
                                         HouseSystem system,
                                         bool modern) {
    QVector<ResolvedPoint> resolved;
    const QVector<int> houses = normalizedHouses(requestedHouses);
    QHash<QString, QVector<int>> housesByRuler;
    QStringList rulerOrder;
    for (int house : houses) {
        int sign = -1;
        if (system == HouseSystem::WholeSign) {
            sign = (signIndex(chart.angles.asc) + house - 1) % 12;
        } else if (chart.cusps.size() == 12) {
            sign = signIndex(chart.cusps[house - 1].longitude);
        }
        const QString ruler = houseRulerForSign(sign, modern);
        if (ruler.isEmpty()) {
            continue;
        }
        if (!housesByRuler.contains(ruler)) {
            rulerOrder.push_back(ruler);
        }
        housesByRuler[ruler].push_back(house);
    }
    for (const QString& ruler : rulerOrder) {
        double longitude = 0.0;
        if (!bodyLongitude(chart, ruler, &longitude)) {
            continue;
        }
        QStringList labels;
        const QVector<int> rulerHouses = housesByRuler.value(ruler);
        for (int house : rulerHouses) {
            labels.push_back(QString::number(house));
        }
        resolved.push_back({
            QString("%1H lord %2").arg(labels.join("/"), ruler),
            ruler,
            longitude,
            rulerHouses.isEmpty() ? 0 : rulerHouses.front(),
        });
    }
    return resolved;
}

QString profectionLord(const ReturnFinderQuery& query, const QDateTime& returnLocal) {
    const int birthYear = query.natalChart.localDateTime.isValid()
        ? query.natalChart.localDateTime.date().year()
        : query.natalInput.date.year();
    const int age = std::max(0, returnLocal.date().year() - birthYear);
    const int ageMod = ((age % 12) + 12) % 12;
    const int profectedSign = (signIndex(query.natalChart.angles.asc) + ageMod) % 12;
    return houseRulerForSign(profectedSign, query.modernRulership);
}

QVector<ResolvedPoint> resolveTarget(const ReturnFinderTarget& target,
                                     const ReturnFinderQuery& query,
                                     const NatalChart& returnChart,
                                     const QDateTime& returnLocal,
                                     HouseSystem system) {
    const NatalChart& scopedChart = target.scope == ReturnFinderScope::Natal
        ? query.natalChart : returnChart;
    if (target.kind == ReturnFinderTargetKind::HouseLord) {
        return resolveHouseLords(scopedChart, target.houses, system, query.modernRulership);
    }
    if (target.kind == ReturnFinderTargetKind::ProfectionLord) {
        const QString lord = profectionLord(query, returnLocal);
        double longitude = 0.0;
        const NatalChart& positionChart = target.scope == ReturnFinderScope::Natal
            ? query.natalChart : returnChart;
        if (!lord.isEmpty() && bodyLongitude(positionChart, lord, &longitude)) {
            return {{QString("Profection lord %1").arg(lord), lord, longitude, 0}};
        }
        return {};
    }

    const QString scopeLabel = target.scope == ReturnFinderScope::Natal ? "Natal" : "Return";
    if (target.kind == ReturnFinderTargetKind::Angle && target.name == "Any Angle") {
        QVector<ResolvedPoint> angles;
        for (const QString& angle : QStringList{"Ascendant", "Descendant", "Midheaven", "IC"}) {
            double longitude = 0.0;
            if (transitcalc::findAngleLongitude(scopedChart, angle, &longitude)) {
                angles.push_back({QString("%1 %2").arg(scopeLabel, angle), angle, longitude, 0});
            }
        }
        return angles;
    }

    double longitude = 0.0;
    if (target.kind == ReturnFinderTargetKind::Angle) {
        if (!transitcalc::findAngleLongitude(scopedChart, target.name, &longitude)) {
            return {};
        }
    } else if (!bodyLongitude(scopedChart, target.name, &longitude)) {
        return {};
    }
    const QString displayName = lunarNodeDisplayName(target.name, scopedChart.lunarNodePolicy);
    return {{QString("%1 %2").arg(scopeLabel, displayName), target.name, longitude, 0}};
}

struct AspectMatch {
    bool matched = false;
    QString aspect;
    double orb = std::numeric_limits<double>::infinity();
};

AspectMatch matchAspect(double a, double b, ReturnFinderAspect requested, double limit) {
    const double separation = transitcalc::angularDiffAbs(a, b);
    AspectMatch best;
    const QVector<ReturnFinderAspect> aspects = requested == ReturnFinderAspect::AnyMajor
        ? QVector<ReturnFinderAspect>{ReturnFinderAspect::Conjunction, ReturnFinderAspect::Sextile,
                                      ReturnFinderAspect::Square, ReturnFinderAspect::Trine,
                                      ReturnFinderAspect::Opposition}
        : QVector<ReturnFinderAspect>{requested};
    for (ReturnFinderAspect aspect : aspects) {
        const double orb = std::fabs(separation - returnFinderAspectAngle(aspect));
        if (orb < best.orb) {
            best.orb = orb;
            best.aspect = returnFinderAspectLabel(aspect);
        }
    }
    best.matched = best.orb <= limit + 1e-9;
    return best;
}

QString placementTargetLabel(const ReturnFinderCondition& condition) {
    if (condition.placementKind == ReturnFinderPlacementKind::Sign) {
        return signName(std::clamp(condition.targetSign, 0, 11));
    }
    return QString("House %1").arg(condition.targetHouse);
}

ReturnFinderConditionEvaluation evaluatePlanetPlacement(const ReturnFinderCondition& condition,
                                                         const ReturnFinderQuery& query,
                                                         const NatalChart& returnChart,
                                                         const QDateTime& returnLocal) {
    ReturnFinderConditionEvaluation evaluation;
    evaluation.conditionId = condition.id;
    evaluation.excluded = condition.exclude;
    double longitude = 0.0;
    if (!bodyLongitude(returnChart, condition.subject.name, &longitude)) {
        evaluation.description = QString("Return %1 is unavailable.")
            .arg(lunarNodeDisplayName(condition.subject.name, returnChart.lunarNodePolicy));
        return evaluation;
    }
    const QString subjectDisplayName = lunarNodeDisplayName(
        condition.subject.name, returnChart.lunarNodePolicy);
    evaluation.resolvedSubject = QString("Return %1").arg(subjectDisplayName);
    const bool requirePlacement = condition.planetPlacementEnabled;
    const bool requireAngle = condition.planetAngleContactEnabled;
    if (!requirePlacement && !requireAngle) {
        evaluation.description = "Planet Placement has no active placement or angle requirement.";
        return evaluation;
    }

    bool placementMatched = true;
    QString placementDescription;
    if (requirePlacement) {
        if (condition.placementKind == ReturnFinderPlacementKind::Sign) {
            placementMatched = signIndex(longitude) == std::clamp(condition.targetSign, 0, 11);
            placementDescription = QString("%1 in %2")
                .arg(placementMatched ? "is" : "is not", placementTargetLabel(condition));
        } else {
            QVector<bool> systemMatches;
            for (HouseSystem system : systemsForMode(query.houseMode)) {
                const int house = houseForLongitude(returnChart, longitude, system);
                systemMatches.push_back(house == condition.targetHouse);
            }
            placementMatched = combineSystemMatches(query.houseMode, systemMatches);
            evaluation.systemLabel = systemMatchLabel(query.houseMode, systemMatches);
            placementDescription = QString("%1 in House %2%3")
                .arg(placementMatched ? "is" : "is not")
                .arg(condition.targetHouse)
                .arg(evaluation.systemLabel.isEmpty()
                         ? QString()
                         : QString(" (%1)").arg(evaluation.systemLabel));
        }
    }

    bool angleMatched = true;
    QString angleDescription;
    if (requireAngle) {
        angleMatched = false;
        double closest = std::numeric_limits<double>::infinity();
        QString closestTarget;
        if (condition.target.kind == ReturnFinderTargetKind::Angle) {
            const auto targets = resolveTarget(condition.target, query, returnChart,
                                               returnLocal, HouseSystem::WholeSign);
            for (const auto& target : targets) {
                const AspectMatch match = matchAspect(
                    longitude, target.longitude, ReturnFinderAspect::Conjunction, condition.orb);
                if (match.orb < closest) {
                    closest = match.orb;
                    closestTarget = target.label;
                    angleMatched = match.matched;
                }
            }
        }
        if (std::isfinite(closest)) {
            evaluation.orb = closest;
            evaluation.aspectLabel = returnFinderAspectLabel(ReturnFinderAspect::Conjunction);
            evaluation.resolvedTarget = closestTarget;
            angleDescription = QString("%1 conjunct %2 - orb %3 deg")
                .arg(angleMatched ? "is" : "is not", closestTarget)
                .arg(closest, 0, 'f', 2);
        } else {
            angleDescription = "has an unavailable angle target";
        }
    }

    evaluation.matched = placementMatched && angleMatched;
    QStringList requirements;
    if (requirePlacement) requirements.push_back(placementDescription);
    if (requireAngle) requirements.push_back(angleDescription);
    evaluation.description = QString("Return %1 %2")
        .arg(subjectDisplayName, requirements.join(" and "));
    return evaluation;
}

ReturnFinderConditionEvaluation evaluateAspect(const ReturnFinderCondition& condition,
                                                const ReturnFinderQuery& query,
                                                const NatalChart& returnChart,
                                                const QDateTime& returnLocal) {
    ReturnFinderConditionEvaluation evaluation;
    evaluation.conditionId = condition.id;
    evaluation.excluded = condition.exclude;
    const bool systemDependent = condition.subject.kind == ReturnFinderTargetKind::HouseLord
        || condition.target.kind == ReturnFinderTargetKind::HouseLord;
    const QVector<HouseSystem> systems = systemDependent
        ? systemsForMode(query.houseMode) : QVector<HouseSystem>{HouseSystem::WholeSign};
    QVector<bool> systemMatches;
    QString bestDescription;
    double closest = std::numeric_limits<double>::infinity();
    QString bestAspect;
    QString bestSubject;
    QString bestTarget;
    bool resolvedTargets = false;
    bool comparedDistinctPoints = false;

    for (HouseSystem system : systems) {
        const auto subjects = resolveTarget(condition.subject, query, returnChart, returnLocal, system);
        const auto targets = resolveTarget(condition.target, query, returnChart, returnLocal, system);
        resolvedTargets = resolvedTargets || (!subjects.isEmpty() && !targets.isEmpty());
        QVector<bool> subjectMatches(subjects.size(), false);
        QVector<bool> targetMatches(targets.size(), false);
        for (int targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
            const auto& target = targets[targetIndex];
            for (int subjectIndex = 0; subjectIndex < subjects.size(); ++subjectIndex) {
                const auto& subject = subjects[subjectIndex];
                if (condition.subject.scope == condition.target.scope
                    && subject.name == target.name) {
                    continue;
                }
                comparedDistinctPoints = true;
                const AspectMatch aspect = matchAspect(subject.longitude, target.longitude, condition.aspect, condition.orb);
                if (aspect.orb < closest) {
                    closest = aspect.orb;
                    bestAspect = aspect.aspect;
                    bestSubject = subject.label;
                    bestTarget = target.label;
                    bestDescription = QString("%1 %2 %3 - orb %4 deg")
                        .arg(subject.label, aspect.aspect.toLower(), target.label)
                        .arg(aspect.orb, 0, 'f', 2);
                }
                if (aspect.matched) {
                    subjectMatches[subjectIndex] = true;
                    targetMatches[targetIndex] = true;
                }
            }
        }
        auto resolvedSetMatches = [](const QVector<bool>& matches, bool requireAll) {
            if (matches.isEmpty()) return false;
            return requireAll
                ? std::all_of(matches.begin(), matches.end(), [](bool value) { return value; })
                : std::any_of(matches.begin(), matches.end(), [](bool value) { return value; });
        };
        const bool allSubjects = condition.subject.kind == ReturnFinderTargetKind::HouseLord
            && condition.subject.houseLordMatch == ReturnFinderHouseLordMatch::All;
        const bool allTargets = condition.target.kind == ReturnFinderTargetKind::HouseLord
            && condition.target.houseLordMatch == ReturnFinderHouseLordMatch::All;
        const bool matchedSystem = resolvedSetMatches(subjectMatches, allSubjects)
            && resolvedSetMatches(targetMatches, allTargets);
        systemMatches.push_back(matchedSystem);
    }

    evaluation.matched = combineSystemMatches(systemDependent ? query.houseMode : ReturnFinderHouseMode::WholeSign,
                                               systemMatches);
    evaluation.systemLabel = systemDependent ? systemMatchLabel(query.houseMode, systemMatches) : QString();
    evaluation.orb = std::isfinite(closest) ? closest : -1.0;
    evaluation.aspectLabel = bestAspect;
    evaluation.resolvedSubject = bestSubject;
    evaluation.resolvedTarget = bestTarget;
    if (!bestDescription.isEmpty()) {
        evaluation.description = bestDescription;
    } else if (resolvedTargets && !comparedDistinctPoints) {
        evaluation.description = "Aspect requires two distinct points in the same Return/Natal scope.";
    } else {
        evaluation.description = "Aspect targets could not be resolved.";
    }
    if (!evaluation.matched && !evaluation.description.startsWith("Aspect targets")) {
        evaluation.description.prepend("No match: ");
    } else if (evaluation.matched && !evaluation.systemLabel.isEmpty()) {
        evaluation.description += QString(" (%1)").arg(evaluation.systemLabel);
    }
    return evaluation;
}

ReturnFinderConditionEvaluation evaluateHouseLordPlacement(const ReturnFinderCondition& condition,
                                                            const ReturnFinderQuery& query,
                                                            const NatalChart& returnChart) {
    ReturnFinderConditionEvaluation evaluation;
    evaluation.conditionId = condition.id;
    evaluation.excluded = condition.exclude;
    QVector<bool> systemMatches;
    QStringList allResolvedLords;
    QStringList matchedLords;

    for (HouseSystem system : systemsForMode(query.houseMode)) {
        const NatalChart& rulerChart = condition.houseLordScope == ReturnFinderScope::Natal
            ? query.natalChart : returnChart;
        const auto rulers = resolveHouseLords(rulerChart, condition.houseLordHouses, system, query.modernRulership);
        QVector<bool> rulerMatches;
        QSet<QString> seenRulers;
        for (const auto& ruler : rulers) {
            if (seenRulers.contains(ruler.name)) {
                continue;
            }
            seenRulers.insert(ruler.name);
            if (!allResolvedLords.contains(ruler.label)) {
                allResolvedLords.push_back(ruler.label);
            }
            double returnLongitude = 0.0;
            if (!bodyLongitude(returnChart, ruler.name, &returnLongitude)) {
                rulerMatches.push_back(false);
                continue;
            }
            bool matched = false;
            if (condition.placementKind == ReturnFinderPlacementKind::Sign) {
                matched = signIndex(returnLongitude) == std::clamp(condition.targetSign, 0, 11);
            } else {
                matched = houseForLongitude(returnChart, returnLongitude, system) == condition.targetHouse;
            }
            rulerMatches.push_back(matched);
            if (matched && !matchedLords.contains(ruler.label)) {
                matchedLords.push_back(ruler.label);
            }
        }
        bool matchedSystem = false;
        if (!rulerMatches.isEmpty()) {
            matchedSystem = condition.houseLordMatch == ReturnFinderHouseLordMatch::All
                ? std::all_of(rulerMatches.begin(), rulerMatches.end(), [](bool value) { return value; })
                : std::any_of(rulerMatches.begin(), rulerMatches.end(), [](bool value) { return value; });
        }
        systemMatches.push_back(matchedSystem);
    }

    evaluation.matched = combineSystemMatches(query.houseMode, systemMatches);
    evaluation.systemLabel = systemMatchLabel(query.houseMode, systemMatches);
    evaluation.resolvedSubject = allResolvedLords.join(", ");
    const QString source = condition.houseLordScope == ReturnFinderScope::Natal ? "Natal" : "Return";
    if (allResolvedLords.isEmpty()) {
        evaluation.description = QString("%1 house lords could not be resolved.").arg(source);
    } else {
        const QStringList describedLords = evaluation.matched ? matchedLords : allResolvedLords;
        evaluation.description = QString("%1 house lord(s) %2 %3 the Return %4 requirement%5")
            .arg(source,
                 describedLords.join(", "),
                 evaluation.matched ? "satisfy" : "do not satisfy",
                 placementTargetLabel(condition),
                 evaluation.systemLabel.isEmpty()
                     ? QString()
                     : QString(" (%1)").arg(evaluation.systemLabel));
    }
    return evaluation;
}

ReturnFinderConditionEvaluation evaluateProfectionPlacement(const ReturnFinderCondition& condition,
                                                             const ReturnFinderQuery& query,
                                                             const NatalChart& returnChart,
                                                             const QDateTime& returnLocal) {
    ReturnFinderConditionEvaluation evaluation;
    evaluation.conditionId = condition.id;
    evaluation.excluded = condition.exclude;
    const QString lord = profectionLord(query, returnLocal);
    double longitude = 0.0;
    if (lord.isEmpty() || !bodyLongitude(returnChart, lord, &longitude)) {
        evaluation.description = "Profection lord is unavailable.";
        return evaluation;
    }
    evaluation.resolvedSubject = QString("Profection lord %1").arg(lord);
    if (condition.placementKind == ReturnFinderPlacementKind::Sign) {
        evaluation.matched = signIndex(longitude) == std::clamp(condition.targetSign, 0, 11);
    } else {
        QVector<bool> matches;
        for (HouseSystem system : systemsForMode(query.houseMode)) {
            const bool matched = houseForLongitude(returnChart, longitude, system) == condition.targetHouse;
            matches.push_back(matched);
        }
        evaluation.matched = combineSystemMatches(query.houseMode, matches);
        evaluation.systemLabel = systemMatchLabel(query.houseMode, matches);
    }
    evaluation.description = QString("Profection lord %1 %2 in Return %3%4")
        .arg(lord, evaluation.matched ? "is" : "is not", placementTargetLabel(condition),
             evaluation.systemLabel.isEmpty() ? QString() : QString(" (%1)").arg(evaluation.systemLabel));
    return evaluation;
}

ReturnFinderConditionEvaluation evaluateStellium(const ReturnFinderCondition& condition,
                                                  const ReturnFinderQuery& query,
                                                  const NatalChart& returnChart) {
    static const QStringList bodies = {
        "Sun", "Moon", "Mercury", "Venus", "Mars",
        "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto",
    };
    ReturnFinderConditionEvaluation evaluation;
    evaluation.conditionId = condition.id;
    evaluation.excluded = condition.exclude;
    const int minimum = std::clamp(condition.stelliumMinimum, 2, 10);

    if (condition.stelliumBySign) {
        QVector<int> counts(12, 0);
        QVector<QStringList> names(12);
        for (const QString& body : bodies) {
            double longitude = 0.0;
            if (bodyLongitude(returnChart, body, &longitude)) {
                const int sign = signIndex(longitude);
                ++counts[sign];
                names[sign].push_back(body);
            }
        }
        int target = std::clamp(condition.stelliumTarget, 0, 11);
        if (condition.stelliumAny) {
            target = static_cast<int>(std::distance(counts.begin(), std::max_element(counts.begin(), counts.end())));
        }
        evaluation.matched = counts[target] >= minimum;
        evaluation.description = evaluation.matched
            ? QString("%1-planet stellium in %2: %3")
                  .arg(minimum).arg(signName(target), names[target].join(", "))
            : QString("No sign contains at least %1 selected planets; %2 has %3 (%4).")
                  .arg(minimum).arg(signName(target)).arg(counts[target]).arg(names[target].join(", "));
        return evaluation;
    }

    QVector<bool> systemMatches;
    QString bestDescription;
    for (HouseSystem system : systemsForMode(query.houseMode)) {
        QVector<int> counts(13, 0);
        QVector<QStringList> names(13);
        for (const QString& body : bodies) {
            double longitude = 0.0;
            if (!bodyLongitude(returnChart, body, &longitude)) continue;
            const int house = houseForLongitude(returnChart, longitude, system);
            if (house >= 1 && house <= 12) {
                ++counts[house];
                names[house].push_back(body);
            }
        }
        int target = std::clamp(condition.stelliumTarget, 1, 12);
        if (condition.stelliumAny) {
            target = 1;
            for (int house = 2; house <= 12; ++house) {
                if (counts[house] > counts[target]) target = house;
            }
        }
        const bool matched = counts[target] >= minimum;
        systemMatches.push_back(matched);
        if (matched) {
            bestDescription = QString("%1-planet stellium in House %2: %3")
                .arg(minimum).arg(target).arg(names[target].join(", "));
        }
    }
    evaluation.matched = combineSystemMatches(query.houseMode, systemMatches);
    evaluation.systemLabel = systemMatchLabel(query.houseMode, systemMatches);
    evaluation.description = bestDescription.isEmpty()
        ? QString("No house contains at least %1 selected planets.").arg(minimum)
        : bestDescription + QString(" (%1)").arg(evaluation.systemLabel);
    return evaluation;
}

ReturnFinderConditionEvaluation evaluateCondition(const ReturnFinderCondition& condition,
                                                   const ReturnFinderQuery& query,
                                                   const NatalChart& returnChart,
                                                   const QDateTime& returnLocal) {
    switch (condition.type) {
        case ReturnFinderConditionType::Aspect:
            return evaluateAspect(condition, query, returnChart, returnLocal);
        case ReturnFinderConditionType::HouseLordPlacement:
            return evaluateHouseLordPlacement(condition, query, returnChart);
        case ReturnFinderConditionType::ProfectionLordPlacement:
            return evaluateProfectionPlacement(condition, query, returnChart, returnLocal);
        case ReturnFinderConditionType::Stellium:
            return evaluateStellium(condition, query, returnChart);
        case ReturnFinderConditionType::PlanetPlacement:
        default:
            return evaluatePlanetPlacement(condition, query, returnChart, returnLocal);
    }
}

bool evaluateQuery(const ReturnFinderQuery& query,
                   const NatalChart& returnChart,
                   const QDateTime& returnLocal,
                   QVector<ReturnFinderConditionEvaluation>* evaluations,
                   QString* summary,
                   double* closestOrb) {
    bool includeMatched = query.matchMode == ReturnFinderMatchMode::All;
    bool sawInclude = false;
    bool excluded = false;
    QStringList matchedDescriptions;
    double bestOrb = std::numeric_limits<double>::infinity();

    for (const auto& condition : query.conditions) {
        if (!condition.enabled) continue;
        ReturnFinderConditionEvaluation evaluation = evaluateCondition(condition, query, returnChart, returnLocal);
        if (condition.exclude) {
            if (evaluation.matched) excluded = true;
        } else {
            sawInclude = true;
            if (query.matchMode == ReturnFinderMatchMode::All) {
                includeMatched = includeMatched && evaluation.matched;
            } else {
                includeMatched = includeMatched || evaluation.matched;
            }
            if (evaluation.matched) {
                matchedDescriptions.push_back(evaluation.description);
                if (evaluation.orb >= 0.0) bestOrb = std::min(bestOrb, evaluation.orb);
            }
        }
        if (evaluations) evaluations->push_back(evaluation);
    }

    if (summary) {
        *summary = matchedDescriptions.isEmpty() ? "Matched configured conditions" : matchedDescriptions.join("; ");
    }
    if (closestOrb) {
        *closestOrb = std::isfinite(bestOrb) ? bestOrb : -1.0;
    }
    return sawInclude && includeMatched && !excluded;
}

HouseSystem computeHouseSystem(ReturnFinderHouseMode mode) {
    return mode == ReturnFinderHouseMode::WholeSign ? HouseSystem::WholeSign : HouseSystem::Placidus;
}

bool buildReturnChart(TropicalNatalEngine& engine,
                      const ReturnFinderQuery& query,
                      const QDateTime& local,
                      NatalChart* chart,
                      QString* error) {
    NatalInput input = query.natalInput;
    input.name = QString("%1 %2").arg(returnFinderTypeLabel(query.returnType), local.toString("yyyy-MM-dd"));
    input.date = local.date();
    input.time = local.time();
    input.timezone = query.timezone;
    input.latitude = query.latitude;
    input.longitude = query.longitude;
    input.houseSystem = computeHouseSystem(query.houseMode);
    input.aspectOrbs = query.aspectOrbs;
    TropicalComputeOptions options;
    options.includeArabicLots = false;
    options.includeFixedStars = false;
    options.includeAspectGrid = false;
    return engine.compute(input, options, chart, error);
}

}  // namespace

ReturnFinderWorker::ReturnFinderWorker(const ReturnFinderQuery& query)
    : query_(query) {}

void ReturnFinderWorker::cancel() {
    cancelled_.store(true);
}

void ReturnFinderWorker::run() {
    QVector<ReturnFinderResult> results;
    ReturnFinderRunSummary summary;
    QString fatalError;
    ReturnFinderQuery query = query_;

    SwissEph swe;
    if (!swe.load(query.dllSearchPaths, &fatalError)) {
        emit finished(results, summary, fatalError);
        return;
    }
    if (query.ephePath.isEmpty()) {
        emit finished(results, summary, "Ephemeris folder is not available.");
        return;
    }
    swe.setEphePath(query.ephePath);
    TropicalNatalEngine engine(&swe, query.ephePath);

    auto targetNeedsNatalPlacidus = [](const ReturnFinderTarget& target) {
        return target.kind == ReturnFinderTargetKind::HouseLord
            && target.scope == ReturnFinderScope::Natal;
    };
    const bool needsNatalPlacidus = query.houseMode != ReturnFinderHouseMode::WholeSign
        && std::any_of(query.conditions.begin(), query.conditions.end(),
                       [targetNeedsNatalPlacidus](const ReturnFinderCondition& condition) {
            if (!condition.enabled) return false;
            if (condition.type == ReturnFinderConditionType::HouseLordPlacement
                && condition.houseLordScope == ReturnFinderScope::Natal) {
                return true;
            }
            return condition.type == ReturnFinderConditionType::Aspect
                && (targetNeedsNatalPlacidus(condition.subject)
                    || targetNeedsNatalPlacidus(condition.target));
        });
    if (needsNatalPlacidus && query.natalChart.cusps.size() != 12) {
        NatalInput placidusInput = query.natalInput;
        placidusInput.houseSystem = HouseSystem::Placidus;
        TropicalComputeOptions options;
        options.includeArabicLots = false;
        options.includeFixedStars = false;
        options.includeAspectGrid = false;
        NatalChart placidusNatal;
        QString placidusError;
        if (engine.compute(placidusInput, options, &placidusNatal, &placidusError)) {
            query.natalChart = placidusNatal;
        } else if (query.houseMode == ReturnFinderHouseMode::BothOr) {
            summary.warnings.push_back(
                QString("Natal Placidus cusps unavailable; Whole Sign remains eligible: %1")
                    .arg(placidusError));
        } else {
            emit finished(results, summary,
                          QString("Unable to resolve natal Placidus house lords: %1")
                              .arg(placidusError));
            return;
        }
    }

    const QString targetBody = query.returnType == ReturnFinderType::Solar ? "Sun" : "Moon";
    double targetLongitude = 0.0;
    if (!bodyLongitude(query.natalChart, targetBody, &targetLongitude)) {
        emit finished(results, summary, QString("Unable to locate natal %1 longitude.").arg(targetBody));
        return;
    }

    int nextStableId = 1;
    auto processReturn = [&](int year, const QDateTime& utc, const QDateTime& local) {
        NatalChart chart;
        QString error;
        if (!buildReturnChart(engine, query, local, &chart, &error)) {
            ++summary.failed;
            summary.warnings.push_back(QString("%1: %2").arg(local.toString("yyyy-MM-dd"), error));
            return;
        }
        QVector<ReturnFinderConditionEvaluation> evaluations;
        QString matchSummary;
        double closestOrb = -1.0;
        if (!evaluateQuery(query, chart, local, &evaluations, &matchSummary, &closestOrb)) {
            return;
        }
        ReturnFinderResult result;
        result.stableId = nextStableId++;
        result.returnType = query.returnType;
        result.year = year;
        result.utcDateTime = utc;
        result.localDateTime = local;
        result.matchSummary = matchSummary;
        result.closestOrb = closestOrb;
        result.evaluations = evaluations;
        if (!chart.warnings.isEmpty()) result.warning = chart.warnings.join("; ");
        results.push_back(result);
    };

    const auto cancelled = [this]() { return cancelled_.load(); };
    if (query.returnType == ReturnFinderType::Solar) {
        const int startYear = std::clamp(std::min(query.startYear, query.endYear), 1800, 2399);
        const int endYear = std::clamp(std::max(query.startYear, query.endYear), 1800, 2399);
        const int total = endYear - startYear + 1;
        for (int year = startYear; year <= endYear && !cancelled(); ++year) {
            QDateTime utc;
            QDateTime local;
            QString error;
            if (!returncalc::solarReturnTimeUtc(swe, query.natalInput, year, query.timezone,
                                                targetLongitude, &utc, &local, &error, cancelled)) {
                if (!cancelled()) {
                    ++summary.failed;
                    summary.warnings.push_back(QString("%1: %2").arg(year).arg(error));
                }
            } else {
                processReturn(year, utc, local);
            }
            ++summary.scanned;
            emit progress(summary.scanned, total, QString("Scanning solar return %1").arg(year));
        }
    } else {
        QTimeZone timezone;
        QString normalizedTimezone;
        QString timezoneError;
        if (!parseTimezoneInput(query.timezone, &timezone, &normalizedTimezone, &timezoneError)) {
            emit finished(results, summary, timezoneError);
            return;
        }
        QDate startDate = query.startDate;
        QDate endDate = query.endDate;
        if (startDate > endDate) std::swap(startDate, endDate);
        const QDateTime startLocal(startDate, QTime(0, 0), timezone);
        QDateTime currentUtc;
        QDateTime currentLocal;
        QString error;
        bool haveReturn = returncalc::lunarReturnTimeUtc(swe, query.natalInput, startLocal.toUTC(), +1,
                                                         targetLongitude, normalizedTimezone,
                                                         &currentUtc, &currentLocal, &error, cancelled);
        if (!haveReturn) {
            if (cancelled()) {
                summary.cancelled = true;
                emit finished(results, summary, QString());
            } else {
                emit finished(results, summary,
                              error.isEmpty()
                                  ? QString("Unable to initialize the lunar return scan.")
                                  : error);
            }
            return;
        }
        const int estimatedTotal = static_cast<int>(
            std::max<qint64>(1, startDate.daysTo(endDate) / 27 + 2));
        constexpr int maxReturns = 5000;
        while (haveReturn && currentLocal.date() <= endDate && !cancelled()) {
            if (summary.scanned >= maxReturns) {
                summary.capped = true;
                summary.warnings.push_back(QString("Search capped at %1 lunar returns; narrow the date range.").arg(maxReturns));
                break;
            }
            processReturn(currentLocal.date().year(), currentUtc, currentLocal);
            ++summary.scanned;
            emit progress(summary.scanned, estimatedTotal,
                          QString("Scanning lunar return %1").arg(currentLocal.toString("yyyy-MM-dd")));
            QDateTime nextUtc;
            QDateTime nextLocal;
            const QDateTime nextAnchor = currentUtc.addSecs(static_cast<qint64>(24) * 24 * 3600);
            error.clear();
            haveReturn = returncalc::lunarReturnTimeUtc(swe, query.natalInput, nextAnchor, +1,
                                                        targetLongitude, normalizedTimezone,
                                                        &nextUtc, &nextLocal, &error, cancelled);
            if (!haveReturn || !nextUtc.isValid() || nextUtc <= currentUtc) {
                if (!cancelled() && !error.isEmpty()) {
                    ++summary.failed;
                    summary.warnings.push_back(error);
                }
                break;
            }
            currentUtc = nextUtc;
            currentLocal = nextLocal;
        }
    }

    summary.cancelled = cancelled();
    summary.matched = results.size();
    emit finished(results, summary, QString());
}

}  // namespace dracoved
