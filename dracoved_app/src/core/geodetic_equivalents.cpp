#include "geodetic_equivalents.h"

#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

constexpr double kPi = 3.1415926535897932384626433832795;

double degreesToRadians(double degrees) {
    return degrees * kPi / 180.0;
}

double radiansToDegrees(double radians) {
    return radians * 180.0 / kPi;
}

}  // namespace

double normalizeGeodeticDegrees(double degrees) {
    double normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    return normalized;
}

double normalizeGeodeticLongitude(double longitude) {
    double normalized = normalizeGeodeticDegrees(longitude + 180.0) - 180.0;
    if (normalized <= -180.0) {
        normalized = 180.0;
    }
    return normalized;
}

double geodeticAngularDistance(double first, double second) {
    double difference = std::fabs(
        normalizeGeodeticDegrees(first) - normalizeGeodeticDegrees(second));
    if (difference > 180.0) {
        difference = 360.0 - difference;
    }
    return difference;
}

GeodeticEquatorialPosition geodeticEclipticToEquatorial(
    double eclipticLongitude, double obliquity) {
    const double longitude = degreesToRadians(
        normalizeGeodeticDegrees(eclipticLongitude));
    const double epsilon = degreesToRadians(obliquity);

    GeodeticEquatorialPosition position;
    position.rightAscension = normalizeGeodeticDegrees(radiansToDegrees(
        std::atan2(std::sin(longitude) * std::cos(epsilon),
                   std::cos(longitude))));
    position.declination = radiansToDegrees(std::asin(
        std::sin(epsilon) * std::sin(longitude)));
    return position;
}

double geodeticEclipticLongitudeToArmc(
    double eclipticLongitude, double obliquity) {
    return geodeticEclipticToEquatorial(
        eclipticLongitude, obliquity).rightAscension;
}

double geodeticArmcToEclipticLongitude(
    double armc, double obliquity) {
    const double theta = degreesToRadians(normalizeGeodeticDegrees(armc));
    const double epsilon = degreesToRadians(obliquity);
    return normalizeGeodeticDegrees(radiansToDegrees(std::atan2(
        std::sin(theta), std::cos(theta) * std::cos(epsilon))));
}

GeodeticAngles calculateGeodeticAngles(
    double latitude, double longitude, double referenceMeridian,
    double obliquity) {
    GeodeticAngles result;
    result.latitude = std::clamp(latitude, -89.999, 89.999);
    result.longitude = normalizeGeodeticLongitude(longitude);
    result.midheaven = normalizeGeodeticDegrees(
        result.longitude - referenceMeridian);
    result.imumCoeli = normalizeGeodeticDegrees(result.midheaven + 180.0);

    const double armc = degreesToRadians(
        geodeticEclipticLongitudeToArmc(result.midheaven, obliquity));
    const double epsilon = degreesToRadians(obliquity);
    const double phi = degreesToRadians(result.latitude);
    const double ascendant = std::atan2(
        -std::cos(armc),
        std::sin(epsilon) * std::tan(phi)
            + std::cos(epsilon) * std::sin(armc)) + kPi;
    result.ascendant = normalizeGeodeticDegrees(
        radiansToDegrees(ascendant));
    result.descendant = normalizeGeodeticDegrees(result.ascendant + 180.0);
    result.valid = std::isfinite(result.ascendant)
        && std::isfinite(result.midheaven);
    return result;
}

QVector<QPointF> makeGeodeticMeridianLine(double longitude) {
    QVector<QPointF> points;
    points.reserve(2);
    const double normalized = normalizeGeodeticLongitude(longitude);
    points.push_back(QPointF(normalized, -85.0));
    points.push_back(QPointF(normalized, 85.0));
    return points;
}

QVector<QPointF> makeGeodeticHorizonLine(
    double targetEclipticLongitude, bool rising,
    double referenceMeridian, double obliquity,
    double latitudeStep) {
    QVector<QPointF> points;
    const double step = std::clamp(latitudeStep, 0.1, 5.0);
    const GeodeticEquatorialPosition target =
        geodeticEclipticToEquatorial(targetEclipticLongitude, obliquity);
    const double declination = degreesToRadians(target.declination);

    points.reserve(static_cast<int>(170.0 / step) + 1);
    for (double latitude = -85.0; latitude <= 85.0001; latitude += step) {
        const double phi = degreesToRadians(latitude);
        const double cosineHourAngle = -std::tan(phi) * std::tan(declination);
        if (cosineHourAngle < -1.0 || cosineHourAngle > 1.0) {
            continue;
        }
        const double hourAngle = radiansToDegrees(std::acos(
            std::clamp(cosineHourAngle, -1.0, 1.0)));
        const double armc = normalizeGeodeticDegrees(
            target.rightAscension + (rising ? -hourAngle : hourAngle));
        const double geodeticMc = geodeticArmcToEclipticLongitude(
            armc, obliquity);
        const double longitude = normalizeGeodeticLongitude(
            referenceMeridian + geodeticMc);
        points.push_back(QPointF(longitude, latitude));
    }
    return points;
}

}  // namespace dracoved
