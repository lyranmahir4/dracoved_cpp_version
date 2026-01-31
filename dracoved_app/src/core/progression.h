#pragma once

#include "chart_types.h"
#include "swiss_eph.h"

#include <QDateTime>

namespace dracoved {

class SecondaryProgressionEngine {
public:
    SecondaryProgressionEngine(SwissEph* swe, const QString& ephePath);

    bool compute(const NatalInput& natalInput, const QDateTime& targetLocal, const QString& targetTzLabel,
                 NatalChart* out, QString* error);
    void setEphePath(const QString& path);

private:
    SwissEph* swe_ = nullptr;
    QString ephePath_;
};

}  // namespace dracoved
