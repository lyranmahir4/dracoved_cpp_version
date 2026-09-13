#pragma once

#include "main_window.h"
#include "transit_calc_service.h"

#include "../core/formatting.h"
#include "../core/progression.h"
#include "../core/timezone_utils.h"
#include "../core/tropical_natal.h"

#include <QDate>
#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QTime>
#include <QTimeZone>
#include <QVector>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>

namespace dracoved {

enum class SearchEventType {
    SignIngress,
    SignEgress,
    HouseIngress,
    HouseEgress,
    Aspect,
    DegreeHit,
    Station,
    TransitAspect,
    RetrogradeShadow,
    ClosestApproach,
};

enum class SearchDirection {
    WithinRange,
    Forward,
    Backward,
};

enum class SearchFindMode {
    Range,
    Next,
    Previous,
};

enum class LunationFindMode {
    Range,
    Next,
    Previous,
};

enum class LunationEclipseRule {
    AstronomicalSwiss,
    StrictVedicWholeSign,
};

enum class AspectMode {
    Exact,
    WithinOrb,
};

struct SearchParams {
    SearchEventType eventType = SearchEventType::SignIngress;
    SearchDirection direction = SearchDirection::WithinRange;
    SearchFindMode findMode = SearchFindMode::Range;
    AspectMode aspectMode = AspectMode::Exact;
    double orb = 0.0;
    double aspectAngle = 0.0;
    QString aspectLabel;
    QString movingTarget;
    int shadowPhase = 0; // Both / pre-shadow entry / post-shadow exit.
    bool anyMajorAspect = false;
    int signFilter = -1;
    int houseFilter = 0;
    QStringList transitPlanets;
    QStringList targetNames;
    QMap<QString, double> natalTargets;
    QVector<double> natalCusps;
    double natalAsc = 0.0;
    bool hasNatal = false;
    bool overlayMode = false;
    HouseSystem houseSystem = HouseSystem::WholeSign;
    double latitude = 0.0;
    double longitude = 0.0;
    QDateTime startUtc;
    QDateTime endUtc;
    QTimeZone tz;
    QString tzLabel;
    ZodiacSystem zodiacSystem = ZodiacSystem::Tropical;
    SiderealAyanamsa siderealAyanamsa = SiderealAyanamsa::Lahiri;
    LunarNodePolicy lunarNodePolicy;
    QString ephePath;
    QStringList dllSearchPaths;
};

struct LunationParams {
    LunationFindMode findMode = LunationFindMode::Next;
    LunationEclipseRule eclipseRule = LunationEclipseRule::AstronomicalSwiss;
    QDateTime startUtc;
    QDateTime endUtc;
    bool includeNewMoon = true;
    bool includeFullMoon = true;
    bool includeSolarEclipse = false;
    bool includeLunarEclipse = false;
    bool useDegreeRange = false;
    double degreeRangeStart = 0.0;
    double degreeRangeEnd = 29.99;
    bool requirePlanetConjunction = false;
    QString conjunctionPlanet;
    bool targetSun = true;
    bool targetMoon = true;
    bool targetNorthNode = false;
    bool targetSouthNode = false;
    double conjunctionOrb = 3.0;
    QTimeZone tz;
    QString tzLabel;
    ZodiacSystem zodiacSystem = ZodiacSystem::Tropical;
    SiderealAyanamsa siderealAyanamsa = SiderealAyanamsa::Lahiri;
    LunarNodePolicy lunarNodePolicy;
    QString ephePath;
    QStringList dllSearchPaths;
};

struct CalendarParams {
    QDateTime startUtc;
    QDateTime endUtc;
    QTimeZone tz;
    QString tzLabel;
    QString ephePath;
    QStringList dllSearchPaths;
    ZodiacSystem zodiacSystem = ZodiacSystem::Tropical;
    SiderealAyanamsa siderealAyanamsa = SiderealAyanamsa::Lahiri;
    LunarNodePolicy lunarNodePolicy;
    QStringList planetNames;
    bool includeHouses = false;
    bool overlayMode = false;
    HouseSystem houseSystem = HouseSystem::WholeSign;
    QVector<double> natalCusps;
    double natalAsc = 0.0;
    double latitude = 0.0;
    double longitude = 0.0;
};

struct ConjunctionParams {
    QDateTime startUtc;
    QDateTime endUtc;
    QTimeZone tz;
    QString tzLabel;
    QString ephePath;
    QStringList dllSearchPaths;
    ZodiacSystem zodiacSystem = ZodiacSystem::Tropical;
    SiderealAyanamsa siderealAyanamsa = SiderealAyanamsa::Lahiri;
    LunarNodePolicy lunarNodePolicy;
    QStringList planetNames;
    int minCount = 2;
    bool useOrb = false;
    double orbDeg = 0.0;
    bool bucketByHouse = false;
    HouseSystem houseSystem = HouseSystem::WholeSign;
    QVector<double> natalCusps;
    double natalAsc = 0.0;
    bool uniqueFirstOnly = false;
    double uniqueDegreeStep = 1.0;
    QDateTime uniqueWindowStartUtc;
    QDateTime uniqueWindowEndUtc;
};


class SearchWorker : public QObject {
    Q_OBJECT

public:
    explicit SearchWorker(const SearchParams& params)
        : params_(params) {}

    QStringList warnings() const { return warnings_; }

public slots:
    void run() {
        QString err;
        if (!swe_.load(params_.dllSearchPaths, &err)) {
            emit finished(true, err);
            return;
        }
        if (!params_.ephePath.isEmpty()) {
            swe_.setEphePath(params_.ephePath);
        }
        calcFlags_ = 0;
        if (params_.zodiacSystem == ZodiacSystem::Sidereal) {
            swe_.setSidMode(siderealAyanamsaSwissMode(params_.siderealAyanamsa));
            calcFlags_ = SEFLG_SIDEREAL;
        }

        const int totalPlanets = params_.transitPlanets.size();
        if (totalPlanets == 0) {
            emit finished(false, "No transit planets selected.");
            return;
        }

        if (params_.eventType == SearchEventType::TransitAspect
            || params_.eventType == SearchEventType::RetrogradeShadow
            || params_.eventType == SearchEventType::ClosestApproach) {
            runAdvancedSearch();
            return;
        }

        const qint64 totalSecs = std::max<qint64>(1, std::llabs(params_.startUtc.secsTo(params_.endUtc)));
        int lastProgress = -1;

        for (int pIndex = 0; pIndex < totalPlanets; ++pIndex) {
            if (cancelled_.load()) {
                emit finished(true, QString());
                return;
            }
            const QString planetName = params_.transitPlanets[pIndex];
            const bool forward = params_.direction != SearchDirection::Backward;
            QDateTime t0 = forward ? params_.startUtc : params_.endUtc;
            const QDateTime tEnd = forward ? params_.endUtc : params_.startUtc;

            QString planetErr;
            double lon0 = 0.0;
            if (!planetLongitude(t0, planetName, &lon0, &planetErr)) {
                if (isAsteroidBody(planetName)) {
                    warnings_.push_back(QString("Skipped %1: %2").arg(planetName, planetErr));
                    continue;
                }
                emit finished(true, planetErr);
                return;
            }

            while (forward ? (t0 < tEnd) : (t0 > tEnd)) {
                if (cancelled_.load()) {
                    emit finished(true, QString());
                    return;
                }

                double speed = 0.0;
                if (!planetSpeed(t0, planetName, &speed, &planetErr)) {
                    if (isAsteroidBody(planetName)) {
                        warnings_.push_back(QString("Skipped %1: %2").arg(planetName, planetErr));
                        break;
                    }
                    emit finished(true, planetErr);
                    return;
                }
                if (transitcalc::isNodeName(planetName)
                    && lunarNodeTypeForName(planetName, effectivePrimaryNodeType(params_.lunarNodePolicy))
                        == LunarNodeType::Mean) {
                    speed = -std::abs(speed);
                }
                const double stepDays = transitcalc::clampStepDays(std::abs(speed));
                QDateTime t1 = t0.addSecs(static_cast<qint64>(stepDays * 86400.0 * (forward ? 1.0 : -1.0)));
                if (forward && t1 > tEnd) {
                    t1 = tEnd;
                } else if (!forward && t1 < tEnd) {
                    t1 = tEnd;
                }

                double lon1 = 0.0;
                if (!planetLongitude(t1, planetName, &lon1, &planetErr)) {
                    if (isAsteroidBody(planetName)) {
                        warnings_.push_back(QString("Skipped %1: %2").arg(planetName, planetErr));
                        break;
                    }
                    emit finished(true, planetErr);
                    return;
                }

                if (params_.eventType == SearchEventType::SignIngress || params_.eventType == SearchEventType::SignEgress) {
                    handleSignEvent(t0, t1, lon0, lon1, planetName);
                } else if (params_.eventType == SearchEventType::HouseIngress || params_.eventType == SearchEventType::HouseEgress) {
                    handleHouseEvent(t0, t1, lon0, lon1, planetName);
                } else if (params_.eventType == SearchEventType::Aspect || params_.eventType == SearchEventType::DegreeHit) {
                    handleAspectEvent(t0, t1, lon0, lon1, planetName);
                } else if (params_.eventType == SearchEventType::Station) {
                    handleStationEvent(t0, t1, planetName);
                }

                t0 = t1;
                lon0 = lon1;

                const qint64 elapsed = forward
                    ? params_.startUtc.secsTo(t0)
                    : params_.endUtc.secsTo(t0);
                const double planetProgress = static_cast<double>(std::min<qint64>(std::llabs(elapsed), totalSecs)) / totalSecs;
                const double overall = (static_cast<double>(pIndex) + planetProgress) / totalPlanets;
                const int progress = static_cast<int>(overall * 100.0);
                if (progress != lastProgress && progress % 5 == 0) {
                    lastProgress = progress;
                    emit progressUpdate(progress, QString("Searching %1 (%2%)").arg(planetName).arg(progress));
                }
            }
        }

        if (params_.findMode != SearchFindMode::Range && hasBestResult_) {
            emit resultFound(bestResult_);
        }
        emit finished(false, QString());
    }

    void cancel() {
        cancelled_.store(true);
    }

signals:
    void progressUpdate(int percent, const QString& status);
    void resultFound(const dracoved::MainWindow::TransitSearchResult& result);
    void finished(bool cancelled, const QString& error);

private:
    void runAdvancedSearch();

    bool planetLongitude(const QDateTime& utc, const QString& name, double* outLon, QString* error) {
        const int bodyId = transitcalc::bodyIdForName(
            name, effectivePrimaryNodeType(params_.lunarNodePolicy));
        if (bodyId < 0) {
            if (error) {
                *error = QString("Unsupported body: %1").arg(name);
            }
            return false;
        }
        const QDate date = utc.date();
        const QTime time = utc.time();
        const double hour = time.hour() + time.minute() / 60.0 + time.second() / 3600.0 + time.msec() / 3600000.0;
        const double jd = swe_.julianDay(date.year(), date.month(), date.day(), hour, SE_GREG_CAL);
        double lon = 0.0;
        QString calcErr;
        if (!swe_.calcUt(jd, bodyId, calcFlags_, &lon, &calcErr)) {
            if (error) {
                *error = calcErr;
            }
            return false;
        }
        lon = normalizeDegrees(lon);
        if (isLunarNodeName(name) && !isNorthLunarNodeName(name)) {
            lon = normalizeDegrees(lon + 180.0);
        }
        if (outLon) {
            *outLon = lon;
        }
        return true;
    }

    bool planetSpeed(const QDateTime& utc, const QString& name, double* outSpeed, QString* error) {
        const double deltaDays = 0.5;
        const QDateTime next = utc.addSecs(static_cast<qint64>(deltaDays * 86400.0));
        double lon0 = 0.0;
        double lon1 = 0.0;
        if (!planetLongitude(utc, name, &lon0, error)) {
            return false;
        }
        if (!planetLongitude(next, name, &lon1, error)) {
            return false;
        }
        const double diff = transitcalc::angularDiffSigned(lon1, lon0);
        if (outSpeed) {
            *outSpeed = diff / deltaDays;
        }
        return true;
    }

    bool houseData(const QDateTime& utc, double* outAsc, QVector<double>* outCusps, QString* error) {
        const QDate date = utc.date();
        const QTime time = utc.time();
        const double hour = time.hour() + time.minute() / 60.0 + time.second() / 3600.0 + time.msec() / 3600000.0;
        const double jd = swe_.julianDay(date.year(), date.month(), date.day(), hour, SE_GREG_CAL);
        double cuspsRaw[13] = {0};
        double ascmc[10] = {0};
        QString houseErr;
        if (!swe_.housesEx(jd, calcFlags_, params_.latitude, params_.longitude, 'P', cuspsRaw, ascmc, &houseErr)) {
            if (error) {
                *error = houseErr;
            }
            return false;
        }
        if (outAsc) {
            *outAsc = normalizeDegrees(ascmc[0]);
        }
        if (outCusps) {
            outCusps->clear();
            outCusps->reserve(12);
            for (int i = 1; i <= 12; ++i) {
                outCusps->push_back(normalizeDegrees(cuspsRaw[i]));
            }
        }
        return true;
    }

    int houseForLongitude(double lon, const QVector<double>& cusps, double asc) const {
        if (params_.houseSystem == HouseSystem::Placidus && cusps.size() == 12) {
            const double c1 = cusps[0];
            double target = normalizeDegrees(lon - c1);
            int house = 1;
            double last = 0.0;
            for (int i = 0; i < cusps.size(); ++i) {
                double v = normalizeDegrees(cusps[i] - c1);
                if (v < last) {
                    continue;
                }
                if (target >= v) {
                    house = i + 1;
                    last = v;
                }
            }
            return house;
        }
        const int ascIdx = signIndex(asc);
        const int lonIdx = signIndex(lon);
        return ((lonIdx - ascIdx + 12) % 12) + 1;
    }

