#pragma once

#include "chart_types.h"
#include "swiss_eph.h"

#include <QDateTime>
#include <QString>

namespace dracoved {

struct ProgressedLunarReturnEvent {
    bool valid = false;
    int returnNumber = 0;
    QDateTime targetUtc;
    QDateTime targetLocal;
    QDateTime progressedUtc;
    QString timezoneLabel;
    double natalMoonLongitude = 0.0;
    double progressedMoonLongitude = 0.0;
    double exactOrb = 0.0;
};

// Finds the previous or next postnatal secondary-progressed lunar return.
// The anchor is expressed in real time; internally the same 365.2425-day
// day-for-a-year key used by SecondaryProgressionEngine is applied.
bool findProgressedLunarReturn(SwissEph& swe,
                               const QString& ephePath,
                               const NatalInput& natalInput,
                               const QDateTime& anchorLocal,
                               const QString& anchorTimezone,
                               int direction,
                               ProgressedLunarReturnEvent* event,
                               QString* error);

}  // namespace dracoved
