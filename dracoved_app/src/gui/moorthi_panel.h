#pragma once

#include "../core/chart_types.h"
#include "../core/moorthi.h"
#include <QWidget>
#include <QTimeZone>
#include <QSet>

class QDateEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QTableWidget;
class QTimer;

namespace dracoved {
class MoorthiPanel final : public QWidget {
public:
    explicit MoorthiPanel(SwissEph* swe, QWidget* parent = nullptr);
    void setContext(const NatalInput& input, const NatalChart& chart);
    void setBirthFacts(const QString& facts) { birthFacts_ = facts; }
    void setActiveLords(const QStringList& lords, const QString& context, bool only);
private:
    struct Target { QString name; int id; double offset; };
    struct Result { QString planet; MoorthiEntry entry; };
    void start();
    void step();
    void stop(const QString& message);
    void display();
    void copy();
    void invalidate();
    SwissEph* swe_;
    QDateEdit* from_;
    QDateEdit* through_;
    QComboBox* planet_;
    QComboBox* filter_;
    QPushButton* run_;
    QPushButton* stop_;
    QPushButton* copy_;
    QLabel* status_;
    QLabel* context_;
    QTableWidget* table_;
    QTimer* timer_;
    NatalInput input_;
    SiderealAyanamsa ayanamsa_ = SiderealAyanamsa::Lahiri;
    int natalMoonSign_ = -1;
    QString sourceKey_;
    QString resultContext_;
    QString operationStatus_;
    QString birthFacts_;
    QSet<QString> activeLords_;
    QString activeContext_;
    bool activeOnly_ = false;
    QVector<Target> targets_;
    QVector<Target> selected_;
    QVector<Result> results_;
    QTimeZone zone_;
    double startJd_ = 0.0, endJd_ = 0.0, cursorJd_ = 0.0;
    int targetIndex_ = 0;
};
}  // namespace dracoved
