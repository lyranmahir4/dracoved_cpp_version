#include "transit_calc_service.h"

#include "../core/formatting.h"
#include "../core/swiss_eph.h"

#include <QMap>

#include <algorithm>
#include <cmath>

namespace dracoved::transitcalc {

double angularDiffSigned(double a, double b) {
    return std::fmod((a - b + 540.0), 360.0) - 180.0;
}

double angularDiffAbs(double a, double b) {
    return std::fabs(angularDiffSigned(a, b));
}

bool isNodeName(const QString& name) {
    return name == "North Node" || name == "South Node";
}

bool isAngleName(const QString& name) {
    return name == "Ascendant" || name == "Midheaven" || name == "Descendant" || name == "IC";
}

bool isDerivedPointName(const QString& name) {
    return name == "Vertex" || isArabicLotName(name);
}

bool isBenefic(const QString& name) {
    return name == "Venus" || name == "Jupiter";
}

bool isMalefic(const QString& name) {
    return name == "Mars" || name == "Saturn";
}

double bodyWeightFor(const QString& name) {
    if (isAngleName(name)) {
        return 1.0;
    }
    if (name == "Sun" || name == "Moon") {
        return 1.0;
    }
    if (name == "Mercury" || name == "Venus" || name == "Mars") {
        return 0.8;
    }
    if (name == "Jupiter" || name == "Saturn") {
        return 0.7;
    }
    if (name == "Uranus" || name == "Neptune" || name == "Pluto") {
        return 0.5;
    }
    if (name == "Chiron") {
        return 0.4;
    }
    if (isAsteroidBody(name)) {
        return 0.35;
    }
    if (isNodeName(name)) {
        return 0.4;
    }
    if (isArabicLotName(name)) {
        return 0.45;
    }
    return 0.6;
}

int bodyIdForName(const QString& name) {
    if (name == "Sun") return SE_SUN;
    if (name == "Moon") return SE_MOON;
    if (name == "Mercury") return SE_MERCURY;
    if (name == "Venus") return SE_VENUS;
    if (name == "Mars") return SE_MARS;
    if (name == "Jupiter") return SE_JUPITER;
    if (name == "Saturn") return SE_SATURN;
    if (name == "Uranus") return SE_URANUS;
    if (name == "Neptune") return SE_NEPTUNE;
    if (name == "Pluto") return SE_PLUTO;
    if (name == "Chiron") return SE_CHIRON;
    if (name == "Pholus") return SE_PHOLUS;
    if (name == "Ceres") return SE_CERES;
    if (name == "Pallas") return SE_PALLAS;
    if (name == "Juno") return SE_JUNO;
    if (name == "Vesta") return SE_VESTA;
    if (name == "North Node" || name == "South Node") return SE_MEAN_NODE;
    if (name == "Lilith") return SE_MEAN_APOG;
    return -1;
}

bool isComputableBody(const QString& name) {
    return bodyIdForName(name) >= 0;
}

QStringList transitCalculableBodyOrder() {
    QStringList bodies;
    for (const auto& name : tropicalBodyOrder()) {
        if (isAngleName(name) || isDerivedPointName(name)) {
            continue;
        }
        if (!isComputableBody(name)) {
            continue;
        }
        bodies.push_back(name);
    }
    return bodies;
}

QStringList geodeticBodyOrder() {
    QStringList bodies;
    for (const auto& name : tropicalBodyOrder()) {
        if (isAngleName(name)) {
            continue;
        }
        if (isDerivedPointName(name) || isAsteroidBody(name)) {
            continue;
        }
        bodies.push_back(name);
    }
    return bodies;
}

QColor geodeticColorForIndex(int index) {
    static const QVector<QColor> palette = {
        QColor("#e74c3c"),
        QColor("#f1c40f"),
        QColor("#2ecc71"),
        QColor("#3498db"),
        QColor("#9b59b6"),
        QColor("#e67e22"),
        QColor("#1abc9c"),
        QColor("#95a5a6"),
        QColor("#34495e"),
        QColor("#d35400"),
        QColor("#7f8c8d"),
        QColor("#8e44ad"),
        QColor("#16a085"),
        QColor("#27ae60"),
        QColor("#2980b9"),
        QColor("#c0392b"),
    };
    if (palette.isEmpty()) {
        return QColor("#2c3e50");
    }
    const int idx = (index >= 0) ? (index % palette.size()) : 0;
    return palette[idx];
}

double aspectAngleForLabel(const QString& label) {
    if (label == "Conjunction") return 0.0;
    if (label == "Sextile") return 60.0;
    if (label == "Square") return 90.0;
    if (label == "Trine") return 120.0;
    if (label == "Opposition") return 180.0;
    return 0.0;
}

double clampStepDays(double speedAbs) {
    const double minStep = 0.05;
    const double maxStep = 5.0;
    if (speedAbs <= 0.001) {
        return maxStep;
    }
    double step = 2.0 / speedAbs;
    if (step < minStep) {
        return minStep;
    }
    if (step > maxStep) {
        return maxStep;
    }
    return step;
}

QDateTime midTimeUtc(const QDateTime& a, const QDateTime& b) {
    const qint64 half = a.secsTo(b) / 2;
    return a.addSecs(half);
}

QString formatDegreeDms(double deg) {
    if (std::isnan(deg)) {
        return "N/A";
    }
    double v = deg;
    while (v < 0.0) {
        v += 30.0;
    }
    while (v >= 30.0) {
        v -= 30.0;
    }
    int whole = static_cast<int>(v);
    double minutesFull = (v - whole) * 60.0;
    int minutes = static_cast<int>(minutesFull);
    double seconds = (minutesFull - minutes) * 60.0;
    if (seconds >= 59.995) {
        seconds = 0.0;
        minutes += 1;
    }
    if (minutes >= 60) {
        minutes -= 60;
        whole += 1;
    }
    if (whole >= 30) {
        whole -= 30;
    }
    return QString("%1%2%3'%4\"")
        .arg(QString::number(whole).rightJustified(2, '0'))
        .arg(QChar(0x00B0))
        .arg(QString::number(minutes).rightJustified(2, '0'))
        .arg(QString::number(seconds, 'f', 0).rightJustified(2, '0'));
}

QString aspectTargetFromLabel(const QString& text) {
    const QStringList labels = {"Conjunction", "Sextile", "Square", "Trine", "Opposition"};
    for (const auto& label : labels) {
        const QString prefix = label + " ";
        if (text.startsWith(prefix)) {
            return text.mid(prefix.size());
        }
    }
    return QString();
}

QString abbrevForName(const QString& name) {
    static const QMap<QString, QString> abbrev = [] {
        QMap<QString, QString> map;
        const auto order = tropicalBodyOrder();
        const auto abbrev = tropicalBodyAbbrev();
        for (int i = 0; i < order.size() && i < abbrev.size(); ++i) {
            map.insert(order[i], abbrev[i]);
        }
        return map;
    }();
    return abbrev.value(name, name.left(2));
}

bool findAngleLongitude(const NatalChart& chart, const QString& name, double* outLon) {
    if (name == "Ascendant") {
        if (outLon) *outLon = chart.angles.asc;
        return true;
    }
    if (name == "Midheaven") {
        if (outLon) *outLon = chart.angles.mc;
        return true;
    }
    if (name == "Descendant") {
        if (outLon) *outLon = chart.angles.desc;
        return true;
    }
    if (name == "IC") {
        if (outLon) *outLon = chart.angles.ic;
        return true;
    }
    return false;
}

bool findBodyLongitude(const NatalChart& chart, const QString& name, double* outLon) {
    for (const auto& body : chart.bodies) {
        if (body.name == name) {
            if (outLon) {
                *outLon = body.longitude;
            }
            return true;
        }
    }
    return false;
}

bool aspectForDiff(double diff, const AspectOrbs& orbs, QString* outLabel, double* outOrb, double* outMaxOrb) {
    struct AspectDef {
        const char* name;
        double exact;
        double orb;
    };
    const AspectDef aspects[] = {
        {"Conjunction", 0.0, orbs.conjunction},
        {"Sextile", 60.0, orbs.sextile},
        {"Square", 90.0, orbs.square},
        {"Trine", 120.0, orbs.trine},
        {"Opposition", 180.0, orbs.opposition},
    };
    for (const auto& asp : aspects) {
        double delta = std::fabs(diff - asp.exact);
        if (delta <= asp.orb) {
            if (outLabel) {
                *outLabel = asp.name;
            }
            if (outOrb) {
                *outOrb = delta;
            }
            if (outMaxOrb) {
                *outMaxOrb = asp.orb;
            }
            return true;
        }
    }
    return false;
}

int calcHouseForLongitude(double lon, const QVector<HouseCusp>& cusps, double asc, HouseSystem system) {
    if (system == HouseSystem::Placidus && cusps.size() == 12) {
        const double c1 = cusps[0].longitude;
        double target = normalizeDegrees(lon - c1);
        int house = 1;
        double last = 0.0;
        for (int i = 0; i < cusps.size(); ++i) {
            double v = normalizeDegrees(cusps[i].longitude - c1);
            if (v < last) {
                continue;
            }
            if (target >= v) {
                house = i + 1;
                last = v;
            }
        }
        return house;
    }
    const int ascIdx = signIndex(asc);
    const int lonIdx = signIndex(lon);
    return ((lonIdx - ascIdx + 12) % 12) + 1;
}

}  // namespace dracoved::transitcalc
