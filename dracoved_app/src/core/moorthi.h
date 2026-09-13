#pragma once

#include "swiss_eph.h"
#include <QVector>

namespace dracoved {

enum class Moorthi { Swarna, Rajata, Tamra, Loha };
Moorthi moorthiForCount(int count);
QString moorthiName(Moorthi value);

struct MoorthiEntry {
    double jd = 0.0;
    int newSign = 0;
    bool retrograde = false;
    int moonSign = 0;
    double moonLongitude = 0.0;
    int count = 0;
    Moorthi moorthi = Moorthi::Loha;
};

// Scan at most six hours with sidereal mode already configured by the caller.
// A node's south longitude is obtained by adding 180 degrees.
bool findMoorthiEntries(SwissEph& swe, int body, double offset, double fromJd,
                       double toJd, int natalMoonSign, QVector<MoorthiEntry>* out,
                       QString* error);
}  // namespace dracoved
