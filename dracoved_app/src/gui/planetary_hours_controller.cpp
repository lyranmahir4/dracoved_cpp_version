#include "planetary_hours_controller.h"

#include "../core/timezone_utils.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimeEdit>
#include <QTimer>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

QString durationText(qint64 milliseconds) {
    const qint64 totalSeconds = std::max<qint64>(0, milliseconds / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QString("%1h %2m %3s")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }
    return QString("%1m %2s").arg(minutes).arg(seconds, 2, 10, QChar('0'));
}

QTableWidgetItem* readOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return item;
}

QString hourPeriodLabel(const PlanetaryHourInterval& hour) {
    return hour.daytime
        ? QString("Day %1").arg(hour.periodHour)
        : QString("Night %1").arg(hour.periodHour);
}

QString pressureText(const PlanetaryHoursCalculationOptions& options) {
    return options.pressureHPa > 0.0
        ? QString("%1 hPa").arg(options.pressureHPa, 0, 'f', 1)
        : QString("Auto (estimated from elevation)");
}

QString coordinateLocationName(double latitude, double longitude) {
    return QString("Coordinates %1, %2")
        .arg(latitude, 0, 'f', 4)
        .arg(longitude, 0, 'f', 4);
}

QString scheduleIdentity(const PlanetaryHoursResult& result,
                         double latitude,
                         double longitude) {
    return QString("%1|%2|%3|%4|%5|%6|%7")
        .arg(result.planetaryDate.toString(Qt::ISODate), result.timezoneLabel)
        .arg(latitude, 0, 'f', 8)
        .arg(longitude, 0, 'f', 8)
        .arg(result.options.elevationMeters, 0, 'f', 2)
        .arg(result.options.pressureHPa, 0, 'f', 2)
        .arg(result.options.temperatureC, 0, 'f', 2);
}

}  // namespace

PlanetaryHoursController::PlanetaryHoursController(SwissEph* swe,
                                                   QNetworkAccessManager* network,
                                                   QObject* parent)
    : QObject(parent), swe_(swe), network_(network) {
    buildFiltersUi();
    buildWorkspaceUi();

    liveTimer_ = new QTimer(this);
    liveTimer_->setInterval(1000);
    liveTimer_->setTimerType(Qt::PreciseTimer);
    connect(liveTimer_, &QTimer::timeout, this, [this]() {
        if (active_ && isLiveMode()) recalculate(false);
    });

    recalculateTimer_ = new QTimer(this);
    recalculateTimer_->setSingleShot(true);
    recalculateTimer_->setInterval(120);
    connect(recalculateTimer_, &QTimer::timeout, this, [this]() {
        const bool force = pendingForceSolarCalculation_;
        pendingForceSolarCalculation_ = false;
        recalculate(force);
    });

    loadSettings();
    updateLocationUi();
    updateMomentUi();
    updateTimezoneStatus();
    showError("Choose a location to calculate planetary hours.");
}

PlanetaryHoursController::~PlanetaryHoursController() {
    rememberCustomLocation();
    saveSettings();
}

QWidget* PlanetaryHoursController::filtersWidget() const { return filtersRoot_; }
QWidget* PlanetaryHoursController::workspaceWidget() const { return workspaceRoot_; }

void PlanetaryHoursController::setNatalContext(const NatalInput& input,
                                                const QString& locationNameValue) {
    if (locationModeCombo_ && !locationModeCombo_->currentData().toBool()) {
        rememberCustomLocation();
    }
    natalInput_ = input;
    natalLocationName_ = locationNameValue;
    hasNatalContext_ = true;
    updateLocationUi();
    if (active_) scheduleRecalculation(true);
}

void PlanetaryHoursController::clearNatalContext() {
    if (locationModeCombo_ && !locationModeCombo_->currentData().toBool()) {
        rememberCustomLocation();
    }
    hasNatalContext_ = false;
    natalInput_ = {};
    natalLocationName_.clear();
    updateLocationUi();
    if (active_) scheduleRecalculation(true);
}

void PlanetaryHoursController::setActive(bool active) {
    active_ = active;
    if (active_) {
        if (isLiveMode()) liveTimer_->start();
        recalculate(false);
    } else {
        if (liveTimer_) liveTimer_->stop();
        if (recalculateTimer_) recalculateTimer_->stop();
    }
}

void PlanetaryHoursController::refresh() {
    recalculate(true);
}

void PlanetaryHoursController::refreshNow() {
    if (!liveCheck_) return;
    if (!liveCheck_->isChecked()) {
        liveCheck_->setChecked(true);
    } else {
        recalculate(false);
    }
}

const PlanetaryHoursResult& PlanetaryHoursController::result() const { return result_; }
bool PlanetaryHoursController::hasResult() const { return result_.valid; }
bool PlanetaryHoursController::isLiveMode() const {
    return liveCheck_ && liveCheck_->isChecked();
}

int PlanetaryHoursController::selectedHourIndex() const {
    if (scheduleTable_ && scheduleTable_->currentRow() >= 0
        && scheduleTable_->currentRow() < result_.hours.size()) {
        return scheduleTable_->currentRow();
    }
    return result_.currentIndex;
}

PlanetaryHourInterval PlanetaryHoursController::selectedHour() const {
    const int index = selectedHourIndex();
    return index >= 0 && index < result_.hours.size()
        ? result_.hours[index] : PlanetaryHourInterval{};
}

QString PlanetaryHoursController::locationName() const { return activeLocationName_; }
QString PlanetaryHoursController::timezoneLabel() const { return activeTimezoneLabel_; }
double PlanetaryHoursController::latitude() const { return activeLatitude_; }
double PlanetaryHoursController::longitude() const { return activeLongitude_; }

