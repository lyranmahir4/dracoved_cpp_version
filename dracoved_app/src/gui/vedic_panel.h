#pragma once

#include <QWidget>

#include "../core/chart_types.h"
#include "../core/tropical_natal.h"
#include "../core/vedic_nakshatra.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace dracoved {

class SwissEph;
class MoorthiPanel;
class TaraPanel;

class VedicPanel final : public QWidget {
    Q_OBJECT

public:
    explicit VedicPanel(TropicalNatalEngine* engine, SwissEph* swe, QWidget* parent = nullptr);

    void setNatalContext(const NatalInput& input, const NatalChart& chart,
                         const QString& location);

    QTableWidget* table() const { return table_; }
    SiderealAyanamsa ayanamsa() const { return ayanamsa_; }

signals:
    void statusMessage(const QString& message);

private:
    struct Row {
        QString key;
        QString body;
        double longitude = 0.0;
        bool valid = false;
    };

    void refresh();
    void updateAyanamsaControl();
    void copyTable();

    TropicalNatalEngine* engine_ = nullptr;
    SwissEph* swe_ = nullptr;
    QComboBox* ayanamsaCombo_ = nullptr;
    QPushButton* copyButton_ = nullptr;
    QLabel* contextLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    MoorthiPanel* moorthiPanel_ = nullptr;
    TaraPanel* taraPanel_ = nullptr;
    NatalInput input_;
    NatalChart chart_;
    QString location_;
    SiderealAyanamsa ayanamsa_ = SiderealAyanamsa::Lahiri;
    bool ayanamsaInitialized_ = false;
    bool hasContext_ = false;
};

}  // namespace dracoved
