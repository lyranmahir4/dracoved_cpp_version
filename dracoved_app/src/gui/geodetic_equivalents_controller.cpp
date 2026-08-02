#include "geodetic_equivalents_controller.h"

#include "../core/formatting.h"
#include "../core/swiss_eph.h"
#include "../core/timezone_utils.h"
#include "astro_map_widget.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTimeEdit>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

namespace dracoved {

namespace {

struct BodyDefinition {
    const char* name;
    int swissId;
    QColor color;
    bool enabledByDefault;
};

const std::array<BodyDefinition, 13>& bodyDefinitions() {
    static const std::array<BodyDefinition, 13> definitions = {{
        {"Sun", SE_SUN, QColor("#D89A00"), false},
        {"Moon", SE_MOON, QColor("#68758A"), false},
        {"Mercury", SE_MERCURY, QColor("#4D7FA8"), false},
        {"Venus", SE_VENUS, QColor("#4B9B72"), false},
        {"Mars", SE_MARS, QColor("#D4564C"), false},
        {"Jupiter", SE_JUPITER, QColor("#A06B3B"), true},
        {"Saturn", SE_SATURN, QColor("#5D6370"), true},
        {"Uranus", SE_URANUS, QColor("#2A9BA5"), true},
        {"Neptune", SE_NEPTUNE, QColor("#536FC3"), true},
        {"Pluto", SE_PLUTO, QColor("#8256A8"), true},
        {"Mean North Node", SE_MEAN_NODE, QColor("#A56B6B"), false},
        {"True North Node", SE_TRUE_NODE, QColor("#9B5252"), false},
        {"Chiron", SE_CHIRON, QColor("#7A6746"), false},
    }};
    return definitions;
}

const BodyDefinition* definitionForName(const QString& name) {
    for (const auto& definition : bodyDefinitions()) {
        if (name == QString::fromLatin1(definition.name)) {
            return &definition;
        }
    }
    return nullptr;
}

QColor signLineColor() {
    return QColor("#7B8794");
}

QString compactDateTime(const QDateTime& dateTime) {
    return dateTime.isValid()
        ? dateTime.toString("ddd, d MMM yyyy, h:mm:ss AP")
        : QString("-");
}

QString markdownCell(QString value) {
    value.replace('|', "\\|");
    value.replace('\n', "<br>");
    return value;
}

}  // namespace

GeodeticEquivalentsController::GeodeticEquivalentsController(
    SwissEph* swissEphemeris, QObject* parent)
    : QObject(parent),
      swissEphemeris_(swissEphemeris) {
    buildFiltersUi();
    buildWorkspaceUi();
}

QWidget* GeodeticEquivalentsController::filtersWidget() const {
    return filtersRoot_;
}

QWidget* GeodeticEquivalentsController::workspaceWidget() const {
    return workspaceRoot_;
}

void GeodeticEquivalentsController::setActive(bool active) {
    active_ = active;
    if (active_ && !snapshot_.valid) {
        calculate();
    }
}

bool GeodeticEquivalentsController::hasResult() const {
    return snapshot_.valid;
}

bool GeodeticEquivalentsController::isStale() const {
    return snapshot_.stale;
}

bool GeodeticEquivalentsController::hasSelectedLocation() const {
    return hasSelectedLocation_;
}

double GeodeticEquivalentsController::selectedLatitude() const {
    return selectedLatitude_;
}

double GeodeticEquivalentsController::selectedLongitude() const {
    return selectedLongitude_;
}

GeodeticAngles GeodeticEquivalentsController::selectedAngles() const {
    return selectedAngles_;
}

QVector<GeodeticContact>
GeodeticEquivalentsController::selectedContacts() const {
    return selectedContacts_;
}

GeodeticMapSnapshot GeodeticEquivalentsController::snapshot() const {
    return snapshot_;
}

void GeodeticEquivalentsController::buildFiltersUi() {
    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* contents = new QWidget(scrollArea);
    auto* rootLayout = new QVBoxLayout(contents);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);

    auto* title = new QLabel("Geodetic Equivalents", contents);
    title->setObjectName("sectionTitle");
    auto* introduction = new QLabel(
        "Map collective planetary transits onto the terrestrial zodiac. "
        "This workspace does not require a natal chart.", contents);
    introduction->setObjectName("hintLabel");
    introduction->setWordWrap(true);
    rootLayout->addWidget(title);
    rootLayout->addWidget(introduction);

