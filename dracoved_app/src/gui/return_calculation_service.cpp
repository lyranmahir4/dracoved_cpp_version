#include "return_calculation_service.h"

#include "../core/formatting.h"
#include "../core/timezone_utils.h"
#include "transit_calc_service.h"

#include <QDate>
#include <QTime>
#include <QTimeZone>

#include <cmath>

namespace dracoved::returncalc {

namespace {

int calculationFlags(const NatalInput& input) {
    return input.zodiacSystem == ZodiacSystem::Sidereal ? SEFLG_SIDEREAL : 0;
}

void applyZodiacMode(SwissEph& swe, const NatalInput& input) {
    if (input.zodiacSystem == ZodiacSystem::Sidereal) {
        swe.setSidMode(siderealAyanamsaSwissMode(input.siderealAyanamsa));
    }
}

bool wasCancelled(const CancelCheck& cancelled) {
    return cancelled && cancelled();
}

}  // namespace

bool solarReturnTimeUtc(SwissEph& swe,
                        const NatalInput& natalInput,
                        int year,
                        const QString& timezone,
                        double targetLongitude,
                        QDateTime* outUtc,
                        QDateTime* outLocal,
                        QString* error,
                        const CancelCheck& cancelled) {
    QTimeZone tz;
    QString normalizedTimezone;
    QString timezoneError;
    if (!parseTimezoneInput(timezone, &tz, &normalizedTimezone, &timezoneError)) {
        if (error) {
            *error = timezoneError;
        }
        return false;
    }

    const int month = natalInput.date.month();
    const int day = natalInput.date.day();
    QDate baseDate(year, month, day);
    if (!baseDate.isValid()) {
        baseDate = QDate(year, month, 1).addMonths(1).addDays(-1);
    }
    QDateTime baseLocal(baseDate, natalInput.time, tz);
    if (!baseLocal.isValid()) {
        baseLocal = QDateTime(baseDate, QTime(12, 0), tz);
    }
    if (!baseLocal.isValid()) {
        if (error) {
            *error = "Invalid solar return base date/time.";
        }
        return false;
    }

    const QDateTime baseUtc = baseLocal.toUTC();
    const double target = normalizeDegrees(targetLongitude);
    applyZodiacMode(swe, natalInput);
    const int flags = calculationFlags(natalInput);

    auto sunLongitude = [&](const QDateTime& utc, double* outLongitude) {
        if (wasCancelled(cancelled)) {
            return false;
        }
        const double hour = utc.time().hour() + utc.time().minute() / 60.0
            + utc.time().second() / 3600.0 + utc.time().msec() / 3600000.0;
        const double jd = swe.julianDay(utc.date().year(), utc.date().month(), utc.date().day(), hour, SE_GREG_CAL);
        QString calculationError;
        double longitude = 0.0;
        if (!swe.calcUt(jd, SE_SUN, flags, &longitude, &calculationError)) {
            if (error) {
                *error = QString("Failed to compute Sun longitude: %1").arg(calculationError);
            }
            return false;
        }
        if (outLongitude) {
            *outLongitude = normalizeDegrees(longitude);
        }
        return true;
    };

    double baseLongitude = 0.0;
    if (!sunLongitude(baseUtc, &baseLongitude)) {
        return false;
    }
    double baseDifference = transitcalc::angularDiffSigned(baseLongitude, target);
    if (std::fabs(baseDifference) < 1e-6) {
        if (outUtc) *outUtc = baseUtc;
        if (outLocal) *outLocal = baseUtc.toTimeZone(tz);
        return true;
    }

    constexpr int stepHours = 6;
    constexpr int maxSteps = 28;
    auto findBracket = [&](int direction, QDateTime* lo, QDateTime* hi, double* diffLo) {
        QDateTime previousTime = baseUtc;
        double previousDifference = baseDifference;
        for (int i = 1; i <= maxSteps; ++i) {
            if (wasCancelled(cancelled)) {
                return false;
            }
            const QDateTime nextTime = baseUtc.addSecs(static_cast<qint64>(direction) * i * stepHours * 3600);
            double longitude = 0.0;
            if (!sunLongitude(nextTime, &longitude)) {
                return false;
            }
            const double difference = transitcalc::angularDiffSigned(longitude, target);
            if ((previousDifference <= 0.0 && difference >= 0.0)
                || (previousDifference >= 0.0 && difference <= 0.0)) {
                if (direction > 0) {
                    *lo = previousTime;
                    *hi = nextTime;
                    *diffLo = previousDifference;
                } else {
                    *lo = nextTime;
                    *hi = previousTime;
                    *diffLo = difference;
                }
                return true;
            }
            previousTime = nextTime;
            previousDifference = difference;
        }
        return false;
    };

    QDateTime lo;
    QDateTime hi;
    double differenceLo = 0.0;
    bool bracketed = findBracket(1, &lo, &hi, &differenceLo);
    if (!bracketed && !wasCancelled(cancelled)) {
        bracketed = findBracket(-1, &lo, &hi, &differenceLo);
    }
    if (!bracketed) {
        if (error && !wasCancelled(cancelled)) {
            *error = "Unable to find solar return time within +/- 7 days of the natal date.";
        }
        return false;
    }

    for (int i = 0; i < 32; ++i) {
        if (wasCancelled(cancelled)) {
            return false;
        }
        const QDateTime mid = transitcalc::midTimeUtc(lo, hi);
        double longitude = 0.0;
        if (!sunLongitude(mid, &longitude)) {
            return false;
        }
        const double difference = transitcalc::angularDiffSigned(longitude, target);
        if ((differenceLo <= 0.0 && difference >= 0.0)
            || (differenceLo >= 0.0 && difference <= 0.0)) {
            hi = mid;
        } else {
            lo = mid;
            differenceLo = difference;
        }
        if (lo.secsTo(hi) <= 1) {
            break;
        }
    }

    if (outUtc) *outUtc = hi;
    if (outLocal) *outLocal = hi.toTimeZone(tz);
    return true;
}

bool lunarReturnTimeUtc(SwissEph& swe,
                        const NatalInput& natalInput,
                        const QDateTime& anchorUtc,
                        int direction,
                        double targetLongitude,
                        const QString& timezone,
                        QDateTime* outUtc,
                        QDateTime* outLocal,
                        QString* error,
                        const CancelCheck& cancelled) {
    QTimeZone tz;
    QString normalizedTimezone;
    QString timezoneError;
    if (!parseTimezoneInput(timezone, &tz, &normalizedTimezone, &timezoneError)) {
        if (error) *error = timezoneError;
        return false;
    }
    if (!anchorUtc.isValid()) {
        if (error) *error = "Invalid lunar return anchor date/time.";
        return false;
    }

    const double target = normalizeDegrees(targetLongitude);
    applyZodiacMode(swe, natalInput);
    const int flags = calculationFlags(natalInput);

    auto moonDifference = [&](const QDateTime& utc, double* outDifference) {
        if (wasCancelled(cancelled)) {
            return false;
        }
        const double hour = utc.time().hour() + utc.time().minute() / 60.0
            + utc.time().second() / 3600.0 + utc.time().msec() / 3600000.0;
        const double jd = swe.julianDay(utc.date().year(), utc.date().month(), utc.date().day(), hour, SE_GREG_CAL);
        QString calculationError;
        double longitude = 0.0;
        if (!swe.calcUt(jd, SE_MOON, flags, &longitude, &calculationError)) {
            if (error) *error = QString("Failed to compute Moon longitude: %1").arg(calculationError);
            return false;
        }
        if (outDifference) {
            *outDifference = transitcalc::angularDiffSigned(normalizeDegrees(longitude), target);
        }
        return true;
    };

    constexpr int stepHours = 6;
    constexpr int maxSteps = 128;
    const int stepDirection = direction >= 0 ? 1 : -1;
    QDateTime lo;
    QDateTime hi;
    bool bracketed = false;
    QDateTime previousTime = anchorUtc;
    double previousDifference = 0.0;
    if (!moonDifference(previousTime, &previousDifference)) {
        return false;
    }

    for (int i = 1; i <= maxSteps && !bracketed; ++i) {
        if (wasCancelled(cancelled)) {
            return false;
        }
        const QDateTime nextTime = anchorUtc.addSecs(static_cast<qint64>(stepDirection) * i * stepHours * 3600);
        double difference = 0.0;
        if (!moonDifference(nextTime, &difference)) {
            return false;
        }
        if (stepDirection > 0) {
            if (previousDifference < 0.0 && difference >= 0.0
                && std::fabs(previousDifference) < 90.0 && std::fabs(difference) < 90.0) {
                lo = previousTime;
                hi = nextTime;
                bracketed = true;
            }
        } else if (difference < 0.0 && previousDifference >= 0.0
                   && std::fabs(previousDifference) < 90.0 && std::fabs(difference) < 90.0) {
            lo = nextTime;
            hi = previousTime;
            bracketed = true;
        }
        previousTime = nextTime;
        previousDifference = difference;
    }

    if (!bracketed) {
        if (error && !wasCancelled(cancelled)) {
            *error = "Unable to find a lunar return within ~32 days of the anchor date.";
        }
        return false;
    }

    for (int i = 0; i < 40; ++i) {
        if (wasCancelled(cancelled)) {
            return false;
        }
        const QDateTime mid = transitcalc::midTimeUtc(lo, hi);
        double difference = 0.0;
        if (!moonDifference(mid, &difference)) {
            return false;
        }
        if (difference >= 0.0) {
            hi = mid;
        } else {
            lo = mid;
        }
        if (lo.secsTo(hi) <= 1) {
            break;
        }
    }

    if (outUtc) *outUtc = hi;
    if (outLocal) *outLocal = hi.toTimeZone(tz);
    return true;
}

}  // namespace dracoved::returncalc
