#pragma once

#include "chart_types.h"
#include "swiss_eph.h"

namespace dracoved {

struct TropicalComputeOptions {
    bool includeArabicLots = true;
    bool includeFixedStars = true;
    bool includeAspectGrid = true;
};

class TropicalNatalEngine {
public:
    TropicalNatalEngine(SwissEph* swe, const QString& ephePath);

    bool compute(const NatalInput& input, NatalChart* out, QString* error);
    bool compute(const NatalInput& input, const TropicalComputeOptions& options, NatalChart* out, QString* error);
    void setEphePath(const QString& path);

private:
    SwissEph* swe_ = nullptr;
    QString ephePath_;
};

}  // namespace dracoved