    auto* momentGroup = new QGroupBox("Reference Moment", contents);
    auto* momentLayout = new QFormLayout(momentGroup);
    dateEdit_ = new QDateEdit(QDate::currentDate(), momentGroup);
    dateEdit_->setCalendarPopup(true);
    dateEdit_->setDisplayFormat("yyyy-MM-dd");
    dateEdit_->setDateRange(QDate(1800, 1, 1), QDate(2399, 12, 31));
    timeEdit_ = new QTimeEdit(QTime::currentTime(), momentGroup);
    timeEdit_->setDisplayFormat("hh:mm:ss AP");
    timezoneEdit_ = new QLineEdit(momentGroup);
    const QByteArray systemTimezone = QTimeZone::systemTimeZoneId();
    timezoneEdit_->setText(systemTimezone.isEmpty()
        ? QString("UTC") : QString::fromUtf8(systemTimezone));
    timezoneEdit_->setPlaceholderText("e.g. Asia/Dhaka or UTC+6");
    momentLayout->addRow("Date", dateEdit_);
    momentLayout->addRow("Time", timeEdit_);
    momentLayout->addRow("Timezone", timezoneEdit_);

    auto* navigationRow = new QWidget(momentGroup);
    auto* navigationLayout = new QHBoxLayout(navigationRow);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    navigationLayout->setSpacing(4);
    auto* previousWeek = new QPushButton("-1 Week", navigationRow);
    auto* previousDay = new QPushButton("-1 Day", navigationRow);
    auto* nowButton = new QPushButton("Now", navigationRow);
    auto* nextDay = new QPushButton("+1 Day", navigationRow);
    auto* nextWeek = new QPushButton("+1 Week", navigationRow);
    navigationLayout->addWidget(previousWeek);
    navigationLayout->addWidget(previousDay);
    navigationLayout->addWidget(nowButton);
    navigationLayout->addWidget(nextDay);
    navigationLayout->addWidget(nextWeek);
    momentLayout->addRow(navigationRow);
    rootLayout->addWidget(momentGroup);

    auto* methodGroup = new QGroupBox("Geodetic Method", contents);
    auto* methodLayout = new QFormLayout(methodGroup);
    auto* methodLabel = new QLabel("Standard / Sepharial", methodGroup);
    methodLabel->setToolTip(
        "0 degrees Aries is placed on the Greenwich meridian; "
        "the zodiac advances eastward.");
    referenceMeridianSpin_ = new QDoubleSpinBox(methodGroup);
    referenceMeridianSpin_->setRange(-180.0, 180.0);
    referenceMeridianSpin_->setDecimals(2);
    referenceMeridianSpin_->setSingleStep(1.0);
    referenceMeridianSpin_->setSuffix(QString(QChar(0x00B0)));
    referenceMeridianSpin_->setValue(0.0);
    referenceMeridianSpin_->setToolTip(
        "Reference meridian for 0 degrees Aries. Greenwich is 0 degrees.");
    auto* meridianRow = new QWidget(methodGroup);
    auto* meridianLayout = new QHBoxLayout(meridianRow);
    meridianLayout->setContentsMargins(0, 0, 0, 0);
    meridianLayout->setSpacing(4);
    auto* greenwichButton = new QPushButton("Greenwich", meridianRow);
    meridianLayout->addWidget(referenceMeridianSpin_, 1);
    meridianLayout->addWidget(greenwichButton);

    zodiacCombo_ = new QComboBox(methodGroup);
    zodiacCombo_->addItem("Tropical", static_cast<int>(ZodiacSystem::Tropical));
    zodiacCombo_->addItem("Sidereal", static_cast<int>(ZodiacSystem::Sidereal));
    ayanamsaCombo_ = new QComboBox(methodGroup);
    for (const QString& name : availableSiderealAyanamsaNames()) {
        ayanamsaCombo_->addItem(name);
    }
    ayanamsaCombo_->setEnabled(false);
    methodLayout->addRow("Method", methodLabel);
    methodLayout->addRow("Aries meridian", meridianRow);
    methodLayout->addRow("Transit zodiac", zodiacCombo_);
    methodLayout->addRow("Ayanamsa", ayanamsaCombo_);
    rootLayout->addWidget(methodGroup);

