#pragma once

#include "moorthi_graph_panel.h"
#include "../core/ashtakavarga.h"
#include "../core/vedic_benchmark.h"
#include "../core/vedic_gochar.h"
#include "../core/vimshottari.h"
#include <functional>

class QComboBox;
class QTimeEdit;
class QTableWidget;

namespace dracoved {
class VedicBenchmarkPlot;
struct VedicBenchmarkReading {
    QString planet;
    QString roles;
    unsigned roleMask = 0;
    double planetWeight = 1;
    int planetIndex = 0;
    double longitude = 0;
    MoorthiEntry entry;
    int tara = 0, nakshatra = -1, bav = -1, sav = -1;
    KakshaBindu kaksha;
    VedicBenchmarkScore score;
    VedicBenchmarkScore d1Score;
    NavamsaTransitReading d9;
    GocharReading gochar;
};
struct VedicBenchmarkSample {
    double jd = 0;
    QString error;
    QString dashaPath;
    GocharContext gochar;
    QVector<VedicBenchmarkReading> readings;
    double overall = 0;
    double d1Overall = std::numeric_limits<double>::quiet_NaN();
    double d9Overall = std::numeric_limits<double>::quiet_NaN();
    QStringList d9Unresolved, d9NotCovered;
    double d1Contribution = 0, d9Contribution = 0;
    double effectiveD1Share = 0, effectiveD9Share = 0;
    int scoredPlanets = 0;
};

class VedicBenchmarkPanel final : public QWidget {
public:
    explicit VedicBenchmarkPanel(SwissEph* swe, QWidget* parent = nullptr);
    void setContext(const NatalInput& input, const NatalChart& chart);
    void setDashaYearDays(double days);
    bool activeDashasOnly() const;
    void setControlsBusy(bool busy);
    void setSeries(const QVector<MoorthiGraphSeries>& series, double start, double end, const QTimeZone& zone);
    void clear();
    void calculate();
    void stop();
    bool isRunning() const;
    bool hasSource() const { return !series_.isEmpty(); }
    const QVector<VedicBenchmarkSample>& samples() const { return samples_; }
    // Copy results (left-click): compact summary, per-year overview and merged
    // periods. copyFullDetail (right-click menu): every sample, every column.
    void copySummary();
    void copyFullDetail();
    std::function<void(bool)> onBusyChanged;
    std::function<void(qint64)> onMomentSelected;
    std::function<void()> onPlanetScopeChanged;
private:
    void step();
    void complete(const QString& message);
    void rescore();
    void select(int index);
    void editRules();
    void clearSamples();
    QString ruleDescription() const;
    double displayedScore(int index) const;
    QString tooltip(int index) const;
    QString dashaDescription() const;
    QString scoreDescription() const;
    QString d9Summary(const VedicBenchmarkSample& sample) const;
    QString d9RangeSummary() const;

    SwissEph* swe_;
    NatalInput input_;
    SiderealAyanamsa ayanamsa_ = SiderealAyanamsa::Lahiri;
    AshtakavargaResult natalScores_;
    QString natalError_;
    bool haveBav_ = false;
    int natalStar_ = -1;
    Vimshottari dashas_;
    qint64 birthMs_ = 0;
    double birthMoon_ = 0, yearDays_ = 365.25;
    bool validBirth_ = false;
    NatalChart natalChart_;
    double nextDashaEvent_ = 0;
    QCheckBox* activeDashas_;
    std::array<QCheckBox*, 5> levels_{};
    VedicBenchmarkRules rules_;
    GocharRules gocharRules_;
    NavamsaTransitRules d9Rules_;
    std::array<double, 9> natalLongitudes_{};
    QVector<MoorthiGraphSeries> series_;
    QVector<VedicBenchmarkSample> samples_;
    QVector<int> entryIndexes_;
    QTimeZone zone_;
    double start_ = 0, end_ = 0;
    QDate nextDate_;
    int nextEvent_ = 0;
    QVector<double> eventTimes_;
    bool scanningKaksha_ = false;
    bool scanningNatal_ = false;
    int kakshaSeries_ = 0;
    int natalSeries_ = 0;
    double kakshaCursor_ = 0;
    double natalCursor_ = 0;
    int selected_ = -1;
    bool partial_ = false;
    QComboBox* sampling_;
    QComboBox* anchor_;
    QComboBox* display_;
    QComboBox* scoreMode_;
    QComboBox* chartScope_;
    QTimeEdit* time_;
    QPushButton* weights_;
    QPushButton* copy_;
    QLabel* status_;
    QLabel* detailLabel_;
    QTableWidget* details_;
    VedicBenchmarkPlot* plot_;
    QTimer* timer_;
};
} // namespace dracoved
