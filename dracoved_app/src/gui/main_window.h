#pragma once

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QVariant>
#include <QTimeZone>

#include "../core/chart_types.h"
#include "../core/swiss_eph.h"
#include "../core/tropical_natal.h"
#include "../core/progression.h"

class QDockWidget;
class QToolButton;
class QAction;
class QTabWidget;
class QTabBar;
class QTableWidget;
class QStackedWidget;
class QDateEdit;
class QTimeEdit;
class QPushButton;
class QRadioButton;
class QLineEdit;
class QCheckBox;
class QLabel;
class QDoubleSpinBox;
class QWidget;
class QFrame;
class QSplitter;
class QComboBox;
class QThread;
class QEvent;
class QSpinBox;
class QProgressBar;
class QTextEdit;
class QQuickWidget;
class QGroupBox;
class QTimer;
namespace dracoved {

struct ChartWheelTheme;
class ChartWheelWidget;
class ChartSetupDialog;
class SearchWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    struct TransitSearchResult {
        QDateTime timeLocal;
        QDateTime timeUtc;
        QString tzLabel;
        QString planet;
        QString event;
        QString signHouse;
        QString aspect;
        double orb = 0.0;
        bool hasOrb = false;
    };
    struct TransitCalendarEvent {
        QDateTime timeUtc;
        QString tzLabel;
        QString planet;
        QString event;
        QString signHouse;
        double longitude = 0.0;
    };
    struct TransitConjunctionWindow {
        QDateTime startUtc;
        QDateTime endUtc;
        QString tzLabel;
        QString bucketLabel;
        QStringList planetsInBucketAtStart;
        QStringList orbClusterAtStart;
        int bucketCount = 0;
        int clusterCount = 0;
        double clusterSpanDeg = 0.0;
        bool uniqueFirstOccurrence = false;
        QString uniqueSignature;
    };
    struct LunationResult {
        QDateTime timeLocal;
        QDateTime timeUtc;
        QString tzLabel;
        QString event;
        QString eclipseType;
        double sunLon = 0.0;
        double moonLon = 0.0;
        int eclipseFlags = 0;
    };
    struct DayScanResult {
        QDate date;
        double support = 0.0;
        double challenge = 0.0;
        double net = 0.0;
        double solarBias = 0.0;
        int aspectCount = 0;
        int supportCount = 0;
        int challengeCount = 0;
        QStringList topAspects;
    };
    enum class TransitScanMode {
        TransitNatal,
        TransitTransit,
        TransitSolar,
        TransitProgressed,
        Combined,
    };
    enum class ConjunctionPolicy {
        Neutral,
        BeneficMalefic,
    };

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    enum class AppTab {
        Natal,
        Transits,
        Progression,
        SolarReturn,
        Relocation,
        Astrocartography,
    };

    enum class ThemeMode {
        Light,
        Dark,
        Creme,
    };

    enum class AspectHeaderMode {
        Glyphs,
        Abbrev,
        Full,
    };
    enum class ChartReadabilityPreset {
        Clean,
        Standard,
        Technical,
        Custom,
    };

    enum class SolarAspectView {
        SolarReturn,
        SolarNatal,
    };
    enum class SolarPlacementFinderHouseMode {
        WholeSign,
        Placidus,
        Both,
    };
    enum class RelocationAspectView {
        Relocation,
        RelocationNatal,
    };

    enum class TransitMode {
        NatalOverlay,
        TransitOnly,
    };
    enum class TransitAspectView {
        TransitNatal,
        TransitTransit,
        NatalNatal,
    };
    enum class TransitSubTab {
        Overview,
        Search,
        Calendar,
        Conjunctions,
        Scan,
        Profections,
        Lunations,
    };
    enum class ConjunctionFindMode {
        Range,
        Next,
        Previous,
    };
    enum class ProgressionView {
        NatalOnly,
        ProgressedOnly,
        Overlay,
    };
    enum class LunationAnalysisMode {
        List,
        RepeatedDegrees,
        TargetDegree,
    };
    enum class LunationMatchMode {
        Degree,
        DegreeSign,
        DegreeHouse,
        DegreeSignHouse,
    };
    enum class LunationFilterMode {
        Any,
        OnlySelected,
        ExcludeSelected,
    };

    struct AspectGridBodyFilter {
        bool showNodes = true;
        bool showLilith = false;
        bool showLots = false;
        bool showDerivedPoints = false;
        bool showAsteroids = false;
        bool showAngles = true;
    };

    struct LunationDegreeGroup {
        double degree = 0.0;
        int signIndex = -1;
        int house = 0;
        QVector<int> eventIndices;
    };

    struct SolarPlacementFinderResult {
        int year = 0;
        QDateTime localDateTime;
        QString bodyName;
        int houseWhole = 0;
        int housePlacidus = 0;
        bool matchedWhole = false;
        bool matchedPlacidus = false;
        bool matchedConjunction = false;
        QString matchedAngleName;
        double conjunctionOrb = 0.0;
        QString warning;
    };

