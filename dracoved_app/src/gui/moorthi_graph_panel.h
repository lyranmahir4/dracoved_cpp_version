#pragma once

#include "../core/chart_types.h"
#include "../core/moorthi.h"
#include <QTimeZone>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QMenu;
class QPushButton;
class QTimer;
class QToolButton;

namespace dracoved {
class MoorthiGraphPlot;
class VedicBenchmarkPanel;

struct MoorthiGraphSeries {
    QString planet;
    int body = 0;
    double offset = 0.0;
    int colorIndex = 0;
    // The first entry establishes the metal at the range start and can predate it.
    QVector<MoorthiEntry> entries;
    double calculatedThrough = 0.0;
};

class MoorthiGraphPanel final : public QWidget {
public:
    explicit MoorthiGraphPanel(SwissEph* swe, QWidget* parent = nullptr);
    void setContext(const NatalInput& input, const NatalChart& chart);
    void setDashaYearDays(double days);
    const QVector<MoorthiGraphSeries>& series() const { return series_; }
    std::function<void(qint64)> onMomentSelected;

private:
    void selectionChanged();
    void invalidate();
    void start();
    void step();
    void finish(const QString& message);
    void updateProgress();
    void setBusy(bool busy);
    bool automaticPlanets() const;
    void updatePlanetLabel();

    SwissEph* swe_;
    QDateEdit* from_;
    QDateEdit* through_;
    QToolButton* planets_;
    QMenu* menu_;
    QCheckBox* all_ = nullptr;
    QVector<QCheckBox*> choices_;
    QPushButton* run_;
    QPushButton* stop_;
    QLabel* context_;
    QLabel* status_;
    MoorthiGraphPlot* plot_;
    QComboBox* view_;
    QLabel* hint_;
    VedicBenchmarkPanel* benchmark_;
    QTimer* timer_;
    NatalInput input_;
    SiderealAyanamsa ayanamsa_ = SiderealAyanamsa::Lahiri;
    int natalMoonSign_ = -1;
    QString sourceKey_;
    QTimeZone zone_;
    QVector<MoorthiGraphSeries> targets_;
    QVector<MoorthiGraphSeries> series_;
    double startJd_ = 0.0, endJd_ = 0.0, cursorJd_ = 0.0, minimumJd_ = 0.0;
    int seriesIndex_ = 0;
    bool lookingBack_ = true;
    qint64 lastProgressMs_ = 0;
};
} // namespace dracoved
