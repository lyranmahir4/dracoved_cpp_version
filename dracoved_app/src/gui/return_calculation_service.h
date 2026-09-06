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

// Tajaka (Varshaphala) return per P.V.R. Narasimha Rao: the return moment is
// always the Sun reaching its natal TROPICAL longitude, regardless of the
// zodiac used for judging the chart. targetLongitude must therefore be the
// natal Sun's tropical longitude. The resulting chart itself is computed in
// the caller's chosen zodiac as usual.
bool tajakaSolarReturnTimeUtc(SwissEph& swe,
                              const NatalInput& natalInput,
                              int year,
                              const QString& timezone,
                              double targetTropicalLongitude,
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

