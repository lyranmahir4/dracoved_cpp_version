#pragma once

#include "../core/chart_types.h"
#include "../core/zodiacal_releasing.h"

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QObject>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimeEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace dracoved {

QIcon zodiacalReleasingSignIcon(int signIndex, const QColor& color);

class ZodiacalReleasingController : public QObject {
    Q_OBJECT

public:
    explicit ZodiacalReleasingController(QObject* parent = nullptr);

    QWidget* filtersWidget() const;
    QWidget* workspaceWidget() const;

    void setNatalContext(const NatalInput& input, const NatalChart& chart,
                         const QString& locationName);
    void clearNatalContext();
    void setDefaults(const ZodiacalReleasingSettings& defaults);
    void setActive(bool active);
    void markStale();

    bool hasTimeline() const;
    bool isStale() const;
    bool hasSelectedPeriod() const;
    ZodiacalReleasingPeriod selectedPeriod() const;
    QVector<ZodiacalReleasingPeriod> referenceChain() const;
    ZodiacalReleasingTimeline timeline() const;
    ZodiacalReleasingSettings settings() const;
    QDateTime referenceMomentUtc() const;
    QString natalName() const;
    QString locationName() const;

signals:
    void selectionChanged();
    void timelineChanged();
    void statusMessage(const QString& message);

private:
    void buildFiltersUi();
    void buildWorkspaceUi();
    void syncControlsFromDefaults();
    void handleSettingsChanged();
    ZodiacalReleasingSettings settingsFromControls() const;
    void calculate();
    void populateTimeline();
    QTreeWidgetItem* makePeriodItem(const ZodiacalReleasingPeriod& period);
    void ensureChildren(QTreeWidgetItem* item);
    void handleSelectionChanged();
    void locateReferenceMoment(const QDateTime& utc, bool updateEditors);
    void locateEditedMoment();
    void locateNow();
    void locateBirth();
    void updateReferenceChain(const QDateTime& utc);
    void updateChainCards();
    QTreeWidgetItem* findTopLevelItem(const QString& stableId) const;
    QTreeWidgetItem* findDirectChild(QTreeWidgetItem* parent,
                                     const QString& stableId) const;
    void expandReferenceChain();
    void collapseAll();
    void copyReferenceChain();
    void copyVisibleSchedule();
    QString localDateTimeText(const QDateTime& utc) const;
    QString durationText(const ZodiacalReleasingPeriod& period) const;
    QString markerText(const ZodiacalReleasingPeriod& period) const;
    QString ageText(const QDateTime& utc) const;
    QDateTime ageBoundaryUtc(int age) const;
    bool resolveLotLongitudes(double* fortune, double* spirit, double* eros,
                              bool* hasFortune, bool* hasSpirit,
                              bool* hasEros) const;
    void showEmptyState(const QString& message);

    QWidget* filtersRoot_ = nullptr;
    QWidget* workspaceRoot_ = nullptr;
    QLabel* contextLabel_ = nullptr;
    QComboBox* releasePointCombo_ = nullptr;
    QComboBox* timeKeyCombo_ = nullptr;
    QComboBox* capricornCombo_ = nullptr;
    QCheckBox* sameSignRuleCheck_ = nullptr;
    QSpinBox* startAgeSpin_ = nullptr;
    QSpinBox* endAgeSpin_ = nullptr;
    QComboBox* maximumLevelCombo_ = nullptr;
    QPushButton* calculateButton_ = nullptr;
    QLabel* calculationStatusLabel_ = nullptr;
    QDateEdit* referenceDateEdit_ = nullptr;
    QTimeEdit* referenceTimeEdit_ = nullptr;
    QPushButton* locateButton_ = nullptr;
    QPushButton* nowButton_ = nullptr;
    QPushButton* birthButton_ = nullptr;
    QLabel* methodLabel_ = nullptr;
    QLabel* methodSignIconLabel_ = nullptr;
    QLabel* methodSuffixLabel_ = nullptr;
    QLabel* chainIconLabels_[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel* chainLabels_[4] = {nullptr, nullptr, nullptr, nullptr};
    QTreeWidget* periodsTree_ = nullptr;
    QPushButton* expandCurrentButton_ = nullptr;
    QPushButton* collapseButton_ = nullptr;
    QPushButton* copyChainButton_ = nullptr;
    QPushButton* copyScheduleButton_ = nullptr;

    NatalInput natalInput_;
    NatalChart natalChart_;
    QString natalLocationName_;
    ZodiacalReleasingSettings defaults_;
    ZodiacalReleasingTimeline timeline_;
    QVector<ZodiacalReleasingPeriod> referenceChain_;
    QHash<QString, ZodiacalReleasingPeriod> periodById_;
    QDateTime referenceMomentUtc_;
    QDateTime displayStartUtc_;
    bool hasNatalContext_ = false;
    bool active_ = false;
    bool stale_ = true;
    bool updatingControls_ = false;
    bool engineHealthy_ = false;
    QString engineError_;
};

}  // namespace dracoved
