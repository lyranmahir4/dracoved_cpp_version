#include "chart_profile_store.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTime>

namespace dracoved::profilestore {

QString profilesDir() {
    QDir base(QCoreApplication::applicationDirPath());
    const QString dirPath = base.absoluteFilePath("profiles");
    if (!QDir(dirPath).exists()) {
        QDir().mkpath(dirPath);
    }
    return dirPath;
}

QString sanitizeProfileName(const QString& name) {
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    const QString invalid = "<>:\"/\\|?*";
    QString safe;
    safe.reserve(trimmed.size());
    for (QChar ch : trimmed) {
        safe.append(invalid.contains(ch) ? '_' : ch);
    }
    while (!safe.isEmpty() && (safe.endsWith(' ') || safe.endsWith('.'))) {
        safe.chop(1);
    }
    return safe.trimmed();
}

QString profileFilePath(const QString& name) {
    const QString safe = sanitizeProfileName(name);
    if (safe.isEmpty()) {
        return QString();
    }
    QDir dir(profilesDir());
    return dir.filePath(safe + ".json");
}

QStringList listProfiles() {
    QDir dir(profilesDir());
    const QStringList files = dir.entryList(QStringList() << "*.json", QDir::Files, QDir::Name);
    QStringList names;
    names.reserve(files.size());
    for (const auto& file : files) {
        names << QFileInfo(file).completeBaseName();
    }
    return names;
}

bool readProfileInput(const QString& profileName,
                      const LunarNodePolicy& appDefaultNodePolicy,
                      NatalInput* outInput,
                      QString* outLocation,
                      QString* outError) {
    auto fail = [outError](const QString& message) {
        if (outError) {
            *outError = message;
        }
        return false;
    };
    if (!outInput) {
        return fail("Internal error: no chart input to fill.");
    }

    const QString normalized = profileName.trimmed();
    if (normalized.isEmpty()) {
        return fail("Select a saved chart to load.");
    }

    const QString filePath = profileFilePath(normalized);
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        return fail("Saved chart file not found.");
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QString("Unable to load chart: %1").arg(file.errorString()));
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail("Saved chart file is not valid JSON.");
    }
    const QJsonObject obj = doc.object();

    NatalInput input;
    input.name = obj.value("name").toString();
    input.date = QDate::fromString(obj.value("date").toString(), Qt::ISODate);
    input.time = QTime::fromString(obj.value("time").toString(), "HH:mm:ss");
    if (!input.time.isValid()) {
        input.time = QTime::fromString(obj.value("time").toString(), "HH:mm");
    }
    input.timezone = obj.value("timezone").toString();
    input.zodiacSystem = zodiacSystemFromString(obj.value("zodiac_system").toString());
    input.siderealAyanamsa = siderealAyanamsaFromString(obj.value("sidereal_ayanamsa").toString());
    if (obj.contains("lunar_node_mode")) {
        input.lunarNodePolicy.mode = lunarNodeModeFromString(obj.value("lunar_node_mode").toString());
        input.lunarNodePolicy.primary = lunarNodeTypeFromString(obj.value("lunar_node_primary").toString());
        input.useDefaultLunarNodePolicy = obj.value("lunar_node_uses_app_default").toBool(false);
        if (input.useDefaultLunarNodePolicy) {
            input.lunarNodePolicy = appDefaultNodePolicy;
        } else if (input.lunarNodePolicy.mode == LunarNodeMode::MeanOnly) {
            input.lunarNodePolicy.primary = LunarNodeType::Mean;
        } else if (input.lunarNodePolicy.mode == LunarNodeMode::TrueOnly) {
            input.lunarNodePolicy.primary = LunarNodeType::True;
        }
    } else {
        // Version 3 and older had no selectable node model. The application
        // default remains Mean for compatibility, but once the user changes
        // that default these legacy charts should follow it instead of being
        // silently pinned to Mean forever.
        input.lunarNodePolicy = appDefaultNodePolicy;
        input.useDefaultLunarNodePolicy = true;
    }
    if (!input.date.isValid()) {
        return fail("Saved chart date is invalid.");
    }
    if (!input.time.isValid()) {
        return fail("Saved chart time is invalid.");
    }
    if (input.timezone.trimmed().isEmpty()) {
        input.timezone = "UTC";
    }
    input.gender = genderFromString(obj.value("gender").toString());
    input.latitude = obj.value("latitude").toDouble();
    input.longitude = obj.value("longitude").toDouble();
    const QString houseSystem = obj.value("house_system").toString();
    input.houseSystem = houseSystem.contains("Placidus", Qt::CaseInsensitive)
        ? HouseSystem::Placidus
        : HouseSystem::WholeSign;
    const QJsonArray fixedStarsJson = obj.value("fixed_stars").toArray();
    for (const auto& value : fixedStarsJson) {
        const QString starName = value.toString().trimmed();
        if (!starName.isEmpty()) {
            input.fixedStars.push_back(starName);
        }
    }

    *outInput = input;
    if (outLocation) {
        *outLocation = obj.value("location").toString();
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

bool writeProfileInput(const QString& profileName,
                       const NatalInput& input,
                       const QString& locationName,
                       QString* outError) {
    auto fail = [outError](const QString& message) {
        if (outError) {
            *outError = message;
        }
        return false;
    };

    const QString safeName = sanitizeProfileName(profileName);
    if (safeName.isEmpty()) {
        return fail("Chart name contains only invalid characters.");
    }
    const QString filePath = profileFilePath(safeName);
    if (filePath.isEmpty()) {
        return fail("Chart name contains only invalid characters.");
    }

    QJsonObject obj;
    obj["profile_name"] = safeName;
    obj["name"] = input.name;
    obj["date"] = input.date.toString(Qt::ISODate);
    obj["time"] = input.time.toString("HH:mm:ss");
    obj["timezone"] = input.timezone;
    obj["zodiac_system"] = zodiacSystemToString(input.zodiacSystem);
    obj["sidereal_ayanamsa"] = siderealAyanamsaToString(input.siderealAyanamsa);
    obj["lunar_node_mode"] = lunarNodeModeToString(input.lunarNodePolicy.mode);
    obj["lunar_node_primary"] =
        lunarNodeTypeToString(effectivePrimaryNodeType(input.lunarNodePolicy));
    obj["lunar_node_uses_app_default"] = input.useDefaultLunarNodePolicy;
    obj["gender"] = genderToString(input.gender);
    obj["location"] = locationName;
    obj["latitude"] = input.latitude;
    obj["longitude"] = input.longitude;
    obj["house_system"] =
        (input.houseSystem == HouseSystem::Placidus) ? "Placidus" : "Whole Sign";
    QJsonArray fixedStarsJson;
    for (const auto& starName : input.fixedStars) {
        fixedStarsJson.push_back(starName);
    }
    obj["fixed_stars"] = fixedStarsJson;
    obj["saved_at_utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    obj["version"] = 4;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(QString("Unable to save chart: %1").arg(file.errorString()));
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
    if (outError) {
        outError->clear();
    }
    return true;
}

}  // namespace dracoved::profilestore
