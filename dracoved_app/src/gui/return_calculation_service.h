#pragma once

#include "../core/chart_types.h"
#include "../core/swiss_eph.h"

#include <QDateTime>
#include <QString>

#include <functional>

namespace dracoved::returncalc {

using CancelCheck = std::function<bool()>;

bool solarReturnTimeUtc(SwissEph& swe,
                        const NatalInput& natalInput,
                        int year,
                        const QString& timezone,
                        double targetLongitude,
                        QDateTime* outUtc,
                        QDateTime* outLocal,
                        QString* error,
                        const CancelCheck& cancelled = {});

bool lunarReturnTimeUtc(SwissEph& swe,
                        const NatalInput& natalInput,
                        const QDateTime& anchorUtc,
                        int direction,
                        double targetLongitude,
                        const QString& timezone,
                        QDateTime* outUtc,
                        QDateTime* outLocal,
                        QString* error,
                        const CancelCheck& cancelled = {});

}  // namespace dracoved::returncalc