    void setupUi();
    void setupConnections();
    void setupMenuBar();
    void setupDockLayout();
    void resetDockLayout();
    void setLayoutLocked(bool locked);
    void setStatusMessage(const QString& text);
    void setCriticalMessage(const QString& text);
    void applyTheme(ThemeMode mode);
    QString buildStyleSheet(ThemeMode mode) const;
    ChartWheelTheme buildChartTheme(ThemeMode mode) const;
    QString aspectHeaderLabel(const QString& name) const;
    void refreshAspectsForHeaderMode();
    void applyAspectTableFont();
    bool isBodyVisibleInAspectGrid(const QString& name) const;
    void handleAspectGridSettings();
    void updateAspectHover(int row, int column);
    void clearAspectHover();
    void markSolarPending();
    void markSolarPlacementFinderStale();
    void updateSolarStatusLabels();
    void updateSolarLocationAvailability();
    void updateSolarTimezoneStatus();
    void syncSolarLocationFromNatal();
    void handleSolarGeocode();
    void fetchSolarTimezoneForCoords(double lat, double lon);
    void handleSolarCalculate();
    void refreshSolarReturnView();
    void handleSolarPlacementFinderRun();
    void refreshSolarPlacementFinderView();
    void showSolarPlacementFinderResults();
    void showSolarPlacementFinderDetails(int index);
    void handleSolarPlacementFinderResultActivated(int row, int column);
    void showSolarPlaceholder();
    void markRelocationPending();
    void updateRelocationStatusLabels();
    void updateRelocationTimezoneStatus();
    void handleRelocationGeocode();
    void fetchRelocationTimezoneForCoords(double lat, double lon);
    void handleRelocationCalculate();
    void refreshRelocationView();
    void showRelocationPlaceholder();
    bool computeSolarReturnChart(int year, const QString& tzLabel, double targetLon, const QString& locationName,
                                 double lat, double lon, NatalChart* out, QString* error);
    bool solarReturnTimeUtc(int year, const QString& tzLabel, double targetLon, QDateTime* outUtc, QDateTime* outLocal, QString* error);
    QString findEphePath() const;
    QStringList sweSearchPaths() const;
    QString profilesDir() const;
    QString sanitizeProfileName(const QString& name) const;
    QString profileFilePath(const QString& name) const;
    QStringList listProfiles() const;
    bool saveProfileByName(const QString& profileName, bool promptOverwrite);
    bool loadProfileByName(const QString& profileName);
    void refreshProfileToolbar();
    void loadUiState();
    void saveUiState();
    void showChartSettingsMenu();
    void showAsteroidSelectionDialog();
    void showFixedStarSelectionDialog();
    void applyChartReadabilityPreset(ChartReadabilityPreset preset);
    void markChartReadabilityCustom();
    bool isAsteroidVisible(const QString& name) const;
    bool isFixedStarVisible(const QString& name) const;

