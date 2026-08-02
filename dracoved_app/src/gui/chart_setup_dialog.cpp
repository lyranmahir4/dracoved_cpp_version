#include "chart_setup_dialog.h"

#include <QDateEdit>
#include <QTimeEdit>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QTimeZone>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <cmath>

namespace {

QString defaultTimezoneLabel() {
    QString tz = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    return tz.isEmpty() ? "UTC" : tz;
}

}  // namespace

namespace dracoved {

ChartSetupDialog::ChartSetupDialog(QNetworkAccessManager* net, QWidget* parent)
    : QDialog(parent),
      net_(net) {
    setWindowTitle("Chart Setup");
    setModal(true);
    setMinimumWidth(560);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto* formWidget = new QWidget(this);
    auto* grid = new QGridLayout(formWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);

    nameEdit_ = new QLineEdit(formWidget);
    dateEdit_ = new QDateEdit(QDate::currentDate(), formWidget);
    dateEdit_->setCalendarPopup(true);
    timeEdit_ = new QTimeEdit(QTime::currentTime(), formWidget);
    timezoneEdit_ = new QLineEdit(defaultTimezoneLabel(), formWidget);
    locationEdit_ = new QLineEdit(formWidget);
    genderCombo_ = new QComboBox(formWidget);
    latSpin_ = new QDoubleSpinBox(formWidget);
    lonSpin_ = new QDoubleSpinBox(formWidget);
    latSpin_->setRange(-90.0, 90.0);
    lonSpin_->setRange(-180.0, 180.0);
    latSpin_->setDecimals(6);
    lonSpin_->setDecimals(6);
    latSpin_->setValue(0.0);
    lonSpin_->setValue(0.0);

    houseCombo_ = new QComboBox(formWidget);
    houseCombo_->addItem("Whole Sign");
    houseCombo_->addItem("Placidus");
    nodeModeCombo_ = new QComboBox(formWidget);
    nodeModeCombo_->addItem("Use application default", -1);
    nodeModeCombo_->addItem("Mean Nodes", static_cast<int>(LunarNodeMode::MeanOnly));
    nodeModeCombo_->addItem("True Nodes", static_cast<int>(LunarNodeMode::TrueOnly));
    nodeModeCombo_->addItem("Show Both", static_cast<int>(LunarNodeMode::Both));
    nodePrimaryCombo_ = new QComboBox(formWidget);
    nodePrimaryCombo_->addItem("Mean primary", static_cast<int>(LunarNodeType::Mean));
    nodePrimaryCombo_->addItem("True primary", static_cast<int>(LunarNodeType::True));
    genderCombo_->addItem("Unspecified", static_cast<int>(Gender::Unspecified));
    genderCombo_->addItem("Male", static_cast<int>(Gender::Male));
    genderCombo_->addItem("Female", static_cast<int>(Gender::Female));

    geocodeButton_ = new QPushButton("Geocode", formWidget);
    auto* hintLabel = new QLabel("Geocoding uses Nominatim (OpenStreetMap).", formWidget);
    hintLabel->setObjectName("hintLabel");

    int row = 0;
    grid->addWidget(new QLabel("Name:", formWidget), row, 0);
    grid->addWidget(nameEdit_, row, 1);
    grid->addWidget(new QLabel("Date:", formWidget), row, 2);
    grid->addWidget(dateEdit_, row, 3);

    row++;
    grid->addWidget(new QLabel("Time:", formWidget), row, 0);
    grid->addWidget(timeEdit_, row, 1);
    grid->addWidget(new QLabel("Timezone:", formWidget), row, 2);
    grid->addWidget(timezoneEdit_, row, 3);

    row++;
    grid->addWidget(new QLabel("Location:", formWidget), row, 0);
    grid->addWidget(locationEdit_, row, 1);
    grid->addWidget(new QLabel("House system:", formWidget), row, 2);
    grid->addWidget(houseCombo_, row, 3);

    row++;
    grid->addWidget(new QLabel("Gender:", formWidget), row, 0);
    grid->addWidget(genderCombo_, row, 1);
    grid->addWidget(new QLabel("Latitude:", formWidget), row, 2);
    grid->addWidget(latSpin_, row, 3);

    row++;
    grid->addWidget(new QLabel("Longitude:", formWidget), row, 0);
    grid->addWidget(lonSpin_, row, 1);
    grid->addWidget(new QLabel("Lunar nodes:", formWidget), row, 2);
    grid->addWidget(nodeModeCombo_, row, 3);
    grid->setColumnStretch(1, 1);

    row++;
    grid->addWidget(new QLabel("If showing both:", formWidget), row, 2);
    grid->addWidget(nodePrimaryCombo_, row, 3);

    row++;
    grid->addWidget(geocodeButton_, row, 0);
    grid->addWidget(hintLabel, row, 1, 1, 3);

    mainLayout->addWidget(formWidget);

    auto* buttonRow = new QHBoxLayout();
    restoreButton_ = new QPushButton("Restore Defaults", this);
    cancelButton_ = new QPushButton("Cancel", this);
    applyButton_ = new QPushButton("Apply", this);
    applyButton_->setDefault(true);

    buttonRow->addWidget(restoreButton_);
    buttonRow->addStretch();
    buttonRow->addWidget(cancelButton_);
    buttonRow->addWidget(applyButton_);
    mainLayout->addLayout(buttonRow);

    connect(restoreButton_, &QPushButton::clicked, this, &ChartSetupDialog::applyDefaults);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyButton_, &QPushButton::clicked, this, &QDialog::accept);
    connect(geocodeButton_, &QPushButton::clicked, this, &ChartSetupDialog::handleGeocode);
    connect(nodeModeCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        const bool both = nodeModeCombo_ && nodeModeCombo_->currentData().toInt() == static_cast<int>(LunarNodeMode::Both);
        if (nodePrimaryCombo_) {
            nodePrimaryCombo_->setEnabled(both);
        }
    });

    applyDefaults();
}

