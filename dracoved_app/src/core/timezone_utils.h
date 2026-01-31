#pragma once

#include <QTimeZone>

namespace dracoved {

bool parseTimezoneInput(const QString& input, QTimeZone* outTz, QString* outLabel, QString* error);

}  // namespace dracoved