    void openChartSetupDialog(bool newChart);
    bool computeChart(const dracoved::NatalInput& input, const QString& location);
    void handleRecompute();
    void handleNewChart();
    void handleEditChart();
    void handleSaveProfile();
    void handleLoadProfile();
    void handleDeleteProfile();
    void handleAspectOrbs();
    void handleMainTabChanged(int index);
    void handleTransitNow();
    void handleTransitShiftDays(int days);
    void handleTransitModeChanged();
    void handleTransitCalculate();
    void handleTransitResetTime();
    void handleTransitGeocode();
    void handleTransitAspectViewChanged(int index);
    void handleTransitSubTabChanged(int index);
    void handleTransitSearchRun();
    void handleTransitSearchStop();
    void handleTransitSearchResultActivated(int row, int column);
    void handleTransitCalendarRun();
    void handleTransitCalendarResultActivated(int row, int column);
    void handleTransitConjunctionRun();
    void handleTransitConjunctionStop();
    void handleTransitConjunctionResultActivated(int row, int column);
    void handleTransitProfectionRun();
    void handleLunationSearchRun();
    void handleLunationSearchStop();
    void handleLunationResultActivated(int row, int column);
    void handleTransitScanStart();
    void handleTransitScanCancel();
    void handleTransitScanResultActivated(int row, int column);
    void handleTransitScanFinished();
    void handleCopyAspects();
    void handleCopyTransitSearchDetails();
    void handleCopyTransitCalendarDetails();
    void handleCopyTransitConjunctionDetails();
    void handleCopyTransitScanDetails();
    void handleCopyLunationDetails();
    void handleCopyReport();
    void markTransitPending();
    void applyTransitCalculation();
    void updateTransitTargetLabels();
    void updateTransitTimezoneStatus();
    void syncTransitLocationFromNatal();
    void updateTransitLocationAvailability();
    void fetchTransitTimezoneForCoords(double lat, double lon);
    void updateAspectScopeTabs();
    void updateChartLegend();
    void refreshSolarTechniqueView();
    bool isSolarTechniqueTabActive() const;
    bool isSolarPlacementFinderTabActive() const;
    void updateSolarTechniqueDockTitles();
    void handleProgressionNow();
    void handleProgressionCalculate();
    void handleProgressionViewChanged();
    void refreshProgressionView();
    void showProgressionPlaceholder();
    void markProgressionPending();
    void updateProgressionStatusLabels();
    void updateProgressionTimezoneStatus();
    QDateTime progressionTargetLocal() const;
    QString progressionTimezoneLabel() const;
    bool computeProgressionChart(const QDateTime& localTime, const QString& tzLabel, NatalChart* out, QString* error);
    void updateTransitSearchVisibility();
    void refreshTransitScanTab();
    void refreshTransitProfectionTab();
    void syncTransitProfectionAgeFromTransitDate();
    void updateTransitScanResultsTable();
    void showTransitScanDetails(int index);
    void updateTransitSearchTargets();
    void runTransitSearch();
    void showTransitSearchResults();
    void showTransitCalendarResults();
    void showTransitCalendarDetails(const TransitCalendarEvent& result);
    void showTransitConjunctionResults();
    void showTransitConjunctionDetails(const TransitConjunctionWindow& result);
    void runLunationSearch();
    void updateLunationModeAvailability();
    void updateLunationAnalysisAvailability();
    void updateConjunctionModeAvailability();
    void buildLunationDegreeGroups();
    void showLunationResults();
    void showLunationAnalysisResults();
    void showLunationGroupDetails(int groupIndex);
    void showLunationDetails(const LunationResult& result);
    void applyLunationResult(const LunationResult& result);
    void updateLunationCopyButtonState();
    QString buildTransitSearchDetailsClipboardText() const;
    QString buildTransitCalendarDetailsClipboardText() const;
    QString buildTransitConjunctionDetailsClipboardText() const;
    QString buildTransitScanDetailsClipboardText() const;
    QString buildLunationDetailsClipboardText() const;
    void refreshNatalReport();
    QString buildNatalReportText() const;
    QString buildAspectsClipboardText() const;
    bool computeTransitChartAt(const QDateTime& localTime, const QString& tzLabel, NatalChart* out, QString* error);
    bool canApplyLunationResult(QString* error = nullptr) const;
    void applyTransitSearchResult(const TransitSearchResult& result);
    void showTransitSearchDetails(const TransitSearchResult& result);
    void populateSummary(const dracoved::NatalChart& chart, const dracoved::NatalInput& input, const QString& location);
    void populateAngles(const dracoved::NatalChart& chart);
    void populatePlanets(const dracoved::NatalChart& chart);
    void populateFixedStars(const dracoved::NatalChart& chart);
    void populateHouses(const dracoved::NatalChart& chart, dracoved::HouseSystem system);
    void populateAspects(const dracoved::NatalChart& chart);
    void populateTransitAspectsOverlay(const dracoved::NatalChart& transitChart, const dracoved::NatalChart& natalChart);
    void populateProgressedAspectsOverlay(const dracoved::NatalChart& progressedChart, const dracoved::NatalChart& natalChart);
    void populateSolarNatalAspectsOverlay(const dracoved::NatalChart& solarChart, const dracoved::NatalChart& natalChart);
    void populateRelocationNatalAspectsOverlay(const dracoved::NatalChart& relocationChart, const dracoved::NatalChart& natalChart);
    void populateTransitList(const dracoved::NatalChart& transitChart, bool overlayMode);
    void populateCurrentTransits(const dracoved::NatalChart& transitChart, const dracoved::NatalChart& natalChart);
    void populateIngressCountdown(const dracoved::NatalChart& transitChart, const dracoved::NatalInput& transitInput);
    bool computeTransitChart(const QDateTime& localTime, const QString& tzLabel, dracoved::NatalChart* out, QString* error);
    void refreshNatalTransitsPanels();
    void refreshTransitsTab();
    QDateTime transitSelectedLocal() const;
    QString transitTimezoneLabel() const;
    dracoved::NatalInput transitInputFor(const QDateTime& localTime, const QString& tzLabel) const;
    void updateAstrocartographyModeUi();
    void updateAstrocartographyView();
    void updateGeodeticOverlays();
    void setWorldMapOverlays(const QVariantList& lineOverlays, const QVariantList& bandOverlays);
    bool resolveSolarReturnContext(QString* outTzLabel, QString* outLocationName, double* outLat, double* outLon,
                                   QString* error) const;
    bool computeSolarReturnChartPure(int year, const QString& tzLabel, double targetLon, const QString& locationName,
                                     double lat, double lon, dracoved::HouseSystem houseSystem,
                                     dracoved::NatalChart* outChart, dracoved::NatalInput* outInput, QString* error);
    bool applySolarReturnYear(int year, QString* error = nullptr);