void ChartSetupDialog::setDefaultHouseSystem(dracoved::HouseSystem system) {
    defaultHouseSystem_ = system;
    if (houseCombo_) {
        houseCombo_->setCurrentIndex(system == HouseSystem::Placidus ? 1 : 0);
    }
}

void ChartSetupDialog::setDefaultLunarNodePolicy(const dracoved::LunarNodePolicy& policy) {
    defaultLunarNodePolicy_ = policy;
    if (nodeModeCombo_) {
        nodeModeCombo_->setCurrentIndex(0);
        nodeModeCombo_->setToolTip(QString("Application default: %1").arg(lunarNodePolicySummary(policy)));
    }
    if (nodePrimaryCombo_) {
        const int index = nodePrimaryCombo_->findData(static_cast<int>(policy.primary));
        nodePrimaryCombo_->setCurrentIndex(index >= 0 ? index : 0);
        nodePrimaryCombo_->setEnabled(false);
    }
}

void ChartSetupDialog::applyDefaults() {
    if (nameEdit_) {
        nameEdit_->clear();
    }
    if (dateEdit_) {
        dateEdit_->setDate(QDate::currentDate());
    }
    if (timeEdit_) {
        timeEdit_->setTime(QTime::currentTime());
    }
    if (timezoneEdit_) {
        timezoneEdit_->setText(defaultTimezoneLabel());
    }
    if (locationEdit_) {
        locationEdit_->clear();
    }
    if (genderCombo_) {
        genderCombo_->setCurrentIndex(0);
    }
    if (latSpin_) {
        latSpin_->setValue(0.0);
    }
    if (lonSpin_) {
        lonSpin_->setValue(0.0);
    }
    setDefaultHouseSystem(defaultHouseSystem_);
    setDefaultLunarNodePolicy(defaultLunarNodePolicy_);
}

