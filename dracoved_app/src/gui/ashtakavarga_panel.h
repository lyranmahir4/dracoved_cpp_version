#pragma once

#include "../core/ashtakavarga.h"
#include <QWidget>
#include <QTimeZone>
#include <functional>
#include <optional>

class QComboBox;
class QDateEdit;
class QTimeEdit;
class QTableWidget;
class QLabel;
class QPushButton;

namespace dracoved {
class SwissEph;

class AshtakavargaPanel final : public QWidget {
public:
    explicit AshtakavargaPanel(SwissEph* swe, QWidget* parent = nullptr);
    void setContext(const NatalInput& input, const NatalChart& chart, const QString& facts);
    void setInspectionTime(qint64 utcMs);
    void setActiveLords(const QStringList& lords);
    std::function<void(qint64)> onMomentCalculated;
private:
    void fillScores();
    void fillContributions();
    void calculateTransits();
    void clearTransits();
    void copy();
    int signForColumn(int column) const;
    void setSignHeaders(QTableWidget* table);

    SwissEph* swe_;
    AshtakavargaResult scores_;
    NatalInput input_;
    SiderealAyanamsa ayanamsa_ = SiderealAyanamsa::Lahiri;
    QTimeZone zone_;
    QString facts_, transitContext_;
    QStringList activeLords_;
    bool valid_ = false;
    std::optional<qint64> resolvedUtc_;
    QComboBox* order_;
    QComboBox* planet_;
    QDateEdit* date_;
    QTimeEdit* time_;
    QTableWidget* matrix_;
    QTableWidget* contributions_;
    QTableWidget* transits_;
    QLabel* natalStatus_;
    QLabel* transitStatus_;
    QPushButton* run_;
    QPushButton* copy_;
};
} // namespace dracoved