void PlanetaryHoursController::buildFiltersUi() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(7, 7, 7, 7);
    layout->setSpacing(7);

    auto* title = new QLabel("Planetary Hours", page);
    title->setStyleSheet("font-weight: 600;");
    layout->addWidget(title);
    contextLabel_ = new QLabel("Use a natal chart location or enter a custom location.", page);
    contextLabel_->setWordWrap(true);
    contextLabel_->setObjectName("hintLabel");
    layout->addWidget(contextLabel_);

    auto* locationGroup = new QGroupBox("Location", page);
    auto* locationLayout = new QGridLayout(locationGroup);
    locationModeCombo_ = new QComboBox(locationGroup);
    locationModeCombo_->addItem("Use natal chart location", true);
    locationModeCombo_->addItem("Use custom location", false);
    locationEdit_ = new QLineEdit(locationGroup);
    locationEdit_->setPlaceholderText("Place name (optional if coordinates are known)");
    geocodeButton_ = new QPushButton("Locate", locationGroup);
    geocodeButton_->setToolTip("Find coordinates and timezone for the place name");
    timezoneEdit_ = new QLineEdit(locationGroup);
    timezoneEdit_->setPlaceholderText("e.g. Asia/Dhaka");
    timezoneStatusLabel_ = new QLabel(locationGroup);
    timezoneStatusLabel_->setObjectName("hintLabel");
    latitudeSpin_ = new QDoubleSpinBox(locationGroup);
    longitudeSpin_ = new QDoubleSpinBox(locationGroup);
    latitudeSpin_->setRange(-90.0, 90.0);
    longitudeSpin_->setRange(-180.0, 180.0);
    latitudeSpin_->setDecimals(6);
    longitudeSpin_->setDecimals(6);
    latitudeSpin_->setSingleStep(0.1);
    longitudeSpin_->setSingleStep(0.1);
    locationLayout->addWidget(locationModeCombo_, 0, 0, 1, 3);
    locationLayout->addWidget(new QLabel("Location", locationGroup), 1, 0);
    locationLayout->addWidget(locationEdit_, 1, 1);
    locationLayout->addWidget(geocodeButton_, 1, 2);
    locationLayout->addWidget(new QLabel("Timezone", locationGroup), 2, 0);
    locationLayout->addWidget(timezoneEdit_, 2, 1, 1, 2);
    locationLayout->addWidget(timezoneStatusLabel_, 3, 1, 1, 2);
    locationLayout->addWidget(new QLabel("Latitude", locationGroup), 4, 0);
    locationLayout->addWidget(latitudeSpin_, 4, 1, 1, 2);
    locationLayout->addWidget(new QLabel("Longitude", locationGroup), 5, 0);
    locationLayout->addWidget(longitudeSpin_, 5, 1, 1, 2);
    locationLayout->setColumnStretch(1, 1);
    layout->addWidget(locationGroup);

    auto* momentGroup = new QGroupBox("Reference Moment", page);
    auto* momentLayout = new QGridLayout(momentGroup);
    liveCheck_ = new QCheckBox("Live - follow current time", momentGroup);
    liveCheck_->setChecked(true);
    dateEdit_ = new QDateEdit(momentGroup);
    dateEdit_->setCalendarPopup(true);
    dateEdit_->setDisplayFormat("yyyy-MM-dd");
    dateEdit_->setDateRange(QDate(1800, 1, 1), QDate(2399, 12, 31));
    dateEdit_->setDate(QDate::currentDate());
    timeEdit_ = new QTimeEdit(momentGroup);
    timeEdit_->setDisplayFormat("h:mm:ss AP");
    timeEdit_->setTime(QTime::currentTime());
    auto* previousButton = new QPushButton("-1 Day", momentGroup);
    auto* nowButton = new QPushButton("Live Now", momentGroup);
    auto* nextButton = new QPushButton("+1 Day", momentGroup);
    previousButton->setToolTip("Browse the same local clock time on the previous date");
    nextButton->setToolTip("Browse the same local clock time on the next date");
    nowButton->setToolTip("Return to the live clock");
    momentLayout->addWidget(liveCheck_, 0, 0, 1, 3);
    momentLayout->addWidget(new QLabel("Date", momentGroup), 1, 0);
    momentLayout->addWidget(dateEdit_, 1, 1, 1, 2);
    momentLayout->addWidget(new QLabel("Time", momentGroup), 2, 0);
    momentLayout->addWidget(timeEdit_, 2, 1, 1, 2);
    momentLayout->addWidget(previousButton, 3, 0);
    momentLayout->addWidget(nowButton, 3, 1);
    momentLayout->addWidget(nextButton, 3, 2);
    layout->addWidget(momentGroup);

    auto* settingsGroup = new QGroupBox("Sunrise Calculation Settings", page);
    auto* settingsLayout = new QGridLayout(settingsGroup);
    elevationSpin_ = new QDoubleSpinBox(settingsGroup);
    elevationSpin_->setRange(-500.0, 10000.0);
    elevationSpin_->setDecimals(0);
    elevationSpin_->setSuffix(" m");
    pressureSpin_ = new QDoubleSpinBox(settingsGroup);
    pressureSpin_->setRange(0.0, 1100.0);
    pressureSpin_->setDecimals(1);
    pressureSpin_->setSuffix(" hPa");
    pressureSpin_->setSpecialValueText("Auto");
    temperatureSpin_ = new QDoubleSpinBox(settingsGroup);
    temperatureSpin_->setRange(-100.0, 100.0);
    temperatureSpin_->setDecimals(1);
    temperatureSpin_->setSuffix(" C");
    temperatureSpin_->setValue(15.0);
    settingsLayout->addWidget(new QLabel("Elevation", settingsGroup), 0, 0);
    settingsLayout->addWidget(elevationSpin_, 0, 1);
    settingsLayout->addWidget(new QLabel("Pressure", settingsGroup), 1, 0);
    settingsLayout->addWidget(pressureSpin_, 1, 1);
    settingsLayout->addWidget(new QLabel("Temperature", settingsGroup), 2, 0);
    settingsLayout->addWidget(temperatureSpin_, 2, 1);
    layout->addWidget(settingsGroup);

    auto* runGroup = new QGroupBox("Calculation", page);
    auto* runLayout = new QVBoxLayout(runGroup);
    calculateButton_ = new QPushButton("Refresh Calculation", runGroup);
    calculateButton_->setToolTip("Recalculate sunrise, sunset and all 24 planetary hours");
    statusLabel_ = new QLabel("Ready", runGroup);
    statusLabel_->setObjectName("hintLabel");
    statusLabel_->setWordWrap(true);
    auto* method = new QLabel(
        "Results update automatically. Sunrise/set uses the apparent upper solar limb with refraction and a standard horizon.",
        runGroup);
    method->setObjectName("hintLabel");
    method->setWordWrap(true);
    runLayout->addWidget(calculateButton_);
    runLayout->addWidget(statusLabel_);
    runLayout->addWidget(method);
    layout->addWidget(runGroup);
    layout->addStretch();

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    filtersRoot_ = scroll;

    connect(locationModeCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        if (!updatingControls_ && locationModeCombo_->currentData().toBool()) {
            customLocationName_ = locationEdit_->text().trimmed();
            customTimezone_ = timezoneEdit_->text().trimmed();
            customLatitude_ = latitudeSpin_->value();
            customLongitude_ = longitudeSpin_->value();
        }
        updateLocationUi();
        if (!updatingControls_) handleInputChanged(true);
    });
    connect(locationEdit_, &QLineEdit::editingFinished, this, [this]() {
        rememberCustomLocation();
        handleInputChanged(true);
    });
    connect(geocodeButton_, &QPushButton::clicked,
            this, &PlanetaryHoursController::handleGeocode);
    connect(timezoneEdit_, &QLineEdit::textChanged,
            this, &PlanetaryHoursController::updateTimezoneStatus);
    connect(timezoneEdit_, &QLineEdit::editingFinished, this, [this]() {
        rememberCustomLocation();
        handleInputChanged(true);
    });
    connect(latitudeSpin_, &QDoubleSpinBox::valueChanged, this, [this]() {
        if (updatingControls_) return;
        rememberCustomLocation();
        handleInputChanged(true);
    });
    connect(longitudeSpin_, &QDoubleSpinBox::valueChanged, this, [this]() {
        if (updatingControls_) return;
        rememberCustomLocation();
        handleInputChanged(true);
    });
    connect(elevationSpin_, &QDoubleSpinBox::valueChanged, this, [this]() {
        if (!updatingControls_) handleInputChanged(true);
    });
    connect(pressureSpin_, &QDoubleSpinBox::valueChanged, this, [this]() {
        if (!updatingControls_) handleInputChanged(true);
    });
    connect(temperatureSpin_, &QDoubleSpinBox::valueChanged, this, [this]() {
        if (!updatingControls_) handleInputChanged(true);
    });
    connect(liveCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        updateMomentUi();
        if (checked && active_) liveTimer_->start();
        else if (liveTimer_) liveTimer_->stop();
        if (!updatingControls_) recalculate(false);
    });
    connect(dateEdit_, &QDateEdit::dateChanged, this, [this]() {
        if (!updatingControls_) scheduleRecalculation(true);
    });
    connect(timeEdit_, &QTimeEdit::timeChanged, this, [this]() {
        if (!updatingControls_) scheduleRecalculation(false);
    });

    const auto browseDay = [this](int days) {
        updatingControls_ = true;
        {
            const QSignalBlocker liveBlocker(liveCheck_);
            const QSignalBlocker dateBlocker(dateEdit_);
            liveCheck_->setChecked(false);
            dateEdit_->setDate(dateEdit_->date().addDays(days));
        }
        updatingControls_ = false;
        if (liveTimer_) liveTimer_->stop();
        updateMomentUi();
        recalculate(true);
    };
    connect(previousButton, &QPushButton::clicked, this, [browseDay]() { browseDay(-1); });
    connect(nextButton, &QPushButton::clicked, this, [browseDay]() { browseDay(1); });
    connect(nowButton, &QPushButton::clicked, this, &PlanetaryHoursController::refreshNow);
    connect(calculateButton_, &QPushButton::clicked, this, [this]() { recalculate(true); });
}