    void handleSignEvent(const QDateTime& t0, const QDateTime& t1, double lon0, double lon1, const QString& planetName) {
        const int s0 = signIndex(lon0);
        const int s1 = signIndex(lon1);
        if (s0 == s1) {
            return;
        }
        QDateTime lo = t0;
        QDateTime hi = t1;
        int signLo = s0;
        for (int i = 0; i < 24; ++i) {
            const QDateTime mid = transitcalc::midTimeUtc(lo, hi);
            double lonMid = 0.0;
            if (!planetLongitude(mid, planetName, &lonMid, nullptr)) {
                break;
            }
            const int sMid = signIndex(lonMid);
            if (sMid == signLo) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        double lonEvent = 0.0;
        if (!planetLongitude(hi, planetName, &lonEvent, nullptr)) {
            return;
        }
        const int sOld = s0;
        const int sNew = signIndex(lonEvent);
        if (params_.eventType == SearchEventType::SignIngress) {
            if (params_.signFilter >= 0 && sNew != params_.signFilter) {
                return;
            }
            emitResult(hi, planetName, "Sign Ingress", signName(sNew), QString(), 0.0, false);
        } else {
            if (params_.signFilter >= 0 && sOld != params_.signFilter) {
                return;
            }
            emitResult(hi, planetName, "Sign Egress", signName(sOld), QString(), 0.0, false);
        }
    }

    void handleHouseEvent(const QDateTime& t0, const QDateTime& t1, double lon0, double lon1, const QString& planetName) {
        // Ingress/egress describes chronological motion, even when finding
        // the previous event. Refine and label the bracket in time order.
        if (t1 < t0) {
            handleHouseEvent(t1, t0, lon1, lon0, planetName);
            return;
        }
        int house0 = 0;
        int house1 = 0;
        if (params_.overlayMode) {
            house0 = houseForLongitude(lon0, params_.natalCusps, params_.natalAsc);
            house1 = houseForLongitude(lon1, params_.natalCusps, params_.natalAsc);
        } else {
            QVector<double> cusps0;
            QVector<double> cusps1;
            double asc0 = 0.0;
            double asc1 = 0.0;
            if (!houseData(t0, &asc0, &cusps0, nullptr) || !houseData(t1, &asc1, &cusps1, nullptr)) {
                return;
            }
            house0 = houseForLongitude(lon0, cusps0, asc0);
            house1 = houseForLongitude(lon1, cusps1, asc1);
        }
        if (house0 == house1) {
            return;
        }
        QDateTime lo = t0;
        QDateTime hi = t1;
        int houseLo = house0;
        for (int i = 0; i < 24; ++i) {
            const QDateTime mid = transitcalc::midTimeUtc(lo, hi);
            double lonMid = 0.0;
            if (!planetLongitude(mid, planetName, &lonMid, nullptr)) {
                break;
            }
            int houseMid = 0;
            if (params_.overlayMode) {
                houseMid = houseForLongitude(lonMid, params_.natalCusps, params_.natalAsc);
            } else {
                QVector<double> cuspsMid;
                double ascMid = 0.0;
                if (!houseData(mid, &ascMid, &cuspsMid, nullptr)) {
                    break;
                }
                houseMid = houseForLongitude(lonMid, cuspsMid, ascMid);
            }
            if (houseMid == houseLo) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        double lonEvent = 0.0;
        if (!planetLongitude(hi, planetName, &lonEvent, nullptr)) {
            return;
        }
        const int hOld = house0;
        int hNew = 0;
        if (params_.overlayMode) {
            hNew = houseForLongitude(lonEvent, params_.natalCusps, params_.natalAsc);
        } else {
            QVector<double> cuspsEvent;
            double ascEvent = 0.0;
            if (!houseData(hi, &ascEvent, &cuspsEvent, nullptr)) {
                return;
            }
            hNew = houseForLongitude(lonEvent, cuspsEvent, ascEvent);
        }
        if (params_.eventType == SearchEventType::HouseIngress) {
            if (params_.houseFilter > 0 && hNew != params_.houseFilter) {
                return;
            }
            emitResult(hi, planetName, "House Ingress", QString("House %1").arg(hNew), QString(), 0.0, false);
        } else {
            if (params_.houseFilter > 0 && hOld != params_.houseFilter) {
                return;
            }
            emitResult(hi, planetName, "House Egress", QString("House %1").arg(hOld), QString(), 0.0, false);
        }
    }

    void handleAspectEvent(const QDateTime& t0, const QDateTime& t1, double lon0, double lon1, const QString& planetName) {
        const QStringList aspectLabels = params_.anyMajorAspect
            ? QStringList{"Conjunction", "Sextile", "Square", "Trine", "Opposition"}
            : QStringList{params_.aspectLabel};
        for (const auto& targetName : params_.targetNames) {
            if (!params_.natalTargets.contains(targetName)) {
                continue;
            }
            const double targetLon = params_.natalTargets.value(targetName);
            for (const auto& aspectLabel : aspectLabels) {
                handleAspectForTarget(
                    t0,
                    t1,
                    lon0,
                    lon1,
                    planetName,
                    targetName,
                    targetLon,
                    transitcalc::aspectAngleForLabel(aspectLabel),
                    aspectLabel);
            }
        }
    }

    void handleAspectForTarget(
        const QDateTime& t0,
        const QDateTime& t1,
        double lon0,
        double lon1,
        const QString& planetName,
        const QString& targetName,
        double targetLon,
        double aspectAngle,
        const QString& aspectLabel) {
        const double diff0 = transitcalc::angularDiffAbs(lon0, targetLon);
        const double diff1 = transitcalc::angularDiffAbs(lon1, targetLon);
        const double delta0 = diff0 - aspectAngle;
        const double delta1 = diff1 - aspectAngle;

        if (params_.aspectMode == AspectMode::Exact) {
            const double exactTolerance = 0.1;
            QVector<double> targets;
            targets.reserve(2);
            targets.push_back(normalizeDegrees(targetLon + aspectAngle));
            if (aspectAngle > 0.01 && aspectAngle < 179.99) {
                const double opposite = normalizeDegrees(targetLon - aspectAngle);
                if (std::fabs(transitcalc::angularDiffSigned(opposite, targets[0])) > 0.01) {
                    targets.push_back(opposite);
                }
            }
            for (double exactLon : targets) {
                const double f0 = transitcalc::angularDiffSigned(lon0, exactLon);
                const double f1 = transitcalc::angularDiffSigned(lon1, exactLon);
                if (std::fabs(f0) < 1e-6) {
                    if (std::fabs(diff0 - aspectAngle) <= exactTolerance) {
                        emitAspectResult(
                            t0, planetName, targetName, diff0, aspectAngle, aspectLabel);
                    }
                    continue;
                }
                if (f0 * f1 > 0.0) {
                    continue;
                }
                QDateTime hi = bisectRoot(t0, t1, [&](const QDateTime& t, double* outDiff) {
                    double lon = 0.0;
                    if (!planetLongitude(t, planetName, &lon, nullptr)) {
                        return 0.0;
                    }
                    const double diff = transitcalc::angularDiffSigned(lon, exactLon);
                    if (outDiff) {
                        *outDiff = diff;
                    }
                    return diff;
                });
                double diff = 0.0;
                double eventLon = 0.0;
                if (planetLongitude(hi, planetName, &eventLon, nullptr)) {
                    diff = transitcalc::angularDiffAbs(eventLon, targetLon);
                }
                if (std::fabs(diff - aspectAngle) > exactTolerance) {
                    continue;
                }
                emitAspectResult(
                    hi, planetName, targetName, diff, aspectAngle, aspectLabel);
            }
        } else {
            const double f0 = std::fabs(delta0) - params_.orb;
            const double f1 = std::fabs(delta1) - params_.orb;
            if (f0 > 0.0 && f1 <= 0.0) {
                QDateTime entry = bisectRoot(t0, t1, [&](const QDateTime& t, double* outDiff) {
                    double lon = 0.0;
                    if (!planetLongitude(t, planetName, &lon, nullptr)) {
                        return 0.0;
                    }
                    const double diff = transitcalc::angularDiffAbs(lon, targetLon);
                    if (outDiff) {
                        *outDiff = diff;
                    }
                    const double delta = diff - aspectAngle;
                    return std::fabs(delta) - params_.orb;
                });
                const QString eventLabel = (params_.eventType == SearchEventType::DegreeHit)
                    ? QString("Degree Entry")
                    : QString("Aspect Entry");
                emitAspectWindowResult(
                    entry,
                    planetName,
                    targetName,
                    eventLabel,
                    aspectAngle,
                    aspectLabel);
            } else if (f0 <= 0.0 && f1 > 0.0) {
                QDateTime exit = bisectRoot(t0, t1, [&](const QDateTime& t, double* outDiff) {
                    double lon = 0.0;
                    if (!planetLongitude(t, planetName, &lon, nullptr)) {
                        return 0.0;
                    }
                    const double diff = transitcalc::angularDiffAbs(lon, targetLon);
                    if (outDiff) {
                        *outDiff = diff;
                    }
                    const double delta = diff - aspectAngle;
                    return std::fabs(delta) - params_.orb;
                });
                const QString eventLabel = (params_.eventType == SearchEventType::DegreeHit)
                    ? QString("Degree Exit")
                    : QString("Aspect Exit");
                emitAspectWindowResult(
                    exit,
                    planetName,
                    targetName,
                    eventLabel,
                    aspectAngle,
                    aspectLabel);
            }
        }
    }
    void handleStationEvent(const QDateTime& t0, const QDateTime& t1, const QString& planetName) {
        if (transitcalc::isNodeName(planetName)
            && lunarNodeTypeForName(planetName, effectivePrimaryNodeType(params_.lunarNodePolicy))
                == LunarNodeType::Mean) {
            return;
        }
        double speed0 = 0.0;
        double speed1 = 0.0;
        if (!planetSpeed(t0, planetName, &speed0, nullptr) || !planetSpeed(t1, planetName, &speed1, nullptr)) {
            return;
        }
        if (speed0 == 0.0 || speed0 * speed1 > 0.0) {
            return;
        }
        QDateTime station = bisectRoot(t0, t1, [&](const QDateTime& t, double* outDiff) {
            double speed = 0.0;
            if (!planetSpeed(t, planetName, &speed, nullptr)) {
                return 0.0;
            }
            if (outDiff) {
                *outDiff = speed;
            }
            return speed;
        });
        double speedAt = 0.0;
        planetSpeed(station, planetName, &speedAt, nullptr);
        const QString label = speedAt < 0.0 ? "Station Retrograde" : "Station Direct";
        double lon = 0.0;
        planetLongitude(station, planetName, &lon, nullptr);
        emitResult(station, planetName, label, signName(signIndex(lon)), QString(), 0.0, false);
    }

    QDateTime bisectRoot(const QDateTime& lo, const QDateTime& hi,
                         const std::function<double(const QDateTime&, double*)>& func) {
        QDateTime a = lo;
        QDateTime b = hi;
        double fa = 0.0;
        double fb = 0.0;
        func(a, &fa);
        func(b, &fb);
        for (int i = 0; i < 24; ++i) {
            if (cancelled_.load()) {
                return a;
            }
            if (a.secsTo(b) <= 60) {
                return b;
            }
            QDateTime mid = transitcalc::midTimeUtc(a, b);
            double fm = 0.0;
            func(mid, &fm);
            if ((fa <= 0.0 && fm <= 0.0) || (fa >= 0.0 && fm >= 0.0)) {
                a = mid;
                fa = fm;
            } else {
                b = mid;
                fb = fm;
            }
        }
        return b;
    }

    void emitAspectResult(
        const QDateTime& utc,
        const QString& planetName,
        const QString& targetName,
        double diff,
        double aspectAngle,
        const QString& aspectLabel) {
        const double orb = std::fabs(diff - aspectAngle);
        QString signHouse;
        double lon = 0.0;
        if (planetLongitude(utc, planetName, &lon, nullptr)) {
            signHouse = signName(signIndex(lon));
        }
        const bool degreeMode = (params_.eventType == SearchEventType::DegreeHit);
        const QString eventLabel = degreeMode ? QString("Degree Hit") : QString("Aspect");
        const QString detailLabel = degreeMode
            ? targetName
            : QString("%1 %2").arg(aspectLabel, targetName);
        emitResult(utc, planetName, eventLabel, signHouse, detailLabel, orb, true);
    }

    void emitAspectWindowResult(
        const QDateTime& utc,
        const QString& planetName,
        const QString& targetName,
        const QString& eventLabel,
        double aspectAngle,
        const QString& aspectLabel) {
        double lon = 0.0;
        if (!planetLongitude(utc, planetName, &lon, nullptr)) {
            return;
        }
        const double targetLon = params_.natalTargets.value(targetName);
        const double diff = transitcalc::angularDiffAbs(lon, targetLon);
        const double orb = std::fabs(diff - aspectAngle);
        const QString signHouse = signName(signIndex(lon));
        const QString detailLabel = (params_.eventType == SearchEventType::DegreeHit)
            ? targetName
            : QString("%1 %2").arg(aspectLabel, targetName);
        emitResult(utc, planetName, eventLabel, signHouse, detailLabel, orb, true);
    }
    void emitResult(const QDateTime& utc, const QString& planetName, const QString& eventLabel,
                    const QString& signHouse, const QString& aspectLabel, double orb, bool hasOrb) {
        MainWindow::TransitSearchResult result;
        result.timeUtc = utc;
        result.timeLocal = utc.toTimeZone(params_.tz);
        result.tzLabel = params_.tzLabel;
        result.planet = planetName;
        result.event = eventLabel;
        result.signHouse = signHouse;
        result.aspect = aspectLabel;
        result.orb = orb;
        result.hasOrb = hasOrb;
        if (params_.findMode == SearchFindMode::Range) {
            emit resultFound(result);
            return;
        }
        if (!hasBestResult_) {
            bestResult_ = result;
            hasBestResult_ = true;
            return;
        }
        const bool preferEarlier = (params_.findMode == SearchFindMode::Next);
        if ((preferEarlier && result.timeUtc < bestResult_.timeUtc)
            || (!preferEarlier && result.timeUtc > bestResult_.timeUtc)) {
            bestResult_ = result;
        }
    }

    SearchParams params_;
    SwissEph swe_;
    int calcFlags_ = 0;
    std::atomic<bool> cancelled_{false};
    MainWindow::TransitSearchResult bestResult_;
    bool hasBestResult_ = false;
    QStringList warnings_;
};

class CalendarWorker : public QObject {
    Q_OBJECT

public:
    explicit CalendarWorker(const CalendarParams& params)
        : params_(params) {}

    const QVector<MainWindow::TransitCalendarEvent>& results() const { return events_; }
    QStringList warnings() const { return warnings_; }

public slots:
    void run() {
        QString err;
        if (!swe_.load(params_.dllSearchPaths, &err)) {
            emit finished(false, err);
            return;
        }
        if (!params_.ephePath.isEmpty()) {
            swe_.setEphePath(params_.ephePath);
        }
        calcFlags_ = 0;
        if (params_.zodiacSystem == ZodiacSystem::Sidereal) {
            swe_.setSidMode(siderealAyanamsaSwissMode(params_.siderealAyanamsa));
            calcFlags_ = SEFLG_SIDEREAL;
        }
        if (params_.planetNames.isEmpty()) {
            emit finished(false, "No planets selected.");
            return;
        }

        events_.clear();
        const int totalPlanets = params_.planetNames.size();
        constexpr int kShadowBufferDays = 400;
        const QDateTime scanStartUtc = params_.startUtc.addDays(-kShadowBufferDays);
        const QDateTime scanEndUtc = params_.endUtc.addDays(kShadowBufferDays);
        const qint64 totalSecs = std::max<qint64>(1, std::llabs(scanStartUtc.secsTo(scanEndUtc)));
        int lastProgress = -1;

        for (int pIndex = 0; pIndex < totalPlanets; ++pIndex) {
            if (cancelled_.load()) {
                emit finished(true, QString());
                return;
            }
            const QString planetName = params_.planetNames[pIndex];
            QDateTime t0 = scanStartUtc;
            QString planetErr;
            double lon0 = 0.0;
            if (!planetLongitude(t0, planetName, &lon0, &planetErr)) {
                if (isAsteroidBody(planetName)) {
                    warnings_.push_back(QString("Skipped %1: %2").arg(planetName, planetErr));
                    continue;
                }
                emit finished(false, planetErr);
                return;
            }

            QVector<StationMarker> stations;

            while (t0 < scanEndUtc) {
                if (cancelled_.load()) {
                    emit finished(true, QString());
                    return;
                }
                double speed = 0.0;
                if (!planetSpeed(t0, planetName, &speed, &planetErr)) {
                    if (isAsteroidBody(planetName)) {
                        warnings_.push_back(QString("Skipped %1: %2").arg(planetName, planetErr));
                        break;
                    }
                    emit finished(false, planetErr);
                    return;
                }
                if (transitcalc::isNodeName(planetName)
                    && lunarNodeTypeForName(planetName, effectivePrimaryNodeType(params_.lunarNodePolicy))
                        == LunarNodeType::Mean) {
                    speed = -std::abs(speed);
                }
                const double stepDays = transitcalc::clampStepDays(std::abs(speed));
                qint64 stepSecs = static_cast<qint64>(stepDays * 86400.0);
                if (stepSecs <= 0) {
                    stepSecs = 3600;
                }
                QDateTime t1 = t0.addSecs(stepSecs);
                if (t1 > scanEndUtc) {
                    t1 = scanEndUtc;
                }

                double lon1 = 0.0;
                if (!planetLongitude(t1, planetName, &lon1, &planetErr)) {
                    if (isAsteroidBody(planetName)) {
                        warnings_.push_back(QString("Skipped %1: %2").arg(planetName, planetErr));
                        break;
                    }
                    emit finished(false, planetErr);
                    return;
                }

                handleSignEvent(t0, t1, lon0, lon1, planetName);
                if (params_.includeHouses) {
                    handleHouseEvent(t0, t1, lon0, lon1, planetName);
                }
                handleStationEvent(t0, t1, planetName, &stations);

                t0 = t1;
                lon0 = lon1;

                const qint64 elapsed = scanStartUtc.secsTo(t0);
                const double planetProgress = static_cast<double>(std::min<qint64>(std::llabs(elapsed), totalSecs)) / totalSecs;
                const double overall = (static_cast<double>(pIndex) + planetProgress) / totalPlanets;
                const int progress = static_cast<int>(overall * 100.0);
                if (progress != lastProgress && progress % 5 == 0) {
                    lastProgress = progress;
                    emit progressUpdate(progress, QString("Computing calendar (%1%)").arg(progress));
                }
            }

            std::sort(stations.begin(), stations.end(), [](const StationMarker& a, const StationMarker& b) {
                return a.timeUtc < b.timeUtc;
            });
            for (int i = 0; i < stations.size(); ++i) {
                if (!stations[i].retrograde) {
                    continue;
                }
                int directIndex = -1;
                for (int j = i + 1; j < stations.size(); ++j) {
                    if (!stations[j].retrograde) {
                        directIndex = j;
                        break;
                    }
                }
                if (directIndex < 0) {
                    continue;
                }
                const double shadowDegree = stations[i].longitude;
                QDateTime preShadowStart;
                if (findShadowCrossing(stations[i].timeUtc, planetName, shadowDegree, false, &preShadowStart)) {
                    emitEvent(preShadowStart, planetName, "Pre-shadow start", signName(signIndex(shadowDegree)), shadowDegree);
                }
                QDateTime postShadowEnd;
                if (findShadowCrossing(stations[directIndex].timeUtc, planetName, shadowDegree, true, &postShadowEnd)) {
                    emitEvent(postShadowEnd, planetName, "Post-shadow end", signName(signIndex(shadowDegree)), shadowDegree);
                }
            }
        }

        std::sort(events_.begin(), events_.end(), [](const MainWindow::TransitCalendarEvent& a, const MainWindow::TransitCalendarEvent& b) {
            if (a.timeUtc == b.timeUtc) {
                if (a.planet == b.planet) {
                    return a.event < b.event;
                }
                return a.planet < b.planet;
            }
            return a.timeUtc < b.timeUtc;
        });

        emit finished(false, QString());
    }

    void cancel() {
        cancelled_.store(true);
    }

signals:
    void progressUpdate(int percent, const QString& status);
    void finished(bool cancelled, const QString& error);

private:
    struct StationMarker {
        QDateTime timeUtc;
        bool retrograde = false;
        double longitude = 0.0;
    };

    bool inDisplayRange(const QDateTime& utc) const {
        return utc >= params_.startUtc && utc <= params_.endUtc;
    }

    bool planetLongitude(const QDateTime& utc, const QString& name, double* outLon, QString* error) {
        const int bodyId = transitcalc::bodyIdForName(
            name, effectivePrimaryNodeType(params_.lunarNodePolicy));
        if (bodyId < 0) {
            if (error) {
                *error = QString("Unsupported body: %1").arg(name);
            }
            return false;
        }
        const QDate date = utc.date();
        const QTime time = utc.time();
        const double hour = time.hour() + time.minute() / 60.0 + time.second() / 3600.0 + time.msec() / 3600000.0;
        const double jd = swe_.julianDay(date.year(), date.month(), date.day(), hour, SE_GREG_CAL);
        double lon = 0.0;
        QString calcErr;
        if (!swe_.calcUt(jd, bodyId, calcFlags_, &lon, &calcErr)) {
            if (error) {
                *error = calcErr;
            }
            return false;
        }
        lon = normalizeDegrees(lon);
        if (isLunarNodeName(name) && !isNorthLunarNodeName(name)) {
            lon = normalizeDegrees(lon + 180.0);
        }
        if (outLon) {
            *outLon = lon;
        }
        return true;
    }

    bool planetSpeed(const QDateTime& utc, const QString& name, double* outSpeed, QString* error) {
        const double deltaDays = 0.5;
        const QDateTime next = utc.addSecs(static_cast<qint64>(deltaDays * 86400.0));
        double lon0 = 0.0;
        double lon1 = 0.0;
        if (!planetLongitude(utc, name, &lon0, error)) {
            return false;
        }
        if (!planetLongitude(next, name, &lon1, error)) {
            return false;
        }
        const double diff = transitcalc::angularDiffSigned(lon1, lon0);
        if (outSpeed) {
            *outSpeed = diff / deltaDays;
        }
        return true;
    }

    bool houseData(const QDateTime& utc, double* outAsc, QVector<double>* outCusps, QString* error) {
        const QDate date = utc.date();
        const QTime time = utc.time();
        const double hour = time.hour() + time.minute() / 60.0 + time.second() / 3600.0 + time.msec() / 3600000.0;
        const double jd = swe_.julianDay(date.year(), date.month(), date.day(), hour, SE_GREG_CAL);
        double cuspsRaw[13] = {0};
        double ascmc[10] = {0};
        QString houseErr;
        if (!swe_.housesEx(jd, calcFlags_, params_.latitude, params_.longitude, 'P', cuspsRaw, ascmc, &houseErr)) {
            if (error) {
                *error = houseErr;
            }
            return false;
        }
        if (outAsc) {
            *outAsc = normalizeDegrees(ascmc[0]);
        }
        if (outCusps) {
            outCusps->clear();
            outCusps->reserve(12);
            for (int i = 1; i <= 12; ++i) {
                outCusps->push_back(normalizeDegrees(cuspsRaw[i]));
            }
        }
        return true;
    }

    int houseForLongitude(double lon, const QVector<double>& cusps, double asc) const {
        if (params_.houseSystem == HouseSystem::Placidus && cusps.size() == 12) {
            const double c1 = cusps[0];
            double target = normalizeDegrees(lon - c1);
            int house = 1;
            double last = 0.0;
            for (int i = 0; i < cusps.size(); ++i) {
                double v = normalizeDegrees(cusps[i] - c1);
                if (v < last) {
                    continue;
                }
                if (target >= v) {
                    house = i + 1;
                    last = v;
                }
            }
            return house;
        }
        const int ascIdx = signIndex(asc);
        const int lonIdx = signIndex(lon);
        return ((lonIdx - ascIdx + 12) % 12) + 1;
    }

    void emitEvent(const QDateTime& utc, const QString& planetName, const QString& eventLabel, const QString& signHouse, double longitude) {
        if (!inDisplayRange(utc)) {
            return;
        }
        MainWindow::TransitCalendarEvent result;
        result.timeUtc = utc;
        result.tzLabel = params_.tzLabel;
        result.planet = planetName;
        result.event = eventLabel;
        result.signHouse = signHouse;
        result.longitude = normalizeDegrees(longitude);
        events_.push_back(result);
    }

    void handleSignEvent(const QDateTime& t0, const QDateTime& t1, double lon0, double lon1, const QString& planetName) {
        const int s0 = signIndex(lon0);
        const int s1 = signIndex(lon1);
        if (s0 == s1) {
            return;
        }
        QDateTime lo = t0;
        QDateTime hi = t1;
        int signLo = s0;
        for (int i = 0; i < 24; ++i) {
            const QDateTime mid = transitcalc::midTimeUtc(lo, hi);
            double lonMid = 0.0;
            if (!planetLongitude(mid, planetName, &lonMid, nullptr)) {
                break;
            }
            const int sMid = signIndex(lonMid);
            if (sMid == signLo) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        double lonEvent = 0.0;
        if (!planetLongitude(hi, planetName, &lonEvent, nullptr)) {
            return;
        }
        const int sOld = s0;
        const int sNew = signIndex(lonEvent);
        emitEvent(hi, planetName, "Sign Egress", signName(sOld), lonEvent);
        emitEvent(hi, planetName, "Sign Ingress", signName(sNew), lonEvent);
    }

    void handleHouseEvent(const QDateTime& t0, const QDateTime& t1, double lon0, double lon1, const QString& planetName) {
        int house0 = 0;
        int house1 = 0;
        if (params_.overlayMode) {
            house0 = houseForLongitude(lon0, params_.natalCusps, params_.natalAsc);
            house1 = houseForLongitude(lon1, params_.natalCusps, params_.natalAsc);
        } else {
            QVector<double> cusps0;
            QVector<double> cusps1;
            double asc0 = 0.0;
            double asc1 = 0.0;
            if (!houseData(t0, &asc0, &cusps0, nullptr) || !houseData(t1, &asc1, &cusps1, nullptr)) {
                return;
            }
            house0 = houseForLongitude(lon0, cusps0, asc0);
            house1 = houseForLongitude(lon1, cusps1, asc1);
        }
        if (house0 == house1) {
            return;
        }

        QDateTime lo = t0;
        QDateTime hi = t1;
        int houseLo = house0;
        for (int i = 0; i < 24; ++i) {
            const QDateTime mid = transitcalc::midTimeUtc(lo, hi);
            double lonMid = 0.0;
            if (!planetLongitude(mid, planetName, &lonMid, nullptr)) {
                break;
            }
            int houseMid = 0;
            if (params_.overlayMode) {
                houseMid = houseForLongitude(lonMid, params_.natalCusps, params_.natalAsc);
            } else {
                QVector<double> cuspsMid;
                double ascMid = 0.0;
                if (!houseData(mid, &ascMid, &cuspsMid, nullptr)) {
                    break;
                }
                houseMid = houseForLongitude(lonMid, cuspsMid, ascMid);
            }
            if (houseMid == houseLo) {
                lo = mid;
            } else {
                hi = mid;
            }
        }

        double lonEvent = 0.0;
        if (!planetLongitude(hi, planetName, &lonEvent, nullptr)) {
            return;
        }
        const int hOld = house0;
        int hNew = 0;
        if (params_.overlayMode) {
            hNew = houseForLongitude(lonEvent, params_.natalCusps, params_.natalAsc);
        } else {
            QVector<double> cuspsEvent;
            double ascEvent = 0.0;
            if (!houseData(hi, &ascEvent, &cuspsEvent, nullptr)) {
                return;
            }
            hNew = houseForLongitude(lonEvent, cuspsEvent, ascEvent);
        }
        emitEvent(hi, planetName, "House Egress", QString("House %1").arg(hOld), lonEvent);
        emitEvent(hi, planetName, "House Ingress", QString("House %1").arg(hNew), lonEvent);
    }

    void handleStationEvent(const QDateTime& t0, const QDateTime& t1, const QString& planetName, QVector<StationMarker>* stations) {
        if (transitcalc::isNodeName(planetName)
            && lunarNodeTypeForName(planetName, effectivePrimaryNodeType(params_.lunarNodePolicy))
                == LunarNodeType::Mean) {
            return;
        }
        double speed0 = 0.0;
        double speed1 = 0.0;
        if (!planetSpeed(t0, planetName, &speed0, nullptr) || !planetSpeed(t1, planetName, &speed1, nullptr)) {
            return;
        }
        if (speed0 == 0.0 || speed0 * speed1 > 0.0) {
            return;
        }
        QDateTime station = bisectRoot(t0, t1, [&](const QDateTime& t, double* outDiff) {
            double speed = 0.0;
            if (!planetSpeed(t, planetName, &speed, nullptr)) {
                return 0.0;
            }
            if (outDiff) {
                *outDiff = speed;
            }
            return speed;
        });
        double speedAt = 0.0;
        if (!planetSpeed(station, planetName, &speedAt, nullptr)) {
            return;
        }
        double lon = 0.0;
        if (!planetLongitude(station, planetName, &lon, nullptr)) {
            return;
        }
        const bool retrograde = (speedAt < 0.0);
        const QString label = retrograde ? "Station Retrograde" : "Station Direct";
        emitEvent(station, planetName, label, signName(signIndex(lon)), lon);
        if (stations) {
            StationMarker marker;
            marker.timeUtc = station;
            marker.retrograde = retrograde;
            marker.longitude = lon;
            stations->push_back(marker);
        }
    }

    bool crossesLongitude(double lon0, double lon1, double targetLon) const {
        const double d0 = transitcalc::angularDiffSigned(lon0, targetLon);
        const double d1 = transitcalc::angularDiffSigned(lon1, targetLon);
        if (std::fabs(d0) < 1e-6 || std::fabs(d1) < 1e-6) {
            return true;
        }
        return (d0 <= 0.0 && d1 >= 0.0) || (d0 >= 0.0 && d1 <= 0.0);
    }

    QDateTime bisectRoot(const QDateTime& lo, const QDateTime& hi,
                         const std::function<double(const QDateTime&, double*)>& func) {
        QDateTime a = lo;
        QDateTime b = hi;
        double fa = 0.0;
        double fb = 0.0;
        func(a, &fa);
        func(b, &fb);
        for (int i = 0; i < 24; ++i) {
            if (cancelled_.load()) {
                return a;
            }
            if (a.secsTo(b) <= 60) {
                return b;
            }
            const QDateTime mid = transitcalc::midTimeUtc(a, b);
            double fm = 0.0;
            func(mid, &fm);
            if ((fa <= 0.0 && fm <= 0.0) || (fa >= 0.0 && fm >= 0.0)) {
                a = mid;
                fa = fm;
            } else {
                b = mid;
                fb = fm;
            }
        }
        return b;
    }

    QDateTime bisectLongitude(const QDateTime& lo, const QDateTime& hi, const QString& planetName, double targetLon) {
        return bisectRoot(lo, hi, [&](const QDateTime& t, double* outDiff) {
            double lon = 0.0;
            if (!planetLongitude(t, planetName, &lon, nullptr)) {
                if (outDiff) {
                    *outDiff = 0.0;
                }
                return 0.0;
            }
            const double diff = transitcalc::angularDiffSigned(lon, targetLon);
            if (outDiff) {
                *outDiff = diff;
            }
            return diff;
        });
    }

    bool findShadowCrossing(const QDateTime& anchor, const QString& planetName, double targetLon,
                            bool forward, QDateTime* outTime) {
        if (!outTime) {
            return false;
        }
        QDateTime t0 = anchor;
        double lon0 = 0.0;
        if (!planetLongitude(t0, planetName, &lon0, nullptr)) {
            return false;
        }
        if (std::fabs(transitcalc::angularDiffSigned(lon0, targetLon)) < 1e-6) {
            t0 = t0.addSecs(forward ? 60 : -60);
            if (!planetLongitude(t0, planetName, &lon0, nullptr)) {
                return false;
            }
        }
        double travelledDays = 0.0;
        constexpr double kMaxDays = 500.0;
        while (travelledDays < kMaxDays) {
            if (cancelled_.load()) {
                return false;
            }
            double speed = 0.0;
            if (!planetSpeed(t0, planetName, &speed, nullptr)) {
                return false;
            }
            double stepDays = transitcalc::clampStepDays(std::abs(speed));
            if (stepDays < 0.1) {
                stepDays = 0.1;
            }
            qint64 stepSecs = static_cast<qint64>(stepDays * 86400.0);
            if (stepSecs <= 0) {
                stepSecs = 60;
            }
            if (!forward) {
                stepSecs = -stepSecs;
            }
            const QDateTime t1 = t0.addSecs(stepSecs);
            double lon1 = 0.0;
            if (!planetLongitude(t1, planetName, &lon1, nullptr)) {
                return false;
            }
            if (crossesLongitude(lon0, lon1, targetLon)) {
                if (forward) {
                    *outTime = bisectLongitude(t0, t1, planetName, targetLon);
                } else {
                    *outTime = bisectLongitude(t1, t0, planetName, targetLon);
                }
                return outTime->isValid();
            }
            t0 = t1;
            lon0 = lon1;
            travelledDays += stepDays;
        }
        return false;
    }

    CalendarParams params_;
    SwissEph swe_;
    int calcFlags_ = 0;
    std::atomic<bool> cancelled_{false};
    QVector<MainWindow::TransitCalendarEvent> events_;
    QStringList warnings_;
};

class ConjunctionWorker : public QObject {
    Q_OBJECT

public:
    explicit ConjunctionWorker(const ConjunctionParams& params)
        : params_(params) {}

    const QVector<MainWindow::TransitConjunctionWindow>& results() const { return results_; }
    QStringList warnings() const { return warnings_; }

public slots:
    void run() {
        QString err;
        if (!swe_.load(params_.dllSearchPaths, &err)) {
            emit finished(false, err);
            return;
        }
        if (!params_.ephePath.isEmpty()) {
            swe_.setEphePath(params_.ephePath);
        }
        calcFlags_ = 0;
        if (params_.zodiacSystem == ZodiacSystem::Sidereal) {
            swe_.setSidMode(siderealAyanamsaSwissMode(params_.siderealAyanamsa));
            calcFlags_ = SEFLG_SIDEREAL;
        }
        if (params_.planetNames.isEmpty()) {
            emit finished(false, "No planets selected.");
            return;
        }
        if (params_.minCount < 2) {
            emit finished(false, "Minimum planet count must be 2 or more.");
            return;
        }
        if (params_.bucketByHouse && params_.houseSystem == HouseSystem::Placidus && params_.natalCusps.size() != 12) {
            emit finished(false, "Natal Placidus cusps unavailable for house-based conjunctions.");
            return;
        }
        if (!params_.startUtc.isValid() || !params_.endUtc.isValid() || params_.startUtc >= params_.endUtc) {
            emit finished(false, "Invalid conjunction range.");
            return;
        }

        activePlanetNames_ = params_.planetNames;
        for (int i = activePlanetNames_.size() - 1; i >= 0; --i) {
            const QString& name = activePlanetNames_[i];
            double lonProbe = 0.0;
            QString probeErr;
            if (!planetLongitude(params_.startUtc, name, &lonProbe, &probeErr)) {
                if (isAsteroidBody(name)) {
                    warnings_.push_back(QString("Skipped %1: %2").arg(name, probeErr));
                    activePlanetNames_.removeAt(i);
                    continue;
                }
                emit finished(false, probeErr);
                return;
            }
        }
        if (activePlanetNames_.size() < 2) {
            emit finished(false, "Not enough planets available after filtering.");
            return;
        }
        if (params_.minCount > activePlanetNames_.size()) {
            emit finished(false, QString("Minimum planet count exceeds available planets (%1).").arg(activePlanetNames_.size()));
            return;
        }

        results_.clear();
        if (params_.minCount == 2 && activePlanetNames_.size() == 2) {
            if (!runExactPairConjunctions(&err)) {
                if (cancelled_.load()) {
                    emit finished(true, QString());
                } else {
                    emit finished(false, err);
                }
                return;
            }
            std::sort(results_.begin(), results_.end(), [](const MainWindow::TransitConjunctionWindow& a,
                                                          const MainWindow::TransitConjunctionWindow& b) {
                if (a.startUtc == b.startUtc) {
                    return a.bucketLabel < b.bucketLabel;
                }
                return a.startUtc < b.startUtc;
            });
            if (params_.uniqueFirstOnly) {
                if (!applyUniqueFirstFilter(&err)) {
                    emit finished(false, err);
                    return;
                }
            }
            if (cancelled_.load()) {
                emit finished(true, QString());
            } else {
                emit finished(false, QString());
            }
            return;
        }

        State state0;
        if (!computeState(params_.startUtc, &state0, &err)) {
            emit finished(false, err);
            return;
        }

        QMap<int, ActiveWindow> active;
        QDateTime t0 = params_.startUtc;
        const QDateTime tEnd = params_.endUtc;
        const qint64 totalSecs = std::max<qint64>(1, std::llabs(params_.startUtc.secsTo(params_.endUtc)));
        int lastProgress = -1;

        while (t0 < tEnd) {
            if (cancelled_.load()) {
                emit finished(true, QString());
                return;
            }

            double maxSpeed = 0.0;
            for (const auto& name : activePlanetNames_) {
                double speed = 0.0;
                if (planetSpeed(t0, name, &speed, nullptr)) {
                    maxSpeed = std::max(maxSpeed, std::fabs(speed));
                }
            }
            double stepDays = transitcalc::clampStepDays(maxSpeed);
            if (params_.useOrb && maxSpeed > 0.0 && params_.orbDeg > 0.0) {
                const double orbStep = params_.orbDeg / (maxSpeed * 4.0);
                stepDays = std::min(stepDays, std::clamp(orbStep, 0.02, 2.0));
            }
            if (stepDays < 0.02) {
                stepDays = 0.02;
            }

            QDateTime t1 = t0.addSecs(static_cast<qint64>(stepDays * 86400.0));
            if (t1 > tEnd) {
                t1 = tEnd;
            }

            State state1;
            if (!computeState(t1, &state1, &err)) {
                emit finished(false, err);
                return;
            }

            QDateTime boundary;
            bool hasBoundary = false;
            for (const auto& name : activePlanetNames_) {
                const int bucket0 = state0.planetBucket.value(name, -1);
                const int bucket1 = state1.planetBucket.value(name, -1);
                if (bucket0 < 0 || bucket1 < 0 || bucket0 == bucket1) {
                    continue;
                }
                QDateTime changeTime = refineBucketChange(t0, t1, name, bucket0);
                if (!hasBoundary || changeTime < boundary) {
                    boundary = changeTime;
                    hasBoundary = true;
                }
            }

            if (!hasBoundary && params_.useOrb) {
                QSet<int> buckets;
                for (auto it = state0.bucketEvals.begin(); it != state0.bucketEvals.end(); ++it) {
                    buckets.insert(it.key());
                }
                for (auto it = state1.bucketEvals.begin(); it != state1.bucketEvals.end(); ++it) {
                    buckets.insert(it.key());
                }
                for (int bucketId : buckets) {
                    const bool qual0 = state0.bucketEvals.contains(bucketId) && state0.bucketEvals.value(bucketId).qualifies;
                    const bool qual1 = state1.bucketEvals.contains(bucketId) && state1.bucketEvals.value(bucketId).qualifies;
                    if (qual0 == qual1) {
                        continue;
                    }
                    QDateTime changeTime = refineQualChange(t0, t1, bucketId, qual0);
                    if (!hasBoundary || changeTime < boundary) {
                        boundary = changeTime;
                        hasBoundary = true;
                    }
                }
            }

            if (hasBoundary && boundary > t0 && boundary < t1) {
                processState(t0, state0, &active);
                State stateBoundary;
                if (!computeState(boundary, &stateBoundary, &err)) {
                    emit finished(false, err);
                    return;
                }
                t0 = boundary;
                state0 = stateBoundary;
            } else {
                processState(t0, state0, &active);
                t0 = t1;
                state0 = state1;
            }

            const qint64 elapsed = params_.startUtc.secsTo(t0);
            const double progressRatio = static_cast<double>(std::min<qint64>(std::llabs(elapsed), totalSecs)) / totalSecs;
            const int progress = static_cast<int>(progressRatio * 100.0);
            if (progress != lastProgress && progress % 5 == 0) {
                lastProgress = progress;
                emit progressUpdate(progress, QString("Finding conjunctions (%1%)").arg(progress));
            }
        }

        for (auto it = active.begin(); it != active.end(); ++it) {
            emitWindow(it.key(), it.value(), tEnd);
        }

        std::sort(results_.begin(), results_.end(), [](const MainWindow::TransitConjunctionWindow& a,
                                                      const MainWindow::TransitConjunctionWindow& b) {
            if (a.startUtc == b.startUtc) {
                return a.bucketLabel < b.bucketLabel;
            }
            return a.startUtc < b.startUtc;
        });
        if (params_.uniqueFirstOnly) {
            if (!applyUniqueFirstFilter(&err)) {
                emit finished(false, err);
                return;
            }
        }
        if (cancelled_.load()) {
            emit finished(true, QString());
            return;
        }

        emit finished(false, QString());
    }

    void cancel() {
        cancelled_.store(true);
    }

signals:
    void progressUpdate(int percent, const QString& status);
    void finished(bool cancelled, const QString& error);

private:
    struct PlanetSample {
        QString name;
        double lon = 0.0;
        int bucket = 0;
    };
    struct BucketEval {
        QStringList planets;
        QStringList cluster;
        int bucketCount = 0;
        int clusterCount = 0;
        double clusterSpan = 0.0;
        bool qualifies = false;
    };
    struct State {
        QMap<QString, int> planetBucket;
        QMap<int, BucketEval> bucketEvals;
    };
    struct ActiveWindow {
        QDateTime startUtc;
        BucketEval eval;
    };

    bool planetLongitude(const QDateTime& utc, const QString& name, double* outLon, QString* error) {
        const int bodyId = transitcalc::bodyIdForName(
            name, effectivePrimaryNodeType(params_.lunarNodePolicy));
        if (bodyId < 0) {
            if (error) {
                *error = QString("Unsupported body: %1").arg(name);
            }
            return false;
        }
        const QDate date = utc.date();
        const QTime time = utc.time();
        const double hour = time.hour() + time.minute() / 60.0 + time.second() / 3600.0 + time.msec() / 3600000.0;
        const double jd = swe_.julianDay(date.year(), date.month(), date.day(), hour, SE_GREG_CAL);
        double lon = 0.0;
        QString calcErr;
        if (!swe_.calcUt(jd, bodyId, calcFlags_, &lon, &calcErr)) {
            if (error) {
                *error = calcErr;
            }
            return false;
        }
        lon = normalizeDegrees(lon);
        if (isLunarNodeName(name) && !isNorthLunarNodeName(name)) {
            lon = normalizeDegrees(lon + 180.0);
        }
        if (outLon) {
            *outLon = lon;
        }
        return true;
    }

    bool planetSpeed(const QDateTime& utc, const QString& name, double* outSpeed, QString* error) {
        const double deltaDays = 0.5;
        const QDateTime next = utc.addSecs(static_cast<qint64>(deltaDays * 86400.0));
        double lon0 = 0.0;
        double lon1 = 0.0;
        if (!planetLongitude(utc, name, &lon0, error)) {
            return false;
        }
        if (!planetLongitude(next, name, &lon1, error)) {
            return false;
        }
        const double diff = transitcalc::angularDiffSigned(lon1, lon0);
        if (outSpeed) {
            *outSpeed = diff / deltaDays;
        }
        return true;
    }

    int houseForLongitude(double lon, const QVector<double>& cusps, double asc) const {
        if (params_.houseSystem == HouseSystem::Placidus && cusps.size() == 12) {
            const double c1 = cusps[0];
            double target = normalizeDegrees(lon - c1);
            int house = 1;
            double last = 0.0;
            for (int i = 0; i < cusps.size(); ++i) {
                double v = normalizeDegrees(cusps[i] - c1);
                if (v < last) {
                    continue;
                }
                if (target >= v) {
                    house = i + 1;
                    last = v;
                }
            }
            return house;
        }
        const int ascIdx = signIndex(asc);
        const int lonIdx = signIndex(lon);
        return ((lonIdx - ascIdx + 12) % 12) + 1;
    }

    bool planetSample(const QDateTime& utc, const QString& name, PlanetSample* out, QString* error) {
        double lon = 0.0;
        if (!planetLongitude(utc, name, &lon, error)) {
            return false;
        }
        int bucket = 0;
        if (params_.bucketByHouse) {
            bucket = houseForLongitude(lon, params_.natalCusps, params_.natalAsc);
        } else {
            bucket = signIndex(lon);
        }
        if (out) {
            out->name = name;
            out->lon = lon;
            out->bucket = bucket;
        }
        return true;
    }

    bool computeState(const QDateTime& utc, State* out, QString* error) {
        QVector<PlanetSample> samples;
        samples.reserve(activePlanetNames_.size());
        QMap<QString, int> bucketMap;
        for (const auto& name : activePlanetNames_) {
            PlanetSample sample;
            if (!planetSample(utc, name, &sample, error)) {
                return false;
            }
            samples.push_back(sample);
            bucketMap.insert(name, sample.bucket);
        }

        QMap<int, QVector<PlanetSample>> buckets;
        for (const auto& sample : samples) {
            buckets[sample.bucket].push_back(sample);
        }

        QMap<int, BucketEval> evals;
        for (auto it = buckets.begin(); it != buckets.end(); ++it) {
            evals.insert(it.key(), evaluateBucket(it.value()));
        }

        if (out) {
            out->planetBucket = bucketMap;
            out->bucketEvals = evals;
        }
        return true;
    }

    BucketEval evaluateBucket(const QVector<PlanetSample>& samples) {
        BucketEval eval;
        eval.bucketCount = samples.size();
        if (eval.bucketCount == 0) {
            return eval;
        }

        QStringList ordered;
        QSet<QString> sampleNames;
        for (const auto& sample : samples) {
            sampleNames.insert(sample.name);
        }
        for (const auto& name : tropicalBodyOrder()) {
            if (sampleNames.contains(name)) {
                ordered.push_back(name);
            }
        }
        for (const auto& sample : samples) {
            if (!ordered.contains(sample.name)) {
                ordered.push_back(sample.name);
            }
        }
        eval.planets = ordered;

        if (!params_.useOrb) {
            eval.cluster = eval.planets;
            eval.clusterCount = eval.bucketCount;
            eval.clusterSpan = 0.0;
            eval.qualifies = eval.bucketCount >= params_.minCount;
            return eval;
        }

        if (eval.bucketCount < params_.minCount) {
            eval.cluster = eval.planets;
            eval.clusterCount = eval.bucketCount;
            eval.clusterSpan = 0.0;
            eval.qualifies = false;
            return eval;
        }

        struct LonRow {
            double lon;
            int index;
        };
        QVector<LonRow> sorted;
        sorted.reserve(samples.size());
        for (int i = 0; i < samples.size(); ++i) {
            sorted.push_back({samples[i].lon, i});
        }
        std::sort(sorted.begin(), sorted.end(), [](const LonRow& a, const LonRow& b) {
            return a.lon < b.lon;
        });

        const int m = sorted.size();
        QVector<double> lon2;
        QVector<int> idx2;
        lon2.reserve(m * 2);
        idx2.reserve(m * 2);
        for (int i = 0; i < m; ++i) {
            lon2.push_back(sorted[i].lon);
            idx2.push_back(sorted[i].index);
        }
        for (int i = 0; i < m; ++i) {
            lon2.push_back(sorted[i].lon + 360.0);
            idx2.push_back(sorted[i].index);
        }

        int bestSize = 0;
        double bestSpan = 0.0;
        int bestI = 0;
        int bestJ = -1;
        int j = 0;
        const double orb = params_.orbDeg;
        for (int i = 0; i < m; ++i) {
            if (j < i) {
                j = i;
            }
            while (j + 1 < i + m && lon2[j + 1] - lon2[i] <= orb + 1e-6) {
                ++j;
            }
            const int size = j - i + 1;
            const double span = lon2[j] - lon2[i];
            if (size > bestSize || (size == bestSize && span < bestSpan)) {
                bestSize = size;
                bestSpan = span;
                bestI = i;
                bestJ = j;
            }
        }

        QSet<int> clusterIndices;
        if (bestJ >= bestI) {
            for (int i = bestI; i <= bestJ; ++i) {
                clusterIndices.insert(idx2[i]);
            }
        }

        QStringList clusterNames;
        QSet<QString> clusterNameSet;
        for (int idx : clusterIndices) {
            if (idx >= 0 && idx < samples.size()) {
                clusterNameSet.insert(samples[idx].name);
            }
        }
        for (const auto& name : tropicalBodyOrder()) {
            if (clusterNameSet.contains(name)) {
                clusterNames.push_back(name);
            }
        }
        for (const auto& sample : samples) {
            if (clusterNameSet.contains(sample.name) && !clusterNames.contains(sample.name)) {
                clusterNames.push_back(sample.name);
            }
        }

        eval.cluster = clusterNames;
        eval.clusterCount = clusterNames.size();
        eval.clusterSpan = bestSpan;
        eval.qualifies = eval.clusterCount >= params_.minCount;
        return eval;
    }

    QString bucketLabelFor(int bucketId) const {
        if (params_.bucketByHouse) {
            return QString("House %1").arg(bucketId);
        }
        return signName(bucketId);
    }

    QStringList orderedPlanetNames(const QStringList& names) const {
        QStringList ordered;
        QSet<QString> seen;
        for (const auto& name : tropicalBodyOrder()) {
            if (names.contains(name) && !seen.contains(name)) {
                ordered.push_back(name);
                seen.insert(name);
            }
        }
        for (const auto& name : names) {
            if (!seen.contains(name)) {
                ordered.push_back(name);
                seen.insert(name);
            }
        }
        return ordered;
    }

    bool buildUniqueSignature(const MainWindow::TransitConjunctionWindow& result, QString* outSignature, QString* error) {
        const QStringList cluster = result.orbClusterAtStart.isEmpty()
            ? result.planetsInBucketAtStart
            : result.orbClusterAtStart;
        if (cluster.isEmpty()) {
            if (error) {
                *error = "Empty conjunction cluster.";
            }
            return false;
        }
        const QStringList ordered = orderedPlanetNames(cluster);
        if (ordered.isEmpty()) {
            if (error) {
                *error = "Unable to resolve conjunction cluster ordering.";
            }
            return false;
        }
        const double step = std::max(0.01, params_.uniqueDegreeStep);
        const int decimals = (step < 0.1) ? 2 : ((step < 1.0) ? 1 : 0);
        QStringList tokens;
        tokens.reserve(ordered.size());
        for (const auto& name : ordered) {
            double lon = 0.0;
            QString lonErr;
            if (!planetLongitude(result.startUtc, name, &lon, &lonErr)) {
                if (error) {
                    *error = QString("%1: %2").arg(name, lonErr);
                }
                return false;
            }
            int signIdx = signIndex(lon);
            double lonInSign = normalizeDegrees(lon) - static_cast<double>(signIdx) * 30.0;
            if (lonInSign < 0.0) {
                lonInSign += 30.0;
            }
            double roundedDeg = std::round(lonInSign / step) * step;
            if (roundedDeg >= 30.0 - 1e-6) {
                roundedDeg = 0.0;
                signIdx = (signIdx + 1) % 12;
            }
            tokens.push_back(QString("%1@%2 %3")
                                 .arg(name)
                                 .arg(signName(signIdx))
                                 .arg(QString::number(roundedDeg, 'f', decimals)));
        }
        if (outSignature) {
            *outSignature = tokens.join(" | ");
        }
        return true;
    }

    bool applyUniqueFirstFilter(QString* error) {
        QDateTime gateStart = params_.uniqueWindowStartUtc;
        QDateTime gateEnd = params_.uniqueWindowEndUtc;
        if (!gateStart.isValid()) {
            gateStart = params_.startUtc;
        }
        if (!gateEnd.isValid()) {
            gateEnd = params_.endUtc;
        }
        if (!gateStart.isValid() || !gateEnd.isValid() || gateStart > gateEnd) {
            if (error) {
                *error = "Invalid unique conjunction gate range.";
            }
            return false;
        }

        QSet<QString> seenSignatures;
        QVector<MainWindow::TransitConjunctionWindow> filtered;
        filtered.reserve(results_.size());
        for (const auto& event : results_) {
            if (cancelled_.load()) {
                return true;
            }
            QString signature;
            QString signatureErr;
            if (!buildUniqueSignature(event, &signature, &signatureErr)) {
                warnings_.push_back(QString("%1: %2")
                    .arg(event.startUtc.toString("yyyy-MM-dd HH:mm:ss"))
                    .arg(signatureErr));
                continue;
            }
            if (seenSignatures.contains(signature)) {
                continue;
            }
            seenSignatures.insert(signature);
            if (event.startUtc < gateStart || event.startUtc > gateEnd) {
                continue;
            }
            MainWindow::TransitConjunctionWindow tagged = event;
            tagged.uniqueFirstOccurrence = true;
            tagged.uniqueSignature = signature;
            filtered.push_back(tagged);
        }
        results_ = filtered;
        return true;
    }

    bool relativeLongitude(const QDateTime& utc, const QString& aName, const QString& bName,
                           double* outRel, QString* error) {
        double aLon = 0.0;
        double bLon = 0.0;
        if (!planetLongitude(utc, aName, &aLon, error)) {
            return false;
        }
        if (!planetLongitude(utc, bName, &bLon, error)) {
            return false;
        }
        if (outRel) {
            *outRel = normalizeDegrees(aLon - bLon);
        }
        return true;
    }

    bool exactPairCrossing(double rel0, double rel1) const {
        const double d0 = transitcalc::angularDiffSigned(rel0, 0.0);
        const double d1 = transitcalc::angularDiffSigned(rel1, 0.0);
        if (std::fabs(d0) <= 1e-9 || std::fabs(d1) <= 1e-9) {
            return true;
        }
        if (d0 * d1 > 0.0) {
            return false;
        }
        // Avoid false positives from the +/-180 discontinuity.
        return (std::fabs(d0) < 90.0 || std::fabs(d1) < 90.0);
    }

    QDateTime bisectRoot(const QDateTime& lo, const QDateTime& hi,
                         const std::function<double(const QDateTime&, double*)>& func) {
        QDateTime a = lo;
        QDateTime b = hi;
        double fa = 0.0;
        double fb = 0.0;
        func(a, &fa);
        func(b, &fb);
        for (int i = 0; i < 24; ++i) {
            if (cancelled_.load()) {
                return a;
            }
            if (a.secsTo(b) <= 60) {
                return b;
            }
            const QDateTime mid = transitcalc::midTimeUtc(a, b);
            double fm = 0.0;
            func(mid, &fm);
            if ((fa <= 0.0 && fm <= 0.0) || (fa >= 0.0 && fm >= 0.0)) {
                a = mid;
                fa = fm;
            } else {
                b = mid;
                fb = fm;
            }
        }
        return b;
    }

    QDateTime refineExactPairConjunction(const QDateTime& lo, const QDateTime& hi,
                                         const QString& aName, const QString& bName) {
        return bisectRoot(lo, hi, [&](const QDateTime& t, double* outDiff) {
            double rel = 0.0;
            if (!relativeLongitude(t, aName, bName, &rel, nullptr)) {
                if (outDiff) {
                    *outDiff = 0.0;
                }
                return 0.0;
            }
            const double diff = transitcalc::angularDiffSigned(rel, 0.0);
            if (outDiff) {
                *outDiff = diff;
            }
            return diff;
        });
    }

    void emitExactPairWindow(const QDateTime& hitUtc, const QString& aName, const QString& bName,
                             const QStringList& orderedPair) {
        double aLon = 0.0;
        if (!planetLongitude(hitUtc, aName, &aLon, nullptr)) {
            return;
        }
        int bucketId = 0;
        if (params_.bucketByHouse) {
            bucketId = houseForLongitude(aLon, params_.natalCusps, params_.natalAsc);
        } else {
            bucketId = signIndex(aLon);
        }

        MainWindow::TransitConjunctionWindow result;
        result.startUtc = hitUtc;
        result.endUtc = hitUtc;
        result.tzLabel = params_.tzLabel;
        result.bucketLabel = bucketLabelFor(bucketId);
        result.planetsInBucketAtStart = orderedPair;
        result.orbClusterAtStart = orderedPair;
        result.bucketCount = orderedPair.size();
        result.clusterCount = orderedPair.size();
        result.clusterSpanDeg = 0.0;
        results_.push_back(result);
    }

    bool runExactPairConjunctions(QString* error) {
        if (activePlanetNames_.size() != 2) {
            return false;
        }
        const QString aName = activePlanetNames_[0];
        const QString bName = activePlanetNames_[1];
        const QStringList orderedPair = orderedPlanetNames({aName, bName});
        const qint64 totalSecs = std::max<qint64>(1, std::llabs(params_.startUtc.secsTo(params_.endUtc)));
        int lastProgress = -1;

        QDateTime t0 = params_.startUtc;
        const QDateTime tEnd = params_.endUtc;
        double rel0 = 0.0;
        if (!relativeLongitude(t0, aName, bName, &rel0, error)) {
            return false;
        }

        QDateTime lastHit;
        auto emitIfDistinct = [&](const QDateTime& hit) {
            if (!hit.isValid()) {
                return;
            }
            if (hit < params_.startUtc || hit > params_.endUtc) {
                return;
            }
            if (lastHit.isValid() && std::llabs(lastHit.secsTo(hit)) <= 120) {
                return;
            }
            emitExactPairWindow(hit, aName, bName, orderedPair);
            lastHit = hit;
        };

        if (std::fabs(transitcalc::angularDiffSigned(rel0, 0.0)) <= 0.02) {
            emitIfDistinct(t0);
        }

        while (t0 < tEnd) {
            if (cancelled_.load()) {
                return true;
            }

            double maxSpeed = 0.0;
            double speedA = 0.0;
            double speedB = 0.0;
            if (!planetSpeed(t0, aName, &speedA, error)) {
                return false;
            }
            if (!planetSpeed(t0, bName, &speedB, error)) {
                return false;
            }
            maxSpeed = std::max(std::fabs(speedA), std::fabs(speedB));
            double stepDays = transitcalc::clampStepDays(maxSpeed);
            stepDays = std::min(stepDays, 1.0);
            if (stepDays < 0.02) {
                stepDays = 0.02;
            }

            QDateTime t1 = t0.addSecs(static_cast<qint64>(stepDays * 86400.0));
            if (t1 > tEnd) {
                t1 = tEnd;
            }

            double rel1 = 0.0;
            if (!relativeLongitude(t1, aName, bName, &rel1, error)) {
                return false;
            }
            if (exactPairCrossing(rel0, rel1)) {
                const QDateTime hit = refineExactPairConjunction(t0, t1, aName, bName);
                double relHit = 0.0;
                if (relativeLongitude(hit, aName, bName, &relHit, nullptr)
                    && std::fabs(transitcalc::angularDiffSigned(relHit, 0.0)) <= 0.02) {
                    emitIfDistinct(hit);
                }
            }

            t0 = t1;
            rel0 = rel1;

            const qint64 elapsed = params_.startUtc.secsTo(t0);
            const double progressRatio = static_cast<double>(std::min<qint64>(std::llabs(elapsed), totalSecs)) / totalSecs;
            const int progress = static_cast<int>(progressRatio * 100.0);
            if (progress != lastProgress && progress % 5 == 0) {
                lastProgress = progress;
                emit progressUpdate(progress, QString("Finding exact conjunctions (%1%)").arg(progress));
            }
        }

        return true;
    }

    void processState(const QDateTime& utc, const State& state, QMap<int, ActiveWindow>* active) {
        if (!active) {
            return;
        }
        QSet<int> seenBuckets;
        for (auto it = state.bucketEvals.begin(); it != state.bucketEvals.end(); ++it) {
            const int bucketId = it.key();
            const BucketEval& eval = it.value();
            seenBuckets.insert(bucketId);

            if (eval.qualifies) {
                if (!active->contains(bucketId)) {
                    ActiveWindow window;
                    window.startUtc = utc;
                    window.eval = eval;
                    active->insert(bucketId, window);
                } else {
                    const BucketEval& existing = active->value(bucketId).eval;
                    const QStringList& currentList = params_.useOrb ? eval.cluster : eval.planets;
                    const QStringList& existingList = params_.useOrb ? existing.cluster : existing.planets;
                    if (currentList != existingList) {
                        emitWindow(bucketId, active->value(bucketId), utc);
                        ActiveWindow window;
                        window.startUtc = utc;
                        window.eval = eval;
                        (*active)[bucketId] = window;
                    }
                }
            } else {
                if (active->contains(bucketId)) {
                    emitWindow(bucketId, active->value(bucketId), utc);
                    active->remove(bucketId);
                }
            }
        }

        for (auto it = active->begin(); it != active->end();) {
            if (!seenBuckets.contains(it.key())) {
                emitWindow(it.key(), it.value(), utc);
                it = active->erase(it);
            } else {
                ++it;
            }
        }
    }

    void emitWindow(int bucketId, const ActiveWindow& window, const QDateTime& endUtc) {
        MainWindow::TransitConjunctionWindow result;
        result.startUtc = window.startUtc;
        result.endUtc = endUtc;
        result.tzLabel = params_.tzLabel;
        result.bucketLabel = bucketLabelFor(bucketId);
        result.planetsInBucketAtStart = window.eval.planets;
        result.orbClusterAtStart = window.eval.cluster;
        result.bucketCount = window.eval.bucketCount;
        result.clusterCount = window.eval.clusterCount;
        result.clusterSpanDeg = window.eval.clusterSpan;
        results_.push_back(result);
    }

    QDateTime refineBucketChange(const QDateTime& lo, const QDateTime& hi,
                                 const QString& planetName, int bucketAtLo) {
        QDateTime a = lo;
        QDateTime b = hi;
        for (int i = 0; i < 24; ++i) {
            if (cancelled_.load()) {
                return a;
            }
            const QDateTime mid = transitcalc::midTimeUtc(a, b);
            PlanetSample sample;
            if (!planetSample(mid, planetName, &sample, nullptr)) {
                return b;
            }
            if (sample.bucket == bucketAtLo) {
                a = mid;
            } else {
                b = mid;
            }
        }
        return b;
    }

    QDateTime refineQualChange(const QDateTime& lo, const QDateTime& hi,
                               int bucketId, bool qualAtLo) {
        QDateTime a = lo;
        QDateTime b = hi;
        for (int i = 0; i < 24; ++i) {
            if (cancelled_.load()) {
                return a;
            }
            const QDateTime mid = transitcalc::midTimeUtc(a, b);
            State state;
            if (!computeState(mid, &state, nullptr)) {
                return b;
            }
            const bool qualMid = state.bucketEvals.contains(bucketId)
                && state.bucketEvals.value(bucketId).qualifies;
            if (qualMid == qualAtLo) {
                a = mid;
            } else {
                b = mid;
            }
        }
        return b;
    }

    ConjunctionParams params_;
    QStringList activePlanetNames_;
    SwissEph swe_;
    int calcFlags_ = 0;
    std::atomic<bool> cancelled_{false};
    QVector<MainWindow::TransitConjunctionWindow> results_;
    QStringList warnings_;
};

class LunationWorker : public QObject {
    Q_OBJECT

public:
    explicit LunationWorker(const LunationParams& params)
        : params_(params) {}

    const QVector<MainWindow::LunationResult>& results() const { return results_; }

public slots:
    void run() {
        QString err;
        if (!swe_.load(params_.dllSearchPaths, &err)) {
            emit finished(false, err);
            return;
        }
        if (!params_.ephePath.isEmpty()) {
            swe_.setEphePath(params_.ephePath);
        }
        calcFlags_ = 0;
        if (params_.zodiacSystem == ZodiacSystem::Sidereal) {
            swe_.setSidMode(siderealAyanamsaSwissMode(params_.siderealAyanamsa));
            calcFlags_ = SEFLG_SIDEREAL;
        }

        results_.clear();
        QString runErr;
        if (params_.findMode == LunationFindMode::Range) {
            if (!runRange(&runErr)) {
                if (cancelled_.load()) {
                    emit finished(true, QString());
                } else {
                    emit finished(false, runErr);
                }
                return;
            }
        } else {
            const bool forward = (params_.findMode == LunationFindMode::Next);
            if (!runSingle(forward, &runErr)) {
                if (cancelled_.load()) {
                    emit finished(true, QString());
                } else {
                    emit finished(false, runErr);
                }
                return;
            }
        }
        if (cancelled_.load()) {
            emit finished(true, QString());
            return;
        }
        emit finished(false, QString());
    }

    void cancel() {
        cancelled_.store(true);
    }

signals:
    void progressUpdate(int percent, const QString& status);
    void finished(bool cancelled, const QString& error);

private:
    static bool targetBetween(double startAngle, double endAngle, double target) {
        double a0 = startAngle;
        double a1 = endAngle;
        double t = target;
        if (a1 < a0) {
            a1 += 360.0;
            if (t < a0) {
                t += 360.0;
            }
        }
        return (t >= a0 && t <= a1);
    }

    static bool isSameInstant(const QDateTime& a, const QDateTime& b, int toleranceSeconds) {
        if (!a.isValid() || !b.isValid()) {
            return false;
        }
        return std::llabs(a.secsTo(b)) <= toleranceSeconds;
    }

    static bool isNewMoonLabel(const QString& label) {
        return label.contains("New Moon", Qt::CaseInsensitive);
    }

    static bool isFullMoonLabel(const QString& label) {
        return label.contains("Full Moon", Qt::CaseInsensitive);
    }

    static bool isSolarEclipseLabel(const QString& label) {
        return label.contains("Solar Eclipse", Qt::CaseInsensitive);
    }

    static bool isLunarEclipseLabel(const QString& label) {
        return label.contains("Lunar Eclipse", Qt::CaseInsensitive);
    }

    static QString mergeEventLabel(const QString& a, const QString& b) {
        const bool newMoon = isNewMoonLabel(a) || isNewMoonLabel(b);
        const bool fullMoon = isFullMoonLabel(a) || isFullMoonLabel(b);
        const bool solar = isSolarEclipseLabel(a) || isSolarEclipseLabel(b);
        const bool lunar = isLunarEclipseLabel(a) || isLunarEclipseLabel(b);

        if (solar && newMoon) {
            return "Solar Eclipse (New Moon)";
        }
        if (lunar && fullMoon) {
            return "Lunar Eclipse (Full Moon)";
        }
        if (solar) {
            return "Solar Eclipse";
        }
        if (lunar) {
            return "Lunar Eclipse";
        }
        if (newMoon) {
            return "New Moon";
        }
        if (fullMoon) {
            return "Full Moon";
        }
        return a;
    }

    bool matchesDegreeRange(double moonLon) const {
        if (!params_.useDegreeRange) {
            return true;
        }
        const double degree = degInSign(moonLon);
        const double start = std::clamp(params_.degreeRangeStart, 0.0, 29.99);
        const double end = std::clamp(params_.degreeRangeEnd, 0.0, 29.99);
        if (start <= end) {
            return degree >= (start - 1e-9) && degree <= (end + 1e-9);
        }
        // Wrap-around range support, e.g. 29.0 -> 2.0.
        return degree >= (start - 1e-9) || degree <= (end + 1e-9);
    }

    bool phaseAngleAtUtc(const QDateTime& utc, double* outAngle, QString* error) {
        if (!outAngle) {
            return false;
        }
        const QDate date = utc.date();
        const QTime time = utc.time();
        const double hour = time.hour() + time.minute() / 60.0 + time.second() / 3600.0 + time.msec() / 3600000.0;
        const double jd = swe_.julianDay(date.year(), date.month(), date.day(), hour, SE_GREG_CAL);
        double sunLon = 0.0;
        double moonLon = 0.0;
        QString calcErr;
        if (!swe_.calcUt(jd, SE_SUN, calcFlags_, &sunLon, &calcErr)) {
            if (error) {
                *error = calcErr;
            }
            return false;
        }
        if (!swe_.calcUt(jd, SE_MOON, calcFlags_, &moonLon, &calcErr)) {
            if (error) {
                *error = calcErr;
            }
            return false;
        }
        *outAngle = normalizeDegrees(moonLon - sunLon);
        return true;
    }

    bool sunMoonLonAtUtc(const QDateTime& utc, double* outSun, double* outMoon, QString* error) {
        if (!outSun || !outMoon) {
            return false;
        }
        const QDate date = utc.date();
        const QTime time = utc.time();
        const double hour = time.hour() + time.minute() / 60.0 + time.second() / 3600.0 + time.msec() / 3600000.0;
        const double jd = swe_.julianDay(date.year(), date.month(), date.day(), hour, SE_GREG_CAL);
        QString calcErr;
        if (!swe_.calcUt(jd, SE_SUN, calcFlags_, outSun, &calcErr)) {
            if (error) {
                *error = calcErr;
            }
            return false;
        }
        if (!swe_.calcUt(jd, SE_MOON, calcFlags_, outMoon, &calcErr)) {
            if (error) {
                *error = calcErr;
            }
            return false;
        }
        *outSun = normalizeDegrees(*outSun);
        *outMoon = normalizeDegrees(*outMoon);
        return true;
    }

    bool nodeLonAtUtc(const QDateTime& utc, double* outNorthNode, QString* error) {
        if (!outNorthNode) {
            return false;
        }
        const QDate date = utc.date();
        const QTime time = utc.time();
        const double hour = time.hour() + time.minute() / 60.0 + time.second() / 3600.0 + time.msec() / 3600000.0;
        const double jd = swe_.julianDay(date.year(), date.month(), date.day(), hour, SE_GREG_CAL);
        QString calcErr;
        const int nodeId = effectivePrimaryNodeType(params_.lunarNodePolicy) == LunarNodeType::True
            ? SE_TRUE_NODE
            : SE_MEAN_NODE;
        if (!swe_.calcUt(jd, nodeId, calcFlags_, outNorthNode, &calcErr)) {
            if (error) {
                *error = calcErr;
            }
            return false;
        }
        *outNorthNode = normalizeDegrees(*outNorthNode);
        return true;
    }

    // Longitude of the conjunction-test planet at a given instant. Swiss
    // Ephemeris exposes a north-node body for each model; south is opposite it.
    bool conjunctionPlanetLonAtUtc(const QDateTime& utc, double* outLon, QString* error) {
        if (!outLon) {
            return false;
        }
        const QString name = params_.conjunctionPlanet;
        const int bodyId = transitcalc::bodyIdForName(
            name, effectivePrimaryNodeType(params_.lunarNodePolicy));
        if (bodyId < 0) {
            if (error) {
                *error = QString("Unsupported conjunction planet: %1").arg(name);
            }
            return false;
        }
        const QDate date = utc.date();
        const QTime time = utc.time();
        const double hour = time.hour() + time.minute() / 60.0 + time.second() / 3600.0 + time.msec() / 3600000.0;
        const double jd = swe_.julianDay(date.year(), date.month(), date.day(), hour, SE_GREG_CAL);
        double lon = 0.0;
        QString calcErr;
        if (!swe_.calcUt(jd, bodyId, calcFlags_, &lon, &calcErr)) {
            if (error) {
                *error = calcErr;
            }
            return false;
        }
        if (isLunarNodeName(name) && !isNorthLunarNodeName(name)) {
            lon += 180.0;
        }
        *outLon = normalizeDegrees(lon);
        return true;
    }

    // Returns true if the event passes the optional planet-conjunction filter.
    // When it passes, *outSummary describes the tightest matching conjunction.
    bool passesPlanetConjunction(const QDateTime& utc, double sunLon, double moonLon, QString* outSummary) {
        if (!params_.requirePlanetConjunction) {
            if (outSummary) {
                outSummary->clear();
            }
            return true;
        }
        double planetLon = 0.0;
        if (!conjunctionPlanetLonAtUtc(utc, &planetLon, nullptr)) {
            return false;
        }
        double nodeLon = 0.0;
        bool haveNode = false;
        if (params_.targetNorthNode || params_.targetSouthNode) {
            haveNode = nodeLonAtUtc(utc, &nodeLon, nullptr);
        }
        auto sep = [](double a, double b) {
            double d = std::fabs(normalizeDegrees(a) - normalizeDegrees(b));
            if (d > 180.0) {
                d = 360.0 - d;
            }
            return d;
        };
        double bestOrb = 1e9;
        QString bestTarget;
        auto consider = [&](const QString& name, double tlon) {
            const double d = sep(planetLon, tlon);
            if (d < bestOrb) {
                bestOrb = d;
                bestTarget = name;
            }
        };
        if (params_.targetSun) {
            consider("Sun", sunLon);
        }
        if (params_.targetMoon) {
            consider("Moon", moonLon);
        }
        if (params_.targetNorthNode && haveNode) {
            consider(lunarNodeDisplayName("North Node", params_.lunarNodePolicy), nodeLon);
        }
        if (params_.targetSouthNode && haveNode) {
            consider(lunarNodeDisplayName("South Node", params_.lunarNodePolicy), nodeLon + 180.0);
        }
        if (bestTarget.isEmpty() || bestOrb > params_.conjunctionOrb) {
            return false;
        }
        if (outSummary) {
            *outSummary = QString("%1 conj %2 (%3%4)")
                              .arg(lunarNodeDisplayName(params_.conjunctionPlanet, params_.lunarNodePolicy),
                                   bestTarget,
                                   QString::number(bestOrb, 'f', 1), QString(QChar(0x00B0)));
        }
        return true;
    }

    bool passesSiderealWholeSignEclipseRule(bool solar, const QDateTime& eventUtc,
                                            double sunLon, double moonLon, QString* error) {
        if (params_.zodiacSystem != ZodiacSystem::Sidereal) {
            return true;
        }

        double northNodeLon = 0.0;
        if (!nodeLonAtUtc(eventUtc, &northNodeLon, error)) {
            return false;
        }
        const double southNodeLon = normalizeDegrees(northNodeLon + 180.0);

        const int sunSign = signIndex(sunLon);
        const int moonSign = signIndex(moonLon);
        const int northSign = signIndex(northNodeLon);
        const int southSign = signIndex(southNodeLon);

        if (solar) {
            // Sidereal whole-sign eclipse gate: new-moon sign must be exactly on the node axis.
            return sunSign == moonSign && (sunSign == northSign || sunSign == southSign);
        }

        // Sidereal whole-sign eclipse gate: full-moon signs must match the node axis signs.
        return (moonSign == northSign && sunSign == southSign)
            || (moonSign == southSign && sunSign == northSign);
    }

    bool jdToUtc(double jd, QDateTime* outUtc, QString* error) {
        if (!outUtc) {
            return false;
        }
        int year = 0;
        int month = 0;
        int day = 0;
        double hour = 0.0;
        QString err;
        if (!swe_.revJul(jd, SE_GREG_CAL, &year, &month, &day, &hour, &err)) {
            if (error) {
                *error = err;
            }
            return false;
        }
        QDate date(year, month, day);
        if (!date.isValid()) {
            if (error) {
                *error = "Invalid Julian date conversion.";
            }
            return false;
        }
        qint64 totalMs = static_cast<qint64>(std::llround(hour * 3600000.0));
        while (totalMs >= 86400000) {
            totalMs -= 86400000;
            date = date.addDays(1);
        }
        while (totalMs < 0) {
            totalMs += 86400000;
            date = date.addDays(-1);
        }
        QTime time = QTime::fromMSecsSinceStartOfDay(static_cast<int>(totalMs));
        if (!time.isValid()) {
            if (error) {
                *error = "Invalid Julian time conversion.";
            }
            return false;
        }
        *outUtc = QDateTime(date, time, QTimeZone::utc());
        return true;
    }

    bool findNextPhase(const QDateTime& startUtc, double targetAngle, QDateTime* outUtc, QString* error) {
        if (!outUtc) {
            return false;
        }
        double angle0 = 0.0;
        if (!phaseAngleAtUtc(startUtc, &angle0, error)) {
            return false;
        }
        QDateTime lo = startUtc;
        double aLo = angle0;
        const double stepDays = 1.0;
        const int maxSteps = 90;
        for (int i = 0; i < maxSteps; ++i) {
            if (cancelled_.load()) {
                return false;
            }
            QDateTime hi = lo.addSecs(static_cast<qint64>(stepDays * 86400.0));
            double aHi = 0.0;
            if (!phaseAngleAtUtc(hi, &aHi, error)) {
                return false;
            }
            if (targetBetween(aLo, aHi, targetAngle)) {
                QDateTime left = lo;
                QDateTime right = hi;
                double aLeft = aLo;
                for (int iter = 0; iter < 40; ++iter) {
                    if (cancelled_.load()) {
                        return false;
                    }
                    const QDateTime mid = transitcalc::midTimeUtc(left, right);
                    double aMid = 0.0;
                    if (!phaseAngleAtUtc(mid, &aMid, nullptr)) {
                        break;
                    }
                    if (targetBetween(aLeft, aMid, targetAngle)) {
                        right = mid;
                    } else {
                        left = mid;
                        aLeft = aMid;
                    }
                }
                *outUtc = right;
                return true;
            }
            lo = hi;
            aLo = aHi;
        }
        if (error) {
            *error = "Unable to locate lunation in search window.";
        }
        return false;
    }

    bool findPreviousPhase(const QDateTime& startUtc, double targetAngle, QDateTime* outUtc, QString* error) {
        if (!outUtc) {
            return false;
        }
        double angle0 = 0.0;
        if (!phaseAngleAtUtc(startUtc, &angle0, error)) {
            return false;
        }
        QDateTime hi = startUtc;
        double aHi = angle0;
        const double stepDays = 1.0;
        const int maxSteps = 90;
        for (int i = 0; i < maxSteps; ++i) {
            if (cancelled_.load()) {
                return false;
            }
            QDateTime lo = hi.addSecs(static_cast<qint64>(-stepDays * 86400.0));
            double aLo = 0.0;
            if (!phaseAngleAtUtc(lo, &aLo, error)) {
                return false;
            }
            if (targetBetween(aLo, aHi, targetAngle)) {
                QDateTime left = lo;
                QDateTime right = hi;
                double aLeft = aLo;
                for (int iter = 0; iter < 40; ++iter) {
                    if (cancelled_.load()) {
                        return false;
                    }
                    const QDateTime mid = transitcalc::midTimeUtc(left, right);
                    double aMid = 0.0;
                    if (!phaseAngleAtUtc(mid, &aMid, nullptr)) {
                        break;
                    }
                    if (targetBetween(aLeft, aMid, targetAngle)) {
                        right = mid;
                    } else {
                        left = mid;
                        aLeft = aMid;
                    }
                }
                *outUtc = right;
                return true;
            }
            hi = lo;
            aHi = aLo;
        }
        if (error) {
            *error = "Unable to locate lunation in search window.";
        }
        return false;
    }

    QString eclipseTypeForFlags(int flags, bool solar) const {
        if (solar) {
            if (flags & SE_ECL_TOTAL) {
                return "Total";
            }
            if (flags & SE_ECL_ANNULAR_TOTAL) {
                return "Hybrid";
            }
            if (flags & SE_ECL_ANNULAR) {
                return "Annular";
            }
            if (flags & SE_ECL_PARTIAL) {
                return "Partial";
            }
        } else {
            if (flags & SE_ECL_TOTAL) {
                return "Total";
            }
            if (flags & SE_ECL_PARTIAL) {
                return "Partial";
            }
            if (flags & SE_ECL_PENUMBRAL) {
                return "Penumbral";
            }
        }
        return QString();
    }

    void addResult(const QDateTime& utc, const QString& eventLabel, const QString& eclipseType, int eclipseFlags,
                   double sunLon, double moonLon) {
        QString conjunctionSummary;
        if (!passesPlanetConjunction(utc, sunLon, moonLon, &conjunctionSummary)) {
            return;  // optional planet-conjunction filter rejected this event
        }
        MainWindow::LunationResult incoming;
        incoming.timeUtc = utc;
        incoming.timeLocal = utc.toTimeZone(params_.tz);
        incoming.tzLabel = params_.tzLabel;
        incoming.event = eventLabel;
        incoming.eclipseType = eclipseType;
        incoming.sunLon = sunLon;
        incoming.moonLon = moonLon;
        incoming.eclipseFlags = eclipseFlags;
        incoming.conjunctionSummary = conjunctionSummary;

        constexpr int kMergeToleranceSeconds = 120;
        for (auto& existing : results_) {
            if (!isSameInstant(existing.timeUtc, incoming.timeUtc, kMergeToleranceSeconds)) {
                continue;
            }
            existing.event = mergeEventLabel(existing.event, incoming.event);
            if (!incoming.eclipseType.isEmpty()) {
                existing.eclipseType = incoming.eclipseType;
            }
            if (incoming.eclipseFlags != 0) {
                existing.eclipseFlags = incoming.eclipseFlags;
            }
            if (incoming.timeUtc < existing.timeUtc) {
                existing.timeUtc = incoming.timeUtc;
                existing.timeLocal = incoming.timeLocal;
            }
            existing.sunLon = incoming.sunLon;
            existing.moonLon = incoming.moonLon;
            if (existing.conjunctionSummary.isEmpty()) {
                existing.conjunctionSummary = incoming.conjunctionSummary;
            }
            return;
        }

        results_.push_back(incoming);
    }

    bool findMatchingPhase(const QDateTime& anchor, double targetAngle, bool forward, const QString& label,
                           QDateTime* outUtc, double* outSunLon, double* outMoonLon, QString* error) {
        QDateTime cursor = anchor;
        for (int i = 0; i < 600; ++i) {
            if (cancelled_.load()) {
                return false;
            }
            QDateTime eventUtc;
            const bool ok = forward
                ? findNextPhase(cursor, targetAngle, &eventUtc, error)
                : findPreviousPhase(cursor, targetAngle, &eventUtc, error);
            if (!ok) {
                return false;
            }
            double sunLon = 0.0;
            double moonLon = 0.0;
            if (!sunMoonLonAtUtc(eventUtc, &sunLon, &moonLon, error)) {
                return false;
            }
            if (matchesDegreeRange(moonLon)) {
                if (outUtc) *outUtc = eventUtc;
                if (outSunLon) *outSunLon = sunLon;
                if (outMoonLon) *outMoonLon = moonLon;
                return true;
            }
            if (forward) {
                cursor = eventUtc > cursor ? eventUtc.addSecs(60) : cursor.addSecs(60);
            } else {
                cursor = eventUtc < cursor ? eventUtc.addSecs(-60) : cursor.addSecs(-60);
            }
        }
        if (error) {
            *error = QString("No %1 found in the selected degree range.").arg(label.toLower());
        }
        return false;
    }

    bool findMatchingEclipse(const QDateTime& anchor, bool solar, bool forward,
                             QDateTime* outUtc, QString* outType, int* outFlags,
                             double* outSunLon, double* outMoonLon, QString* error) {
        QDateTime cursor = anchor;
        for (int i = 0; i < 800; ++i) {
            if (cancelled_.load()) {
                return false;
            }
            double tret[10] = {0};
            const double jdStart = toJulianDay(cursor);
            const int backward = forward ? 0 : 1;
            const int ret = solar
                ? swe_.solEclipseWhenGlob(jdStart, 0, 0, tret, backward, error)
                : swe_.lunEclipseWhen(jdStart, 0, 0, tret, backward, error);
            if (ret < 0) {
                return false;
            }
            QDateTime eventUtc;
            if (!jdToUtc(tret[0], &eventUtc, error)) {
                return false;
            }
            double sunLon = 0.0;
            double moonLon = 0.0;
            if (!sunMoonLonAtUtc(eventUtc, &sunLon, &moonLon, error)) {
                return false;
            }
            if (matchesDegreeRange(moonLon) && passesSiderealWholeSignEclipseRule(solar, eventUtc, sunLon, moonLon, error)) {
                if (outUtc) *outUtc = eventUtc;
                if (outType) *outType = eclipseTypeForFlags(ret, solar);
                if (outFlags) *outFlags = ret;
                if (outSunLon) *outSunLon = sunLon;
                if (outMoonLon) *outMoonLon = moonLon;
                return true;
            }
            if (forward) {
                cursor = eventUtc > cursor ? eventUtc.addSecs(60) : cursor.addSecs(60);
            } else {
                cursor = eventUtc < cursor ? eventUtc.addSecs(-60) : cursor.addSecs(-60);
            }
        }
        if (error) {
            *error = QString("No %1 found in the selected degree range.")
                .arg(solar ? "solar eclipse" : "lunar eclipse");
        }
        return false;
    }

    bool findMatchingStrictVedicEclipse(const QDateTime& anchor, bool solar, bool forward,
                                        QDateTime* outUtc, QString* outType, int* outFlags,
                                        double* outSunLon, double* outMoonLon, QString* error) {
        if (params_.zodiacSystem != ZodiacSystem::Sidereal) {
            if (error) {
                *error = "Strict Vedic eclipse rule is only available in sidereal mode.";
            }
            return false;
        }
        const double targetAngle = solar ? 0.0 : 180.0;
        QDateTime cursor = anchor;
        for (int i = 0; i < 800; ++i) {
            if (cancelled_.load()) {
                return false;
            }
            QDateTime eventUtc;
            const bool ok = forward
                ? findNextPhase(cursor, targetAngle, &eventUtc, error)
                : findPreviousPhase(cursor, targetAngle, &eventUtc, error);
            if (!ok) {
                return false;
            }
            double sunLon = 0.0;
            double moonLon = 0.0;
            if (!sunMoonLonAtUtc(eventUtc, &sunLon, &moonLon, error)) {
                return false;
            }
            if (matchesDegreeRange(moonLon)
                && passesSiderealWholeSignEclipseRule(solar, eventUtc, sunLon, moonLon, error)) {
                if (outUtc) *outUtc = eventUtc;
                if (outType) *outType = "Vedic";
                if (outFlags) *outFlags = 0;
                if (outSunLon) *outSunLon = sunLon;
                if (outMoonLon) *outMoonLon = moonLon;
                return true;
            }
            if (forward) {
                cursor = eventUtc > cursor ? eventUtc.addSecs(60) : cursor.addSecs(60);
            } else {
                cursor = eventUtc < cursor ? eventUtc.addSecs(-60) : cursor.addSecs(-60);
            }
        }
        if (error) {
            *error = QString("No strict vedic %1 eclipse found in the selected degree range.")
                .arg(solar ? "solar" : "lunar");
        }
        return false;
    }

    bool runSingle(bool forward, QString* error) {
        const QDateTime anchor = params_.startUtc;
        const double newAngle = 0.0;
        const double fullAngle = 180.0;
        const bool useStrictVedic =
            params_.zodiacSystem == ZodiacSystem::Sidereal
            && params_.eclipseRule == LunationEclipseRule::StrictVedicWholeSign;
        if (params_.includeNewMoon) {
            QDateTime eventUtc;
            double sunLon = 0.0;
            double moonLon = 0.0;
            if (!findMatchingPhase(anchor, newAngle, forward, "New Moon", &eventUtc, &sunLon, &moonLon, error)) {
                return false;
            }
            addResult(eventUtc, "New Moon", QString(), 0, sunLon, moonLon);
        }
        if (params_.includeFullMoon) {
            QDateTime eventUtc;
            double sunLon = 0.0;
            double moonLon = 0.0;
            if (!findMatchingPhase(anchor, fullAngle, forward, "Full Moon", &eventUtc, &sunLon, &moonLon, error)) {
                return false;
            }
            addResult(eventUtc, "Full Moon", QString(), 0, sunLon, moonLon);
        }
        if (params_.includeSolarEclipse) {
            QDateTime eventUtc;
            QString type;
            int flags = 0;
            double sunLon = 0.0;
            double moonLon = 0.0;
            const bool eclipseOk = useStrictVedic
                ? findMatchingStrictVedicEclipse(anchor, true, forward, &eventUtc, &type, &flags, &sunLon, &moonLon, error)
                : findMatchingEclipse(anchor, true, forward, &eventUtc, &type, &flags, &sunLon, &moonLon, error);
            if (!eclipseOk) {
                return false;
            }
            addResult(eventUtc, "Solar Eclipse", type, flags, sunLon, moonLon);
        }
        if (params_.includeLunarEclipse) {
            QDateTime eventUtc;
            QString type;
            int flags = 0;
            double sunLon = 0.0;
            double moonLon = 0.0;
            const bool eclipseOk = useStrictVedic
                ? findMatchingStrictVedicEclipse(anchor, false, forward, &eventUtc, &type, &flags, &sunLon, &moonLon, error)
                : findMatchingEclipse(anchor, false, forward, &eventUtc, &type, &flags, &sunLon, &moonLon, error);
            if (!eclipseOk) {
                return false;
            }
            addResult(eventUtc, "Lunar Eclipse", type, flags, sunLon, moonLon);
        }
        return true;
    }

    bool runRange(QString* error) {
        const double newAngle = 0.0;
        const double fullAngle = 180.0;
        const bool useStrictVedic =
            params_.zodiacSystem == ZodiacSystem::Sidereal
            && params_.eclipseRule == LunationEclipseRule::StrictVedicWholeSign;
        const QDateTime startUtc = params_.startUtc;
        const QDateTime endUtc = params_.endUtc;
        if (!startUtc.isValid() || !endUtc.isValid() || startUtc > endUtc) {
            if (error) {
                *error = "Invalid lunation range.";
            }
            return false;
        }

        auto updateProgress = [&]() {
            emit progressUpdate(-1, QString("Found %1 events").arg(results_.size()));
        };

        auto nextPhaseLoop = [&](double targetAngle, const QString& label) -> bool {
            QDateTime cursor = startUtc;
            QDateTime eventUtc;
            QString localErr;
            if (!findNextPhase(cursor, targetAngle, &eventUtc, &localErr)) {
                if (error) {
                    *error = localErr;
                }
                return false;
            }
            while (eventUtc <= endUtc) {
                if (cancelled_.load()) {
                    return false;
                }
                double sunLon = 0.0;
                double moonLon = 0.0;
                if (!sunMoonLonAtUtc(eventUtc, &sunLon, &moonLon, error)) {
                    return false;
                }
                if (matchesDegreeRange(moonLon)) {
                    addResult(eventUtc, label, QString(), 0, sunLon, moonLon);
                    updateProgress();
                }
                cursor = eventUtc.addSecs(60);
                if (!findNextPhase(cursor, targetAngle, &eventUtc, &localErr)) {
                    if (error) {
                        *error = localErr;
                    }
                    return false;
                }
            }
            return true;
        };

        auto eclipseLoop = [&](bool solar) -> bool {
            QDateTime cursor = startUtc;
            double tret[10] = {0};
            while (true) {
                if (cancelled_.load()) {
                    return false;
                }
                const double jdStart = toJulianDay(cursor);
                const int ret = solar
                    ? swe_.solEclipseWhenGlob(jdStart, 0, 0, tret, 0, error)
                    : swe_.lunEclipseWhen(jdStart, 0, 0, tret, 0, error);
                if (ret < 0) {
                    return false;
                }
                QDateTime eventUtc;
                if (!jdToUtc(tret[0], &eventUtc, error)) {
                    return false;
                }
                if (eventUtc > endUtc) {
                    break;
                }
                if (eventUtc >= startUtc) {
                    double sunLon = 0.0;
                    double moonLon = 0.0;
                    if (!sunMoonLonAtUtc(eventUtc, &sunLon, &moonLon, error)) {
                        return false;
                    }
                    if (matchesDegreeRange(moonLon)
                        && passesSiderealWholeSignEclipseRule(solar, eventUtc, sunLon, moonLon, error)) {
                        const QString type = eclipseTypeForFlags(ret, solar);
                        addResult(eventUtc, solar ? "Solar Eclipse" : "Lunar Eclipse", type, ret, sunLon, moonLon);
                        updateProgress();
                    }
                }
                cursor = eventUtc.addSecs(60);
            }
            return true;
        };

        auto strictVedicEclipseLoop = [&](bool solar) -> bool {
            const double targetAngle = solar ? 0.0 : 180.0;
            const QString eventLabel = solar ? "Solar Eclipse" : "Lunar Eclipse";
            QDateTime cursor = startUtc;
            QDateTime eventUtc;
            QString localErr;
            if (!findNextPhase(cursor, targetAngle, &eventUtc, &localErr)) {
                if (error) {
                    *error = localErr;
                }
                return false;
            }
            while (eventUtc <= endUtc) {
                if (cancelled_.load()) {
                    return false;
                }
                double sunLon = 0.0;
                double moonLon = 0.0;
                if (!sunMoonLonAtUtc(eventUtc, &sunLon, &moonLon, error)) {
                    return false;
                }
                if (matchesDegreeRange(moonLon)
                    && passesSiderealWholeSignEclipseRule(solar, eventUtc, sunLon, moonLon, error)) {
                    addResult(eventUtc, eventLabel, "Vedic", 0, sunLon, moonLon);
                    updateProgress();
                }
                cursor = eventUtc.addSecs(60);
                if (!findNextPhase(cursor, targetAngle, &eventUtc, &localErr)) {
                    if (error) {
                        *error = localErr;
                    }
                    return false;
                }
            }
            return true;
        };

        if (params_.includeNewMoon) {
            if (!nextPhaseLoop(newAngle, "New Moon")) {
                return false;
            }
        }
        if (params_.includeFullMoon) {
            if (!nextPhaseLoop(fullAngle, "Full Moon")) {
                return false;
            }
        }
        if (params_.includeSolarEclipse) {
            if (useStrictVedic) {
                if (!strictVedicEclipseLoop(true)) {
                    return false;
                }
            } else if (!eclipseLoop(true)) {
                return false;
            }
        }
        if (params_.includeLunarEclipse) {
            if (useStrictVedic) {
                if (!strictVedicEclipseLoop(false)) {
                    return false;
                }
            } else if (!eclipseLoop(false)) {
                return false;
            }
        }
        return true;
    }

    double toJulianDay(const QDateTime& utc) {
        const QDate date = utc.date();
        const QTime time = utc.time();
        const double hour = time.hour() + time.minute() / 60.0 + time.second() / 3600.0 + time.msec() / 3600000.0;
        return swe_.julianDay(date.year(), date.month(), date.day(), hour, SE_GREG_CAL);
    }

    LunationParams params_;
    SwissEph swe_;
    int calcFlags_ = 0;
    std::atomic<bool> cancelled_{false};
    QVector<MainWindow::LunationResult> results_;
};

class TransitAspectPeakWorker : public QObject {
    Q_OBJECT

public:
    struct Config {
        QDateTime startUtc;
        QDateTime endUtc;
        QTimeZone timezone;
        QString timezoneLabel;
        ZodiacSystem zodiacSystem = ZodiacSystem::Tropical;
        SiderealAyanamsa siderealAyanamsa = SiderealAyanamsa::Lahiri;
        LunarNodePolicy lunarNodePolicy;
        QStringList transitBodies;
        QMap<QString, double> natalTargets;
        QStringList aspectLabels;
        double orb = 1.0;
        int sampleMinutes = 360;
        int minimumHits = 2;
        bool groupPeriods = false;
        bool includeTransitTransit = false;
        bool weightingEnabled = false;
        QMap<QString, double> aspectWeights;
        QString ephePath;
        QStringList dllSearchPaths;
    };

    explicit TransitAspectPeakWorker(const Config& config);

    const QVector<MainWindow::TransitAspectPeakResult>& results() const;
    bool wasCancelled() const;

public slots:
    void run();
    void cancel();

signals:
    void progress(int done, int total);
    void error(const QString& message);
    void finished();

private:
    bool planetLongitude(const QDateTime& utc, const QString& name,
                         double* outLongitude, QString* error);

    Config config_;
    SwissEph swe_;
    int calcFlags_ = 0;
    std::atomic<bool> cancelled_{false};
    QVector<MainWindow::TransitAspectPeakResult> results_;
};
class TransitScanWorker : public QObject {
    Q_OBJECT

public:
    struct Config {
        QDate startDate;
        QDate endDate;
        QTime scanTime;
        QString tzLabel;
        NatalInput natalInput;
        NatalChart natalChart;
        AspectOrbs orbs;
        QString ephePath;
        QStringList dllSearchPaths;
        MainWindow::TransitScanMode mode = MainWindow::TransitScanMode::TransitNatal;
        MainWindow::ConjunctionPolicy conjunctionPolicy = MainWindow::ConjunctionPolicy::Neutral;
        bool includeNodes = false;
        bool includeAngles = true;
        bool includeAsteroidAspects = false;
        double weightTransitNatal = 0.25;
        double weightTransitTransit = 0.5;
        double weightTransitSolar = 0.25;
        double weightTransitProgressed = 0.0;
        bool useSolarBias = false;
        double solarBiasWeight = 0.25;
    };

    explicit TransitScanWorker(const Config& config)
        : config_(config),
          swe_(),
          engine_(&swe_, config.ephePath),
          progressionEngine_(&swe_, config.ephePath) {}

    const QVector<MainWindow::DayScanResult>& results() const { return results_; }
    QStringList warnings() const { return warnings_; }

public slots:
    void run() {
        QString err;
        if (!swe_.load(config_.dllSearchPaths, &err)) {
            emit error(err);
            emit finished();
            return;
        }
        swe_.setEphePath(config_.ephePath);
        calcFlags_ = 0;
        if (config_.natalInput.zodiacSystem == ZodiacSystem::Sidereal) {
            swe_.setSidMode(siderealAyanamsaSwissMode(config_.natalInput.siderealAyanamsa));
            calcFlags_ = SEFLG_SIDEREAL;
        }

        QTimeZone tz;
        QString normLabel;
        QString tzErr;
        if (!parseTimezoneInput(config_.tzLabel, &tz, &normLabel, &tzErr)) {
            emit error(tzErr);
            emit finished();
            return;
        }

        const int totalDays = config_.startDate.daysTo(config_.endDate) + 1;
        if (totalDays <= 0) {
            emit error("Invalid date range.");
            emit finished();
            return;
        }
        results_.clear();
        results_.reserve(totalDays);

        auto classifyConjunctionTransitNatal = [&](const QString& transitName, bool* supportive) -> bool {
            if (config_.conjunctionPolicy == MainWindow::ConjunctionPolicy::Neutral) {
                return false;
            }
            if (transitcalc::isBenefic(transitName)) {
                if (supportive) *supportive = true;
                return true;
            }
            if (transitcalc::isMalefic(transitName)) {
                if (supportive) *supportive = false;
                return true;
            }
            return false;
        };

        auto classifyConjunctionTransitTransit = [&](const QString& aName, const QString& bName, bool* supportive) -> bool {
            if (config_.conjunctionPolicy == MainWindow::ConjunctionPolicy::Neutral) {
                return false;
            }
            const bool malefic = transitcalc::isMalefic(aName) || transitcalc::isMalefic(bName);
            const bool benefic = transitcalc::isBenefic(aName) || transitcalc::isBenefic(bName);
            if (malefic) {
                if (supportive) *supportive = false;
                return true;
            }
            if (benefic) {
                if (supportive) *supportive = true;
                return true;
            }
            return false;
        };

        struct AspectHit {
            double weight = 0.0;
            QString label;
        };

        auto baseWeightFor = [](const QString& label) -> double {
            if (label == "Trine") return 1.0;
            if (label == "Sextile") return 0.7;
            if (label == "Square") return 1.0;
            if (label == "Opposition") return 1.0;
            if (label == "Conjunction") return 1.0;
            return 0.0;
        };

        double natalSunLon = 0.0;
        if (!transitcalc::findBodyLongitude(config_.natalChart, "Sun", &natalSunLon)) {
            emit error("Unable to locate natal Sun longitude.");
            emit finished();
            return;
        }

        QMap<int, NatalChart> solarChartCache;
        QMap<int, double> solarBiasCache;

        auto solarReturnTimeLocal = [&](int year, QDateTime* outLocal, QString* outErr) -> bool {
            if (!outLocal) {
                return false;
            }
            const int month = config_.natalInput.date.month();
            const int day = config_.natalInput.date.day();
            QDate baseDate(year, month, day);
            if (!baseDate.isValid()) {
                baseDate = QDate(year, month, 1).addMonths(1).addDays(-1);
            }
            QDateTime baseLocal(baseDate, config_.natalInput.time, tz);
            if (!baseLocal.isValid()) {
                baseLocal = QDateTime(baseDate, QTime(12, 0, 0), tz);
                if (!baseLocal.isValid()) {
                    if (outErr) *outErr = "Invalid solar return base date/time.";
                    return false;
                }
            }
            const QDateTime baseUtc = baseLocal.toUTC();
            const double target = normalizeDegrees(natalSunLon);

            auto sunLongitudeAtUtc = [&](const QDateTime& utc, double* outLon) -> bool {
                double hourDec = utc.time().hour() + utc.time().minute() / 60.0 + utc.time().second() / 3600.0
                    + utc.time().msec() / 3600000.0;
                const double jd = swe_.julianDay(utc.date().year(), utc.date().month(), utc.date().day(), hourDec, SE_GREG_CAL);
                QString calcErr;
                double lon = 0.0;
                if (!swe_.calcUt(jd, SE_SUN, calcFlags_, &lon, &calcErr)) {
                    if (outErr) {
                        *outErr = QString("Failed to compute Sun longitude: %1").arg(calcErr);
                    }
                    return false;
                }
                if (outLon) {
                    *outLon = normalizeDegrees(lon);
                }
                return true;
            };

            double baseLon = 0.0;
            if (!sunLongitudeAtUtc(baseUtc, &baseLon)) {
                if (outErr && outErr->isEmpty()) {
                    *outErr = "Failed to compute Sun longitude.";
                }
                return false;
            }
            double baseDiff = transitcalc::angularDiffSigned(baseLon, target);
            if (std::fabs(baseDiff) < 1e-6) {
                *outLocal = baseUtc.toTimeZone(tz);
                return true;
            }

            const int stepHours = 6;
            const int rangeDays = 7;
            const int maxSteps = (rangeDays * 24) / stepHours;

            auto findBracket = [&](int direction, QDateTime* lo, QDateTime* hi, double* diffLo, double* diffHi) -> bool {
                QDateTime prevTime = baseUtc;
                double prevDiff = baseDiff;
                for (int i = 1; i <= maxSteps; ++i) {
                    const QDateTime nextTime = baseUtc.addSecs(direction * i * stepHours * 3600);
                    double lon = 0.0;
                    if (!sunLongitudeAtUtc(nextTime, &lon)) {
                        return false;
                    }
                    const double diff = transitcalc::angularDiffSigned(lon, target);
                    if ((prevDiff <= 0.0 && diff >= 0.0) || (prevDiff >= 0.0 && diff <= 0.0)) {
                        if (direction > 0) {
                            *lo = prevTime;
                            *hi = nextTime;
                            *diffLo = prevDiff;
                            *diffHi = diff;
                        } else {
                            *lo = nextTime;
                            *hi = prevTime;
                            *diffLo = diff;
                            *diffHi = prevDiff;
                        }
                        return true;
                    }
                    prevTime = nextTime;
                    prevDiff = diff;
                }
                return false;
            };

            QDateTime lo;
            QDateTime hi;
            double diffLo = 0.0;
            double diffHi = 0.0;
            bool bracketFound = findBracket(1, &lo, &hi, &diffLo, &diffHi);
            if (!bracketFound) {
                bracketFound = findBracket(-1, &lo, &hi, &diffLo, &diffHi);
            }
            if (!bracketFound) {
                if (outErr) {
                    *outErr = "Unable to find solar return time within +/- 7 days of the natal date.";
                }
                return false;
            }

            for (int i = 0; i < 32; ++i) {
                const QDateTime mid = transitcalc::midTimeUtc(lo, hi);
                double lon = 0.0;
                if (!sunLongitudeAtUtc(mid, &lon)) {
                    return false;
                }
                const double diff = transitcalc::angularDiffSigned(lon, target);
                if ((diffLo <= 0.0 && diff >= 0.0) || (diffLo >= 0.0 && diff <= 0.0)) {
                    hi = mid;
                    diffHi = diff;
                } else {
                    lo = mid;
                    diffLo = diff;
                }
                if (lo.secsTo(hi) <= 1) {
                    break;
                }
            }
            *outLocal = hi.toTimeZone(tz);
            return true;
        };

        auto getSolarChart = [&](int year, NatalChart* outChart, QString* outErr) -> bool {
            if (!outChart) {
                return false;
            }
            if (solarChartCache.contains(year)) {
                *outChart = solarChartCache.value(year);
                return true;
            }
            QDateTime localTime;
            QString err;
            if (!solarReturnTimeLocal(year, &localTime, &err)) {
                if (outErr) {
                    *outErr = err;
                }
                return false;
            }
            NatalInput input = config_.natalInput;
            input.name = QString("Solar Return %1").arg(year);
            input.date = localTime.date();
            input.time = localTime.time();
            input.timezone = normLabel;
            input.aspectOrbs = config_.orbs;
            NatalChart chart;
            QString calcErr;
            if (!engine_.compute(input, &chart, &calcErr)) {
                if (outErr) {
                    *outErr = calcErr;
                }
                return false;
            }
            for (const auto& warning : chart.warnings) {
                if (!warnings_.contains(warning)) {
                    warnings_.push_back(warning);
                }
            }
            solarChartCache.insert(year, chart);
            *outChart = chart;
            return true;
        };

        auto solarPlacementBias = [&](const NatalChart& solarChart) -> double {
            double bias = 0.0;
            auto houseScore = [](int house) -> double {
                switch (house) {
                    case 1:
                    case 5:
                    case 9:
                    case 10:
                    case 11:
                        return 1.0;
                    case 6:
                    case 8:
                    case 12:
                        return -1.0;
                    default:
                        return 0.0;
                }
            };
            for (const auto& body : solarChart.bodies) {
                if (!config_.includeNodes && transitcalc::isNodeName(body.name)) {
                    continue;
                }
                if (body.isLunarNode
                    && body.lunarNodeType != effectivePrimaryNodeType(solarChart.lunarNodePolicy)) {
                    continue;
                }
                if (!config_.includeAsteroidAspects && isAsteroidBody(body.name)) {
                    continue;
                }
                const int house = transitcalc::calcHouseForLongitude(body.longitude, config_.natalChart.cusps, config_.natalChart.angles.asc,
                    config_.natalInput.houseSystem);
                if (house <= 0) {
                    continue;
                }
                const double weight = houseScore(house) * transitcalc::bodyWeightFor(body.name);
                bias += weight;
            }
            return bias * 0.3;
        };

        auto scoreTransitToChart = [&](const NatalChart& transitChart, const NatalChart& targetChart, const QString& targetLabel) {
            MainWindow::DayScanResult bucket;
            QVector<AspectHit> hits;

            QVector<QString> transitNames;
            transitNames.reserve(transitChart.bodies.size());
            for (const auto& body : transitChart.bodies) {
                if (!config_.includeNodes && transitcalc::isNodeName(body.name)) {
                    continue;
                }
                if (body.isLunarNode
                    && body.lunarNodeType != effectivePrimaryNodeType(transitChart.lunarNodePolicy)) {
                    continue;
                }
                if (!config_.includeAsteroidAspects && isAsteroidBody(body.name)) {
                    continue;
                }
                transitNames.push_back(body.name);
            }

            QVector<QString> targetNames;
            targetNames.reserve(targetChart.bodies.size() + 4);
            for (const auto& body : targetChart.bodies) {
                if (!config_.includeNodes && transitcalc::isNodeName(body.name)) {
                    continue;
                }
                if (body.isLunarNode
                    && body.lunarNodeType != effectivePrimaryNodeType(targetChart.lunarNodePolicy)) {
                    continue;
                }
                if (!config_.includeAsteroidAspects && isAsteroidBody(body.name)) {
                    continue;
                }
                targetNames.push_back(body.name);
            }
            if (config_.includeAngles) {
                targetNames.push_back("Ascendant");
                targetNames.push_back("Midheaven");
                targetNames.push_back("Descendant");
                targetNames.push_back("IC");
            }

            QMap<QString, double> transitMap;
            for (const auto& body : transitChart.bodies) {
                transitMap.insert(body.name, body.longitude);
            }
            QMap<QString, double> targetMap;
            for (const auto& body : targetChart.bodies) {
                targetMap.insert(body.name, body.longitude);
            }
            targetMap.insert("Ascendant", targetChart.angles.asc);
            targetMap.insert("Midheaven", targetChart.angles.mc);
            targetMap.insert("Descendant", targetChart.angles.desc);
            targetMap.insert("IC", targetChart.angles.ic);

            for (const auto& tName : transitNames) {
                const double tLon = transitMap.value(tName);
                for (const auto& nName : targetNames) {
                    if (!targetMap.contains(nName)) {
                        continue;
                    }
                    const double nLon = targetMap.value(nName);
                    const double diff = transitcalc::angularDiffAbs(tLon, nLon);
                    QString label;
                    double orb = 0.0;
                    double maxOrb = 0.0;
                    if (!transitcalc::aspectForDiff(diff, config_.orbs, &label, &orb, &maxOrb)) {
                        continue;
                    }
                    bool supportive = false;
                    if (label == "Conjunction") {
                        if (!classifyConjunctionTransitNatal(tName, &supportive)) {
                            continue;
                        }
                    } else if (label == "Trine" || label == "Sextile") {
                        supportive = true;
                    } else if (label == "Square" || label == "Opposition") {
                        supportive = false;
                    } else {
                        continue;
                    }
                    double base = baseWeightFor(label);
                    if (base <= 0.0) {
                        continue;
                    }
                    double orbFactor = (maxOrb > 0.0) ? (1.0 - orb / maxOrb) : 1.0;
                    orbFactor = std::clamp(orbFactor, 0.0, 1.0);
                    const double weight = base * orbFactor * transitcalc::bodyWeightFor(tName);
                    if (supportive) {
                        bucket.support += weight;
                        bucket.supportCount++;
                    } else {
                        bucket.challenge += weight;
                        bucket.challengeCount++;
                    }
                    bucket.aspectCount++;
                    const QString aspectText = QString("Transit %1 %2 %3 %4 (orb %5)")
                        .arg(lunarNodeDisplayName(tName, transitChart.lunarNodePolicy))
                        .arg(label.toLower())
                        .arg(targetLabel)
                        .arg(lunarNodeDisplayName(nName, targetChart.lunarNodePolicy))
                        .arg(QString::number(orb, 'f', 2));
                    hits.push_back({weight, aspectText});
                }
            }

            std::sort(hits.begin(), hits.end(), [](const AspectHit& a, const AspectHit& b) {
                return a.weight > b.weight;
            });
            const int limit = std::min(5, static_cast<int>(hits.size()));
            for (int i = 0; i < limit; ++i) {
                bucket.topAspects.push_back(hits[i].label);
            }
            return bucket;
        };

        auto scoreTransitTransit = [&](const NatalChart& transitChart) {
            MainWindow::DayScanResult bucket;
            QVector<AspectHit> hits;
            QVector<QString> names;
            QMap<QString, double> map;
            for (const auto& body : transitChart.bodies) {
                if (!config_.includeNodes && transitcalc::isNodeName(body.name)) {
                    continue;
                }
                if (body.isLunarNode
                    && body.lunarNodeType != effectivePrimaryNodeType(transitChart.lunarNodePolicy)) {
                    continue;
                }
                if (!config_.includeAsteroidAspects && isAsteroidBody(body.name)) {
                    continue;
                }
                names.push_back(body.name);
                map.insert(body.name, body.longitude);
            }
            if (config_.includeAngles) {
                names.push_back("Ascendant");
                names.push_back("Midheaven");
                names.push_back("Descendant");
                names.push_back("IC");
                map.insert("Ascendant", transitChart.angles.asc);
                map.insert("Midheaven", transitChart.angles.mc);
                map.insert("Descendant", transitChart.angles.desc);
                map.insert("IC", transitChart.angles.ic);
            }
            for (int i = 0; i < names.size(); ++i) {
                for (int j = i + 1; j < names.size(); ++j) {
                    const QString& aName = names[i];
                    const QString& bName = names[j];
                    const double diff = transitcalc::angularDiffAbs(map.value(aName), map.value(bName));
                    QString label;
                    double orb = 0.0;
                    double maxOrb = 0.0;
                    if (!transitcalc::aspectForDiff(diff, config_.orbs, &label, &orb, &maxOrb)) {
                        continue;
                    }
                    bool supportive = false;
                    if (label == "Conjunction") {
                        if (!classifyConjunctionTransitTransit(aName, bName, &supportive)) {
                            continue;
                        }
                    } else if (label == "Trine" || label == "Sextile") {
                        supportive = true;
                    } else if (label == "Square" || label == "Opposition") {
                        supportive = false;
                    } else {
                        continue;
                    }
                    double base = baseWeightFor(label);
                    if (base <= 0.0) {
                        continue;
                    }
                    double orbFactor = (maxOrb > 0.0) ? (1.0 - orb / maxOrb) : 1.0;
                    orbFactor = std::clamp(orbFactor, 0.0, 1.0);
                    const double weight = base * orbFactor * ((transitcalc::bodyWeightFor(aName) + transitcalc::bodyWeightFor(bName)) * 0.5);
                    if (supportive) {
                        bucket.support += weight;
                        bucket.supportCount++;
                    } else {
                        bucket.challenge += weight;
                        bucket.challengeCount++;
                    }
                    bucket.aspectCount++;
                    const QString aspectText = QString("Transit %1 %2 Transit %3 (orb %4)")
                        .arg(lunarNodeDisplayName(aName, transitChart.lunarNodePolicy))
                        .arg(label.toLower())
                        .arg(lunarNodeDisplayName(bName, transitChart.lunarNodePolicy))
                        .arg(QString::number(orb, 'f', 2));
                    hits.push_back({weight, aspectText});
                }
            }

            std::sort(hits.begin(), hits.end(), [](const AspectHit& a, const AspectHit& b) {
                return a.weight > b.weight;
            });
            const int limit = std::min(5, static_cast<int>(hits.size()));
            for (int i = 0; i < limit; ++i) {
                bucket.topAspects.push_back(hits[i].label);
            }
            return bucket;
        };

        for (QDate date = config_.startDate; date <= config_.endDate; date = date.addDays(1)) {
            if (cancelled_.load()) {
                break;
            }
            QDateTime local(date, config_.scanTime, tz);
            if (!local.isValid()) {
                continue;
            }
            NatalInput transitInput = config_.natalInput;
            transitInput.name = "Transit";
            transitInput.date = date;
            transitInput.time = config_.scanTime;
            transitInput.timezone = normLabel;
            transitInput.aspectOrbs = config_.orbs;
            NatalChart transitChart;
            QString calcErr;
            if (!engine_.compute(transitInput, &transitChart, &calcErr)) {
                emit error(calcErr);
                break;
            }
            for (const auto& warning : transitChart.warnings) {
                if (!warnings_.contains(warning)) {
                    warnings_.push_back(warning);
                }
            }

            MainWindow::DayScanResult tn;
            MainWindow::DayScanResult tt;
            MainWindow::DayScanResult ts;
            MainWindow::DayScanResult tp;
            const int year = date.year();
            NatalChart solarChart;
            bool hasSolarChart = false;
            if (config_.mode == MainWindow::TransitScanMode::TransitNatal
                || config_.mode == MainWindow::TransitScanMode::Combined) {
                tn = scoreTransitToChart(transitChart, config_.natalChart, "Natal");
            }
            if (config_.mode == MainWindow::TransitScanMode::TransitTransit
                || config_.mode == MainWindow::TransitScanMode::Combined) {
                tt = scoreTransitTransit(transitChart);
            }
            if (config_.mode == MainWindow::TransitScanMode::TransitSolar
                || config_.mode == MainWindow::TransitScanMode::Combined) {
                QString solarErr;
                if (!getSolarChart(year, &solarChart, &solarErr)) {
                    emit error(solarErr);
                    break;
                }
                hasSolarChart = true;
                ts = scoreTransitToChart(transitChart, solarChart, "Solar");
            }
            if (config_.mode == MainWindow::TransitScanMode::TransitProgressed
                || config_.mode == MainWindow::TransitScanMode::Combined) {
                NatalChart progressedChart;
                QString progErr;
                const QDateTime targetLocal(date, config_.scanTime, tz);
                if (!progressionEngine_.compute(config_.natalInput, targetLocal, normLabel, &progressedChart, &progErr)) {
                    emit error(progErr);
                    break;
                }
                for (const auto& warning : progressedChart.warnings) {
                    if (!warnings_.contains(warning)) {
                        warnings_.push_back(warning);
                    }
                }
                tp = scoreTransitToChart(transitChart, progressedChart, "Progressed");
            }

            MainWindow::DayScanResult result;
            result.date = date;
            if (config_.mode == MainWindow::TransitScanMode::TransitNatal) {
                result.support = tn.support;
                result.challenge = tn.challenge;
                result.supportCount = tn.supportCount;
                result.challengeCount = tn.challengeCount;
                result.aspectCount = tn.aspectCount;
                result.topAspects = tn.topAspects;
            } else if (config_.mode == MainWindow::TransitScanMode::TransitTransit) {
                result.support = tt.support;
                result.challenge = tt.challenge;
                result.supportCount = tt.supportCount;
                result.challengeCount = tt.challengeCount;
                result.aspectCount = tt.aspectCount;
                result.topAspects = tt.topAspects;
            } else if (config_.mode == MainWindow::TransitScanMode::TransitSolar) {
                result.support = ts.support;
                result.challenge = ts.challenge;
                result.supportCount = ts.supportCount;
                result.challengeCount = ts.challengeCount;
                result.aspectCount = ts.aspectCount;
                result.topAspects = ts.topAspects;
            } else if (config_.mode == MainWindow::TransitScanMode::TransitProgressed) {
                result.support = tp.support;
                result.challenge = tp.challenge;
                result.supportCount = tp.supportCount;
                result.challengeCount = tp.challengeCount;
                result.aspectCount = tp.aspectCount;
                result.topAspects = tp.topAspects;
            } else {
                double wTN = std::max(0.0, config_.weightTransitNatal);
                double wTT = std::max(0.0, config_.weightTransitTransit);
                double wTS = std::max(0.0, config_.weightTransitSolar);
                double wTP = std::max(0.0, config_.weightTransitProgressed);
                double sum = wTN + wTT + wTS + wTP;
                if (sum <= 0.0) {
                    wTN = 0.25;
                    wTT = 0.5;
                    wTS = 0.25;
                    wTP = 0.0;
                    sum = wTN + wTT + wTS + wTP;
                }
                wTN /= sum;
                wTT /= sum;
                wTS /= sum;
                wTP /= sum;
                result.support = tn.support * wTN + tt.support * wTT + ts.support * wTS + tp.support * wTP;
                result.challenge = tn.challenge * wTN + tt.challenge * wTT + ts.challenge * wTS + tp.challenge * wTP;
                result.supportCount = tn.supportCount + tt.supportCount + ts.supportCount + tp.supportCount;
                result.challengeCount = tn.challengeCount + tt.challengeCount + ts.challengeCount + tp.challengeCount;
                result.aspectCount = tn.aspectCount + tt.aspectCount + ts.aspectCount + tp.aspectCount;
                QVector<AspectHit> combinedHits;
                for (const auto& hit : tn.topAspects) {
                    combinedHits.push_back({wTN, hit});
                }
                for (const auto& hit : tt.topAspects) {
                    combinedHits.push_back({wTT, hit});
                }
                for (const auto& hit : ts.topAspects) {
                    combinedHits.push_back({wTS, hit});
                }
                for (const auto& hit : tp.topAspects) {
                    combinedHits.push_back({wTP, hit});
                }
                std::sort(combinedHits.begin(), combinedHits.end(), [](const AspectHit& a, const AspectHit& b) {
                    return a.weight > b.weight;
                });
                const int limit = std::min(5, static_cast<int>(combinedHits.size()));
                for (int i = 0; i < limit; ++i) {
                    result.topAspects.push_back(combinedHits[i].label);
                }
            }
            result.net = result.support - result.challenge;
            if (config_.useSolarBias) {
                double bias = 0.0;
                if (solarBiasCache.contains(year)) {
                    bias = solarBiasCache.value(year);
                } else {
                    if (!hasSolarChart) {
                        QString solarErr;
                        if (!getSolarChart(year, &solarChart, &solarErr)) {
                            emit error(solarErr);
                            break;
                        }
                    }
                    bias = solarPlacementBias(solarChart);
                    solarBiasCache.insert(year, bias);
                }
                result.solarBias = bias * std::clamp(config_.solarBiasWeight, 0.0, 1.0);
                result.net += result.solarBias;
            }
            results_.push_back(result);

            const int done = config_.startDate.daysTo(date) + 1;
            emit progress(done, totalDays);
        }

        emit finished();
    }

    void cancel() { cancelled_.store(true); }

signals:
    void progress(int done, int total);
    void error(const QString& message);
    void finished();

private:
    Config config_;
    std::atomic<bool> cancelled_{false};
    QVector<MainWindow::DayScanResult> results_;
    QStringList warnings_;
    SwissEph swe_;
    int calcFlags_ = 0;
    TropicalNatalEngine engine_;
    SecondaryProgressionEngine progressionEngine_;
};


}  // namespace dracoved
