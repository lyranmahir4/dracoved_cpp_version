#include "formatting.h"
#include "chart_types.h"

#include <QHash>
#include <QMap>
#include <QSet>
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
    if (isNorthLunarNodeName(planet)) {
        planetKey = "Rahu";
    } else if (isLunarNodeName(planet)) {
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
    if (isNorthLunarNodeName(name)) {
        if (glyph) return QString(QChar(0x260A));
        if (name.startsWith("Mean ")) return "mNN";
        if (name.startsWith("True ")) return "tNN";
        return "NN";
    }
    if (isLunarNodeName(name)) {
        if (glyph) return QString(QChar(0x260B));
        if (name.startsWith("Mean ")) return "mSN";
        if (name.startsWith("True ")) return "tSN";
        return "SN";
    }
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

QStringList bodyOrderForLunarNodePolicy(const LunarNodePolicy& policy) {
    QStringList order = tropicalBodyOrder();
    if (policy.mode != LunarNodeMode::Both) {
        return order;
    }
    const LunarNodeType secondary = effectivePrimaryNodeType(policy) == LunarNodeType::Mean
        ? LunarNodeType::True
        : LunarNodeType::Mean;
    const int southIndex = order.indexOf("South Node");
    const int insertionIndex = southIndex >= 0 ? southIndex + 1 : order.size();
    order.insert(insertionIndex, explicitLunarNodeName(false, secondary));
    order.insert(insertionIndex, explicitLunarNodeName(true, secondary));
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

QStringList chartToggleableBodyOrder() {
    QStringList order = {
        "Sun", "Moon", "Mercury", "Venus", "Mars", "Jupiter", "Saturn",
        "Uranus", "Neptune", "Pluto",
        "North Node", "South Node", "Lilith", "Vertex",
    };
    order.append(asteroidBodyOrder());
    return order;
}

QString chartBodyVisibilityKey(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    if (isLunarNodeName(trimmed)) {
        return isNorthLunarNodeName(trimmed) ? QString("North Node") : QString("South Node");
    }
    return trimmed;
}

QStringList chartBodyPresetVisibleBodies(ChartBodyPreset preset) {
    static const QStringList classicalPlanets = {
        "Sun", "Moon", "Mercury", "Venus", "Mars", "Jupiter", "Saturn",
    };
    static const QStringList outerPlanets = {"Uranus", "Neptune", "Pluto"};
    static const QStringList nodes = {"North Node", "South Node"};

    switch (preset) {
        case ChartBodyPreset::AllBodies:
            return chartToggleableBodyOrder();
        case ChartBodyPreset::Classical: {
            QStringList bodies = classicalPlanets;
            bodies.append(nodes);
            return bodies;
        }
        case ChartBodyPreset::Modern: {
            QStringList bodies = classicalPlanets;
            bodies.append(outerPlanets);
            bodies.append(nodes);
            return bodies;
        }
        case ChartBodyPreset::MainPlanetsOnly: {
            QStringList bodies = classicalPlanets;
            bodies.append(outerPlanets);
            return bodies;
        }
        case ChartBodyPreset::Custom:
        default:
            return {};
    }
}

ChartBodyPreset chartBodyPresetForVisibleBodies(const QStringList& visibleBodies) {
    QSet<QString> current;
    for (const auto& name : visibleBodies) {
        const QString key = chartBodyVisibilityKey(name);
        if (!key.isEmpty()) {
            current.insert(key);
        }
    }

    const ChartBodyPreset candidates[] = {
        ChartBodyPreset::AllBodies,
        ChartBodyPreset::Classical,
        ChartBodyPreset::Modern,
        ChartBodyPreset::MainPlanetsOnly,
    };
    for (const ChartBodyPreset preset : candidates) {
        QSet<QString> expected;
        for (const auto& name : chartBodyPresetVisibleBodies(preset)) {
            expected.insert(name);
        }
        if (expected == current) {
            return preset;
        }
    }
    return ChartBodyPreset::Custom;
}

QString chartBodyPresetLabel(ChartBodyPreset preset) {
    switch (preset) {
        case ChartBodyPreset::AllBodies:
            return "All Bodies";
        case ChartBodyPreset::Classical:
            return "Classical (7 Planets + Nodes)";
        case ChartBodyPreset::Modern:
            return "Modern (10 Planets + Nodes)";
        case ChartBodyPreset::MainPlanetsOnly:
            return "Main Planets Only (no Nodes)";
        case ChartBodyPreset::Custom:
        default:
            return "Custom";
    }
}

QString bodySvgResourcePath(const QString& name) {
    static const QHash<QString, QString> paths = {
        {"Sun", ":/resources/icons/planets/sun.svg"},
        {"Moon", ":/resources/icons/planets/moon.svg"},
        {"Mercury", ":/resources/icons/planets/mercury.svg"},
        {"Venus", ":/resources/icons/planets/venus.svg"},
        {"Mars", ":/resources/icons/planets/mars.svg"},
        {"Jupiter", ":/resources/icons/planets/jupiter.svg"},
        {"Saturn", ":/resources/icons/planets/saturn.svg"},
        {"Uranus", ":/resources/icons/planets/uranus.svg"},
        {"Neptune", ":/resources/icons/planets/neptune.svg"},
        {"Pluto", ":/resources/icons/planets/pluto.svg"},
        {"Chiron", ":/resources/icons/planets/chiron.svg"},
        {"North Node", ":/resources/icons/planets/north_node.svg"},
        {"South Node", ":/resources/icons/planets/south_node.svg"},
        {"Lilith", ":/resources/icons/planets/lilith.svg"},
    };
    const QString lookupName = isLunarNodeName(name)
        ? (isNorthLunarNodeName(name) ? QString("North Node") : QString("South Node"))
        : name;
    return paths.value(lookupName);
}

}  // namespace dracoved