void PlanetaryHoursController::buildWorkspaceUi() {
    workspaceRoot_ = new QWidget();
    auto* layout = new QVBoxLayout(workspaceRoot_);
    layout->setContentsMargins(9, 9, 9, 9);
    layout->setSpacing(8);

    auto* header = new QWidget(workspaceRoot_);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    planetaryDateLabel_ = new QLabel("Planetary day", header);
    planetaryDateLabel_->setStyleSheet("font-weight: 600;");
    modeLabel_ = new QLabel("LIVE", header);
    modeLabel_->setAlignment(Qt::AlignCenter);
    modeLabel_->setMinimumWidth(90);
    copyButton_ = new QPushButton("Copy Schedule", header);
    copyButton_->setEnabled(false);
    headerLayout->addWidget(planetaryDateLabel_);
    headerLayout->addStretch();
    headerLayout->addWidget(modeLabel_);
    headerLayout->addWidget(copyButton_);
    layout->addWidget(header);

    auto* currentCard = new QFrame(workspaceRoot_);
    currentCard->setObjectName("dataPanel");
    currentCard->setFrameShape(QFrame::StyledPanel);
    auto* currentLayout = new QGridLayout(currentCard);
    rulerLabel_ = new QLabel("-", currentCard);
    QFont rulerFont = rulerLabel_->font();
    rulerFont.setPointSize(rulerFont.pointSize() + 8);
    rulerFont.setBold(true);
    rulerLabel_->setFont(rulerFont);
    hourLabel_ = new QLabel("No planetary hour calculated", currentCard);
    hourLabel_->setStyleSheet("font-weight: 600;");
    rangeLabel_ = new QLabel("-", currentCard);
    remainingLabel_ = new QLabel("-", currentCard);
    meaningLabel_ = new QLabel(currentCard);
    meaningLabel_->setWordWrap(true);
    meaningLabel_->setObjectName("hintLabel");
    hourProgress_ = new QProgressBar(currentCard);
    hourProgress_->setRange(0, 1000);
    hourProgress_->setValue(0);
    hourProgress_->setTextVisible(true);
    hourProgress_->setFormat("%p% elapsed");
    currentLayout->addWidget(rulerLabel_, 0, 0, 3, 1);
    currentLayout->addWidget(hourLabel_, 0, 1);
    currentLayout->addWidget(rangeLabel_, 1, 1);
    currentLayout->addWidget(remainingLabel_, 2, 1);
    currentLayout->addWidget(hourProgress_, 3, 0, 1, 2);
    currentLayout->addWidget(meaningLabel_, 4, 0, 1, 2);
    currentLayout->setColumnStretch(1, 1);
    layout->addWidget(currentCard);

    scheduleTable_ = new QTableWidget(workspaceRoot_);
    scheduleTable_->setColumnCount(7);
    scheduleTable_->setHorizontalHeaderLabels({
        "#", "Period", "Ruler", "Starts", "Ends", "Duration", "Relation",
    });
    scheduleTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    scheduleTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    scheduleTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    scheduleTable_->setAlternatingRowColors(false);
    scheduleTable_->setWordWrap(false);
    scheduleTable_->setShowGrid(false);
    scheduleTable_->verticalHeader()->setVisible(false);
    scheduleTable_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    scheduleTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    scheduleTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    scheduleTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    scheduleTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    scheduleTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    scheduleTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    scheduleTable_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    layout->addWidget(scheduleTable_, 1);

    connect(copyButton_, &QPushButton::clicked, this, &PlanetaryHoursController::copySchedule);
    connect(scheduleTable_, &QTableWidget::itemSelectionChanged,
            this, &PlanetaryHoursController::selectedHourChanged);
}

