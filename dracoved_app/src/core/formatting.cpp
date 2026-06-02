#include "formatting.h"

#include <QMap>
#include <algorithm>
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

QStringList arabicLotOrder() {
    return {
        "Part of Fortune",
        "Lot of Spirit",
        "Lot of Eros",
        "Lot of Basis",
        "Lot of Necessity",
        "Lot of Courage",
        "Lot of Victory",
        "Lot of Nemesis",
        "Lot of Life",
        "Lot of Releaser",
        "Lot of Origins",
        "Lot of Assets",
        "Lot of Brothers",
        "Lot of Brothers (Sahl Variant)",
        "Lot of Death of Brothers",
        "Lot of Father",
        "Lot of Death of Father",
        "Lot of Grandfathers",
        "Lot of Real Estate",
        "Lot of Cultivation",
        "Lot of End of Matters",
        "Lot of Children",
        "Lot of Child Timing",
        "Lot of Male Children",
        "Lot of Female Children",
        "Lot of Child's Sex",
        "Lot of Delight",
        "Lot of Chronic Illness",
        "Lot of Slaves",
        "Lot of Slaves Variant",
        "Lot of Marriage",
        "Lot of Marriage (Men Hermes)",
        "Lot of Marriage (Men Valens)",
        "Lot of Marriage (Women Hermes)",
        "Lot of Marriage (Women Valens)",
        "Lot of Marriage (Time)",
        "Lot of Delight and Pleasure",
        "Lot of Death",
        "Lot of Killing Planet",
        "Lot of Suspected Year",
        "Lot of Oppressive Place",
        "Lot of Travel",
        "Lot of Navigation",
        "Lot of Intellect",
        "Lot of Wisdom",
        "Lot of Rumors",
        "Lot of Rumors (Night Reversed)",
        "Lot of Religion",
        "Lot of Nobility",
        "Lot of Kingdom and Authority",
        "Lot of Power",
        "Lot of Authority and Work",
        "Lot of Action",
        "Lot of Mother",
        "Lot of Job and Authority",
        "Lot of Cause of Kingdom",
        "Lot of Hope",
        "Lot of Friends",
        "Lot of Friends (Night Reversed)",
        "Lot of Friends and Enemies",
        "Lot of Enemies",
        "Lot of Enemies (Hermes)",
        "Lot of Enemies (Sahl 1)",
        "Lot of Enemies (Sahl 2)",
        "Lot of Knowledge",
        "Lot of War",
        "Lot of Peace Among Soldiers",
        "Lot of Revolution Year",
        "Lot of Food/Wheat",
        "Lot of Water",
        "Lot of Barley",
        "Lot of Chickpeas",
        "Lot of Lentils",
        "Lot of Egyptian Beans",
        "Lot of Indian Peas",
        "Lot of Dates",
        "Lot of Honey",
        "Lot of Rice",
        "Lot of Olives",
        "Lot of Grapes",
        "Lot of Cotton",
        "Lot of Sesame (Jupiter-Saturn)",
        "Lot of Sesame (Venus-Saturn)",
        "Lot of Watermelons",
        "Lot of Acidic Foods",
        "Lot of Sweet Foods",
        "Lot of Pungent Foods",
        "Lot of Bitter Foods",
        "Lot of Purgative Sweet Medicines",
        "Lot of Purgative Acidic Medicines",
        "Lot of Purgative Salty Medicines",
        "Lot of Poisons",
        "Lot of Duration of Kingdom",
    };
}

bool isArabicLotName(const QString& name) {
    static const QStringList kLots = arabicLotOrder();
    return kLots.contains(name);
}

QString lotAbbrev(const QString& name) {
    if (name == "Part of Fortune") {
        return "PF";
    }
    QString phrase = name;
    if (phrase.startsWith("Lot of ")) {
        phrase = phrase.mid(7);
    }

    QString cleaned;
    cleaned.reserve(phrase.size());
    for (const QChar ch : phrase) {
        cleaned.append(ch.isLetterOrNumber() ? ch : QChar(' '));
    }
    QStringList words = cleaned.split(' ', Qt::SkipEmptyParts);
    words.erase(std::remove_if(words.begin(), words.end(), [](const QString& word) {
        return word.compare("and", Qt::CaseInsensitive) == 0
            || word.compare("of", Qt::CaseInsensitive) == 0
            || word.compare("the", Qt::CaseInsensitive) == 0;
    }), words.end());

    QString abbr = "L";
    if (words.isEmpty()) {
        return abbr;
    }
    if (words.size() == 1) {
        abbr += words[0].left(3);
    } else {
        for (const auto& word : words) {
            abbr += word.left(1);
            if (abbr.size() >= 5) {
                break;
            }
        }
    }
    return abbr.left(5);
}

QString bodyLabel(const QString& name, bool glyph) {
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
    if (name == "Chiron") return glyph ? QString(QChar(0x26B7)) : "Ch";
    if (name == "Ceres") return glyph ? QString(QChar(0x26B3)) : "Ce";
    if (name == "Pallas") return glyph ? QString(QChar(0x26B4)) : "Pa";
    if (name == "Juno") return glyph ? QString(QChar(0x26B5)) : "Jn";
    if (name == "Vesta") return glyph ? QString(QChar(0x26B6)) : "Vs";
    if (name == "Pholus") return "Ph";
    if (name == "North Node") return glyph ? QString(QChar(0x260A)) : "NN";
    if (name == "South Node") return glyph ? QString(QChar(0x260B)) : "SN";
    if (name == "Lilith") return glyph ? QString(QChar(0x26B8)) : "Li";
    if (name == "Part of Fortune") return glyph ? QString(QChar(0x2297)) : "PF";
    if (isArabicLotName(name)) return lotAbbrev(name);
    if (name == "Vertex") return "Vx";
    if (name == "Ascendant") return glyph ? "AC" : "AS";
    if (name == "Midheaven") return "MC";
    if (name == "Descendant") return glyph ? "DC" : "DS";
    if (name == "IC") return "IC";
    return "?";
}

QStringList tropicalBodyOrder() {
    QStringList order = {
        "Sun", "Moon", "Mercury", "Venus", "Mars", "Jupiter", "Saturn",
        "Uranus", "Neptune", "Pluto", "Chiron", "Ceres", "Pallas", "Juno", "Vesta", "Pholus",
        "North Node", "South Node", "Lilith",
    };
    order.append(arabicLotOrder());
    order << "Vertex" << "Ascendant" << "Midheaven" << "Descendant" << "IC";
    return order;
}

QStringList tropicalBodyAbbrev() {
    QStringList labels;
    const QStringList order = tropicalBodyOrder();
    labels.reserve(order.size());
    for (const auto& name : order) {
        labels.push_back(bodyLabel(name, false));
    }
    return labels;
}

QStringList tropicalBodyGlyphs() {
    QStringList glyphs;
    const QStringList order = tropicalBodyOrder();
    glyphs.reserve(order.size());
    for (const auto& name : order) {
        glyphs.push_back(bodyLabel(name, true));
    }
    return glyphs;
}

QStringList asteroidBodyOrder() {
    return {"Chiron", "Ceres", "Pallas", "Juno", "Vesta", "Pholus"};
}

bool isAsteroidBody(const QString& name) {
    static const QStringList kAsteroids = asteroidBodyOrder();
    return kAsteroids.contains(name);
}

QString bodyGlyph(const QString& name) {
    return bodyLabel(name, true);
}

}  // namespace dracoved
