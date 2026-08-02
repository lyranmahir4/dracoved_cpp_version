#include "return_finder_controller.h"

#include "../core/formatting.h"
#include "../core/timezone_utils.h"
#include "return_finder_worker.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimeZone>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <limits>

namespace dracoved {

QStringList finderPlanets() {
    return {
        "Sun", "Moon", "Mercury", "Venus", "Mars", "Jupiter", "Saturn",
        "Uranus", "Neptune", "Pluto", "North Node", "South Node",
        "Mean North Node", "Mean South Node", "True North Node", "True South Node", "Lilith",
    };
}

QStringList finderAngles() {
    return {"Ascendant", "Descendant", "Midheaven", "IC", "Any Angle"};
}

QString finderAngleDisplayName(const QString& angle) {
    if (angle == "Ascendant") return "ASC - Ascendant";
    if (angle == "Descendant") return "DSC - Descendant";
    if (angle == "Midheaven") return "MC - Midheaven";
    if (angle == "IC") return "IC - Imum Coeli";
    if (angle == "Any Angle") return "Any Angle (ASC / DSC / MC / IC)";
    return angle;
}

void fillFinderAngles(QComboBox* combo) {
    if (!combo) return;
    combo->clear();
    for (const QString& angle : finderAngles()) {
        combo->addItem(finderAngleDisplayName(angle), angle);
    }
}

class ReturnFinderNumberItem final : public QTableWidgetItem {
public:
    ReturnFinderNumberItem(const QString& text, double value)
        : QTableWidgetItem(text) {
        setData(Qt::UserRole + 1, value);
    }

    bool operator<(const QTableWidgetItem& other) const override {
        return data(Qt::UserRole + 1).toDouble()
            < other.data(Qt::UserRole + 1).toDouble();
    }
};

QVector<int> parseHouses(const QString& text) {
    QVector<int> houses;
    QSet<int> seen;
    const QStringList parts = text.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const int house = part.toInt(&ok);
        if (ok && house >= 1 && house <= 12 && !seen.contains(house)) {
            seen.insert(house);
            houses.push_back(house);
        }
    }
    return houses;
}

QString housesText(const QVector<int>& houses) {
    QStringList values;
    for (int house : houses) values.push_back(QString::number(house));
    return values.join(", ");
}

QString markdownTableCell(QString value) {
    value.replace("\\", "\\\\");
    value.replace("|", "\\|");
    value.replace("\r\n", "<br>");
    value.replace('\r', "<br>");
    value.replace('\n', "<br>");
    value = value.trimmed();
    return value.isEmpty() ? QString("N/A") : value;
}

QString reportHouseSystemLabel(HouseSystem system) {
    return system == HouseSystem::Placidus ? "Placidus" : "Whole Sign";
}

QString reportHouseModeLabel(ReturnFinderHouseMode mode) {
    switch (mode) {
        case ReturnFinderHouseMode::Placidus: return "Placidus";
        case ReturnFinderHouseMode::BothOr: return "Whole Sign or Placidus (OR)";
        case ReturnFinderHouseMode::BothAnd: return "Whole Sign and Placidus (AND)";
        case ReturnFinderHouseMode::WholeSign:
        default: return "Whole Sign";
    }
}

QString reportConditionTypeLabel(ReturnFinderConditionType type) {
    switch (type) {
        case ReturnFinderConditionType::Aspect: return "Aspect";
        case ReturnFinderConditionType::HouseLordPlacement: return "House Lord Placement";
        case ReturnFinderConditionType::ProfectionLordPlacement: return "Profection Lord Placement";
        case ReturnFinderConditionType::Stellium: return "Stellium";
        case ReturnFinderConditionType::PlanetPlacement:
        default: return "Planet Placement";
    }
}

QString reportScopeLabel(ReturnFinderScope scope) {
    return scope == ReturnFinderScope::Natal ? "Natal" : "Return";
}

QString reportHouseList(const QVector<int>& houses) {
    QStringList values;
    for (int house : houses) values.push_back(QString::number(house));
    return values.isEmpty() ? QString("unspecified") : values.join(" or ");
}

QString reportPlacementLabel(ReturnFinderPlacementKind kind, int sign, int house) {
    if (kind == ReturnFinderPlacementKind::Sign) {
        const QStringList signs = zodiacSigns();
        return signs.value(std::clamp(sign, 0, 11), "Unknown sign");
    }
    return QString("House %1").arg(std::clamp(house, 1, 12));
}