    auto* bodiesGroup = new QGroupBox("Transit Bodies", contents);
    auto* bodiesLayout = new QVBoxLayout(bodiesGroup);
    bodyList_ = new QListWidget(bodiesGroup);
    bodyList_->setSelectionMode(QAbstractItemView::NoSelection);
    bodyList_->setMinimumHeight(210);
    for (const auto& definition : bodyDefinitions()) {
        auto* item = new QListWidgetItem(
            QString::fromLatin1(definition.name), bodyList_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(
            definition.enabledByDefault ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, definition.swissId);
    }
    auto* bodyButtons = new QWidget(bodiesGroup);
    auto* bodyButtonsLayout = new QHBoxLayout(bodyButtons);
    bodyButtonsLayout->setContentsMargins(0, 0, 0, 0);
    auto* allBodiesButton = new QPushButton("Select All", bodyButtons);
    auto* outerBodiesButton = new QPushButton("Outer Planets", bodyButtons);
    auto* clearBodiesButton = new QPushButton("Clear", bodyButtons);
    bodyButtonsLayout->addWidget(allBodiesButton);
    bodyButtonsLayout->addWidget(outerBodiesButton);
    bodyButtonsLayout->addWidget(clearBodiesButton);
    bodiesLayout->addWidget(bodyList_);
    bodiesLayout->addWidget(bodyButtons);
    rootLayout->addWidget(bodiesGroup);

    auto* layersGroup = new QGroupBox("Map Layers", contents);
    auto* layersLayout = new QVBoxLayout(layersGroup);
    auto* angleRow = new QWidget(layersGroup);
    auto* angleLayout = new QHBoxLayout(angleRow);
    angleLayout->setContentsMargins(0, 0, 0, 0);
    mcCheck_ = new QCheckBox("MC", angleRow);
    icCheck_ = new QCheckBox("IC", angleRow);
    ascCheck_ = new QCheckBox("ASC", angleRow);
    dscCheck_ = new QCheckBox("DSC", angleRow);
    mcCheck_->setChecked(true);
    icCheck_->setChecked(true);
    ascCheck_->setChecked(true);
    dscCheck_->setChecked(true);
    angleLayout->addWidget(mcCheck_);
    angleLayout->addWidget(icCheck_);
    angleLayout->addWidget(ascCheck_);
    angleLayout->addWidget(dscCheck_);
    angleLayout->addStretch();
    signsMcCheck_ = new QCheckBox("Signs on MC", layersGroup);
    signsAscCheck_ = new QCheckBox("Signs on ASC", layersGroup);
    signsMcCheck_->setChecked(true);
    signsAscCheck_->setChecked(false);
    contactOrbSpin_ = new QDoubleSpinBox(layersGroup);
    contactOrbSpin_->setRange(0.1, 10.0);
    contactOrbSpin_->setDecimals(1);
    contactOrbSpin_->setSingleStep(0.5);
    contactOrbSpin_->setSuffix(QString(QChar(0x00B0)));
    contactOrbSpin_->setValue(2.0);
    auto* orbRow = new QFormLayout;
    orbRow->addRow("Selected-location orb", contactOrbSpin_);
    layersLayout->addWidget(angleRow);
    layersLayout->addWidget(signsMcCheck_);
    layersLayout->addWidget(signsAscCheck_);
    layersLayout->addLayout(orbRow);
    rootLayout->addWidget(layersGroup);

    calculateButton_ = new QPushButton("Calculate Geodetic Map", contents);
    calculateButton_->setDefault(true);
    statusLabel_ = new QLabel(
        "Ready. The current moment will be calculated when this tab opens.",
        contents);
    statusLabel_->setObjectName("hintLabel");
    statusLabel_->setWordWrap(true);
    rootLayout->addWidget(calculateButton_);
    rootLayout->addWidget(statusLabel_);
    rootLayout->addStretch(1);

    scrollArea->setWidget(contents);
    filtersRoot_ = scrollArea;

    connect(dateEdit_, &QDateEdit::dateChanged,
            this, &GeodeticEquivalentsController::markStale);
    connect(timeEdit_, &QTimeEdit::timeChanged,
            this, &GeodeticEquivalentsController::markStale);
    connect(timezoneEdit_, &QLineEdit::textChanged,
            this, &GeodeticEquivalentsController::markStale);
    connect(referenceMeridianSpin_,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &GeodeticEquivalentsController::markStale);
    connect(zodiacCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
                updateZodiacControls();
                markStale();
            });
    connect(ayanamsaCombo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GeodeticEquivalentsController::markStale);
    connect(greenwichButton, &QPushButton::clicked, this, [this]() {
        referenceMeridianSpin_->setValue(0.0);
    });
    connect(previousWeek, &QPushButton::clicked,
            this, [this]() { adjustDays(-7); });
    connect(previousDay, &QPushButton::clicked,
            this, [this]() { adjustDays(-1); });
    connect(nowButton, &QPushButton::clicked,
            this, &GeodeticEquivalentsController::setNow);
    connect(nextDay, &QPushButton::clicked,
            this, [this]() { adjustDays(1); });
    connect(nextWeek, &QPushButton::clicked,
            this, [this]() { adjustDays(7); });
    connect(calculateButton_, &QPushButton::clicked,
            this, &GeodeticEquivalentsController::calculate);
    connect(bodyList_, &QListWidget::itemChanged,
            this, [this]() { markStale(); });
    connect(allBodiesButton, &QPushButton::clicked, this, [this]() {
        for (int row = 0; row < bodyList_->count(); ++row) {
            bodyList_->item(row)->setCheckState(Qt::Checked);
        }
    });
    connect(outerBodiesButton, &QPushButton::clicked, this, [this]() {
        for (int row = 0; row < bodyList_->count(); ++row) {
            const QString name = bodyList_->item(row)->text();
            const bool outer = name == "Jupiter" || name == "Saturn"
                || name == "Uranus" || name == "Neptune" || name == "Pluto";
            bodyList_->item(row)->setCheckState(
                outer ? Qt::Checked : Qt::Unchecked);
        }
    });
    connect(clearBodiesButton, &QPushButton::clicked, this, [this]() {
        for (int row = 0; row < bodyList_->count(); ++row) {
            bodyList_->item(row)->setCheckState(Qt::Unchecked);
        }
    });
    const QList<QCheckBox*> layerChecks = {
        mcCheck_, icCheck_, ascCheck_, dscCheck_,
        signsMcCheck_, signsAscCheck_,
    };
    for (QCheckBox* check : layerChecks) {
        connect(check, &QCheckBox::toggled, this, [this]() {
            if (snapshot_.valid) {
                updateMapLines();
                updateSelectedLocation();
                emit resultChanged();
            }
        });
    }
    connect(contactOrbSpin_,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this]() {
                updateSelectedLocation();
                emit resultChanged();
            });
}

