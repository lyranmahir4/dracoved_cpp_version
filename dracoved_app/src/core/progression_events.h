#pragma once

#include "chart_types.h"
#include "swiss_eph.h"
#include <functional>

namespace dracoved {

struct ProgressionEventQuery {
    NatalInput input;
    NatalChart natal;
    QDateTime start;
    QDateTime end; // Exclusive; expressed in the requested display timezone.
    QString timezone;
    QStringList bodies;
    bool signs = true;
    bool houses = true;
    bool angles = true;
    bool stations = false;
    bool progressedHouses = false;
};

struct ProgressionEvent {
    QDateTime time;
    QString body;
    QString kind;
    QString transition;
    QString motion;
    double longitude = 0.0;
    QString detail;
};

// Returns partial results on cancellation/error; callers must label them accordingly.
QVector<ProgressionEvent> findProgressionEvents(SwissEph& swe, const QString& ephePath,
    const ProgressionEventQuery& query, const std::function<bool()>& cancelled,
    const std::function<void(int)>& progress, QString* error);

} // namespace dracoved
