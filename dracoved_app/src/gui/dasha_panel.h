#pragma once
#include "../core/chart_types.h"
#include "../core/moorthi.h"
#include "../core/vimshottari.h"
#include <QWidget>
#include <QTimeZone>
#include <functional>
#include <optional>

class QDateEdit;
class QTimeEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

namespace dracoved {
class DashaPanel final : public QWidget {
public:
    explicit DashaPanel(SwissEph* swe, QWidget* parent = nullptr);
    void setContext(const NatalInput& input, const NatalChart& chart, const QString& birthFacts);
    void setInspectionTime(qint64 utcMs);
    qint64 inspectionMs() const { return inspectionMs_; }
    double yearDays() const;
    QStringList activeLords() const;
    QString activeSummary() const;
    std::function<void()> onStateChanged;
    std::function<void(QString, qint64)> onTransitRequested;
protected:
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;
private:
    struct Transit {
        QString name, roles;
        int lord = -1, body = -1;
        double offset = 0, longitude = 0;
        std::optional<MoorthiEntry> entry;
        QString lookupStatus;
    };
    void calculate(bool lookup = true);
    void invalidate();
    void navigate(int direction);
    void rebuildSchedule();
    void expandPeriod(QTreeWidgetItem* item);
    void jumpToActive();
    void renderActive();
    void applyColors();
    void startSnapshot();
    void lookupStep();
    void stopLookup();
    void renderSnapshot();
    void showTransitDetail();
    void copy(bool schedule);
    QString localTime(qint64 ms, bool precise = false) const;
    QString reportContext() const;
    SwissEph* swe_;
    NatalInput input_;
    SiderealAyanamsa ayanamsa_ = SiderealAyanamsa::Lahiri;
    QTimeZone zone_;
    double birthMoon_ = 0;
    qint64 birthMs_ = 0, inspectionMs_ = 0;
    bool hasContext_ = false, snapshotPending_ = false;
    std::optional<qint64> resolvedUtc_;
    QString birthFacts_;
    Vimshottari engine_;
    QVector<DashaPeriod> active_;
    QVector<Transit> transits_;
    QDateEdit* date_;
    QTimeEdit* time_;
    QComboBox* year_;
    QComboBox* level_;
    QPushButton* run_;
    QPushButton* copy_;
    QPushButton* stop_;
    QPushButton* openTransit_;
    QLabel* balance_;
    QLabel* status_;
    QLabel* detail_;
    QTreeWidget* schedule_ = nullptr;
    QTableWidget* activeTable_ = nullptr;
    QTableWidget* transitTable_ = nullptr;
    QTimer* timer_;
    int lookupRow_ = 0;
    double cursorJd_ = 0, minimumJd_ = 0;
};
} // namespace dracoved