void GeodeticEquivalentsController::buildWorkspaceUi() {
    workspaceRoot_ = new QWidget;
    auto* layout = new QVBoxLayout(workspaceRoot_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* header = new QWidget(workspaceRoot_);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 6, 8, 0);
    auto* title = new QLabel("Geodetic Equivalents Map", header);
    title->setObjectName("sectionTitle");
    workspaceSummaryLabel_ = new QLabel(
        "Current world-transit map has not been calculated.", header);
    workspaceSummaryLabel_->setObjectName("hintLabel");
    workspaceSummaryLabel_->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* worldButton = new QPushButton("World View", header);
    auto* clearSelectionButton = new QPushButton("Clear Selection", header);
    copyButton_ = new QPushButton("Copy Report", header);
    copyButton_->setEnabled(false);
    headerLayout->addWidget(title);
    headerLayout->addWidget(workspaceSummaryLabel_, 1);
    headerLayout->addWidget(worldButton);
    headerLayout->addWidget(clearSelectionButton);
    headerLayout->addWidget(copyButton_);
    layout->addWidget(header);

    mapWidget_ = new AstroMapWidget(workspaceRoot_);
    mapWidget_->setMinimumSize(600, 420);
    layout->addWidget(mapWidget_, 1);

    auto* legend = new QLabel(
        "Click the map to inspect its geodetic ASC, MC, DSC and IC and "
        "the transit bodies activating those angles.", workspaceRoot_);
    legend->setObjectName("hintLabel");
    legend->setContentsMargins(8, 0, 8, 6);
    legend->setWordWrap(true);
    layout->addWidget(legend);

    connect(worldButton, &QPushButton::clicked,
            mapWidget_, &AstroMapWidget::zoomToWorld);
    connect(clearSelectionButton, &QPushButton::clicked,
            this, &GeodeticEquivalentsController::clearSelectedLocation);
    connect(copyButton_, &QPushButton::clicked,
            this, &GeodeticEquivalentsController::copyReport);
    connect(mapWidget_, &AstroMapWidget::mapClicked,
            this, &GeodeticEquivalentsController::handleMapClicked);
    connect(mapWidget_, &AstroMapWidget::mapHovered,
            this, &GeodeticEquivalentsController::handleMapHovered);
    connect(mapWidget_, &AstroMapWidget::mapHoverCleared,
            this, [this]() {
                if (mapWidget_) {
                    mapWidget_->setHoverInfo(QString());
                }
            });
}

void GeodeticEquivalentsController::markStale() {
    if (updatingControls_) {
        return;
    }
    snapshot_.stale = true;
    if (copyButton_) {
        copyButton_->setEnabled(false);
    }
    if (snapshot_.valid) {
        statusLabel_->setText(
            "Pending changes. Calculate again to refresh the map.");
        workspaceSummaryLabel_->setText(
            "Stale map - controls changed after the last calculation.");
    }
    emit resultChanged();
}

