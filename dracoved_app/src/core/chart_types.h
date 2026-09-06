#pragma once

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QVector>

namespace dracoved {

enum class HouseSystem {
    WholeSign,
    Placidus,
};

enum class ZodiacSystem {
    Tropical,
    Sidereal,
};

enum class LunarNodeType {
    Mean,
    True,
};

enum class LunarNodeMode {
    MeanOnly,
    TrueOnly,
    Both,
};

struct LunarNodePolicy {
    LunarNodeMode mode = LunarNodeMode::MeanOnly;
    LunarNodeType primary = LunarNodeType::Mean;
};

inline QString lunarNodeTypeToString(LunarNodeType type) {
    return type == LunarNodeType::True ? "True" : "Mean";
}

inline LunarNodeType lunarNodeTypeFromString(const QString& text) {
    return text.trimmed().compare("True", Qt::CaseInsensitive) == 0
        ? LunarNodeType::True
        : LunarNodeType::Mean;
}

inline QString lunarNodeModeToString(LunarNodeMode mode) {
    switch (mode) {
        case LunarNodeMode::TrueOnly:
            return "True";
        case LunarNodeMode::Both:
            return "Both";
        case LunarNodeMode::MeanOnly:
        default:
            return "Mean";
    }
}

inline LunarNodeMode lunarNodeModeFromString(const QString& text) {
    const QString normalized = text.trimmed();
    if (normalized.compare("True", Qt::CaseInsensitive) == 0
        || normalized.compare("TrueOnly", Qt::CaseInsensitive) == 0) {
        return LunarNodeMode::TrueOnly;
    }
    if (normalized.compare("Both", Qt::CaseInsensitive) == 0) {
        return LunarNodeMode::Both;
    }
    return LunarNodeMode::MeanOnly;
}

inline LunarNodeType effectivePrimaryNodeType(const LunarNodePolicy& policy) {
    if (policy.mode == LunarNodeMode::TrueOnly) {
        return LunarNodeType::True;
    }
    if (policy.mode == LunarNodeMode::MeanOnly) {
        return LunarNodeType::Mean;
    }
    return policy.primary;
}

inline bool lunarNodePolicyIncludes(const LunarNodePolicy& policy, LunarNodeType type) {
    return policy.mode == LunarNodeMode::Both
        || (policy.mode == LunarNodeMode::MeanOnly && type == LunarNodeType::Mean)
        || (policy.mode == LunarNodeMode::TrueOnly && type == LunarNodeType::True);
}

inline QString lunarNodePolicySummary(const LunarNodePolicy& policy) {
    if (policy.mode == LunarNodeMode::Both) {
        return QString("Both (%1 primary)").arg(lunarNodeTypeToString(effectivePrimaryNodeType(policy)));
    }
    return QString("%1 Nodes").arg(lunarNodeModeToString(policy.mode));
}

inline bool isLunarNodeName(const QString& name) {
    return name == "North Node" || name == "South Node"
        || name == "Mean North Node" || name == "Mean South Node"
        || name == "True North Node" || name == "True South Node";
}

inline bool isNorthLunarNodeName(const QString& name) {
    return name == "North Node" || name == "Mean North Node" || name == "True North Node";
}

inline LunarNodeType lunarNodeTypeForName(const QString& name, LunarNodeType genericType) {
    if (name.startsWith("True ")) {
        return LunarNodeType::True;
    }
    if (name.startsWith("Mean ")) {
        return LunarNodeType::Mean;
    }
    return genericType;
}

inline QString explicitLunarNodeName(bool north, LunarNodeType type) {
    return QString("%1 %2 Node")
        .arg(lunarNodeTypeToString(type))
        .arg(north ? "North" : "South");
}

inline QString lunarNodeDisplayName(const QString& internalName, const LunarNodePolicy& policy) {
    if (!isLunarNodeName(internalName)) {
        return internalName;
    }
    const bool north = isNorthLunarNodeName(internalName);
    const LunarNodeType type = lunarNodeTypeForName(internalName, effectivePrimaryNodeType(policy));
    return QString("%1 Node (%2)")
        .arg(north ? "North" : "South")
        .arg(lunarNodeTypeToString(type));
}

inline QString zodiacSystemToString(ZodiacSystem system) {
    switch (system) {
        case ZodiacSystem::Sidereal:
            return "Sidereal";
        case ZodiacSystem::Tropical:
        default:
            return "Tropical";
    }
}

inline ZodiacSystem zodiacSystemFromString(const QString& text) {
    const QString normalized = text.trimmed();
    if (normalized.compare("Sidereal", Qt::CaseInsensitive) == 0) {
        return ZodiacSystem::Sidereal;
    }
    return ZodiacSystem::Tropical;
}

enum class SiderealAyanamsa {
    Lahiri,
    Raman,
    Krishnamurti,
    FaganBradley,
    Yukteshwar,
    TrueCitra,
    TrueRevati,
    PushyaPaksha,
};

inline QString siderealAyanamsaToString(SiderealAyanamsa ayanamsa) {
    switch (ayanamsa) {
        case SiderealAyanamsa::Raman:
            return "Raman";
        case SiderealAyanamsa::Krishnamurti:
            return "Krishnamurti";
        case SiderealAyanamsa::FaganBradley:
            return "Fagan/Bradley";
        case SiderealAyanamsa::Yukteshwar:
            return "Yukteshwar";
        case SiderealAyanamsa::TrueCitra:
            return "True Citra";
        case SiderealAyanamsa::TrueRevati:
            return "True Revati";
        case SiderealAyanamsa::PushyaPaksha:
            return "Pushya-paksha";
        case SiderealAyanamsa::Lahiri:
        default:
            return "Lahiri";
    }
}

inline SiderealAyanamsa siderealAyanamsaFromString(const QString& text) {
    const QString normalized = text.trimmed();
    if (normalized.compare("Raman", Qt::CaseInsensitive) == 0) {
        return SiderealAyanamsa::Raman;
    }
    if (normalized.compare("Krishnamurti", Qt::CaseInsensitive) == 0) {
        return SiderealAyanamsa::Krishnamurti;
    }
    if (normalized.compare("Fagan/Bradley", Qt::CaseInsensitive) == 0) {
        return SiderealAyanamsa::FaganBradley;
    }
    if (normalized.compare("Yukteshwar", Qt::CaseInsensitive) == 0) {
        return SiderealAyanamsa::Yukteshwar;
    }
    if (normalized.compare("True Citra", Qt::CaseInsensitive) == 0) {
        return SiderealAyanamsa::TrueCitra;
    }
    if (normalized.compare("True Revati", Qt::CaseInsensitive) == 0) {
        return SiderealAyanamsa::TrueRevati;
    }
    if (normalized.compare("Pushya-paksha", Qt::CaseInsensitive) == 0) {
        return SiderealAyanamsa::PushyaPaksha;
    }
    return SiderealAyanamsa::Lahiri;
}

inline int siderealAyanamsaSwissMode(SiderealAyanamsa ayanamsa) {
    // Swiss Ephemeris sidereal mode IDs (swephexp.h).
    switch (ayanamsa) {
        case SiderealAyanamsa::Raman:
            return 3;
        case SiderealAyanamsa::Krishnamurti:
            return 5;
        case SiderealAyanamsa::FaganBradley:
            return 0;
        case SiderealAyanamsa::Yukteshwar:
            return 7;
        case SiderealAyanamsa::TrueCitra:
            return 27;
        case SiderealAyanamsa::TrueRevati:
            return 28;
        case SiderealAyanamsa::PushyaPaksha:
            // True Pushya: Delta Cancri fixed at 16 Cancer 0' (P.V.R. Rao's
            // Pushya-paksha ayanamsa, computed exactly per date).
            return 29;
        case SiderealAyanamsa::Lahiri:
        default:
            return 1;
    }
}

inline QStringList availableSiderealAyanamsaNames() {
    return {
        siderealAyanamsaToString(SiderealAyanamsa::Lahiri),
        siderealAyanamsaToString(SiderealAyanamsa::Raman),
        siderealAyanamsaToString(SiderealAyanamsa::Krishnamurti),
        siderealAyanamsaToString(SiderealAyanamsa::FaganBradley),
        siderealAyanamsaToString(SiderealAyanamsa::Yukteshwar),
        siderealAyanamsaToString(SiderealAyanamsa::TrueCitra),
        siderealAyanamsaToString(SiderealAyanamsa::TrueRevati),
        siderealAyanamsaToString(SiderealAyanamsa::PushyaPaksha),
    };
}

enum class Gender {
    Unspecified,
    Male,
    Female,
};

inline QString genderToString(Gender gender) {
    switch (gender) {
        case Gender::Male:
            return "Male";
        case Gender::Female:
            return "Female";
        case Gender::Unspecified:
        default:
            return "Unspecified";
    }
}

inline Gender genderFromString(const QString& text) {
    const QString normalized = text.trimmed();
    if (normalized.compare("Male", Qt::CaseInsensitive) == 0) {
        return Gender::Male;
    }
    if (normalized.compare("Female", Qt::CaseInsensitive) == 0) {
        return Gender::Female;
    }
    return Gender::Unspecified;
}

struct AspectOrbs {
    double conjunction = 10.0;
    double sextile = 10.0;
    double square = 10.0;
    double trine = 10.0;
    double opposition = 10.0;
};

inline AspectOrbs defaultAspectOrbs() {
    return AspectOrbs{};
}

struct NatalInput {
    QString name;
    QDate date;
    QTime time;
    QString timezone;
    ZodiacSystem zodiacSystem = ZodiacSystem::Tropical;
    SiderealAyanamsa siderealAyanamsa = SiderealAyanamsa::Lahiri;
    LunarNodePolicy lunarNodePolicy;
    // When true, the resolved policy comes from application preferences. Saved
    // legacy profiles set this false and retain their historical mean-node data.
    bool useDefaultLunarNodePolicy = true;
    Gender gender = Gender::Unspecified;
    QStringList fixedStars;
    double latitude = 0.0;
    double longitude = 0.0;
    HouseSystem houseSystem = HouseSystem::WholeSign;
    AspectOrbs aspectOrbs = defaultAspectOrbs();
};

struct BodyPosition {
    QString name;
    double longitude = 0.0;   // 0..360
    int signIndex = -1;       // 0..11
    QString signName;
    double degInSign = 0.0;
    int house = 0;            // 1..12
    bool retrograde = false;
    double speed = 0.0;       // deg/day (signed); valid only when hasSpeed
    bool hasSpeed = false;
    QString element;
    QString mode;
    QString dignity;
    bool isLunarNode = false;
    bool isNorthLunarNode = false;
    LunarNodeType lunarNodeType = LunarNodeType::Mean;
};

struct AnglePositions {
    double asc = 0.0;
    double mc = 0.0;
    double desc = 0.0;
    double ic = 0.0;
    double vertex = 0.0;
};

struct HouseCusp {
    int number = 0;
    double longitude = 0.0;
    QString signName;
};

struct FixedStarPosition {
    QString name;
    double longitude = 0.0;   // 0..360
    int signIndex = -1;       // 0..11
    QString signName;
    double degInSign = 0.0;
    int house = 0;            // 1..12
    double magnitude = 0.0;
};

struct AspectGrid {
    QVector<QString> bodyOrder;
    QVector<QString> bodyAbbrev;
    QVector<QString> bodyGlyphs;
    struct Cell {
        QString symbol;
        QString label;
        double orb = 0.0;
        double maxOrb = 0.0;
        bool hasAspect = false;
        bool applying = false;   // valid only when hasMotion
        bool hasMotion = false;  // true when both bodies have ephemeris speed
    };
    QVector<QVector<Cell>> cells;
};

struct NatalChart {
    QDateTime localDateTime;
    QDateTime utcDateTime;
    QString timezoneLabel;
    ZodiacSystem zodiacSystem = ZodiacSystem::Tropical;
    SiderealAyanamsa siderealAyanamsa = SiderealAyanamsa::Lahiri;
    LunarNodePolicy lunarNodePolicy;
    AnglePositions angles;
    QVector<BodyPosition> bodies;
    QVector<FixedStarPosition> fixedStars;
    QVector<HouseCusp> cusps;
    QStringList warnings;
    bool isDayChart = false;
    double partOfFortune = 0.0;
    bool hasPartOfFortune = false;
    AspectGrid aspects;
};

}  // namespace dracoved
