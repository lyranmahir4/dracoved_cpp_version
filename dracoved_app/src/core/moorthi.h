#pragma once

#include "swiss_eph.h"
#include <QVector>

namespace dracoved {

enum class Moorthi { NotApplicable = -1, Swarna = 0, Rajata, Tamra, Loha };
Moorthi moorthiForCount(int count);
QString moorthiName(Moorthi value);

struct MoorthiEntry {
    double jd = 0.0;
    int newSign = 0;
    bool retrograde = false;
    int moonSign = 0;
    double moonLongitude = 0.0;
    int count = 0;
    Moorthi moorthi = Moorthi::NotApplicable;
};

// Scan at most six hours. Caller configures the sidereal mode when used;
// zodiacFlags = 0 selects tropical positions, the default remains sidereal.
// A node's south longitude is obtained by adding 180 degrees.
// Moon entries are sampling anchors only: Moorthi is NotApplicable, count is 0.
bool findMoorthiEntries(SwissEph& swe, int body, double offset, double fromJd,
                       double toJd, int natalMoonSign, QVector<MoorthiEntry>* out,
                       QString* error, int zodiacFlags = SEFLG_SIDEREAL);

// Kaksha boundaries within signs (multiples of 3°45'); sign boundaries are
// supplied by findMoorthiEntries. Uses the same zodiac flags. Up to six hours.
bool findKakshaCrossings(SwissEph& swe, int body, double offset, double fromJd,
                         double toJd, QVector<double>* out, QString* error,
                         int zodiacFlags = SEFLG_SIDEREAL);
}  // namespace dracoved
