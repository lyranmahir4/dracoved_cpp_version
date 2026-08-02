#pragma once

#include "return_finder_types.h"

#include <QObject>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QDoubleSpinBox;
class QStackedWidget;
class QTableWidget;
class QThread;
class QVBoxLayout;
class QWidget;

namespace dracoved {

class ReturnFinderWorker;
class ReturnFinderConditionRow;

class ReturnFinderController : public QObject {
    Q_OBJECT

public:
    explicit ReturnFinderController(QObject* parent = nullptr);
    ~ReturnFinderController() override;

    QWidget* filtersWidget() const;
    QWidget* workspaceWidget() const;

    void setRuntimePaths(const QString& ephePath, const QStringList& dllSearchPaths);
    void setNatalContext(const NatalInput& input, const NatalChart& chart, const QString& locationName);
    void clearNatalContext();
    void setReturnType(ReturnFinderType type);
    ReturnFinderType returnType() const;

    bool isRunning() const;
    bool hasCompletedRun() const;
    void cancelSearch();
    bool shutdown(int timeoutMs = 4000);
    void markResultsStale();

    bool hasSelectedResult() const;
    ReturnFinderResult selectedResult() const;
    ReturnFinderQuery lastQuery() const;
    ReturnFinderRunSummary runSummary() const;
    bool resultsAreStale() const;

signals:
    void selectionChanged();
    void summaryChanged();
    void openResultRequested(const dracoved::ReturnFinderResult& result,
                             const dracoved::ReturnFinderQuery& query);
    void statusMessage(const QString& message);

private:
    void buildFiltersUi();
    void buildWorkspaceUi();
    void addCondition(const ReturnFinderCondition& condition = {});
    void duplicateCondition(ReturnFinderConditionRow* row);
    void removeCondition(ReturnFinderConditionRow* row);
    void clearConditions();
    void conditionChanged();
    void updateRangeUi();
    void updateLocationUi();
    void updateRunUi();
    void startSearch();
    bool buildQuery(ReturnFinderQuery* query, QString* error) const;
    void populateResults();
    void handleSelectionChanged();
    const ReturnFinderResult* resultForStableId(int stableId) const;
    int selectedStableId() const;
    void openSelectedResult();
    void copyResults();

    QString presetsDirectory() const;
    void refreshPresetList(const QString& preferred = {});
    void savePreset();
    void loadSelectedPreset();
    void renameSelectedPreset();
    void deleteSelectedPreset();
    bool applyPresetFile(const QString& path, QString* error);
    bool writePresetFile(const QString& path, const QString& displayName, QString* error) const;

    QWidget* filtersRoot_ = nullptr;
    QWidget* workspaceRoot_ = nullptr;
    QComboBox* returnTypeCombo_ = nullptr;
    QStackedWidget* rangeStack_ = nullptr;
    QSpinBox* startYearSpin_ = nullptr;
    QSpinBox* endYearSpin_ = nullptr;
    QDateEdit* startDateEdit_ = nullptr;
    QDateEdit* endDateEdit_ = nullptr;
    QComboBox* locationModeCombo_ = nullptr;
    QLineEdit* locationEdit_ = nullptr;
    QLineEdit* timezoneEdit_ = nullptr;
    QDoubleSpinBox* latitudeSpin_ = nullptr;
    QDoubleSpinBox* longitudeSpin_ = nullptr;
    QComboBox* houseModeCombo_ = nullptr;
    QComboBox* rulershipCombo_ = nullptr;
    QComboBox* matchModeCombo_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QWidget* conditionsContainer_ = nullptr;
    QVBoxLayout* conditionsLayout_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* contextLabel_ = nullptr;
    QLabel* workspaceStateLabel_ = nullptr;
    QTableWidget* resultsTable_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* copyButton_ = nullptr;

    QVector<ReturnFinderConditionRow*> conditionRows_;
    QVector<ReturnFinderResult> results_;
    ReturnFinderRunSummary summary_;
    ReturnFinderQuery lastQuery_;
    NatalInput natalInput_;
    NatalChart natalChart_;
    QString natalLocationName_;
    QString ephePath_;
    QStringList dllSearchPaths_;
    bool hasNatalContext_ = false;
    bool hasRun_ = false;
    bool stale_ = false;
    bool running_ = false;
    int nextConditionId_ = 1;
    quint64 runGeneration_ = 0;

    QThread* workerThread_ = nullptr;
    ReturnFinderWorker* worker_ = nullptr;
};

}  // namespace dracoved