void PlanetaryHoursController::loadSettings() {
    QSettings settings;
    customLocationName_ = settings.value("planetary_hours/custom_location").toString();
    customTimezone_ = settings.value("planetary_hours/custom_timezone",
                                     QString::fromUtf8(QTimeZone::systemTimeZoneId())).toString();
    customLatitude_ = settings.value("planetary_hours/custom_latitude", 0.0).toDouble();
    customLongitude_ = settings.value("planetary_hours/custom_longitude", 0.0).toDouble();

    updatingControls_ = true;
    locationModeCombo_->setCurrentIndex(
        std::clamp(settings.value("planetary_hours/location_mode", 0).toInt(), 0, 1));
    liveCheck_->setChecked(settings.value("planetary_hours/live", true).toBool());
    const QDate savedDate = settings.value("planetary_hours/date", QDate::currentDate()).toDate();
    const QTime savedTime = settings.value("planetary_hours/time", QTime::currentTime()).toTime();
    if (savedDate.isValid()) dateEdit_->setDate(savedDate);
    if (savedTime.isValid()) timeEdit_->setTime(savedTime);
    elevationSpin_->setValue(settings.value("planetary_hours/elevation_m", 0.0).toDouble());
    pressureSpin_->setValue(settings.value("planetary_hours/pressure_hpa", 0.0).toDouble());
    temperatureSpin_->setValue(settings.value("planetary_hours/temperature_c", 15.0).toDouble());
    updatingControls_ = false;
}

void PlanetaryHoursController::saveSettings() const {
    QSettings settings;
    settings.setValue("planetary_hours/location_mode",
                      locationModeCombo_ ? locationModeCombo_->currentIndex() : 0);
    settings.setValue("planetary_hours/custom_location", customLocationName_);
    settings.setValue("planetary_hours/custom_timezone", customTimezone_);
    settings.setValue("planetary_hours/custom_latitude", customLatitude_);
    settings.setValue("planetary_hours/custom_longitude", customLongitude_);
    settings.setValue("planetary_hours/live", isLiveMode());
    if (dateEdit_) settings.setValue("planetary_hours/date", dateEdit_->date());
    if (timeEdit_) settings.setValue("planetary_hours/time", timeEdit_->time());
    if (elevationSpin_) settings.setValue("planetary_hours/elevation_m", elevationSpin_->value());
    if (pressureSpin_) settings.setValue("planetary_hours/pressure_hpa", pressureSpin_->value());
    if (temperatureSpin_) settings.setValue("planetary_hours/temperature_c", temperatureSpin_->value());
}

void PlanetaryHoursController::rememberCustomLocation() {
    if (!locationModeCombo_ || locationModeCombo_->currentData().toBool()) return;
    customLocationName_ = locationEdit_->text().trimmed();
    customTimezone_ = timezoneEdit_->text().trimmed();
    customLatitude_ = latitudeSpin_->value();
    customLongitude_ = longitudeSpin_->value();
}

