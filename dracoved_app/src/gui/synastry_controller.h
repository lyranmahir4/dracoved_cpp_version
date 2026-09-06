#pragma once

#include "../core/chart_types.h"
#include "../core/tropical_natal.h"

#include <QObject>
#include <QString>

class QComboBox;
class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QCheckBox;
class QWidget;

namespace dracoved {

class SwissEph;

// Stage 1 synastry: compares Person A (the chart currently active in the app)
// against Person B (manual birth data, or another saved profile).
//
// This controller deliberately owns NO chart wheel. The Synastry tab reuses the
// shared chart view the same way the Transits tab does, so it inherits all the
// existing theme, zoom and readability wiring for free. The controller owns the
// Person B panel and the computed Person B chart; MainWindow does the drawing.
class SynastryController : public QObject {
    Q_OBJECT

public:
    explicit SynastryController(SwissEph* swe,
                                QNetworkAccessManager* network,
                                QObject* parent = nullptr);

    QWidget* filtersWidget() const;

    // The ephemeris path is only resolved after setupUi() has already run, so it
    // arrives after construction. Mirrors ReturnFinderController::setRuntimePaths.
    void setEphePath(const QString& ephePath);

    // Person A is whatever chart is active in the app. Person B inherits A's
    // zodiac system and ayanamsa; without that the two charts would be compared
    // across different zodiacs, which would be meaningless.
    void setPersonA(const NatalInput& input, const QString& locationName);
    void clearPersonA();

    void setActive(bool active);

    bool hasPersonB() const;
    const NatalChart& personBChart() const;
    const NatalInput& personBInput() const;
    QString personBDisplayName() const;
    // Person B's stored location name. Not part of NatalInput, so it has to be
    // exposed separately for the summary panel to show it when B is on the
    // inner ring.
    QString personBLocation() const;
    QString personALabelText() const;

    // When swapped, Person B is drawn on the inner ring. The wheel rotates on
    // the inner chart's Ascendant, so this genuinely re-frames the comparison
    // rather than just relabelling it.
    bool isSwapped() const;

    void setDefaultLunarNodePolicy(const LunarNodePolicy& policy);
    void setDefaultHouseSystem(HouseSystem system);

    void refreshProfileList();

signals:
    // Person B, or the swap orientation, changed. MainWindow re-renders.
    void personBChanged();
    void statusMessage(const QString& message);
    // Asks MainWindow to persist Person B using its existing profile writer, so
    // the JSON format keeps a single owner.
    void saveProfileRequested(const NatalInput& input,
                              const QString& locationName,
                              const QString& suggestedName);

private:
    void buildFiltersUi();
    void updateSummary();
    // Shows the message in the panel's own status label AND forwards it to the
    // main window status bar. The panel label matters: it is where the user is
    // already looking when they click one of these buttons.
    void report(const QString& message);
    void loadSelectedProfile();
    void enterManualPersonB();
    void requestSavePersonB();
    void clearPersonB();
    bool computePersonB(NatalInput input, const QString& locationName,
                        const QString& label);

    SwissEph* swe_ = nullptr;
    QNetworkAccessManager* network_ = nullptr;
    TropicalNatalEngine engine_;

    QWidget* filtersRoot_ = nullptr;
    QComboBox* profileCombo_ = nullptr;
    QPushButton* loadProfileButton_ = nullptr;
    QPushButton* manualEntryButton_ = nullptr;
    QPushButton* saveProfileButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QCheckBox* swapCheck_ = nullptr;
    QLabel* personASummary_ = nullptr;
    QLabel* personBSummary_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    NatalInput personAInput_;
    QString personALocation_;
    bool hasPersonA_ = false;

    NatalInput personBInput_;
    QString personBLocation_;
    NatalChart personBChart_;
    QString personBLabel_;
    bool hasPersonB_ = false;

    LunarNodePolicy defaultLunarNodePolicy_;
    HouseSystem defaultHouseSystem_ = HouseSystem::WholeSign;
    bool swapped_ = false;
    bool active_ = false;
};

}  // namespace dracoved
