#pragma once

#include "../core/chart_types.h"
#include "../core/geodetic_equivalents.h"

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QLineEdit;
class QPushButton;
class QTimeEdit;
class QWidget;

namespace dracoved {

class AstroMapWidget;
class SwissEph;

struct GeodeticTransitBody {
    QString name;
    double longitude = 0.0;
    double dailySpeed = 0.0;
    bool retrograde = false;
};

struct GeodeticContact {
    QString body;
    QString angle;
    double bodyLongitude = 0.0;
    double angleLongitude = 0.0;
    double orb = 0.0;
    QString motion;
};

struct GeodeticMapSnapshot {
    bool valid = false;
    bool stale = true;
    QDateTime localMoment;
    QDateTime utcMoment;
    QString timezone;
    ZodiacSystem zodiacSystem = ZodiacSystem::Tropical;
    SiderealAyanamsa ayanamsa = SiderealAyanamsa::Lahiri;
    double referenceMeridian = 0.0;
    double obliquity = 23.4392911;
    int visibleLineCount = 0;
    QVector<GeodeticTransitBody> bodies;
    QStringList warnings;
};

class GeodeticEquivalentsController : public QObject {
    Q_OBJECT

public:
    explicit GeodeticEquivalentsController(
        SwissEph* swissEphemeris, QObject* parent = nullptr);

    QWidget* filtersWidget() const;
    QWidget* workspaceWidget() const;
    void setActive(bool active);

    bool hasResult() const;
    bool isStale() const;
    bool hasSelectedLocation() const;
    double selectedLatitude() const;
    double selectedLongitude() const;
    GeodeticAngles selectedAngles() const;
    QVector<GeodeticContact> selectedContacts() const;
    GeodeticMapSnapshot snapshot() const;

signals:
    void resultChanged();
    void statusMessage(const QString& message);

private:
    void buildFiltersUi();
    void buildWorkspaceUi();
    void markStale();
    void calculate();
    void adjustDays(int days);
    void setNow();
    void updateZodiacControls();
    void updateMapLines();
    void updateSelectedLocation();
    void clearSelectedLocation();
    void handleMapClicked(double latitude, double longitude);
    void handleMapHovered(double latitude, double longitude);
    void copyReport();
    QVector<GeodeticTransitBody> selectedBodies() const;
    QVector<GeodeticContact> contactsForAngles(
        const GeodeticAngles& angles) const;
    QString longitudeText(double longitude) const;
    QString coordinateText(double latitude, double longitude) const;
    QString methodSummary() const;

    SwissEph* swissEphemeris_ = nullptr;
    QWidget* filtersRoot_ = nullptr;
    QWidget* workspaceRoot_ = nullptr;
    AstroMapWidget* mapWidget_ = nullptr;
    QLabel* workspaceSummaryLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QDateEdit* dateEdit_ = nullptr;
    QTimeEdit* timeEdit_ = nullptr;
    QLineEdit* timezoneEdit_ = nullptr;
    QComboBox* zodiacCombo_ = nullptr;
    QComboBox* ayanamsaCombo_ = nullptr;
    QDoubleSpinBox* referenceMeridianSpin_ = nullptr;
    QListWidget* bodyList_ = nullptr;
    QCheckBox* mcCheck_ = nullptr;
    QCheckBox* icCheck_ = nullptr;
    QCheckBox* ascCheck_ = nullptr;
    QCheckBox* dscCheck_ = nullptr;
    QCheckBox* signsMcCheck_ = nullptr;
    QCheckBox* signsAscCheck_ = nullptr;
    QDoubleSpinBox* contactOrbSpin_ = nullptr;
    QPushButton* calculateButton_ = nullptr;
    QPushButton* copyButton_ = nullptr;

    GeodeticMapSnapshot snapshot_;
    GeodeticAngles selectedAngles_;
    QVector<GeodeticContact> selectedContacts_;
    double selectedLatitude_ = 0.0;
    double selectedLongitude_ = 0.0;
    bool active_ = false;
    bool hasSelectedLocation_ = false;
    bool updatingControls_ = false;
};

}  // namespace dracoved