void PlanetaryHoursController::updateLocationUi() {
    if (!locationModeCombo_) return;
    const bool useNatal = locationModeCombo_->currentData().toBool();
    updatingControls_ = true;
    {
        const QSignalBlocker b1(locationEdit_);
        const QSignalBlocker b2(timezoneEdit_);
        const QSignalBlocker b3(latitudeSpin_);
        const QSignalBlocker b4(longitudeSpin_);
        if (useNatal) {
            if (hasNatalContext_) {
                locationEdit_->setText(natalLocationName_);
                timezoneEdit_->setText(natalInput_.timezone);
                latitudeSpin_->setValue(natalInput_.latitude);
                longitudeSpin_->setValue(natalInput_.longitude);
                contextLabel_->setText(QString("Using natal chart location: %1")
                    .arg(natalLocationName_.isEmpty() ? "Unnamed chart location" : natalLocationName_));
            } else {
                locationEdit_->clear();
                timezoneEdit_->clear();
                latitudeSpin_->setValue(0.0);
                longitudeSpin_->setValue(0.0);
                contextLabel_->setText("Load a natal chart or choose a custom location.");
            }
        } else {
            if (customTimezone_.isEmpty()) {
                customTimezone_ = QString::fromUtf8(QTimeZone::systemTimeZoneId());
            }
            locationEdit_->setText(customLocationName_);
            timezoneEdit_->setText(customTimezone_);
            latitudeSpin_->setValue(customLatitude_);
            longitudeSpin_->setValue(customLongitude_);
            contextLabel_->setText(
                "Custom location. Enter coordinates directly or use Locate to fill them from a place name.");
        }
    }
    updatingControls_ = false;
    const bool editable = !useNatal;
    locationEdit_->setEnabled(editable);
    geocodeButton_->setEnabled(editable && network_);
    timezoneEdit_->setEnabled(editable);
    latitudeSpin_->setEnabled(editable);
    longitudeSpin_->setEnabled(editable);
    updateTimezoneStatus();
}

void PlanetaryHoursController::updateMomentUi() {
    const bool manual = !isLiveMode();
    dateEdit_->setEnabled(manual);
    timeEdit_->setEnabled(manual);
    if (hourProgress_) hourProgress_->setVisible(!manual);
    if (modeLabel_) {
        modeLabel_->setText(manual ? "SNAPSHOT" : "LIVE");
        modeLabel_->setStyleSheet(manual
            ? "font-weight: 600; color: #8a5a00;"
            : "font-weight: 600; color: #167a2f;");
        modeLabel_->setToolTip(manual
            ? "The table is evaluated at the selected fixed date and time."
            : "The reference moment follows the current clock every second.");
    }
}

void PlanetaryHoursController::updateTimezoneStatus() {
    if (!timezoneStatusLabel_ || !timezoneEdit_) return;
    QTimeZone timezone;
    QString normalized;
    QString error;
    if (parseTimezoneInput(timezoneEdit_->text().trimmed(), &timezone, &normalized, &error)) {
        timezoneStatusLabel_->setText(QString("OK - %1").arg(normalized));
        timezoneStatusLabel_->setStyleSheet("color: #16823b;");
        timezoneEdit_->setToolTip(QString("Valid timezone: %1").arg(normalized));
    } else {
        timezoneStatusLabel_->setText(error.isEmpty() ? "Invalid timezone" : error);
        timezoneStatusLabel_->setStyleSheet("color: #b3261e;");
        timezoneEdit_->setToolTip(timezoneStatusLabel_->text());
    }
}

void PlanetaryHoursController::handleInputChanged(bool solarInputsChanged) {
    if (updatingControls_) return;
    updateTimezoneStatus();
    scheduleRecalculation(solarInputsChanged);
}

void PlanetaryHoursController::scheduleRecalculation(bool forceSolarCalculation) {
    pendingForceSolarCalculation_ = pendingForceSolarCalculation_ || forceSolarCalculation;
    if (active_ && recalculateTimer_) recalculateTimer_->start();
}

bool PlanetaryHoursController::resolveContext(QString* locationNameValue,
                                               QString* timezoneLabelValue,
                                               double* latitudeValue,
                                               double* longitudeValue,
                                               QString* error) const {
    const bool useNatal = locationModeCombo_ && locationModeCombo_->currentData().toBool();
    if (useNatal && !hasNatalContext_) {
        if (error) *error = "Load a natal chart or choose a custom location.";
        return false;
    }
    const QString name = useNatal ? natalLocationName_ : locationEdit_->text().trimmed();
    const QString timezone = useNatal ? natalInput_.timezone : timezoneEdit_->text().trimmed();
    const double latitudeRaw = useNatal ? natalInput_.latitude : latitudeSpin_->value();
    const double longitudeRaw = useNatal ? natalInput_.longitude : longitudeSpin_->value();
    if (timezone.isEmpty()) {
        if (error) *error = "Enter a valid timezone for the selected location.";
        return false;
    }
    if (!std::isfinite(latitudeRaw) || !std::isfinite(longitudeRaw)) {
        if (error) *error = "Enter valid latitude and longitude values.";
        return false;
    }
    if (locationNameValue) {
        *locationNameValue = name.isEmpty()
            ? coordinateLocationName(latitudeRaw, longitudeRaw) : name;
    }
    if (timezoneLabelValue) *timezoneLabelValue = timezone;
    if (latitudeValue) *latitudeValue = latitudeRaw;
    if (longitudeValue) *longitudeValue = longitudeRaw;
    return true;
}

QDateTime PlanetaryHoursController::selectedMoment(const QString& timezoneLabelValue,
                                                   QString* normalizedTimezone,
                                                   QString* error) {
    QTimeZone timezone;
    QString normalized;
    QString timezoneError;
    if (!parseTimezoneInput(timezoneLabelValue, &timezone, &normalized, &timezoneError)) {
        if (error) *error = timezoneError;
        return {};
    }
    QDateTime moment;
    if (isLiveMode()) {
        moment = QDateTime::currentDateTimeUtc().toTimeZone(timezone);
        updatingControls_ = true;
        {
            const QSignalBlocker b1(dateEdit_);
            const QSignalBlocker b2(timeEdit_);
            dateEdit_->setDate(moment.date());
            timeEdit_->setTime(moment.time());
        }
        updatingControls_ = false;
    } else {
        moment = QDateTime(dateEdit_->date(), timeEdit_->time(), timezone,
                           QDateTime::TransitionResolution::Reject);
    }
    if (!moment.isValid()) {
        if (error) {
            *error = "The selected local time is missing or ambiguous because of a timezone clock change. Choose an unambiguous time.";
        }
        return {};
    }
    if (normalizedTimezone) *normalizedTimezone = normalized;
    return moment;
}