    QDockWidget* dataDock_ = nullptr;
    QDockWidget* rightTopDock_ = nullptr;
    QDockWidget* rightBottomDock_ = nullptr;
    QAction* lockLayoutAction_ = nullptr;
    QTabBar* mainTabBar_ = nullptr;
    QFrame* profileToolbarFrame_ = nullptr;
    QComboBox* profileToolbarCombo_ = nullptr;
    QToolButton* profileToolbarLoadButton_ = nullptr;
    QToolButton* profileToolbarSaveButton_ = nullptr;
    QToolButton* profileToolbarSaveAsButton_ = nullptr;
    QToolButton* profileToolbarEditButton_ = nullptr;
    QToolButton* profileToolbarDeleteButton_ = nullptr;
    QLabel* profileToolbarStateLabel_ = nullptr;
    QStackedWidget* dataStack_ = nullptr;
    QSplitter* leftSplitter_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTabBar* aspectScopeTabs_ = nullptr;
    QTableWidget* summaryTable_ = nullptr;
    QTableWidget* anglesTable_ = nullptr;
    QTableWidget* planetsTable_ = nullptr;
    QTableWidget* fixedStarsTable_ = nullptr;
    QTableWidget* housesTable_ = nullptr;
    QTableWidget* aspectsTable_ = nullptr;
    QTableWidget* rightTopTable_ = nullptr;
    QTableWidget* rightBottomTable_ = nullptr;
    QPushButton* rightBottomCopyButton_ = nullptr;
    QWidget* transitPanel_ = nullptr;
    QTabBar* transitSubTabBar_ = nullptr;
    QStackedWidget* transitPanelStack_ = nullptr;
    QWidget* transitOverviewPanel_ = nullptr;
    QWidget* transitSearchPanel_ = nullptr;
    QWidget* transitCalendarPanel_ = nullptr;
    QWidget* transitConjunctionPanel_ = nullptr;
    QWidget* transitProfectionPanel_ = nullptr;
    QWidget* aspectsPanel_ = nullptr;
    QRadioButton* transitOverlayRadio_ = nullptr;
    QRadioButton* transitOnlyRadio_ = nullptr;
    QRadioButton* transitWholeRadio_ = nullptr;
    QRadioButton* transitPlacidusRadio_ = nullptr;
    QDateEdit* transitDateEdit_ = nullptr;
    QTimeEdit* transitTimeEdit_ = nullptr;
    QLineEdit* transitTimezoneEdit_ = nullptr;
    QLabel* transitTimezoneStatus_ = nullptr;
    QPushButton* transitResetTimeButton_ = nullptr;
    QPushButton* transitNowButton_ = nullptr;
    QPushButton* transitPlusDayButton_ = nullptr;
    QPushButton* transitPlusWeekButton_ = nullptr;
    QPushButton* transitPlusMonthButton_ = nullptr;
    QPushButton* transitCalculateButton_ = nullptr;
    QLabel* transitTargetLabel_ = nullptr;
    QLabel* transitStatusLabel_ = nullptr;
    QLabel* transitLastLabel_ = nullptr;
    QLineEdit* transitLocationEdit_ = nullptr;
    QPushButton* transitGeocodeButton_ = nullptr;
    QDoubleSpinBox* transitLatSpin_ = nullptr;
    QDoubleSpinBox* transitLonSpin_ = nullptr;
    QCheckBox* transitUseNatalLocation_ = nullptr;
    QComboBox* searchEventCombo_ = nullptr;
    QComboBox* searchTransitPlanetCombo_ = nullptr;
    QComboBox* searchTargetCombo_ = nullptr;
    QComboBox* searchAspectCombo_ = nullptr;
    QComboBox* searchHouseCombo_ = nullptr;
    QComboBox* searchSignCombo_ = nullptr;
    QLabel* searchDegreeLabel_ = nullptr;
    QDoubleSpinBox* searchDegreeSpin_ = nullptr;
    QLabel* searchDegreeSignLabel_ = nullptr;
    QComboBox* searchDegreeSignCombo_ = nullptr;
    QComboBox* searchRangeModeCombo_ = nullptr;
    QRadioButton* searchModeRangeRadio_ = nullptr;
    QRadioButton* searchModeNextRadio_ = nullptr;
    QRadioButton* searchModePrevRadio_ = nullptr;
    QDateEdit* searchStartDateEdit_ = nullptr;
    QTimeEdit* searchStartTimeEdit_ = nullptr;
    QDateEdit* searchEndDateEdit_ = nullptr;
    QTimeEdit* searchEndTimeEdit_ = nullptr;
    QLineEdit* searchTimezoneEdit_ = nullptr;
    QDoubleSpinBox* searchOrbSpin_ = nullptr;
    QPushButton* searchRunButton_ = nullptr;
    QPushButton* searchStopButton_ = nullptr;
    QPushButton* searchUseCurrentButton_ = nullptr;
    QLabel* searchStatusLabel_ = nullptr;
    QLabel* searchTargetLabel_ = nullptr;
    QComboBox* calendarYearCombo_ = nullptr;
    QComboBox* calendarMonthCombo_ = nullptr;
    QComboBox* calendarPlanetCombo_ = nullptr;
    QCheckBox* calendarShowIngressCheck_ = nullptr;
    QCheckBox* calendarShowEgressCheck_ = nullptr;
    QCheckBox* calendarShowStationCheck_ = nullptr;
    QCheckBox* calendarShowShadowCheck_ = nullptr;
    QCheckBox* calendarIncludeHousesCheck_ = nullptr;
    QPushButton* calendarRefreshButton_ = nullptr;
    QLabel* calendarStatusLabel_ = nullptr;
    QRadioButton* conjModeNextRadio_ = nullptr;
    QRadioButton* conjModePrevRadio_ = nullptr;
    QRadioButton* conjModeRangeRadio_ = nullptr;
    QSpinBox* conjStartYearSpin_ = nullptr;
    QSpinBox* conjEndYearSpin_ = nullptr;
    QLabel* conjReferenceLabel_ = nullptr;
    QRadioButton* conjBucketSignRadio_ = nullptr;
    QRadioButton* conjBucketHouseRadio_ = nullptr;
    QComboBox* conjPlanetCombo_ = nullptr;
    QSpinBox* conjCountSpin_ = nullptr;
    QCheckBox* conjUseOrbCheck_ = nullptr;
    QDoubleSpinBox* conjOrbSpin_ = nullptr;
    QCheckBox* conjIncludeMoonCheck_ = nullptr;
    QCheckBox* conjUniqueFirstCheck_ = nullptr;
    QComboBox* conjUniqueDegreeCombo_ = nullptr;
    QPushButton* conjRunButton_ = nullptr;
    QPushButton* conjStopButton_ = nullptr;
    QLabel* conjStatusLabel_ = nullptr;
    QWidget* transitLunationPanel_ = nullptr;
    QCheckBox* lunationNewMoonCheck_ = nullptr;
    QCheckBox* lunationFullMoonCheck_ = nullptr;
    QCheckBox* lunationSolarEclipseCheck_ = nullptr;
    QCheckBox* lunationLunarEclipseCheck_ = nullptr;
    QRadioButton* lunationModePrevRadio_ = nullptr;
    QRadioButton* lunationModeNextRadio_ = nullptr;
    QRadioButton* lunationModeRangeRadio_ = nullptr;
    QSpinBox* lunationStartYearSpin_ = nullptr;
    QSpinBox* lunationEndYearSpin_ = nullptr;
    QCheckBox* lunationDegreeRangeCheck_ = nullptr;
    QDoubleSpinBox* lunationDegreeRangeStartSpin_ = nullptr;
    QDoubleSpinBox* lunationDegreeRangeEndSpin_ = nullptr;
    QLabel* lunationTimezoneLabel_ = nullptr;
    QPushButton* lunationRunButton_ = nullptr;
    QPushButton* lunationStopButton_ = nullptr;
    QLabel* lunationStatusLabel_ = nullptr;
    QComboBox* lunationAnalysisCombo_ = nullptr;
    QRadioButton* lunationDegreeExactRadio_ = nullptr;
    QRadioButton* lunationDegreeOrbRadio_ = nullptr;
    QComboBox* lunationOrbCombo_ = nullptr;
    QComboBox* lunationMatchCombo_ = nullptr;
    QComboBox* lunationSignModeCombo_ = nullptr;
    QComboBox* lunationSignCombo_ = nullptr;
    QComboBox* lunationHouseModeCombo_ = nullptr;
    QComboBox* lunationHouseCombo_ = nullptr;
    QSpinBox* lunationTargetDegSpin_ = nullptr;
    QSpinBox* lunationTargetMinSpin_ = nullptr;
    QSpinBox* lunationTargetSecSpin_ = nullptr;
    QLabel* lunationAnalysisHintLabel_ = nullptr;
    QSpinBox* scanStartYearSpin_ = nullptr;
    QSpinBox* scanEndYearSpin_ = nullptr;
    QComboBox* scanModeCombo_ = nullptr;
    QSpinBox* scanWeightTransitNatalSpin_ = nullptr;
    QSpinBox* scanWeightTransitTransitSpin_ = nullptr;
    QSpinBox* scanWeightSolarSpin_ = nullptr;
    QSpinBox* scanWeightProgressedSpin_ = nullptr;
    QComboBox* scanConjunctionCombo_ = nullptr;
    QCheckBox* scanIncludeNodesCheck_ = nullptr;
    QCheckBox* scanIncludeAnglesCheck_ = nullptr;
    QCheckBox* scanSolarBiasCheck_ = nullptr;
    QSpinBox* scanSolarBiasSpin_ = nullptr;
    QTimeEdit* scanTimeEdit_ = nullptr;
    QLabel* scanTimezoneLabel_ = nullptr;
    QPushButton* scanRunButton_ = nullptr;
    QPushButton* scanCancelButton_ = nullptr;
    QProgressBar* scanProgressBar_ = nullptr;
    QLabel* scanStatusLabel_ = nullptr;
    QComboBox* scanSortCombo_ = nullptr;
    QSpinBox* scanTopCountSpin_ = nullptr;
    QLabel* profectionReferenceLabel_ = nullptr;
    QSpinBox* profectionAgeSpin_ = nullptr;
    QPushButton* profectionUseTransitAgeButton_ = nullptr;
    QPushButton* profectionRunButton_ = nullptr;
    QLabel* profectionStatusLabel_ = nullptr;
    QToolButton* chartSettingsButton_ = nullptr;
    QToolButton* zoomInButton_ = nullptr;
    QToolButton* zoomOutButton_ = nullptr;
    QToolButton* zoomResetButton_ = nullptr;
    QLabel* chartTitleLabel_ = nullptr;
    QAction* themeLightAction_ = nullptr;
    QAction* themeDarkAction_ = nullptr;
    QAction* themeCremeAction_ = nullptr;
    QAction* aspectHeaderGlyphAction_ = nullptr;
    QAction* aspectHeaderAbbrevAction_ = nullptr;
    QAction* aspectHeaderFullAction_ = nullptr;
    QLabel* chartLegendLabel_ = nullptr;
    ChartWheelWidget* chartWheel_ = nullptr;
    QWidget* progressionControls_ = nullptr;
    QRadioButton* progressionViewNatalRadio_ = nullptr;
    QRadioButton* progressionViewProgressedRadio_ = nullptr;
    QRadioButton* progressionViewOverlayRadio_ = nullptr;
    QDateEdit* progressionDateEdit_ = nullptr;
    QTimeEdit* progressionTimeEdit_ = nullptr;
    QLineEdit* progressionTimezoneEdit_ = nullptr;
    QLabel* progressionTimezoneStatus_ = nullptr;
    QPushButton* progressionNowButton_ = nullptr;
    QPushButton* progressionCalculateButton_ = nullptr;
    QLabel* progressionStatusLabel_ = nullptr;
    QLabel* progressionLastLabel_ = nullptr;
    QPushButton* aspectsCopyButton_ = nullptr;
    QToolButton* aspectGridSettingsButton_ = nullptr;
    QWidget* reportPanel_ = nullptr;
    QTextEdit* reportText_ = nullptr;
    QPushButton* reportCopyButton_ = nullptr;
    QWidget* solarControls_ = nullptr;
    QWidget* solarTechniquePanel_ = nullptr;
    QWidget* solarPlacementFinderPanel_ = nullptr;
    QSpinBox* solarYearSpin_ = nullptr;
    QRadioButton* solarUseNatalRadio_ = nullptr;
    QRadioButton* solarUseCustomRadio_ = nullptr;
    QLineEdit* solarLocationEdit_ = nullptr;
    QPushButton* solarGeocodeButton_ = nullptr;
    QDoubleSpinBox* solarLatSpin_ = nullptr;
    QDoubleSpinBox* solarLonSpin_ = nullptr;
    QLineEdit* solarTimezoneEdit_ = nullptr;
    QLabel* solarTimezoneStatus_ = nullptr;
    QPushButton* solarCalculateButton_ = nullptr;
    QLabel* solarStatusLabel_ = nullptr;
    QLabel* solarLastLabel_ = nullptr;
    QDateEdit* solarTechniqueDateEdit_ = nullptr;
    QDoubleSpinBox* solarTechniqueOrbSpin_ = nullptr;
    QCheckBox* solarTechniqueNatalCheck_ = nullptr;
    QCheckBox* solarTechniqueSolarCheck_ = nullptr;
    QLabel* solarTechniqueRangeLabel_ = nullptr;
    QComboBox* solarTechniqueRankMetricCombo_ = nullptr;
    QComboBox* solarTechniqueRankOrderCombo_ = nullptr;
    QSpinBox* solarTechniqueTopSpin_ = nullptr;
    QSpinBox* solarFinderStartYearSpin_ = nullptr;
    QSpinBox* solarFinderEndYearSpin_ = nullptr;
    QComboBox* solarFinderPlanetCombo_ = nullptr;
    QComboBox* solarFinderHouseCombo_ = nullptr;
    QComboBox* solarFinderHouseSystemCombo_ = nullptr;
    QComboBox* solarFinderConjunctionTargetCombo_ = nullptr;
    QDoubleSpinBox* solarFinderConjunctionOrbSpin_ = nullptr;
    QPushButton* solarFinderRunButton_ = nullptr;
    QLabel* solarFinderStatusLabel_ = nullptr;
    QWidget* relocationControls_ = nullptr;
    QLineEdit* relocationLocationEdit_ = nullptr;
    QPushButton* relocationGeocodeButton_ = nullptr;
    QDoubleSpinBox* relocationLatSpin_ = nullptr;
    QDoubleSpinBox* relocationLonSpin_ = nullptr;
    QLineEdit* relocationTimezoneEdit_ = nullptr;
    QLabel* relocationTimezoneStatus_ = nullptr;
    QRadioButton* relocationWholeRadio_ = nullptr;
    QRadioButton* relocationPlacidusRadio_ = nullptr;
    QCheckBox* relocationOverlayCheck_ = nullptr;
    QPushButton* relocationCalculateButton_ = nullptr;
    QLabel* relocationStatusLabel_ = nullptr;
    QLabel* relocationLastLabel_ = nullptr;
    QStackedWidget* centerStack_ = nullptr;
    QWidget* chartViewPanel_ = nullptr;
    QWidget* worldMapPanel_ = nullptr;
    QQuickWidget* worldMapView_ = nullptr;
    QObject* worldMapRoot_ = nullptr;
    QWidget* astrocartographyPanel_ = nullptr;
    QComboBox* astroModeCombo_ = nullptr;
    QLabel* astroModeHintLabel_ = nullptr;
    QGroupBox* geodeticGroup_ = nullptr;
    QComboBox* geodeticPlanetCombo_ = nullptr;
    QRadioButton* geodeticExactRadio_ = nullptr;
    QRadioButton* geodeticOrbRadio_ = nullptr;
    QComboBox* geodeticOrbCombo_ = nullptr;
    QLabel* geodeticTimeLabel_ = nullptr;
    QPushButton* geodeticRefreshButton_ = nullptr;
    QLabel* geodeticStatusLabel_ = nullptr;
    bool worldMapReady_ = false;
    QByteArray defaultDockState_;
    bool layoutLocked_ = false;
    bool hasCurrentChart_ = false;
    bool hasTransitChart_ = false;
    bool hasProgressionChart_ = false;
    bool hasSolarChart_ = false;
    bool hasRelocationChart_ = false;
    QString currentLocation_;
    QString currentProfileName_;
    dracoved::NatalInput currentInput_;
    dracoved::NatalChart currentChart_;
    dracoved::NatalChart currentTransitChart_;
    dracoved::NatalChart currentProgressionChart_;
    dracoved::NatalChart currentSolarChart_;
    dracoved::NatalChart currentRelocationChart_;
    dracoved::NatalInput currentProgressionInput_;
    dracoved::NatalInput currentSolarInput_;
    dracoved::NatalInput currentRelocationInput_;
    QString currentSolarLocation_;
    QString currentRelocationLocation_;
    QVector<dracoved::HouseCusp> natalPlacidusCusps_;
    dracoved::HouseSystem defaultHouseSystem_ = dracoved::HouseSystem::WholeSign;
    dracoved::AspectOrbs aspectOrbs_ = dracoved::defaultAspectOrbs();
    AppTab activeTab_ = AppTab::Natal;
    TransitMode transitMode_ = TransitMode::NatalOverlay;
    TransitAspectView transitAspectView_ = TransitAspectView::TransitNatal;
    TransitSubTab transitSubTab_ = TransitSubTab::Overview;
    dracoved::HouseSystem transitHouseSystem_ = dracoved::HouseSystem::WholeSign;
    bool transitPending_ = false;
    bool progressionPending_ = false;
    bool solarPending_ = false;
    bool relocationPending_ = false;
    QDateTime lastTransitCalculated_;
    QDateTime lastProgressionCalculated_;
    QDateTime lastSolarCalculated_;
    QDateTime lastRelocationCalculated_;
    bool overlayAspectsTransitNatal_ = true;
    bool overlayAspectsTransitTransit_ = false;
    bool overlayAspectsNatalNatal_ = false;
    double aspectDisplayMaxOrb_ = 0.0;
    bool showAsteroids_ = false;
    bool includeAsteroidAspects_ = false;
    QStringList visibleAsteroids_;
    bool showLots_ = true;
    bool showDerivedPoints_ = true;
    bool showFixedStars_ = false;
    QStringList visibleFixedStars_;
    ChartReadabilityPreset chartReadabilityPreset_ = ChartReadabilityPreset::Clean;
    bool transitSearchCancel_ = false;
    QVector<TransitSearchResult> transitSearchResults_;
    bool hasTransitSearchSelection_ = false;
    TransitSearchResult lastTransitSearchSelection_;
    QVector<TransitCalendarEvent> transitCalendarEvents_;
    QVector<int> transitCalendarDisplayOrder_;
    bool hasTransitCalendarSelection_ = false;
    TransitCalendarEvent lastTransitCalendarSelection_;
    QVector<TransitConjunctionWindow> transitConjunctionResults_;
    QVector<int> transitConjunctionDisplayOrder_;
    bool hasTransitConjunctionSelection_ = false;
    TransitConjunctionWindow lastTransitConjunctionSelection_;
    QVector<DayScanResult> transitScanResults_;
    QVector<int> transitScanDisplayOrder_;
    bool hasTransitScanSelection_ = false;
    DayScanResult lastTransitScanSelection_;
    QDateTime lastTransitScanSelectionLocal_;
    QString lastTransitScanSelectionTzLabel_;
    bool transitScanRunning_ = false;
    QThread* scanThread_ = nullptr;
    QObject* scanWorker_ = nullptr;
    QThread* searchThread_ = nullptr;
    SearchWorker* searchWorker_ = nullptr;
    QThread* calendarThread_ = nullptr;
    QObject* calendarWorker_ = nullptr;
    QTimer* calendarRecomputeTimer_ = nullptr;
    bool calendarRunning_ = false;
    bool calendarRestartPending_ = false;
    QTimeZone calendarTz_;
    QString calendarTzLabel_;
    QThread* conjThread_ = nullptr;
    QObject* conjWorker_ = nullptr;
    bool conjRunning_ = false;
    bool conjRestartPending_ = false;
    bool conjAutoApplied_ = false;
    ConjunctionFindMode conjFindMode_ = ConjunctionFindMode::Range;
    QDateTime conjAnchorUtc_;
    QTimeZone conjTz_;
    QString conjTzLabel_;
    bool conjLastRunUniqueFirst_ = false;
    bool conjLastRunIncludeMoon_ = true;
    double conjLastRunUniqueDegreeStep_ = 1.0;
    bool searchRunning_ = false;
    bool searchAutoApplied_ = false;
    QVector<LunationResult> lunationResults_;
    QVector<LunationDegreeGroup> lunationDegreeGroups_;
    QVector<int> lunationDegreeGroupDisplayOrder_;
    QVector<int> lunationListDisplayOrder_;
    QVector<int> lunationAnalysisEventOrder_;
    QVector<int> lunationBottomEventOrder_;
    int lunationSelectedGroupIndex_ = -1;
    bool lunationRunning_ = false;
    bool lunationAutoApplied_ = false;
    bool hasLunationSelection_ = false;
    LunationResult lastLunationSelection_;
    QVector<SolarPlacementFinderResult> solarPlacementFinderResults_;
    QStringList solarPlacementFinderWarnings_;
    bool solarPlacementFinderRan_ = false;
    bool solarPlacementFinderStale_ = false;
    int solarPlacementFinderSelectedIndex_ = -1;
    int solarPlacementFinderLastSearchedCount_ = 0;
    int solarPlacementFinderLastFailedCount_ = 0;
    int solarPlacementFinderLastStartYear_ = 0;
    int solarPlacementFinderLastEndYear_ = 0;
    QString solarPlacementFinderLastPlanet_;
    int solarPlacementFinderLastHouse_ = 1;
    SolarPlacementFinderHouseMode solarPlacementFinderLastHouseMode_ = SolarPlacementFinderHouseMode::WholeSign;
    QString solarPlacementFinderLastConjunctionTarget_ = "None";
    double solarPlacementFinderLastConjunctionOrb_ = 1.0;
    LunationAnalysisMode lunationAnalysisMode_ = LunationAnalysisMode::List;
    QThread* lunationThread_ = nullptr;
    QObject* lunationWorker_ = nullptr;
    ThemeMode theme_ = ThemeMode::Light;
    AspectHeaderMode aspectHeaderMode_ = AspectHeaderMode::Abbrev;
    ProgressionView progressionView_ = ProgressionView::ProgressedOnly;
    SolarAspectView solarAspectView_ = SolarAspectView::SolarReturn;
    RelocationAspectView relocationAspectView_ = RelocationAspectView::Relocation;
    dracoved::HouseSystem relocationHouseSystem_ = dracoved::HouseSystem::WholeSign;
    bool aspectTriangleEnabled_ = false;
    int aspectHoverRow_ = -1;
    int aspectHoverCol_ = -1;
    AspectGridBodyFilter aspectGridFilter_;

    QNetworkAccessManager* net_ = nullptr;

    dracoved::SwissEph swe_;
    dracoved::TropicalNatalEngine engine_;
    dracoved::SecondaryProgressionEngine progressionEngine_;
    QString ephePath_;
};

}  // namespace dracoved

Q_DECLARE_METATYPE(dracoved::MainWindow::TransitSearchResult)