void ChartSetupDialog::setInput(const dracoved::NatalInput& input, const QString& locationName) {
    if (nameEdit_) {
        nameEdit_->setText(input.name);
    }
    if (dateEdit_ && input.date.isValid()) {
        dateEdit_->setDate(input.date);
    }
    if (timeEdit_) {
        timeEdit_->setTime(input.time.isValid() ? input.time : QTime::currentTime());
    }
    if (timezoneEdit_) {
        const QString tz = input.timezone.trimmed();
        timezoneEdit_->setText(tz.isEmpty() ? defaultTimezoneLabel() : tz);
    }
    if (locationEdit_) {
        locationEdit_->setText(locationName);
    }
    if (genderCombo_) {
        const int idx = genderCombo_->findData(static_cast<int>(input.gender));
        genderCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (latSpin_) {
        latSpin_->setValue(input.latitude);
    }
    if (lonSpin_) {
        lonSpin_->setValue(input.longitude);
    }
    setDefaultHouseSystem(input.houseSystem);
    if (nodeModeCombo_) {
        if (input.useDefaultLunarNodePolicy) {
            nodeModeCombo_->setCurrentIndex(0);
        } else {
            const int index = nodeModeCombo_->findData(static_cast<int>(input.lunarNodePolicy.mode));
            nodeModeCombo_->setCurrentIndex(index >= 0 ? index : 1);
        }
    }
    if (nodePrimaryCombo_) {
        const int index = nodePrimaryCombo_->findData(static_cast<int>(input.lunarNodePolicy.primary));
        nodePrimaryCombo_->setCurrentIndex(index >= 0 ? index : 0);
        nodePrimaryCombo_->setEnabled(input.lunarNodePolicy.mode == LunarNodeMode::Both
                                      && !input.useDefaultLunarNodePolicy);
    }
}

dracoved::NatalInput ChartSetupDialog::input() const {
    dracoved::NatalInput input;
    input.name = nameEdit_ ? nameEdit_->text().trimmed() : QString();
    input.date = dateEdit_ ? dateEdit_->date() : QDate::currentDate();
    input.time = timeEdit_ ? timeEdit_->time() : QTime::currentTime();
    input.timezone = timezoneEdit_ ? timezoneEdit_->text().trimmed() : defaultTimezoneLabel();
    input.gender = genderCombo_
        ? static_cast<Gender>(genderCombo_->currentData().toInt())
        : Gender::Unspecified;
    input.latitude = latSpin_ ? latSpin_->value() : 0.0;
    input.longitude = lonSpin_ ? lonSpin_->value() : 0.0;
    input.houseSystem = (houseCombo_ && houseCombo_->currentText().contains("Placidus", Qt::CaseInsensitive))
        ? dracoved::HouseSystem::Placidus
        : dracoved::HouseSystem::WholeSign;
    input.useDefaultLunarNodePolicy = !nodeModeCombo_ || nodeModeCombo_->currentData().toInt() < 0;
    if (input.useDefaultLunarNodePolicy) {
        input.lunarNodePolicy = defaultLunarNodePolicy_;
    } else {
        input.lunarNodePolicy.mode = static_cast<LunarNodeMode>(nodeModeCombo_->currentData().toInt());
        input.lunarNodePolicy.primary = nodePrimaryCombo_
            ? static_cast<LunarNodeType>(nodePrimaryCombo_->currentData().toInt())
            : LunarNodeType::Mean;
        if (input.lunarNodePolicy.mode == LunarNodeMode::MeanOnly) {
            input.lunarNodePolicy.primary = LunarNodeType::Mean;
        } else if (input.lunarNodePolicy.mode == LunarNodeMode::TrueOnly) {
            input.lunarNodePolicy.primary = LunarNodeType::True;
        }
    }
    return input;
}

QString ChartSetupDialog::locationName() const {
    return locationEdit_ ? locationEdit_->text().trimmed() : QString();
}

void ChartSetupDialog::handleGeocode() {
    if (!net_) {
        QMessageBox::warning(this, "Chart Setup", "Network manager not available.");
        return;
    }
    const QString queryText = locationEdit_ ? locationEdit_->text().trimmed() : QString();
    if (queryText.isEmpty()) {
        QMessageBox::warning(this, "Chart Setup", "Enter a place name to geocode.");
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
    auto* reply = net_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "Chart Setup", QString("Geocoding failed: %1").arg(reply->errorString()));
            return;
        }
        const auto payload = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            QMessageBox::warning(this, "Chart Setup", "Unable to parse geocoding response.");
            return;
        }
        const QJsonArray arr = doc.array();
        if (arr.isEmpty() || !arr[0].isObject()) {
            QMessageBox::warning(this, "Chart Setup", "No results found for that location.");
            return;
        }
        const QJsonObject obj = arr[0].toObject();
        bool okLat = false;
        bool okLon = false;
        const double lat = obj.value("lat").toString().toDouble(&okLat);
        const double lon = obj.value("lon").toString().toDouble(&okLon);
        if (!okLat || !okLon) {
            QMessageBox::warning(this, "Chart Setup", "Geocoding response missing coordinates.");
            return;
        }
        if (latSpin_) {
            latSpin_->setValue(lat);
        }
        if (lonSpin_) {
            lonSpin_->setValue(lon);
        }
        fetchTimezoneForCoords(lat, lon);
    });
}

void ChartSetupDialog::fetchTimezoneForCoords(double lat, double lon) {
    if (!net_) {
        return;
    }
    QUrl url("https://api.open-meteo.com/v1/forecast");
    QUrlQuery query;
    query.addQueryItem("latitude", QString::number(lat, 'f', 6));
    query.addQueryItem("longitude", QString::number(lon, 'f', 6));
    query.addQueryItem("current", "temperature_2m");
    query.addQueryItem("timezone", "auto");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "DracoVedCpp/0.1");
    auto* reply = net_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "Chart Setup", QString("Timezone lookup failed: %1").arg(reply->errorString()));
            return;
        }
        const auto payload = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, "Chart Setup", "Unable to parse timezone response.");
            return;
        }
        const QJsonObject obj = doc.object();
        const QString tzName = obj.value("timezone").toString().trimmed();
        if (!tzName.isEmpty()) {
            if (timezoneEdit_) {
                timezoneEdit_->setText(tzName);
            }
            return;
        }
        const int offsetSeconds = obj.value("utc_offset_seconds").toInt();
        if (offsetSeconds != 0 && timezoneEdit_) {
            const int totalMinutes = offsetSeconds / 60;
            const int hours = totalMinutes / 60;
            const int minutes = std::abs(totalMinutes % 60);
            const QString sign = hours >= 0 ? "+" : "-";
            const QString label = QString("UTC%1%2:%3")
                .arg(sign)
                .arg(QString::number(std::abs(hours)).rightJustified(2, '0'))
                .arg(QString::number(minutes).rightJustified(2, '0'));
            timezoneEdit_->setText(label);
        }
    });
}

}  // namespace dracoved
