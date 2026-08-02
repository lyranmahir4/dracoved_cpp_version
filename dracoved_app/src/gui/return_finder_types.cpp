#include "return_finder_types.h"

namespace dracoved {

QString returnFinderTypeLabel(ReturnFinderType type) {
    return type == ReturnFinderType::Lunar ? "Lunar Return" : "Solar Return";
}

QString returnFinderAspectLabel(ReturnFinderAspect aspect) {
    switch (aspect) {
        case ReturnFinderAspect::Sextile: return "Sextile";
        case ReturnFinderAspect::Square: return "Square";
        case ReturnFinderAspect::Trine: return "Trine";
        case ReturnFinderAspect::Opposition: return "Opposition";
        case ReturnFinderAspect::AnyMajor: return "Any Major Aspect";
        case ReturnFinderAspect::Conjunction:
        default: return "Conjunction";
    }
}

double returnFinderAspectAngle(ReturnFinderAspect aspect) {
    switch (aspect) {
        case ReturnFinderAspect::Sextile: return 60.0;
        case ReturnFinderAspect::Square: return 90.0;
        case ReturnFinderAspect::Trine: return 120.0;
        case ReturnFinderAspect::Opposition: return 180.0;
        case ReturnFinderAspect::AnyMajor:
        case ReturnFinderAspect::Conjunction:
        default: return 0.0;
    }
}

}  // namespace dracoved
