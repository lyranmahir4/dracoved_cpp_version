#pragma once

#include <QDialog>

#include "../core/chart_types.h"

class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QTimeEdit;
class QNetworkAccessManager;

namespace dracoved {

class ChartSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit ChartSetupDialog(QNetworkAccessManager* net, QWidget* parent = nullptr);

    void setInput(const dracoved::NatalInput& input, const QString& locationName);
    void setDefaultHouseSystem(dracoved::HouseSystem system);
    void setDefaultLunarNodePolicy(const dracoved::LunarNodePolicy& policy);
    dracoved::NatalInput input() const;
    QString locationName() const;

private:
    void applyDefaults();
    void handleGeocode();
    void fetchTimezoneForCoords(double lat, double lon);

    QNetworkAccessManager* net_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QDateEdit* dateEdit_ = nullptr;
    QTimeEdit* timeEdit_ = nullptr;
    QLineEdit* timezoneEdit_ = nullptr;
    QLineEdit* locationEdit_ = nullptr;
    QComboBox* genderCombo_ = nullptr;
    QDoubleSpinBox* latSpin_ = nullptr;
    QDoubleSpinBox* lonSpin_ = nullptr;
    QComboBox* houseCombo_ = nullptr;
    QComboBox* nodeModeCombo_ = nullptr;
    QComboBox* nodePrimaryCombo_ = nullptr;
    QPushButton* geocodeButton_ = nullptr;
    QPushButton* restoreButton_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    dracoved::HouseSystem defaultHouseSystem_ = dracoved::HouseSystem::WholeSign;
    dracoved::LunarNodePolicy defaultLunarNodePolicy_;
};

}  // namespace dracoved