PlanetaryHoursCalculationOptions PlanetaryHoursController::calculationOptions() const {
    PlanetaryHoursCalculationOptions options;
    if (elevationSpin_) options.elevationMeters = elevationSpin_->value();
    if (pressureSpin_) options.pressureHPa = pressureSpin_->value();
    if (temperatureSpin_) options.temperatureC = temperatureSpin_->value();
    return options;
}

void PlanetaryHoursController::recalculate(bool forceSolarCalculation) {
    if (recalculateTimer_) recalculateTimer_->stop();
    pendingForceSolarCalculation_ = false;

    QString location;
    QString timezone;
    QString contextError;
    double latitudeValue = 0.0;
    double longitudeValue = 0.0;
    if (!resolveContext(&location, &timezone, &latitudeValue, &longitudeValue, &contextError)) {
        showError(contextError);
        return;
    }
    QString normalizedTimezone;
    const QDateTime moment = selectedMoment(timezone, &normalizedTimezone, &contextError);
    if (!moment.isValid()) {
        showError(contextError);
        return;
    }
    if (!swe_ || !swe_->isLoaded()) {
        showError("Swiss Ephemeris is unavailable; planetary hours cannot be calculated.");
        return;
    }

    const PlanetaryHoursCalculationOptions options = calculationOptions();
    const bool sameOptions = result_.valid
        && std::fabs(result_.options.elevationMeters - options.elevationMeters) < 1e-9
        && std::fabs(result_.options.pressureHPa - options.pressureHPa) < 1e-9
        && std::fabs(result_.options.temperatureC - options.temperatureC) < 1e-9;
    const bool sameContext = result_.valid && sameOptions
        && activeTimezoneLabel_ == normalizedTimezone
        && std::fabs(activeLatitude_ - latitudeValue) < 1e-9
        && std::fabs(activeLongitude_ - longitudeValue) < 1e-9;
    const bool withinSchedule = sameContext
        && moment >= result_.sunrise && moment < result_.nextSunrise;
    activeLocationName_ = location;
    activeTimezoneLabel_ = normalizedTimezone;
    activeLatitude_ = latitudeValue;
    activeLongitude_ = longitudeValue;

    if (!forceSolarCalculation && withinSchedule) {
        updatePlanetaryHoursMoment(&result_, moment);
        lastErrorMessage_.clear();
        statusLabel_->setText(isLiveMode()
            ? QString("Live - updated at %1").arg(moment.toString("h:mm:ss AP"))
            : QString("Snapshot at %1").arg(moment.toString("yyyy-MM-dd h:mm:ss AP")));
        renderResult();
        emit resultChanged();
        return;
    }

    PlanetaryHoursResult calculated;
    QString calculationError;
    if (!calculatePlanetaryHours(*swe_, moment, normalizedTimezone,
                                 latitudeValue, longitudeValue, options,
                                 &calculated, &calculationError)) {
        showError(calculationError);
        return;
    }
    result_ = calculated;
    lastErrorMessage_.clear();
    statusLabel_->setText(isLiveMode()
        ? QString("Live calculation updated at %1").arg(moment.toString("h:mm:ss AP"))
        : QString("Snapshot calculated for %1").arg(moment.toString("yyyy-MM-dd h:mm:ss AP")));
    renderResult();
    emit resultChanged();
}

void PlanetaryHoursController::showError(const QString& message) {
    if (!result_.valid && lastErrorMessage_ == message) return;
    result_ = {};
    lastRenderedReferenceIndex_ = -1;
    renderedScheduleKey_.clear();
    lastErrorMessage_ = message;
    result_.error = message;
    if (statusLabel_) statusLabel_->setText(message);
    if (planetaryDateLabel_) planetaryDateLabel_->setText("Planetary day unavailable");
    if (rulerLabel_) rulerLabel_->setText("-");
    if (hourLabel_) hourLabel_->setText("No planetary hour calculated");
    if (rangeLabel_) rangeLabel_->setText("-");
    if (remainingLabel_) remainingLabel_->setText(message);
    if (meaningLabel_) meaningLabel_->clear();
    if (hourProgress_) {
        hourProgress_->setValue(0);
        hourProgress_->setFormat("No reference hour");
    }
    if (scheduleTable_) {
        const QSignalBlocker blocker(scheduleTable_);
        scheduleTable_->clearContents();
        scheduleTable_->setRowCount(0);
    }
    if (copyButton_) copyButton_->setEnabled(false);
    emit resultChanged();
    emit statusMessage(message);
}

QString PlanetaryHoursController::stateForHour(int row) const {
    if (row < 0 || row >= result_.hours.size()) return {};
    if (row == result_.currentIndex) return isLiveMode() ? "LIVE NOW" : "SELECTED TIME";
    if (result_.hours[row].endLocal <= result_.momentLocal) {
        return isLiveMode() ? "Past" : "Earlier";
    }
    return isLiveMode() ? "Upcoming" : "Later";
}