void GeodeticEquivalentsController::calculate() {
    if (!swissEphemeris_ || !swissEphemeris_->isLoaded()) {
        statusLabel_->setText("Swiss Ephemeris is not loaded.");
        emit statusMessage("Cannot calculate Geodetic Equivalents: Swiss Ephemeris is unavailable.");
        return;
    }

    if (selectedBodies().isEmpty()) {
        statusLabel_->setText("Select at least one transit body.");
        emit statusMessage("Select at least one transit body for the geodetic map.");
        return;
    }

    QTimeZone timezone;
    QString timezoneLabel;
    QString timezoneError;
    if (!parseTimezoneInput(timezoneEdit_->text(), &timezone,
                            &timezoneLabel, &timezoneError)) {
        statusLabel_->setText(timezoneError);
        return;
    }
    const QDateTime localMoment(dateEdit_->date(), timeEdit_->time(), timezone);
    if (!localMoment.isValid()) {
        statusLabel_->setText("The selected local date and time are invalid.");
        return;
    }
    const QDateTime utcMoment = localMoment.toUTC();
    const double decimalHour = utcMoment.time().hour()
        + utcMoment.time().minute() / 60.0
        + utcMoment.time().second() / 3600.0;
    const double julianDay = swissEphemeris_->julianDay(
        utcMoment.date().year(), utcMoment.date().month(),
        utcMoment.date().day(), decimalHour, SE_GREG_CAL);

    const ZodiacSystem zodiacSystem = static_cast<ZodiacSystem>(
        zodiacCombo_->currentData().toInt());
    const SiderealAyanamsa ayanamsa = siderealAyanamsaFromString(
        ayanamsaCombo_->currentText());
    int flags = SEFLG_SPEED;
    if (zodiacSystem == ZodiacSystem::Sidereal) {
        swissEphemeris_->setSidMode(siderealAyanamsaSwissMode(ayanamsa));
        flags |= SEFLG_SIDEREAL;
    }

    double obliquity = 23.4392911;
    QString obliquityError;
    if (!swissEphemeris_->calcUt(
            julianDay, SE_ECL_NUT, 0, &obliquity, &obliquityError)) {
        obliquityError = QString(
            "Obliquity fallback used: %1").arg(obliquityError);
        obliquity = 23.4392911;
    }

    QVector<GeodeticTransitBody> calculatedBodies;
    QStringList warnings;
    if (!obliquityError.isEmpty()) {
        warnings.push_back(obliquityError);
    }
    for (int row = 0; row < bodyList_->count(); ++row) {
        const QListWidgetItem* item = bodyList_->item(row);
        if (!item || item->checkState() != Qt::Checked) {
            continue;
        }
        const int swissId = item->data(Qt::UserRole).toInt();
        double values[6] = {0};
        QString calculationError;
        if (!swissEphemeris_->calcUtFull(
                julianDay, swissId, flags, values, &calculationError)) {
            warnings.push_back(
                QString("%1 skipped: %2").arg(item->text(), calculationError));
            continue;
        }
        GeodeticTransitBody body;
        body.name = item->text();
        body.longitude = normalizeGeodeticDegrees(values[0]);
        body.dailySpeed = values[3];
        body.retrograde = body.dailySpeed < 0.0;
        calculatedBodies.push_back(body);
    }

    if (calculatedBodies.isEmpty()) {
        statusLabel_->setText(
            "None of the selected bodies could be calculated.");
        snapshot_.warnings = warnings;
        emit resultChanged();
        return;
    }

    snapshot_.valid = true;
    snapshot_.stale = false;
    snapshot_.localMoment = localMoment;
    snapshot_.utcMoment = utcMoment;
    snapshot_.timezone = timezoneLabel;
    snapshot_.zodiacSystem = zodiacSystem;
    snapshot_.ayanamsa = ayanamsa;
    snapshot_.referenceMeridian = referenceMeridianSpin_->value();
    snapshot_.obliquity = obliquity;
    snapshot_.bodies = calculatedBodies;
    snapshot_.warnings = warnings;

    {
        const QSignalBlocker blocker(timezoneEdit_);
        timezoneEdit_->setText(timezoneLabel);
    }
    updateMapLines();
    updateSelectedLocation();
    const QString warningSuffix = warnings.isEmpty()
        ? QString() : QString(" - %1 warning(s)").arg(warnings.size());
    statusLabel_->setText(
        QString("Current map: %1 bodies, %2 lines%3.")
            .arg(calculatedBodies.size())
            .arg(snapshot_.visibleLineCount)
            .arg(warningSuffix));
    workspaceSummaryLabel_->setText(
        QString("%1 | %2 | %3 | %4 lines")
            .arg(compactDateTime(localMoment),
                 timezoneLabel,
                 methodSummary())
            .arg(snapshot_.visibleLineCount));
    copyButton_->setEnabled(true);
    emit resultChanged();
    emit statusMessage("Geodetic Equivalents map calculated.");
}

