#include "formatting.h"

#include <QMap>
#include <cmath>

namespace dracoved {

QStringList zodiacSigns() {
    return {
        "Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo",
        "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces",
    };
}

QStringList zodiacSignGlyphs() {
    return {
        QString(QChar(0x2648)),
        QString(QChar(0x2649)),
        QString(QChar(0x264A)),
        QString(QChar(0x264B)),
        QString(QChar(0x264C)),
        QString(QChar(0x264D)),
        QString(QChar(0x264E)),
        QString(QChar(0x264F)),
        QString(QChar(0x2650)),
        QString(QChar(0x2651)),
        QString(QChar(0x2652)),
        QString(QChar(0x2653)),
    };
}

QString signName(int index) {
    const auto signs = zodiacSigns();
    if (index < 0 || index >= signs.size()) {
        return "N/A";
    }
    return signs[index];
}

double normalizeDegrees(double deg) {
    double v = deg;
    while (v < 0.0) {
        v += 360.0;
    }
    while (v >= 360.0) {
        v -= 360.0;
    }
    return v;
}

int signIndex(double longitude) {
    double norm = normalizeDegrees(longitude);
    return static_cast<int>(norm / 30.0);
}

double degInSign(double longitude) {
    double norm = normalizeDegrees(longitude);
    return std::fmod(norm, 30.0);
}

QString formatDegInSign(double longitude) {
    if (std::isnan(longitude)) {
        return "N/A";
    }
    const double degVal = degInSign(longitude);
    int wholeDeg = static_cast<int>(degVal);
    double minutesFull = (degVal - wholeDeg) * 60.0;
    int minutes = static_cast<int>(minutesFull);
    double seconds = (minutesFull - minutes) * 60.0;

    if (seconds >= 60.0) {
        seconds -= 60.0;
        minutes += 1;
    }
    if (minutes >= 60) {
        minutes -= 60;
        wholeDeg += 1;
    }
    if (wholeDeg >= 30) {
        wholeDeg -= 30;
    }

    return QString("%1 %2 %3' %4\"")
        .arg(QString::number(wholeDeg).rightJustified(2, '0'))
        .arg(signName(signIndex(longitude)))
        .arg(QString::number(minutes).rightJustified(2, '0'))
        .arg(QString::number(seconds, 'f', 2).rightJustified(5, '0'));
}

QString formatDegOnly(double longitude) {
    if (std::isnan(longitude)) {
        return "N/A";
    }
    const double degVal = degInSign(longitude);
    int wholeDeg = static_cast<int>(degVal);
    double minutesFull = (degVal - wholeDeg) * 60.0;
    int minutes = static_cast<int>(minutesFull);
    double seconds = (minutesFull - minutes) * 60.0;

    if (seconds >= 60.0) {
        seconds -= 60.0;
        minutes += 1;
    }
    if (minutes >= 60) {
        minutes -= 60;
        wholeDeg += 1;
    }
    if (wholeDeg >= 30) {
        wholeDeg -= 30;
    }

    return QString("%1 %2' %3\"")
        .arg(QString::number(wholeDeg).rightJustified(2, '0'))
        .arg(QString::number(minutes).rightJustified(2, '0'))
        .arg(QString::number(seconds, 'f', 2).rightJustified(5, '0'));
}

QString elementForSign(const QString& sign) {
    static const QMap<QString, QString> elements = {
        {"Aries", "Fire"}, {"Leo", "Fire"}, {"Sagittarius", "Fire"},
        {"Taurus", "Earth"}, {"Virgo", "Earth"}, {"Capricorn", "Earth"},
        {"Gemini", "Air"}, {"Libra", "Air"}, {"Aquarius", "Air"},
        {"Cancer", "Water"}, {"Scorpio", "Water"}, {"Pisces", "Water"},
    };
    return elements.value(sign, "-");
}

int elementIndexForSign(int idx) {
    if (idx < 0 || idx >= 12) {
        return 0;
    }
    return idx % 4;
}

QString modeForSign(const QString& sign) {
    static const QMap<QString, QString> modes = {
        {"Aries", "Cardinal"}, {"Cancer", "Cardinal"}, {"Libra", "Cardinal"}, {"Capricorn", "Cardinal"},
        {"Taurus", "Fixed"}, {"Leo", "Fixed"}, {"Scorpio", "Fixed"}, {"Aquarius", "Fixed"},
        {"Gemini", "Mutable"}, {"Virgo", "Mutable"}, {"Sagittarius", "Mutable"}, {"Pisces", "Mutable"},
    };
    return modes.value(sign, "-");
}

QString dignityLabel(const QString& planet, const QString& sign) {
    if (planet.isEmpty() || sign.isEmpty()) {
        return "-";
    }
    static const QMap<QString, QString> signLords = {
        {"Aries", "Mars"},
        {"Taurus", "Venus"},
        {"Gemini", "Mercury"},
        {"Cancer", "Moon"},
        {"Leo", "Sun"},
        {"Virgo", "Mercury"},
        {"Libra", "Venus"},
        {"Scorpio", "Mars"},
        {"Sagittarius", "Jupiter"},
        {"Capricorn", "Saturn"},
        {"Aquarius", "Saturn"},
        {"Pisces", "Jupiter"},
    };
    static const QMap<QString, QString> exaltations = {
        {"Sun", "Aries"},
        {"Moon", "Taurus"},
        {"Mars", "Capricorn"},
        {"Mercury", "Virgo"},
        {"Jupiter", "Cancer"},
        {"Venus", "Pisces"},
        {"Saturn", "Libra"},
        {"Rahu", "Taurus"},
        {"Ketu", "Scorpio"},
    };

    QString planetKey = planet;
    if (planet == "North Node") {
        planetKey = "Rahu";
    } else if (planet == "South Node") {
        planetKey = "Ketu";
    }

    auto signs = zodiacSigns();
    int idx = signs.indexOf(sign);
    QString oppSign = (idx >= 0) ? signs[(idx + 6) % 12] : QString();
    QString ruler = signLords.value(sign);
    QString detriment = signLords.value(oppSign);
    QString exaltSign = exaltations.value(planetKey);
    QString fallSign;
    if (!exaltSign.isEmpty()) {
        int exaltIdx = signs.indexOf(exaltSign);
        if (exaltIdx >= 0) {
            fallSign = signs[(exaltIdx + 6) % 12];
        }
    }

    if (ruler == planetKey) {
        return "Ruler";
    }
    if (detriment == planetKey) {
        return "Detriment";
    }
    if (!exaltSign.isEmpty() && exaltSign == sign) {
        return "Exalt";
    }
    if (!fallSign.isEmpty() && fallSign == sign) {
        return "Fall";
    }
    return "-";
}

QStringList tropicalBodyOrder() {
    return {
        "Sun", "Moon", "Mercury", "Venus", "Mars", "Jupiter", "Saturn",
        "Uranus", "Neptune", "Pluto", "Chiron", "Ceres", "Pallas", "Juno", "Vesta", "Pholus",
        "North Node", "South Node", "Lilith", "Part of Fortune", "Vertex",
        "Ascendant", "Midheaven", "Descendant", "IC",
    };
}

QStringList tropicalBodyAbbrev() {
    return {
        "Su", "Mo", "Me", "Ve", "Ma", "Ju", "Sa",
        "Ur", "Ne", "Pl", "Ch", "Ce", "Pa", "Jn", "Vs", "Ph", "NN", "SN",
        "Li", "PF", "Vx",
        "AS", "MC", "DS", "IC",
    };
}

QStringList tropicalBodyGlyphs() {
    return {
        QString(QChar(0x2609)), // Sun
        QString(QChar(0x263E)), // Moon
        QString(QChar(0x263F)), // Mercury
        QString(QChar(0x2640)), // Venus
        QString(QChar(0x2642)), // Mars
        QString(QChar(0x2643)), // Jupiter
        QString(QChar(0x2644)), // Saturn
        QString(QChar(0x26E2)), // Uranus
        QString(QChar(0x2646)), // Neptune
        QString(QChar(0x2647)), // Pluto
        QString(QChar(0x26B7)), // Chiron
        QString(QChar(0x26B3)), // Ceres
        QString(QChar(0x26B4)), // Pallas
        QString(QChar(0x26B5)), // Juno
        QString(QChar(0x26B6)), // Vesta
        "Ph",                   // Pholus
        QString(QChar(0x260A)), // North Node
        QString(QChar(0x260B)), // South Node
        QString(QChar(0x26B8)), // Lilith
        QString(QChar(0x2297)), // Part of Fortune
        "Vx",                   // Vertex
        "AC",
        "MC",
        "DC",
        "IC",
    };
}

QStringList asteroidBodyOrder() {
    return {"Chiron", "Ceres", "Pallas", "Juno", "Vesta", "Pholus"};
}

bool isAsteroidBody(const QString& name) {
    static const QStringList kAsteroids = asteroidBodyOrder();
    return kAsteroids.contains(name);
}

QString bodyGlyph(const QString& name) {
    if (name == "Sun") return QString(QChar(0x2609));
    if (name == "Moon") return QString(QChar(0x263E));
    if (name == "Mercury") return QString(QChar(0x263F));
    if (name == "Venus") return QString(QChar(0x2640));
    if (name == "Mars") return QString(QChar(0x2642));
    if (name == "Jupiter") return QString(QChar(0x2643));
    if (name == "Saturn") return QString(QChar(0x2644));
    if (name == "Uranus") return QString(QChar(0x26E2));
    if (name == "Neptune") return QString(QChar(0x2646));
    if (name == "Pluto") return QString(QChar(0x2647));
    if (name == "Chiron") return QString(QChar(0x26B7));
    if (name == "Ceres") return QString(QChar(0x26B3));
    if (name == "Pallas") return QString(QChar(0x26B4));
    if (name == "Juno") return QString(QChar(0x26B5));
    if (name == "Vesta") return QString(QChar(0x26B6));
    if (name == "Pholus") return "Ph";
    if (name == "North Node") return QString(QChar(0x260A));
    if (name == "South Node") return QString(QChar(0x260B));
    if (name == "Lilith") return QString(QChar(0x26B8));
    if (name == "Part of Fortune") return QString(QChar(0x2297));
    if (name == "Vertex") return "Vx";
    if (name == "Ascendant") return "AC";
    if (name == "Midheaven") return "MC";
    if (name == "Descendant") return "DC";
    if (name == "IC") return "IC";
    return "?";
}

}  // namespace dracoved
