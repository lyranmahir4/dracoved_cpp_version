#pragma once

#include "../core/chart_types.h"
#include "../core/planetary_hours.h"

#include <QObject>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTimeEdit;
class QTimer;
class QWidget;

namespace dracoved {

class PlanetaryHoursController : public QObject {
    Q_OBJECT

public:
    explicit PlanetaryHoursController(SwissEph* swe,
                                      QNetworkAccessManager* network,
                                      QObject* parent = nullptr);
    ~PlanetaryHoursController() override;

    QWidget* filtersWidget() const;
    QWidget* workspaceWidget() const;

    void setNatalContext(const NatalInput& input, const QString& locationName);
    void clearNatalContext();
    void setActive(bool active);
    void refresh();
    void refreshNow();

    const PlanetaryHoursResult& result() const;
    bool hasResult() const;
    bool isLiveMode() const;
    int selectedHourIndex() const;
    PlanetaryHourInterval selectedHour() const;
    QString locationName() const;
    QString timezoneLabel() const;
    double latitude() const;
    double longitude() const;

signals:
    void resultChanged();
    void selectedHourChanged();
    void statusMessage(const QString& message);

private:
    void buildFiltersUi();
    void buildWorkspaceUi();
    void loadSettings();
    void saveSettings() const;
    void rememberCustomLocation();
    void updateLocationUi();
    void updateMomentUi();
    void updateTimezoneStatus();
    void handleInputChanged(bool solarInputsChanged = true);
    void scheduleRecalculation(bool forceSolarCalculation = true);
    void recalculate(bool forceSolarCalculation = false);
    void showError(const QString& message);
    void renderResult();
    void copySchedule();
    void handleGeocode();
    void fetchTimezoneForCoords(double latitude, double longitude);
    bool resolveContext(QString* locationName, QString* timezoneLabel,
                        double* latitude, double* longitude, QString* error) const;
    QDateTime selectedMoment(const QString& timezoneLabel, QString* normalizedTimezone,
                             QString* error);
    PlanetaryHoursCalculationOptions calculationOptions() const;
    QString stateForHour(int row) const;

    SwissEph* swe_ = nullptr;
    QNetworkAccessManager* network_ = nullptr;
    QWidget* filtersRoot_ = nullptr;
    QWidget* workspaceRoot_ = nullptr;
    QComboBox* locationModeCombo_ = nullptr;
    QLineEdit* locationEdit_ = nullptr;
    QPushButton* geocodeButton_ = nullptr;
    QLineEdit* timezoneEdit_ = nullptr;
    QLabel* timezoneStatusLabel_ = nullptr;
    QDoubleSpinBox* latitudeSpin_ = nullptr;
    QDoubleSpinBox* longitudeSpin_ = nullptr;
    QDoubleSpinBox* elevationSpin_ = nullptr;
    QDoubleSpinBox* pressureSpin_ = nullptr;
    QDoubleSpinBox* temperatureSpin_ = nullptr;
    QLabel* contextLabel_ = nullptr;
    QCheckBox* liveCheck_ = nullptr;
    QDateEdit* dateEdit_ = nullptr;
    QTimeEdit* timeEdit_ = nullptr;
    QPushButton* calculateButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QLabel* planetaryDateLabel_ = nullptr;
    QLabel* modeLabel_ = nullptr;
    QLabel* rulerLabel_ = nullptr;
    QLabel* hourLabel_ = nullptr;
    QLabel* rangeLabel_ = nullptr;
    QLabel* remainingLabel_ = nullptr;
    QLabel* meaningLabel_ = nullptr;
    QProgressBar* hourProgress_ = nullptr;
    QTableWidget* scheduleTable_ = nullptr;
    QPushButton* copyButton_ = nullptr;
    QTimer* liveTimer_ = nullptr;
    QTimer* recalculateTimer_ = nullptr;

    NatalInput natalInput_;
    QString natalLocationName_;
    QString customLocationName_;
    QString customTimezone_;
    double customLatitude_ = 0.0;
    double customLongitude_ = 0.0;
    bool hasNatalContext_ = false;
    bool active_ = false;
    bool updatingControls_ = false;
    bool pendingForceSolarCalculation_ = false;
    int lastRenderedReferenceIndex_ = -1;
    bool lastRenderedLiveMode_ = true;
    QString renderedScheduleKey_;
    QString lastErrorMessage_;
    PlanetaryHoursResult result_;
    QString activeLocationName_;
    QString activeTimezoneLabel_;
    double activeLatitude_ = 0.0;
    double activeLongitude_ = 0.0;
};

}  // namespace dracoved
