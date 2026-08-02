#pragma once

#include "swiss_eph.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dracoved {

struct PlanetaryHourInterval {
    int sequence = 0;
    int periodHour = 0;
    bool daytime = true;
    QString ruler;
    QDateTime startLocal;
    QDateTime endLocal;
};

struct PlanetaryHoursCalculationOptions {
    double elevationMeters = 0.0;
    double pressureHPa = 0.0;  // 0 lets Swiss Ephemeris estimate pressure.
    double temperatureC = 15.0;
};

struct PlanetaryHoursResult {
    bool valid = false;
    QString error;
    QString timezoneLabel;
    QDateTime momentLocal;
    QDate planetaryDate;
    QString dayRuler;
    QDateTime sunrise;
    QDateTime sunset;
    QDateTime nextSunrise;
    qint64 dayHourMilliseconds = 0;
    qint64 nightHourMilliseconds = 0;
    PlanetaryHoursCalculationOptions options;
    bool beforeSunrise = false;
    QVector<PlanetaryHourInterval> hours;
    int currentIndex = -1;
    QStringList warnings;
};

QString planetaryDayRuler(Qt::DayOfWeek weekday);
QString planetaryRulerMeaning(const QString& ruler);

bool calculatePlanetaryHours(SwissEph& swe,
                             const QDateTime& localMoment,
                             const QString& timezoneLabel,
                             double latitude,
                             double longitude,
                             const PlanetaryHoursCalculationOptions& options,
                             PlanetaryHoursResult* result,
                             QString* error);

void updatePlanetaryHoursMoment(PlanetaryHoursResult* result,
                                const QDateTime& localMoment);

}  // namespace dracoved
