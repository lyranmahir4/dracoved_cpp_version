#include "planetary_hours.h"

#include "timezone_utils.h"

#include <QTime>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

const QStringList& chaldeanOrder() {
    static const QStringList order = {
        "Saturn", "Jupiter", "Mars", "Sun", "Venus", "Mercury", "Moon",
    };
    return order;
}

bool solarEventForLocalDate(SwissEph& swe,
                            const QDate& date,
                            const QTimeZone& timezone,
                            double latitude,
                            double longitude,
                            const PlanetaryHoursCalculationOptions& options,
                            bool rise,
                            QDateTime* eventLocal,
                            QString* error) {
    if (!date.isValid() || !eventLocal) {
        if (error) *error = "Invalid date for sunrise/sunset calculation.";
        return false;
    }
    QDateTime midnightLocal(date, QTime(0, 0), timezone);
    if (!midnightLocal.isValid()) {
        midnightLocal = QDateTime(date, QTime(1, 0), timezone);
    }
    if (!midnightLocal.isValid()) {
        if (error) *error = "Unable to construct local midnight for the selected timezone.";
        return false;
    }
    const QDateTime startUtc = midnightLocal.toUTC();
    const double hour = startUtc.time().hour()
        + startUtc.time().minute() / 60.0
        + startUtc.time().second() / 3600.0
        + startUtc.time().msec() / 3600000.0;
    const double startJd = swe.julianDay(startUtc.date().year(), startUtc.date().month(),
                                        startUtc.date().day(), hour, SE_GREG_CAL);
    double eventJd = 0.0;
    QString eventError;
    const int result = swe.riseTrans(startJd, SE_SUN, 0,
                                     rise ? SE_CALC_RISE : SE_CALC_SET,
                                     longitude, latitude, options.elevationMeters,
                                     options.pressureHPa, options.temperatureC,
                                     &eventJd, &eventError);
    if (result == -2) {
        if (error) {
            *error = rise
                ? "The Sun does not rise on this date at the selected location."
                : "The Sun does not set on this date at the selected location.";
        }
        return false;
    }
    if (result < 0 || !std::isfinite(eventJd)) {
        if (error) {
            *error = eventError.isEmpty()
                ? QString("Unable to calculate %1.").arg(rise ? "sunrise" : "sunset")
                : eventError;
        }
        return false;
    }
    const qint64 offsetMs = static_cast<qint64>(std::llround((eventJd - startJd) * 86400000.0));
    *eventLocal = startUtc.addMSecs(offsetMs).toTimeZone(timezone);
    if (eventLocal->date() != date) {
        if (error) {
            *error = QString("No %1 occurs on %2 at the selected location.")
                .arg(rise ? "sunrise" : "sunset", date.toString("yyyy-MM-dd"));
        }
        return false;
    }
    return true;
}

bool solarDay(SwissEph& swe,
              const QDate& planetaryDate,
              const QTimeZone& timezone,
              double latitude,
              double longitude,
              const PlanetaryHoursCalculationOptions& options,
              QDateTime* sunrise,
              QDateTime* sunset,
              QDateTime* nextSunrise,
              QString* error) {
    QString calculationError;
    if (!solarEventForLocalDate(swe, planetaryDate, timezone, latitude, longitude,
                                options, true, sunrise, &calculationError)) {
        if (error) *error = calculationError;
        return false;
    }
    if (!solarEventForLocalDate(swe, planetaryDate, timezone, latitude, longitude,
                                options, false, sunset, &calculationError)) {
        if (error) *error = calculationError;
        return false;
    }
    if (!solarEventForLocalDate(swe, planetaryDate.addDays(1), timezone, latitude, longitude,
                                options, true, nextSunrise, &calculationError)) {
        if (error) *error = calculationError;
        return false;
    }
    if (!(*sunrise < *sunset && *sunset < *nextSunrise)) {
        if (error) *error = "Sunrise and sunset did not form a valid planetary day.";
        return false;
    }
    return true;
}

void appendIntervals(QVector<PlanetaryHourInterval>* hours,
                     const QDateTime& start,
                     const QDateTime& end,
                     bool daytime,
                     int firstRulerIndex) {
    if (!hours) return;
    const qint64 totalMs = start.msecsTo(end);
    const int sequenceOffset = daytime ? 0 : 12;
    for (int i = 0; i < 12; ++i) {
        PlanetaryHourInterval interval;
        interval.sequence = sequenceOffset + i + 1;
        interval.periodHour = i + 1;
        interval.daytime = daytime;
        interval.ruler = chaldeanOrder()[(firstRulerIndex + sequenceOffset + i) % 7];
        interval.startLocal = start.addMSecs(totalMs * i / 12);
        interval.endLocal = start.addMSecs(totalMs * (i + 1) / 12);
        hours->push_back(interval);
    }
}

}  // namespace

QString planetaryDayRuler(Qt::DayOfWeek weekday) {
    switch (weekday) {
        case Qt::Monday: return "Moon";
        case Qt::Tuesday: return "Mars";
        case Qt::Wednesday: return "Mercury";
        case Qt::Thursday: return "Jupiter";
        case Qt::Friday: return "Venus";
        case Qt::Saturday: return "Saturn";
        case Qt::Sunday:
        default: return "Sun";
    }
}

