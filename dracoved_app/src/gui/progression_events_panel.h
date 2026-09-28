#pragma once

#include "../core/progression_events.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QTableWidget;

namespace dracoved {
class ProgressionEventsWorker;

class ProgressionEventsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProgressionEventsPanel(QWidget* parent = nullptr);
    ~ProgressionEventsPanel() override;
    void setNatalContext(const NatalInput& input, const NatalChart& chart,
                         const QString& ephePath, const QString& dllPath);
    void showResults(QTableWidget* results, QTableWidget* details);
    void activateRow(int row);
signals:
    void resultsChanged();
    void eventActivated(const QDateTime& local, const QString& timezone);
private:
    void start();
    void copy();
    ProgressionEventQuery context_;
    ProgressionEventQuery lastQuery_;
    QString ephePath_;
    QString dllPath_;
    bool hasContext_ = false;
    int generation_ = 0;
    int selected_ = -1;
    QVector<ProgressionEvent> results_;
    QString resultStatus_ = "Choose a range, then Find events.";
    ProgressionEventsWorker* worker_ = nullptr;
    QDateEdit* from_ = nullptr;
    QDateEdit* through_ = nullptr;
    QLineEdit* timezone_ = nullptr;
    QComboBox* body_ = nullptr;
    QComboBox* houseReference_ = nullptr;
    QCheckBox* signs_ = nullptr;
    QCheckBox* houses_ = nullptr;
    QCheckBox* angles_ = nullptr;
    QCheckBox* stations_ = nullptr;
    QPushButton* find_ = nullptr;
    QPushButton* stop_ = nullptr;
    QPushButton* copy_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* contextLabel_ = nullptr;
    QLabel* status_ = nullptr;
};
} // namespace dracoved