QString reportTargetLabel(const ReturnFinderTarget& target) {
    const QString scope = reportScopeLabel(target.scope);
    switch (target.kind) {
        case ReturnFinderTargetKind::Angle:
            return QString("%1 %2").arg(scope, target.name);
        case ReturnFinderTargetKind::HouseLord:
            return QString("%1 lord of House %2 (%3 selected lord)")
                .arg(scope, reportHouseList(target.houses),
                     target.houseLordMatch == ReturnFinderHouseLordMatch::All ? "all" : "any");
        case ReturnFinderTargetKind::ProfectionLord:
            return QString("%1 position of the annual profection lord").arg(scope);
        case ReturnFinderTargetKind::Planet:
        default:
            return QString("%1 %2").arg(scope, target.name);
    }
}

QString reportConditionDescription(const ReturnFinderCondition& condition) {
    switch (condition.type) {
        case ReturnFinderConditionType::Aspect:
            return QString("%1 %2 %3; maximum orb %4 deg")
                .arg(reportTargetLabel(condition.subject),
                     returnFinderAspectLabel(condition.aspect).toLower(),
                     reportTargetLabel(condition.target))
                .arg(condition.orb, 0, 'f', 2);
        case ReturnFinderConditionType::HouseLordPlacement:
            return QString("%1 house lord(s) of House %2 placed in Return %3; require %4 selected lord(s)")
                .arg(reportScopeLabel(condition.houseLordScope),
                     reportHouseList(condition.houseLordHouses),
                     reportPlacementLabel(condition.placementKind,
                                          condition.targetSign, condition.targetHouse),
                     condition.houseLordMatch == ReturnFinderHouseLordMatch::All ? "all" : "any");
        case ReturnFinderConditionType::ProfectionLordPlacement:
            return QString("Annual profection lord placed in Return %1")
                .arg(reportPlacementLabel(condition.placementKind,
                                          condition.targetSign, condition.targetHouse));
        case ReturnFinderConditionType::Stellium: {
            const QString target = condition.stelliumAny
                ? QString("any %1").arg(condition.stelliumBySign ? "sign" : "house")
                : reportPlacementLabel(condition.stelliumBySign
                                           ? ReturnFinderPlacementKind::Sign
                                           : ReturnFinderPlacementKind::House,
                                       condition.stelliumTarget, condition.stelliumTarget);
            return QString("At least %1 bodies from the 10-planet Sun-through-Pluto set in %2")
                .arg(condition.stelliumMinimum).arg(target);
        }
        case ReturnFinderConditionType::PlanetPlacement:
        default: {
            QStringList requirements;
            if (condition.planetPlacementEnabled) {
                requirements.push_back(QString("placed in %1")
                    .arg(reportPlacementLabel(condition.placementKind,
                                              condition.targetSign, condition.targetHouse)));
            }
            if (condition.planetAngleContactEnabled) {
                requirements.push_back(QString("conjunct %1; maximum orb %2 deg")
                    .arg(reportTargetLabel(condition.target))
                    .arg(condition.orb, 0, 'f', 2));
            }
            return QString("Return %1 %2")
                .arg(condition.subject.name,
                     requirements.isEmpty() ? QString("has no active constraint")
                                            : requirements.join(" and "));
        }
    }
}

void refillPlacementValues(QComboBox* combo, ReturnFinderPlacementKind kind, int selected) {
    if (!combo) return;
    const QSignalBlocker blocker(combo);
    combo->clear();
    if (kind == ReturnFinderPlacementKind::Sign) {
        const QStringList signs = zodiacSigns();
        for (int i = 0; i < signs.size(); ++i) combo->addItem(signs[i], i);
        combo->setCurrentIndex(std::clamp(selected, 0, 11));
    } else {
        for (int house = 1; house <= 12; ++house) combo->addItem(QString("House %1").arg(house), house);
        const int index = combo->findData(std::clamp(selected, 1, 12));
        combo->setCurrentIndex(std::max(0, index));
    }
}

QString sanitizedPresetName(QString name) {
    name = name.trimmed();
    name.replace(QRegularExpression("[<>:\"/\\\\|?*]+"), "_");
    name.replace(QRegularExpression("\\s+"), " ");
    while (name.endsWith('.')) name.chop(1);
    return name.left(100).trimmed();
}

QJsonArray housesToJson(const QVector<int>& houses) {
    QJsonArray array;
    for (int house : houses) array.push_back(house);
    return array;
}

QVector<int> housesFromJson(const QJsonArray& array) {
    QVector<int> houses;
    for (const auto& value : array) {
        const int house = value.toInt();
        if (house >= 1 && house <= 12 && !houses.contains(house)) houses.push_back(house);
    }
    return houses.isEmpty() ? QVector<int>{1} : houses;
}