void GeodeticEquivalentsController::adjustDays(int days) {
    updatingControls_ = true;
    dateEdit_->setDate(dateEdit_->date().addDays(days));
    updatingControls_ = false;
    calculate();
}

void GeodeticEquivalentsController::setNow() {
    QTimeZone timezone;
    QString timezoneLabel;
    QString error;
    if (!parseTimezoneInput(timezoneEdit_->text(),
                            &timezone, &timezoneLabel, &error)) {
        statusLabel_->setText(error);
        return;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc().toTimeZone(timezone);
    updatingControls_ = true;
    dateEdit_->setDate(now.date());
    timeEdit_->setTime(now.time());
    timezoneEdit_->setText(timezoneLabel);
    updatingControls_ = false;
    calculate();
}

void GeodeticEquivalentsController::updateZodiacControls() {
    const ZodiacSystem zodiacSystem = static_cast<ZodiacSystem>(
        zodiacCombo_->currentData().toInt());
    ayanamsaCombo_->setEnabled(zodiacSystem == ZodiacSystem::Sidereal);
}

void GeodeticEquivalentsController::updateMapLines() {
    if (!snapshot_.valid || !mapWidget_) {
        return;
    }

    QVector<AstroMapLine> lines;
    const double reference = snapshot_.referenceMeridian;
    const double obliquity = snapshot_.obliquity;
    auto addLine = [&lines](const QString& label, const QString& edgeLabel,
                            const QString& iconPath, const QColor& color,
                            const QVector<QPointF>& points, double width) {
        if (points.size() < 2) {
            return;
        }
        AstroMapLine line;
        line.label = label;
        line.edgeLabel = edgeLabel;
        line.symbolResourcePath = iconPath;
        line.color = color;
        line.lonLatPoints = points;
        line.width = width;
        lines.push_back(line);
    };

    for (const GeodeticTransitBody& body : snapshot_.bodies) {
        const BodyDefinition* definition = definitionForName(body.name);
        const QColor color = definition ? definition->color : QColor("#555555");
        const QString iconPath = bodySvgResourcePath(body.name);
        if (mcCheck_->isChecked()) {
            addLine(QString("%1 on geodetic MC").arg(body.name),
                    QString("%1 MC").arg(body.name), iconPath, color,
                    makeGeodeticMeridianLine(
                        reference + body.longitude), 1.7);
        }
        if (icCheck_->isChecked()) {
            addLine(QString("%1 on geodetic IC").arg(body.name),
                    QString("%1 IC").arg(body.name), iconPath, color,
                    makeGeodeticMeridianLine(
                        reference + body.longitude + 180.0), 1.45);
        }
        if (ascCheck_->isChecked()) {
            addLine(QString("%1 on geodetic ASC").arg(body.name),
                    QString("%1 ASC").arg(body.name), iconPath, color,
                    makeGeodeticHorizonLine(
                        body.longitude, true, reference, obliquity), 1.7);
        }
        if (dscCheck_->isChecked()) {
            addLine(QString("%1 on geodetic DSC").arg(body.name),
                    QString("%1 DSC").arg(body.name), iconPath, color,
                    makeGeodeticHorizonLine(
                        body.longitude, false, reference, obliquity), 1.45);
        }
    }

    if (signsMcCheck_->isChecked()) {
        for (int sign = 0; sign < 12; ++sign) {
            const QString name = signName(sign);
            addLine(QString("%1 on geodetic MC").arg(name),
                    QString(), QString(), signLineColor(),
                    makeGeodeticMeridianLine(reference + sign * 30.0), 0.9);
        }
    }
    if (signsAscCheck_->isChecked()) {
        for (int sign = 0; sign < 12; ++sign) {
            const QString name = signName(sign);
            addLine(QString("%1 on geodetic ASC").arg(name),
                    QString(), QString(), signLineColor(),
                    makeGeodeticHorizonLine(
                        sign * 30.0, true, reference, obliquity), 0.9);
        }
    }

    snapshot_.visibleLineCount = lines.size();
    mapWidget_->setLines(lines);
    if (workspaceSummaryLabel_ && snapshot_.localMoment.isValid()) {
        workspaceSummaryLabel_->setText(
            QString("%1 | %2 | %3 | %4 lines")
                .arg(compactDateTime(snapshot_.localMoment),
                     snapshot_.timezone,
                     methodSummary())
                .arg(snapshot_.visibleLineCount));
    }
}

void GeodeticEquivalentsController::updateSelectedLocation() {
    if (!snapshot_.valid || !hasSelectedLocation_) {
        selectedAngles_ = GeodeticAngles();
        selectedContacts_.clear();
        return;
    }
    selectedAngles_ = calculateGeodeticAngles(
        selectedLatitude_, selectedLongitude_,
        snapshot_.referenceMeridian, snapshot_.obliquity);
    selectedContacts_ = contactsForAngles(selectedAngles_);
    if (mapWidget_) {
        mapWidget_->setSelectedLocation(
            selectedLatitude_, selectedLongitude_,
            QString("%1 | MC %2 | ASC %3")
                .arg(coordinateText(selectedLatitude_, selectedLongitude_),
                     longitudeText(selectedAngles_.midheaven),
                     longitudeText(selectedAngles_.ascendant)));
    }
}

void GeodeticEquivalentsController::clearSelectedLocation() {
    hasSelectedLocation_ = false;
    selectedAngles_ = GeodeticAngles();
    selectedContacts_.clear();
    if (mapWidget_) {
        mapWidget_->clearSelectedLocation();
    }
    emit resultChanged();
}

void GeodeticEquivalentsController::handleMapClicked(
    double latitude, double longitude) {
    if (!snapshot_.valid || snapshot_.stale) {
        emit statusMessage(
            "Calculate the current Geodetic Equivalents map before selecting a location.");
        return;
    }
    selectedLatitude_ = latitude;
    selectedLongitude_ = longitude;
    hasSelectedLocation_ = true;
    updateSelectedLocation();
    emit resultChanged();
}

void GeodeticEquivalentsController::handleMapHovered(
    double latitude, double longitude) {
    if (!snapshot_.valid || !mapWidget_) {
        return;
    }
    const GeodeticAngles angles = calculateGeodeticAngles(
        latitude, longitude, snapshot_.referenceMeridian,
        snapshot_.obliquity);
    mapWidget_->setHoverInfo(
        QString("%1 | MC %2 | ASC %3")
            .arg(coordinateText(latitude, longitude),
                 longitudeText(angles.midheaven),
                 longitudeText(angles.ascendant)));
}

void GeodeticEquivalentsController::copyReport() {
    if (!snapshot_.valid) {
        return;
    }
    QString report;
    report += "# Geodetic Equivalents Transit Map\n\n";
    report += "## Calculation\n\n";
    report += QString("- Local moment: %1 (%2)\n")
        .arg(compactDateTime(snapshot_.localMoment), snapshot_.timezone);
    report += QString("- UTC moment: %1 UTC\n")
        .arg(snapshot_.utcMoment.toString("yyyy-MM-dd HH:mm:ss"));
    report += QString("- Method: %1\n").arg(methodSummary());
    report += QString("- Reference meridian: %1%2\n")
        .arg(snapshot_.referenceMeridian, 0, 'f', 2)
        .arg(QChar(0x00B0));
    report += QString("- Visible map lines: %1\n\n")
        .arg(snapshot_.visibleLineCount);

    report += "## Transit Bodies\n\n";
    report += "| Body | Position | Daily Motion |\n";
    report += "|---|---:|---:|\n";
    for (const auto& body : snapshot_.bodies) {
        report += QString("| %1 | %2%3 | %4%5/day |\n")
            .arg(markdownCell(body.name),
                 markdownCell(longitudeText(body.longitude)),
                 body.retrograde ? " R" : "",
                 body.dailySpeed >= 0.0 ? "+" : "")
            .arg(body.dailySpeed, 0, 'f', 3)
            .arg(QChar(0x00B0));
    }

    if (hasSelectedLocation_ && selectedAngles_.valid) {
        report += "\n## Selected Location\n\n";
        report += QString("- Coordinates: %1\n\n")
            .arg(coordinateText(selectedLatitude_, selectedLongitude_));
        report += "| Geodetic Angle | Position |\n";
        report += "|---|---:|\n";
        report += QString("| ASC | %1 |\n").arg(longitudeText(selectedAngles_.ascendant));
        report += QString("| MC | %1 |\n").arg(longitudeText(selectedAngles_.midheaven));
        report += QString("| DSC | %1 |\n").arg(longitudeText(selectedAngles_.descendant));
        report += QString("| IC | %1 |\n").arg(longitudeText(selectedAngles_.imumCoeli));
        report += "\n## Transit Activations\n\n";
        if (selectedContacts_.isEmpty()) {
            report += QString("No selected transit body is within %1%2 of a geodetic angle.\n")
                .arg(contactOrbSpin_->value(), 0, 'f', 1)
                .arg(QChar(0x00B0));
        } else {
            report += "| Body | Angle | Orb | Motion |\n";
            report += "|---|---|---:|---|\n";
            for (const auto& contact : selectedContacts_) {
                report += QString("| %1 | %2 | %3%4 | %5 |\n")
                    .arg(markdownCell(contact.body),
                         markdownCell(contact.angle))
                    .arg(contact.orb, 0, 'f', 2)
                    .arg(QChar(0x00B0))
                    .arg(markdownCell(contact.motion));
            }
        }
    }

    if (!snapshot_.warnings.isEmpty()) {
        report += "\n## Warnings\n\n";
        for (const QString& warning : snapshot_.warnings) {
            report += QString("- %1\n").arg(warning);
        }
    }
    QApplication::clipboard()->setText(report);
    emit statusMessage("Geodetic Equivalents report copied as Markdown.");
}

QVector<GeodeticTransitBody>
GeodeticEquivalentsController::selectedBodies() const {
    if (!bodyList_) {
        return {};
    }
    QVector<GeodeticTransitBody> bodies;
    for (int row = 0; row < bodyList_->count(); ++row) {
        const QListWidgetItem* item = bodyList_->item(row);
        if (item && item->checkState() == Qt::Checked) {
            GeodeticTransitBody body;
            body.name = item->text();
            bodies.push_back(body);
        }
    }
    return bodies;
}

QVector<GeodeticContact>
GeodeticEquivalentsController::contactsForAngles(
    const GeodeticAngles& angles) const {
    if (!angles.valid) {
        return {};
    }
    struct AngleValue {
        const char* name;
        double longitude;
        bool enabled;
    };
    const std::array<AngleValue, 4> angleValues = {{
        {"MC", angles.midheaven, mcCheck_->isChecked()},
        {"IC", angles.imumCoeli, icCheck_->isChecked()},
        {"ASC", angles.ascendant, ascCheck_->isChecked()},
        {"DSC", angles.descendant, dscCheck_->isChecked()},
    }};
    QVector<GeodeticContact> contacts;
    const double maximumOrb = contactOrbSpin_->value();
    for (const auto& body : snapshot_.bodies) {
        for (const auto& angle : angleValues) {
            if (!angle.enabled) {
                continue;
            }
            const double orb = geodeticAngularDistance(
                body.longitude, angle.longitude);
            if (orb > maximumOrb) {
                continue;
            }
            GeodeticContact contact;
            contact.body = body.name;
            contact.angle = QString::fromLatin1(angle.name);
            contact.bodyLongitude = body.longitude;
            contact.angleLongitude = angle.longitude;
            contact.orb = orb;
            if (std::fabs(body.dailySpeed) < 0.000001) {
                contact.motion = "Stationary";
            } else {
                const double futureLongitude = normalizeGeodeticDegrees(
                    body.longitude + body.dailySpeed / 24.0);
                const double futureOrb = geodeticAngularDistance(
                    futureLongitude, angle.longitude);
                contact.motion = futureOrb < orb ? "Applying" : "Separating";
            }
            contacts.push_back(contact);
        }
    }
    std::sort(contacts.begin(), contacts.end(),
              [](const GeodeticContact& first,
                 const GeodeticContact& second) {
                  return first.orb < second.orb;
              });
    return contacts;
}

QString GeodeticEquivalentsController::longitudeText(double longitude) const {
    const double normalized = normalizeGeodeticDegrees(longitude);
    const int sign = std::clamp(
        static_cast<int>(std::floor(normalized / 30.0)), 0, 11);
    const double inSign = normalized - sign * 30.0;
    int degrees = static_cast<int>(std::floor(inSign));
    int minutes = static_cast<int>(std::floor(
        (inSign - degrees) * 60.0 + 0.5));
    if (minutes >= 60) {
        minutes = 0;
        degrees += 1;
    }
    return QString("%1%2 %3 %4'")
        .arg(degrees, 2, 10, QChar('0'))
        .arg(QChar(0x00B0))
        .arg(signName(sign))
        .arg(minutes, 2, 10, QChar('0'));
}

QString GeodeticEquivalentsController::coordinateText(
    double latitude, double longitude) const {
    return QString("%1%2 %3, %4%5 %6")
        .arg(std::fabs(latitude), 0, 'f', 4)
        .arg(QChar(0x00B0))
        .arg(latitude >= 0.0 ? "N" : "S")
        .arg(std::fabs(longitude), 0, 'f', 4)
        .arg(QChar(0x00B0))
        .arg(longitude >= 0.0 ? "E" : "W");
}

QString GeodeticEquivalentsController::methodSummary() const {
    const QString zodiac = snapshot_.zodiacSystem == ZodiacSystem::Sidereal
        ? QString("Sidereal (%1)").arg(
              siderealAyanamsaToString(snapshot_.ayanamsa))
        : QString("Tropical");
    return QString("Standard / Sepharial | %1 | Aries meridian %2%3")
        .arg(zodiac)
        .arg(snapshot_.referenceMeridian, 0, 'f', 2)
        .arg(QChar(0x00B0));
}

}  // namespace dracoved
