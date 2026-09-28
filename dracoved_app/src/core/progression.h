#pragma once

#include "chart_types.h"
#include "swiss_eph.h"

#include <QDateTime>

namespace dracoved {

class SecondaryProgressionEngine {
public:
    SecondaryProgressionEngine(SwissEph* swe, const QString& ephePath);

    // Scans can omit fixed stars/aspects; event searches can also omit Lots.
    bool compute(const NatalInput& natalInput, const QDateTime& targetLocal, const QString& targetTzLabel,
                 NatalChart* out, QString* error, bool includeDisplayData = true, bool includeLots = true);
    void setEphePath(const QString& path);

private:
    SwissEph* swe_ = nullptr;
    QString ephePath_;
};

}  // namespace dracoved
