#include "progressed_lunar_return.h"

#include "formatting.h"
#include "timezone_utils.h"

#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

constexpr double kDaysPerYear = 365.2425;
constexpr double kMeanLunarReturnDays = 27.321661;
constexpr double kSearchStepDays = 0.25;
constexpr int kMaximumSearchSteps = 160;

double julianFromUtc(SwissEph& swe, const QDateTime& utc) {
    const QTime time = utc.time();
    const double hour = time.hour()
        + time.minute() / 60.0
        + time.second() / 3600.0
        + time.msec() / 3600000.0;
    return swe.julianDay(utc.date().year(), utc.date().month(), utc.date().day(),
                         hour, SE_GREG_CAL);
}

double signedLongitudeError(double longitude, double target) {
    return std::fmod(longitude - target + 540.0, 360.0) - 180.0;
}

bool moonLongitude(SwissEph& swe, double jd, int flags,
                   double* longitude, QString* error) {
    double value = 0.0;
    QString calculationError;
    if (!swe.calcUt(jd, SE_MOON, flags, &value, &calculationError)) {
        if (error) {
            *error = calculationError.isEmpty()
                ? "Unable to calculate the progressed Moon."
                : calculationError;
        }
        return false;
    }
    if (longitude) *longitude = normalizeDegrees(value);
    return true;
}

bool refineReturn(SwissEph& swe, double firstJd, double secondJd,
                  double firstError, double targetLongitude, int flags,
                  double* resultJd, QString* error) {
    double left = firstJd;
    double right = secondJd;
    double leftError = firstError;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const double middle = (left + right) * 0.5;
        double longitude = 0.0;
        if (!moonLongitude(swe, middle, flags, &longitude, error)) return false;
        const double middleError = signedLongitudeError(longitude, targetLongitude);
        if (std::fabs(middleError) < 1e-10 || std::fabs(right - left) < 1e-9) {
            if (resultJd) *resultJd = middle;
            return true;
        }
        const bool sameSign = (leftError < 0.0 && middleError < 0.0)
            || (leftError > 0.0 && middleError > 0.0);
        if (sameSign) {
            left = middle;
            leftError = middleError;
        } else {
            right = middle;
        }
    }
    if (resultJd) *resultJd = (left + right) * 0.5;
    return true;
}

}  // namespace

