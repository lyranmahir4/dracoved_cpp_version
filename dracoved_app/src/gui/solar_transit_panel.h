#pragma once
#include "solar_transit_types.h"
#include "solar_transit_activity.h"
#include <QWidget>
#include <atomic>
#include <memory>
#include <optional>

class QLabel;
class QPushButton;
class QTreeWidget;
class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QProgressBar;
class QTableWidget;
class QTabWidget;
class QThread;
class QMenu;
class QDateEdit;

namespace dracoved {
class ChartWheelWidget;
class SolarTransitTimeline;

class SolarTransitPanel final : public QWidget {
public:
    using SourceProvider = std::function<bool(SolarTransitSource*, QString*)>;
    explicit SolarTransitPanel(SourceProvider provider, QWidget* parent = nullptr);
    ~SolarTransitPanel() override;
    void refreshSource();
    bool renderSelectedChart(ChartWheelWidget* wheel, const AspectOrbs& orbs, double maxOrb) const;
    std::function<void()> selectionChanged;
    std::function<void(bool)> expandResultsChanged;
    const NatalChart* selectedTransitChart() const;
    const SolarTransitSource* selectedSource() const;
private:
    void startSearch();
    void populateTargets();
    void renderResults();
    void selectEvent(int index);
    void copyResults(bool selectedOnly);
    void selectContact(const SolarTransitEvent& event, int index = -1);
    void updateEventMenu();
    void renderActivity();
    QWidget* createActivityPage();
    void showActivityPeriod(int row);
    void selectActivityContact(int row);
    void copyVisibleDates();
    SourceProvider provider_;
    SolarTransitSource source_;
    QString sourceKey_;
    bool sourceValid_ = false;
    std::shared_ptr<SolarTransitResult> result_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    QThread* worker_ = nullptr;
    QLabel* sourceLabel_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* detailLabel_ = nullptr;
    QWidget* filters_ = nullptr;
    QTreeWidget* bodies_ = nullptr;
    QTreeWidget* targets_ = nullptr;
    QComboBox* aspects_ = nullptr;
    QPushButton* eventFilter_ = nullptr;
    QMenu* eventMenu_ = nullptr;
    unsigned eventVisibility_ = 255;
    QComboBox* activityGroup_ = nullptr;
    QComboBox* activityAspect_ = nullptr;
    QComboBox* activityMode_ = nullptr;
    QComboBox* activityOrder_ = nullptr;
    QDateEdit* activityFrom_ = nullptr;
    QDateEdit* activityThrough_ = nullptr;
    QTableWidget* activityTable_ = nullptr;
    QTableWidget* activityDetails_ = nullptr;
    QLabel* activityLabel_ = nullptr;
    QVector<SolarActivityPeriod> activityPeriods_;
    int activitySelectedPeriod_ = -1;
    QDoubleSpinBox* orb_ = nullptr;
    QCheckBox* houses_ = nullptr;
    QPushButton* run_ = nullptr;
    QPushButton* stop_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTableWidget* dates_ = nullptr;
    QTabWidget* views_ = nullptr;
    NatalChart selectedTransit_;
    SolarTransitTimeline* timeline_ = nullptr;
    int selectedEvent_ = -1;
    std::optional<SolarTransitEvent> selectedContact_;
};
} // namespace dracoved
