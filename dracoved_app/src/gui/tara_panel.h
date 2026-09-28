#pragma once

#include "../core/chart_types.h"
#include <QWidget>
#include <QTimeZone>
#include <QSet>
#include <optional>
#include <functional>

class QDateEdit;
class QTimeEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QTableWidget;

namespace dracoved {
class SwissEph;
class TaraPanel final : public QWidget {
public:
    explicit TaraPanel(SwissEph* swe, QWidget* parent = nullptr);
    void setContext(const NatalInput& input, const NatalChart& chart);
    void setBirthFacts(const QString& facts) { birthFacts_ = facts; }
    void setActiveLords(const QStringList& lords, const QString& context, bool only);
    void setInspectionTime(qint64 utcMs);
    void inspectPlanet(const QString& name, qint64 utcMs);
    std::function<void(qint64)> onMomentCalculated;
private:
    void calculate();
    void invalidate();
    void filterRows();
    void copy();
    void setNow();
    SwissEph* swe_;
    QDateEdit* date_;
    QTimeEdit* time_;
    QComboBox* planet_;
    QPushButton* run_;
    QPushButton* copy_;
    QLabel* context_;
    QLabel* status_;
    QTableWidget* table_;
    NatalInput input_;
    SiderealAyanamsa ayanamsa_ = SiderealAyanamsa::Lahiri;
    QTimeZone zone_;
    int natalStar_ = -1;
    bool initialized_ = false;
    QString resultContext_;
    QString birthFacts_;
    QSet<QString> activeLords_;
    QString activeContext_;
    bool activeOnly_ = false;
    std::optional<qint64> resolvedUtc_;
};
} // namespace dracoved
