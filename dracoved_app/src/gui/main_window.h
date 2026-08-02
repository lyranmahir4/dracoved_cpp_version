#pragma once

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QVariant>
#include <QTimeZone>
#include <QHash>

#include <functional>

#include "../core/chart_types.h"
#include "../core/swiss_eph.h"
#include "../core/tropical_natal.h"
#include "../core/progression.h"
#include "../core/progressed_lunar_return.h"
#include "../core/zodiacal_releasing.h"
#include "aspect_matrix_delegate.h"

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
class AstroMapWidget;
class ReturnFinderController;
class PlanetaryHoursController;
class ZodiacalReleasingController;
class GeodeticEquivalentsController;
struct ReturnFinderResult;
struct ReturnFinderQuery;

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
        QString conjunctionSummary;
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
    struct TransitAspectPeakHit {
        QString transitBody;
        QString aspect;
        QString natalTarget;
        double transitLongitude = 0.0;
        double natalLongitude = 0.0;
        double orb = 0.0;
        bool transitTransit = false;
        double weight = 0.0;
    };
    struct TransitAspectPeakResult {
        QDateTime startUtc;
        QDateTime endUtc;
        QDateTime peakUtc;
        int peakHitCount = 0;
        int transitNatalHitCount = 0;
        int transitTransitHitCount = 0;
        double positiveWeight = 0.0;
        double negativeWeight = 0.0;
        double netWeight = 0.0;
        double tightness = 0.0;
        int sampleCount = 0;
        bool groupedPeriod = false;
        QStringList transitBodies;
        QStringList natalTargets;
        QVector<TransitAspectPeakHit> peakHits;
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
        Natal = 0,
        Transits = 1,
        Progression = 2,
        SolarReturn = 3,
        LunarReturn = 4,
        ReturnFinder = 5,
        PlanetaryHours = 9,
        ZodiacalReleasing = 10,
        Lunations = 6,
        Relocation = 7,
        Astrocartography = 8,
        GeodeticEquivalents = 11,
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
    enum class SolarReportPreset {
        Basic,
        Full,
        Custom,
    };
    enum class SolarReportLotScope {
        None,
        Core,
        All,
    };
    enum class SolarReportAspectScope {
        Tight,
        Standard,
        Configured,
    };
    struct SolarReportOptions {
        SolarReportPreset preset = SolarReportPreset::Basic;
        SolarReportLotScope lotScope = SolarReportLotScope::Core;
        SolarReportAspectScope aspectScope = SolarReportAspectScope::Tight;
        bool includeAnnualProfection = true;
        bool includeNatalPositions = true;
        bool includeSolarPositions = true;
        bool includeHouseCusps = true;
        bool includeHouseOverlays = true;
        bool includeSolarNatalAspects = true;
        bool includeSolarSolarAspects = true;
        bool includeNatalNatalAspects = false;
        bool includeMinorBodies = false;
        bool includeDailyMotion = false;
        bool includeDignities = true;
        bool includeFixedStars = false;
    };
    enum class LunarAspectView {
        LunarReturn,
        LunarNatal,
    };
    enum class SolarTechniqueCountingMode {
        SRStartDate,
        SymbolicJanuaryFirst,
    };
    enum class SolarTechniqueBodyPreset {
        Core,
        CoreWithLots,
        FullChartBodies,
        Custom,
    };
    enum class SolarPlacementFinderHouseMode {
        WholeSign,
        Placidus,
        Both,     // Both (OR): matches if either system places it in the house
        BothAnd,  // Both (AND): matches only if both systems agree
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
        AspectPeaks,
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
    enum class AstroSourceMode {
        Natal,
        ProgressedNow,
        ProgressedCustom,
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
        bool showLots = true;
        bool showDerivedPoints = true;
        bool showAsteroids = false;
        bool showAngles = true;
    };

    struct SolarTechniqueBodyFilter {
        bool planets = true;
        bool nodes = true;
        bool angles = true;
        bool lots = false;
        bool asteroids = false;
        bool lilith = false;
        bool vertex = false;

        bool operator==(const SolarTechniqueBodyFilter& other) const = default;
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
        bool isStellium = false;
        int stelliumHouseWhole = 0;
        int stelliumHousePlacidus = 0;
        int stelliumCountWhole = 0;
        int stelliumCountPlacidus = 0;
        QString stelliumBodies;
        bool isHouseRuler = false;
        int rulerOfHouse = 0;
        QString rulerNameWhole;
        QString rulerNamePlacidus;
        bool isProfectionLord = false;
    };

    struct LunarPlacementFinderResult {
        QDateTime localDateTime;
        QDateTime returnUtc;
        QString bodyName;
        int houseWhole = 0;
        int housePlacidus = 0;
        bool matchedWhole = false;
        bool matchedPlacidus = false;
        bool matchedConjunction = false;
        QString matchedAngleName;
        double conjunctionOrb = 0.0;
        QString warning;
        bool isStellium = false;
        int stelliumHouseWhole = 0;
        int stelliumHousePlacidus = 0;
        int stelliumCountWhole = 0;
        int stelliumCountPlacidus = 0;
        QString stelliumBodies;
        bool isHouseRuler = false;
        int rulerOfHouse = 0;
        QString rulerNameWhole;
        QString rulerNamePlacidus;
        bool isProfectionLord = false;
    };

    void setupUi();
    void setupConnections();
    void setupMenuBar();
    void setupDockLayout();
    void refreshWindowTitle();
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
    void handleSolarShiftYear(int delta);
    void handleSolarNow();
    void refreshSolarReturnView();
    void handleSolarPlacementFinderRun();
    void refreshSolarPlacementFinderView();
    void updateSolarFinderModeAvailability();
    void showSolarPlacementFinderResults();
    void showSolarPlacementFinderDetails(int index);
    void handleSolarPlacementFinderResultActivated(int row, int column);
    void showSolarPlaceholder();
    void markLunarPending();
    void updateLunarStatusLabels();
    void updateLunarLocationAvailability();
    void updateLunarTimezoneStatus();
    void syncLunarLocationFromNatal();
    void handleLunarGeocode();
    void fetchLunarTimezoneForCoords(double lat, double lon);
    void handleLunarCalculate();
    void handleLunarPrev();
    void handleLunarNow();
    void handleLunarNext();
    void refreshLunarReturnView();
    void showLunarPlaceholder();
    void handleLunarPlacementFinderRun();
    void refreshLunarPlacementFinderView();
    void updateLunarFinderModeAvailability();
    void showLunarPlacementFinderResults();
    void showLunarPlacementFinderDetails(int index);
    void handleLunarPlacementFinderResultActivated(int row, int column);
    void markLunarPlacementFinderStale();
    bool isLunarPlacementFinderTabActive() const;
    void updateLunarReturnDockTitles();
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
    bool saveCurrentChart();
    bool confirmUnsavedChartChanges();
    bool deleteProfileByName(const QString& profileName);
    bool renameProfileByName(const QString& profileName);
    void showChartManager();
    void setCurrentChartModified(bool modified);
    void refreshProfileToolbar();
    void syncZodiacToolbarControls();
    void syncLunarNodeToolbarControl();
    void syncLunarNodeResearchSelectionDefaults();
    void applyZodiacToolbarSelection(bool recomputeIfChartLoaded);
    void loadUiState();
    void saveUiState();
    void showChartSettingsMenu();
    void showAsteroidSelectionDialog();
    void showFixedStarSelectionDialog();
    void applyChartReadabilityPreset(ChartReadabilityPreset preset);
    void markChartReadabilityCustom();
    void applyAspectDisplayMaxOrb(double maxOrb, bool markCustom = true);
    void refreshAspectMatrixForCurrentView();
    void syncAspectOrbQuickControls();
    bool isAsteroidVisible(const QString& name) const;
    bool isFixedStarVisible(const QString& name) const;

    bool openChartSetupDialog(bool newChart);
    bool computeChart(const dracoved::NatalInput& input, const QString& location);
    void handleRecompute();
    void handleNewChart();
    void handleEditChart();
    void handleSaveProfile();
    void handleLoadProfile();
    void handleDeleteProfile();
    void handleAspectOrbs();
    void handlePreferences();
    void handleMainTabChanged(int index);
    void updateTransitWorkspaceLayout();
    void updateTransitAspectGridVisibility();
    void refreshReturnFinderDocks();
    void handleReturnFinderOpen(const ReturnFinderResult& result, const ReturnFinderQuery& query);
    void refreshPlanetaryHoursDocks();
    void refreshZodiacalReleasingDocks();
    void refreshGeodeticEquivalentsDocks();
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
    void handleTransitAspectPeakStart();
    void handleTransitAspectPeakCancel();
    void handleTransitAspectPeakResultActivated(int row, int column);
    void handleTransitAspectPeakFinished();
    void handleCopyTransitAspectPeakDetails();
    void handleCopyAspects();
    void handleCopyTransitSearchDetails();
    void handleCopyTransitCalendarDetails();
    void handleCopyTransitConjunctionDetails();
    void handleCopyTransitScanDetails();
    void handleCopyLunationDetails();
    void handleCopyReport();
    void applySolarReportBasicPreset();
    void applySolarReportFullPreset();
    void markSolarReportOptionsCustom();
    void updateSolarReportOptionsUi();
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
    SolarTechniqueBodyFilter solarTechniqueBodyFilter() const;
    SolarTechniqueBodyPreset solarTechniqueBodyPresetFromFilter(const SolarTechniqueBodyFilter& filter) const;
    void applySolarTechniqueBodyPreset(SolarTechniqueBodyPreset preset, bool refreshView = true);
    void syncSolarTechniqueBodyPresetSelection(bool refreshView = true);
    QString solarTechniqueBodyPresetLabel(SolarTechniqueBodyPreset preset) const;
    QString solarTechniqueBodySummary(const SolarTechniqueBodyFilter& filter) const;
    bool solarTechniqueIncludesBodyName(const QString& name, const SolarTechniqueBodyFilter& filter) const;
    bool isSolarTechniqueTabActive() const;
    bool isSolarPlacementFinderTabActive() const;
    void updateSolarTechniqueDockTitles();
    void handleProgressionNow();
    void handleProgressionCalculate();
    void handleProgressedLunarReturn(int direction);
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
    void refreshTransitAspectPeakTab();
    void refreshTransitProfectionTab();
    void syncTransitProfectionAgeFromTransitDate();
    void updateTransitScanResultsTable();
    void updateTransitAspectPeakResultsTable();
    void scheduleTransitSearchResultsRefresh();
    void showTransitScanDetails(int index);
    void showTransitAspectPeakDetails(int index);
    QWidget* createTransitAspectPeakPanel(QWidget* parent);
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
    bool inLunationsView() const;
    void refreshLunationsTab();
    QString buildTransitSearchDetailsClipboardText() const;
    QString buildTransitCalendarDetailsClipboardText() const;
    QString buildTransitConjunctionDetailsClipboardText() const;
    QString buildTransitScanDetailsClipboardText() const;
    QString buildTransitAspectPeakClipboardText() const;
    QString buildLunationDetailsClipboardText() const;
    void refreshNatalReport();
    QString buildNatalReportText() const;
    QString buildSolarReturnReportMarkdown() const;
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
    void populateCrossAspectsOverlay(const dracoved::NatalChart& rowChart, const dracoved::NatalChart& natalChart, const QString& rowPrefix);
    struct AspectMatrixCellData {
        bool hasAspect = false;
        QString label;
        double orb = 0.0;
        int applying = -1;  // -1 unknown, 0 separating, 1 applying
    };
    void populateAspectMatrix(const QStringList& rowNames, const QStringList& colNames, bool symmetric,
                              const std::function<AspectMatrixCellData(const QString&, const QString&)>& lookup,
                              const QString& rowPrefix, const QString& colPrefix);
    dracoved::AspectMatrixPalette buildAspectMatrixPalette(ThemeMode mode) const;
    void populateTransitList(const dracoved::NatalChart& transitChart, bool overlayMode);
    void populateTransitAspectsInEffect(const dracoved::NatalChart& transitChart, bool overlayMode);
    void updateTransitListFilterVisibility();
    void populateCurrentTransits(const dracoved::NatalChart& transitChart, const dracoved::NatalChart& natalChart);
    void populateIngressCountdown(const dracoved::NatalChart& transitChart, const dracoved::NatalInput& transitInput);
    bool computeTransitChart(const QDateTime& localTime, const QString& tzLabel, dracoved::NatalChart* out, QString* error);
    bool computeTransitChart(const QDateTime& localTime, const QString& tzLabel,
                             const dracoved::TropicalComputeOptions& options,
                             dracoved::NatalChart* out, QString* error);
    void refreshNatalTransitsPanels();
    void refreshTransitsTab();
    void applyTransitHouseSystem(dracoved::HouseSystem system);
    dracoved::NatalChart natalChartForTransitDisplay() const;
    QDateTime transitSelectedLocal() const;
    QString transitTimezoneLabel() const;
    dracoved::NatalInput transitInputFor(const QDateTime& localTime, const QString& tzLabel) const;
    void updateAstrocartographyModeUi();
    void updateAstroSourceUi();
    void updateAstrocartographyView();
    void updateGeodeticOverlays();
    void handleAstroMapClicked(double latitude, double longitude);
    void handleAstroMapHovered(double latitude, double longitude);
    void flushAstroHoverPreview();
    void clearAstroHoverPreview();
    void handleAstroProgressionNow();
    void refreshAstroClickedLocationView();
    QString astroHoverCacheKey(double latitude, double longitude, double* roundedLatitude, double* roundedLongitude) const;
    QString astroHoverInfoFor(double latitude, double longitude);
    AstroSourceMode astroSourceMode() const;
    dracoved::HouseSystem astroClickedHouseSystem() const;
    bool computeProgressionChartForInput(const dracoved::NatalInput& input, const QDateTime& localTime,
                                         const QString& tzLabel, dracoved::NatalChart* out, QString* error);
    bool computeAstroSourceChart(double latitude, double longitude, dracoved::HouseSystem houseSystem,
                                 dracoved::NatalChart* outChart, dracoved::NatalInput* outInput,
                                 QString* outSourceLabel, QString* error);
    void setWorldMapOverlays(const QVariantList& lineOverlays, const QVariantList& bandOverlays);
    bool resolveSolarReturnContext(QString* outTzLabel, QString* outLocationName, double* outLat, double* outLon,
                                   QString* error) const;
    bool resolveLunarReturnContext(QString* outTzLabel, QString* outLocationName, double* outLat, double* outLon,
                                   QString* error) const;
    bool computeSolarReturnChartPure(int year, const QString& tzLabel, double targetLon, const QString& locationName,
                                     double lat, double lon, dracoved::HouseSystem houseSystem,
                                     dracoved::NatalChart* outChart, dracoved::NatalInput* outInput, QString* error,
                                     const dracoved::TropicalComputeOptions& options = dracoved::TropicalComputeOptions{});
    bool applySolarReturnYear(int year, QString* error = nullptr);
    bool lunarReturnTimeUtc(const QDateTime& anchorUtc, int direction, double targetLon,
                            const QString& tzLabel, QDateTime* outUtc, QDateTime* outLocal, QString* error);
    bool applyLunarReturnAnchor(int direction, bool fromAnchorDate, QString* error = nullptr,
                                const QDateTime& absoluteAnchorUtc = QDateTime());

    QDockWidget* dataDock_ = nullptr;
    QDockWidget* rightTopDock_ = nullptr;
    QDockWidget* transitAspectsDock_ = nullptr;
    QDockWidget* rightBottomDock_ = nullptr;
    QAction* lockLayoutAction_ = nullptr;
    QTabBar* mainTabBar_ = nullptr;
    QFrame* profileToolbarFrame_ = nullptr;
    QComboBox* profileToolbarCombo_ = nullptr;
    QToolButton* profileToolbarNewButton_ = nullptr;
    QToolButton* profileToolbarLoadButton_ = nullptr;
    QToolButton* profileToolbarManageButton_ = nullptr;
    QToolButton* profileToolbarSaveButton_ = nullptr;
    QToolButton* profileToolbarEditButton_ = nullptr;
    QToolButton* profileToolbarDeleteButton_ = nullptr;
    QAction* newChartAction_ = nullptr;
    QAction* openChartAction_ = nullptr;
    QAction* manageChartsAction_ = nullptr;
    QAction* saveChartAction_ = nullptr;
    QAction* editChartAction_ = nullptr;
    QAction* deleteChartAction_ = nullptr;
    QRadioButton* zodiacToolbarTropicalRadio_ = nullptr;
    QRadioButton* zodiacToolbarSiderealRadio_ = nullptr;
    QComboBox* zodiacToolbarAyanamsaCombo_ = nullptr;
    QToolButton* nodeSettingsButton_ = nullptr;
    QLabel* profileToolbarStateLabel_ = nullptr;
    QStackedWidget* dataStack_ = nullptr;
    QSplitter* leftSplitter_ = nullptr;
    QSplitter* chartWorkspaceSplitter_ = nullptr;
    QWidget* chartWheelHost_ = nullptr;
    QList<int> nonTransitLeftSplitterSizes_;
    QList<int> transitWorkspaceSplitterSizes_;
    bool transitWorkspaceLayoutActive_ = false;
    QTabWidget* tabs_ = nullptr;
    QTabBar* aspectScopeTabs_ = nullptr;
    QTableWidget* summaryTable_ = nullptr;
    QTableWidget* anglesTable_ = nullptr;
    QTableWidget* planetsTable_ = nullptr;
    QTableWidget* fixedStarsTable_ = nullptr;
    QTableWidget* housesTable_ = nullptr;
    QTableWidget* aspectsTable_ = nullptr;
    QTableWidget* rightTopTable_ = nullptr;
    QWidget* transitListFilterPanel_ = nullptr;
    QComboBox* transitListFilterCombo_ = nullptr;
    QTableWidget* transitAspectsTable_ = nullptr;
    QLabel* transitAspectsCountLabel_ = nullptr;
    QPushButton* transitAspectsCopyButton_ = nullptr;
    QTableWidget* rightBottomTable_ = nullptr;
    QPushButton* rightBottomCopyButton_ = nullptr;
    QWidget* transitPanel_ = nullptr;
    QTabBar* transitSubTabBar_ = nullptr;
    QStackedWidget* transitPanelStack_ = nullptr;
    QWidget* transitOverviewPanel_ = nullptr;
    QWidget* transitSearchPanel_ = nullptr;
    QWidget* transitCalendarPanel_ = nullptr;
    QWidget* transitConjunctionPanel_ = nullptr;
    QWidget* transitAspectPeakPanel_ = nullptr;
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
    QPushButton* transitMinusWeekButton_ = nullptr;
    QPushButton* transitMinusDayButton_ = nullptr;
    QPushButton* transitNowButton_ = nullptr;
    QPushButton* transitPlusDayButton_ = nullptr;
    QPushButton* transitPlusWeekButton_ = nullptr;
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
    QComboBox* lunationEclipseRuleCombo_ = nullptr;
    QCheckBox* lunationPlanetConjCheck_ = nullptr;
    QComboBox* lunationPlanetCombo_ = nullptr;
    QCheckBox* lunationConjSunCheck_ = nullptr;
    QCheckBox* lunationConjMoonCheck_ = nullptr;
    QCheckBox* lunationConjNorthNodeCheck_ = nullptr;
    QCheckBox* lunationConjSouthNodeCheck_ = nullptr;
    QDoubleSpinBox* lunationConjOrbSpin_ = nullptr;
    QCheckBox* lunationOverlayCheck_ = nullptr;
    bool lunationOverlay_ = true;
    QWidget* lunationsPanel_ = nullptr;
    int lunationsDataStackIndex_ = -1;
    int astrocartographyDataStackIndex_ = -1;
    int returnFinderDataStackIndex_ = -1;
    int planetaryHoursDataStackIndex_ = -1;
    int zodiacalReleasingDataStackIndex_ = -1;
    int geodeticEquivalentsDataStackIndex_ = -1;
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
    QGroupBox* aspectPeakRangeGroup_ = nullptr;
    QGroupBox* aspectPeakSelectionGroup_ = nullptr;
    QGroupBox* aspectPeakSettingsGroup_ = nullptr;
    QGroupBox* aspectPeakWeightingGroup_ = nullptr;
    QWidget* aspectPeakWeightingOptions_ = nullptr;
    QDateEdit* aspectPeakStartDateEdit_ = nullptr;
    QTimeEdit* aspectPeakStartTimeEdit_ = nullptr;
    QDateEdit* aspectPeakEndDateEdit_ = nullptr;
    QTimeEdit* aspectPeakEndTimeEdit_ = nullptr;
    QLineEdit* aspectPeakTimezoneEdit_ = nullptr;
    QComboBox* aspectPeakTransitBodiesCombo_ = nullptr;
    QComboBox* aspectPeakNatalTargetsCombo_ = nullptr;
    QComboBox* aspectPeakAspectsCombo_ = nullptr;
    QDoubleSpinBox* aspectPeakOrbSpin_ = nullptr;
    QComboBox* aspectPeakResolutionCombo_ = nullptr;
    QComboBox* aspectPeakModeCombo_ = nullptr;
    QCheckBox* aspectPeakIncludeTransitTransitCheck_ = nullptr;
    QDoubleSpinBox* aspectPeakConjunctionWeightSpin_ = nullptr;
    QDoubleSpinBox* aspectPeakSextileWeightSpin_ = nullptr;
    QDoubleSpinBox* aspectPeakSquareWeightSpin_ = nullptr;
    QDoubleSpinBox* aspectPeakTrineWeightSpin_ = nullptr;
    QDoubleSpinBox* aspectPeakOppositionWeightSpin_ = nullptr;
    QComboBox* aspectPeakWeightRankingCombo_ = nullptr;
    QSpinBox* aspectPeakMinHitsSpin_ = nullptr;
    QSpinBox* aspectPeakTopCountSpin_ = nullptr;
    QPushButton* aspectPeakRunButton_ = nullptr;
    QPushButton* aspectPeakCancelButton_ = nullptr;
    QProgressBar* aspectPeakProgressBar_ = nullptr;
    QLabel* aspectPeakStatusLabel_ = nullptr;
    QLabel* profectionReferenceLabel_ = nullptr;
    QSpinBox* profectionAgeSpin_ = nullptr;
    QPushButton* profectionUseTransitAgeButton_ = nullptr;
    QPushButton* profectionRunButton_ = nullptr;
    QLabel* profectionStatusLabel_ = nullptr;
    QToolButton* chartSettingsButton_ = nullptr;
    QToolButton* transitAspectGridToggleButton_ = nullptr;
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
    QFrame* aspectOrbQuickPanel_ = nullptr;
    QToolButton* aspectOrbPreset1Button_ = nullptr;
    QToolButton* aspectOrbPreset2Button_ = nullptr;
    QToolButton* aspectOrbPreset3Button_ = nullptr;
    QDoubleSpinBox* aspectOrbCustomSpin_ = nullptr;
    QWidget* progressionControls_ = nullptr;
    QRadioButton* progressionViewNatalRadio_ = nullptr;
    QRadioButton* progressionViewProgressedRadio_ = nullptr;
    QRadioButton* progressionViewOverlayRadio_ = nullptr;
    QDateEdit* progressionDateEdit_ = nullptr;
    QTimeEdit* progressionTimeEdit_ = nullptr;
    QLineEdit* progressionTimezoneEdit_ = nullptr;
    QLabel* progressionTimezoneStatus_ = nullptr;
    QPushButton* progressionNowButton_ = nullptr;
    QPushButton* progressionLunarReturnPreviousButton_ = nullptr;
    QPushButton* progressionLunarReturnNextButton_ = nullptr;
    QLabel* progressionLunarReturnStatusLabel_ = nullptr;
    QPushButton* progressionCalculateButton_ = nullptr;
    QLabel* progressionStatusLabel_ = nullptr;
    QLabel* progressionLastLabel_ = nullptr;
    QPushButton* aspectsCopyButton_ = nullptr;
    QToolButton* aspectGridSettingsButton_ = nullptr;
    QWidget* reportPanel_ = nullptr;
    QTextEdit* reportText_ = nullptr;
    QToolButton* reportOptionsButton_ = nullptr;
    QPushButton* reportCopyButton_ = nullptr;
    QAction* solarReportBasicPresetAction_ = nullptr;
    QAction* solarReportFullPresetAction_ = nullptr;
    QAction* solarReportAnnualProfectionAction_ = nullptr;
    QAction* solarReportNatalPositionsAction_ = nullptr;
    QAction* solarReportSolarPositionsAction_ = nullptr;
    QAction* solarReportHouseCuspsAction_ = nullptr;
    QAction* solarReportHouseOverlaysAction_ = nullptr;
    QAction* solarReportSolarNatalAspectsAction_ = nullptr;
    QAction* solarReportSolarSolarAspectsAction_ = nullptr;
    QAction* solarReportNatalNatalAspectsAction_ = nullptr;
    QAction* solarReportMinorBodiesAction_ = nullptr;
    QAction* solarReportDailyMotionAction_ = nullptr;
    QAction* solarReportDignitiesAction_ = nullptr;
    QAction* solarReportFixedStarsAction_ = nullptr;
    QAction* solarReportNoLotsAction_ = nullptr;
    QAction* solarReportCoreLotsAction_ = nullptr;
    QAction* solarReportAllLotsAction_ = nullptr;
    QAction* solarReportTightAspectsAction_ = nullptr;
    QAction* solarReportStandardAspectsAction_ = nullptr;
    QAction* solarReportConfiguredAspectsAction_ = nullptr;
    SolarReportOptions solarReportOptions_;
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
    QPushButton* solarPreviousButton_ = nullptr;
    QPushButton* solarNowButton_ = nullptr;
    QPushButton* solarNextButton_ = nullptr;
    QPushButton* solarCalculateButton_ = nullptr;
    QLabel* solarStatusLabel_ = nullptr;
    QLabel* solarLastLabel_ = nullptr;
    QWidget* lunarControls_ = nullptr;
    QDateEdit* lunarAnchorDateEdit_ = nullptr;
    QRadioButton* lunarUseNatalRadio_ = nullptr;
    QRadioButton* lunarUseCustomRadio_ = nullptr;
    QLineEdit* lunarLocationEdit_ = nullptr;
    QPushButton* lunarGeocodeButton_ = nullptr;
    QDoubleSpinBox* lunarLatSpin_ = nullptr;
    QDoubleSpinBox* lunarLonSpin_ = nullptr;
    QLineEdit* lunarTimezoneEdit_ = nullptr;
    QLabel* lunarTimezoneStatus_ = nullptr;
    QPushButton* lunarPrevButton_ = nullptr;
    QPushButton* lunarNowButton_ = nullptr;
    QPushButton* lunarNextButton_ = nullptr;
    QPushButton* lunarCalculateButton_ = nullptr;
    QLabel* lunarStatusLabel_ = nullptr;
    QLabel* lunarLastLabel_ = nullptr;
    QWidget* lunarPlacementFinderPanel_ = nullptr;
    QDateEdit* lunarFinderStartDateEdit_ = nullptr;
    QDateEdit* lunarFinderEndDateEdit_ = nullptr;
    QComboBox* lunarFinderModeCombo_ = nullptr;
    QComboBox* lunarFinderPlanetCombo_ = nullptr;
    QSpinBox* lunarFinderStelliumCountSpin_ = nullptr;
    QComboBox* lunarFinderHouseCombo_ = nullptr;
    QComboBox* lunarFinderHouseSystemCombo_ = nullptr;
    QComboBox* lunarFinderConjunctionTargetCombo_ = nullptr;
    QDoubleSpinBox* lunarFinderConjunctionOrbSpin_ = nullptr;
    QComboBox* lunarFinderRulerHouseCombo_ = nullptr;
    QComboBox* lunarFinderRulerSchemeCombo_ = nullptr;
    QComboBox* lunarFinderPlanet2Combo_ = nullptr;
    QPushButton* lunarFinderRunButton_ = nullptr;
    QLabel* lunarFinderStatusLabel_ = nullptr;
    QComboBox* solarTechniqueModeCombo_ = nullptr;
    QComboBox* solarTechniqueBodyPresetCombo_ = nullptr;
    QDateEdit* solarTechniqueDateEdit_ = nullptr;
    QDoubleSpinBox* solarTechniqueOrbSpin_ = nullptr;
    QCheckBox* solarTechniqueBodyPlanetsCheck_ = nullptr;
    QCheckBox* solarTechniqueBodyNodesCheck_ = nullptr;
    QCheckBox* solarTechniqueBodyAnglesCheck_ = nullptr;
    QCheckBox* solarTechniqueBodyLotsCheck_ = nullptr;
    QCheckBox* solarTechniqueBodyAsteroidsCheck_ = nullptr;
    QCheckBox* solarTechniqueBodyLilithCheck_ = nullptr;
    QCheckBox* solarTechniqueBodyVertexCheck_ = nullptr;
    QCheckBox* solarTechniqueNatalCheck_ = nullptr;
    QCheckBox* solarTechniqueSolarCheck_ = nullptr;
    QLabel* solarTechniqueRangeLabel_ = nullptr;
    QComboBox* solarTechniqueRankMetricCombo_ = nullptr;
    QComboBox* solarTechniqueRankOrderCombo_ = nullptr;
    QSpinBox* solarTechniqueTopSpin_ = nullptr;
    bool solarTechniqueUpdatingBodyControls_ = false;
    QSpinBox* solarFinderStartYearSpin_ = nullptr;
    QSpinBox* solarFinderEndYearSpin_ = nullptr;
    QComboBox* solarFinderPlanetCombo_ = nullptr;
    QComboBox* solarFinderModeCombo_ = nullptr;
    QSpinBox* solarFinderStelliumCountSpin_ = nullptr;    QComboBox* solarFinderHouseCombo_ = nullptr;
    QComboBox* solarFinderHouseSystemCombo_ = nullptr;
    QComboBox* solarFinderConjunctionTargetCombo_ = nullptr;
    QDoubleSpinBox* solarFinderConjunctionOrbSpin_ = nullptr;
    QComboBox* solarFinderRulerHouseCombo_ = nullptr;
    QComboBox* solarFinderRulerSchemeCombo_ = nullptr;
    QComboBox* solarFinderPlanet2Combo_ = nullptr;
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
    ReturnFinderController* returnFinderController_ = nullptr;
    PlanetaryHoursController* planetaryHoursController_ = nullptr;
    ZodiacalReleasingController* zodiacalReleasingController_ = nullptr;
    GeodeticEquivalentsController* geodeticEquivalentsController_ = nullptr;
    QWidget* chartViewPanel_ = nullptr;
    QWidget* worldMapPanel_ = nullptr;
    AstroMapWidget* astroMapWidget_ = nullptr;
    QObject* worldMapRoot_ = nullptr;
    QWidget* astrocartographyPanel_ = nullptr;
    QComboBox* astroModeCombo_ = nullptr;
    QLabel* astroModeHintLabel_ = nullptr;
    QGroupBox* geodeticGroup_ = nullptr;
    QComboBox* astroSourceCombo_ = nullptr;
    QLabel* astroProgressionTargetLabel_ = nullptr;
    QWidget* astroProgressionTargetRow_ = nullptr;
    QDateEdit* astroProgressionDateEdit_ = nullptr;
    QTimeEdit* astroProgressionTimeEdit_ = nullptr;
    QLineEdit* astroProgressionTimezoneEdit_ = nullptr;
    QLabel* astroProgressionTimezoneStatus_ = nullptr;
    QPushButton* astroProgressionNowButton_ = nullptr;
    QComboBox* geodeticPlanetCombo_ = nullptr;
    QRadioButton* geodeticExactRadio_ = nullptr;
    QRadioButton* geodeticOrbRadio_ = nullptr;
    QComboBox* geodeticOrbCombo_ = nullptr;
    QLabel* geodeticTimeLabel_ = nullptr;
    QCheckBox* astroLineAcCheck_ = nullptr;
    QCheckBox* astroLineDcCheck_ = nullptr;
    QCheckBox* astroLineMcCheck_ = nullptr;
    QCheckBox* astroLineIcCheck_ = nullptr;
    QCheckBox* astroHarmoniousAspectsCheck_ = nullptr;
    QCheckBox* astroDisharmoniousAspectsCheck_ = nullptr;
    QComboBox* astroClickedHouseCombo_ = nullptr;
    QGroupBox* astroPreviewGroup_ = nullptr;
    ChartWheelWidget* astroPreviewWheel_ = nullptr;
    QLabel* astroPreviewStatusLabel_ = nullptr;
    QPushButton* astroWorldButton_ = nullptr;
    QPushButton* astroBirthplaceButton_ = nullptr;
    QPushButton* geodeticRefreshButton_ = nullptr;
    QLabel* geodeticStatusLabel_ = nullptr;
    QTimer* astroHoverTimer_ = nullptr;
    QHash<QString, QString> astroHoverCache_;
    QString astroPendingHoverKey_;
    double astroPendingHoverLat_ = 0.0;
    double astroPendingHoverLon_ = 0.0;
    bool hasAstroPendingHover_ = false;
    bool worldMapReady_ = false;
    bool hasAstroSelectedLocation_ = false;
    double astroSelectedLat_ = 0.0;
    double astroSelectedLon_ = 0.0;
    bool hasAstroSourceChart_ = false;
    QString astroSourceLabel_;
    dracoved::NatalInput astroSourceInput_;
    dracoved::NatalChart astroSourceChart_;
    dracoved::NatalChart astroSelectedChart_;
    bool syncingZodiacToolbar_ = false;
    QByteArray defaultDockState_;
    bool layoutLocked_ = false;
    bool hasCurrentChart_ = false;
    bool hasTransitChart_ = false;
    bool hasProgressionChart_ = false;
    bool hasSolarChart_ = false;
    bool hasLunarChart_ = false;
    bool hasRelocationChart_ = false;
    QString currentLocation_;
    QString currentProfileName_;
    bool currentChartModified_ = false;
    dracoved::NatalInput currentInput_;
    dracoved::NatalChart currentChart_;
    dracoved::NatalChart currentTransitChart_;
    dracoved::NatalChart currentProgressionChart_;
    dracoved::NatalChart currentSolarChart_;
    dracoved::NatalChart currentLunarChart_;
    dracoved::NatalChart currentRelocationChart_;
    dracoved::NatalInput currentProgressionInput_;
    dracoved::NatalInput currentSolarInput_;
    dracoved::NatalInput currentLunarInput_;
    dracoved::NatalInput currentRelocationInput_;
    QString currentSolarLocation_;
    QString currentLunarLocation_;
    QString currentRelocationLocation_;
    QVector<dracoved::HouseCusp> natalPlacidusCusps_;
    dracoved::HouseSystem defaultHouseSystem_ = dracoved::HouseSystem::WholeSign;
    dracoved::LunarNodePolicy defaultLunarNodePolicy_;
    dracoved::ZodiacalReleasingSettings defaultZodiacalReleasingSettings_;
    dracoved::AspectOrbs aspectOrbs_ = dracoved::defaultAspectOrbs();
    AppTab activeTab_ = AppTab::Natal;
    TransitMode transitMode_ = TransitMode::NatalOverlay;
    TransitAspectView transitAspectView_ = TransitAspectView::TransitNatal;
    TransitSubTab transitSubTab_ = TransitSubTab::Overview;
    dracoved::HouseSystem transitHouseSystem_ = dracoved::HouseSystem::WholeSign;
    bool transitAspectGridVisible_ = true;
    bool transitPending_ = false;
    bool transitSpaceNavigationHeld_ = false;
    bool progressionPending_ = false;
    bool progressionIsLunarReturn_ = false;
    dracoved::ProgressedLunarReturnEvent currentProgressedLunarReturn_;
    bool solarPending_ = false;
    bool lunarPending_ = false;
    bool relocationPending_ = false;
    QDateTime lastTransitCalculated_;
    QDateTime lastProgressionCalculated_;
    QDateTime lastSolarCalculated_;
    QDateTime lastLunarCalculated_;
    QDateTime lastRelocationCalculated_;
    QDateTime currentLunarReturnUtc_;
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
    QVector<TransitAspectPeakResult> transitAspectPeakResults_;
    QVector<int> transitAspectPeakDisplayOrder_;
    bool hasTransitAspectPeakSelection_ = false;
    TransitAspectPeakResult lastTransitAspectPeakSelection_;
    bool transitAspectPeakRunning_ = false;
    QThread* aspectPeakThread_ = nullptr;
    QObject* aspectPeakWorker_ = nullptr;
    QTimeZone aspectPeakLastTz_;
    QString aspectPeakLastTzLabel_;
    QDateTime aspectPeakLastRangeStartUtc_;
    QDateTime aspectPeakLastRangeEndUtc_;
    QStringList aspectPeakLastTransitBodies_;
    QStringList aspectPeakLastNatalTargets_;
    QStringList aspectPeakLastAspects_;
    double aspectPeakLastOrb_ = 1.0;
    int aspectPeakLastResolutionMinutes_ = 360;
    int aspectPeakLastMinHits_ = 2;
    bool aspectPeakLastGroupedPeriods_ = false;
    bool aspectPeakLastIncludeTransitTransit_ = false;
    bool aspectPeakLastWeightingEnabled_ = false;
    QMap<QString, double> aspectPeakLastAspectWeights_;
    QString aspectPeakLastWeightRanking_ = "hits";
    QThread* searchThread_ = nullptr;
    SearchWorker* searchWorker_ = nullptr;
    QThread* calendarThread_ = nullptr;
    QObject* calendarWorker_ = nullptr;
    QTimer* calendarRecomputeTimer_ = nullptr;
    QTimer* searchResultsRefreshTimer_ = nullptr;
    bool searchResultsDirty_ = false;
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
    bool solarPlacementFinderLastStelliumMode_ = false;
    int solarPlacementFinderLastStelliumMin_ = 3;
    bool solarPlacementFinderLastAnyHouse_ = false;
    bool solarPlacementFinderLastRulerMode_ = false;
    int solarPlacementFinderLastRulerOfHouse_ = 7;
    bool solarPlacementFinderLastRulerModern_ = false;
    bool solarPlacementFinderLastProfectionMode_ = false;
    QVector<LunarPlacementFinderResult> lunarPlacementFinderResults_;
    QStringList lunarPlacementFinderWarnings_;
    bool lunarPlacementFinderRan_ = false;
    bool lunarPlacementFinderStale_ = false;
    int lunarPlacementFinderSelectedIndex_ = -1;
    int lunarPlacementFinderLastSearchedCount_ = 0;
    int lunarPlacementFinderLastFailedCount_ = 0;
    QDate lunarPlacementFinderLastStartDate_;
    QDate lunarPlacementFinderLastEndDate_;
    QString lunarPlacementFinderLastPlanet_;
    int lunarPlacementFinderLastHouse_ = 1;
    SolarPlacementFinderHouseMode lunarPlacementFinderLastHouseMode_ = SolarPlacementFinderHouseMode::WholeSign;
    QString lunarPlacementFinderLastConjunctionTarget_ = "None";
    double lunarPlacementFinderLastConjunctionOrb_ = 1.0;
    bool lunarPlacementFinderLastStelliumMode_ = false;
    int lunarPlacementFinderLastStelliumMin_ = 3;
    bool lunarPlacementFinderLastAnyHouse_ = false;
    bool lunarPlacementFinderLastRulerMode_ = false;
    int lunarPlacementFinderLastRulerOfHouse_ = 7;
    bool lunarPlacementFinderLastRulerModern_ = false;
    bool lunarPlacementFinderLastProfectionMode_ = false;
    LunationAnalysisMode lunationAnalysisMode_ = LunationAnalysisMode::List;
    QThread* lunationThread_ = nullptr;
    QObject* lunationWorker_ = nullptr;
    ThemeMode theme_ = ThemeMode::Light;
    AspectHeaderMode aspectHeaderMode_ = AspectHeaderMode::Abbrev;
    ProgressionView progressionView_ = ProgressionView::ProgressedOnly;
    SolarAspectView solarAspectView_ = SolarAspectView::SolarReturn;
    LunarAspectView lunarAspectView_ = LunarAspectView::LunarReturn;
    RelocationAspectView relocationAspectView_ = RelocationAspectView::Relocation;
    dracoved::HouseSystem relocationHouseSystem_ = dracoved::HouseSystem::WholeSign;
    bool aspectTriangleEnabled_ = false;
    dracoved::AspectMatrixDelegate* aspectDelegate_ = nullptr;
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