void PlanetaryHoursController::renderResult() {
    if (!result_.valid) return;
    updateMomentUi();
    const QString key = scheduleIdentity(result_, activeLatitude_, activeLongitude_);
    const bool sameSchedule = key == renderedScheduleKey_;
    const bool tableNeedsRefresh = !sameSchedule
        || result_.currentIndex != lastRenderedReferenceIndex_
        || isLiveMode() != lastRenderedLiveMode_;
    const int previousSelection = scheduleTable_ ? scheduleTable_->currentRow() : -1;
    const bool wasFollowingReference = previousSelection < 0
        || previousSelection == lastRenderedReferenceIndex_;

    planetaryDateLabel_->setText(QString("Planetary day: %1 - %2 day")
        .arg(result_.planetaryDate.toString("dddd, d MMMM yyyy"), result_.dayRuler));
    copyButton_->setEnabled(true);

    if (result_.currentIndex >= 0 && result_.currentIndex < result_.hours.size()) {
        const auto& referenceHour = result_.hours[result_.currentIndex];
        rulerLabel_->setText(referenceHour.ruler);
        hourLabel_->setText(QString("%1 - %2, hour %3 of 24")
            .arg(isLiveMode() ? "Current hour" : "Hour at selected time")
            .arg(hourPeriodLabel(referenceHour))
            .arg(referenceHour.sequence));
        rangeLabel_->setText(QString("%1 to %2")
            .arg(referenceHour.startLocal.toString("ddd, d MMM h:mm:ss AP"),
                 referenceHour.endLocal.toString("ddd, d MMM h:mm:ss AP")));
        const qint64 duration = referenceHour.startLocal.msecsTo(referenceHour.endLocal);
        const qint64 elapsed = referenceHour.startLocal.msecsTo(result_.momentLocal);
        const qint64 remaining = result_.momentLocal.msecsTo(referenceHour.endLocal);
        if (isLiveMode()) {
            remainingLabel_->setText(QString("%1 remaining - %2 day ruler")
                .arg(durationText(remaining), result_.dayRuler));
        } else {
            remainingLabel_->setText(QString("Snapshot position: %1 elapsed, %2 until hour end")
                .arg(durationText(elapsed), durationText(remaining)));
        }
        meaningLabel_->setText(planetaryRulerMeaning(referenceHour.ruler));
        const int progress = duration > 0
            ? static_cast<int>(std::clamp<qint64>(elapsed * 1000 / duration, 0, 1000)) : 0;
        hourProgress_->setValue(progress);
        hourProgress_->setFormat(isLiveMode() ? "%p% elapsed (live)" : "%p% through selected hour");
    } else {
        rulerLabel_->setText("-");
        hourLabel_->setText("Reference moment is outside this planetary day");
        rangeLabel_->setText("-");
        remainingLabel_->setText("-");
        meaningLabel_->clear();
        hourProgress_->setValue(0);
        hourProgress_->setFormat("No reference hour");
    }

    if (!tableNeedsRefresh) {
        return;
    }

    const QBrush dayBrush(QColor(255, 211, 102, 35));
    const QBrush nightBrush(QColor(100, 149, 237, 30));
    const QSignalBlocker blocker(scheduleTable_);
    scheduleTable_->setRowCount(result_.hours.size());
    for (int row = 0; row < result_.hours.size(); ++row) {
        const auto& hour = result_.hours[row];
        scheduleTable_->setItem(row, 0, readOnlyItem(QString::number(hour.sequence)));
        scheduleTable_->setItem(row, 1, readOnlyItem(hourPeriodLabel(hour)));
        scheduleTable_->setItem(row, 2, readOnlyItem(hour.ruler));
        scheduleTable_->setItem(row, 3, readOnlyItem(hour.startLocal.toString("ddd, d MMM h:mm:ss AP")));
        scheduleTable_->setItem(row, 4, readOnlyItem(hour.endLocal.toString("ddd, d MMM h:mm:ss AP")));
        scheduleTable_->setItem(row, 5, readOnlyItem(durationText(hour.startLocal.msecsTo(hour.endLocal))));
        scheduleTable_->setItem(row, 6, readOnlyItem(stateForHour(row)));
        for (int column = 0; column < scheduleTable_->columnCount(); ++column) {
            auto* item = scheduleTable_->item(row, column);
            item->setBackground(hour.daytime ? dayBrush : nightBrush);
            if (row == result_.currentIndex) {
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
        }
    }

    int selection = result_.currentIndex;
    if (sameSchedule && !wasFollowingReference
        && previousSelection >= 0 && previousSelection < result_.hours.size()) {
        selection = previousSelection;
    }
    renderedScheduleKey_ = key;
    lastRenderedReferenceIndex_ = result_.currentIndex;
    lastRenderedLiveMode_ = isLiveMode();
    if (selection >= 0) {
        scheduleTable_->selectRow(selection);
        if (!sameSchedule) scheduleTable_->scrollToItem(scheduleTable_->item(selection, 0));
    }
}

void PlanetaryHoursController::copySchedule() {
    if (!result_.valid) return;
    QStringList lines;
    lines.push_back(QString("Planetary Hours - %1").arg(activeLocationName_));
    lines.push_back(QString("Coordinates: %1, %2")
        .arg(activeLatitude_, 0, 'f', 6)
        .arg(activeLongitude_, 0, 'f', 6));
    lines.push_back(QString("Timezone: %1").arg(activeTimezoneLabel_));
    lines.push_back(QString("Reference: %1 - %2")
        .arg(isLiveMode() ? "Live current time" : "Fixed snapshot",
             result_.momentLocal.toString("dddd, d MMMM yyyy h:mm:ss AP")));
    lines.push_back(QString("Planetary day: %1 - %2 day")
        .arg(result_.planetaryDate.toString("dddd, d MMMM yyyy"), result_.dayRuler));
    lines.push_back(QString("Sunrise: %1 | Sunset: %2 | Next sunrise: %3")
        .arg(result_.sunrise.toString("yyyy-MM-dd h:mm:ss AP"),
             result_.sunset.toString("yyyy-MM-dd h:mm:ss AP"),
             result_.nextSunrise.toString("yyyy-MM-dd h:mm:ss AP")));
    lines.push_back(QString("Elevation: %1 m | Pressure: %2 | Temperature: %3 C")
        .arg(result_.options.elevationMeters, 0, 'f', 0)
        .arg(pressureText(result_.options))
        .arg(result_.options.temperatureC, 0, 'f', 1));
    lines.push_back("Method: apparent upper solar limb with refraction; standard horizon");
    if (!result_.warnings.isEmpty()) {
        lines.push_back(QString("Warnings: %1").arg(result_.warnings.join(" | ")));
    }
    lines.push_back(QString());
    lines.push_back("#\tPeriod\tRuler\tStarts\tEnds\tDuration\tRelation");
    for (int i = 0; i < result_.hours.size(); ++i) {
        const auto& hour = result_.hours[i];
        lines.push_back(QString("%1\t%2\t%3\t%4\t%5\t%6\t%7")
            .arg(hour.sequence)
            .arg(hourPeriodLabel(hour), hour.ruler,
                 hour.startLocal.toString("yyyy-MM-dd h:mm:ss AP"),
                 hour.endLocal.toString("yyyy-MM-dd h:mm:ss AP"),
                 durationText(hour.startLocal.msecsTo(hour.endLocal)), stateForHour(i)));
    }
    QApplication::clipboard()->setText(lines.join('\n'));
    emit statusMessage("Copied the complete planetary-hours schedule and calculation context.");
}

void PlanetaryHoursController::handleGeocode() {
    if (!network_) {
        emit statusMessage("Network manager is unavailable.");
        return;
    }
    const QString queryText = locationEdit_ ? locationEdit_->text().trimmed() : QString();
    if (queryText.isEmpty()) {
        emit statusMessage("Enter a place name before using Locate.");
        return;
    }

    QUrl url("https://nominatim.openstreetmap.org/search");
    QUrlQuery query;
    query.addQueryItem("format", "json");
    query.addQueryItem("limit", "1");
    query.addQueryItem("q", queryText);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "DracoVedCpp/0.1");
    geocodeButton_->setEnabled(false);
    statusLabel_->setText("Finding location...");
    QNetworkReply* reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        geocodeButton_->setEnabled(locationModeCombo_ && !locationModeCombo_->currentData().toBool());
        if (reply->error() != QNetworkReply::NoError) {
            const QString message = QString("Location lookup failed: %1").arg(reply->errorString());
            statusLabel_->setText(message);
            emit statusMessage(message);
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            statusLabel_->setText("Unable to read the location lookup response.");
            emit statusMessage(statusLabel_->text());
            return;
        }
        const QJsonArray results = document.array();
        if (results.isEmpty() || !results.first().isObject()) {
            statusLabel_->setText("No matching location was found.");
            emit statusMessage(statusLabel_->text());
            return;
        }
        const QJsonObject object = results.first().toObject();
        bool latitudeOk = false;
        bool longitudeOk = false;
        const double latitudeValue = object.value("lat").toString().toDouble(&latitudeOk);
        const double longitudeValue = object.value("lon").toString().toDouble(&longitudeOk);
        if (!latitudeOk || !longitudeOk) {
            statusLabel_->setText("The location service returned invalid coordinates.");
            emit statusMessage(statusLabel_->text());
            return;
        }
        customLatitude_ = latitudeValue;
        customLongitude_ = longitudeValue;
        const bool customMode = locationModeCombo_ && !locationModeCombo_->currentData().toBool();
        if (customMode) {
            updatingControls_ = true;
            {
                const QSignalBlocker b1(latitudeSpin_);
                const QSignalBlocker b2(longitudeSpin_);
                latitudeSpin_->setValue(latitudeValue);
                longitudeSpin_->setValue(longitudeValue);
            }
            updatingControls_ = false;
        }
        statusLabel_->setText("Coordinates found; detecting timezone...");
        fetchTimezoneForCoords(latitudeValue, longitudeValue);
    });
}

