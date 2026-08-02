#pragma once

#include <QPointF>
#include <QVector>

namespace dracoved {

struct GeodeticEquatorialPosition {
    double rightAscension = 0.0;
    double declination = 0.0;
};

struct GeodeticAngles {
    bool valid = false;
    double latitude = 0.0;
    double longitude = 0.0;
    double midheaven = 0.0;
    double imumCoeli = 0.0;
    double ascendant = 0.0;
    double descendant = 0.0;
};

double normalizeGeodeticDegrees(double degrees);
double normalizeGeodeticLongitude(double longitude);
double geodeticAngularDistance(double first, double second);

GeodeticEquatorialPosition geodeticEclipticToEquatorial(
    double eclipticLongitude, double obliquity);
double geodeticEclipticLongitudeToArmc(
    double eclipticLongitude, double obliquity);
double geodeticArmcToEclipticLongitude(
    double armc, double obliquity);

GeodeticAngles calculateGeodeticAngles(
    double latitude, double longitude, double referenceMeridian,
    double obliquity);

QVector<QPointF> makeGeodeticMeridianLine(double longitude);
QVector<QPointF> makeGeodeticHorizonLine(
    double targetEclipticLongitude, bool rising,
    double referenceMeridian, double obliquity,
    double latitudeStep = 0.5);

}  // namespace dracoved
