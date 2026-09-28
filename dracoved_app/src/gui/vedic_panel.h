#pragma once

#include <QWidget>

#include "../core/chart_types.h"
#include "../core/tropical_natal.h"
#include "../core/vedic_nakshatra.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSplitter;
class QTableWidget;
class QToolButton;
class QTabWidget;
class QCheckBox;

namespace dracoved {

class SwissEph;
class MoorthiPanel;
class MoorthiGraphPanel;
class TaraPanel;
class DashaPanel;
class AshtakavargaPanel;

// Dense D1 research workspace: a wide placement table (sign, house, lords,
// motion, nakshatra, pada, navamsa, dignity, solar distance) over a
// side-by-side Moorthi Nirnaya / Transit Tara research pair. Column order is
// part of the copy format and of vedic_panel_integration_tests.cpp.
class VedicPanel final : public QWidget {
    Q_OBJECT

public:
    enum Column {
        ColBody = 0,
        ColSign,
        ColDegree,
        ColLongitude,
        ColHouse,
        ColSignLord,
        ColMotion,
        ColNakshatra,
        ColPada,
        ColInStar,
        ColStarLord,
        ColNavamsa,
        ColDignity,
        ColFromSun,
        ColumnCount,
    };

    explicit VedicPanel(TropicalNatalEngine* engine, SwissEph* swe, QWidget* parent = nullptr);

    void setNatalContext(const NatalInput& input, const NatalChart& chart,
                         const QString& location);

    QTableWidget* table() const { return table_; }
    SiderealAyanamsa ayanamsa() const { return ayanamsa_; }
    static QStringList columnHeaders();

signals:
    void statusMessage(const QString& message);

private:
    struct Row {
        QString key;
        QString body;
        double longitude = 0.0;
        bool valid = false;
        bool hasSpeed = false;
        double speed = 0.0;
        bool retrograde = false;
        bool isNode = false;
    };

    void refresh();
    void updateAyanamsaControl();
    void updateFacts(const NatalChart& vedicChart, double ayanamsaValue, bool hasAyanamsaValue);
    void updateStatus();
    void resetOrder();
    void copyTable();
    void restoreSplitters();
    void persistSplitters();
    void updateActiveLords(bool syncTime = true);

    TropicalNatalEngine* engine_ = nullptr;
    SwissEph* swe_ = nullptr;
    QComboBox* ayanamsaCombo_ = nullptr;
    QPushButton* copyButton_ = nullptr;
    QToolButton* orderButton_ = nullptr;
    QLabel* contextLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QSplitter* mainSplitter_ = nullptr;
    QSplitter* transitSplitter_ = nullptr;
    MoorthiPanel* moorthiPanel_ = nullptr;
    MoorthiGraphPanel* moorthiGraphPanel_ = nullptr;
    TaraPanel* taraPanel_ = nullptr;
    DashaPanel* dashaPanel_ = nullptr;
    AshtakavargaPanel* ashtakavargaPanel_ = nullptr;
    QTabWidget* views_ = nullptr;
    QLabel* activeLabel_ = nullptr;
    QCheckBox* activeOnly_ = nullptr;
    NatalInput input_;
    NatalChart chart_;
    QString location_;
    QString contextPlain_;
    SiderealAyanamsa ayanamsa_ = SiderealAyanamsa::Lahiri;
    ZodiacSystem zodiac_ = ZodiacSystem::Sidereal;
    bool ayanamsaInitialized_ = false;
    bool hasContext_ = false;
    bool populating_ = false;
    // -1 means natural (Lagna-first) order; otherwise the user's sort column.
    int sortColumn_ = -1;
    Qt::SortOrder sortOrder_ = Qt::AscendingOrder;
};

}  // namespace dracoved