void PlanetaryHoursController::fetchTimezoneForCoords(double latitudeValue,
                                                      double longitudeValue) {
    if (!network_) return;
    QUrl url("https://api.open-meteo.com/v1/forecast");
    QUrlQuery query;
    query.addQueryItem("latitude", QString::number(latitudeValue, 'f', 6));
    query.addQueryItem("longitude", QString::number(longitudeValue, 'f', 6));
    query.addQueryItem("current", "temperature_2m");
    query.addQueryItem("timezone", "auto");
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "DracoVedCpp/0.1");
    QNetworkReply* reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const QString message = QString("Coordinates found, but timezone lookup failed: %1")
                .arg(reply->errorString());
            statusLabel_->setText(message);
            emit statusMessage(message);
            scheduleRecalculation(true);
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            statusLabel_->setText("Coordinates found, but the timezone response was invalid.");
            emit statusMessage(statusLabel_->text());
            scheduleRecalculation(true);
            return;
        }
        const QJsonObject object = document.object();
        QString timezone = object.value("timezone").toString().trimmed();
        if (timezone.isEmpty() && object.contains("utc_offset_seconds")) {
            const int totalSeconds = object.value("utc_offset_seconds").toInt();
            const QChar sign = totalSeconds < 0 ? '-' : '+';
            const int absoluteSeconds = std::abs(totalSeconds);
            timezone = QString("UTC%1%2:%3")
                .arg(sign)
                .arg(absoluteSeconds / 3600, 2, 10, QChar('0'))
                .arg((absoluteSeconds % 3600) / 60, 2, 10, QChar('0'));
        }
        if (!timezone.isEmpty()) {
            customTimezone_ = timezone;
            const bool customMode = locationModeCombo_ && !locationModeCombo_->currentData().toBool();
            if (customMode) {
                const QSignalBlocker blocker(timezoneEdit_);
                timezoneEdit_->setText(timezone);
                updateTimezoneStatus();
            }
            statusLabel_->setText(QString("Location ready - timezone %1").arg(timezone));
            emit statusMessage("Custom location coordinates and timezone updated.");
        } else {
            statusLabel_->setText("Coordinates found, but no timezone was returned.");
            emit statusMessage(statusLabel_->text());
        }
        if (locationModeCombo_ && !locationModeCombo_->currentData().toBool()) {
            scheduleRecalculation(true);
        }
    });
}

}  // namespace dracoved
