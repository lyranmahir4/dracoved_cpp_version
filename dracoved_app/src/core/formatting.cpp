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
        QString::fromUtf8(u8"♈"),
        QString::fromUtf8(u8"♉"),
        QString::fromUtf8(u8"♊"),
        QString::fromUtf8(u8"♋"),
        QString::fromUtf8(u8"♌"),
        QString::fromUtf8(u8"♍"),
        QString::fromUtf8(u8"♎"),
        QString::fromUtf8(u8"♏"),
        QString::fromUtf8(u8"♐"),
        QString::fromUtf8(u8"♑"),
        QString::fromUtf8(u8"♒"),
        QString::fromUtf8(u8"♓"),
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
    const double deg_val = degInSign(longitude);
    int whole_deg = static_cast<int>(deg_val);
    double minutes_full = (deg_val - whole_deg) * 60.0;
    int minutes = static_cast<int>(minutes_full);
    double seconds = (minutes_full - minutes) * 60.0;

    if (seconds >= 60.0) {
        seconds -= 60.0;
        minutes += 1;
    }
    if (minutes >= 60) {
        minutes -= 60;
        whole_deg += 1;
    }
    if (whole_deg >= 30) {
        whole_deg -= 30;
    }

    return QString("%1 %2 %3' %4\"")
        .arg(QString::number(whole_deg).rightJustified(2, '0'))
        .arg(signName(signIndex(longitude)))
        .arg(QString::number(minutes).rightJustified(2, '0'))
        .arg(QString::number(seconds, 'f', 2).rightJustified(5, '0'));
}

QString formatDegOnly(double longitude) {
    if (std::isnan(longitude)) {
        return "N/A";
    }
    const double deg_val = degInSign(longitude);
    int whole_deg = static_cast<int>(deg_val);
    double minutes_full = (deg_val - whole_deg) * 60.0;
    int minutes = static_cast<int>(minutes_full);
    double seconds = (minutes_full - minutes) * 60.0;

    if (seconds >= 60.0) {
        seconds -= 60.0;
        minutes += 1;
    }
    if (minutes >= 60) {
        minutes -= 60;
        whole_deg += 1;
    }
    if (whole_deg >= 30) {
        whole_deg -= 30;
    }

    return QString("%1 %2' %3\"")
        .arg(QString::number(whole_deg).rightJustified(2, '0'))
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

int elementIndexForSign(int signIndex) {
    // Fire = 0 (Aries, Leo, Sagittarius: indices 0, 4, 8)
    // Earth = 1 (Taurus, Virgo, Capricorn: indices 1, 5, 9)
    // Air = 2 (Gemini, Libra, Aquarius: indices 2, 6, 10)
    // Water = 3 (Cancer, Scorpio, Pisces: indices 3, 7, 11)
    if (signIndex < 0 || signIndex >= 12) {
        return 0;
    }
    return signIndex % 4;
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
        "Uranus", "Neptune", "Pluto", "Chiron", "North Node", "South Node",
        "Lilith", "Part of Fortune", "Vertex",
        "Ascendant", "Midheaven", "Descendant", "IC",
    };
}

QStringList tropicalBodyAbbrev() {
    return {
        "Su", "Mo", "Me", "Ve", "Ma", "Ju", "Sa",
        "Ur", "Ne", "Pl", "Ch", "NN", "SN",
        "Li", "PF", "Vx",
        "AS", "MC", "DS", "IC",
    };
}

QStringList tropicalBodyGlyphs() {
    return {
        QString::fromUtf8(u8"☉"),
        QString::fromUtf8(u8"☾"),
        QString::fromUtf8(u8"☿"),
        QString::fromUtf8(u8"♀"),
        QString::fromUtf8(u8"♂"),
        QString::fromUtf8(u8"♃"),
        QString::fromUtf8(u8"♄"),
        QString::fromUtf8(u8"♅"),
        QString::fromUtf8(u8"♆"),
        QString::fromUtf8(u8"♇"),
        QString::fromUtf8(u8"⚷"),
        QString::fromUtf8(u8"☊"),
        QString::fromUtf8(u8"☋"),
        QString::fromUtf8(u8"⚸"), // Lilith
        QString::fromUtf8(u8"⊗"), // Part of Fortune
        "Vx",                     // Vertex
        "AC",
        "MC",
        "DC",
        "IC",
    };
}

QString bodyGlyph(const QString& name) {
    if (name == "Sun") return QString::fromUtf8(u8"☉");
    if (name == "Moon") return QString::fromUtf8(u8"☾");
    if (name == "Mercury") return QString::fromUtf8(u8"☿");
    if (name == "Venus") return QString::fromUtf8(u8"♀");
    if (name == "Mars") return QString::fromUtf8(u8"♂");
    if (name == "Jupiter") return QString::fromUtf8(u8"♃");
    if (name == "Saturn") return QString::fromUtf8(u8"♄");
    if (name == "Uranus") return QString::fromUtf8(u8"♅");
    if (name == "Neptune") return QString::fromUtf8(u8"♆");
    if (name == "Pluto") return QString::fromUtf8(u8"♇");
    if (name == "Chiron") return QString::fromUtf8(u8"⚷");
    if (name == "North Node") return QString::fromUtf8(u8"☊");
    if (name == "South Node") return QString::fromUtf8(u8"☋");
    if (name == "Lilith") return QString::fromUtf8(u8"⚸");
    if (name == "Part of Fortune") return QString::fromUtf8(u8"⊗");
    if (name == "Vertex") return "Vx";
    if (name == "Ascendant") return "AC";
    if (name == "Midheaven") return "MC";
    if (name == "Descendant") return "DC";
    if (name == "IC") return "IC";
    return "?";
}

}  // namespace dracoved