QString planetaryRulerMeaning(const QString& ruler) {
    if (ruler == "Sun") return "Visibility, leadership, authority and vitality.";
    if (ruler == "Moon") return "Emotion, movement, domestic matters and public response.";
    if (ruler == "Mercury") return "Communication, study, writing, analysis and trade.";
    if (ruler == "Venus") return "Relationships, harmony, art, beauty and pleasure.";
    if (ruler == "Mars") return "Action, courage, competition, conflict and physical effort.";
    if (ruler == "Jupiter") return "Growth, opportunity, teaching, law and generosity.";
    if (ruler == "Saturn") return "Discipline, responsibility, boundaries and long-term work.";
    return {};
}

void updatePlanetaryHoursMoment(PlanetaryHoursResult* result,
                                const QDateTime& localMoment) {
    if (!result || !result->valid) return;
    result->momentLocal = localMoment;
    result->currentIndex = -1;
    for (int i = 0; i < result->hours.size(); ++i) {
        const auto& hour = result->hours[i];
        if (localMoment >= hour.startLocal && localMoment < hour.endLocal) {
            result->currentIndex = i;
            break;
        }
    }
}

bool calculatePlanetaryHours(SwissEph& swe,
                             const QDateTime& localMoment,
                             const QString& timezoneLabel,
                             double latitude,
                             double longitude,
                             const PlanetaryHoursCalculationOptions& options,
                             PlanetaryHoursResult* result,
                             QString* error) {
    if (!result) {
        if (error) *error = "Planetary-hours output is unavailable.";
        return false;
    }
    *result = {};
    if (!swe.isLoaded()) {
        if (error) *error = "Swiss Ephemeris is not loaded.";
        result->error = error ? *error : QString("Swiss Ephemeris is not loaded.");
        return false;
    }
    if (!std::isfinite(latitude) || !std::isfinite(longitude)
        || latitude < -90.0 || latitude > 90.0
        || longitude < -180.0 || longitude > 180.0) {
        if (error) *error = "Latitude or longitude is outside the valid range.";
        result->error = error ? *error : QString("Invalid coordinates.");
        return false;
    }
    if (!std::isfinite(options.elevationMeters)
        || options.elevationMeters < -500.0 || options.elevationMeters > 10000.0
        || !std::isfinite(options.pressureHPa)
        || options.pressureHPa < 0.0 || options.pressureHPa > 1100.0
        || !std::isfinite(options.temperatureC)
        || options.temperatureC < -100.0 || options.temperatureC > 100.0) {
        if (error) *error = "Atmospheric calculation settings are outside their valid ranges.";
        result->error = error ? *error : QString("Invalid atmospheric settings.");
        return false;
    }
    QTimeZone timezone;
    QString normalizedTimezone;
    QString timezoneError;
    if (!parseTimezoneInput(timezoneLabel, &timezone, &normalizedTimezone, &timezoneError)) {
        if (error) *error = timezoneError;
        result->error = timezoneError;
        return false;
    }
    if (!localMoment.isValid()) {
        if (error) *error = "The selected local date and time are invalid.";
        result->error = error ? *error : QString("Invalid local date/time.");
        return false;
    }

    QDateTime moment = localMoment.toTimeZone(timezone);
    QDate planetaryDate = moment.date();
    QDateTime sunrise;
    QDateTime sunset;
    QDateTime nextSunrise;
    QString calculationError;
    if (!solarDay(swe, planetaryDate, timezone, latitude, longitude, options,
                  &sunrise, &sunset, &nextSunrise, &calculationError)) {
        if (error) *error = calculationError;
        result->error = calculationError;
        return false;
    }
    if (moment < sunrise) {
        planetaryDate = planetaryDate.addDays(-1);
        if (!solarDay(swe, planetaryDate, timezone, latitude, longitude, options,
                      &sunrise, &sunset, &nextSunrise, &calculationError)) {
            if (error) *error = calculationError;
            result->error = calculationError;
            return false;
        }
    }

    result->timezoneLabel = normalizedTimezone;
    result->planetaryDate = planetaryDate;
    result->options = options;
    result->beforeSunrise = moment.date() != planetaryDate;
    result->dayRuler = planetaryDayRuler(static_cast<Qt::DayOfWeek>(planetaryDate.dayOfWeek()));
    result->sunrise = sunrise;
    result->sunset = sunset;
    result->nextSunrise = nextSunrise;
    result->dayHourMilliseconds = sunrise.msecsTo(sunset) / 12;
    result->nightHourMilliseconds = sunset.msecsTo(nextSunrise) / 12;
    const int firstRulerIndex = chaldeanOrder().indexOf(result->dayRuler);
    appendIntervals(&result->hours, sunrise, sunset, true, std::max(0, firstRulerIndex));
    appendIntervals(&result->hours, sunset, nextSunrise, false, std::max(0, firstRulerIndex));
    result->valid = result->hours.size() == 24;
    if (normalizedTimezone.startsWith("UTC+") || normalizedTimezone.startsWith("UTC-")) {
        result->warnings.push_back(
            "A fixed UTC offset is being used; daylight-saving changes are not applied.");
    }
    result->warnings.push_back(
        "Sunrise and sunset use a standard apparent horizon; local terrain and obstructions are not modeled.");
    if (result->beforeSunrise) {
        result->warnings.push_back(
            "The selected civil date is before sunrise, so it belongs to the previous planetary day.");
    }
    updatePlanetaryHoursMoment(result, moment);
    if (!result->valid) {
        result->error = "Unable to construct all 24 planetary hours.";
        if (error) *error = result->error;
        return false;
    }
    return true;
}

}  // namespace dracoved