bool findProgressedLunarReturn(SwissEph& swe,
                               const QString& ephePath,
                               const NatalInput& natalInput,
                               const QDateTime& anchorLocal,
                               const QString& anchorTimezone,
                               int direction,
                               ProgressedLunarReturnEvent* event,
                               QString* error) {
    if (!event) {
        if (error) *error = "Progressed lunar return output is unavailable.";
        return false;
    }
    *event = {};
    if (!swe.isLoaded()) {
        if (error) *error = "Swiss Ephemeris is not loaded.";
        return false;
    }
    if (direction == 0) {
        if (error) *error = "Choose previous or next progressed lunar return.";
        return false;
    }
    if (!ephePath.isEmpty()) swe.setEphePath(ephePath);

    QTimeZone natalTimezone;
    QString natalTimezoneLabel;
    QString timezoneError;
    if (!parseTimezoneInput(natalInput.timezone, &natalTimezone,
                            &natalTimezoneLabel, &timezoneError)) {
        if (error) *error = timezoneError;
        return false;
    }
    const QDateTime natalLocal(natalInput.date, natalInput.time, natalTimezone);
    if (!natalLocal.isValid()) {
        if (error) *error = "Invalid natal date and time.";
        return false;
    }
    const QDateTime natalUtc = natalLocal.toUTC();

    QTimeZone targetTimezone;
    QString targetTimezoneLabel;
    if (!parseTimezoneInput(anchorTimezone, &targetTimezone,
                            &targetTimezoneLabel, &timezoneError)) {
        if (error) *error = timezoneError;
        return false;
    }
    const QDateTime cleanAnchor(anchorLocal.date(), anchorLocal.time(), targetTimezone);
    if (!cleanAnchor.isValid()) {
        if (error) *error = "Invalid progressed lunar return anchor date and time.";
        return false;
    }
    const QDateTime anchorUtc = cleanAnchor.toUTC();
    if (anchorUtc < natalUtc) {
        if (error) *error = "Select an anchor at or after the natal moment.";
        return false;
    }

    const bool sidereal = natalInput.zodiacSystem == ZodiacSystem::Sidereal;
    if (sidereal) {
        swe.setSidMode(siderealAyanamsaSwissMode(natalInput.siderealAyanamsa));
    }
    const int calculationFlags = sidereal ? SEFLG_SIDEREAL : 0;
    const double natalJd = julianFromUtc(swe, natalUtc);
    double natalMoon = 0.0;
    if (!moonLongitude(swe, natalJd, calculationFlags, &natalMoon, error)) return false;

    const double anchorAgeDays = natalUtc.msecsTo(anchorUtc) / 86400000.0;
    const double anchorProgressedJd = natalJd + anchorAgeDays / kDaysPerYear;
    const double searchDirection = direction > 0 ? 1.0 : -1.0;
    double previousJd = anchorProgressedJd + searchDirection * 1e-8;
    double previousLongitude = 0.0;
    if (!moonLongitude(swe, previousJd, calculationFlags,
                       &previousLongitude, error)) return false;
    double previousError = signedLongitudeError(previousLongitude, natalMoon);
    double returnJd = 0.0;
    bool found = false;

    for (int step = 1; step <= kMaximumSearchSteps; ++step) {
        const double currentJd = previousJd + searchDirection * kSearchStepDays;
        double currentLongitude = 0.0;
        if (!moonLongitude(swe, currentJd, calculationFlags,
                           &currentLongitude, error)) return false;
        const double currentError = signedLongitudeError(currentLongitude, natalMoon);
        const bool crossesTarget = (previousError <= 0.0 && currentError >= 0.0)
            || (previousError >= 0.0 && currentError <= 0.0);
        // The signed angular error also changes sign at the opposition wrap.
        // Requiring both samples near the target rejects that false crossing.
        if (crossesTarget && std::fabs(previousError) < 30.0
            && std::fabs(currentError) < 30.0) {
            if (!refineReturn(swe, previousJd, currentJd, previousError,
                              natalMoon, calculationFlags, &returnJd, error)) {
                return false;
            }
            found = true;
            break;
        }
        previousJd = currentJd;
        previousError = currentError;
    }
    if (!found) {
        if (error) *error = "Unable to bracket a progressed lunar return near the selected anchor.";
        return false;
    }

    const double progressedDays = returnJd - natalJd;
    if (progressedDays < 0.5) {
        if (error) {
            *error = direction < 0
                ? "No earlier postnatal progressed lunar return occurs before this anchor."
                : "Unable to resolve a postnatal progressed lunar return.";
        }
        return false;
    }
    const double realAgeDays = progressedDays * kDaysPerYear;
    const QDateTime targetUtc = natalUtc.addMSecs(
        static_cast<qint64>(std::llround(realAgeDays * 86400000.0)));
    if (targetUtc.date().year() < 1800 || targetUtc.date().year() > 2399) {
        if (error) *error = "The progressed lunar return falls outside the supported 1800-2399 range.";
        return false;
    }
    double exactMoon = 0.0;
    if (!moonLongitude(swe, returnJd, calculationFlags, &exactMoon, error)) return false;

    event->valid = true;
    event->returnNumber = std::max(1, static_cast<int>(std::llround(
        progressedDays / kMeanLunarReturnDays)));
    event->targetUtc = targetUtc;
    event->targetLocal = targetUtc.toTimeZone(targetTimezone);
    event->progressedUtc = natalUtc.addMSecs(
        static_cast<qint64>(std::llround(progressedDays * 86400000.0)));
    event->timezoneLabel = targetTimezoneLabel;
    event->natalMoonLongitude = natalMoon;
    event->progressedMoonLongitude = exactMoon;
    event->exactOrb = std::fabs(signedLongitudeError(exactMoon, natalMoon));
    return true;
}

}  // namespace dracoved
