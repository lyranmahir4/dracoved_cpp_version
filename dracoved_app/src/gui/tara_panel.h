#pragma once

#include "../core/chart_types.h"
#include <QWidget>
#include <QTimeZone>

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
    void setContext(const NatalInput& input, const NatalChart& siderealChart);
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
};
} // namespace dracoved