QJsonObject targetToJson(const ReturnFinderTarget& target) {
    QJsonObject object;
    object["scope"] = static_cast<int>(target.scope);
    object["kind"] = static_cast<int>(target.kind);
    object["name"] = target.name;
    object["houses"] = housesToJson(target.houses);
    object["house_lord_match"] = static_cast<int>(target.houseLordMatch);
    return object;
}

ReturnFinderTarget targetFromJson(const QJsonObject& object) {
    ReturnFinderTarget target;
    target.scope = static_cast<ReturnFinderScope>(object.value("scope").toInt(0));
    target.kind = static_cast<ReturnFinderTargetKind>(object.value("kind").toInt(0));
    target.name = object.value("name").toString("Sun");
    target.houses = housesFromJson(object.value("houses").toArray());
    target.houseLordMatch = static_cast<ReturnFinderHouseLordMatch>(object.value("house_lord_match").toInt(0));
    return target;
}

QJsonObject conditionToJson(const ReturnFinderCondition& condition) {
    QJsonObject object;
    object["id"] = condition.id;
    object["enabled"] = condition.enabled;
    object["exclude"] = condition.exclude;
    object["type"] = static_cast<int>(condition.type);
    object["subject"] = targetToJson(condition.subject);
    object["target"] = targetToJson(condition.target);
    object["placement_kind"] = static_cast<int>(condition.placementKind);
    object["target_sign"] = condition.targetSign;
    object["target_house"] = condition.targetHouse;
    object["planet_placement_enabled"] = condition.planetPlacementEnabled;
    object["planet_angle_contact_enabled"] = condition.planetAngleContactEnabled;
    object["aspect"] = static_cast<int>(condition.aspect);
    object["orb"] = condition.orb;
    object["house_lord_houses"] = housesToJson(condition.houseLordHouses);
    object["house_lord_scope"] = static_cast<int>(condition.houseLordScope);
    object["house_lord_match"] = static_cast<int>(condition.houseLordMatch);
    object["stellium_minimum"] = condition.stelliumMinimum;
    object["stellium_by_sign"] = condition.stelliumBySign;
    object["stellium_any"] = condition.stelliumAny;
    object["stellium_target"] = condition.stelliumTarget;
    return object;
}

ReturnFinderCondition conditionFromJson(const QJsonObject& object) {
    ReturnFinderCondition condition;
    condition.id = object.value("id").toString();
    condition.enabled = object.value("enabled").toBool(true);
    condition.exclude = object.value("exclude").toBool(false);
    condition.type = static_cast<ReturnFinderConditionType>(object.value("type").toInt(0));
    condition.subject = targetFromJson(object.value("subject").toObject());
    condition.target = targetFromJson(object.value("target").toObject());
    condition.placementKind = static_cast<ReturnFinderPlacementKind>(object.value("placement_kind").toInt(1));
    condition.targetSign = object.value("target_sign").toInt(0);
    condition.targetHouse = object.value("target_house").toInt(1);
    condition.planetPlacementEnabled = object.value("planet_placement_enabled").toBool(true);
    condition.planetAngleContactEnabled = object.value("planet_angle_contact_enabled").toBool(false);
    condition.aspect = static_cast<ReturnFinderAspect>(object.value("aspect").toInt(0));
    condition.orb = object.value("orb").toDouble(1.0);
    condition.houseLordHouses = housesFromJson(object.value("house_lord_houses").toArray());
    condition.houseLordScope = static_cast<ReturnFinderScope>(object.value("house_lord_scope").toInt(0));
    condition.houseLordMatch = static_cast<ReturnFinderHouseLordMatch>(object.value("house_lord_match").toInt(0));
    condition.stelliumMinimum = object.value("stellium_minimum").toInt(3);
    condition.stelliumBySign = object.value("stellium_by_sign").toBool(false);
    condition.stelliumAny = object.value("stellium_any").toBool(false);
    condition.stelliumTarget = object.value("stellium_target").toInt(1);
    return condition;
}

