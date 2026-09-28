#pragma once

#include <algorithm>
#include <cmath>

namespace dracoved {
// A display-only time window. The calculation range never changes when zooming.
class TimeGraphRange {
public:
    void setRange(double start, double end) {
        fullStart_ = start; fullEnd_ = end; reset();
    }
    void reset() { start_ = fullStart_; end_ = fullEnd_; }
    double start() const { return start_; }
    double end() const { return end_; }
    bool zoomed() const { return start_ != fullStart_ || end_ != fullEnd_; }
    void panTo(double start) {
        if (!zoomed() || !std::isfinite(start)) return;
        const double span=end_-start_;
        start_=std::clamp(start,fullStart_,fullEnd_-span);
        end_=start_+span;
    }
    void zoom(double fraction, double steps) {
        const double fullSpan = fullEnd_ - fullStart_;
        if (fullSpan <= 0 || !std::isfinite(steps)) return;
        fraction = std::clamp(fraction, 0.0, 1.0);
        const double span = std::clamp((end_ - start_) * std::pow(0.8, std::clamp(steps, -8.0, 8.0)),
                                       std::min(fullSpan, 1.0 / 1440), fullSpan);
        if (span >= fullSpan) { reset(); return; }
        const double anchor = start_ + fraction * (end_ - start_);
        start_ = std::clamp(anchor - fraction * span, fullStart_, fullEnd_ - span);
        end_ = start_ + span;
    }
private:
    double fullStart_ = 0, fullEnd_ = 0, start_ = 0, end_ = 0;
};
} // namespace dracoved
