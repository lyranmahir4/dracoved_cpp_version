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
