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

// Part of Fortune depends only on the Ascendant, Sun, Moon and sect - not on the
// prenatal syzygy the other lots require - so it can be computed on the fast
// paths that skip the full Arabic Lots calculation. This is the single source of
// the formula: calculateArabicLots calls it too.
double calculatePartOfFortuneLongitude(double ascendant, double sun, double moon, bool isDay);

// Builds Part of Fortune on its own, with sign, degree and house populated
// exactly as calculateArabicLots would. Returns a default-constructed
// BodyPosition (empty name) if the context has no Sun or Moon, so callers should
// check `name.isEmpty()` before using it.
BodyPosition makePartOfFortune(const LotCalculationContext& ctx);
QVector<BodyPosition> calculateArabicLots(const LotCalculationContext& ctx);

}  // namespace dracoved
