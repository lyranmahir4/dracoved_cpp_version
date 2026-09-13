#include "transit_workers.h"
#include "search_event_algorithms.h"

namespace dracoved {

void SearchWorker::runAdvancedSearch() {
    struct Cancelled {};
    auto check = [&]() { if (cancelled_.load()) throw Cancelled{}; };
    auto jdFor = [&](const QDateTime& utc) {
        return swe_.julianDay(utc.date().year(), utc.date().month(), utc.date().day(),
            utc.time().msecsSinceStartOfDay() / 3600000.0, SE_GREG_CAL);
    };
    const double start = jdFor(params_.startUtc);
    const double end = jdFor(params_.endUtc);
    auto utcFor = [&](double jd) {
        return params_.startUtc.addMSecs(qRound64((jd - start) * 86400000.0));
    };
    auto ephemeris = [&](const QString& name) -> search_events::Ephemeris {
        return [&, name](double jd) {
            check();
            double values[6] = {};
            QString error;
            const int id = transitcalc::bodyIdForName(name,
                effectivePrimaryNodeType(params_.lunarNodePolicy));
            if (id < 0 || !swe_.calcUtFull(jd, id, calcFlags_ | SEFLG_SPEED, values, &error))
                throw QString("%1: %2").arg(name, error.isEmpty() ? "Unsupported body" : error);
            double longitude = values[0];
            if (isLunarNodeName(name) && !isNorthLunarNodeName(name)) longitude += 180.0;
            return search_events::Position{search_events::wrap(longitude), values[3]};
        };
    };
    const QStringList aspects = params_.anyMajorAspect
        ? QStringList{"Conjunction", "Sextile", "Square", "Trine", "Opposition"}
        : QStringList{params_.aspectLabel};
    try {
        for (int i = 0; i < params_.transitPlanets.size(); ++i) {
            check();
            const QString name = params_.transitPlanets[i];
            emit progressUpdate(i * 100 / params_.transitPlanets.size(),
                QString("Searching %1 (%2/%3)").arg(name).arg(i + 1).arg(params_.transitPlanets.size()));
            const auto body = ephemeris(name);
            try {
                body(start);
                if (params_.eventType == SearchEventType::TransitAspect) {
                    if (name == params_.movingTarget) continue;
                    // Node counterparts are permanently opposite, not events.
                    if (isLunarNodeName(name) && isLunarNodeName(params_.movingTarget)
                        && lunarNodeTypeForName(name, effectivePrimaryNodeType(params_.lunarNodePolicy))
                            == lunarNodeTypeForName(params_.movingTarget, effectivePrimaryNodeType(params_.lunarNodePolicy))) continue;
                    const auto target = ephemeris(params_.movingTarget);
                    for (const auto& aspect : aspects) {
                        search_events::aspects(body, target, start, end,
                            transitcalc::aspectAngleForLabel(aspect), params_.orb,
                            params_.aspectMode == AspectMode::Exact, check,
                            [&](double t, double orb, int kind) {
                                emitResult(utcFor(t), name,
                                    kind == 0 ? "Transit Aspect" : kind == 1 ? "Transit Aspect Entry" : "Transit Aspect Exit",
                                    signName(signIndex(body(t).longitude)),
                                    QString("%1 transit %2").arg(aspect, params_.movingTarget), orb, true);
                            });
                    }
                } else if (params_.eventType == SearchEventType::RetrogradeShadow) {
                    search_events::shadows(body, start, end, check,
                        [&](double t, double degree, int kind) {
                            if (params_.shadowPhase != 0 && params_.shadowPhase != kind) return;
                            emitResult(utcFor(t), name, kind == 1 ? "Pre-shadow Entry" : "Post-shadow Exit",
                                signName(signIndex(degree)), formatDegInSign(degree), 0.0, false);
                        });
                } else {
                    const auto turns = search_events::stations(body, start, end, check);
                    for (const auto& target : params_.targetNames) {
                        for (const auto& aspect : aspects) {
                            search_events::closest(body, turns, params_.natalTargets.value(target),
                                transitcalc::aspectAngleForLabel(aspect), params_.orb, check,
                                [&](double t, double orb, int) {
                                    emitResult(utcFor(t), name, "Closest Approach (near miss)",
                                        signName(signIndex(body(t).longitude)),
                                        QString("%1 natal %2").arg(aspect, target), orb, true);
                                });
                        }
                    }
                }
            } catch (const QString& error) {
                if (!isAsteroidBody(name)) throw;
                warnings_ << error;
            }
        }
        check();
        if (params_.findMode != SearchFindMode::Range && hasBestResult_) emit resultFound(bestResult_);
        emit finished(false, QString());
    } catch (const Cancelled&) {
        emit finished(true, QString());
    } catch (const QString& error) {
        emit finished(false, error);
    }
}

} // namespace dracoved