class ReturnFinderTargetEditor : public QWidget {
public:
    ReturnFinderTargetEditor(const QString& title, QWidget* parent, const std::function<void()>& changed)
        : QWidget(parent), changed_(changed) {
        auto* layout = new QGridLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setHorizontalSpacing(6);
        layout->setVerticalSpacing(4);
        titleLabel_ = new QLabel(title, this);
        titleLabel_->setStyleSheet("font-weight: 600;");
        scopeCombo_ = new QComboBox(this);
        scopeCombo_->addItem("Return", static_cast<int>(ReturnFinderScope::Return));
        scopeCombo_->addItem("Natal", static_cast<int>(ReturnFinderScope::Natal));
        kindCombo_ = new QComboBox(this);
        kindCombo_->addItem("Planet", static_cast<int>(ReturnFinderTargetKind::Planet));
        kindCombo_->addItem("Angle (ASC / DSC / MC / IC)", static_cast<int>(ReturnFinderTargetKind::Angle));
        kindCombo_->addItem("House Lord", static_cast<int>(ReturnFinderTargetKind::HouseLord));
        kindCombo_->setToolTip("Choose whether this side of the aspect is a planet, an angle, or a house lord.");
        valueStack_ = new QStackedWidget(this);
        planetCombo_ = new QComboBox(valueStack_);
        planetCombo_->addItems(finderPlanets());
        angleCombo_ = new QComboBox(valueStack_);
        fillFinderAngles(angleCombo_);
        angleCombo_->setToolTip("Angles are independent points; for example, a planet can conjunct DSC from either side of the cusp.");
        auto* lordPage = new QWidget(valueStack_);
        auto* lordLayout = new QHBoxLayout(lordPage);
        lordLayout->setContentsMargins(0, 0, 0, 0);
        housesEdit_ = new QLineEdit(lordPage);
        housesEdit_->setPlaceholderText("Houses, e.g. 8, 12");
        housesEdit_->setToolTip("Enter one or more house numbers separated by commas.");
        lordMatchCombo_ = new QComboBox(lordPage);
        lordMatchCombo_->addItem("Any lord", static_cast<int>(ReturnFinderHouseLordMatch::Any));
        lordMatchCombo_->addItem("All lords", static_cast<int>(ReturnFinderHouseLordMatch::All));
        lordLayout->addWidget(housesEdit_, 1);
        lordLayout->addWidget(lordMatchCombo_);
        valueStack_->addWidget(planetCombo_);
        valueStack_->addWidget(angleCombo_);
        valueStack_->addWidget(lordPage);
        layout->addWidget(titleLabel_, 0, 0);
        layout->addWidget(scopeCombo_, 0, 1);
        layout->addWidget(kindCombo_, 0, 2);
        layout->addWidget(valueStack_, 1, 0, 1, 3);

        auto notify = [this]() { if (changed_) changed_(); };
        connect(scopeCombo_, &QComboBox::currentIndexChanged, this, notify);
        connect(kindCombo_, &QComboBox::currentIndexChanged, this, [this, notify]() {
            valueStack_->setCurrentIndex(kindCombo_->currentIndex());
            notify();
        });
        connect(planetCombo_, &QComboBox::currentIndexChanged, this, notify);
        connect(angleCombo_, &QComboBox::currentIndexChanged, this, notify);
        connect(housesEdit_, &QLineEdit::textChanged, this, notify);
        connect(lordMatchCombo_, &QComboBox::currentIndexChanged, this, notify);
        housesEdit_->setText("1");
    }

    ReturnFinderTarget target() const {
        ReturnFinderTarget target;
        target.scope = static_cast<ReturnFinderScope>(scopeCombo_->currentData().toInt());
        target.kind = static_cast<ReturnFinderTargetKind>(kindCombo_->currentData().toInt());
        target.name = target.kind == ReturnFinderTargetKind::Angle
            ? angleCombo_->currentData().toString() : planetCombo_->currentText();
        target.houses = parseHouses(housesEdit_->text());
        target.houseLordMatch = static_cast<ReturnFinderHouseLordMatch>(lordMatchCombo_->currentData().toInt());
        return target;
    }

    void setTarget(const ReturnFinderTarget& target) {
        QSignalBlocker b1(scopeCombo_);
        QSignalBlocker b2(kindCombo_);
        scopeCombo_->setCurrentIndex(std::max(0, scopeCombo_->findData(static_cast<int>(target.scope))));
        kindCombo_->setCurrentIndex(std::max(0, kindCombo_->findData(static_cast<int>(target.kind))));
        valueStack_->setCurrentIndex(kindCombo_->currentIndex());
        if (target.kind == ReturnFinderTargetKind::Angle) {
            const int angleIndex = angleCombo_->findData(target.name);
            angleCombo_->setCurrentIndex(std::max(0, angleIndex));
        } else {
            planetCombo_->setCurrentText(target.name);
        }
        housesEdit_->setText(housesText(target.houses));
        lordMatchCombo_->setCurrentIndex(std::max(0, lordMatchCombo_->findData(static_cast<int>(target.houseLordMatch))));
    }

private:
    std::function<void()> changed_;
    QLabel* titleLabel_ = nullptr;
    QComboBox* scopeCombo_ = nullptr;
    QComboBox* kindCombo_ = nullptr;
    QStackedWidget* valueStack_ = nullptr;
    QComboBox* planetCombo_ = nullptr;
    QComboBox* angleCombo_ = nullptr;
    QLineEdit* housesEdit_ = nullptr;
    QComboBox* lordMatchCombo_ = nullptr;
};

}  // namespace dracoved
