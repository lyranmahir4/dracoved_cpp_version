#pragma once

#include "../core/chart_types.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace dracoved::synastry {

// Which compatibility system produced a result. Western cross-aspect synastry is
// the only implemented system today. Vedic Ashtakoota (guna milan) is planned and
// will report through these same structures, so callers written against this API
// will not need to change when it lands.
enum class System {
    WesternAspects = 0,
    VedicAshtakoota = 1,  // reserved: not implemented yet
};

struct Options {
    AspectOrbs orbs = defaultAspectOrbs();
    bool includeAngles = true;
    // When non-empty, only these unprefixed point names participate. This is how
    // the caller applies its own body-visibility policy; this module stays free
    // of any knowledge of user settings.
    QStringList allowedNames;
};

// One point in a chart that synastry should consider. Angles carry no speed.
struct Point {
    QString name;
    double longitude = 0.0;
    double speed = 0.0;
    bool hasSpeed = false;
};

// One directed contact from a point in chart A to a point in chart B.
// Deliberately NOT named "Aspect": a Vedic koota comparison also produces
// pairwise findings and will reuse this shape, with label carrying its meaning
// and orb/applying simply left at their defaults.
struct Contact {
    System system = System::WesternAspects;
    QString aName;   // unprefixed point name in chart A
    QString bName;   // unprefixed point name in chart B
    QString label;   // "Trine" today; a koota name once Vedic lands
    double aLongitude = 0.0;
    double bLongitude = 0.0;
    double orb = 0.0;      // degrees from exact
    double maxOrb = 0.0;
    int applying = -1;     // -1 unknown, 0 separating, 1 applying
    bool mutualPair = false;  // the reciprocal contact also exists
    double score = 0.0;    // system-defined weight; unused in Stage 1
};

QVector<Point> collectPoints(const NatalChart& chart, const Options& options);

// Cross-aspect extraction between two charts, returned tightest orb first.
QVector<Contact> westernContacts(const NatalChart& chartA, const NatalChart& chartB,
                                 const Options& options);

// Dispatch by system. Returns an empty list for systems not yet implemented, so
// callers can be written once and never revisited.
QVector<Contact> contactsFor(System system, const NatalChart& chartA,
                            const NatalChart& chartB, const Options& options);

// Flags contacts whose reciprocal also exists (A's Venus to B's Mars alongside
// A's Mars to B's Venus). Most synastry traditions weight these more heavily.
void markMutualPairs(QVector<Contact>& contacts);

}  // namespace dracoved::synastry
