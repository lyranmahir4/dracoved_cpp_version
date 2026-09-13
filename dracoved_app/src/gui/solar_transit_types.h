#pragma once
#include "../core/chart_types.h"
#include "solar_transit_algorithms.h"
#include <QVector>
#include <QStringList>
#include <functional>

namespace dracoved {

struct SolarTransitSource {
    NatalChart returnChart;
    NatalInput returnInput;
    NatalInput natalInput;
    double natalSunLongitude = 0;
    int returnYear = 0;
    bool tajaka = false;
    QString location;
    QString dllPath;
    QString ephePath;
};
struct SolarTransitTarget { QString name; double longitude = 0; };
struct SolarTransitQuery {
    SolarTransitSource source;
    QStringList bodies;
    QVector<SolarTransitTarget> targets;
    QVector<int> aspects;
    double orb = 1;
    bool houses = false;
};
struct SolarTransitEvent {
    QDateTime utc;
    QDateTime untilUtc;
    QString body;
    QString target;
    int aspect = 0;
    QString kind;
    QString note;
    double orb = 0;
    double speed = 0;
    double targetLongitude = 0;
    bool houseCrossing = false;
    bool exact = false;
    int pass = 0;
    int totalPasses = 0;
};
struct SolarTransitWindow {
    QDateTime startUtc;
    QDateTime endUtc;
    QString body;
    QString target;
    int aspect = 0;
    bool clippedStart = false;
    bool clippedEnd = false;
};
struct SolarTransitResult {
    SolarTransitQuery query;
    QDateTime startUtc;
    QDateTime endUtc;
    QVector<SolarTransitEvent> events;
    QVector<SolarTransitWindow> windows;
    QStringList warnings;
    QString error;
    bool cancelled = false;
    int searchedPairs = 0;
    int inactivePairs = 0;
};

SolarTransitResult calculateSolarTransits(const SolarTransitQuery& query,
    const std::function<bool()>& cancelled, const std::function<void(int, const QString&)>& progress);
bool computeSolarTransitMoment(const SolarTransitSource& source, const QDateTime& utc,
    NatalChart* chart, QString* error);
QString solarTransitAspectLabel(int angle);

} // namespace dracoved
