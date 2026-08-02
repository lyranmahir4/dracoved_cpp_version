#pragma once

#include "chart_types.h"
#include "swiss_eph.h"

#include <QMap>
#include <QString>
#include <QVector>

namespace dracoved {

struct PrenatalSyzygy {
    bool valid = false;
    bool conjunctional = false;
    double longitude = 0.0;
};

struct LotCalculationContext {
    double asc = 0.0;
    double mc = 0.0;
    QVector<double> cusps;
    HouseSystem houseSystem = HouseSystem::WholeSign;
    bool isDay = false;
    Gender gender = Gender::Unspecified;
    QMap<QString, double> bodies;
    bool hasPrenatalSyzygy = false;
    bool prenatalConjunctional = false;
    double prenatalSyzygyLongitude = 0.0;
};

bool findPrenatalSyzygy(const SwissEph* swe, double jdUt, int calcFlags, PrenatalSyzygy* out, QString* error);
double calculateLotOfErosLongitude(double ascendant, double spirit, double venus, bool isDay);
QVector<BodyPosition> calculateArabicLots(const LotCalculationContext& ctx);

}  // namespace dracoved
