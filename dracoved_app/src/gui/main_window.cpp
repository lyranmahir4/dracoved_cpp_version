#include "main_window.h"
#include "aspect_orbs_dialog.h"
#include "chart_setup_dialog.h"
#include "chart_wheel_widget.h"
#include "transit_calc_service.h"
#include "transit_workers.h"

#include "../core/fixed_stars.h"
#include "../core/formatting.h"
#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QDate>
#include <QDir>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMap>
#include <QLocale>
#include <QSet>
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
// Astrocartography / Geodetic world map support (QtLocation/QML).
// Keep this guard in place so builds work without QtLocation installed.
#include <QGeoCoordinate>
#include <QQuickWidget>
#endif
#include <QSignalBlocker>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <cmath>
#include <algorithm>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTime>
#include <QTimeEdit>
#include <QTimer>
#include <QDateEdit>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QUrl>
#include <QEvent>
#include <QVBoxLayout>
#include <QDockWidget>
#include <QGridLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSplitter>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QComboBox>
#include <QStandardItemModel>
#include <QThread>
#include <QProgressBar>
#include <QTextEdit>
#include <functional>
#include <atomic>

#include "../core/timezone_utils.h"

namespace dracoved {

static void setupTable(QTableWidget* table, const QStringList& headers, int rows);
static void setupDetailTable(QTableWidget* table, const QStringList& headers, int rows);
static QTableWidgetItem* makeCell(const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter);
static int calcHouseForLongitude(double lon, const QVector<HouseCusp>& cusps, double asc, HouseSystem system);
static double angularDiff(double a, double b);
static QString aspectTargetFromLabel(const QString& text);
static bool findBodyLongitude(const NatalChart& chart, const QString& name, double* outLon);
static bool findAngleLongitude(const NatalChart& chart, const QString& name, double* outLon);
static QString abbrevForName(const QString& name);
static bool aspectForDiff(double diff, const AspectOrbs& orbs, QString* outLabel, double* outOrb, double* outMaxOrb);

namespace {

class ComboPopupOnClick : public QObject {
public:
    explicit ComboPopupOnClick(QComboBox* combo)
        : QObject(combo),
          combo_(combo) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (!combo_) {
            return QObject::eventFilter(obj, event);
        }
        if (event->type() == QEvent::MouseButtonPress) {
            combo_->setFocus();
            combo_->showPopup();
            return true;
        }
        return QObject::eventFilter(obj, event);
    }

private:
    QComboBox* combo_ = nullptr;
};

static double angularDiffSigned(double a, double b) {
    double d = std::fmod((a - b + 540.0), 360.0) - 180.0;
    return d;
}

static double angularDiffAbs(double a, double b) {
    return std::fabs(angularDiffSigned(a, b));
}

static bool isNodeName(const QString& name) {
    return name == "North Node" || name == "South Node";
}

static bool isAngleName(const QString& name) {
    return name == "Ascendant" || name == "Midheaven" || name == "Descendant" || name == "IC";
}

static bool isDerivedPointName(const QString& name) {
    return name == "Vertex" || isArabicLotName(name);
}

static bool isBenefic(const QString& name) {
    return name == "Venus" || name == "Jupiter";
}

static bool isMalefic(const QString& name) {
    return name == "Mars" || name == "Saturn";
}

static double bodyWeightFor(const QString& name) {
    if (isAngleName(name)) {
        return 1.0;
    }
    if (name == "Sun" || name == "Moon") {
        return 1.0;
    }
    if (name == "Mercury" || name == "Venus" || name == "Mars") {
        return 0.8;
    }
    if (name == "Jupiter" || name == "Saturn") {
        return 0.7;
    }
    if (name == "Uranus" || name == "Neptune" || name == "Pluto") {
        return 0.5;
    }
    if (name == "Chiron") {
        return 0.4;
    }
    if (isAsteroidBody(name)) {
        return 0.35;
    }
    if (isNodeName(name)) {
        return 0.4;
    }
    if (isArabicLotName(name)) {
        return 0.45;
    }
    return 0.6;
}

static int bodyIdForName(const QString& name) {
    if (name == "Sun") return SE_SUN;
    if (name == "Moon") return SE_MOON;
    if (name == "Mercury") return SE_MERCURY;
    if (name == "Venus") return SE_VENUS;
    if (name == "Mars") return SE_MARS;
    if (name == "Jupiter") return SE_JUPITER;
    if (name == "Saturn") return SE_SATURN;
    if (name == "Uranus") return SE_URANUS;
    if (name == "Neptune") return SE_NEPTUNE;
    if (name == "Pluto") return SE_PLUTO;
    if (name == "Chiron") return SE_CHIRON;
    if (name == "Pholus") return SE_PHOLUS;
    if (name == "Ceres") return SE_CERES;
    if (name == "Pallas") return SE_PALLAS;
    if (name == "Juno") return SE_JUNO;
    if (name == "Vesta") return SE_VESTA;
    if (name == "North Node" || name == "South Node") return SE_MEAN_NODE;
    if (name == "Lilith") return SE_MEAN_APOG;
    return -1;
}

static bool isComputableBody(const QString& name) {
    return bodyIdForName(name) >= 0;
}

static QStringList transitCalculableBodyOrder() {
    QStringList bodies;
    for (const auto& name : tropicalBodyOrder()) {
        if (isAngleName(name) || isDerivedPointName(name)) {
            continue;
        }
        if (!isComputableBody(name)) {
            continue;
        }
        bodies.push_back(name);
    }
    return bodies;
}

static QStringList geodeticBodyOrder() {
    QStringList bodies;
    for (const auto& name : tropicalBodyOrder()) {
        if (isAngleName(name)) {
            continue;
        }
        if (isDerivedPointName(name) || isAsteroidBody(name)) {
            continue;
        }
        bodies.push_back(name);
    }
    return bodies;
}

static QColor geodeticColorForIndex(int index) {
    static const QVector<QColor> palette = {
        QColor("#e74c3c"),
        QColor("#f1c40f"),
        QColor("#2ecc71"),
        QColor("#3498db"),
        QColor("#9b59b6"),
        QColor("#e67e22"),
        QColor("#1abc9c"),
        QColor("#95a5a6"),
        QColor("#34495e"),
        QColor("#d35400"),
        QColor("#7f8c8d"),
        QColor("#8e44ad"),
        QColor("#16a085"),
        QColor("#27ae60"),
        QColor("#2980b9"),
        QColor("#c0392b"),
    };
    if (palette.isEmpty()) {
        return QColor("#2c3e50");
    }
    const int idx = (index >= 0) ? (index % palette.size()) : 0;
    return palette[idx];
}

static double aspectAngleForLabel(const QString& label) {
    if (label == "Conjunction") return 0.0;
    if (label == "Sextile") return 60.0;
    if (label == "Square") return 90.0;
    if (label == "Trine") return 120.0;
    if (label == "Opposition") return 180.0;
    return 0.0;
}

static double clampStepDays(double speedAbs) {
    const double minStep = 0.05;
    const double maxStep = 5.0;
    if (speedAbs <= 0.001) {
        return maxStep;
    }
    double step = 2.0 / speedAbs;
    if (step < minStep) {
        return minStep;
    }
    if (step > maxStep) {
        return maxStep;
    }
    return step;
}

static QDateTime midTimeUtc(const QDateTime& a, const QDateTime& b) {
    const qint64 half = a.secsTo(b) / 2;
    return a.addSecs(half);
}

static QStringList checkedItemsFromModel(QStandardItemModel* model) {
    QStringList results;
    if (!model) {
        return results;
    }
    for (int i = 1; i < model->rowCount(); ++i) {
        auto* item = model->item(i);
        if (item && item->checkState() == Qt::Checked) {
            results.push_back(item->text());
        }
    }
    return results;
}

static QStringList selectedTransitPlanets(QComboBox* combo) {
    if (!combo) {
        return {};
    }
    auto* model = qobject_cast<QStandardItemModel*>(combo->model());
    if (model) {
        return checkedItemsFromModel(model);
    }
    const QString selection = combo->currentText();
    if (selection.isEmpty()) {
        return {};
    }
    if (selection == "All") {
        return transitCalculableBodyOrder();
    }
    return {selection};
}

static QStringList selectedCheckableItems(QComboBox* combo) {
    if (!combo) {
        return {};
    }
    auto* model = qobject_cast<QStandardItemModel*>(combo->model());
    if (!model) {
        return {};
    }
    return checkedItemsFromModel(model);
}

static void updateCheckableComboLabel(QComboBox* combo) {
    if (!combo) {
        return;
    }
    auto* model = qobject_cast<QStandardItemModel*>(combo->model());
    if (!model) {
        return;
    }
    const QStringList selected = checkedItemsFromModel(model);
    const int planetCount = std::max(0, model->rowCount() - 1);
    QString label;
    if (selected.isEmpty()) {
        label = "None";
    } else if (planetCount > 0 && selected.size() == planetCount) {
        label = "All";
    } else if (selected.size() <= 3) {
        label = selected.join(", ");
    } else {
        label = QString("%1 selected").arg(selected.size());
    }
    const QSignalBlocker blocker(combo);
    combo->setEditText(label);
}

static void updateTransitPlanetComboLabel(QComboBox* combo) {
    updateCheckableComboLabel(combo);
}

static QVector<BodyPosition> orderedBodiesForDetails(const NatalChart& chart) {
    QMap<QString, BodyPosition> bodyMap;
    for (const auto& body : chart.bodies) {
        bodyMap.insert(body.name, body);
    }

    QVector<BodyPosition> orderedBodies;
    orderedBodies.reserve(bodyMap.size());
    for (const auto& name : tropicalBodyOrder()) {
        if (!bodyMap.contains(name)) {
            continue;
        }
        orderedBodies.push_back(bodyMap.value(name));
        bodyMap.remove(name);
    }
    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        orderedBodies.push_back(it.value());
    }
    return orderedBodies;
}

static void appendBodyPlacementsMarkdown(QStringList* lines, const NatalChart& chart) {
    if (!lines) {
        return;
    }
    lines->push_back("| Body | Degree | Sign | House | Motion |");
    lines->push_back("| --- | --- | --- | --- | --- |");

    const QVector<BodyPosition> orderedBodies = orderedBodiesForDetails(chart);
    for (const auto& body : orderedBodies) {
        lines->push_back(QString("| %1 | %2 | %3 | %4 | %5 |")
            .arg(body.name)
            .arg(formatDegOnly(body.longitude))
            .arg(signName(signIndex(body.longitude)))
            .arg(body.house > 0 ? QString::number(body.house) : "-")
            .arg(body.retrograde ? "R" : "D"));
    }
}

static void appendFixedStarsMarkdown(QStringList* lines, const NatalChart& chart) {
    if (!lines || chart.fixedStars.isEmpty()) {
        return;
    }
    QVector<FixedStarPosition> stars = chart.fixedStars;
    std::sort(stars.begin(), stars.end(), [](const FixedStarPosition& a, const FixedStarPosition& b) {
        return a.longitude < b.longitude;
    });
    lines->push_back("");
    lines->push_back("| Fixed Star | Degree | Sign | House |");
    lines->push_back("| --- | --- | --- | --- |");
    for (const auto& star : stars) {
        lines->push_back(QString("| %1 | %2 | %3 | %4 |")
            .arg(star.name)
            .arg(formatDegOnly(star.longitude))
            .arg(signName(signIndex(star.longitude)))
            .arg(star.house > 0 ? QString::number(star.house) : "-"));
    }
}

static QString formatDegreeDms(double deg) {
    if (std::isnan(deg)) {
        return "N/A";
    }
    double v = deg;
    while (v < 0.0) {
        v += 30.0;
    }
    while (v >= 30.0) {
        v -= 30.0;
    }
    int whole = static_cast<int>(v);
    double minutesFull = (v - whole) * 60.0;
    int minutes = static_cast<int>(minutesFull);
    double seconds = (minutesFull - minutes) * 60.0;
    if (seconds >= 59.995) {
        seconds = 0.0;
        minutes += 1;
    }
    if (minutes >= 60) {
        minutes -= 60;
        whole += 1;
    }
    if (whole >= 30) {
        whole -= 30;
    }
    return QString("%1°%2'%3\"")
        .arg(QString::number(whole).rightJustified(2, '0'))
        .arg(QString::number(minutes).rightJustified(2, '0'))
        .arg(QString::number(seconds, 'f', 0).rightJustified(2, '0'));
}

struct NamedLongitude {
    QString label;
    double lon = 0.0;
};

static QVector<NamedLongitude> collectAngleAndLotRows(const NatalChart& chart) {
    QVector<NamedLongitude> rows = {
        {"Ascendant", chart.angles.asc},
        {"Midheaven", chart.angles.mc},
        {"Descendant", chart.angles.desc},
        {"IC", chart.angles.ic},
        {"Vertex", chart.angles.vertex},
    };

    QMap<QString, double> bodyMap;
    for (const auto& body : chart.bodies) {
        bodyMap.insert(body.name, body.longitude);
    }
    for (const auto& lotName : arabicLotOrder()) {
        if (bodyMap.contains(lotName)) {
            rows.push_back({lotName, bodyMap.value(lotName)});
        } else if (lotName == "Part of Fortune" && chart.hasPartOfFortune) {
            rows.push_back({lotName, chart.partOfFortune});
        }
    }
    return rows;
}

static int completedYearsBetween(const QDate& birthDate, const QDate& referenceDate) {
    if (!birthDate.isValid() || !referenceDate.isValid()) {
        return 0;
    }
    int years = referenceDate.year() - birthDate.year();
    if (referenceDate < birthDate.addYears(years)) {
        --years;
    }
    return std::max(0, years);
}

static QString traditionalRulerForSign(int signIdx) {
    static const QStringList rulers = {
        "Mars",     // Aries
        "Venus",    // Taurus
        "Mercury",  // Gemini
        "Moon",     // Cancer
        "Sun",      // Leo
        "Mercury",  // Virgo
        "Venus",    // Libra
        "Mars",     // Scorpio
        "Jupiter",  // Sagittarius
        "Saturn",   // Capricorn
        "Saturn",   // Aquarius
        "Jupiter",  // Pisces
    };
    if (signIdx < 0 || signIdx >= rulers.size()) {
        return QString();
    }
    return rulers[signIdx];
}

static int signDistance(int fromSign, int toSign) {
    return (toSign - fromSign + 12) % 12;
}

static bool isSquareOrOppSign(int fromSign, int toSign) {
    const int distance = signDistance(fromSign, toSign);
    return distance == 3 || distance == 6 || distance == 9;
}

static bool isConjOrTrineSign(int fromSign, int toSign) {
    const int distance = signDistance(fromSign, toSign);
    return distance == 0 || distance == 4 || distance == 8;
}

static bool hardAspectToLongitude(double aLon, double bLon, double orbDeg, QString* outAspect, double* outOrb) {
    struct HardAspectDef {
        const char* label;
        double exact;
    };
    static const HardAspectDef defs[] = {
        {"Conjunction", 0.0},
        {"Square", 90.0},
        {"Opposition", 180.0},
    };
    const double diff = angularDiffAbs(aLon, bLon);
    for (const auto& def : defs) {
        const double orb = std::fabs(diff - def.exact);
        if (orb <= orbDeg) {
            if (outAspect) {
                *outAspect = def.label;
            }
            if (outOrb) {
                *outOrb = orb;
            }
            return true;
        }
    }
    return false;
}

}  // namespace


// Transit worker classes extracted to transit_workers.h

static void requestWorkerCancel(QObject* worker) {
    if (!worker) {
        return;
    }
    if (auto* typed = qobject_cast<SearchWorker*>(worker)) {
        typed->cancel();
        return;
    }
    if (auto* typed = qobject_cast<CalendarWorker*>(worker)) {
        typed->cancel();
        return;
    }
    if (auto* typed = qobject_cast<ConjunctionWorker*>(worker)) {
        typed->cancel();
        return;
    }
    if (auto* typed = qobject_cast<LunationWorker*>(worker)) {
        typed->cancel();
        return;
    }
    if (auto* typed = qobject_cast<TransitScanWorker*>(worker)) {
        typed->cancel();
        return;
    }
    QMetaObject::invokeMethod(worker, "cancel", Qt::DirectConnection);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      net_(new QNetworkAccessManager(this)),
      engine_(&swe_, QString()),
      progressionEngine_(&swe_, QString()) {
    qRegisterMetaType<dracoved::MainWindow::TransitSearchResult>("dracoved::MainWindow::TransitSearchResult");
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    qRegisterMetaType<QGeoCoordinate>("QGeoCoordinate");
#endif
    setupUi();
    setupConnections();
    calendarRecomputeTimer_ = new QTimer(this);
    calendarRecomputeTimer_->setSingleShot(true);
    calendarRecomputeTimer_->setInterval(250);
    connect(calendarRecomputeTimer_, &QTimer::timeout, this, [this]() {
        if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Calendar) {
            handleTransitCalendarRun();
        }
    });
    loadUiState();

    ephePath_ = findEphePath();
    if (!ephePath_.isEmpty()) {
        engine_.setEphePath(ephePath_);
        progressionEngine_.setEphePath(ephePath_);
    }

    QString err;
    if (!swe_.load(sweSearchPaths(), &err)) {
        setCriticalMessage(err);
    }

    if (mainTabBar_) {
        handleMainTabChanged(mainTabBar_->currentIndex());
    }
}

void MainWindow::setupUi() {
    setWindowTitle("DracoVed - Tropical Natal (MVP)");
    resize(1400, 900);

    QFont base = font();
    base.setPointSize(9);
    setFont(base);

    setupDockLayout();
    setupMenuBar();
    applyTheme(theme_);
}


void MainWindow::setupDockLayout() {
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(8, 8, 8, 8);
    centralLayout->setSpacing(6);

    mainTabBar_ = new QTabBar(central);
    mainTabBar_->addTab("Natal");
    mainTabBar_->addTab("Transits");
    mainTabBar_->addTab("Progression");
    mainTabBar_->addTab("Solar Return");
    mainTabBar_->addTab("Relocation");
// Astrocartography tab is optional (QtLocation). Do not remove the guard.
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    mainTabBar_->addTab("Astrocartography");
#endif
    mainTabBar_->setExpanding(false);
    mainTabBar_->setDrawBase(false);
    mainTabBar_->setMovable(false);
    mainTabBar_->setCurrentIndex(0);

    profileToolbarFrame_ = new QFrame(central);
    profileToolbarFrame_->setObjectName("profileQuickBar");
    auto* profileToolbarLayout = new QHBoxLayout(profileToolbarFrame_);
    profileToolbarLayout->setContentsMargins(8, 6, 8, 6);
    profileToolbarLayout->setSpacing(6);
    auto* profileLabel = new QLabel("Chart Profile", profileToolbarFrame_);
    profileToolbarCombo_ = new QComboBox(profileToolbarFrame_);
    profileToolbarCombo_->setMinimumWidth(240);
    profileToolbarCombo_->setToolTip("Select a saved chart profile.");
    profileToolbarLoadButton_ = new QToolButton(profileToolbarFrame_);
    profileToolbarLoadButton_->setText("Load");
    profileToolbarSaveButton_ = new QToolButton(profileToolbarFrame_);
    profileToolbarSaveButton_->setText("Save");
    profileToolbarSaveAsButton_ = new QToolButton(profileToolbarFrame_);
    profileToolbarSaveAsButton_->setText("Save As");
    profileToolbarEditButton_ = new QToolButton(profileToolbarFrame_);
    profileToolbarEditButton_->setText("Edit");
    profileToolbarDeleteButton_ = new QToolButton(profileToolbarFrame_);
    profileToolbarDeleteButton_->setText("Delete");
    profileToolbarLoadButton_->setCursor(Qt::PointingHandCursor);
    profileToolbarSaveButton_->setCursor(Qt::PointingHandCursor);
    profileToolbarSaveAsButton_->setCursor(Qt::PointingHandCursor);
    profileToolbarEditButton_->setCursor(Qt::PointingHandCursor);
    profileToolbarDeleteButton_->setCursor(Qt::PointingHandCursor);
    profileToolbarStateLabel_ = new QLabel(profileToolbarFrame_);
    profileToolbarStateLabel_->setObjectName("profileQuickState");
    profileToolbarLayout->addWidget(profileLabel);
    profileToolbarLayout->addWidget(profileToolbarCombo_, 1);
    profileToolbarLayout->addWidget(profileToolbarLoadButton_);
    profileToolbarLayout->addWidget(profileToolbarSaveButton_);
    profileToolbarLayout->addWidget(profileToolbarSaveAsButton_);
    profileToolbarLayout->addWidget(profileToolbarEditButton_);
    profileToolbarLayout->addWidget(profileToolbarDeleteButton_);
    profileToolbarLayout->addSpacing(8);
    profileToolbarLayout->addWidget(profileToolbarStateLabel_);

    auto* chartPanel = new QFrame(central);
    chartPanel->setObjectName("chartPlaceholder");
    auto* chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(8, 8, 8, 8);
    chartLayout->setSpacing(6);

    auto* chartHeader = new QWidget(chartPanel);
    auto* chartHeaderLayout = new QHBoxLayout(chartHeader);
    chartHeaderLayout->setContentsMargins(0, 0, 0, 0);
    chartTitleLabel_ = new QLabel("Chart Wheel", chartHeader);
    chartLegendLabel_ = new QLabel("Natal (inner) / Transit (outer)", chartHeader);
    chartLegendLabel_->setObjectName("hintLabel");
    chartLegendLabel_->setVisible(false);
    chartSettingsButton_ = new QToolButton(chartHeader);
    chartSettingsButton_->setText("⚙");
    zoomOutButton_ = new QToolButton(chartHeader);
    zoomOutButton_->setText("-");
    zoomResetButton_ = new QToolButton(chartHeader);
    zoomResetButton_->setText("0");
    zoomInButton_ = new QToolButton(chartHeader);
    zoomInButton_->setText("+");
    chartHeaderLayout->addWidget(chartTitleLabel_);
    chartHeaderLayout->addWidget(chartLegendLabel_);
    chartHeaderLayout->addStretch();
    chartHeaderLayout->addWidget(zoomOutButton_);
    chartHeaderLayout->addWidget(zoomResetButton_);
    chartHeaderLayout->addWidget(zoomInButton_);
    chartHeaderLayout->addWidget(chartSettingsButton_);

    chartWheel_ = new ChartWheelWidget(chartPanel);
    chartWheel_->setMinimumWidth(520);

    chartLayout->addWidget(chartHeader);
    centerStack_ = new QStackedWidget(chartPanel);
    chartViewPanel_ = new QWidget(centerStack_);
    auto* chartViewLayout = new QVBoxLayout(chartViewPanel_);
    chartViewLayout->setContentsMargins(0, 0, 0, 0);
    chartViewLayout->addWidget(chartWheel_, 1);
    centerStack_->addWidget(chartViewPanel_);

// World map widget is only constructed when QtLocation is available.
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    worldMapPanel_ = new QWidget(centerStack_);
    auto* worldMapLayout = new QVBoxLayout(worldMapPanel_);
    worldMapLayout->setContentsMargins(0, 0, 0, 0);
    worldMapView_ = new QQuickWidget(worldMapPanel_);
    worldMapView_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    worldMapView_->setSource(QUrl("qrc:/resources/qml/world_map.qml"));
    worldMapLayout->addWidget(worldMapView_, 1);
    centerStack_->addWidget(worldMapPanel_);
#endif
    centerStack_->setCurrentWidget(chartViewPanel_);

    chartLayout->addWidget(centerStack_, 1);

    centralLayout->addWidget(profileToolbarFrame_);
    centralLayout->addWidget(mainTabBar_);
    centralLayout->addWidget(chartPanel, 1);

    setCentralWidget(central);

    tabs_ = new QTabWidget(this);
    summaryTable_ = new QTableWidget(tabs_);
    anglesTable_ = new QTableWidget(tabs_);
    planetsTable_ = new QTableWidget(tabs_);
    fixedStarsTable_ = new QTableWidget(tabs_);
    housesTable_ = new QTableWidget(tabs_);
    aspectsTable_ = new QTableWidget(this);
    aspectsTable_->setMouseTracking(true);
    if (auto* view = aspectsTable_->viewport()) {
        view->setMouseTracking(true);
        view->installEventFilter(this);
    }

    tabs_->addTab(summaryTable_, "Summary");
    tabs_->addTab(anglesTable_, "Angles");
    tabs_->addTab(planetsTable_, "Planets");
    tabs_->addTab(fixedStarsTable_, "Fixed Stars");
    tabs_->addTab(housesTable_, "Houses");

    reportPanel_ = new QWidget(tabs_);
    auto* reportLayout = new QVBoxLayout(reportPanel_);
    reportLayout->setContentsMargins(6, 6, 6, 6);
    reportLayout->setSpacing(6);
    auto* reportHeader = new QWidget(reportPanel_);
    auto* reportHeaderLayout = new QHBoxLayout(reportHeader);
    reportHeaderLayout->setContentsMargins(0, 0, 0, 0);
    reportCopyButton_ = new QPushButton("Copy Report", reportHeader);
    reportHeaderLayout->addStretch();
    reportHeaderLayout->addWidget(reportCopyButton_);
    reportText_ = new QTextEdit(reportPanel_);
    reportText_->setReadOnly(true);
    reportText_->setPlaceholderText("Load a natal chart to generate the report.");
    reportLayout->addWidget(reportHeader);
    reportLayout->addWidget(reportText_, 1);
    tabs_->addTab(reportPanel_, "Report");

    solarTechniquePanel_ = new QWidget(tabs_);
    auto* techniqueLayout = new QVBoxLayout(solarTechniquePanel_);
    techniqueLayout->setContentsMargins(0, 0, 0, 0);
    techniqueLayout->setSpacing(8);
    auto* techniqueIntro = new QLabel("SR Ascendant = Day 1. Move 1° per day from the SR date to the next SR date.", solarTechniquePanel_);
    techniqueIntro->setWordWrap(true);
    techniqueIntro->setObjectName("hintLabel");
    techniqueLayout->addWidget(techniqueIntro);

    auto* techniqueRangeGroup = new QGroupBox("Solar Return Year Range", solarTechniquePanel_);
    auto* techniqueRangeLayout = new QVBoxLayout(techniqueRangeGroup);
    solarTechniqueRangeLabel_ = new QLabel("Calculate Solar Return to load the SR year range.", techniqueRangeGroup);
    solarTechniqueRangeLabel_->setObjectName("hintLabel");
    techniqueRangeLayout->addWidget(solarTechniqueRangeLabel_);
    techniqueLayout->addWidget(techniqueRangeGroup);

    auto* techniqueDateGroup = new QGroupBox("Date", solarTechniquePanel_);
    auto* techniqueDateLayout = new QGridLayout(techniqueDateGroup);
    techniqueDateLayout->setHorizontalSpacing(8);
    techniqueDateLayout->setVerticalSpacing(6);
    solarTechniqueDateEdit_ = new QDateEdit(techniqueDateGroup);
    solarTechniqueDateEdit_->setCalendarPopup(true);
    solarTechniqueDateEdit_->setDisplayFormat("yyyy-MM-dd");
    solarTechniqueDateEdit_->setDate(QDate::currentDate());
    techniqueDateLayout->addWidget(new QLabel("Date", techniqueDateGroup), 0, 0);
    techniqueDateLayout->addWidget(solarTechniqueDateEdit_, 0, 1);
    techniqueLayout->addWidget(techniqueDateGroup);

    auto* techniqueTargetGroup = new QGroupBox("Targets", solarTechniquePanel_);
    auto* techniqueTargetLayout = new QHBoxLayout(techniqueTargetGroup);
    solarTechniqueNatalCheck_ = new QCheckBox("Natal", techniqueTargetGroup);
    solarTechniqueSolarCheck_ = new QCheckBox("Solar Return", techniqueTargetGroup);
    solarTechniqueNatalCheck_->setChecked(true);
    solarTechniqueSolarCheck_->setChecked(true);
    techniqueTargetLayout->addWidget(solarTechniqueNatalCheck_);
    techniqueTargetLayout->addWidget(solarTechniqueSolarCheck_);
    techniqueTargetLayout->addStretch();
    techniqueLayout->addWidget(techniqueTargetGroup);

    auto* techniqueOrbGroup = new QGroupBox("Aspect Orb", solarTechniquePanel_);
    auto* techniqueOrbLayout = new QHBoxLayout(techniqueOrbGroup);
    solarTechniqueOrbSpin_ = new QDoubleSpinBox(techniqueOrbGroup);
    solarTechniqueOrbSpin_->setRange(0.1, 5.0);
    solarTechniqueOrbSpin_->setDecimals(2);
    solarTechniqueOrbSpin_->setSingleStep(0.1);
    solarTechniqueOrbSpin_->setValue(1.0);
    solarTechniqueOrbSpin_->setSuffix("°");
    techniqueOrbLayout->addWidget(new QLabel("Orb", techniqueOrbGroup));
    techniqueOrbLayout->addWidget(solarTechniqueOrbSpin_);
    techniqueOrbLayout->addStretch();
    techniqueLayout->addWidget(techniqueOrbGroup);

    auto* techniqueRankGroup = new QGroupBox("Ranking", solarTechniquePanel_);
    auto* techniqueRankLayout = new QGridLayout(techniqueRankGroup);
    techniqueRankLayout->setHorizontalSpacing(8);
    techniqueRankLayout->setVerticalSpacing(6);
    solarTechniqueRankMetricCombo_ = new QComboBox(techniqueRankGroup);
    solarTechniqueRankMetricCombo_->addItems({"Net (Support - Challenge)", "Support (Good)", "Challenge (Bad)"});
    solarTechniqueRankOrderCombo_ = new QComboBox(techniqueRankGroup);
    solarTechniqueRankOrderCombo_->addItems({"High -> Low", "Low -> High"});
    solarTechniqueTopSpin_ = new QSpinBox(techniqueRankGroup);
    solarTechniqueTopSpin_->setRange(1, 100);
    solarTechniqueTopSpin_->setValue(20);
    techniqueRankLayout->addWidget(new QLabel("Metric", techniqueRankGroup), 0, 0);
    techniqueRankLayout->addWidget(solarTechniqueRankMetricCombo_, 0, 1);
    techniqueRankLayout->addWidget(new QLabel("Order", techniqueRankGroup), 1, 0);
    techniqueRankLayout->addWidget(solarTechniqueRankOrderCombo_, 1, 1);
    techniqueRankLayout->addWidget(new QLabel("Top N", techniqueRankGroup), 2, 0);
    techniqueRankLayout->addWidget(solarTechniqueTopSpin_, 2, 1);
    techniqueLayout->addWidget(techniqueRankGroup);
    techniqueLayout->addStretch();

    tabs_->addTab(solarTechniquePanel_, "Technique");
    tabs_->setTabVisible(tabs_->indexOf(solarTechniquePanel_), false);

    auto* dataPanel = new QFrame(this);
    dataPanel->setObjectName("dataPanel");
    auto* dataLayout = new QVBoxLayout(dataPanel);
    dataLayout->setContentsMargins(6, 6, 6, 6);

    progressionControls_ = new QWidget(dataPanel);
    auto* progressionLayout = new QVBoxLayout(progressionControls_);
    progressionLayout->setContentsMargins(0, 0, 0, 0);
    progressionLayout->setSpacing(8);

    auto* progressionViewGroup = new QGroupBox("View", progressionControls_);
    auto* progressionViewLayout = new QVBoxLayout(progressionViewGroup);
    progressionViewProgressedRadio_ = new QRadioButton("Progressed chart", progressionViewGroup);
    progressionViewNatalRadio_ = new QRadioButton("Natal chart", progressionViewGroup);
    progressionViewOverlayRadio_ = new QRadioButton("Overlay (Natal + Progressed)", progressionViewGroup);
    progressionViewProgressedRadio_->setChecked(true);
    progressionViewLayout->addWidget(progressionViewProgressedRadio_);
    progressionViewLayout->addWidget(progressionViewNatalRadio_);
    progressionViewLayout->addWidget(progressionViewOverlayRadio_);

    auto* progressionTargetGroup = new QGroupBox("Progression Target", progressionControls_);
    auto* progressionTargetLayout = new QGridLayout(progressionTargetGroup);
    progressionTargetLayout->setHorizontalSpacing(8);
    progressionTargetLayout->setVerticalSpacing(6);
    progressionTargetLayout->setColumnStretch(1, 1);
    progressionDateEdit_ = new QDateEdit(progressionTargetGroup);
    progressionDateEdit_->setCalendarPopup(true);
    progressionDateEdit_->setDisplayFormat("yyyy-MM-dd");
    progressionTimeEdit_ = new QTimeEdit(progressionTargetGroup);
    progressionTimeEdit_->setDisplayFormat("hh:mm:ss AP");
    progressionTimezoneEdit_ = new QLineEdit(progressionTargetGroup);
    progressionTimezoneEdit_->setPlaceholderText("Timezone (e.g., Asia/Dhaka)");
    progressionTimezoneStatus_ = new QLabel("OK", progressionTargetGroup);
    progressionTimezoneStatus_->setMinimumWidth(40);
    progressionNowButton_ = new QPushButton("Now", progressionTargetGroup);
    progressionNowButton_->setToolTip("Set target to current time");
    const QDateTime nowLocal = QDateTime::currentDateTime();
    progressionDateEdit_->setDate(nowLocal.date());
    progressionTimeEdit_->setTime(nowLocal.time());
    const QByteArray progTzId = QTimeZone::systemTimeZoneId();
    progressionTimezoneEdit_->setText(progTzId.isEmpty() ? "UTC" : QString::fromUtf8(progTzId));
    progressionTargetLayout->addWidget(new QLabel("Date", progressionTargetGroup), 0, 0);
    progressionTargetLayout->addWidget(progressionDateEdit_, 0, 1);
    progressionTargetLayout->addWidget(progressionNowButton_, 0, 2);
    progressionTargetLayout->addWidget(new QLabel("Time", progressionTargetGroup), 1, 0);
    progressionTargetLayout->addWidget(progressionTimeEdit_, 1, 1);
    progressionTargetLayout->addWidget(new QLabel("Timezone", progressionTargetGroup), 2, 0);
    progressionTargetLayout->addWidget(progressionTimezoneEdit_, 2, 1);
    progressionTargetLayout->addWidget(progressionTimezoneStatus_, 2, 2);

    auto* progressionRunGroup = new QGroupBox("Run", progressionControls_);
    auto* progressionRunLayout = new QHBoxLayout(progressionRunGroup);
    progressionCalculateButton_ = new QPushButton("Calculate Progression", progressionRunGroup);
    progressionStatusLabel_ = new QLabel("Pending changes", progressionRunGroup);
    progressionStatusLabel_->setObjectName("hintLabel");
    progressionLastLabel_ = new QLabel("Last calculated: -", progressionRunGroup);
    progressionLastLabel_->setObjectName("hintLabel");
    progressionRunLayout->addWidget(progressionCalculateButton_);
    progressionRunLayout->addStretch();
    progressionRunLayout->addWidget(progressionStatusLabel_);
    progressionRunLayout->addWidget(progressionLastLabel_);

    progressionLayout->addWidget(progressionViewGroup);
    progressionLayout->addWidget(progressionTargetGroup);
    progressionLayout->addWidget(progressionRunGroup);
    progressionLayout->addStretch();
    progressionControls_->setVisible(false);

    solarControls_ = new QWidget(dataPanel);
    auto* solarLayout = new QVBoxLayout(solarControls_);
    solarLayout->setContentsMargins(0, 0, 0, 0);
    solarLayout->setSpacing(8);

    auto* solarYearGroup = new QGroupBox("Solar Return", solarControls_);
    auto* solarYearLayout = new QGridLayout(solarYearGroup);
    solarYearLayout->setHorizontalSpacing(8);
    solarYearLayout->setVerticalSpacing(6);
    solarYearSpin_ = new QSpinBox(solarYearGroup);
    solarYearSpin_->setRange(1800, 2399);
    solarYearSpin_->setValue(QDate::currentDate().year());
    solarTimezoneEdit_ = new QLineEdit(solarYearGroup);
    solarTimezoneEdit_->setPlaceholderText("Timezone (e.g., Asia/Dhaka)");
    solarTimezoneEdit_->setText("UTC");
    solarTimezoneStatus_ = new QLabel("OK", solarYearGroup);
    solarTimezoneStatus_->setMinimumWidth(40);
    solarYearLayout->addWidget(new QLabel("Year", solarYearGroup), 0, 0);
    solarYearLayout->addWidget(solarYearSpin_, 0, 1);
    solarYearLayout->addWidget(new QLabel("Timezone", solarYearGroup), 1, 0);
    solarYearLayout->addWidget(solarTimezoneEdit_, 1, 1);
    solarYearLayout->addWidget(solarTimezoneStatus_, 1, 2);

    auto* solarLocationGroup = new QGroupBox("Location", solarControls_);
    auto* solarLocationLayout = new QGridLayout(solarLocationGroup);
    solarLocationLayout->setHorizontalSpacing(8);
    solarLocationLayout->setVerticalSpacing(6);
    solarLocationLayout->setColumnStretch(1, 1);
    solarLocationLayout->setColumnStretch(3, 1);
    solarUseNatalRadio_ = new QRadioButton("Use natal location", solarLocationGroup);
    solarUseCustomRadio_ = new QRadioButton("Use custom location", solarLocationGroup);
    solarUseNatalRadio_->setChecked(true);
    solarLocationEdit_ = new QLineEdit(solarLocationGroup);
    solarLocationEdit_->setPlaceholderText("Location");
    solarGeocodeButton_ = new QPushButton("Geocode", solarLocationGroup);
    solarLatSpin_ = new QDoubleSpinBox(solarLocationGroup);
    solarLonSpin_ = new QDoubleSpinBox(solarLocationGroup);
    solarLatSpin_->setRange(-90.0, 90.0);
    solarLonSpin_->setRange(-180.0, 180.0);
    solarLatSpin_->setDecimals(6);
    solarLonSpin_->setDecimals(6);
    solarLatSpin_->setSingleStep(0.01);
    solarLonSpin_->setSingleStep(0.01);
    solarLocationLayout->addWidget(solarUseNatalRadio_, 0, 0, 1, 2);
    solarLocationLayout->addWidget(solarUseCustomRadio_, 0, 2, 1, 2);
    solarLocationLayout->addWidget(new QLabel("Location", solarLocationGroup), 1, 0);
    solarLocationLayout->addWidget(solarLocationEdit_, 1, 1, 1, 2);
    solarLocationLayout->addWidget(solarGeocodeButton_, 1, 3);
    solarLocationLayout->addWidget(new QLabel("Latitude", solarLocationGroup), 2, 0);
    solarLocationLayout->addWidget(solarLatSpin_, 2, 1);
    solarLocationLayout->addWidget(new QLabel("Longitude", solarLocationGroup), 2, 2);
    solarLocationLayout->addWidget(solarLonSpin_, 2, 3);

    auto* solarRunGroup = new QGroupBox("Run", solarControls_);
    auto* solarRunLayout = new QHBoxLayout(solarRunGroup);
    solarCalculateButton_ = new QPushButton("Calculate Solar Return", solarRunGroup);
    solarStatusLabel_ = new QLabel("Pending changes", solarRunGroup);
    solarStatusLabel_->setObjectName("hintLabel");
    solarLastLabel_ = new QLabel("Last calculated: -", solarRunGroup);
    solarLastLabel_->setObjectName("hintLabel");
    solarRunLayout->addWidget(solarCalculateButton_);
    solarRunLayout->addStretch();
    solarRunLayout->addWidget(solarStatusLabel_);
    solarRunLayout->addWidget(solarLastLabel_);

    solarLayout->addWidget(solarYearGroup);
    solarLayout->addWidget(solarLocationGroup);
    solarLayout->addWidget(solarRunGroup);
    solarLayout->addStretch();
    solarControls_->setVisible(false);

    relocationControls_ = new QWidget(dataPanel);
    auto* relocationLayout = new QVBoxLayout(relocationControls_);
    relocationLayout->setContentsMargins(0, 0, 0, 0);
    relocationLayout->setSpacing(8);

    auto* relocationLocationGroup = new QGroupBox("Location", relocationControls_);
    auto* relocationLocationLayout = new QGridLayout(relocationLocationGroup);
    relocationLocationLayout->setHorizontalSpacing(8);
    relocationLocationLayout->setVerticalSpacing(6);
    relocationLocationLayout->setColumnStretch(1, 1);
    relocationLocationLayout->setColumnStretch(3, 1);
    relocationLocationEdit_ = new QLineEdit(relocationLocationGroup);
    relocationLocationEdit_->setPlaceholderText("Location");
    relocationGeocodeButton_ = new QPushButton("Geocode", relocationLocationGroup);
    relocationLatSpin_ = new QDoubleSpinBox(relocationLocationGroup);
    relocationLonSpin_ = new QDoubleSpinBox(relocationLocationGroup);
    relocationLatSpin_->setRange(-90.0, 90.0);
    relocationLonSpin_->setRange(-180.0, 180.0);
    relocationLatSpin_->setDecimals(6);
    relocationLonSpin_->setDecimals(6);
    relocationLatSpin_->setSingleStep(0.01);
    relocationLonSpin_->setSingleStep(0.01);
    relocationLocationLayout->addWidget(new QLabel("Location", relocationLocationGroup), 0, 0);
    relocationLocationLayout->addWidget(relocationLocationEdit_, 0, 1, 1, 2);
    relocationLocationLayout->addWidget(relocationGeocodeButton_, 0, 3);
    relocationLocationLayout->addWidget(new QLabel("Latitude", relocationLocationGroup), 1, 0);
    relocationLocationLayout->addWidget(relocationLatSpin_, 1, 1);
    relocationLocationLayout->addWidget(new QLabel("Longitude", relocationLocationGroup), 1, 2);
    relocationLocationLayout->addWidget(relocationLonSpin_, 1, 3);

    auto* relocationTimezoneGroup = new QGroupBox("Timezone", relocationControls_);
    auto* relocationTimezoneLayout = new QGridLayout(relocationTimezoneGroup);
    relocationTimezoneLayout->setHorizontalSpacing(8);
    relocationTimezoneLayout->setVerticalSpacing(6);
    relocationTimezoneLayout->setColumnStretch(1, 1);
    relocationTimezoneEdit_ = new QLineEdit(relocationTimezoneGroup);
    relocationTimezoneEdit_->setPlaceholderText("Timezone (e.g., Asia/Dhaka)");
    relocationTimezoneStatus_ = new QLabel("OK", relocationTimezoneGroup);
    relocationTimezoneStatus_->setMinimumWidth(40);
    const QByteArray relocationTzId = QTimeZone::systemTimeZoneId();
    relocationTimezoneEdit_->setText(relocationTzId.isEmpty() ? "UTC" : QString::fromUtf8(relocationTzId));
    relocationTimezoneLayout->addWidget(new QLabel("Timezone", relocationTimezoneGroup), 0, 0);
    relocationTimezoneLayout->addWidget(relocationTimezoneEdit_, 0, 1);
    relocationTimezoneLayout->addWidget(relocationTimezoneStatus_, 0, 2);

    auto* relocationHouseGroup = new QGroupBox("House System", relocationControls_);
    auto* relocationHouseLayout = new QVBoxLayout(relocationHouseGroup);
    relocationWholeRadio_ = new QRadioButton("Whole Sign", relocationHouseGroup);
    relocationPlacidusRadio_ = new QRadioButton("Placidus", relocationHouseGroup);
    relocationWholeRadio_->setChecked(true);
    relocationHouseLayout->addWidget(relocationWholeRadio_);
    relocationHouseLayout->addWidget(relocationPlacidusRadio_);

    auto* relocationViewGroup = new QGroupBox("View", relocationControls_);
    auto* relocationViewLayout = new QVBoxLayout(relocationViewGroup);
    relocationOverlayCheck_ = new QCheckBox("Overlay natal chart", relocationViewGroup);
    relocationOverlayCheck_->setChecked(false);
    relocationViewLayout->addWidget(relocationOverlayCheck_);

    auto* relocationRunGroup = new QGroupBox("Run", relocationControls_);
    auto* relocationRunLayout = new QHBoxLayout(relocationRunGroup);
    relocationCalculateButton_ = new QPushButton("Calculate Relocation", relocationRunGroup);
    relocationStatusLabel_ = new QLabel("Pending changes", relocationRunGroup);
    relocationStatusLabel_->setObjectName("hintLabel");
    relocationLastLabel_ = new QLabel("Last calculated: -", relocationRunGroup);
    relocationLastLabel_->setObjectName("hintLabel");
    relocationRunLayout->addWidget(relocationCalculateButton_);
    relocationRunLayout->addStretch();
    relocationRunLayout->addWidget(relocationStatusLabel_);
    relocationRunLayout->addWidget(relocationLastLabel_);

    relocationLayout->addWidget(relocationLocationGroup);
    relocationLayout->addWidget(relocationTimezoneGroup);
    relocationLayout->addWidget(relocationHouseGroup);
    relocationLayout->addWidget(relocationViewGroup);
    relocationLayout->addWidget(relocationRunGroup);
    relocationLayout->addStretch();
    relocationControls_->setVisible(false);

    dataLayout->addWidget(progressionControls_);
    dataLayout->addWidget(solarControls_);
    dataLayout->addWidget(relocationControls_);
    dataLayout->addWidget(tabs_);

    transitPanel_ = new QFrame(this);
    transitPanel_->setObjectName("dataPanel");
    auto* transitLayout = new QVBoxLayout(transitPanel_);
    transitLayout->setContentsMargins(6, 6, 6, 6);
    transitLayout->setSpacing(8);

    transitSubTabBar_ = new QTabBar(transitPanel_);
    transitSubTabBar_->addTab("Overview");
    transitSubTabBar_->addTab("Search");
    transitSubTabBar_->addTab("Calendar");
    transitSubTabBar_->addTab("Conjunctions");
    transitSubTabBar_->addTab("Best Days");
    transitSubTabBar_->addTab("Profections");
    transitSubTabBar_->addTab("Lunations");
    transitSubTabBar_->setExpanding(false);
    transitSubTabBar_->setDrawBase(false);
    transitSubTabBar_->setCurrentIndex(0);
    transitLayout->addWidget(transitSubTabBar_);

    auto* modeGroup = new QGroupBox("Mode", transitPanel_);
    auto* modeLayout = new QVBoxLayout(modeGroup);
    transitOverlayRadio_ = new QRadioButton("Natal Transits (overlay)", modeGroup);
    transitOnlyRadio_ = new QRadioButton("Transit Only", modeGroup);
    transitOverlayRadio_->setChecked(true);
    modeLayout->addWidget(transitOverlayRadio_);
    modeLayout->addWidget(transitOnlyRadio_);

    auto* targetGroup = new QGroupBox("Transit Target", transitPanel_);
    auto* targetLayout = new QVBoxLayout(targetGroup);
    transitTargetLabel_ = new QLabel("Transit target: -", targetGroup);
    transitStatusLabel_ = new QLabel("Pending changes", targetGroup);
    transitLastLabel_ = new QLabel("Last calculated: -", targetGroup);
    targetLayout->addWidget(transitTargetLabel_);
    targetLayout->addWidget(transitStatusLabel_);
    targetLayout->addWidget(transitLastLabel_);

    auto* timeGroup = new QGroupBox("Transit Time", transitPanel_);
    auto* timeLayout = new QGridLayout(timeGroup);
    timeLayout->setHorizontalSpacing(8);
    timeLayout->setVerticalSpacing(6);
    timeLayout->setColumnStretch(1, 1);
    transitDateEdit_ = new QDateEdit(timeGroup);
    transitDateEdit_->setCalendarPopup(true);
    transitDateEdit_->setDisplayFormat("yyyy-MM-dd");
    transitTimeEdit_ = new QTimeEdit(timeGroup);
    transitTimeEdit_->setDisplayFormat("hh:mm:ss AP");
    transitTimezoneEdit_ = new QLineEdit(timeGroup);
    transitTimezoneEdit_->setPlaceholderText("Timezone (e.g., Asia/Dhaka)");
    transitTimezoneEdit_->setText("UTC");
    transitTimezoneStatus_ = new QLabel("OK", timeGroup);
    transitTimezoneStatus_->setMinimumWidth(40);
    transitResetTimeButton_ = new QPushButton("Reset", timeGroup);
    transitResetTimeButton_->setToolTip("Reset to natal time/timezone");
    transitResetTimeButton_->setFixedWidth(60);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    transitDateEdit_->setDate(nowUtc.date());
    transitTimeEdit_->setTime(nowUtc.time());
    timeLayout->addWidget(new QLabel("Date", timeGroup), 0, 0);
    timeLayout->addWidget(transitDateEdit_, 0, 1);
    timeLayout->addWidget(new QLabel("Time", timeGroup), 1, 0);
    timeLayout->addWidget(transitTimeEdit_, 1, 1);
    timeLayout->addWidget(transitResetTimeButton_, 1, 2);
    timeLayout->addWidget(new QLabel("Timezone", timeGroup), 2, 0);
    timeLayout->addWidget(transitTimezoneEdit_, 2, 1);
    timeLayout->addWidget(transitTimezoneStatus_, 2, 2);

    auto* quickRow = new QHBoxLayout();
    transitNowButton_ = new QPushButton("Now", timeGroup);
    transitPlusDayButton_ = new QPushButton("+1 Day", timeGroup);
    transitPlusWeekButton_ = new QPushButton("+1 Week", timeGroup);
    transitPlusMonthButton_ = new QPushButton("+1 Month", timeGroup);
    quickRow->addWidget(transitNowButton_);
    quickRow->addWidget(transitPlusDayButton_);
    quickRow->addWidget(transitPlusWeekButton_);
    quickRow->addWidget(transitPlusMonthButton_);
    timeLayout->addLayout(quickRow, 3, 0, 1, 3);

    transitCalculateButton_ = new QPushButton("Calculate Transits", timeGroup);
    transitCalculateButton_->setDefault(true);
    timeLayout->addWidget(transitCalculateButton_, 4, 0, 1, 3);

    auto* locationGroup = new QGroupBox("Location", transitPanel_);
    auto* locationLayout = new QGridLayout(locationGroup);
    locationLayout->setHorizontalSpacing(8);
    locationLayout->setVerticalSpacing(6);
    locationLayout->setColumnStretch(1, 1);
    locationLayout->setColumnStretch(3, 1);
    transitUseNatalLocation_ = new QCheckBox("Use natal location", locationGroup);
    transitUseNatalLocation_->setChecked(true);
    transitLocationEdit_ = new QLineEdit(locationGroup);
    transitLocationEdit_->setPlaceholderText("Location");
    transitGeocodeButton_ = new QPushButton("Geocode", locationGroup);
    transitLatSpin_ = new QDoubleSpinBox(locationGroup);
    transitLonSpin_ = new QDoubleSpinBox(locationGroup);
    transitLatSpin_->setRange(-90.0, 90.0);
    transitLonSpin_->setRange(-180.0, 180.0);
    transitLatSpin_->setDecimals(6);
    transitLonSpin_->setDecimals(6);
    transitLatSpin_->setSingleStep(0.01);
    transitLonSpin_->setSingleStep(0.01);
    locationLayout->addWidget(transitUseNatalLocation_, 0, 0, 1, 4);
    locationLayout->addWidget(new QLabel("Location", locationGroup), 1, 0);
    locationLayout->addWidget(transitLocationEdit_, 1, 1, 1, 2);
    locationLayout->addWidget(transitGeocodeButton_, 1, 3);
    locationLayout->addWidget(new QLabel("Latitude", locationGroup), 2, 0);
    locationLayout->addWidget(transitLatSpin_, 2, 1);
    locationLayout->addWidget(new QLabel("Longitude", locationGroup), 2, 2);
    locationLayout->addWidget(transitLonSpin_, 2, 3);

    auto* houseGroup = new QGroupBox("House System", transitPanel_);
    auto* houseLayout = new QHBoxLayout(houseGroup);
    transitWholeRadio_ = new QRadioButton("Whole Sign", houseGroup);
    transitPlacidusRadio_ = new QRadioButton("Placidus", houseGroup);
    transitWholeRadio_->setChecked(true);
    houseLayout->addWidget(transitWholeRadio_);
    houseLayout->addWidget(transitPlacidusRadio_);
    houseLayout->addStretch();

    auto* commonPanel = new QWidget(transitPanel_);
    auto* commonLayout = new QVBoxLayout(commonPanel);
    commonLayout->setContentsMargins(0, 0, 0, 0);
    commonLayout->setSpacing(8);
    commonLayout->addWidget(modeGroup);
    commonLayout->addWidget(houseGroup);
    transitLayout->addWidget(commonPanel);

    transitPanelStack_ = new QStackedWidget(transitPanel_);
    transitOverviewPanel_ = new QWidget(transitPanelStack_);
    auto* transitOverviewLayout = new QVBoxLayout(transitOverviewPanel_);
    transitOverviewLayout->setContentsMargins(0, 0, 0, 0);
    transitOverviewLayout->setSpacing(8);
    transitOverviewLayout->addWidget(targetGroup);
    transitOverviewLayout->addWidget(timeGroup);
    transitOverviewLayout->addWidget(locationGroup);
    transitOverviewLayout->addStretch();

    transitSearchPanel_ = new QWidget(transitPanelStack_);
    auto* transitSearchLayout = new QVBoxLayout(transitSearchPanel_);
    transitSearchLayout->setContentsMargins(0, 0, 0, 0);
    transitSearchLayout->setSpacing(8);

    auto* searchRangeGroup = new QGroupBox("Search Range", transitSearchPanel_);
    auto* searchRangeLayout = new QGridLayout(searchRangeGroup);
    searchRangeLayout->setHorizontalSpacing(8);
    searchRangeLayout->setVerticalSpacing(6);
    searchRangeLayout->setColumnStretch(1, 1);
    searchModeRangeRadio_ = new QRadioButton("Range", searchRangeGroup);
    searchModeNextRadio_ = new QRadioButton("Find Next", searchRangeGroup);
    searchModePrevRadio_ = new QRadioButton("Find Previous", searchRangeGroup);
    searchModeRangeRadio_->setChecked(true);
    auto* searchModeRow = new QWidget(searchRangeGroup);
    auto* searchModeLayout = new QHBoxLayout(searchModeRow);
    searchModeLayout->setContentsMargins(0, 0, 0, 0);
    searchModeLayout->setSpacing(8);
    searchModeLayout->addWidget(searchModeRangeRadio_);
    searchModeLayout->addWidget(searchModeNextRadio_);
    searchModeLayout->addWidget(searchModePrevRadio_);
    searchModeLayout->addStretch();
    searchRangeModeCombo_ = new QComboBox(searchRangeGroup);
    searchRangeModeCombo_->addItems({"Within Range", "Forward", "Backward"});
    searchStartDateEdit_ = new QDateEdit(searchRangeGroup);
    searchStartDateEdit_->setCalendarPopup(true);
    searchStartDateEdit_->setDisplayFormat("yyyy-MM-dd");
    searchStartTimeEdit_ = new QTimeEdit(searchRangeGroup);
    searchStartTimeEdit_->setDisplayFormat("hh:mm:ss AP");
    searchEndDateEdit_ = new QDateEdit(searchRangeGroup);
    searchEndDateEdit_->setCalendarPopup(true);
    searchEndDateEdit_->setDisplayFormat("yyyy-MM-dd");
    searchEndTimeEdit_ = new QTimeEdit(searchRangeGroup);
    searchEndTimeEdit_->setDisplayFormat("hh:mm:ss AP");
    searchTimezoneEdit_ = new QLineEdit(searchRangeGroup);
    searchTimezoneEdit_->setPlaceholderText("Timezone (e.g., Asia/Dhaka)");
    const QDateTime nowSearch = QDateTime::currentDateTime();
    searchStartDateEdit_->setDate(nowSearch.date());
    searchStartTimeEdit_->setTime(nowSearch.time());
    searchEndDateEdit_->setDate(nowSearch.addDays(30).date());
    searchEndTimeEdit_->setTime(nowSearch.time());
    const QByteArray searchTzId = QTimeZone::systemTimeZoneId();
    searchTimezoneEdit_->setText(searchTzId.isEmpty() ? "UTC" : QString::fromUtf8(searchTzId));
    searchRangeLayout->addWidget(new QLabel("Mode", searchRangeGroup), 0, 0);
    searchRangeLayout->addWidget(searchModeRow, 0, 1, 1, 2);
    searchRangeLayout->addWidget(new QLabel("Direction", searchRangeGroup), 1, 0);
    searchRangeLayout->addWidget(searchRangeModeCombo_, 1, 1, 1, 2);
    searchRangeLayout->addWidget(new QLabel("Start Date", searchRangeGroup), 2, 0);
    searchRangeLayout->addWidget(searchStartDateEdit_, 2, 1);
    searchRangeLayout->addWidget(new QLabel("Start Time", searchRangeGroup), 3, 0);
    searchRangeLayout->addWidget(searchStartTimeEdit_, 3, 1);
    searchRangeLayout->addWidget(new QLabel("End Date", searchRangeGroup), 4, 0);
    searchRangeLayout->addWidget(searchEndDateEdit_, 4, 1);
    searchRangeLayout->addWidget(new QLabel("End Time", searchRangeGroup), 5, 0);
    searchRangeLayout->addWidget(searchEndTimeEdit_, 5, 1);
    searchRangeLayout->addWidget(new QLabel("Timezone", searchRangeGroup), 6, 0);
    searchRangeLayout->addWidget(searchTimezoneEdit_, 6, 1, 1, 2);

    auto* searchFilterGroup = new QGroupBox("Search Filters", transitSearchPanel_);
    auto* searchFilterLayout = new QGridLayout(searchFilterGroup);
    searchFilterLayout->setHorizontalSpacing(8);
    searchFilterLayout->setVerticalSpacing(6);
    searchFilterLayout->setColumnStretch(1, 1);
    searchEventCombo_ = new QComboBox(searchFilterGroup);
    searchEventCombo_->addItems({"Sign Ingress", "Sign Egress", "House Ingress", "House Egress", "Aspect to Natal", "Degree Hit", "Station"});
    searchTransitPlanetCombo_ = new QComboBox(searchFilterGroup);
    searchTransitPlanetCombo_->setEditable(true);
    if (auto* edit = searchTransitPlanetCombo_->lineEdit()) {
        edit->setReadOnly(true);
        edit->setPlaceholderText("Select planets");
        edit->setCursor(Qt::ArrowCursor);
        edit->installEventFilter(new ComboPopupOnClick(searchTransitPlanetCombo_));
    }
    auto* transitPlanetModel = new QStandardItemModel(searchTransitPlanetCombo_);
    auto* allItem = new QStandardItem("All");
    allItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    allItem->setData(Qt::Unchecked, Qt::CheckStateRole);
    transitPlanetModel->appendRow(allItem);
    for (const auto& name : transitCalculableBodyOrder()) {
        auto* item = new QStandardItem(name);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        const bool checkedByDefault = !isAsteroidBody(name);
        item->setData(checkedByDefault ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        transitPlanetModel->appendRow(item);
    }
    searchTransitPlanetCombo_->setModel(transitPlanetModel);
    searchTransitPlanetCombo_->setCurrentIndex(0);
    updateTransitPlanetComboLabel(searchTransitPlanetCombo_);
    searchTargetCombo_ = new QComboBox(searchFilterGroup);
    searchTargetLabel_ = new QLabel("Natal Target", searchFilterGroup);
    searchAspectCombo_ = new QComboBox(searchFilterGroup);
    searchAspectCombo_->addItems({"Conjunction", "Sextile", "Square", "Trine", "Opposition"});
    searchOrbSpin_ = new QDoubleSpinBox(searchFilterGroup);
    searchOrbSpin_->setRange(0.0, 10.0);
    searchOrbSpin_->setDecimals(1);
    searchOrbSpin_->setSingleStep(0.5);
    searchHouseCombo_ = new QComboBox(searchFilterGroup);
    searchHouseCombo_->addItem("Any");
    for (int i = 1; i <= 12; ++i) {
        searchHouseCombo_->addItem(QString::number(i));
    }
    searchSignCombo_ = new QComboBox(searchFilterGroup);
    searchSignCombo_->addItem("Any");
    for (int i = 0; i < 12; ++i) {
        searchSignCombo_->addItem(signName(i));
    }
    searchDegreeLabel_ = new QLabel("Degree", searchFilterGroup);
    searchDegreeSpin_ = new QDoubleSpinBox(searchFilterGroup);
    searchDegreeSpin_->setRange(0.0, 29.9999);
    searchDegreeSpin_->setDecimals(4);
    searchDegreeSpin_->setSingleStep(0.1);
    searchDegreeSpin_->setValue(13.0);
    searchDegreeSignLabel_ = new QLabel("Sign", searchFilterGroup);
    searchDegreeSignCombo_ = new QComboBox(searchFilterGroup);
    for (int i = 0; i < 12; ++i) {
        searchDegreeSignCombo_->addItem(signName(i));
    }
    searchFilterLayout->addWidget(new QLabel("Event Type", searchFilterGroup), 0, 0);
    searchFilterLayout->addWidget(searchEventCombo_, 0, 1, 1, 2);
    searchFilterLayout->addWidget(new QLabel("Transit Planets", searchFilterGroup), 1, 0);
    searchFilterLayout->addWidget(searchTransitPlanetCombo_, 1, 1, 1, 2);
    searchFilterLayout->addWidget(searchTargetLabel_, 2, 0);
    searchFilterLayout->addWidget(searchTargetCombo_, 2, 1, 1, 2);
    searchFilterLayout->addWidget(new QLabel("Aspect", searchFilterGroup), 3, 0);
    searchFilterLayout->addWidget(searchAspectCombo_, 3, 1);
    searchFilterLayout->addWidget(new QLabel("Orb", searchFilterGroup), 3, 2);
    searchFilterLayout->addWidget(searchOrbSpin_, 3, 3);
    searchFilterLayout->addWidget(new QLabel("House", searchFilterGroup), 4, 0);
    searchFilterLayout->addWidget(searchHouseCombo_, 4, 1);
    searchFilterLayout->addWidget(new QLabel("Sign", searchFilterGroup), 4, 2);
    searchFilterLayout->addWidget(searchSignCombo_, 4, 3);
    searchFilterLayout->addWidget(searchDegreeLabel_, 5, 0);
    searchFilterLayout->addWidget(searchDegreeSpin_, 5, 1);
    searchFilterLayout->addWidget(searchDegreeSignLabel_, 5, 2);
    searchFilterLayout->addWidget(searchDegreeSignCombo_, 5, 3);

    auto* searchRunGroup = new QGroupBox("Run Search", transitSearchPanel_);
    auto* searchRunLayout = new QHBoxLayout(searchRunGroup);
    searchRunButton_ = new QPushButton("Search", searchRunGroup);
    searchStopButton_ = new QPushButton("Stop", searchRunGroup);
    searchUseCurrentButton_ = new QPushButton("Use Current Transit Time", searchRunGroup);
    searchStopButton_->setEnabled(false);
    searchStatusLabel_ = new QLabel("Idle", searchRunGroup);
    searchStatusLabel_->setObjectName("hintLabel");
    searchRunLayout->addWidget(searchRunButton_);
    searchRunLayout->addWidget(searchStopButton_);
    searchRunLayout->addWidget(searchUseCurrentButton_);
    searchRunLayout->addStretch();
    searchRunLayout->addWidget(searchStatusLabel_);

    transitSearchLayout->addWidget(searchRangeGroup);
    transitSearchLayout->addWidget(searchFilterGroup);
    transitSearchLayout->addWidget(searchRunGroup);
    transitSearchLayout->addStretch();

    transitCalendarPanel_ = new QWidget(transitPanelStack_);
    auto* calendarLayout = new QVBoxLayout(transitCalendarPanel_);
    calendarLayout->setContentsMargins(0, 0, 0, 0);
    calendarLayout->setSpacing(8);

    auto* calendarRangeGroup = new QGroupBox("Range", transitCalendarPanel_);
    auto* calendarRangeLayout = new QGridLayout(calendarRangeGroup);
    calendarRangeLayout->setHorizontalSpacing(8);
    calendarRangeLayout->setVerticalSpacing(6);
    calendarRangeLayout->setColumnStretch(1, 1);
    calendarYearCombo_ = new QComboBox(calendarRangeGroup);
    for (int year = 1800; year <= 2399; ++year) {
        calendarYearCombo_->addItem(QString::number(year), year);
    }
    const int calendarYear = QDate::currentDate().year();
    const int calendarIndex = std::clamp(calendarYear - 1800, 0, calendarYearCombo_->count() - 1);
    calendarYearCombo_->setCurrentIndex(calendarIndex);
    calendarMonthCombo_ = new QComboBox(calendarRangeGroup);
    calendarMonthCombo_->addItem("All Months", 0);
    const QLocale calendarLocale;
    for (int month = 1; month <= 12; ++month) {
        calendarMonthCombo_->addItem(calendarLocale.standaloneMonthName(month, QLocale::LongFormat), month);
    }
    calendarPlanetCombo_ = new QComboBox(calendarRangeGroup);
    calendarPlanetCombo_->setEditable(true);
    if (auto* edit = calendarPlanetCombo_->lineEdit()) {
        edit->setReadOnly(true);
        edit->setPlaceholderText("Select planets");
        edit->setCursor(Qt::ArrowCursor);
        edit->installEventFilter(new ComboPopupOnClick(calendarPlanetCombo_));
    }
    auto* calendarPlanetModel = new QStandardItemModel(calendarPlanetCombo_);
    auto* calendarAllItem = new QStandardItem("All");
    calendarAllItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    calendarAllItem->setData(Qt::Unchecked, Qt::CheckStateRole);
    calendarPlanetModel->appendRow(calendarAllItem);
    const QStringList calendarBodies = transitCalculableBodyOrder();
    for (const auto& name : calendarBodies) {
        auto* item = new QStandardItem(name);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        const bool checkedByDefault = !isAsteroidBody(name) && (name != "Moon");
        item->setData(checkedByDefault ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        calendarPlanetModel->appendRow(item);
    }
    calendarPlanetCombo_->setModel(calendarPlanetModel);
    calendarPlanetCombo_->setCurrentIndex(0);
    updateCheckableComboLabel(calendarPlanetCombo_);
    calendarRangeLayout->addWidget(new QLabel("Year", calendarRangeGroup), 0, 0);
    calendarRangeLayout->addWidget(calendarYearCombo_, 0, 1);
    calendarRangeLayout->addWidget(new QLabel("Month", calendarRangeGroup), 1, 0);
    calendarRangeLayout->addWidget(calendarMonthCombo_, 1, 1);
    calendarRangeLayout->addWidget(new QLabel("Planets", calendarRangeGroup), 2, 0);
    calendarRangeLayout->addWidget(calendarPlanetCombo_, 2, 1);

    auto* calendarOptionsGroup = new QGroupBox("Options", transitCalendarPanel_);
    auto* calendarOptionsLayout = new QVBoxLayout(calendarOptionsGroup);
    calendarShowIngressCheck_ = new QCheckBox("Show ingress events", calendarOptionsGroup);
    calendarShowIngressCheck_->setChecked(true);
    calendarShowEgressCheck_ = new QCheckBox("Show egress events", calendarOptionsGroup);
    calendarShowEgressCheck_->setChecked(true);
    calendarShowStationCheck_ = new QCheckBox("Show station events", calendarOptionsGroup);
    calendarShowStationCheck_->setChecked(true);
    calendarShowShadowCheck_ = new QCheckBox("Show shadow events", calendarOptionsGroup);
    calendarShowShadowCheck_->setChecked(true);
    calendarIncludeHousesCheck_ = new QCheckBox("Include house ingress/egress", calendarOptionsGroup);
    calendarIncludeHousesCheck_->setChecked(false);
    calendarOptionsLayout->addWidget(calendarShowIngressCheck_);
    calendarOptionsLayout->addWidget(calendarShowEgressCheck_);
    calendarOptionsLayout->addWidget(calendarShowStationCheck_);
    calendarOptionsLayout->addWidget(calendarShowShadowCheck_);
    calendarOptionsLayout->addWidget(calendarIncludeHousesCheck_);

    auto* calendarRunGroup = new QGroupBox("Run", transitCalendarPanel_);
    auto* calendarRunLayout = new QHBoxLayout(calendarRunGroup);
    calendarRefreshButton_ = new QPushButton("Refresh Calendar", calendarRunGroup);
    calendarStatusLabel_ = new QLabel("Idle", calendarRunGroup);
    calendarStatusLabel_->setObjectName("hintLabel");
    calendarRunLayout->addWidget(calendarRefreshButton_);
    calendarRunLayout->addStretch();
    calendarRunLayout->addWidget(calendarStatusLabel_);

    calendarLayout->addWidget(calendarRangeGroup);
    calendarLayout->addWidget(calendarOptionsGroup);
    calendarLayout->addWidget(calendarRunGroup);
    calendarLayout->addStretch();

    transitConjunctionPanel_ = new QWidget(transitPanelStack_);
    auto* conjLayout = new QVBoxLayout(transitConjunctionPanel_);
    conjLayout->setContentsMargins(0, 0, 0, 0);
    conjLayout->setSpacing(8);

    auto* conjDefinitionGroup = new QGroupBox("Definition", transitConjunctionPanel_);
    auto* conjDefinitionLayout = new QVBoxLayout(conjDefinitionGroup);
    conjBucketSignRadio_ = new QRadioButton("Same Sign", conjDefinitionGroup);
    conjBucketHouseRadio_ = new QRadioButton("Same Natal House", conjDefinitionGroup);
    conjBucketSignRadio_->setChecked(true);
    conjDefinitionLayout->addWidget(conjBucketSignRadio_);
    conjDefinitionLayout->addWidget(conjBucketHouseRadio_);

    auto* conjPlanetGroup = new QGroupBox("Planets", transitConjunctionPanel_);
    auto* conjPlanetLayout = new QGridLayout(conjPlanetGroup);
    conjPlanetLayout->setHorizontalSpacing(8);
    conjPlanetLayout->setVerticalSpacing(6);
    conjPlanetLayout->setColumnStretch(1, 1);
    conjPlanetCombo_ = new QComboBox(conjPlanetGroup);
    conjPlanetCombo_->setEditable(true);
    if (auto* edit = conjPlanetCombo_->lineEdit()) {
        edit->setReadOnly(true);
        edit->setPlaceholderText("Select planets");
        edit->setCursor(Qt::ArrowCursor);
        edit->installEventFilter(new ComboPopupOnClick(conjPlanetCombo_));
    }
    auto* conjPlanetModel = new QStandardItemModel(conjPlanetCombo_);
    auto* conjAllItem = new QStandardItem("All");
    conjAllItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    conjAllItem->setData(Qt::Unchecked, Qt::CheckStateRole);
    conjPlanetModel->appendRow(conjAllItem);
    const QStringList conjBodies = transitCalculableBodyOrder();
    for (const auto& name : conjBodies) {
        auto* item = new QStandardItem(name);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        const bool checkedByDefault = !isAsteroidBody(name) && (name != "Moon");
        item->setData(checkedByDefault ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        conjPlanetModel->appendRow(item);
    }
    conjPlanetCombo_->setModel(conjPlanetModel);
    conjPlanetCombo_->setCurrentIndex(0);
    updateCheckableComboLabel(conjPlanetCombo_);
    conjPlanetLayout->addWidget(new QLabel("Select", conjPlanetGroup), 0, 0);
    conjPlanetLayout->addWidget(conjPlanetCombo_, 0, 1);

    auto* conjParamGroup = new QGroupBox("Parameters", transitConjunctionPanel_);
    auto* conjParamLayout = new QGridLayout(conjParamGroup);
    conjParamLayout->setHorizontalSpacing(8);
    conjParamLayout->setVerticalSpacing(6);
    conjParamLayout->setColumnStretch(1, 1);
    conjCountSpin_ = new QSpinBox(conjParamGroup);
    const int conjMaxCount = std::max(2, static_cast<int>(conjBodies.size()));
    conjCountSpin_->setRange(2, conjMaxCount);
    conjCountSpin_->setValue(4);
    conjUseOrbCheck_ = new QCheckBox("Use orb span", conjParamGroup);
    conjOrbSpin_ = new QDoubleSpinBox(conjParamGroup);
    conjOrbSpin_->setRange(0.5, 30.0);
    conjOrbSpin_->setDecimals(1);
    conjOrbSpin_->setSingleStep(0.5);
    conjOrbSpin_->setValue(10.0);
    conjOrbSpin_->setSuffix("°");
    conjOrbSpin_->setEnabled(false);
    conjParamLayout->addWidget(new QLabel("Min Planets (N)", conjParamGroup), 0, 0);
    conjParamLayout->addWidget(conjCountSpin_, 0, 1);
    conjParamLayout->addWidget(conjUseOrbCheck_, 1, 0);
    conjParamLayout->addWidget(conjOrbSpin_, 1, 1);

    auto* conjRangeGroup = new QGroupBox("Range", transitConjunctionPanel_);
    auto* conjRangeLayout = new QGridLayout(conjRangeGroup);
    conjRangeLayout->setHorizontalSpacing(8);
    conjRangeLayout->setVerticalSpacing(6);
    conjRangeLayout->setColumnStretch(1, 1);
    conjModeNextRadio_ = new QRadioButton("Find Next", conjRangeGroup);
    conjModePrevRadio_ = new QRadioButton("Find Previous", conjRangeGroup);
    conjModeRangeRadio_ = new QRadioButton("Year Range", conjRangeGroup);
    conjModeRangeRadio_->setChecked(true);
    auto* conjModeRow = new QWidget(conjRangeGroup);
    auto* conjModeLayout = new QHBoxLayout(conjModeRow);
    conjModeLayout->setContentsMargins(0, 0, 0, 0);
    conjModeLayout->setSpacing(8);
    conjModeLayout->addWidget(conjModeNextRadio_);
    conjModeLayout->addWidget(conjModePrevRadio_);
    conjModeLayout->addWidget(conjModeRangeRadio_);
    conjModeLayout->addStretch();
    conjStartYearSpin_ = new QSpinBox(conjRangeGroup);
    conjEndYearSpin_ = new QSpinBox(conjRangeGroup);
    conjStartYearSpin_->setRange(1800, 2399);
    conjEndYearSpin_->setRange(1800, 2399);
    const int conjYear = QDate::currentDate().year();
    conjStartYearSpin_->setValue(conjYear);
    conjEndYearSpin_->setValue(conjYear);
    conjReferenceLabel_ = new QLabel("Reference: transit target", conjRangeGroup);
    conjReferenceLabel_->setObjectName("hintLabel");
    conjRangeLayout->addWidget(new QLabel("Mode", conjRangeGroup), 0, 0);
    conjRangeLayout->addWidget(conjModeRow, 0, 1, 1, 2);
    conjRangeLayout->addWidget(new QLabel("Start Year", conjRangeGroup), 1, 0);
    conjRangeLayout->addWidget(conjStartYearSpin_, 1, 1);
    conjRangeLayout->addWidget(new QLabel("End Year", conjRangeGroup), 2, 0);
    conjRangeLayout->addWidget(conjEndYearSpin_, 2, 1);
    conjRangeLayout->addWidget(conjReferenceLabel_, 3, 0, 1, 3);

    auto* conjRunGroup = new QGroupBox("Run", transitConjunctionPanel_);
    auto* conjRunLayout = new QHBoxLayout(conjRunGroup);
    conjRunButton_ = new QPushButton("Find Conjunctions", conjRunGroup);
    conjStopButton_ = new QPushButton("Stop", conjRunGroup);
    conjStopButton_->setEnabled(false);
    conjStatusLabel_ = new QLabel("Idle", conjRunGroup);
    conjStatusLabel_->setObjectName("hintLabel");
    conjRunLayout->addWidget(conjRunButton_);
    conjRunLayout->addWidget(conjStopButton_);
    conjRunLayout->addStretch();
    conjRunLayout->addWidget(conjStatusLabel_);

    conjLayout->addWidget(conjDefinitionGroup);
    conjLayout->addWidget(conjPlanetGroup);
    conjLayout->addWidget(conjParamGroup);
    conjLayout->addWidget(conjRangeGroup);
    conjLayout->addWidget(conjRunGroup);
    conjLayout->addStretch();

    auto* transitScanPanel = new QWidget(transitPanelStack_);
    auto* scanLayout = new QVBoxLayout(transitScanPanel);
    scanLayout->setContentsMargins(0, 0, 0, 0);
    scanLayout->setSpacing(8);

    auto* scanRangeGroup = new QGroupBox("Range", transitScanPanel);
    auto* scanRangeLayout = new QGridLayout(scanRangeGroup);
    scanRangeLayout->setHorizontalSpacing(8);
    scanRangeLayout->setVerticalSpacing(6);
    scanStartYearSpin_ = new QSpinBox(scanRangeGroup);
    scanEndYearSpin_ = new QSpinBox(scanRangeGroup);
    scanStartYearSpin_->setRange(1800, 2399);
    scanEndYearSpin_->setRange(1800, 2399);
    const int currentYear = QDate::currentDate().year();
    scanStartYearSpin_->setValue(currentYear);
    scanEndYearSpin_->setValue(currentYear);
    scanRangeLayout->addWidget(new QLabel("Start Year", scanRangeGroup), 0, 0);
    scanRangeLayout->addWidget(scanStartYearSpin_, 0, 1);
    scanRangeLayout->addWidget(new QLabel("End Year", scanRangeGroup), 1, 0);
    scanRangeLayout->addWidget(scanEndYearSpin_, 1, 1);

    auto* scanModeGroup = new QGroupBox("Mode", transitScanPanel);
    auto* scanModeLayout = new QGridLayout(scanModeGroup);
    scanModeLayout->setHorizontalSpacing(8);
    scanModeLayout->setVerticalSpacing(6);
    scanModeCombo_ = new QComboBox(scanModeGroup);
    scanModeCombo_->addItem("Transit-Natal");
    scanModeCombo_->addItem("Transit-Transit");
    scanModeCombo_->addItem("Transit-Solar Return");
    scanModeCombo_->addItem("Transit-Progressed");
    scanModeCombo_->addItem("Combined");
    scanModeLayout->addWidget(new QLabel("Scan Mode", scanModeGroup), 0, 0);
    scanModeLayout->addWidget(scanModeCombo_, 0, 1);

    auto* scanWeightsGroup = new QGroupBox("Combined Weights", transitScanPanel);
    auto* scanWeightsLayout = new QGridLayout(scanWeightsGroup);
    scanWeightsLayout->setHorizontalSpacing(8);
    scanWeightsLayout->setVerticalSpacing(6);
    scanWeightTransitNatalSpin_ = new QSpinBox(scanWeightsGroup);
    scanWeightTransitTransitSpin_ = new QSpinBox(scanWeightsGroup);
    scanWeightSolarSpin_ = new QSpinBox(scanWeightsGroup);
    scanWeightProgressedSpin_ = new QSpinBox(scanWeightsGroup);
    scanWeightTransitNatalSpin_->setRange(0, 100);
    scanWeightTransitTransitSpin_->setRange(0, 100);
    scanWeightSolarSpin_->setRange(0, 100);
    scanWeightProgressedSpin_->setRange(0, 100);
    scanWeightTransitNatalSpin_->setValue(25);
    scanWeightTransitTransitSpin_->setValue(50);
    scanWeightSolarSpin_->setValue(25);
    scanWeightProgressedSpin_->setValue(0);
    scanWeightsLayout->addWidget(new QLabel("Transit-Natal %", scanWeightsGroup), 0, 0);
    scanWeightsLayout->addWidget(scanWeightTransitNatalSpin_, 0, 1);
    scanWeightsLayout->addWidget(new QLabel("Transit-Transit %", scanWeightsGroup), 1, 0);
    scanWeightsLayout->addWidget(scanWeightTransitTransitSpin_, 1, 1);
    scanWeightsLayout->addWidget(new QLabel("Transit-Solar %", scanWeightsGroup), 2, 0);
    scanWeightsLayout->addWidget(scanWeightSolarSpin_, 2, 1);
    scanWeightsLayout->addWidget(new QLabel("Transit-Progressed %", scanWeightsGroup), 3, 0);
    scanWeightsLayout->addWidget(scanWeightProgressedSpin_, 3, 1);

    auto* scanScoringGroup = new QGroupBox("Scoring", transitScanPanel);
    auto* scanScoringLayout = new QGridLayout(scanScoringGroup);
    scanScoringLayout->setHorizontalSpacing(8);
    scanScoringLayout->setVerticalSpacing(6);
    scanConjunctionCombo_ = new QComboBox(scanScoringGroup);
    scanConjunctionCombo_->addItem("Conjunction: Neutral");
    scanConjunctionCombo_->addItem("Conjunction: Benefic/Malefic");
    scanIncludeAnglesCheck_ = new QCheckBox("Include angles (ASC/MC)", scanScoringGroup);
    scanIncludeAnglesCheck_->setChecked(true);
    scanIncludeNodesCheck_ = new QCheckBox("Include nodes", scanScoringGroup);
    scanIncludeNodesCheck_->setChecked(false);
    scanSolarBiasCheck_ = new QCheckBox("Apply Solar Return year bias", scanScoringGroup);
    scanSolarBiasCheck_->setChecked(false);
    scanSolarBiasSpin_ = new QSpinBox(scanScoringGroup);
    scanSolarBiasSpin_->setRange(0, 100);
    scanSolarBiasSpin_->setValue(25);
    scanSolarBiasSpin_->setSuffix("% strength");
    scanScoringLayout->addWidget(scanConjunctionCombo_, 0, 0, 1, 2);
    scanScoringLayout->addWidget(scanIncludeAnglesCheck_, 1, 0, 1, 2);
    scanScoringLayout->addWidget(scanIncludeNodesCheck_, 2, 0, 1, 2);
    scanScoringLayout->addWidget(scanSolarBiasCheck_, 3, 0, 1, 2);
    scanScoringLayout->addWidget(scanSolarBiasSpin_, 4, 0, 1, 2);

    auto* scanTimeGroup = new QGroupBox("Time", transitScanPanel);
    auto* scanTimeLayout = new QGridLayout(scanTimeGroup);
    scanTimeLayout->setHorizontalSpacing(8);
    scanTimeLayout->setVerticalSpacing(6);
    scanTimeEdit_ = new QTimeEdit(scanTimeGroup);
    scanTimeEdit_->setDisplayFormat("hh:mm:ss AP");
    scanTimeEdit_->setTime(QTime(12, 0, 0));
    scanTimezoneLabel_ = new QLabel("Timezone: natal", scanTimeGroup);
    scanTimezoneLabel_->setObjectName("hintLabel");
    scanTimeLayout->addWidget(new QLabel("Daily Time", scanTimeGroup), 0, 0);
    scanTimeLayout->addWidget(scanTimeEdit_, 0, 1);
    scanTimeLayout->addWidget(scanTimezoneLabel_, 1, 0, 1, 2);

    auto* scanRunGroup = new QGroupBox("Run", transitScanPanel);
    auto* scanRunLayout = new QHBoxLayout(scanRunGroup);
    scanRunButton_ = new QPushButton("Scan Days", scanRunGroup);
    scanCancelButton_ = new QPushButton("Cancel", scanRunGroup);
    scanCancelButton_->setEnabled(false);
    scanProgressBar_ = new QProgressBar(scanRunGroup);
    scanProgressBar_->setRange(0, 100);
    scanProgressBar_->setValue(0);
    scanProgressBar_->setTextVisible(true);
    scanStatusLabel_ = new QLabel("Idle", scanRunGroup);
    scanStatusLabel_->setObjectName("hintLabel");
    scanRunLayout->addWidget(scanRunButton_);
    scanRunLayout->addWidget(scanCancelButton_);
    scanRunLayout->addWidget(scanProgressBar_, 1);
    scanRunLayout->addWidget(scanStatusLabel_);

    auto* scanResultsGroup = new QGroupBox("Results", transitScanPanel);
    auto* scanResultsLayout = new QGridLayout(scanResultsGroup);
    scanResultsLayout->setHorizontalSpacing(8);
    scanResultsLayout->setVerticalSpacing(6);
    scanSortCombo_ = new QComboBox(scanResultsGroup);
    scanSortCombo_->addItem("Best first");
    scanSortCombo_->addItem("Worst first");
    scanTopCountSpin_ = new QSpinBox(scanResultsGroup);
    scanTopCountSpin_->setRange(10, 1000);
    scanTopCountSpin_->setValue(100);
    scanResultsLayout->addWidget(new QLabel("Sort", scanResultsGroup), 0, 0);
    scanResultsLayout->addWidget(scanSortCombo_, 0, 1);
    scanResultsLayout->addWidget(new QLabel("Show Top N", scanResultsGroup), 1, 0);
    scanResultsLayout->addWidget(scanTopCountSpin_, 1, 1);

    scanLayout->addWidget(scanRangeGroup);
    scanLayout->addWidget(scanModeGroup);
    scanLayout->addWidget(scanWeightsGroup);
    scanLayout->addWidget(scanScoringGroup);
    scanLayout->addWidget(scanTimeGroup);
    scanLayout->addWidget(scanRunGroup);
    scanLayout->addWidget(scanResultsGroup);
    scanLayout->addStretch();

    transitProfectionPanel_ = new QWidget(transitPanelStack_);
    auto* profectionLayout = new QVBoxLayout(transitProfectionPanel_);
    profectionLayout->setContentsMargins(0, 0, 0, 0);
    profectionLayout->setSpacing(8);

    auto* profectionReferenceGroup = new QGroupBox("Reference", transitProfectionPanel_);
    auto* profectionReferenceLayout = new QGridLayout(profectionReferenceGroup);
    profectionReferenceLayout->setHorizontalSpacing(8);
    profectionReferenceLayout->setVerticalSpacing(6);
    profectionReferenceLayout->setColumnStretch(1, 1);
    profectionReferenceLabel_ = new QLabel("Reference: -", profectionReferenceGroup);
    profectionReferenceLabel_->setObjectName("hintLabel");
    profectionAgeSpin_ = new QSpinBox(profectionReferenceGroup);
    profectionAgeSpin_->setRange(0, 130);
    profectionAgeSpin_->setValue(0);
    profectionUseTransitAgeButton_ = new QPushButton("Use Transit Age", profectionReferenceGroup);
    profectionReferenceLayout->addWidget(profectionReferenceLabel_, 0, 0, 1, 3);
    profectionReferenceLayout->addWidget(new QLabel("Age (years)", profectionReferenceGroup), 1, 0);
    profectionReferenceLayout->addWidget(profectionAgeSpin_, 1, 1);
    profectionReferenceLayout->addWidget(profectionUseTransitAgeButton_, 1, 2);

    auto* profectionRunGroup = new QGroupBox("Run", transitProfectionPanel_);
    auto* profectionRunLayout = new QHBoxLayout(profectionRunGroup);
    profectionRunButton_ = new QPushButton("Analyze Profections", profectionRunGroup);
    profectionStatusLabel_ = new QLabel("Ready", profectionRunGroup);
    profectionStatusLabel_->setObjectName("hintLabel");
    profectionRunLayout->addWidget(profectionRunButton_);
    profectionRunLayout->addStretch();
    profectionRunLayout->addWidget(profectionStatusLabel_);

    auto* profectionRulesGroup = new QGroupBox("Rhetorius Rules", transitProfectionPanel_);
    auto* profectionRulesLayout = new QVBoxLayout(profectionRulesGroup);
    auto* profectionRulesLabel = new QLabel(
        "Annual sign = start sign + (age mod 12). "
        "Lord condition: angular/11th is active, 6/8/12 is difficult. "
        "Transit triggers: Mars/Saturn by conjunction-square-opposition pressure; "
        "Jupiter/Venus by conjunction-trine support.",
        profectionRulesGroup);
    profectionRulesLabel->setWordWrap(true);
    profectionRulesLabel->setObjectName("hintLabel");
    profectionRulesLayout->addWidget(profectionRulesLabel);

    profectionLayout->addWidget(profectionReferenceGroup);
    profectionLayout->addWidget(profectionRunGroup);
    profectionLayout->addWidget(profectionRulesGroup);
    profectionLayout->addStretch();

    transitLunationPanel_ = new QWidget(transitPanelStack_);
    auto* lunationLayout = new QVBoxLayout(transitLunationPanel_);
    lunationLayout->setContentsMargins(0, 0, 0, 0);
    lunationLayout->setSpacing(8);

    auto* lunationEventGroup = new QGroupBox("Events", transitLunationPanel_);
    auto* lunationEventLayout = new QGridLayout(lunationEventGroup);
    lunationEventLayout->setHorizontalSpacing(8);
    lunationEventLayout->setVerticalSpacing(6);
    lunationNewMoonCheck_ = new QCheckBox("New Moon", lunationEventGroup);
    lunationFullMoonCheck_ = new QCheckBox("Full Moon", lunationEventGroup);
    lunationSolarEclipseCheck_ = new QCheckBox("Solar Eclipse", lunationEventGroup);
    lunationLunarEclipseCheck_ = new QCheckBox("Lunar Eclipse", lunationEventGroup);
    lunationNewMoonCheck_->setChecked(true);
    lunationFullMoonCheck_->setChecked(true);
    lunationEventLayout->addWidget(lunationNewMoonCheck_, 0, 0);
    lunationEventLayout->addWidget(lunationFullMoonCheck_, 0, 1);
    lunationEventLayout->addWidget(lunationSolarEclipseCheck_, 1, 0);
    lunationEventLayout->addWidget(lunationLunarEclipseCheck_, 1, 1);

    auto* lunationModeGroup = new QGroupBox("Search", transitLunationPanel_);
    auto* lunationModeLayout = new QGridLayout(lunationModeGroup);
    lunationModeLayout->setHorizontalSpacing(8);
    lunationModeLayout->setVerticalSpacing(6);
    lunationModeLayout->setColumnStretch(1, 1);
    lunationModeNextRadio_ = new QRadioButton("Find Next", lunationModeGroup);
    lunationModePrevRadio_ = new QRadioButton("Find Previous", lunationModeGroup);
    lunationModeRangeRadio_ = new QRadioButton("Year Range", lunationModeGroup);
    lunationModeNextRadio_->setChecked(true);
    auto* lunationModeRow = new QWidget(lunationModeGroup);
    auto* lunationModeRowLayout = new QHBoxLayout(lunationModeRow);
    lunationModeRowLayout->setContentsMargins(0, 0, 0, 0);
    lunationModeRowLayout->setSpacing(8);
    lunationModeRowLayout->addWidget(lunationModeNextRadio_);
    lunationModeRowLayout->addWidget(lunationModePrevRadio_);
    lunationModeRowLayout->addWidget(lunationModeRangeRadio_);
    lunationModeRowLayout->addStretch();

    lunationStartYearSpin_ = new QSpinBox(lunationModeGroup);
    lunationEndYearSpin_ = new QSpinBox(lunationModeGroup);
    lunationStartYearSpin_->setRange(1800, 2399);
    lunationEndYearSpin_->setRange(1800, 2399);
    const int lunationYear = QDate::currentDate().year();
    lunationStartYearSpin_->setValue(lunationYear);
    lunationEndYearSpin_->setValue(lunationYear);
    lunationDegreeRangeCheck_ = new QCheckBox("Filter by degree range (in sign)", lunationModeGroup);
    auto* lunationDegreeRangeRow = new QWidget(lunationModeGroup);
    auto* lunationDegreeRangeLayout = new QHBoxLayout(lunationDegreeRangeRow);
    lunationDegreeRangeLayout->setContentsMargins(0, 0, 0, 0);
    lunationDegreeRangeLayout->setSpacing(6);
    lunationDegreeRangeStartSpin_ = new QDoubleSpinBox(lunationDegreeRangeRow);
    lunationDegreeRangeStartSpin_->setRange(0.0, 29.99);
    lunationDegreeRangeStartSpin_->setDecimals(2);
    lunationDegreeRangeStartSpin_->setSingleStep(0.25);
    lunationDegreeRangeStartSpin_->setValue(27.0);
    lunationDegreeRangeStartSpin_->setSuffix(QString(QChar(0x00B0)));
    lunationDegreeRangeEndSpin_ = new QDoubleSpinBox(lunationDegreeRangeRow);
    lunationDegreeRangeEndSpin_->setRange(0.0, 29.99);
    lunationDegreeRangeEndSpin_->setDecimals(2);
    lunationDegreeRangeEndSpin_->setSingleStep(0.25);
    lunationDegreeRangeEndSpin_->setValue(29.0);
    lunationDegreeRangeEndSpin_->setSuffix(QString(QChar(0x00B0)));
    lunationDegreeRangeLayout->addWidget(new QLabel("From", lunationDegreeRangeRow));
    lunationDegreeRangeLayout->addWidget(lunationDegreeRangeStartSpin_);
    lunationDegreeRangeLayout->addWidget(new QLabel("To", lunationDegreeRangeRow));
    lunationDegreeRangeLayout->addWidget(lunationDegreeRangeEndSpin_);
    lunationDegreeRangeLayout->addStretch();
    lunationTimezoneLabel_ = new QLabel("Timezone: natal", lunationModeGroup);
    lunationTimezoneLabel_->setObjectName("hintLabel");
    auto* lunationRefLabel = new QLabel("Reference: system now", lunationModeGroup);
    lunationRefLabel->setObjectName("hintLabel");
    lunationModeLayout->addWidget(new QLabel("Mode", lunationModeGroup), 0, 0);
    lunationModeLayout->addWidget(lunationModeRow, 0, 1, 1, 2);
    lunationModeLayout->addWidget(new QLabel("Start Year", lunationModeGroup), 1, 0);
    lunationModeLayout->addWidget(lunationStartYearSpin_, 1, 1);
    lunationModeLayout->addWidget(new QLabel("End Year", lunationModeGroup), 2, 0);
    lunationModeLayout->addWidget(lunationEndYearSpin_, 2, 1);
    lunationModeLayout->addWidget(lunationDegreeRangeCheck_, 3, 0, 1, 3);
    lunationModeLayout->addWidget(lunationDegreeRangeRow, 4, 1, 1, 2);
    lunationModeLayout->addWidget(new QLabel("Timezone", lunationModeGroup), 5, 0);
    lunationModeLayout->addWidget(lunationTimezoneLabel_, 5, 1, 1, 2);
    lunationModeLayout->addWidget(lunationRefLabel, 6, 0, 1, 3);

    auto* lunationAnalysisGroup = new QGroupBox("Degree Analysis", transitLunationPanel_);
    auto* lunationAnalysisLayout = new QGridLayout(lunationAnalysisGroup);
    lunationAnalysisLayout->setHorizontalSpacing(8);
    lunationAnalysisLayout->setVerticalSpacing(6);
    lunationAnalysisLayout->setColumnStretch(1, 1);
    lunationAnalysisLayout->setColumnStretch(2, 1);

    lunationAnalysisCombo_ = new QComboBox(lunationAnalysisGroup);
    lunationAnalysisCombo_->addItems({"List Events", "Repeated Degrees", "Target Degree"});

    lunationDegreeExactRadio_ = new QRadioButton("Exact (arc-min/sec)", lunationAnalysisGroup);
    lunationDegreeOrbRadio_ = new QRadioButton("Within orb", lunationAnalysisGroup);
    lunationDegreeExactRadio_->setChecked(true);
    lunationOrbCombo_ = new QComboBox(lunationAnalysisGroup);
    lunationOrbCombo_->addItem("±0.25°", 0.25);
    lunationOrbCombo_->addItem("±0.5°", 0.5);
    lunationOrbCombo_->addItem("±1.0°", 1.0);
    lunationOrbCombo_->setCurrentIndex(1);

    auto* strictnessRow = new QWidget(lunationAnalysisGroup);
    auto* strictnessLayout = new QHBoxLayout(strictnessRow);
    strictnessLayout->setContentsMargins(0, 0, 0, 0);
    strictnessLayout->setSpacing(6);
    strictnessLayout->addWidget(lunationDegreeExactRadio_);
    strictnessLayout->addWidget(lunationDegreeOrbRadio_);
    strictnessLayout->addWidget(lunationOrbCombo_);
    strictnessLayout->addStretch();

    lunationMatchCombo_ = new QComboBox(lunationAnalysisGroup);
    lunationMatchCombo_->addItems({"Degree only", "Degree + Sign", "Degree + Natal House", "Degree + Sign + Natal House"});

    lunationSignModeCombo_ = new QComboBox(lunationAnalysisGroup);
    lunationSignModeCombo_->addItems({"Any", "Only selected", "Exclude selected"});
    lunationSignCombo_ = new QComboBox(lunationAnalysisGroup);
    lunationSignCombo_->setEditable(true);
    if (auto* edit = lunationSignCombo_->lineEdit()) {
        edit->setReadOnly(true);
        edit->setPlaceholderText("Select signs");
        edit->setCursor(Qt::ArrowCursor);
        edit->installEventFilter(new ComboPopupOnClick(lunationSignCombo_));
    }
    auto* signModel = new QStandardItemModel(lunationSignCombo_);
    auto* signAllItem = new QStandardItem("All");
    signAllItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    signAllItem->setData(Qt::Checked, Qt::CheckStateRole);
    signModel->appendRow(signAllItem);
    const QStringList signNames = zodiacSigns();
    for (const auto& name : signNames) {
        auto* item = new QStandardItem(name);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setData(Qt::Checked, Qt::CheckStateRole);
        signModel->appendRow(item);
    }
    lunationSignCombo_->setModel(signModel);
    lunationSignCombo_->setCurrentIndex(0);
    updateTransitPlanetComboLabel(lunationSignCombo_);

    lunationHouseModeCombo_ = new QComboBox(lunationAnalysisGroup);
    lunationHouseModeCombo_->addItems({"Any", "Only selected", "Exclude selected"});
    lunationHouseCombo_ = new QComboBox(lunationAnalysisGroup);
    lunationHouseCombo_->setEditable(true);
    if (auto* edit = lunationHouseCombo_->lineEdit()) {
        edit->setReadOnly(true);
        edit->setPlaceholderText("Select houses");
        edit->setCursor(Qt::ArrowCursor);
        edit->installEventFilter(new ComboPopupOnClick(lunationHouseCombo_));
    }
    auto* houseModel = new QStandardItemModel(lunationHouseCombo_);
    auto* houseAllItem = new QStandardItem("All");
    houseAllItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    houseAllItem->setData(Qt::Checked, Qt::CheckStateRole);
    houseModel->appendRow(houseAllItem);
    for (int i = 1; i <= 12; ++i) {
        auto* item = new QStandardItem(QString::number(i));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setData(Qt::Checked, Qt::CheckStateRole);
        houseModel->appendRow(item);
    }
    lunationHouseCombo_->setModel(houseModel);
    lunationHouseCombo_->setCurrentIndex(0);
    updateTransitPlanetComboLabel(lunationHouseCombo_);

    auto* targetRow = new QWidget(lunationAnalysisGroup);
    auto* lunationTargetLayout = new QHBoxLayout(targetRow);
    lunationTargetLayout->setContentsMargins(0, 0, 0, 0);
    lunationTargetLayout->setSpacing(6);
    lunationTargetDegSpin_ = new QSpinBox(targetRow);
    lunationTargetDegSpin_->setRange(0, 29);
    lunationTargetDegSpin_->setSuffix("°");
    lunationTargetMinSpin_ = new QSpinBox(targetRow);
    lunationTargetMinSpin_->setRange(0, 59);
    lunationTargetMinSpin_->setSuffix("'");
    lunationTargetSecSpin_ = new QSpinBox(targetRow);
    lunationTargetSecSpin_->setRange(0, 59);
    lunationTargetSecSpin_->setSuffix("\"");
    lunationTargetLayout->addWidget(lunationTargetDegSpin_);
    lunationTargetLayout->addWidget(lunationTargetMinSpin_);
    lunationTargetLayout->addWidget(lunationTargetSecSpin_);
    lunationTargetLayout->addStretch();

    lunationAnalysisHintLabel_ = new QLabel("Natal house filters require a loaded chart.", lunationAnalysisGroup);
    lunationAnalysisHintLabel_->setObjectName("hintLabel");

    int analysisRow = 0;
    lunationAnalysisLayout->addWidget(new QLabel("Mode", lunationAnalysisGroup), analysisRow, 0);
    lunationAnalysisLayout->addWidget(lunationAnalysisCombo_, analysisRow++, 1, 1, 2);
    lunationAnalysisLayout->addWidget(new QLabel("Strictness", lunationAnalysisGroup), analysisRow, 0);
    lunationAnalysisLayout->addWidget(strictnessRow, analysisRow++, 1, 1, 2);
    lunationAnalysisLayout->addWidget(new QLabel("Match Key", lunationAnalysisGroup), analysisRow, 0);
    lunationAnalysisLayout->addWidget(lunationMatchCombo_, analysisRow++, 1, 1, 2);
    lunationAnalysisLayout->addWidget(new QLabel("Sign Filter", lunationAnalysisGroup), analysisRow, 0);
    lunationAnalysisLayout->addWidget(lunationSignModeCombo_, analysisRow, 1);
    lunationAnalysisLayout->addWidget(lunationSignCombo_, analysisRow++, 2);
    lunationAnalysisLayout->addWidget(new QLabel("House Filter", lunationAnalysisGroup), analysisRow, 0);
    lunationAnalysisLayout->addWidget(lunationHouseModeCombo_, analysisRow, 1);
    lunationAnalysisLayout->addWidget(lunationHouseCombo_, analysisRow++, 2);
    lunationAnalysisLayout->addWidget(new QLabel("Target Degree", lunationAnalysisGroup), analysisRow, 0);
    lunationAnalysisLayout->addWidget(targetRow, analysisRow++, 1, 1, 2);
    lunationAnalysisLayout->addWidget(lunationAnalysisHintLabel_, analysisRow, 0, 1, 3);

    auto* lunationRunGroup = new QGroupBox("Run", transitLunationPanel_);
    auto* lunationRunLayout = new QHBoxLayout(lunationRunGroup);
    lunationRunButton_ = new QPushButton("Search", lunationRunGroup);
    lunationStopButton_ = new QPushButton("Stop", lunationRunGroup);
    lunationStopButton_->setEnabled(false);
    lunationStatusLabel_ = new QLabel("Idle", lunationRunGroup);
    lunationStatusLabel_->setObjectName("hintLabel");
    lunationRunLayout->addWidget(lunationRunButton_);
    lunationRunLayout->addWidget(lunationStopButton_);
    lunationRunLayout->addStretch();
    lunationRunLayout->addWidget(lunationStatusLabel_);

    lunationLayout->addWidget(lunationEventGroup);
    lunationLayout->addWidget(lunationModeGroup);
    lunationLayout->addWidget(lunationAnalysisGroup);
    lunationLayout->addWidget(lunationRunGroup);
    lunationLayout->addStretch();

    transitPanelStack_->addWidget(transitOverviewPanel_);
    transitPanelStack_->addWidget(transitSearchPanel_);
    transitPanelStack_->addWidget(transitCalendarPanel_);
    transitPanelStack_->addWidget(transitConjunctionPanel_);
    transitPanelStack_->addWidget(transitScanPanel);
    transitPanelStack_->addWidget(transitProfectionPanel_);
    transitPanelStack_->addWidget(transitLunationPanel_);

    transitLayout->addWidget(transitPanelStack_);

// Astrocartography panel (controls) is guarded to avoid QtLocation dependency.
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    astrocartographyPanel_ = new QWidget(this);
    astrocartographyPanel_->setObjectName("dataPanel");
    auto* astroLayout = new QVBoxLayout(astrocartographyPanel_);
    astroLayout->setContentsMargins(6, 6, 6, 6);
    astroLayout->setSpacing(8);

    auto* astroModeGroup = new QGroupBox("Mode", astrocartographyPanel_);
    auto* astroModeLayout = new QVBoxLayout(astroModeGroup);
    astroModeCombo_ = new QComboBox(astroModeGroup);
    astroModeCombo_->addItems({"Geodetic (Transits)", "Astrocartography (Chart)"});
    astroModeCombo_->setCurrentIndex(0);
    astroModeHintLabel_ = new QLabel("Astrocartography lines will use the loaded chart (coming soon).", astroModeGroup);
    astroModeHintLabel_->setObjectName("hintLabel");
    astroModeLayout->addWidget(astroModeCombo_);
    astroModeLayout->addWidget(astroModeHintLabel_);

    geodeticGroup_ = new QGroupBox("Geodetic Equivalents", astrocartographyPanel_);
    auto* geodeticLayout = new QGridLayout(geodeticGroup_);
    geodeticLayout->setHorizontalSpacing(8);
    geodeticLayout->setVerticalSpacing(6);
    geodeticLayout->setColumnStretch(1, 1);
    geodeticPlanetCombo_ = new QComboBox(geodeticGroup_);
    geodeticPlanetCombo_->setEditable(true);
    if (auto* edit = geodeticPlanetCombo_->lineEdit()) {
        edit->setReadOnly(true);
        edit->setPlaceholderText("Select planets");
        edit->setCursor(Qt::ArrowCursor);
        edit->installEventFilter(new ComboPopupOnClick(geodeticPlanetCombo_));
    }
    auto* geodeticModel = new QStandardItemModel(geodeticPlanetCombo_);
    auto* allGeodeticItem = new QStandardItem("All");
    allGeodeticItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    allGeodeticItem->setData(Qt::Checked, Qt::CheckStateRole);
    geodeticModel->appendRow(allGeodeticItem);
    for (const auto& name : geodeticBodyOrder()) {
        auto* item = new QStandardItem(name);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setData(Qt::Checked, Qt::CheckStateRole);
        geodeticModel->appendRow(item);
    }
    geodeticPlanetCombo_->setModel(geodeticModel);
    geodeticPlanetCombo_->setCurrentIndex(0);
    updateTransitPlanetComboLabel(geodeticPlanetCombo_);

    geodeticExactRadio_ = new QRadioButton("Exact", geodeticGroup_);
    geodeticOrbRadio_ = new QRadioButton("Within orb", geodeticGroup_);
    geodeticExactRadio_->setChecked(true);
    geodeticOrbCombo_ = new QComboBox(geodeticGroup_);
    geodeticOrbCombo_->addItem("±0.25°", 0.25);
    geodeticOrbCombo_->addItem("±0.5°", 0.5);
    geodeticOrbCombo_->addItem("±1.0°", 1.0);
    geodeticOrbCombo_->setCurrentIndex(1);

    auto* geodeticStrictRow = new QWidget(geodeticGroup_);
    auto* geodeticStrictLayout = new QHBoxLayout(geodeticStrictRow);
    geodeticStrictLayout->setContentsMargins(0, 0, 0, 0);
    geodeticStrictLayout->setSpacing(6);
    geodeticStrictLayout->addWidget(geodeticExactRadio_);
    geodeticStrictLayout->addWidget(geodeticOrbRadio_);
    geodeticStrictLayout->addWidget(geodeticOrbCombo_);
    geodeticStrictLayout->addStretch();

    geodeticTimeLabel_ = new QLabel("Using transit time: -", geodeticGroup_);
    geodeticTimeLabel_->setObjectName("hintLabel");
    geodeticRefreshButton_ = new QPushButton("Refresh Map", geodeticGroup_);
    geodeticStatusLabel_ = new QLabel("Idle", geodeticGroup_);
    geodeticStatusLabel_->setObjectName("hintLabel");

    geodeticLayout->addWidget(new QLabel("Planets", geodeticGroup_), 0, 0);
    geodeticLayout->addWidget(geodeticPlanetCombo_, 0, 1, 1, 2);
    geodeticLayout->addWidget(new QLabel("Strictness", geodeticGroup_), 1, 0);
    geodeticLayout->addWidget(geodeticStrictRow, 1, 1, 1, 2);
    geodeticLayout->addWidget(geodeticTimeLabel_, 2, 0, 1, 3);
    geodeticLayout->addWidget(geodeticRefreshButton_, 3, 0);
    geodeticLayout->addWidget(geodeticStatusLabel_, 3, 1, 1, 2);

    astroLayout->addWidget(astroModeGroup);
    astroLayout->addWidget(geodeticGroup_);
    astroLayout->addStretch();
#endif
    dataStack_ = new QStackedWidget(this);
    dataStack_->addWidget(dataPanel);
    dataStack_->addWidget(transitPanel_);
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    dataStack_->addWidget(astrocartographyPanel_);
#endif

    aspectsPanel_ = new QFrame(this);
    aspectsPanel_->setObjectName("aspectsPanel");
    auto* aspectsLayout = new QVBoxLayout(aspectsPanel_);
    aspectsLayout->setContentsMargins(6, 6, 6, 6);
    aspectScopeTabs_ = new QTabBar(aspectsPanel_);
    aspectScopeTabs_->addTab("Transit-Natal");
    aspectScopeTabs_->addTab("Transit-Transit");
    aspectScopeTabs_->addTab("Natal-Natal");
    aspectScopeTabs_->setExpanding(false);
    aspectScopeTabs_->setDrawBase(false);
    aspectScopeTabs_->setVisible(false);
    auto* aspectsHeader = new QWidget(aspectsPanel_);
    auto* aspectsHeaderLayout = new QHBoxLayout(aspectsHeader);
    aspectsHeaderLayout->setContentsMargins(0, 0, 0, 0);
    aspectsCopyButton_ = new QPushButton("Copy Aspects", aspectsHeader);
    aspectsHeaderLayout->addWidget(aspectScopeTabs_);
    aspectsHeaderLayout->addStretch();
    aspectsHeaderLayout->addWidget(aspectsCopyButton_);
    aspectsLayout->addWidget(aspectsHeader);
    aspectsLayout->addWidget(aspectsTable_, 1);

    leftSplitter_ = new QSplitter(Qt::Vertical, this);
    leftSplitter_->addWidget(dataStack_);
    leftSplitter_->addWidget(aspectsPanel_);
    leftSplitter_->setStretchFactor(0, 1);
    leftSplitter_->setStretchFactor(1, 1);
    leftSplitter_->setChildrenCollapsible(false);
    leftSplitter_->setSizes({520, 280});
    dataStack_->setMinimumHeight(220);
    aspectsPanel_->setMinimumHeight(220);

    dataDock_ = new QDockWidget("Chart Data", this);
    dataDock_->setObjectName("dock_chart_data");
    dataDock_->setWidget(leftSplitter_);
    dataDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::LeftDockWidgetArea, dataDock_);

    rightTopTable_ = new QTableWidget(this);
    auto* rightTopPanel = new QFrame(this);
    rightTopPanel->setObjectName("dataPanel");
    auto* rightTopLayout = new QVBoxLayout(rightTopPanel);
    rightTopLayout->setContentsMargins(6, 6, 6, 6);
    rightTopLayout->addWidget(rightTopTable_);

    rightTopDock_ = new QDockWidget("Transits", this);
    rightTopDock_->setObjectName("dock_right_top");
    rightTopDock_->setWidget(rightTopPanel);
    rightTopDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::RightDockWidgetArea, rightTopDock_);

    rightBottomTable_ = new QTableWidget(this);
    auto* rightBottomPanel = new QFrame(this);
    rightBottomPanel->setObjectName("dataPanel");
    auto* rightBottomLayout = new QVBoxLayout(rightBottomPanel);
    rightBottomLayout->setContentsMargins(6, 6, 6, 6);
    rightBottomLayout->addWidget(rightBottomTable_);
    auto* rightBottomFooter = new QWidget(rightBottomPanel);
    auto* rightBottomFooterLayout = new QHBoxLayout(rightBottomFooter);
    rightBottomFooterLayout->setContentsMargins(0, 0, 0, 0);
    rightBottomCopyButton_ = new QPushButton("Copy Lunation Placements", rightBottomFooter);
    rightBottomCopyButton_->setVisible(false);
    rightBottomCopyButton_->setEnabled(false);
    rightBottomFooterLayout->addStretch();
    rightBottomFooterLayout->addWidget(rightBottomCopyButton_);
    rightBottomLayout->addWidget(rightBottomFooter);

    rightBottomDock_ = new QDockWidget("Ingress Countdown", this);
    rightBottomDock_->setObjectName("dock_right_bottom");
    rightBottomDock_->setWidget(rightBottomPanel);
    rightBottomDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::RightDockWidgetArea, rightBottomDock_);
    splitDockWidget(rightTopDock_, rightBottomDock_, Qt::Vertical);

    const auto dockFeatures = QDockWidget::DockWidgetMovable
        | QDockWidget::DockWidgetFloatable
        | QDockWidget::DockWidgetClosable;
    dataDock_->setFeatures(dockFeatures);
    rightTopDock_->setFeatures(dockFeatures);
    rightBottomDock_->setFeatures(dockFeatures);

    resizeDocks({rightTopDock_, rightBottomDock_}, {380, 320}, Qt::Vertical);
    resizeDocks({dataDock_, rightTopDock_}, {420, 300}, Qt::Horizontal);

    defaultDockState_ = saveState();
    refreshProfileToolbar();

    updateTransitLocationAvailability();
    updateTransitTimezoneStatus();
    markTransitPending();
    updateAstrocartographyModeUi();
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu("&File");
    auto* newChartAction = fileMenu->addAction("New Chart...");
    auto* editChartAction = fileMenu->addAction("Edit Current Chart...");
    fileMenu->addSeparator();
    auto* openProfileAction = fileMenu->addAction("Open Profile...");
    auto* saveProfileAction = fileMenu->addAction("Save Profile...");
    auto* deleteProfileAction = fileMenu->addAction("Delete Profile...");
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction("Exit");

    connect(newChartAction, &QAction::triggered, this, &MainWindow::handleNewChart);
    connect(editChartAction, &QAction::triggered, this, &MainWindow::handleEditChart);
    connect(openProfileAction, &QAction::triggered, this, &MainWindow::handleLoadProfile);
    connect(saveProfileAction, &QAction::triggered, this, &MainWindow::handleSaveProfile);
    connect(deleteProfileAction, &QAction::triggered, this, &MainWindow::handleDeleteProfile);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* chartMenu = menuBar()->addMenu("&Chart");
    auto* chartSetupAction = chartMenu->addAction("Chart Setup...");
    auto* recomputeAction = chartMenu->addAction("Recompute");
    auto* aspectOrbsAction = chartMenu->addAction("Aspect Orbs...");
    chartMenu->addSeparator();
    auto* houseMenu = chartMenu->addMenu("House System");
    auto* houseWhole = houseMenu->addAction("Whole Sign");
    auto* housePlacidus = houseMenu->addAction("Placidus");
    houseWhole->setCheckable(true);
    housePlacidus->setCheckable(true);

    auto updateHouseChecks = [this, houseWhole, housePlacidus]() {
        const auto system = hasCurrentChart_ ? currentInput_.houseSystem : defaultHouseSystem_;
        houseWhole->setChecked(system == HouseSystem::WholeSign);
        housePlacidus->setChecked(system == HouseSystem::Placidus);
    };
    updateHouseChecks();
    connect(houseMenu, &QMenu::aboutToShow, this, updateHouseChecks);

    auto applyHouseSystem = [this, updateHouseChecks](HouseSystem system) {
        defaultHouseSystem_ = system;
        if (hasCurrentChart_) {
            auto input = currentInput_;
            input.houseSystem = system;
            computeChart(input, currentLocation_);
        }
        updateHouseChecks();
    };

    connect(houseWhole, &QAction::triggered, this, [applyHouseSystem]() {
        applyHouseSystem(HouseSystem::WholeSign);
    });
    connect(housePlacidus, &QAction::triggered, this, [applyHouseSystem]() {
        applyHouseSystem(HouseSystem::Placidus);
    });

    connect(chartSetupAction, &QAction::triggered, this, &MainWindow::handleEditChart);
    connect(recomputeAction, &QAction::triggered, this, &MainWindow::handleRecompute);
    connect(aspectOrbsAction, &QAction::triggered, this, &MainWindow::handleAspectOrbs);

    auto* viewMenu = menuBar()->addMenu("&View");
    if (dataDock_) {
        viewMenu->addAction(dataDock_->toggleViewAction());
    }
    if (rightTopDock_) {
        viewMenu->addAction(rightTopDock_->toggleViewAction());
    }
    if (rightBottomDock_) {
        viewMenu->addAction(rightBottomDock_->toggleViewAction());
    }
    viewMenu->addSeparator();
    lockLayoutAction_ = viewMenu->addAction("Lock Layout");
    lockLayoutAction_->setCheckable(true);
    connect(lockLayoutAction_, &QAction::toggled, this, &MainWindow::setLayoutLocked);
    auto* resetLayoutAction = viewMenu->addAction("Reset Layout");
    connect(resetLayoutAction, &QAction::triggered, this, &MainWindow::resetDockLayout);
    viewMenu->addSeparator();
    auto* aspectHeaderMenu = viewMenu->addMenu("Aspect Header Labels");
    auto* aspectHeaderGroup = new QActionGroup(this);
    aspectHeaderGlyphAction_ = aspectHeaderMenu->addAction("Glyphs");
    aspectHeaderAbbrevAction_ = aspectHeaderMenu->addAction("Abbrev");
    aspectHeaderFullAction_ = aspectHeaderMenu->addAction("Full");
    aspectHeaderGlyphAction_->setCheckable(true);
    aspectHeaderAbbrevAction_->setCheckable(true);
    aspectHeaderFullAction_->setCheckable(true);
    aspectHeaderGroup->addAction(aspectHeaderGlyphAction_);
    aspectHeaderGroup->addAction(aspectHeaderAbbrevAction_);
    aspectHeaderGroup->addAction(aspectHeaderFullAction_);
    aspectHeaderGlyphAction_->setChecked(aspectHeaderMode_ == AspectHeaderMode::Glyphs);
    aspectHeaderAbbrevAction_->setChecked(aspectHeaderMode_ == AspectHeaderMode::Abbrev);
    aspectHeaderFullAction_->setChecked(aspectHeaderMode_ == AspectHeaderMode::Full);
    connect(aspectHeaderGlyphAction_, &QAction::triggered, this, [this]() {
        aspectHeaderMode_ = AspectHeaderMode::Glyphs;
        refreshAspectsForHeaderMode();
    });
    connect(aspectHeaderAbbrevAction_, &QAction::triggered, this, [this]() {
        aspectHeaderMode_ = AspectHeaderMode::Abbrev;
        refreshAspectsForHeaderMode();
    });
    connect(aspectHeaderFullAction_, &QAction::triggered, this, [this]() {
        aspectHeaderMode_ = AspectHeaderMode::Full;
        refreshAspectsForHeaderMode();
    });

    auto* themeMenu = menuBar()->addMenu("&Theme");
    auto* themeGroup = new QActionGroup(this);
    themeLightAction_ = themeMenu->addAction("Light");
    themeDarkAction_ = themeMenu->addAction("Dark");
    themeLightAction_->setCheckable(true);
    themeDarkAction_->setCheckable(true);
    themeGroup->addAction(themeLightAction_);
    themeGroup->addAction(themeDarkAction_);
    connect(themeLightAction_, &QAction::triggered, this, [this]() {
        applyTheme(ThemeMode::Light);
    });
    connect(themeDarkAction_, &QAction::triggered, this, [this]() {
        applyTheme(ThemeMode::Dark);
    });

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* aboutAction = helpMenu->addAction("About");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "About DracoVed", "DracoVed C++ Prototype\nTropical Natal MVP");
    });
}

QString MainWindow::buildStyleSheet(ThemeMode mode) const {
    if (mode == ThemeMode::Dark) {
        return
            "QMainWindow { background-color: #0b0c0d; color: #e2e2e2; }"
            "QDialog { background-color: #0b0c0d; color: #e2e2e2; }"
            "QWidget { color: #e2e2e2; }"
            "QMenuBar { background-color: #0f1112; color: #e2e2e2; }"
            "QMenuBar::item:selected { background-color: #1f2326; }"
            "QLineEdit, QDateEdit, QTimeEdit, QComboBox, QDoubleSpinBox {"
            "  background-color: #141618; border: 1px solid #2a2d30; padding: 3px; border-radius: 3px;"
            "}"
            "QDateEdit, QTimeEdit, QDoubleSpinBox { padding-right: 20px; }"
            "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {"
            "  subcontrol-origin: border; width: 16px; border-left: 1px solid #2a2d30; background-color: #1b1f22;"
            "}"
            "QAbstractSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 3px; }"
            "QAbstractSpinBox::down-button { subcontrol-position: bottom right; border-top: 1px solid #2a2d30; border-bottom-right-radius: 3px; }"
            "QAbstractSpinBox::up-arrow, QAbstractSpinBox::down-arrow { width: 8px; height: 8px; }"
            "QLineEdit:focus, QDateEdit:focus, QTimeEdit:focus, QComboBox:focus, QDoubleSpinBox:focus {"
            "  border: 1px solid #b14040;"
            "}"
            "QAbstractItemView {"
            "  background-color: #141618; color: #e2e2e2; selection-background-color: #1f2326; selection-color: #e2e2e2;"
            "}"
            "QAbstractItemView::item { padding: 4px 6px; }"
            "QPushButton { background-color: #1b1f22; border: 1px solid #2a2d30; padding: 4px 10px; border-radius: 3px; }"
            "QPushButton:hover { border: 1px solid #b14040; }"
            "QPushButton:pressed { background-color: #15181b; }"
            "QToolButton { background-color: #1b1f22; border: 1px solid #2a2d30; padding: 2px 6px; border-radius: 3px; }"
            "QToolButton:hover { border: 1px solid #b14040; background-color: #22272b; }"
            "QToolButton:pressed { background-color: #171b1e; }"
            "QDockWidget { background-color: #0f1112; }"
            "QDockWidget::title { background-color: #121416; border: 1px solid #202326; padding: 4px 8px; }"
            "QFrame#profileQuickBar { background-color: #121416; border: 1px solid #202326; border-radius: 6px; }"
            "QLabel#profileQuickState { color: #a0a0a0; border: none; }"
            "QFrame#profileQuickBar QToolButton { padding: 3px 10px; border-radius: 4px; }"
            "QFrame#profileQuickBar QToolButton:hover { background-color: #242a2f; border: 1px solid #c45858; }"
            "QFrame#profileQuickBar QToolButton:pressed { background-color: #1a1f23; }"
            "QFrame#profileQuickBar QToolButton:disabled { color: #5a5f63; border-color: #2a2d30; }"
            "QFrame#dataPanel, QFrame#aspectsPanel, QWidget#chartPlaceholder {"
            "  background-color: #0f1112; border: 1px solid #202326; border-radius: 6px;"
            "}"
            "QHeaderView::section { background-color: #121416; color: #d8d8d8; border: 1px solid #202326; padding: 3px 6px; }"
            "QTableWidget { background-color: #0f1112; alternate-background-color: #121416; gridline-color: #1f2326; }"
            "QTabWidget::pane { border: 1px solid #202326; top: -1px; }"
            "QTabBar::tab { background: #141618; padding: 6px 10px; border: 1px solid #202326; border-bottom: none; }"
            "QTabBar::tab:selected { background: #1b1f22; border-color: #b14040; }"
            "QMenu { background-color: #141618; color: #e2e2e2; border: 1px solid #2a2d30; }"
            "QMenu::item { padding: 6px 24px 6px 24px; }"
            "QMenu::item:selected { background-color: #1f2326; }"
            "QMenu::separator { height: 1px; background: #2a2d30; margin: 4px 8px; }"
            "QMenu::item:disabled { color: #5a5f63; }"
            "QGroupBox { border: 1px solid #202326; margin-top: 8px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
            "QRadioButton { spacing: 8px; }"
            "QLabel#hintLabel { color: #a0a0a0; }";
    }

    return
        "QMainWindow { background-color: #ffffff; color: #1b1b1b; }"
        "QDialog { background-color: #ffffff; color: #1b1b1b; }"
        "QWidget { color: #1b1b1b; }"
        "QMenuBar { background-color: #f2f2f2; color: #1b1b1b; }"
        "QMenuBar::item:selected { background-color: #e6e6e6; }"
        "QLineEdit, QDateEdit, QTimeEdit, QComboBox, QDoubleSpinBox {"
        "  background-color: #ffffff; border: 1px solid #c9c9c9; padding: 3px; border-radius: 3px;"
        "}"
        "QDateEdit, QTimeEdit, QDoubleSpinBox { padding-right: 20px; }"
        "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {"
        "  subcontrol-origin: border; width: 16px; border-left: 1px solid #c9c9c9; background-color: #f3f3f3;"
        "}"
        "QAbstractSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 3px; }"
        "QAbstractSpinBox::down-button { subcontrol-position: bottom right; border-top: 1px solid #c9c9c9; border-bottom-right-radius: 3px; }"
        "QAbstractSpinBox::up-arrow, QAbstractSpinBox::down-arrow { width: 8px; height: 8px; }"
        "QLineEdit:focus, QDateEdit:focus, QTimeEdit:focus, QComboBox:focus, QDoubleSpinBox:focus {"
        "  border: 1px solid #b14040;"
        "}"
        "QAbstractItemView {"
        "  background-color: #ffffff; color: #1b1b1b; selection-background-color: #e6e6e6; selection-color: #1b1b1b;"
        "}"
        "QAbstractItemView::item { padding: 4px 6px; }"
        "QPushButton { background-color: #f3f3f3; border: 1px solid #c9c9c9; padding: 4px 10px; border-radius: 3px; }"
        "QPushButton:hover { border: 1px solid #b14040; }"
        "QPushButton:pressed { background-color: #e8e8e8; }"
        "QToolButton { background-color: #f3f3f3; border: 1px solid #c9c9c9; padding: 2px 6px; border-radius: 3px; }"
        "QToolButton:hover { border: 1px solid #b14040; background-color: #ececec; }"
        "QToolButton:pressed { background-color: #e2e2e2; }"
        "QDockWidget { background-color: #fafafa; }"
        "QDockWidget::title { background-color: #f1f1f1; border: 1px solid #d6d6d6; padding: 4px 8px; }"
        "QFrame#profileQuickBar { background-color: #f8f8f8; border: 1px solid #d6d6d6; border-radius: 6px; }"
        "QLabel#profileQuickState { color: #666666; border: none; }"
        "QFrame#profileQuickBar QToolButton { padding: 3px 10px; border-radius: 4px; }"
        "QFrame#profileQuickBar QToolButton:hover { background-color: #e9e9e9; border: 1px solid #b14040; }"
        "QFrame#profileQuickBar QToolButton:pressed { background-color: #dfdfdf; }"
        "QFrame#profileQuickBar QToolButton:disabled { color: #8a8a8a; border-color: #d0d0d0; }"
        "QFrame#dataPanel, QFrame#aspectsPanel, QWidget#chartPlaceholder {"
        "  background-color: #ffffff; border: 1px solid #d6d6d6; border-radius: 6px;"
        "}"
        "QHeaderView::section { background-color: #f1f1f1; color: #1b1b1b; border: 1px solid #d6d6d6; padding: 3px 6px; }"
        "QTableWidget { background-color: #ffffff; alternate-background-color: #f7f7f7; gridline-color: #e2e2e2; }"
        "QTabWidget::pane { border: 1px solid #d6d6d6; top: -1px; }"
        "QTabBar::tab { background: #f1f1f1; padding: 6px 10px; border: 1px solid #d6d6d6; border-bottom: none; }"
        "QTabBar::tab:selected { background: #ffffff; border-color: #b14040; }"
        "QMenu { background-color: #ffffff; color: #1b1b1b; border: 1px solid #c9c9c9; }"
        "QMenu::item { padding: 6px 24px 6px 24px; }"
        "QMenu::item:selected { background-color: #e6e6e6; }"
        "QMenu::separator { height: 1px; background: #d6d6d6; margin: 4px 8px; }"
        "QMenu::item:disabled { color: #8a8a8a; }"
        "QGroupBox { border: 1px solid #d6d6d6; margin-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
        "QRadioButton { spacing: 8px; }"
        "QLabel#hintLabel { color: #6f6f6f; }";
}

ChartWheelTheme MainWindow::buildChartTheme(ThemeMode mode) const {
    if (mode == ThemeMode::Dark) {
        return ChartWheelTheme{
            QColor("#0f1112"),
            QColor("#202326"),
            QColor("#1f2225"),
            QColor("#24282b"),
            QColor("#1b1f22"),
            QColor("#6a6f73"),
            QColor("#2b2f33"),
            QColor("#2f3337"),
            QColor("#b14040"),
            QColor("#3a3f44"),
            QColor("#9aa0a6"),
            QColor("#23272b"),
            QColor(11, 12, 13, 200),
            QColor("#c0c0c0"),
            QColor("#98a9bf"),
            QColor("#7c7f84"),
            QColor("#e6e6e6"),
            QColor("#cfd3d6"),
            QColor("#e6e6e6"),
        };
    }
    return ChartWheelTheme{
        QColor("#ffffff"),
        QColor("#d7d7d7"),
        QColor("#e2e2e2"),
        QColor("#dedede"),
        QColor("#e8e8e8"),
        QColor("#8a8a8a"),
        QColor("#d0d0d0"),
        QColor("#cfcfcf"),
        QColor("#b14040"),
        QColor("#c0c0c0"),
        QColor("#6f6f6f"),
        QColor("#d0d0d0"),
        QColor(245, 245, 245, 220),
        QColor("#6f7378"),
        QColor("#6a7a90"),
        QColor("#7a7e83"),
        QColor("#2b2b2b"),
        QColor("#2b2b2b"),
        QColor("#1f1f1f"),
    };
}

void MainWindow::applyTheme(ThemeMode mode) {
    theme_ = mode;
    const QString style = buildStyleSheet(mode);
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        app->setStyleSheet(style);
    } else {
        setStyleSheet(style);
    }
    if (chartWheel_) {
        // Enforce Light/Pastel theme regardless of mode for now, or ensure Light mode is truly Light
        if (mode == ThemeMode::Light) {
            ChartWheelTheme lightTheme{
                QColor("#FFFFFF"),          // background - White
                QColor("#E0E0E0"),          // ringOuter - Soft Grey
                QColor("#F5F5F5"),          // ringZodiac - Very Light Grey (or transparent context)
                QColor("#EEEEEE"),          // ringHouseOuter
                QColor("#EEEEEE"),          // ringHouseInner
                QColor("#9E9E9E"),          // placeholderText
                QColor("#BDBDBD"),          // tick
                QColor("#E0E0E0"),          // signBoundary - Soft Grey
                QColor("#5D4037"),          // signGlyph - Dark Brown/Gold for contrast on pastel
                QColor("#C8C8C8"),          // houseLine - Darker for visibility
                QColor("#757575"),          // houseLabel - Dark Grey
                QColor("#E0E0E0"),          // transitRing
                QColor(255, 255, 255, 220), // aspectSymbolBg - White semi-transparent
                QColor("#B0BEC5"),          // aspectLineNeutral - Blue Grey
                QColor("#90CAF9"),          // aspectLineTransitTransit
                QColor("#B39DDB"),          // aspectLineNatalNatal
                QColor("#37474F"),          // body - Dark Blue Grey for planets
                QColor("#546E7A"),          // natalBody
                QColor("#37474F"),          // transitBody
                // Element colors - Pastel Tints
                QColor("#FFF3E0"),          // elementFire - Soft Peach
                QColor("#E8F5E9"),          // elementEarth - Soft Sage
                QColor("#E3F2FD"),          // elementAir - Soft Sky
                QColor("#F3E5F5"),          // elementWater - Soft Lavender
                // Additional visual enhancements
                QColor("#E0E0E0"),          // aspectInnerCircle
                QColor("#D32F2F"),          // retrogradeIndicator - Red
                QColor("#F57F17"),          // angularHouseLabel - Dark Gold
            };
            chartWheel_->setTheme(lightTheme);
        } else {
             // Keep Dark Theme for Dark Mode (optional, but let's stick to Light as requested)
             chartWheel_->setTheme(buildChartTheme(mode)); 
        }
    }
    if (themeLightAction_) {
        themeLightAction_->setChecked(mode == ThemeMode::Light);
    }
    if (themeDarkAction_) {
        themeDarkAction_->setChecked(mode == ThemeMode::Dark);
    }
    if (aspectHoverRow_ >= 0 && aspectHoverCol_ >= 0) {
        const int row = aspectHoverRow_;
        const int col = aspectHoverCol_;
        updateAspectHover(row, col);
    }
}

QString MainWindow::aspectHeaderLabel(const QString& name) const {
    switch (aspectHeaderMode_) {
        case AspectHeaderMode::Glyphs: {
            QString glyph = bodyGlyph(name);
            if (glyph.trimmed().isEmpty() || glyph == "?") {
                glyph = abbrevForName(name);
            }
            return glyph;
        }
        case AspectHeaderMode::Full:
            return name;
        case AspectHeaderMode::Abbrev:
        default:
            return abbrevForName(name);
    }
}

void MainWindow::refreshAspectsForHeaderMode() {
    if (!aspectsTable_) {
        return;
    }
    if (activeTab_ == AppTab::Natal) {
        if (hasCurrentChart_) {
            populateAspects(currentChart_);
        } else {
            setupTable(aspectsTable_, {}, 0);
        }
        return;
    }
    if (activeTab_ == AppTab::Progression) {
        if (progressionView_ == ProgressionView::NatalOnly) {
            if (hasCurrentChart_) {
                populateAspects(currentChart_);
            } else {
                setupTable(aspectsTable_, {}, 0);
            }
            return;
        }
        if (!hasProgressionChart_) {
            setupTable(aspectsTable_, {}, 0);
            return;
        }
        if (progressionView_ == ProgressionView::Overlay) {
            populateProgressedAspectsOverlay(currentProgressionChart_, currentChart_);
        } else {
            populateAspects(currentProgressionChart_);
        }
        return;
    }
    if (activeTab_ == AppTab::SolarReturn) {
        if (!hasCurrentChart_ || !hasSolarChart_) {
            setupTable(aspectsTable_, {}, 0);
            return;
        }
        switch (solarAspectView_) {
            case SolarAspectView::SolarNatal:
                populateSolarNatalAspectsOverlay(currentSolarChart_, currentChart_);
                break;
            case SolarAspectView::SolarReturn:
            default:
                populateAspects(currentSolarChart_);
                break;
        }
        return;
    }
    if (activeTab_ == AppTab::Relocation) {
        if (!hasCurrentChart_ || !hasRelocationChart_) {
            setupTable(aspectsTable_, {}, 0);
            return;
        }
        switch (relocationAspectView_) {
            case RelocationAspectView::RelocationNatal:
                populateRelocationNatalAspectsOverlay(currentRelocationChart_, currentChart_);
                break;
            case RelocationAspectView::Relocation:
            default:
                populateAspects(currentRelocationChart_);
                break;
        }
        return;
    }
    if (transitMode_ == TransitMode::NatalOverlay) {
        if (!hasCurrentChart_ || !hasTransitChart_) {
            setupTable(aspectsTable_, {}, 0);
            return;
        }
        switch (transitAspectView_) {
            case TransitAspectView::TransitTransit:
                populateAspects(currentTransitChart_);
                break;
            case TransitAspectView::NatalNatal:
                populateAspects(currentChart_);
                break;
            case TransitAspectView::TransitNatal:
            default:
                populateTransitAspectsOverlay(currentTransitChart_, currentChart_);
                break;
        }
    } else {
        if (hasTransitChart_) {
            populateAspects(currentTransitChart_);
        } else {
            setupTable(aspectsTable_, {}, 0);
        }
    }
}

void MainWindow::applyAspectTriangle(int size) {
    if (!aspectsTable_) {
        return;
    }
    aspectsTable_->clearSpans();
    if (size <= 0) {
        return;
    }
    const QColor blank = (theme_ == ThemeMode::Dark) ? QColor("#0f1112") : QColor("#ffffff");
    for (int row = 0; row < size; ++row) {
        const int startCol = row + 1;
        const int span = size - startCol;
        if (span <= 0) {
            continue;
        }
        auto* item = aspectsTable_->item(row, startCol);
        if (!item) {
            item = makeCell("");
            aspectsTable_->setItem(row, startCol, item);
        }
        item->setFlags(Qt::NoItemFlags);
        item->setText("");
        item->setToolTip("");
        item->setBackground(blank);

        for (int col = startCol + 1; col < size; ++col) {
            auto* cell = aspectsTable_->item(row, col);
            if (!cell) {
                cell = makeCell("");
                aspectsTable_->setItem(row, col, cell);
            }
            cell->setFlags(Qt::NoItemFlags);
            cell->setText("");
            cell->setToolTip("");
            cell->setBackground(blank);
        }
        aspectsTable_->setSpan(row, startCol, 1, span);
    }
}

void MainWindow::updateAspectHover(int row, int column) {
    if (!aspectsTable_) {
        return;
    }
    if (row < 0 || column < 0) {
        clearAspectHover();
        return;
    }
    if (aspectTriangleEnabled_ && row < column) {
        clearAspectHover();
        return;
    }
    if (row == aspectHoverRow_ && column == aspectHoverCol_) {
        return;
    }
    clearAspectHover();

    const int rows = aspectsTable_->rowCount();
    const int cols = aspectsTable_->columnCount();
    if (row >= rows || column >= cols) {
        return;
    }

    aspectHoverRow_ = row;
    aspectHoverCol_ = column;

    const QColor axisColor = (theme_ == ThemeMode::Dark) ? QColor("#1f2326") : QColor("#e6e6e6");
    const QColor crossColor = (theme_ == ThemeMode::Dark) ? QColor("#2a2f33") : QColor("#dcdcdc");

    for (int c = 0; c < cols; ++c) {
        if (aspectTriangleEnabled_ && row < c) {
            continue;
        }
        if (auto* item = aspectsTable_->item(row, c)) {
            item->setBackground(axisColor);
        }
    }
    for (int r = 0; r < rows; ++r) {
        if (aspectTriangleEnabled_ && r < column) {
            continue;
        }
        if (auto* item = aspectsTable_->item(r, column)) {
            item->setBackground(axisColor);
        }
    }
    if (auto* item = aspectsTable_->item(row, column)) {
        item->setBackground(crossColor);
    }
    if (auto* header = aspectsTable_->horizontalHeaderItem(column)) {
        header->setBackground(axisColor);
    }
    if (auto* header = aspectsTable_->verticalHeaderItem(row)) {
        header->setBackground(axisColor);
    }
}

void MainWindow::clearAspectHover() {
    if (!aspectsTable_) {
        return;
    }
    if (aspectHoverRow_ < 0 && aspectHoverCol_ < 0) {
        return;
    }
    const int rows = aspectsTable_->rowCount();
    const int cols = aspectsTable_->columnCount();
    const QBrush clearBrush;

    if (aspectHoverRow_ >= 0 && aspectHoverRow_ < rows) {
        for (int c = 0; c < cols; ++c) {
            if (aspectTriangleEnabled_ && aspectHoverRow_ < c) {
                continue;
            }
            if (auto* item = aspectsTable_->item(aspectHoverRow_, c)) {
                item->setBackground(clearBrush);
            }
        }
        if (auto* header = aspectsTable_->verticalHeaderItem(aspectHoverRow_)) {
            header->setBackground(clearBrush);
        }
    }
    if (aspectHoverCol_ >= 0 && aspectHoverCol_ < cols) {
        for (int r = 0; r < rows; ++r) {
            if (aspectTriangleEnabled_ && r < aspectHoverCol_) {
                continue;
            }
            if (auto* item = aspectsTable_->item(r, aspectHoverCol_)) {
                item->setBackground(clearBrush);
            }
        }
        if (auto* header = aspectsTable_->horizontalHeaderItem(aspectHoverCol_)) {
            header->setBackground(clearBrush);
        }
    }

    aspectHoverRow_ = -1;
    aspectHoverCol_ = -1;
}
void MainWindow::setupConnections() {
    if (mainTabBar_) {
        connect(mainTabBar_, &QTabBar::currentChanged, this, &MainWindow::handleMainTabChanged);
    }
    if (profileToolbarLoadButton_) {
        connect(profileToolbarLoadButton_, &QToolButton::clicked, this, [this]() {
            if (profileToolbarCombo_) {
                const QString selected = profileToolbarCombo_->currentData().toString().trimmed();
                if (!selected.isEmpty()) {
                    loadProfileByName(selected);
                    return;
                }
            }
            handleLoadProfile();
        });
    }
    if (profileToolbarSaveButton_) {
        connect(profileToolbarSaveButton_, &QToolButton::clicked, this, [this]() {
            if (!currentProfileName_.trimmed().isEmpty()) {
                saveProfileByName(currentProfileName_, false);
            } else {
                handleSaveProfile();
            }
        });
    }
    if (profileToolbarSaveAsButton_) {
        connect(profileToolbarSaveAsButton_, &QToolButton::clicked, this, &MainWindow::handleSaveProfile);
    }
    if (profileToolbarEditButton_) {
        connect(profileToolbarEditButton_, &QToolButton::clicked, this, &MainWindow::handleEditChart);
    }
    if (profileToolbarDeleteButton_) {
        connect(profileToolbarDeleteButton_, &QToolButton::clicked, this, [this]() {
            if (profileToolbarCombo_) {
                const QString selected = profileToolbarCombo_->currentData().toString().trimmed();
                if (!selected.isEmpty()) {
                    const QString filePath = profileFilePath(selected);
                    if (!filePath.isEmpty() && QFileInfo::exists(filePath)) {
                        const auto result = QMessageBox::question(
                            this,
                            "Delete profile",
                            QString("Delete profile \"%1\"?").arg(selected),
                            QMessageBox::Yes | QMessageBox::No);
                        if (result != QMessageBox::Yes) {
                            return;
                        }
                        if (!QFile::remove(filePath)) {
                            setStatusMessage("Unable to delete profile.");
                            return;
                        }
                        if (currentProfileName_ == selected) {
                            currentProfileName_.clear();
                        }
                        refreshProfileToolbar();
                        return;
                    }
                }
            }
            handleDeleteProfile();
        });
    }
    if (profileToolbarCombo_) {
        connect(profileToolbarCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            const QString selected = profileToolbarCombo_->currentData().toString().trimmed();
            if (profileToolbarLoadButton_) {
                profileToolbarLoadButton_->setEnabled(!selected.isEmpty());
            }
            if (profileToolbarDeleteButton_) {
                profileToolbarDeleteButton_->setEnabled(!selected.isEmpty());
            }
        });
    }
    if (transitSubTabBar_) {
        connect(transitSubTabBar_, &QTabBar::currentChanged, this, &MainWindow::handleTransitSubTabChanged);
    }
    if (aspectScopeTabs_) {
        connect(aspectScopeTabs_, &QTabBar::currentChanged, this, &MainWindow::handleTransitAspectViewChanged);
    }
    connect(chartSettingsButton_, &QToolButton::clicked, this, &MainWindow::showChartSettingsMenu);
    connect(zoomInButton_, &QToolButton::clicked, this, [this]() {
        if (chartWheel_) {
            chartWheel_->zoomIn();
        }
    });
    connect(zoomOutButton_, &QToolButton::clicked, this, [this]() {
        if (chartWheel_) {
            chartWheel_->zoomOut();
        }
    });
    connect(zoomResetButton_, &QToolButton::clicked, this, [this]() {
        if (chartWheel_) {
            chartWheel_->resetZoom();
        }
    });
// Map-specific signals are guarded; don't move these outside the macro.
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    if (worldMapView_) {
        connect(worldMapView_, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status status) {
            if (status != QQuickWidget::Ready) {
                return;
            }
            worldMapRoot_ = worldMapView_->rootObject();
            worldMapReady_ = (worldMapRoot_ != nullptr);
            updateAstrocartographyView();
        });
    }
#endif

    if (transitOverlayRadio_) {
        connect(transitOverlayRadio_, &QRadioButton::toggled, this, &MainWindow::handleTransitModeChanged);
    }
    if (transitOnlyRadio_) {
        connect(transitOnlyRadio_, &QRadioButton::toggled, this, &MainWindow::handleTransitModeChanged);
    }
    if (transitWholeRadio_) {
        connect(transitWholeRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                transitHouseSystem_ = HouseSystem::WholeSign;
                refreshTransitsTab();
                updateTransitSearchTargets();
                if (transitSubTab_ == TransitSubTab::Calendar && calendarIncludeHousesCheck_ && calendarIncludeHousesCheck_->isChecked()) {
                    handleTransitCalendarRun();
                }
                if (transitSubTab_ == TransitSubTab::Conjunctions && conjBucketHouseRadio_ && conjBucketHouseRadio_->isChecked()) {
                    handleTransitConjunctionRun();
                }
            }
        });
    }
    if (transitPlacidusRadio_) {
        connect(transitPlacidusRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                transitHouseSystem_ = HouseSystem::Placidus;
                refreshTransitsTab();
                updateTransitSearchTargets();
                if (transitSubTab_ == TransitSubTab::Calendar && calendarIncludeHousesCheck_ && calendarIncludeHousesCheck_->isChecked()) {
                    handleTransitCalendarRun();
                }
                if (transitSubTab_ == TransitSubTab::Conjunctions && conjBucketHouseRadio_ && conjBucketHouseRadio_->isChecked()) {
                    handleTransitConjunctionRun();
                }
            }
        });
    }
    if (transitNowButton_) {
        connect(transitNowButton_, &QPushButton::clicked, this, &MainWindow::handleTransitNow);
    }
    if (transitPlusDayButton_) {
        connect(transitPlusDayButton_, &QPushButton::clicked, this, [this]() {
            handleTransitShiftDays(1);
        });
    }
    if (transitPlusWeekButton_) {
        connect(transitPlusWeekButton_, &QPushButton::clicked, this, [this]() {
            handleTransitShiftDays(7);
        });
    }
    if (transitPlusMonthButton_) {
        connect(transitPlusMonthButton_, &QPushButton::clicked, this, [this]() {
            handleTransitShiftDays(30);
        });
    }
    if (transitCalculateButton_) {
        connect(transitCalculateButton_, &QPushButton::clicked, this, &MainWindow::handleTransitCalculate);
    }
    if (transitResetTimeButton_) {
        connect(transitResetTimeButton_, &QPushButton::clicked, this, &MainWindow::handleTransitResetTime);
    }
    if (transitDateEdit_) {
        connect(transitDateEdit_, &QDateEdit::dateChanged, this, [this](const QDate&) {
            markTransitPending();
            updateAstrocartographyView();
        });
    }
    if (transitTimeEdit_) {
        connect(transitTimeEdit_, &QTimeEdit::timeChanged, this, [this](const QTime&) {
            markTransitPending();
            updateAstrocartographyView();
        });
    }
    if (transitTimezoneEdit_) {
        connect(transitTimezoneEdit_, &QLineEdit::editingFinished, this, [this]() {
            updateTransitTimezoneStatus();
            markTransitPending();
            updateAstrocartographyView();
            if (transitSubTab_ == TransitSubTab::Calendar) {
                handleTransitCalendarRun();
            }
        });
        connect(transitTimezoneEdit_, &QLineEdit::textChanged, this, &MainWindow::updateTransitTimezoneStatus);
    }
    if (transitUseNatalLocation_) {
        connect(transitUseNatalLocation_, &QCheckBox::toggled, this, [this](bool checked) {
            Q_UNUSED(checked);
            updateTransitLocationAvailability();
            if (hasCurrentChart_ && transitUseNatalLocation_->isChecked()) {
                syncTransitLocationFromNatal();
            }
            markTransitPending();
            if (transitSubTab_ == TransitSubTab::Calendar && calendarIncludeHousesCheck_ && calendarIncludeHousesCheck_->isChecked()) {
                handleTransitCalendarRun();
            }
        });
    }
    if (transitLocationEdit_) {
        connect(transitLocationEdit_, &QLineEdit::editingFinished, this, &MainWindow::markTransitPending);
    }
    if (transitLatSpin_) {
        connect(transitLatSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::markTransitPending);
    }
    if (transitLonSpin_) {
        connect(transitLonSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::markTransitPending);
    }
    if (transitGeocodeButton_) {
        connect(transitGeocodeButton_, &QPushButton::clicked, this, &MainWindow::handleTransitGeocode);
    }
    if (progressionViewNatalRadio_) {
        connect(progressionViewNatalRadio_, &QRadioButton::toggled, this, &MainWindow::handleProgressionViewChanged);
    }
    if (progressionViewProgressedRadio_) {
        connect(progressionViewProgressedRadio_, &QRadioButton::toggled, this, &MainWindow::handleProgressionViewChanged);
    }
    if (progressionViewOverlayRadio_) {
        connect(progressionViewOverlayRadio_, &QRadioButton::toggled, this, &MainWindow::handleProgressionViewChanged);
    }
    if (progressionDateEdit_) {
        connect(progressionDateEdit_, &QDateEdit::dateChanged, this, &MainWindow::markProgressionPending);
    }
    if (progressionTimeEdit_) {
        connect(progressionTimeEdit_, &QTimeEdit::timeChanged, this, &MainWindow::markProgressionPending);
    }
    if (progressionTimezoneEdit_) {
        connect(progressionTimezoneEdit_, &QLineEdit::editingFinished, this, [this]() {
            updateProgressionTimezoneStatus();
            markProgressionPending();
        });
        connect(progressionTimezoneEdit_, &QLineEdit::textChanged, this, &MainWindow::updateProgressionTimezoneStatus);
    }
    if (progressionNowButton_) {
        connect(progressionNowButton_, &QPushButton::clicked, this, &MainWindow::handleProgressionNow);
    }
    if (progressionCalculateButton_) {
        connect(progressionCalculateButton_, &QPushButton::clicked, this, &MainWindow::handleProgressionCalculate);
    }
    if (solarYearSpin_) {
        connect(solarYearSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::markSolarPending);
    }
    if (solarUseNatalRadio_) {
        connect(solarUseNatalRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            Q_UNUSED(checked);
            updateSolarLocationAvailability();
            syncSolarLocationFromNatal();
            markSolarPending();
        });
    }
    if (solarUseCustomRadio_) {
        connect(solarUseCustomRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            Q_UNUSED(checked);
            updateSolarLocationAvailability();
            markSolarPending();
        });
    }
    if (solarLocationEdit_) {
        connect(solarLocationEdit_, &QLineEdit::editingFinished, this, &MainWindow::markSolarPending);
    }
    if (solarLatSpin_) {
        connect(solarLatSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::markSolarPending);
    }
    if (solarLonSpin_) {
        connect(solarLonSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::markSolarPending);
    }
    if (solarTimezoneEdit_) {
        connect(solarTimezoneEdit_, &QLineEdit::editingFinished, this, [this]() {
            updateSolarTimezoneStatus();
            markSolarPending();
        });
        connect(solarTimezoneEdit_, &QLineEdit::textChanged, this, &MainWindow::updateSolarTimezoneStatus);
    }
    if (solarGeocodeButton_) {
        connect(solarGeocodeButton_, &QPushButton::clicked, this, &MainWindow::handleSolarGeocode);
    }
    if (solarCalculateButton_) {
        connect(solarCalculateButton_, &QPushButton::clicked, this, &MainWindow::handleSolarCalculate);
    }
    if (relocationLocationEdit_) {
        connect(relocationLocationEdit_, &QLineEdit::editingFinished, this, &MainWindow::markRelocationPending);
    }
    if (relocationLatSpin_) {
        connect(relocationLatSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::markRelocationPending);
    }
    if (relocationLonSpin_) {
        connect(relocationLonSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::markRelocationPending);
    }
    if (relocationTimezoneEdit_) {
        connect(relocationTimezoneEdit_, &QLineEdit::editingFinished, this, [this]() {
            updateRelocationTimezoneStatus();
            markRelocationPending();
        });
        connect(relocationTimezoneEdit_, &QLineEdit::textChanged, this, &MainWindow::updateRelocationTimezoneStatus);
    }
    if (relocationGeocodeButton_) {
        connect(relocationGeocodeButton_, &QPushButton::clicked, this, &MainWindow::handleRelocationGeocode);
    }
    if (relocationWholeRadio_) {
        connect(relocationWholeRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                relocationHouseSystem_ = HouseSystem::WholeSign;
                markRelocationPending();
            }
        });
    }
    if (relocationPlacidusRadio_) {
        connect(relocationPlacidusRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                relocationHouseSystem_ = HouseSystem::Placidus;
                markRelocationPending();
            }
        });
    }
    if (relocationOverlayCheck_) {
        connect(relocationOverlayCheck_, &QCheckBox::toggled, this, [this](bool) {
            if (activeTab_ == AppTab::Relocation) {
                refreshRelocationView();
            }
            updateChartLegend();
        });
    }
    if (relocationCalculateButton_) {
        connect(relocationCalculateButton_, &QPushButton::clicked, this, &MainWindow::handleRelocationCalculate);
    }
    if (solarTechniqueDateEdit_) {
        connect(solarTechniqueDateEdit_, &QDateEdit::dateChanged, this, &MainWindow::refreshSolarTechniqueView);
    }
    if (solarTechniqueOrbSpin_) {
        connect(solarTechniqueOrbSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &MainWindow::refreshSolarTechniqueView);
    }
    if (solarTechniqueNatalCheck_) {
        connect(solarTechniqueNatalCheck_, &QCheckBox::toggled, this, &MainWindow::refreshSolarTechniqueView);
    }
    if (solarTechniqueSolarCheck_) {
        connect(solarTechniqueSolarCheck_, &QCheckBox::toggled, this, &MainWindow::refreshSolarTechniqueView);
    }
    if (solarTechniqueRankMetricCombo_) {
        connect(solarTechniqueRankMetricCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::refreshSolarTechniqueView);
    }
    if (solarTechniqueRankOrderCombo_) {
        connect(solarTechniqueRankOrderCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::refreshSolarTechniqueView);
    }
    if (solarTechniqueTopSpin_) {
        connect(solarTechniqueTopSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &MainWindow::refreshSolarTechniqueView);
    }
    if (tabs_) {
        connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
            if (activeTab_ != AppTab::SolarReturn) {
                return;
            }
            updateAspectScopeTabs();
            updateSolarTechniqueDockTitles();
            refreshSolarTechniqueView();
        });
    }
    if (searchRunButton_) {
        connect(searchRunButton_, &QPushButton::clicked, this, &MainWindow::handleTransitSearchRun);
    }
    if (searchStopButton_) {
        connect(searchStopButton_, &QPushButton::clicked, this, &MainWindow::handleTransitSearchStop);
    }
    if (searchUseCurrentButton_) {
        connect(searchUseCurrentButton_, &QPushButton::clicked, this, [this]() {
            if (!searchStartDateEdit_ || !searchStartTimeEdit_) {
                return;
            }
            const QDateTime currentLocal = transitSelectedLocal();
            if (!currentLocal.isValid()) {
                return;
            }
            QTimeZone tz;
            QString label;
            QString err;
            const QString tzText = searchTimezoneEdit_ ? searchTimezoneEdit_->text().trimmed() : QString("UTC");
            if (!parseTimezoneInput(tzText, &tz, &label, &err)) {
                tz = QTimeZone::utc();
            }
            const QDateTime adjusted = currentLocal.toTimeZone(tz);
            searchStartDateEdit_->setDate(adjusted.date());
            searchStartTimeEdit_->setTime(adjusted.time());
            if (searchEndDateEdit_ && searchEndTimeEdit_) {
                QDateTime endLocal(searchEndDateEdit_->date(), searchEndTimeEdit_->time(), tz);
                if (!endLocal.isValid() || adjusted >= endLocal) {
                    const QDateTime fallback = adjusted.addDays(30);
                    searchEndDateEdit_->setDate(fallback.date());
                    searchEndTimeEdit_->setTime(fallback.time());
                }
            }
        });
    }
    if (searchTransitPlanetCombo_) {
        auto* model = qobject_cast<QStandardItemModel*>(searchTransitPlanetCombo_->model());
        if (model && model->rowCount() > 0) {
            updateTransitPlanetComboLabel(searchTransitPlanetCombo_);
            connect(model, &QStandardItemModel::itemChanged, this, [this, model](QStandardItem* item) {
                if (!item) {
                    return;
                }
                const QSignalBlocker blocker(model);
                if (item->row() == 0) {
                    const bool checked = (item->checkState() == Qt::Checked);
                    for (int i = 1; i < model->rowCount(); ++i) {
                        if (auto* planetItem = model->item(i)) {
                            planetItem->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
                        }
                    }
                } else {
                    bool allChecked = true;
                    for (int i = 1; i < model->rowCount(); ++i) {
                        const auto* planetItem = model->item(i);
                        if (!planetItem || planetItem->checkState() != Qt::Checked) {
                            allChecked = false;
                            break;
                        }
                    }
                    if (auto* allItem = model->item(0)) {
                        allItem->setCheckState(allChecked ? Qt::Checked : Qt::Unchecked);
                    }
                }
                updateTransitPlanetComboLabel(searchTransitPlanetCombo_);
            });
            if (auto* view = searchTransitPlanetCombo_->view()) {
                connect(view, &QAbstractItemView::pressed, this, [this, model](const QModelIndex& index) {
                    if (!index.isValid()) {
                        return;
                    }
                    auto* item = model->itemFromIndex(index);
                    if (!item) {
                        return;
                    }
                    const Qt::CheckState nextState =
                        (item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
                    item->setCheckState(nextState);
                });
            }
        }
    }
// Geodetic/Astrocartography UI signals (guarded for optional feature).
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    if (astroModeCombo_) {
        connect(astroModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            updateAstrocartographyModeUi();
            updateAstrocartographyView();
        });
    }
    if (geodeticPlanetCombo_) {
        auto* model = qobject_cast<QStandardItemModel*>(geodeticPlanetCombo_->model());
        if (model && model->rowCount() > 0) {
            updateTransitPlanetComboLabel(geodeticPlanetCombo_);
            connect(model, &QStandardItemModel::itemChanged, this, [this, model](QStandardItem* item) {
                if (!item) {
                    return;
                }
                const QSignalBlocker blocker(model);
                if (item->row() == 0) {
                    const bool checked = (item->checkState() == Qt::Checked);
                    for (int i = 1; i < model->rowCount(); ++i) {
                        if (auto* planetItem = model->item(i)) {
                            planetItem->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
                        }
                    }
                } else {
                    bool allChecked = true;
                    for (int i = 1; i < model->rowCount(); ++i) {
                        const auto* planetItem = model->item(i);
                        if (!planetItem || planetItem->checkState() != Qt::Checked) {
                            allChecked = false;
                            break;
                        }
                    }
                    if (auto* allItem = model->item(0)) {
                        allItem->setCheckState(allChecked ? Qt::Checked : Qt::Unchecked);
                    }
                }
                updateTransitPlanetComboLabel(geodeticPlanetCombo_);
                updateAstrocartographyView();
            });
            if (auto* view = geodeticPlanetCombo_->view()) {
                connect(view, &QAbstractItemView::pressed, this, [this, model](const QModelIndex& index) {
                    if (!index.isValid()) {
                        return;
                    }
                    auto* item = model->itemFromIndex(index);
                    if (!item) {
                        return;
                    }
                    const Qt::CheckState nextState =
                        (item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
                    item->setCheckState(nextState);
                });
            }
        }
    }
    if (geodeticExactRadio_) {
        connect(geodeticExactRadio_, &QRadioButton::toggled, this, [this](bool) {
            updateAstrocartographyModeUi();
            updateAstrocartographyView();
        });
    }
    if (geodeticOrbRadio_) {
        connect(geodeticOrbRadio_, &QRadioButton::toggled, this, [this](bool) {
            updateAstrocartographyModeUi();
            updateAstrocartographyView();
        });
    }
    if (geodeticOrbCombo_) {
        connect(geodeticOrbCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            updateAstrocartographyView();
        });
    }
    if (geodeticRefreshButton_) {
        connect(geodeticRefreshButton_, &QPushButton::clicked, this, &MainWindow::updateAstrocartographyView);
    }
#endif
    if (searchTargetCombo_) {
        connect(searchTargetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (!searchEventCombo_ || !searchHouseCombo_) {
                return;
            }
            const QString eventType = searchEventCombo_->currentText();
            if (!eventType.contains("House", Qt::CaseInsensitive)) {
                return;
            }
            if (index <= 0) {
                searchHouseCombo_->setCurrentIndex(0);
                return;
            }
            const QVariant houseData = searchTargetCombo_->itemData(index, Qt::UserRole);
            if (!houseData.isValid()) {
                return;
            }
            const int house = houseData.toInt();
            if (house <= 0) {
                return;
            }
            const int houseIndex = searchHouseCombo_->findText(QString::number(house));
            if (houseIndex >= 0) {
                searchHouseCombo_->setCurrentIndex(houseIndex);
            }
        });
    }
    if (searchModeRangeRadio_) {
        connect(searchModeRangeRadio_, &QRadioButton::toggled, this, [this](bool) {
            updateTransitSearchVisibility();
        });
    }
    if (searchModeNextRadio_) {
        connect(searchModeNextRadio_, &QRadioButton::toggled, this, [this](bool) {
            updateTransitSearchVisibility();
        });
    }
    if (searchModePrevRadio_) {
        connect(searchModePrevRadio_, &QRadioButton::toggled, this, [this](bool) {
            updateTransitSearchVisibility();
        });
    }
    if (searchEventCombo_) {
        connect(searchEventCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::updateTransitSearchTargets);
    }
    if (calendarYearCombo_) {
        connect(calendarYearCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            handleTransitCalendarRun();
        });
    }
    if (calendarMonthCombo_) {
        connect(calendarMonthCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            showTransitCalendarResults();
        });
    }
    if (calendarPlanetCombo_) {
        auto* model = qobject_cast<QStandardItemModel*>(calendarPlanetCombo_->model());
        if (model && model->rowCount() > 0) {
            updateCheckableComboLabel(calendarPlanetCombo_);
            connect(model, &QStandardItemModel::itemChanged, this, [this, model](QStandardItem* item) {
                if (!item) {
                    return;
                }
                const QSignalBlocker blocker(model);
                if (item->row() == 0) {
                    const bool checked = (item->checkState() == Qt::Checked);
                    for (int i = 1; i < model->rowCount(); ++i) {
                        if (auto* planetItem = model->item(i)) {
                            planetItem->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
                        }
                    }
                } else {
                    bool allChecked = true;
                    for (int i = 1; i < model->rowCount(); ++i) {
                        const auto* planetItem = model->item(i);
                        if (!planetItem || planetItem->checkState() != Qt::Checked) {
                            allChecked = false;
                            break;
                        }
                    }
                    if (auto* allItem = model->item(0)) {
                        allItem->setCheckState(allChecked ? Qt::Checked : Qt::Unchecked);
                    }
                }
                updateCheckableComboLabel(calendarPlanetCombo_);
                showTransitCalendarResults();
                if (calendarRecomputeTimer_) {
                    calendarRecomputeTimer_->start();
                } else if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Calendar) {
                    handleTransitCalendarRun();
                }
            });
            if (auto* view = calendarPlanetCombo_->view()) {
                connect(view, &QAbstractItemView::pressed, this, [model](const QModelIndex& index) {
                    if (!index.isValid()) {
                        return;
                    }
                    auto* item = model->itemFromIndex(index);
                    if (!item) {
                        return;
                    }
                    const Qt::CheckState nextState =
                        (item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
                    item->setCheckState(nextState);
                });
            }
        }
    }
    if (calendarShowIngressCheck_) {
        connect(calendarShowIngressCheck_, &QCheckBox::toggled, this, [this](bool) {
            showTransitCalendarResults();
        });
    }
    if (calendarShowEgressCheck_) {
        connect(calendarShowEgressCheck_, &QCheckBox::toggled, this, [this](bool) {
            showTransitCalendarResults();
        });
    }
    if (calendarShowStationCheck_) {
        connect(calendarShowStationCheck_, &QCheckBox::toggled, this, [this](bool) {
            showTransitCalendarResults();
        });
    }
    if (calendarShowShadowCheck_) {
        connect(calendarShowShadowCheck_, &QCheckBox::toggled, this, [this](bool) {
            showTransitCalendarResults();
        });
    }
    if (calendarIncludeHousesCheck_) {
        connect(calendarIncludeHousesCheck_, &QCheckBox::toggled, this, [this](bool) {
            if (calendarRecomputeTimer_) {
                calendarRecomputeTimer_->start();
            } else {
                handleTransitCalendarRun();
            }
        });
    }
    if (calendarRefreshButton_) {
        connect(calendarRefreshButton_, &QPushButton::clicked, this, &MainWindow::handleTransitCalendarRun);
    }
    if (conjModeRangeRadio_) {
        connect(conjModeRangeRadio_, &QRadioButton::toggled, this, &MainWindow::updateConjunctionModeAvailability);
    }
    if (conjModeNextRadio_) {
        connect(conjModeNextRadio_, &QRadioButton::toggled, this, &MainWindow::updateConjunctionModeAvailability);
    }
    if (conjModePrevRadio_) {
        connect(conjModePrevRadio_, &QRadioButton::toggled, this, &MainWindow::updateConjunctionModeAvailability);
    }
    if (conjUseOrbCheck_) {
        connect(conjUseOrbCheck_, &QCheckBox::toggled, this, [this](bool) {
            updateConjunctionModeAvailability();
        });
    }
    if (conjCountSpin_) {
        connect(conjCountSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            updateConjunctionModeAvailability();
        });
    }
    if (conjRunButton_) {
        connect(conjRunButton_, &QPushButton::clicked, this, &MainWindow::handleTransitConjunctionRun);
    }
    if (conjStopButton_) {
        connect(conjStopButton_, &QPushButton::clicked, this, &MainWindow::handleTransitConjunctionStop);
    }
    if (conjPlanetCombo_) {
        auto* model = qobject_cast<QStandardItemModel*>(conjPlanetCombo_->model());
        if (model && model->rowCount() > 0) {
            updateCheckableComboLabel(conjPlanetCombo_);
            connect(model, &QStandardItemModel::itemChanged, this, [this, model](QStandardItem* item) {
                if (!item) {
                    return;
                }
                const QSignalBlocker blocker(model);
                if (item->row() == 0) {
                    const bool checked = (item->checkState() == Qt::Checked);
                    for (int i = 1; i < model->rowCount(); ++i) {
                        if (auto* planetItem = model->item(i)) {
                            planetItem->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
                        }
                    }
                } else {
                    bool allChecked = true;
                    for (int i = 1; i < model->rowCount(); ++i) {
                        const auto* planetItem = model->item(i);
                        if (!planetItem || planetItem->checkState() != Qt::Checked) {
                            allChecked = false;
                            break;
                        }
                    }
                    if (auto* allItem = model->item(0)) {
                        allItem->setCheckState(allChecked ? Qt::Checked : Qt::Unchecked);
                    }
                }
                updateCheckableComboLabel(conjPlanetCombo_);
                updateConjunctionModeAvailability();
            });
            if (auto* view = conjPlanetCombo_->view()) {
                connect(view, &QAbstractItemView::pressed, this, [model](const QModelIndex& index) {
                    if (!index.isValid()) {
                        return;
                    }
                    auto* item = model->itemFromIndex(index);
                    if (!item) {
                        return;
                    }
                    const Qt::CheckState nextState =
                        (item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
                    item->setCheckState(nextState);
                });
            }
        }
    }
    if (scanModeCombo_) {
        connect(scanModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            const bool combined = (index == static_cast<int>(TransitScanMode::Combined));
            if (scanWeightTransitNatalSpin_) {
                scanWeightTransitNatalSpin_->setEnabled(combined);
            }
            if (scanWeightTransitTransitSpin_) {
                scanWeightTransitTransitSpin_->setEnabled(combined);
            }
            if (scanWeightSolarSpin_) {
                scanWeightSolarSpin_->setEnabled(combined);
            }
            if (scanWeightProgressedSpin_) {
                scanWeightProgressedSpin_->setEnabled(combined);
            }
        });
        const bool combined = (scanModeCombo_->currentIndex() == static_cast<int>(TransitScanMode::Combined));
        if (scanWeightTransitNatalSpin_) {
            scanWeightTransitNatalSpin_->setEnabled(combined);
        }
        if (scanWeightTransitTransitSpin_) {
            scanWeightTransitTransitSpin_->setEnabled(combined);
        }
        if (scanWeightSolarSpin_) {
            scanWeightSolarSpin_->setEnabled(combined);
        }
        if (scanWeightProgressedSpin_) {
            scanWeightProgressedSpin_->setEnabled(combined);
        }
    }
    if (scanSolarBiasCheck_) {
        connect(scanSolarBiasCheck_, &QCheckBox::toggled, this, [this](bool checked) {
            if (scanSolarBiasSpin_) {
                scanSolarBiasSpin_->setEnabled(checked);
            }
        });
        if (scanSolarBiasSpin_) {
            scanSolarBiasSpin_->setEnabled(scanSolarBiasCheck_->isChecked());
        }
    }
    if (scanRunButton_) {
        connect(scanRunButton_, &QPushButton::clicked, this, &MainWindow::handleTransitScanStart);
    }
    if (scanCancelButton_) {
        connect(scanCancelButton_, &QPushButton::clicked, this, &MainWindow::handleTransitScanCancel);
    }
    if (scanSortCombo_) {
        connect(scanSortCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateTransitScanResultsTable);
    }
    if (scanTopCountSpin_) {
        connect(scanTopCountSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::updateTransitScanResultsTable);
    }
    if (profectionUseTransitAgeButton_) {
        connect(profectionUseTransitAgeButton_, &QPushButton::clicked, this, [this]() {
            syncTransitProfectionAgeFromTransitDate();
            if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Profections) {
                refreshTransitProfectionTab();
            }
        });
    }
    if (profectionRunButton_) {
        connect(profectionRunButton_, &QPushButton::clicked, this, &MainWindow::handleTransitProfectionRun);
    }
    if (profectionAgeSpin_) {
        connect(profectionAgeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            if (profectionStatusLabel_) {
                profectionStatusLabel_->setText("Ready");
            }
            if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Profections) {
                refreshTransitProfectionTab();
            }
        });
    }
    if (lunationRunButton_) {
        connect(lunationRunButton_, &QPushButton::clicked, this, &MainWindow::handleLunationSearchRun);
    }
    if (lunationStopButton_) {
        connect(lunationStopButton_, &QPushButton::clicked, this, &MainWindow::handleLunationSearchStop);
    }
    if (lunationModeNextRadio_) {
        connect(lunationModeNextRadio_, &QRadioButton::toggled, this, &MainWindow::updateLunationModeAvailability);
    }
    if (lunationModePrevRadio_) {
        connect(lunationModePrevRadio_, &QRadioButton::toggled, this, &MainWindow::updateLunationModeAvailability);
    }
    if (lunationModeRangeRadio_) {
        connect(lunationModeRangeRadio_, &QRadioButton::toggled, this, &MainWindow::updateLunationModeAvailability);
    }
    if (lunationDegreeRangeCheck_) {
        connect(lunationDegreeRangeCheck_, &QCheckBox::toggled, this, &MainWindow::updateLunationModeAvailability);
    }
    if (lunationDegreeRangeStartSpin_) {
        connect(lunationDegreeRangeStartSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::updateLunationModeAvailability);
    }
    if (lunationDegreeRangeEndSpin_) {
        connect(lunationDegreeRangeEndSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::updateLunationModeAvailability);
    }
    if (lunationAnalysisCombo_) {
        connect(lunationAnalysisCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateLunationAnalysisAvailability);
    }
    if (lunationDegreeExactRadio_) {
        connect(lunationDegreeExactRadio_, &QRadioButton::toggled, this, &MainWindow::updateLunationAnalysisAvailability);
    }
    if (lunationDegreeOrbRadio_) {
        connect(lunationDegreeOrbRadio_, &QRadioButton::toggled, this, &MainWindow::updateLunationAnalysisAvailability);
    }
    if (lunationOrbCombo_) {
        connect(lunationOrbCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateLunationAnalysisAvailability);
    }
    if (lunationMatchCombo_) {
        connect(lunationMatchCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateLunationAnalysisAvailability);
    }
    if (lunationSignModeCombo_) {
        connect(lunationSignModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateLunationAnalysisAvailability);
    }
    if (lunationHouseModeCombo_) {
        connect(lunationHouseModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateLunationAnalysisAvailability);
    }
    if (lunationTargetDegSpin_) {
        connect(lunationTargetDegSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::updateLunationAnalysisAvailability);
    }
    if (lunationTargetMinSpin_) {
        connect(lunationTargetMinSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::updateLunationAnalysisAvailability);
    }
    if (lunationTargetSecSpin_) {
        connect(lunationTargetSecSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::updateLunationAnalysisAvailability);
    }
    if (lunationSignCombo_) {
        auto* model = qobject_cast<QStandardItemModel*>(lunationSignCombo_->model());
        if (model && model->rowCount() > 0) {
            updateTransitPlanetComboLabel(lunationSignCombo_);
            connect(model, &QStandardItemModel::itemChanged, this, [this, model](QStandardItem* item) {
                if (!item) {
                    return;
                }
                const QSignalBlocker blocker(model);
                if (item->row() == 0) {
                    const bool checked = (item->checkState() == Qt::Checked);
                    for (int i = 1; i < model->rowCount(); ++i) {
                        if (auto* signItem = model->item(i)) {
                            signItem->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
                        }
                    }
                } else {
                    bool allChecked = true;
                    for (int i = 1; i < model->rowCount(); ++i) {
                        const auto* signItem = model->item(i);
                        if (!signItem || signItem->checkState() != Qt::Checked) {
                            allChecked = false;
                            break;
                        }
                    }
                    if (auto* allItem = model->item(0)) {
                        allItem->setCheckState(allChecked ? Qt::Checked : Qt::Unchecked);
                    }
                }
                updateTransitPlanetComboLabel(lunationSignCombo_);
                updateLunationAnalysisAvailability();
            });
            if (auto* view = lunationSignCombo_->view()) {
                connect(view, &QAbstractItemView::pressed, this, [this, model](const QModelIndex& index) {
                    if (!index.isValid()) {
                        return;
                    }
                    auto* item = model->itemFromIndex(index);
                    if (!item) {
                        return;
                    }
                    const Qt::CheckState nextState =
                        (item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
                    item->setCheckState(nextState);
                });
            }
        }
    }
    if (lunationHouseCombo_) {
        auto* model = qobject_cast<QStandardItemModel*>(lunationHouseCombo_->model());
        if (model && model->rowCount() > 0) {
            updateTransitPlanetComboLabel(lunationHouseCombo_);
            connect(model, &QStandardItemModel::itemChanged, this, [this, model](QStandardItem* item) {
                if (!item) {
                    return;
                }
                const QSignalBlocker blocker(model);
                if (item->row() == 0) {
                    const bool checked = (item->checkState() == Qt::Checked);
                    for (int i = 1; i < model->rowCount(); ++i) {
                        if (auto* houseItem = model->item(i)) {
                            houseItem->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
                        }
                    }
                } else {
                    bool allChecked = true;
                    for (int i = 1; i < model->rowCount(); ++i) {
                        const auto* houseItem = model->item(i);
                        if (!houseItem || houseItem->checkState() != Qt::Checked) {
                            allChecked = false;
                            break;
                        }
                    }
                    if (auto* allItem = model->item(0)) {
                        allItem->setCheckState(allChecked ? Qt::Checked : Qt::Unchecked);
                    }
                }
                updateTransitPlanetComboLabel(lunationHouseCombo_);
                updateLunationAnalysisAvailability();
            });
            if (auto* view = lunationHouseCombo_->view()) {
                connect(view, &QAbstractItemView::pressed, this, [this, model](const QModelIndex& index) {
                    if (!index.isValid()) {
                        return;
                    }
                    auto* item = model->itemFromIndex(index);
                    if (!item) {
                        return;
                    }
                    const Qt::CheckState nextState =
                        (item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
                    item->setCheckState(nextState);
                });
            }
        }
    }
    if (aspectsCopyButton_) {
        connect(aspectsCopyButton_, &QPushButton::clicked, this, &MainWindow::handleCopyAspects);
    }
    if (reportCopyButton_) {
        connect(reportCopyButton_, &QPushButton::clicked, this, &MainWindow::handleCopyReport);
    }
    if (rightBottomCopyButton_) {
        connect(rightBottomCopyButton_, &QPushButton::clicked, this, [this]() {
            if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Search) {
                handleCopyTransitSearchDetails();
            } else if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Calendar) {
                handleCopyTransitCalendarDetails();
            } else if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Conjunctions) {
                handleCopyTransitConjunctionDetails();
            } else if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Scan) {
                handleCopyTransitScanDetails();
            } else if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Lunations) {
                handleCopyLunationDetails();
            }
        });
    }
    if (aspectsTable_) {
        connect(aspectsTable_, &QTableWidget::cellEntered, this, &MainWindow::updateAspectHover);
    }
    if (rightTopTable_) {
        connect(rightTopTable_, &QTableWidget::cellClicked, this, [this](int row, int column) {
            Q_UNUSED(column);
            if (activeTab_ == AppTab::SolarReturn && isSolarTechniqueTabActive()) {
                if (!rightTopTable_ || !solarTechniqueDateEdit_) {
                    return;
                }
                if (auto* dateItem = rightTopTable_->item(row, 0)) {
                    const QVariant dateValue = dateItem->data(Qt::UserRole);
                    if (dateValue.canConvert<QDate>()) {
                        solarTechniqueDateEdit_->setDate(dateValue.toDate());
                    }
                }
                refreshSolarTechniqueView();
                return;
            }
            if (activeTab_ != AppTab::Transits) {
                return;
            }
            if (transitSubTab_ == TransitSubTab::Search) {
                handleTransitSearchResultActivated(row, column);
            } else if (transitSubTab_ == TransitSubTab::Calendar) {
                handleTransitCalendarResultActivated(row, column);
            } else if (transitSubTab_ == TransitSubTab::Conjunctions) {
                handleTransitConjunctionResultActivated(row, column);
            } else if (transitSubTab_ == TransitSubTab::Scan) {
                handleTransitScanResultActivated(row, column);
            } else if (transitSubTab_ == TransitSubTab::Profections) {
                refreshTransitProfectionTab();
            } else if (transitSubTab_ == TransitSubTab::Lunations) {
                handleLunationResultActivated(row, column);
            }
        });
    }
    if (rightBottomTable_) {
        connect(rightBottomTable_, &QTableWidget::cellClicked, this, [this](int row, int column) {
            Q_UNUSED(column);
            if (activeTab_ != AppTab::Transits || transitSubTab_ != TransitSubTab::Lunations) {
                return;
            }
            if (lunationAnalysisMode_ != LunationAnalysisMode::RepeatedDegrees) {
                return;
            }
            if (row < 0 || row >= lunationBottomEventOrder_.size()) {
                return;
            }
            const int idx = lunationBottomEventOrder_[row];
            if (idx < 0 || idx >= lunationResults_.size()) {
                return;
            }
            applyLunationResult(lunationResults_[idx]);
        });
    }

    updateLunationModeAvailability();
    updateConjunctionModeAvailability();
    updateLunationCopyButtonState();
}

void MainWindow::resetDockLayout() {
    if (!defaultDockState_.isEmpty()) {
        restoreState(defaultDockState_);
        setLayoutLocked(layoutLocked_);
    }
}

void MainWindow::setLayoutLocked(bool locked) {
    layoutLocked_ = locked;
    const auto features = locked
        ? QDockWidget::NoDockWidgetFeatures
        : (QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

    if (dataDock_) {
        dataDock_->setFeatures(features);
    }
    if (rightTopDock_) {
        rightTopDock_->setFeatures(features);
    }
    if (rightBottomDock_) {
        rightBottomDock_->setFeatures(features);
    }

    if (lockLayoutAction_ && lockLayoutAction_->isChecked() != locked) {
        lockLayoutAction_->setChecked(locked);
    }
}

void MainWindow::setStatusMessage(const QString& text) {
    const QString message = text.trimmed();
    if (message.isEmpty()) {
        return;
    }
    if (statusBar()) {
        statusBar()->showMessage(message, 10000);
    }
}

void MainWindow::setCriticalMessage(const QString& text) {
    const QString message = text.trimmed();
    if (message.isEmpty()) {
        return;
    }
    if (statusBar()) {
        statusBar()->showMessage(message, 15000);
    }
    QMessageBox::critical(this, "DracoVed", message);
}


void MainWindow::loadUiState() {
    QSettings settings;
    const QByteArray dockState = settings.value("ui/dock_state").toByteArray();
    if (!dockState.isEmpty() && !restoreState(dockState)) {
        if (!defaultDockState_.isEmpty()) {
            restoreState(defaultDockState_);
        }
    } else if (dockState.isEmpty() && !defaultDockState_.isEmpty()) {
        restoreState(defaultDockState_);
    }

    layoutLocked_ = settings.value("ui/layout_locked", false).toBool();
    setLayoutLocked(layoutLocked_);

    if (leftSplitter_) {
        const QByteArray splitState = settings.value("ui/left_splitter").toByteArray();
        if (!splitState.isEmpty()) {
            leftSplitter_->restoreState(splitState);
        } else {
            leftSplitter_->setSizes({520, 280});
        }
    }

    overlayAspectsTransitNatal_ = settings.value("chart/overlay_aspects/transit_natal", true).toBool();
    overlayAspectsTransitTransit_ = settings.value("chart/overlay_aspects/transit_transit", false).toBool();
    overlayAspectsNatalNatal_ = settings.value("chart/overlay_aspects/natal_natal", false).toBool();
    aspectDisplayMaxOrb_ = settings.value("chart/overlay_aspects/max_orb", 0.0).toDouble();
    showAsteroids_ = settings.value("chart/show_asteroids", false).toBool();
    includeAsteroidAspects_ = settings.value("chart/include_asteroid_aspects", false).toBool();
    showLots_ = settings.value("chart/show_lots", true).toBool();
    showDerivedPoints_ = settings.value("chart/show_derived_points", true).toBool();
    showFixedStars_ = settings.value("chart/show_fixed_stars", false).toBool();
    if (settings.contains("chart/visible_asteroids")) {
        visibleAsteroids_ = settings.value("chart/visible_asteroids").toStringList();
    } else {
        visibleAsteroids_ = asteroidBodyOrder();
    }
    if (settings.contains("chart/visible_fixed_stars")) {
        visibleFixedStars_ = settings.value("chart/visible_fixed_stars").toStringList();
    } else {
        visibleFixedStars_ = defaultFixedStars();
    }
    QStringList cleanedAsteroids;
    for (const auto& name : visibleAsteroids_) {
        if (isAsteroidBody(name) && !cleanedAsteroids.contains(name)) {
            cleanedAsteroids.push_back(name);
        }
    }
    visibleAsteroids_ = cleanedAsteroids;

    QStringList cleanedFixedStars;
    const QStringList fixedCatalog = fixedStarCatalog();
    for (const auto& name : visibleFixedStars_) {
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        QString canonical;
        for (const auto& catalogName : fixedCatalog) {
            if (catalogName.compare(trimmed, Qt::CaseInsensitive) == 0) {
                canonical = catalogName;
                break;
            }
        }
        if (canonical.isEmpty()) {
            continue;
        }
        if (!cleanedFixedStars.contains(canonical)) {
            cleanedFixedStars.push_back(canonical);
        }
    }
    visibleFixedStars_ = cleanedFixedStars;
    if (!overlayAspectsTransitNatal_ && !overlayAspectsTransitTransit_ && !overlayAspectsNatalNatal_) {
        overlayAspectsTransitNatal_ = true;
    }

    int readabilityPresetValue = settings.value("chart/readability_preset",
        static_cast<int>(ChartReadabilityPreset::Clean)).toInt();
    if (readabilityPresetValue < 0 || readabilityPresetValue > static_cast<int>(ChartReadabilityPreset::Custom)) {
        readabilityPresetValue = static_cast<int>(ChartReadabilityPreset::Clean);
    }
    chartReadabilityPreset_ = static_cast<ChartReadabilityPreset>(readabilityPresetValue);

    if (chartWheel_) {
        chartWheel_->setZoom(settings.value("chart/zoom", 1.0).toDouble());
        chartWheel_->setShowAspects(settings.value("chart/show_aspects", true).toBool());
        chartWheel_->setShowTicks(settings.value("chart/show_ticks", true).toBool());
        chartWheel_->setShowDegrees(settings.value("chart/show_degrees", true).toBool());
        chartWheel_->setShowAspectSymbols(settings.value("chart/show_aspect_symbols", true).toBool());
        int tickDensityValue = settings.value("chart/tick_density",
            static_cast<int>(ChartWheelWidget::TickDensity::Full)).toInt();
        if (tickDensityValue < 0 || tickDensityValue > static_cast<int>(ChartWheelWidget::TickDensity::Minimal)) {
            tickDensityValue = static_cast<int>(ChartWheelWidget::TickDensity::Full);
        }
        chartWheel_->setTickDensity(static_cast<ChartWheelWidget::TickDensity>(tickDensityValue));
        chartWheel_->setFontScale(settings.value("chart/font_scale", 1.0).toDouble());
        chartWheel_->setShowAsteroids(showAsteroids_);
        chartWheel_->setIncludeAsteroidAspects(includeAsteroidAspects_);
        chartWheel_->setVisibleAsteroids(visibleAsteroids_);
        chartWheel_->setShowLots(showLots_);
        chartWheel_->setShowDerivedPoints(showDerivedPoints_);
        chartWheel_->setShowFixedStars(showFixedStars_);
        chartWheel_->setVisibleFixedStars(visibleFixedStars_);
        chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        if (chartReadabilityPreset_ != ChartReadabilityPreset::Custom) {
            applyChartReadabilityPreset(chartReadabilityPreset_);
        }
    }
    const auto defaults = defaultAspectOrbs();
    aspectOrbs_.conjunction = settings.value("chart/orbs/conjunction", defaults.conjunction).toDouble();
    aspectOrbs_.sextile = settings.value("chart/orbs/sextile", defaults.sextile).toDouble();
    aspectOrbs_.square = settings.value("chart/orbs/square", defaults.square).toDouble();
    aspectOrbs_.trine = settings.value("chart/orbs/trine", defaults.trine).toDouble();
    aspectOrbs_.opposition = settings.value("chart/orbs/opposition", defaults.opposition).toDouble();

    const int tabIndex = settings.value("ui/main_tab", 0).toInt();
    if (mainTabBar_) {
        const int maxIndex = std::max(0, mainTabBar_->count() - 1);
        const int clamped = std::clamp(tabIndex, 0, maxIndex);
        mainTabBar_->setCurrentIndex(clamped);
    }
    const int mode = settings.value("ui/transit_mode", 0).toInt();
    transitMode_ = (mode == 1) ? TransitMode::TransitOnly : TransitMode::NatalOverlay;
    const int transitHouse = settings.value("ui/transit_house_system", 0).toInt();
    transitHouseSystem_ = (transitHouse == 1) ? HouseSystem::Placidus : HouseSystem::WholeSign;
    const int aspectView = settings.value("ui/transit_aspect_view", 0).toInt();
    if (aspectView >= 0 && aspectView <= 2) {
        transitAspectView_ = static_cast<TransitAspectView>(aspectView);
    }
    if (transitOverlayRadio_ && transitOnlyRadio_) {
        transitOverlayRadio_->setChecked(transitMode_ == TransitMode::NatalOverlay);
        transitOnlyRadio_->setChecked(transitMode_ == TransitMode::TransitOnly);
    }
    if (transitWholeRadio_ && transitPlacidusRadio_) {
        transitWholeRadio_->setChecked(transitHouseSystem_ == HouseSystem::WholeSign);
        transitPlacidusRadio_->setChecked(transitHouseSystem_ == HouseSystem::Placidus);
    }
    if (aspectScopeTabs_) {
        aspectScopeTabs_->setCurrentIndex(static_cast<int>(transitAspectView_));
    }
    const int progressionView = settings.value("ui/progression_view", static_cast<int>(ProgressionView::ProgressedOnly)).toInt();
    if (progressionView >= 0 && progressionView <= 2) {
        progressionView_ = static_cast<ProgressionView>(progressionView);
    }
    if (progressionViewNatalRadio_ && progressionViewProgressedRadio_ && progressionViewOverlayRadio_) {
        progressionViewNatalRadio_->setChecked(progressionView_ == ProgressionView::NatalOnly);
        progressionViewProgressedRadio_->setChecked(progressionView_ == ProgressionView::ProgressedOnly);
        progressionViewOverlayRadio_->setChecked(progressionView_ == ProgressionView::Overlay);
    }
    if (progressionDateEdit_) {
        const QDate defaultDate = progressionDateEdit_->date().isValid() ? progressionDateEdit_->date() : QDate::currentDate();
        progressionDateEdit_->setDate(settings.value("progression/target_date", defaultDate).toDate());
    }
    if (progressionTimeEdit_) {
        const QTime defaultTime = progressionTimeEdit_->time().isValid() ? progressionTimeEdit_->time() : QTime::currentTime();
        progressionTimeEdit_->setTime(settings.value("progression/target_time", defaultTime).toTime());
    }
    if (progressionTimezoneEdit_) {
        progressionTimezoneEdit_->setText(settings.value("progression/timezone", progressionTimezoneEdit_->text()).toString());
    }
    updateProgressionTimezoneStatus();
    markProgressionPending();
    if (solarYearSpin_) {
        solarYearSpin_->setValue(settings.value("solar/year", QDate::currentDate().year()).toInt());
    }
    const bool useNatalLocation = settings.value("solar/use_natal_location", true).toBool();
    if (solarUseNatalRadio_ && solarUseCustomRadio_) {
        solarUseNatalRadio_->setChecked(useNatalLocation);
        solarUseCustomRadio_->setChecked(!useNatalLocation);
    }
    if (solarLocationEdit_) {
        solarLocationEdit_->setText(settings.value("solar/location", solarLocationEdit_->text()).toString());
    }
    if (solarLatSpin_) {
        solarLatSpin_->setValue(settings.value("solar/lat", solarLatSpin_->value()).toDouble());
    }
    if (solarLonSpin_) {
        solarLonSpin_->setValue(settings.value("solar/lon", solarLonSpin_->value()).toDouble());
    }
    if (solarTimezoneEdit_) {
        solarTimezoneEdit_->setText(settings.value("solar/timezone", solarTimezoneEdit_->text()).toString());
    }
    const int solarView = settings.value("solar/aspect_view", 0).toInt();
    if (solarView >= 0 && solarView <= 1) {
        solarAspectView_ = static_cast<SolarAspectView>(solarView);
    }
    updateSolarLocationAvailability();
    updateSolarTimezoneStatus();
    markSolarPending();
    const int relocationHouse = settings.value("relocation/house_system", 0).toInt();
    relocationHouseSystem_ = (relocationHouse == 1) ? HouseSystem::Placidus : HouseSystem::WholeSign;
    if (relocationWholeRadio_ && relocationPlacidusRadio_) {
        relocationWholeRadio_->setChecked(relocationHouseSystem_ == HouseSystem::WholeSign);
        relocationPlacidusRadio_->setChecked(relocationHouseSystem_ == HouseSystem::Placidus);
    }
    if (relocationLocationEdit_) {
        relocationLocationEdit_->setText(settings.value("relocation/location", relocationLocationEdit_->text()).toString());
    }
    if (relocationLatSpin_) {
        relocationLatSpin_->setValue(settings.value("relocation/lat", relocationLatSpin_->value()).toDouble());
    }
    if (relocationLonSpin_) {
        relocationLonSpin_->setValue(settings.value("relocation/lon", relocationLonSpin_->value()).toDouble());
    }
    if (relocationTimezoneEdit_) {
        relocationTimezoneEdit_->setText(settings.value("relocation/timezone", relocationTimezoneEdit_->text()).toString());
    }
    const bool relocationOverlay = settings.value("relocation/overlay", false).toBool();
    if (relocationOverlayCheck_) {
        relocationOverlayCheck_->setChecked(relocationOverlay);
    }
    const int relocationView = settings.value("relocation/aspect_view", 0).toInt();
    if (relocationView >= 0 && relocationView <= 1) {
        relocationAspectView_ = static_cast<RelocationAspectView>(relocationView);
    }
    updateRelocationTimezoneStatus();
    markRelocationPending();
    updateAspectScopeTabs();
    updateChartLegend();
    updateTransitSearchTargets();
    updateTransitSearchVisibility();
}

void MainWindow::saveUiState() {
    QSettings settings;
    settings.setValue("ui/dock_state", saveState());
    settings.setValue("ui/layout_locked", layoutLocked_);
    if (leftSplitter_) {
        settings.setValue("ui/left_splitter", leftSplitter_->saveState());
    }
    if (mainTabBar_) {
        settings.setValue("ui/main_tab", mainTabBar_->currentIndex());
    }
    settings.setValue("ui/transit_mode", transitMode_ == TransitMode::TransitOnly ? 1 : 0);
    settings.setValue("ui/transit_house_system", transitHouseSystem_ == HouseSystem::Placidus ? 1 : 0);
    settings.setValue("ui/transit_aspect_view", static_cast<int>(transitAspectView_));
    settings.setValue("ui/progression_view", static_cast<int>(progressionView_));
    if (chartWheel_) {
        settings.setValue("chart/zoom", chartWheel_->zoom());
        settings.setValue("chart/show_aspects", chartWheel_->showAspects());
        settings.setValue("chart/show_ticks", chartWheel_->showTicks());
        settings.setValue("chart/show_degrees", chartWheel_->showDegrees());
        settings.setValue("chart/show_aspect_symbols", chartWheel_->showAspectSymbols());
        settings.setValue("chart/show_asteroids", chartWheel_->showAsteroids());
        settings.setValue("chart/include_asteroid_aspects", chartWheel_->includeAsteroidAspects());
        settings.setValue("chart/visible_asteroids", chartWheel_->visibleAsteroids());
        settings.setValue("chart/show_lots", chartWheel_->showLots());
        settings.setValue("chart/show_derived_points", chartWheel_->showDerivedPoints());
        settings.setValue("chart/show_fixed_stars", chartWheel_->showFixedStars());
        settings.setValue("chart/visible_fixed_stars", chartWheel_->visibleFixedStars());
        settings.setValue("chart/tick_density", static_cast<int>(chartWheel_->tickDensity()));
        settings.setValue("chart/font_scale", chartWheel_->fontScale());
    }
    settings.setValue("chart/readability_preset", static_cast<int>(chartReadabilityPreset_));
    settings.setValue("chart/overlay_aspects/transit_natal", overlayAspectsTransitNatal_);
    settings.setValue("chart/overlay_aspects/transit_transit", overlayAspectsTransitTransit_);
    settings.setValue("chart/overlay_aspects/natal_natal", overlayAspectsNatalNatal_);
    settings.setValue("chart/overlay_aspects/max_orb", aspectDisplayMaxOrb_);
    settings.setValue("chart/orbs/conjunction", aspectOrbs_.conjunction);
    settings.setValue("chart/orbs/sextile", aspectOrbs_.sextile);
    settings.setValue("chart/orbs/square", aspectOrbs_.square);
    settings.setValue("chart/orbs/trine", aspectOrbs_.trine);
    settings.setValue("chart/orbs/opposition", aspectOrbs_.opposition);
    if (progressionDateEdit_) {
        settings.setValue("progression/target_date", progressionDateEdit_->date());
    }
    if (progressionTimeEdit_) {
        settings.setValue("progression/target_time", progressionTimeEdit_->time());
    }
    if (progressionTimezoneEdit_) {
        settings.setValue("progression/timezone", progressionTimezoneEdit_->text());
    }
    if (solarYearSpin_) {
        settings.setValue("solar/year", solarYearSpin_->value());
    }
    if (solarUseNatalRadio_) {
        settings.setValue("solar/use_natal_location", solarUseNatalRadio_->isChecked());
    }
    if (solarLocationEdit_) {
        settings.setValue("solar/location", solarLocationEdit_->text());
    }
    if (solarLatSpin_) {
        settings.setValue("solar/lat", solarLatSpin_->value());
    }
    if (solarLonSpin_) {
        settings.setValue("solar/lon", solarLonSpin_->value());
    }
    if (solarTimezoneEdit_) {
        settings.setValue("solar/timezone", solarTimezoneEdit_->text());
    }
    settings.setValue("solar/aspect_view", static_cast<int>(solarAspectView_));
    settings.setValue("relocation/house_system", relocationHouseSystem_ == HouseSystem::Placidus ? 1 : 0);
    if (relocationLocationEdit_) {
        settings.setValue("relocation/location", relocationLocationEdit_->text());
    }
    if (relocationLatSpin_) {
        settings.setValue("relocation/lat", relocationLatSpin_->value());
    }
    if (relocationLonSpin_) {
        settings.setValue("relocation/lon", relocationLonSpin_->value());
    }
    if (relocationTimezoneEdit_) {
        settings.setValue("relocation/timezone", relocationTimezoneEdit_->text());
    }
    if (relocationOverlayCheck_) {
        settings.setValue("relocation/overlay", relocationOverlayCheck_->isChecked());
    }
    settings.setValue("relocation/aspect_view", static_cast<int>(relocationAspectView_));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveUiState();

    requestWorkerCancel(searchWorker_);
    requestWorkerCancel(calendarWorker_);
    requestWorkerCancel(conjWorker_);
    requestWorkerCancel(lunationWorker_);
    requestWorkerCancel(scanWorker_);

    bool allStopped = true;
    auto waitForThread = [this, &allStopped](QThread* thread, const QString& label) {
        if (!thread || !thread->isRunning()) {
            return;
        }
        thread->quit();
        if (!thread->wait(4000)) {
            allStopped = false;
            if (statusBar()) {
                statusBar()->showMessage(QString("Waiting for %1 to stop...").arg(label), 5000);
            }
        }
    };
    waitForThread(searchThread_, "transit search");
    waitForThread(calendarThread_, "transit calendar");
    waitForThread(conjThread_, "conjunction finder");
    waitForThread(lunationThread_, "lunation search");
    waitForThread(scanThread_, "transit scan");

    if (!allStopped) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (aspectsTable_ && obj == aspectsTable_->viewport()) {
        if (event->type() == QEvent::Leave) {
            clearAspectHover();
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

bool MainWindow::isAsteroidVisible(const QString& name) const {
    if (!isAsteroidBody(name)) {
        return true;
    }
    return visibleAsteroids_.contains(name);
}

bool MainWindow::isFixedStarVisible(const QString& name) const {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    for (const auto& visible : visibleFixedStars_) {
        if (visible.compare(trimmed, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void MainWindow::showAsteroidSelectionDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Select Asteroids");
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* label = new QLabel("Select asteroid bodies to show:", &dialog);
    layout->addWidget(label);

    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const auto& name : asteroidBodyOrder()) {
        auto* item = new QListWidgetItem(name, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(visibleAsteroids_.contains(name) ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (auto* applyButton = buttons->button(QDialogButtonBox::Ok)) {
        applyButton->setText("Apply");
    }
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QStringList selected;
    for (int i = 0; i < list->count(); ++i) {
        auto* item = list->item(i);
        if (item && item->checkState() == Qt::Checked) {
            selected.push_back(item->text());
        }
    }
    visibleAsteroids_ = selected;
    if (chartWheel_) {
        chartWheel_->setVisibleAsteroids(visibleAsteroids_);
    }

    if (!includeAsteroidAspects_) {
        return;
    }
    if (activeTab_ == AppTab::Transits) {
        refreshTransitsTab();
    } else if (activeTab_ == AppTab::Progression) {
        refreshProgressionView();
    } else if (activeTab_ == AppTab::SolarReturn) {
        refreshSolarReturnView();
    } else if (activeTab_ == AppTab::Relocation) {
        refreshRelocationView();
    } else if (hasCurrentChart_) {
        populateAspects(currentChart_);
    }
}

void MainWindow::applyChartReadabilityPreset(ChartReadabilityPreset preset) {
    if (!chartWheel_) {
        return;
    }
    chartReadabilityPreset_ = preset;
    if (preset == ChartReadabilityPreset::Custom) {
        return;
    }

    if (preset == ChartReadabilityPreset::Clean) {
        chartWheel_->setShowAspects(true);
        chartWheel_->setShowTicks(true);
        chartWheel_->setTickDensity(ChartWheelWidget::TickDensity::Minimal);
        chartWheel_->setShowDegrees(false);
        chartWheel_->setShowAspectSymbols(false);
        aspectDisplayMaxOrb_ = 2.0;
        overlayAspectsTransitNatal_ = true;
        overlayAspectsTransitTransit_ = false;
        overlayAspectsNatalNatal_ = false;
    } else if (preset == ChartReadabilityPreset::Standard) {
        chartWheel_->setShowAspects(true);
        chartWheel_->setShowTicks(true);
        chartWheel_->setTickDensity(ChartWheelWidget::TickDensity::Medium);
        chartWheel_->setShowDegrees(true);
        chartWheel_->setShowAspectSymbols(false);
        aspectDisplayMaxOrb_ = 4.0;
        overlayAspectsTransitNatal_ = true;
        overlayAspectsTransitTransit_ = false;
        overlayAspectsNatalNatal_ = false;
    } else if (preset == ChartReadabilityPreset::Technical) {
        chartWheel_->setShowAspects(true);
        chartWheel_->setShowTicks(true);
        chartWheel_->setTickDensity(ChartWheelWidget::TickDensity::Full);
        chartWheel_->setShowDegrees(true);
        chartWheel_->setShowAspectSymbols(true);
        aspectDisplayMaxOrb_ = 0.0;
        overlayAspectsTransitNatal_ = true;
        overlayAspectsTransitTransit_ = true;
        overlayAspectsNatalNatal_ = true;
    }

    chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
    chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
}

void MainWindow::markChartReadabilityCustom() {
    chartReadabilityPreset_ = ChartReadabilityPreset::Custom;
}

void MainWindow::showChartSettingsMenu() {
    if (!chartWheel_) {
        return;
    }
    QMenu menu(this);

    QMenu* readabilityMenu = menu.addMenu("Readability preset");
    QAction* presetClean = readabilityMenu->addAction("Clean (recommended)");
    QAction* presetStandard = readabilityMenu->addAction("Standard");
    QAction* presetTechnical = readabilityMenu->addAction("Technical");
    QAction* presetCustom = readabilityMenu->addAction("Custom");
    const QList<QAction*> presetActions = {presetClean, presetStandard, presetTechnical, presetCustom};
    for (auto* act : presetActions) {
        act->setCheckable(true);
    }
    presetCustom->setEnabled(false);
    if (chartReadabilityPreset_ == ChartReadabilityPreset::Clean) {
        presetClean->setChecked(true);
    } else if (chartReadabilityPreset_ == ChartReadabilityPreset::Standard) {
        presetStandard->setChecked(true);
    } else if (chartReadabilityPreset_ == ChartReadabilityPreset::Technical) {
        presetTechnical->setChecked(true);
    } else {
        presetCustom->setChecked(true);
    }
    menu.addSeparator();

    QMenu* houseMenu = menu.addMenu("House system");
    QAction* houseWhole = houseMenu->addAction("Whole Sign");
    QAction* housePlacidus = houseMenu->addAction("Placidus");
    houseWhole->setCheckable(true);
    housePlacidus->setCheckable(true);
    const auto system = hasCurrentChart_ ? currentInput_.houseSystem : defaultHouseSystem_;
    houseWhole->setChecked(system == HouseSystem::WholeSign);
    housePlacidus->setChecked(system == HouseSystem::Placidus);
    menu.addSeparator();

    QAction* toggleAspects = menu.addAction("Show aspect lines");
    toggleAspects->setCheckable(true);
    toggleAspects->setChecked(chartWheel_->showAspects());
    if (activeTab_ == AppTab::Transits && transitMode_ == TransitMode::NatalOverlay) {
        toggleAspects->setEnabled(false);
    }

    QMenu* overlayMenu = menu.addMenu("Overlay aspect scope");
    QAction* overlayTransitNatal = overlayMenu->addAction("Transit-Natal");
    QAction* overlayTransitTransit = overlayMenu->addAction("Transit-Transit");
    QAction* overlayNatalNatal = overlayMenu->addAction("Natal-Natal");
    overlayTransitNatal->setCheckable(true);
    overlayTransitTransit->setCheckable(true);
    overlayNatalNatal->setCheckable(true);
    overlayTransitNatal->setChecked(overlayAspectsTransitNatal_);
    overlayTransitTransit->setChecked(overlayAspectsTransitTransit_);
    overlayNatalNatal->setChecked(overlayAspectsNatalNatal_);
    if (!(activeTab_ == AppTab::Transits && transitMode_ == TransitMode::NatalOverlay)) {
        overlayMenu->setEnabled(false);
    }

    QAction* toggleTicks = menu.addAction("Show degree ticks");
    toggleTicks->setCheckable(true);
    toggleTicks->setChecked(chartWheel_->showTicks());

    QMenu* tickDensityMenu = menu.addMenu("Tick density");
    QAction* tickMinimal = tickDensityMenu->addAction("Minimal");
    QAction* tickMedium = tickDensityMenu->addAction("Medium");
    QAction* tickFull = tickDensityMenu->addAction("Full");
    const QList<QAction*> tickDensityActions = {tickMinimal, tickMedium, tickFull};
    for (auto* act : tickDensityActions) {
        act->setCheckable(true);
    }
    if (chartWheel_->tickDensity() == ChartWheelWidget::TickDensity::Minimal) {
        tickMinimal->setChecked(true);
    } else if (chartWheel_->tickDensity() == ChartWheelWidget::TickDensity::Medium) {
        tickMedium->setChecked(true);
    } else {
        tickFull->setChecked(true);
    }
    tickDensityMenu->setEnabled(chartWheel_->showTicks());

    QAction* toggleDegrees = menu.addAction("Show degree labels");
    toggleDegrees->setCheckable(true);
    toggleDegrees->setChecked(chartWheel_->showDegrees());

    QAction* toggleAspectSymbols = menu.addAction("Show aspect symbols");
    toggleAspectSymbols->setCheckable(true);
    toggleAspectSymbols->setChecked(chartWheel_->showAspectSymbols());

    QAction* toggleAsteroids = menu.addAction("Show asteroids");
    toggleAsteroids->setCheckable(true);
    toggleAsteroids->setChecked(chartWheel_->showAsteroids());
    QAction* selectAsteroids = menu.addAction("Select visible asteroids...");

    QAction* toggleLots = menu.addAction("Show Arabic lots");
    toggleLots->setCheckable(true);
    toggleLots->setChecked(chartWheel_->showLots());

    QAction* toggleDerivedPoints = menu.addAction("Show derived points");
    toggleDerivedPoints->setCheckable(true);
    toggleDerivedPoints->setChecked(chartWheel_->showDerivedPoints());

    QAction* toggleFixedStars = menu.addAction("Show fixed stars");
    toggleFixedStars->setCheckable(true);
    toggleFixedStars->setChecked(chartWheel_->showFixedStars());
    QAction* selectFixedStars = menu.addAction("Select visible fixed stars...");

    QAction* toggleAsteroidAspects = menu.addAction("Include asteroid aspects");
    toggleAsteroidAspects->setCheckable(true);
    toggleAsteroidAspects->setChecked(chartWheel_->includeAsteroidAspects());

    QMenu* orbMenu = menu.addMenu("Aspect display orb");
    QAction* orbAll = orbMenu->addAction("All (no filter)");
    QAction* orb6 = orbMenu->addAction("<= 6 degrees");
    QAction* orb4 = orbMenu->addAction("<= 4 degrees");
    QAction* orb3 = orbMenu->addAction("<= 3 degrees");
    QAction* orb2 = orbMenu->addAction("<= 2 degrees");
    QAction* orb1 = orbMenu->addAction("<= 1 degree");
    const QList<QAction*> orbActions = {orbAll, orb6, orb4, orb3, orb2, orb1};
    for (auto* act : orbActions) {
        act->setCheckable(true);
    }
    if (aspectDisplayMaxOrb_ <= 0.0) {
        orbAll->setChecked(true);
    } else if (aspectDisplayMaxOrb_ <= 1.01) {
        orb1->setChecked(true);
    } else if (aspectDisplayMaxOrb_ <= 2.01) {
        orb2->setChecked(true);
    } else if (aspectDisplayMaxOrb_ <= 3.01) {
        orb3->setChecked(true);
    } else if (aspectDisplayMaxOrb_ <= 4.01) {
        orb4->setChecked(true);
    } else {
        orb6->setChecked(true);
    }

    menu.addSeparator();

    QMenu* fontMenu = menu.addMenu("Label size");
    QAction* fontSmall = fontMenu->addAction("Small");
    QAction* fontMed = fontMenu->addAction("Normal");
    QAction* fontLarge = fontMenu->addAction("Large");
    fontSmall->setCheckable(true);
    fontMed->setCheckable(true);
    fontLarge->setCheckable(true);
    if (chartWheel_->fontScale() <= 0.9) {
        fontSmall->setChecked(true);
    } else if (chartWheel_->fontScale() >= 1.2) {
        fontLarge->setChecked(true);
    } else {
        fontMed->setChecked(true);
    }

    QAction* action = menu.exec(chartSettingsButton_->mapToGlobal(QPoint(0, chartSettingsButton_->height())));
    if (!action) {
        return;
    }
    if (action == presetClean) {
        applyChartReadabilityPreset(ChartReadabilityPreset::Clean);
    } else if (action == presetStandard) {
        applyChartReadabilityPreset(ChartReadabilityPreset::Standard);
    } else if (action == presetTechnical) {
        applyChartReadabilityPreset(ChartReadabilityPreset::Technical);
    } else if (action == houseWhole || action == housePlacidus) {
        const auto selected = (action == housePlacidus) ? HouseSystem::Placidus : HouseSystem::WholeSign;
        defaultHouseSystem_ = selected;
        if (hasCurrentChart_) {
            auto input = currentInput_;
            input.houseSystem = selected;
            computeChart(input, currentLocation_);
        }
    } else if (action == toggleAspects) {
        markChartReadabilityCustom();
        chartWheel_->setShowAspects(toggleAspects->isChecked());
    } else if (action == overlayTransitNatal || action == overlayTransitTransit || action == overlayNatalNatal) {
        markChartReadabilityCustom();
        overlayAspectsTransitNatal_ = overlayTransitNatal->isChecked();
        overlayAspectsTransitTransit_ = overlayTransitTransit->isChecked();
        overlayAspectsNatalNatal_ = overlayNatalNatal->isChecked();
        if (!overlayAspectsTransitNatal_ && !overlayAspectsTransitTransit_ && !overlayAspectsNatalNatal_) {
            overlayAspectsTransitNatal_ = true;
            overlayTransitNatal->setChecked(true);
        }
        chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
        if (activeTab_ == AppTab::Transits && transitMode_ == TransitMode::NatalOverlay) {
            chartWheel_->update();
        }
    } else if (action == toggleTicks) {
        markChartReadabilityCustom();
        chartWheel_->setShowTicks(toggleTicks->isChecked());
    } else if (action == tickMinimal || action == tickMedium || action == tickFull) {
        markChartReadabilityCustom();
        if (action == tickMinimal) {
            chartWheel_->setTickDensity(ChartWheelWidget::TickDensity::Minimal);
        } else if (action == tickMedium) {
            chartWheel_->setTickDensity(ChartWheelWidget::TickDensity::Medium);
        } else {
            chartWheel_->setTickDensity(ChartWheelWidget::TickDensity::Full);
        }
    } else if (action == toggleDegrees) {
        markChartReadabilityCustom();
        chartWheel_->setShowDegrees(toggleDegrees->isChecked());
    } else if (action == toggleAspectSymbols) {
        markChartReadabilityCustom();
        chartWheel_->setShowAspectSymbols(toggleAspectSymbols->isChecked());
    } else if (action == toggleAsteroids) {
        markChartReadabilityCustom();
        showAsteroids_ = toggleAsteroids->isChecked();
        chartWheel_->setShowAsteroids(showAsteroids_);
        chartWheel_->update();
    } else if (action == selectAsteroids) {
        markChartReadabilityCustom();
        showAsteroidSelectionDialog();
    } else if (action == toggleLots) {
        markChartReadabilityCustom();
        showLots_ = toggleLots->isChecked();
        chartWheel_->setShowLots(showLots_);
        chartWheel_->update();
    } else if (action == toggleDerivedPoints) {
        markChartReadabilityCustom();
        showDerivedPoints_ = toggleDerivedPoints->isChecked();
        chartWheel_->setShowDerivedPoints(showDerivedPoints_);
        chartWheel_->update();
    } else if (action == toggleFixedStars) {
        markChartReadabilityCustom();
        showFixedStars_ = toggleFixedStars->isChecked();
        chartWheel_->setShowFixedStars(showFixedStars_);
        chartWheel_->update();
    } else if (action == selectFixedStars) {
        markChartReadabilityCustom();
        showFixedStarSelectionDialog();
    } else if (action == toggleAsteroidAspects) {
        markChartReadabilityCustom();
        includeAsteroidAspects_ = toggleAsteroidAspects->isChecked();
        chartWheel_->setIncludeAsteroidAspects(includeAsteroidAspects_);
        if (activeTab_ == AppTab::Transits) {
            refreshTransitsTab();
        } else if (activeTab_ == AppTab::Progression) {
            refreshProgressionView();
        } else if (activeTab_ == AppTab::SolarReturn) {
            refreshSolarReturnView();
        } else if (activeTab_ == AppTab::Relocation) {
            refreshRelocationView();
        } else {
            if (hasCurrentChart_) {
                populateAspects(currentChart_);
            }
        }
    } else if (orbActions.contains(action)) {
        markChartReadabilityCustom();
        if (action == orbAll) {
            aspectDisplayMaxOrb_ = 0.0;
        } else if (action == orb1) {
            aspectDisplayMaxOrb_ = 1.0;
        } else if (action == orb2) {
            aspectDisplayMaxOrb_ = 2.0;
        } else if (action == orb3) {
            aspectDisplayMaxOrb_ = 3.0;
        } else if (action == orb4) {
            aspectDisplayMaxOrb_ = 4.0;
        } else if (action == orb6) {
            aspectDisplayMaxOrb_ = 6.0;
        }
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
    } else if (action == fontSmall) {
        markChartReadabilityCustom();
        chartWheel_->setFontScale(0.85);
    } else if (action == fontMed) {
        markChartReadabilityCustom();
        chartWheel_->setFontScale(1.0);
    } else if (action == fontLarge) {
        markChartReadabilityCustom();
        chartWheel_->setFontScale(1.2);
    }
}

QStringList MainWindow::sweSearchPaths() const {
    QStringList paths;
    const QString envPath = qEnvironmentVariable("DRACOVED_SWE_DLL");
    if (!envPath.isEmpty()) {
        paths << envPath;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    paths << appDir;
    paths << appDir + "/..";
    paths << appDir + "/../..";
    paths << appDir + "/../lib";
    paths << appDir + "/../../lib";
    return paths;
}

QString MainWindow::findEphePath() const {
    const QStringList requiredFiles = {"sepl_18.se1", "semo_18.se1", "seas_18.se1", "sefstars.txt"};
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
        const QString candidate = dir.absoluteFilePath("ephe");
        QDir candidateDir(candidate);
        if (candidateDir.exists()) {
            bool hasAllFiles = true;
            for (const auto& fileName : requiredFiles) {
                if (!candidateDir.exists(fileName)) {
                    hasAllFiles = false;
                    break;
                }
            }
            if (hasAllFiles) {
                return candidate;
            }
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QString();
}

QString MainWindow::profilesDir() const {
    QDir base(QCoreApplication::applicationDirPath());
    const QString dirPath = base.absoluteFilePath("profiles");
    if (!QDir(dirPath).exists()) {
        QDir().mkpath(dirPath);
    }
    return dirPath;
}

QString MainWindow::sanitizeProfileName(const QString& name) const {
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    const QString invalid = "<>:\"/\\|?*";
    QString safe;
    safe.reserve(trimmed.size());
    for (QChar ch : trimmed) {
        safe.append(invalid.contains(ch) ? '_' : ch);
    }
    while (!safe.isEmpty() && (safe.endsWith(' ') || safe.endsWith('.'))) {
        safe.chop(1);
    }
    return safe.trimmed();
}

QString MainWindow::profileFilePath(const QString& name) const {
    const QString safe = sanitizeProfileName(name);
    if (safe.isEmpty()) {
        return QString();
    }
    QDir dir(profilesDir());
    return dir.filePath(safe + ".json");
}

QStringList MainWindow::listProfiles() const {
    QDir dir(profilesDir());
    const QStringList files = dir.entryList(QStringList() << "*.json", QDir::Files, QDir::Name);
    QStringList names;
    names.reserve(files.size());
    for (const auto& file : files) {
        names << QFileInfo(file).completeBaseName();
    }
    return names;
}

bool MainWindow::saveProfileByName(const QString& profileName, bool promptOverwrite) {
    if (!hasCurrentChart_) {
        setStatusMessage("Load or create a chart before saving a profile.");
        return false;
    }

    QString normalized = profileName.trimmed();
    if (normalized.isEmpty()) {
        setStatusMessage("Profile name cannot be empty.");
        return false;
    }
    const QString safeName = sanitizeProfileName(normalized);
    if (safeName.isEmpty()) {
        setStatusMessage("Profile name contains only invalid characters.");
        return false;
    }
    normalized = safeName;
    const QString filePath = profileFilePath(normalized);

    if (promptOverwrite && QFileInfo::exists(filePath) && normalized != currentProfileName_) {
        const auto overwrite = QMessageBox::question(
            this,
            "Overwrite profile",
            QString("Overwrite existing profile \"%1\"?").arg(normalized),
            QMessageBox::Yes | QMessageBox::No);
        if (overwrite != QMessageBox::Yes) {
            return false;
        }
    }

    QJsonObject obj;
    obj["profile_name"] = normalized;
    obj["name"] = currentInput_.name;
    obj["date"] = currentInput_.date.toString(Qt::ISODate);
    obj["time"] = currentInput_.time.toString("HH:mm:ss");
    obj["timezone"] = currentInput_.timezone;
    obj["gender"] = genderToString(currentInput_.gender);
    obj["location"] = currentLocation_;
    obj["latitude"] = currentInput_.latitude;
    obj["longitude"] = currentInput_.longitude;
    obj["house_system"] = (currentInput_.houseSystem == HouseSystem::Placidus) ? "Placidus" : "Whole Sign";
    QJsonArray fixedStarsJson;
    for (const auto& starName : currentInput_.fixedStars) {
        fixedStarsJson.push_back(starName);
    }
    obj["fixed_stars"] = fixedStarsJson;
    obj["saved_at_utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    obj["version"] = 2;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatusMessage(QString("Unable to save profile: %1").arg(file.errorString()));
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();

    currentProfileName_ = normalized;
    refreshProfileToolbar();
    return true;
}

bool MainWindow::loadProfileByName(const QString& profileName) {
    QString normalized = profileName.trimmed();
    if (normalized.isEmpty()) {
        setStatusMessage("Select a profile to load.");
        return false;
    }

    const QString filePath = profileFilePath(normalized);
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        setStatusMessage("Profile file not found.");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatusMessage(QString("Unable to load profile: %1").arg(file.errorString()));
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        setStatusMessage("Profile file is not valid JSON.");
        return false;
    }
    const QJsonObject obj = doc.object();

    NatalInput input;
    input.name = obj.value("name").toString();
    input.date = QDate::fromString(obj.value("date").toString(), Qt::ISODate);
    input.time = QTime::fromString(obj.value("time").toString(), "HH:mm:ss");
    if (!input.time.isValid()) {
        input.time = QTime::fromString(obj.value("time").toString(), "HH:mm");
    }
    input.timezone = obj.value("timezone").toString();
    if (!input.date.isValid()) {
        setStatusMessage("Profile date is invalid.");
        return false;
    }
    if (!input.time.isValid()) {
        setStatusMessage("Profile time is invalid.");
        return false;
    }
    if (input.timezone.trimmed().isEmpty()) {
        input.timezone = "UTC";
    }
    input.gender = genderFromString(obj.value("gender").toString());
    input.latitude = obj.value("latitude").toDouble();
    input.longitude = obj.value("longitude").toDouble();
    const QString houseSystem = obj.value("house_system").toString();
    input.houseSystem = houseSystem.contains("Placidus", Qt::CaseInsensitive)
        ? HouseSystem::Placidus
        : HouseSystem::WholeSign;
    const QJsonArray fixedStarsJson = obj.value("fixed_stars").toArray();
    for (const auto& value : fixedStarsJson) {
        const QString starName = value.toString().trimmed();
        if (!starName.isEmpty()) {
            input.fixedStars.push_back(starName);
        }
    }

    const QString location = obj.value("location").toString();
    if (!computeChart(input, location)) {
        return false;
    }

    currentProfileName_ = normalized;
    defaultHouseSystem_ = input.houseSystem;
    refreshProfileToolbar();
    return true;
}

void MainWindow::refreshProfileToolbar() {
    if (!profileToolbarCombo_) {
        return;
    }

    const QString previousSelection = profileToolbarCombo_->currentData().toString().trimmed();
    const QStringList profiles = listProfiles();
    {
        const QSignalBlocker blocker(profileToolbarCombo_);
        profileToolbarCombo_->clear();
        profileToolbarCombo_->addItem("Select profile...", QString());
        for (const auto& name : profiles) {
            profileToolbarCombo_->addItem(name, name);
        }

        QString preferred = currentProfileName_.trimmed();
        if (preferred.isEmpty()) {
            preferred = previousSelection;
        }
        int index = preferred.isEmpty() ? 0 : profileToolbarCombo_->findData(preferred);
        if (index < 0) {
            index = 0;
        }
        profileToolbarCombo_->setCurrentIndex(index);
    }

    const QString selected = profileToolbarCombo_->currentData().toString().trimmed();
    if (profileToolbarLoadButton_) {
        profileToolbarLoadButton_->setEnabled(!selected.isEmpty());
    }
    if (profileToolbarDeleteButton_) {
        profileToolbarDeleteButton_->setEnabled(!selected.isEmpty());
    }
    if (profileToolbarSaveButton_) {
        profileToolbarSaveButton_->setEnabled(hasCurrentChart_);
    }
    if (profileToolbarSaveAsButton_) {
        profileToolbarSaveAsButton_->setEnabled(hasCurrentChart_);
    }
    if (profileToolbarEditButton_) {
        profileToolbarEditButton_->setEnabled(hasCurrentChart_);
    }
    if (profileToolbarStateLabel_) {
        if (!currentProfileName_.trimmed().isEmpty()) {
            profileToolbarStateLabel_->setText(currentProfileName_);
        } else {
            profileToolbarStateLabel_->clear();
        }
    }
}

void MainWindow::showFixedStarSelectionDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Select Fixed Stars");
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* label = new QLabel("Select fixed stars to show on chart wheel:", &dialog);
    layout->addWidget(label);

    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    const QStringList catalog = fixedStarCatalog();
    for (const auto& name : catalog) {
        auto* item = new QListWidgetItem(name, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(isFixedStarVisible(name) ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (auto* applyButton = buttons->button(QDialogButtonBox::Ok)) {
        applyButton->setText("Apply");
    }
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QStringList selected;
    for (int i = 0; i < list->count(); ++i) {
        auto* item = list->item(i);
        if (item && item->checkState() == Qt::Checked) {
            selected.push_back(item->text());
        }
    }
    visibleFixedStars_ = selected;
    if (chartWheel_) {
        chartWheel_->setVisibleFixedStars(visibleFixedStars_);
    }
    if (activeTab_ == AppTab::Progression) {
        refreshProgressionView();
    } else if (activeTab_ == AppTab::SolarReturn) {
        refreshSolarReturnView();
    } else if (activeTab_ == AppTab::Relocation) {
        refreshRelocationView();
    } else if (activeTab_ == AppTab::Transits) {
        refreshTransitsTab();
    } else if (hasCurrentChart_) {
        populateFixedStars(currentChart_);
    }
}



void MainWindow::openChartSetupDialog(bool newChart) {
    ChartSetupDialog dialog(net_, this);
    dialog.setDefaultHouseSystem(defaultHouseSystem_);
    if (!newChart && hasCurrentChart_) {
        dialog.setInput(currentInput_, currentLocation_);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto input = dialog.input();
    const QString location = dialog.locationName();
    defaultHouseSystem_ = input.houseSystem;
    if (computeChart(input, location)) {
        if (newChart) {
            currentProfileName_.clear();
            refreshProfileToolbar();
        }
    }
}

bool MainWindow::computeChart(const NatalInput& input, const QString& location) {
    if (ephePath_.isEmpty()) {
        setCriticalMessage("Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.");
        return false;
    }

    NatalInput effectiveInput = input;
    effectiveInput.aspectOrbs = aspectOrbs_;
    if (effectiveInput.fixedStars.isEmpty()) {
        effectiveInput.fixedStars = fixedStarCatalog();
    } else {
        QStringList cleanedStars;
        const QStringList catalog = fixedStarCatalog();
        for (const auto& requested : effectiveInput.fixedStars) {
            const QString trimmed = requested.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            QString canonical;
            for (const auto& catalogName : catalog) {
                if (catalogName.compare(trimmed, Qt::CaseInsensitive) == 0) {
                    canonical = catalogName;
                    break;
                }
            }
            if (canonical.isEmpty()) {
                continue;
            }
            if (!cleanedStars.contains(canonical)) {
                cleanedStars.push_back(canonical);
            }
        }
        if (!cleanedStars.isEmpty()) {
            effectiveInput.fixedStars = cleanedStars;
        } else {
            effectiveInput.fixedStars = fixedStarCatalog();
        }
    }
    NatalChart chart;
    QString err;
    if (!engine_.compute(effectiveInput, &chart, &err)) {
        setCriticalMessage(err);
        return false;
    }
    if (!chart.warnings.isEmpty() && statusBar()) {
        statusBar()->showMessage(QString("Computed with warnings: %1").arg(chart.warnings.join("; ")), 12000);
    }

    populateSummary(chart, effectiveInput, location);
    populateAngles(chart);
    populatePlanets(chart);
    populateFixedStars(chart);
    populateHouses(chart, effectiveInput.houseSystem);
    populateAspects(chart);
    if (chartWheel_ && activeTab_ == AppTab::Natal) {
        chartWheel_->setChart(chart, effectiveInput.houseSystem);
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
    }
    if (effectiveInput.houseSystem == HouseSystem::Placidus) {
        natalPlacidusCusps_ = chart.cusps;
    } else {
        NatalInput placidusInput = effectiveInput;
        placidusInput.houseSystem = HouseSystem::Placidus;
        NatalChart placidusChart;
        QString cuspErr;
        if (engine_.compute(placidusInput, &placidusChart, &cuspErr)) {
            natalPlacidusCusps_ = placidusChart.cusps;
        } else {
            natalPlacidusCusps_.clear();
        }
    }
    currentInput_ = effectiveInput;
    currentLocation_ = location;
    currentChart_ = chart;
    hasCurrentChart_ = true;
    refreshNatalReport();
    if (transitTimezoneEdit_ && !effectiveInput.timezone.isEmpty()) {
        transitTimezoneEdit_->setText(effectiveInput.timezone);
    }
    syncTransitLocationFromNatal();
    updateTransitLocationAvailability();
    updateTransitTimezoneStatus();
    updateTransitTargetLabels();
    updateTransitSearchTargets();
    updateLunationModeAvailability();
    if (scanTimezoneLabel_) {
        const QString tzLabel = effectiveInput.timezone.isEmpty() ? QString("UTC") : effectiveInput.timezone;
        scanTimezoneLabel_->setText(QString("Timezone: %1").arg(tzLabel));
    }
    syncSolarLocationFromNatal();
    updateSolarLocationAvailability();
    updateSolarTimezoneStatus();
    markSolarPending();
    hasProgressionChart_ = false;
    if (progressionTimezoneEdit_ && !effectiveInput.timezone.isEmpty()) {
        progressionTimezoneEdit_->setText(effectiveInput.timezone);
        updateProgressionTimezoneStatus();
    }
    markProgressionPending();
    hasRelocationChart_ = false;
    markRelocationPending();
    if (activeTab_ == AppTab::Progression) {
        refreshProgressionView();
    }
    if (activeTab_ == AppTab::Transits) {
        refreshTransitsTab();
    } else if (activeTab_ == AppTab::Natal) {
        refreshNatalTransitsPanels();
    } else if (activeTab_ == AppTab::Progression) {
        // Progression view already refreshed.
    } else if (activeTab_ == AppTab::Relocation) {
        refreshRelocationView();
    } else if (activeTab_ == AppTab::Astrocartography) {
        updateAstrocartographyView();
    } else {
        refreshNatalTransitsPanels();
    }
    refreshProfileToolbar();
    return true;
}

void MainWindow::handleRecompute() {
    if (!hasCurrentChart_) {
        setStatusMessage("No chart loaded yet.");
        return;
    }
    computeChart(currentInput_, currentLocation_);
}

void MainWindow::handleNewChart() {
    openChartSetupDialog(true);
}

void MainWindow::handleEditChart() {
    if (!hasCurrentChart_) {
        openChartSetupDialog(true);
        return;
    }
    openChartSetupDialog(false);
}

void MainWindow::handleAspectOrbs() {
    AspectOrbsDialog dialog(aspectOrbs_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    aspectOrbs_ = dialog.orbs();
    saveUiState();
    if (hasCurrentChart_) {
        computeChart(currentInput_, currentLocation_);
    }
}

void MainWindow::handleMainTabChanged(int index) {
    if (index == 1) {
        activeTab_ = AppTab::Transits;
    } else if (index == 2) {
        activeTab_ = AppTab::Progression;
    } else if (index == 3) {
        activeTab_ = AppTab::SolarReturn;
    } else if (index == 4) {
        activeTab_ = AppTab::Relocation;
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    } else if (index == 5) {
        activeTab_ = AppTab::Astrocartography;
#endif
    } else {
        activeTab_ = AppTab::Natal;
    }
    if (dataStack_) {
        if (activeTab_ == AppTab::Transits) {
            dataStack_->setCurrentIndex(1);
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
        } else if (activeTab_ == AppTab::Astrocartography) {
            dataStack_->setCurrentIndex(2);
#endif
        } else {
            dataStack_->setCurrentIndex(0);
        }
    }
    if (progressionControls_) {
        progressionControls_->setVisible(activeTab_ == AppTab::Progression);
    }
    if (solarControls_) {
        solarControls_->setVisible(activeTab_ == AppTab::SolarReturn);
    }
    if (relocationControls_) {
        relocationControls_->setVisible(activeTab_ == AppTab::Relocation);
    }
    if (tabs_ && solarTechniquePanel_) {
        const int techniqueIndex = tabs_->indexOf(solarTechniquePanel_);
        if (techniqueIndex >= 0) {
            const bool showTechnique = (activeTab_ == AppTab::SolarReturn);
            tabs_->setTabVisible(techniqueIndex, showTechnique);
            if (!showTechnique && tabs_->currentWidget() == solarTechniquePanel_) {
                tabs_->setCurrentIndex(0);
            }
        }
    }

    if (centerStack_) {
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
        if (activeTab_ == AppTab::Astrocartography) {
            centerStack_->setCurrentWidget(worldMapPanel_);
        } else {
            centerStack_->setCurrentWidget(chartViewPanel_);
        }
#else
        centerStack_->setCurrentWidget(chartViewPanel_);
#endif
    }
    if (chartTitleLabel_) {
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
        chartTitleLabel_->setText(activeTab_ == AppTab::Astrocartography ? "World Map" : "Chart Wheel");
#else
        chartTitleLabel_->setText("Chart Wheel");
#endif
    }
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    const bool showChartControls = (activeTab_ != AppTab::Astrocartography);
#else
    const bool showChartControls = true;
#endif
    if (chartSettingsButton_) {
        chartSettingsButton_->setVisible(showChartControls);
    }
    if (zoomOutButton_) {
        zoomOutButton_->setVisible(showChartControls);
    }
    if (zoomResetButton_) {
        zoomResetButton_->setVisible(showChartControls);
    }
    if (zoomInButton_) {
        zoomInButton_->setVisible(showChartControls);
    }
    if (aspectsPanel_) {
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
        aspectsPanel_->setVisible(activeTab_ != AppTab::Astrocartography);
#else
        aspectsPanel_->setVisible(true);
#endif
    }

    if (activeTab_ == AppTab::Natal) {
        if (rightTopDock_) {
            rightTopDock_->setWindowTitle("Current Transits");
        }
        if (rightBottomDock_) {
            rightBottomDock_->setWindowTitle("Ingress Countdown");
        }
        if (hasCurrentChart_ && chartWheel_) {
            chartWheel_->setChart(currentChart_, currentInput_.houseSystem);
            chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        }
        if (hasCurrentChart_) {
            populateSummary(currentChart_, currentInput_, currentLocation_);
            populateAngles(currentChart_);
            populatePlanets(currentChart_);
            populateFixedStars(currentChart_);
            populateHouses(currentChart_, currentInput_.houseSystem);
            populateAspects(currentChart_);
        }
        refreshNatalTransitsPanels();
    } else if (activeTab_ == AppTab::Progression) {
        if (rightTopDock_) {
            rightTopDock_->setWindowTitle("Progression");
        }
        if (rightBottomDock_) {
            rightBottomDock_->setWindowTitle("Progression Details");
        }
        updateProgressionTimezoneStatus();
        updateProgressionStatusLabels();
        refreshProgressionView();
    } else if (activeTab_ == AppTab::SolarReturn) {
        updateSolarTechniqueDockTitles();
        updateSolarLocationAvailability();
        syncSolarLocationFromNatal();
        updateSolarTimezoneStatus();
        updateSolarStatusLabels();
        refreshSolarReturnView();
        refreshSolarTechniqueView();
    } else if (activeTab_ == AppTab::Relocation) {
        if (rightTopDock_) {
            rightTopDock_->setWindowTitle("Relocation");
        }
        if (rightBottomDock_) {
            rightBottomDock_->setWindowTitle("Relocation-Natal");
        }
        updateRelocationTimezoneStatus();
        updateRelocationStatusLabels();
        refreshRelocationView();
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    } else if (activeTab_ == AppTab::Astrocartography) {
        if (rightTopDock_) {
            rightTopDock_->setWindowTitle("Geodetic Lines");
        }
        if (rightBottomDock_) {
            rightBottomDock_->setWindowTitle("Map Details");
        }
        updateAstrocartographyModeUi();
        updateAstrocartographyView();
#endif
    } else {
        if (rightTopDock_) {
            rightTopDock_->setWindowTitle("Transits");
        }
        if (rightBottomDock_) {
            rightBottomDock_->setWindowTitle("Ingress Countdown");
        }
        if (scanTimezoneLabel_) {
            const QString tzLabel = hasCurrentChart_ && !currentInput_.timezone.isEmpty()
                ? currentInput_.timezone
                : QString("UTC");
            scanTimezoneLabel_->setText(QString("Timezone: %1").arg(tzLabel));
        }
        syncTransitLocationFromNatal();
        updateTransitLocationAvailability();
        updateTransitTimezoneStatus();
        updateTransitTargetLabels();
        refreshTransitsTab();
    }
    updateAspectScopeTabs();
    updateChartLegend();
    updateTransitSearchVisibility();
    refreshNatalReport();
}

void MainWindow::handleTransitNow() {
    if (!transitDateEdit_ || !transitTimeEdit_) {
        return;
    }
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = transitTimezoneEdit_ ? transitTimezoneEdit_->text().trimmed() : QString("UTC");
    if (!parseTimezoneInput(tzText, &tz, &label, &err)) {
        tz = QTimeZone::utc();
        label = "UTC";
    }
    const QDateTime nowLocal = QDateTime::currentDateTimeUtc().toTimeZone(tz);
    transitDateEdit_->setDate(nowLocal.date());
    transitTimeEdit_->setTime(nowLocal.time());
    if (transitTimezoneEdit_) {
        transitTimezoneEdit_->setText(label);
    }
    updateTransitTimezoneStatus();
    applyTransitCalculation();
}

void MainWindow::handleTransitShiftDays(int days) {
    if (!transitDateEdit_ || !transitTimeEdit_) {
        return;
    }
    const QDateTime current = transitSelectedLocal();
    const QDateTime next = current.addDays(days);
    transitDateEdit_->setDate(next.date());
    transitTimeEdit_->setTime(next.time());
    applyTransitCalculation();
}

void MainWindow::handleTransitModeChanged() {
    if (transitOverlayRadio_ && transitOverlayRadio_->isChecked()) {
        transitMode_ = TransitMode::NatalOverlay;
    } else {
        transitMode_ = TransitMode::TransitOnly;
    }
    if (transitSubTab_ == TransitSubTab::Lunations && hasLunationSelection_) {
        if (canApplyLunationResult(nullptr)) {
            applyLunationResult(lastLunationSelection_);
        }
    } else if (!transitPending_) {
        applyTransitCalculation();
    } else {
        updateTransitTargetLabels();
    }
    updateAspectScopeTabs();
    updateChartLegend();
    updateTransitSearchTargets();
    if (transitSubTab_ == TransitSubTab::Calendar && calendarIncludeHousesCheck_ && calendarIncludeHousesCheck_->isChecked()) {
        handleTransitCalendarRun();
    }
}

void MainWindow::handleTransitAspectViewChanged(int index) {
    if (activeTab_ == AppTab::SolarReturn) {
        if (isSolarTechniqueTabActive()) {
            return;
        }
        if (index < 0 || index > 1) {
            return;
        }
        solarAspectView_ = static_cast<SolarAspectView>(index);
        refreshSolarReturnView();
        return;
    }
    if (activeTab_ == AppTab::Relocation) {
        if (index < 0 || index > 1) {
            return;
        }
        relocationAspectView_ = static_cast<RelocationAspectView>(index);
        refreshRelocationView();
        return;
    }
    if (index < 0 || index > 2) {
        return;
    }
    transitAspectView_ = static_cast<TransitAspectView>(index);
    if (activeTab_ == AppTab::Transits && transitMode_ == TransitMode::NatalOverlay) {
        if (transitSubTab_ == TransitSubTab::Lunations && hasLunationSelection_) {
            if (canApplyLunationResult(nullptr)) {
                applyLunationResult(lastLunationSelection_);
            }
        } else {
            refreshTransitsTab();
        }
    }
}

void MainWindow::handleTransitCalculate() {
    applyTransitCalculation();
}

void MainWindow::handleTransitResetTime() {
    if (!hasCurrentChart_ || !transitDateEdit_ || !transitTimeEdit_) {
        return;
    }
    transitDateEdit_->setDate(currentInput_.date);
    transitTimeEdit_->setTime(currentInput_.time);
    if (transitTimezoneEdit_) {
        transitTimezoneEdit_->setText(currentInput_.timezone);
    }
    updateTransitTimezoneStatus();
    markTransitPending();
}

void MainWindow::handleTransitSubTabChanged(int index) {
    const TransitSubTab previousSubTab = transitSubTab_;
    if (index == 1) {
        transitSubTab_ = TransitSubTab::Search;
    } else if (index == 2) {
        transitSubTab_ = TransitSubTab::Calendar;
    } else if (index == 3) {
        transitSubTab_ = TransitSubTab::Conjunctions;
    } else if (index == 4) {
        transitSubTab_ = TransitSubTab::Scan;
    } else if (index == 5) {
        transitSubTab_ = TransitSubTab::Profections;
    } else if (index == 6) {
        transitSubTab_ = TransitSubTab::Lunations;
    } else {
        transitSubTab_ = TransitSubTab::Overview;
    }
    if (transitPanelStack_) {
        int stackIndex = 0;
        switch (transitSubTab_) {
            case TransitSubTab::Overview:
                stackIndex = 0;
                break;
            case TransitSubTab::Search:
                stackIndex = 1;
                break;
            case TransitSubTab::Calendar:
                stackIndex = 2;
                break;
            case TransitSubTab::Conjunctions:
                stackIndex = 3;
                break;
            case TransitSubTab::Scan:
                stackIndex = 4;
                break;
            case TransitSubTab::Profections:
                stackIndex = 5;
                break;
            case TransitSubTab::Lunations:
                stackIndex = 6;
                break;
        }
        transitPanelStack_->setCurrentIndex(stackIndex);
    }
    if (transitSubTab_ == TransitSubTab::Calendar && transitCalendarEvents_.isEmpty() && !calendarRunning_) {
        handleTransitCalendarRun();
    }
    if (transitSubTab_ == TransitSubTab::Profections && previousSubTab != TransitSubTab::Profections) {
        syncTransitProfectionAgeFromTransitDate();
    }
    updateTransitSearchTargets();
    updateTransitSearchVisibility();
}

void MainWindow::handleTransitSearchRun() {
    runTransitSearch();
}

void MainWindow::handleTransitSearchStop() {
    if (!searchWorker_) {
        return;
    }
    requestWorkerCancel(searchWorker_);
    if (searchStatusLabel_) {
        searchStatusLabel_->setText("Stopping...");
    }
}

void MainWindow::handleTransitSearchResultActivated(int row, int column) {
    Q_UNUSED(column);
    if (row < 0 || row >= transitSearchResults_.size()) {
        return;
    }
    applyTransitSearchResult(transitSearchResults_[row]);
}

void MainWindow::handleTransitCalendarRun() {
    if (!calendarYearCombo_) {
        return;
    }
    if (calendarRunning_) {
        calendarRestartPending_ = true;
        if (calendarWorker_) {
            requestWorkerCancel(calendarWorker_);
        }
        if (calendarStatusLabel_) {
            calendarStatusLabel_->setText("Stopping...");
        }
        return;
    }
    if (ephePath_.isEmpty()) {
        setStatusMessage("Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.");
        return;
    }

    QString tzText = transitTimezoneEdit_ ? transitTimezoneEdit_->text().trimmed() : QString("UTC");
    if (tzText.isEmpty()) {
        tzText = "UTC";
    }
    QTimeZone tz;
    QString normLabel;
    QString tzErr;
    if (!parseTimezoneInput(tzText, &tz, &normLabel, &tzErr)) {
        setStatusMessage(tzErr);
        return;
    }
    calendarTz_ = tz;
    calendarTzLabel_ = normLabel;

    int year = calendarYearCombo_->currentData().toInt();
    if (year <= 0) {
        year = calendarYearCombo_->currentText().toInt();
    }
    if (year < 1800 || year > 2399) {
        setStatusMessage("Calendar year must be between 1800 and 2399.");
        return;
    }

    const QDateTime startLocal(QDate(year, 1, 1), QTime(0, 0, 0), calendarTz_);
    const QDateTime endLocal(QDate(year, 12, 31), QTime(23, 59, 59), calendarTz_);
    if (!startLocal.isValid() || !endLocal.isValid()) {
        setStatusMessage("Invalid calendar range.");
        return;
    }

    const bool includeHouses = (calendarIncludeHousesCheck_ && calendarIncludeHousesCheck_->isChecked());
    const bool overlayMode = (transitMode_ == TransitMode::NatalOverlay && hasCurrentChart_);
    double latitude = 0.0;
    double longitude = 0.0;
    if (transitUseNatalLocation_ && transitUseNatalLocation_->isChecked() && hasCurrentChart_) {
        latitude = currentInput_.latitude;
        longitude = currentInput_.longitude;
    } else {
        latitude = transitLatSpin_ ? transitLatSpin_->value() : currentInput_.latitude;
        longitude = transitLonSpin_ ? transitLonSpin_->value() : currentInput_.longitude;
    }
    if (includeHouses && !overlayMode) {
        const QString locationName = transitLocationEdit_ ? transitLocationEdit_->text().trimmed() : QString();
        if (locationName.isEmpty() && std::abs(latitude) < 0.0001 && std::abs(longitude) < 0.0001) {
            setStatusMessage("Set transit location or latitude/longitude to include house ingress/egress.");
            return;
        }
    }

    CalendarParams params;
    params.startUtc = startLocal.toUTC();
    params.endUtc = endLocal.toUTC();
    params.tz = calendarTz_;
    params.tzLabel = calendarTzLabel_;
    params.ephePath = ephePath_;
    params.dllSearchPaths = sweSearchPaths();
    params.planetNames = selectedCheckableItems(calendarPlanetCombo_);
    if (params.planetNames.isEmpty()) {
        setStatusMessage("Select at least one calendar planet.");
        if (calendarStatusLabel_) {
            calendarStatusLabel_->setText("Select planets");
        }
        return;
    }
    params.includeHouses = includeHouses;
    params.overlayMode = overlayMode;
    params.houseSystem = transitHouseSystem_;
    params.latitude = latitude;
    params.longitude = longitude;
    if (overlayMode) {
        params.natalAsc = currentChart_.angles.asc;
        params.natalCusps.reserve(natalPlacidusCusps_.size());
        for (const auto& cusp : natalPlacidusCusps_) {
            params.natalCusps.push_back(cusp.longitude);
        }
    }

    transitCalendarEvents_.clear();
    transitCalendarDisplayOrder_.clear();
    hasTransitCalendarSelection_ = false;
    calendarRestartPending_ = false;
    calendarRunning_ = true;
    if (calendarStatusLabel_) {
        calendarStatusLabel_->setText("Computing...");
    }
    if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Calendar) {
        showTransitCalendarResults();
    }
    updateLunationCopyButtonState();

    auto* worker = new CalendarWorker(params);
    calendarWorker_ = worker;
    calendarThread_ = new QThread(this);
    worker->moveToThread(calendarThread_);
    connect(calendarThread_, &QThread::started, worker, &CalendarWorker::run);
    connect(worker, &CalendarWorker::progressUpdate, this, [this](int, const QString& status) {
        if (calendarStatusLabel_) {
            calendarStatusLabel_->setText(status);
        }
    });
    connect(worker, &CalendarWorker::finished, this, [this](bool cancelled, const QString& error) {
        calendarRunning_ = false;
        QStringList warnings;
        if (calendarWorker_) {
            auto* typedWorker = qobject_cast<CalendarWorker*>(calendarWorker_);
            if (typedWorker) {
                transitCalendarEvents_ = typedWorker->results();
                warnings = typedWorker->warnings();
            }
            calendarWorker_->deleteLater();
            calendarWorker_ = nullptr;
        }
        if (calendarThread_) {
            calendarThread_->quit();
            calendarThread_->deleteLater();
            calendarThread_ = nullptr;
        }

        if (!error.isEmpty()) {
            if (calendarStatusLabel_) {
                calendarStatusLabel_->setText("Idle");
            }
            setStatusMessage(error);
        } else if (cancelled) {
            if (calendarStatusLabel_) {
                calendarStatusLabel_->setText("Cancelled");
            }
        } else if (calendarStatusLabel_) {
            if (warnings.isEmpty()) {
                calendarStatusLabel_->setText(QString("Done (%1 events)").arg(transitCalendarEvents_.size()));
            } else {
                calendarStatusLabel_->setText(QString("Done with warnings (%1 events)").arg(transitCalendarEvents_.size()));
            }
        }
        if (error.isEmpty() && !warnings.isEmpty() && statusBar()) {
            statusBar()->showMessage(QString("Computed with warnings: %1").arg(warnings.join("; ")), 15000);
        }

        if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Calendar) {
            showTransitCalendarResults();
        }

        if (calendarRestartPending_) {
            calendarRestartPending_ = false;
            handleTransitCalendarRun();
        }
    });
    calendarThread_->start();
}

void MainWindow::handleTransitCalendarResultActivated(int row, int column) {
    Q_UNUSED(column);
    if (row < 0 || row >= transitCalendarDisplayOrder_.size()) {
        return;
    }
    const int eventIndex = transitCalendarDisplayOrder_[row];
    if (eventIndex < 0 || eventIndex >= transitCalendarEvents_.size()) {
        return;
    }
    const auto& event = transitCalendarEvents_[eventIndex];
    const QTimeZone tz = calendarTz_.isValid() ? calendarTz_ : QTimeZone::utc();
    const QString tzLabel = event.tzLabel.isEmpty() ? QString("UTC") : event.tzLabel;
    const QDateTime localTime = event.timeUtc.toTimeZone(tz);

    NatalChart chart;
    QString err;
    if (!computeTransitChartAt(localTime, tzLabel, &chart, &err)) {
        hasTransitCalendarSelection_ = false;
        updateLunationCopyButtonState();
        setStatusMessage(err);
        return;
    }

    currentTransitChart_ = chart;
    hasTransitChart_ = true;
    hasTransitCalendarSelection_ = true;
    lastTransitCalendarSelection_ = event;
    transitPending_ = false;
    lastTransitCalculated_ = QDateTime::currentDateTime();
    updateTransitTargetLabels();

    if (chartWheel_) {
        if (transitMode_ == TransitMode::NatalOverlay && hasCurrentChart_) {
            chartWheel_->setShowAspects(true);
            chartWheel_->setOverlayLabel("Transit");
            chartWheel_->setOverlayCharts(currentChart_, chart, transitHouseSystem_, aspectOrbs_);
            chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
        } else {
            chartWheel_->setTransitChart(chart, transitHouseSystem_);
        }
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        chartWheel_->setHighlight(event.planet, true, event.event, QColor("#f0c24b"));
    }
    showTransitCalendarDetails(event);
    updateLunationCopyButtonState();
}

void MainWindow::handleTransitConjunctionRun() {
    if (conjRunning_) {
        conjRestartPending_ = true;
        if (conjWorker_) {
            requestWorkerCancel(conjWorker_);
        }
        if (conjStatusLabel_) {
            conjStatusLabel_->setText("Stopping...");
        }
        return;
    }
    if (ephePath_.isEmpty()) {
        setStatusMessage("Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.");
        return;
    }

    QStringList planets = selectedCheckableItems(conjPlanetCombo_);
    if (planets.isEmpty()) {
        setStatusMessage("Select at least one conjunction planet.");
        if (conjStatusLabel_) {
            conjStatusLabel_->setText("Select planets");
        }
        return;
    }
    const int minCount = conjCountSpin_ ? conjCountSpin_->value() : 2;
    if (minCount > planets.size()) {
        setStatusMessage("N is greater than the selected planet count.");
        return;
    }

    const bool bucketByHouse = (conjBucketHouseRadio_ && conjBucketHouseRadio_->isChecked());
    if (bucketByHouse && !hasCurrentChart_) {
        setStatusMessage("Load a natal chart to use natal houses.");
        return;
    }

    QTimeZone tz;
    QString tzLabel;
    QString tzErr;
    const QString tzText = transitTimezoneEdit_ ? transitTimezoneEdit_->text().trimmed() : QString("UTC");
    if (!parseTimezoneInput(tzText, &tz, &tzLabel, &tzErr)) {
        setStatusMessage(tzErr);
        return;
    }
    conjTz_ = tz;
    conjTzLabel_ = tzLabel;

    const bool findNext = conjModeNextRadio_ && conjModeNextRadio_->isChecked();
    const bool findPrev = conjModePrevRadio_ && conjModePrevRadio_->isChecked();
    if (findNext) {
        conjFindMode_ = ConjunctionFindMode::Next;
    } else if (findPrev) {
        conjFindMode_ = ConjunctionFindMode::Previous;
    } else {
        conjFindMode_ = ConjunctionFindMode::Range;
    }

    const QDateTime anchorLocal = transitSelectedLocal();
    if (!anchorLocal.isValid()) {
        setStatusMessage("Invalid transit target time.");
        return;
    }
    conjAnchorUtc_ = anchorLocal.toUTC();

    QDateTime startUtc;
    QDateTime endUtc;
    if (conjFindMode_ == ConjunctionFindMode::Range) {
        if (!conjStartYearSpin_ || !conjEndYearSpin_) {
            return;
        }
        int startYear = conjStartYearSpin_->value();
        int endYear = conjEndYearSpin_->value();
        if (startYear > endYear) {
            const int tmp = startYear;
            startYear = endYear;
            endYear = tmp;
            conjStartYearSpin_->setValue(startYear);
            conjEndYearSpin_->setValue(endYear);
        }
        const QDateTime startLocal(QDate(startYear, 1, 1), QTime(0, 0, 0), tz);
        const QDateTime endLocal(QDate(endYear, 12, 31), QTime(23, 59, 59), tz);
        if (!startLocal.isValid() || !endLocal.isValid()) {
            setStatusMessage("Invalid conjunction range.");
            return;
        }
        startUtc = startLocal.toUTC();
        endUtc = endLocal.toUTC();
    } else {
        const int horizonDays = 365 * 50;
        if (conjFindMode_ == ConjunctionFindMode::Next) {
            startUtc = conjAnchorUtc_;
            endUtc = conjAnchorUtc_.addDays(horizonDays);
        } else {
            endUtc = conjAnchorUtc_;
            startUtc = conjAnchorUtc_.addDays(-horizonDays);
        }
    }

    ConjunctionParams params;
    params.startUtc = startUtc;
    params.endUtc = endUtc;
    params.tz = tz;
    params.tzLabel = tzLabel;
    params.ephePath = ephePath_;
    params.dllSearchPaths = sweSearchPaths();
    params.planetNames = planets;
    params.minCount = minCount;
    const bool exactPairMode = (minCount == 2 && planets.size() == 2);
    params.useOrb = !exactPairMode && (conjUseOrbCheck_ && conjUseOrbCheck_->isChecked());
    params.orbDeg = params.useOrb && conjOrbSpin_ ? conjOrbSpin_->value() : 0.0;
    params.bucketByHouse = bucketByHouse;
    params.houseSystem = transitHouseSystem_;
    if (bucketByHouse) {
        params.natalAsc = currentChart_.angles.asc;
        params.natalCusps.reserve(natalPlacidusCusps_.size());
        for (const auto& cusp : natalPlacidusCusps_) {
            params.natalCusps.push_back(normalizeDegrees(cusp.longitude));
        }
    }

    transitConjunctionResults_.clear();
    transitConjunctionDisplayOrder_.clear();
    hasTransitConjunctionSelection_ = false;
    conjRestartPending_ = false;
    conjAutoApplied_ = false;
    conjRunning_ = true;
    if (conjStatusLabel_) {
        conjStatusLabel_->setText("Computing...");
    }
    if (conjRunButton_) {
        conjRunButton_->setEnabled(false);
    }
    if (conjStopButton_) {
        conjStopButton_->setEnabled(true);
    }
    if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Conjunctions) {
        showTransitConjunctionResults();
    }
    updateLunationCopyButtonState();

    auto* worker = new ConjunctionWorker(params);
    conjWorker_ = worker;
    conjThread_ = new QThread(this);
    worker->moveToThread(conjThread_);
    connect(conjThread_, &QThread::started, worker, &ConjunctionWorker::run);
    connect(worker, &ConjunctionWorker::progressUpdate, this, [this](int, const QString& status) {
        if (conjStatusLabel_) {
            conjStatusLabel_->setText(status);
        }
    });
    connect(worker, &ConjunctionWorker::finished, this, [this](bool cancelled, const QString& error) {
        conjRunning_ = false;
        QStringList warnings;
        if (conjWorker_) {
            auto* typedWorker = qobject_cast<ConjunctionWorker*>(conjWorker_);
            if (typedWorker) {
                transitConjunctionResults_ = typedWorker->results();
                warnings = typedWorker->warnings();
            }
            conjWorker_->deleteLater();
            conjWorker_ = nullptr;
        }
        if (conjThread_) {
            conjThread_->quit();
            conjThread_->deleteLater();
            conjThread_ = nullptr;
        }
        if (conjRunButton_) {
            conjRunButton_->setEnabled(true);
        }
        if (conjStopButton_) {
            conjStopButton_->setEnabled(false);
        }
        if (!error.isEmpty()) {
            if (conjStatusLabel_) {
                conjStatusLabel_->setText("Error");
            }
            setStatusMessage(error);
        } else if (cancelled) {
            if (conjStatusLabel_) {
                conjStatusLabel_->setText("Cancelled");
            }
        } else if (conjStatusLabel_) {
            if (warnings.isEmpty()) {
                conjStatusLabel_->setText(QString("Done (%1)").arg(transitConjunctionResults_.size()));
            } else {
                conjStatusLabel_->setText(QString("Done with warnings (%1)").arg(transitConjunctionResults_.size()));
            }
        }
        if (error.isEmpty() && !warnings.isEmpty() && statusBar()) {
            statusBar()->showMessage(QString("Computed with warnings: %1").arg(warnings.join("; ")), 15000);
        }
        if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Conjunctions) {
            showTransitConjunctionResults();
        }
        if (conjRestartPending_) {
            conjRestartPending_ = false;
            handleTransitConjunctionRun();
        }
    });
    conjThread_->start();
}

void MainWindow::handleTransitConjunctionStop() {
    if (!conjWorker_) {
        return;
    }
    requestWorkerCancel(conjWorker_);
    if (conjStatusLabel_) {
        conjStatusLabel_->setText("Stopping...");
    }
}

void MainWindow::handleTransitConjunctionResultActivated(int row, int column) {
    Q_UNUSED(column);
    if (row < 0 || row >= transitConjunctionDisplayOrder_.size()) {
        return;
    }
    const int eventIndex = transitConjunctionDisplayOrder_[row];
    if (eventIndex < 0 || eventIndex >= transitConjunctionResults_.size()) {
        return;
    }
    const auto& event = transitConjunctionResults_[eventIndex];
    const QTimeZone tz = conjTz_.isValid() ? conjTz_ : QTimeZone::utc();
    const QString tzLabel = event.tzLabel.isEmpty() ? QString("UTC") : event.tzLabel;
    const QDateTime localTime = event.startUtc.toTimeZone(tz);

    NatalChart chart;
    QString err;
    if (!computeTransitChartAt(localTime, tzLabel, &chart, &err)) {
        setStatusMessage(err);
        return;
    }

    currentTransitChart_ = chart;
    hasTransitChart_ = true;
    hasTransitConjunctionSelection_ = true;
    lastTransitConjunctionSelection_ = event;
    transitPending_ = false;
    lastTransitCalculated_ = QDateTime::currentDateTime();
    updateTransitTargetLabels();

    if (chartWheel_) {
        if (transitMode_ == TransitMode::NatalOverlay && hasCurrentChart_) {
            chartWheel_->setShowAspects(true);
            chartWheel_->setOverlayLabel("Transit");
            chartWheel_->setOverlayCharts(currentChart_, chart, transitHouseSystem_, aspectOrbs_);
            chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
        } else {
            chartWheel_->setTransitChart(chart, transitHouseSystem_);
        }
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        const QStringList highlightList = event.orbClusterAtStart.isEmpty()
            ? event.planetsInBucketAtStart
            : event.orbClusterAtStart;
        if (!highlightList.isEmpty()) {
            chartWheel_->setHighlight(highlightList.front(), true, "Conjunction", QColor("#f0c24b"));
        }
    }
    showTransitConjunctionDetails(event);
}

void MainWindow::handleTransitProfectionRun() {
    refreshTransitProfectionTab();
}

void MainWindow::handleLunationSearchRun() {
    runLunationSearch();
}

void MainWindow::handleLunationSearchStop() {
    if (!lunationWorker_) {
        return;
    }
    requestWorkerCancel(lunationWorker_);
    if (lunationStatusLabel_) {
        lunationStatusLabel_->setText("Stopping...");
    }
}

void MainWindow::handleLunationResultActivated(int row, int column) {
    Q_UNUSED(column);
    if (row < 0) {
        return;
    }
    if (lunationAnalysisMode_ == LunationAnalysisMode::RepeatedDegrees) {
        if (row >= lunationDegreeGroupDisplayOrder_.size()) {
            return;
        }
        const int groupIndex = lunationDegreeGroupDisplayOrder_[row];
        showLunationGroupDetails(groupIndex);
        return;
    }

    int resultIndex = row;
    if (lunationAnalysisMode_ == LunationAnalysisMode::TargetDegree) {
        if (row >= lunationAnalysisEventOrder_.size()) {
            return;
        }
        resultIndex = lunationAnalysisEventOrder_[row];
    } else if (lunationAnalysisMode_ == LunationAnalysisMode::List) {
        if (!lunationListDisplayOrder_.isEmpty()) {
            if (row >= lunationListDisplayOrder_.size()) {
                return;
            }
            resultIndex = lunationListDisplayOrder_[row];
        }
    }
    if (resultIndex < 0 || resultIndex >= lunationResults_.size()) {
        return;
    }
    applyLunationResult(lunationResults_[resultIndex]);
}

void MainWindow::handleTransitScanStart() {
    if (!hasCurrentChart_) {
        setStatusMessage("Load a natal chart first to run a scan.");
        return;
    }
    if (ephePath_.isEmpty()) {
        setStatusMessage("Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.");
        return;
    }
    if (transitScanRunning_) {
        return;
    }
    if (!scanStartYearSpin_ || !scanEndYearSpin_) {
        return;
    }
    int startYear = scanStartYearSpin_->value();
    int endYear = scanEndYearSpin_->value();
    if (startYear > endYear) {
        const int tmp = startYear;
        startYear = endYear;
        endYear = tmp;
        scanStartYearSpin_->setValue(startYear);
        scanEndYearSpin_->setValue(endYear);
    }
    const QDate startDate(startYear, 1, 1);
    const QDate endDate(endYear, 12, 31);
    if (!startDate.isValid() || !endDate.isValid()) {
        setStatusMessage("Invalid year range.");
        return;
    }

    QString tzLabel = currentInput_.timezone.trimmed();
    if (tzLabel.isEmpty()) {
        tzLabel = "UTC";
    }
    QTimeZone tz;
    QString normLabel;
    QString tzErr;
    if (!parseTimezoneInput(tzLabel, &tz, &normLabel, &tzErr)) {
        setStatusMessage(tzErr);
        return;
    }

    TransitScanWorker::Config config;
    config.startDate = startDate;
    config.endDate = endDate;
    config.scanTime = scanTimeEdit_ ? scanTimeEdit_->time() : QTime(12, 0, 0);
    config.tzLabel = normLabel;
    config.natalInput = currentInput_;
    config.natalInput.aspectOrbs = aspectOrbs_;
    config.natalChart = currentChart_;
    config.orbs = aspectOrbs_;
    config.ephePath = ephePath_;
    config.dllSearchPaths = sweSearchPaths();
    config.mode = static_cast<TransitScanMode>(scanModeCombo_ ? scanModeCombo_->currentIndex() : 0);
    config.conjunctionPolicy = static_cast<ConjunctionPolicy>(scanConjunctionCombo_ ? scanConjunctionCombo_->currentIndex() : 0);
    config.includeNodes = scanIncludeNodesCheck_ && scanIncludeNodesCheck_->isChecked();
    config.includeAngles = scanIncludeAnglesCheck_ && scanIncludeAnglesCheck_->isChecked();
    config.includeAsteroidAspects = includeAsteroidAspects_;
    const int tnWeight = scanWeightTransitNatalSpin_ ? scanWeightTransitNatalSpin_->value() : 25;
    const int ttWeight = scanWeightTransitTransitSpin_ ? scanWeightTransitTransitSpin_->value() : 50;
    const int tsWeight = scanWeightSolarSpin_ ? scanWeightSolarSpin_->value() : 25;
    const int tpWeight = scanWeightProgressedSpin_ ? scanWeightProgressedSpin_->value() : 0;
    config.weightTransitNatal = static_cast<double>(tnWeight) / 100.0;
    config.weightTransitTransit = static_cast<double>(ttWeight) / 100.0;
    config.weightTransitSolar = static_cast<double>(tsWeight) / 100.0;
    config.weightTransitProgressed = static_cast<double>(tpWeight) / 100.0;
    config.useSolarBias = scanSolarBiasCheck_ && scanSolarBiasCheck_->isChecked();
    if (scanSolarBiasSpin_) {
        config.solarBiasWeight = static_cast<double>(scanSolarBiasSpin_->value()) / 100.0;
    }

    transitScanRunning_ = true;
    if (scanRunButton_) {
        scanRunButton_->setEnabled(false);
    }
    if (scanCancelButton_) {
        scanCancelButton_->setEnabled(true);
    }
    if (scanProgressBar_) {
        const int totalDays = startDate.daysTo(endDate) + 1;
        scanProgressBar_->setRange(0, totalDays);
        scanProgressBar_->setValue(0);
    }
    if (scanStatusLabel_) {
        scanStatusLabel_->setText("Scanning...");
    }
    transitScanResults_.clear();
    transitScanDisplayOrder_.clear();
    hasTransitScanSelection_ = false;
    lastTransitScanSelectionLocal_ = QDateTime();
    lastTransitScanSelectionTzLabel_.clear();
    updateLunationCopyButtonState();

    auto* worker = new TransitScanWorker(config);
    scanWorker_ = worker;
    scanThread_ = new QThread(this);
    worker->moveToThread(scanThread_);

    connect(scanThread_, &QThread::started, worker, &TransitScanWorker::run);
    connect(worker, &TransitScanWorker::progress, this, [this](int done, int total) {
        if (scanProgressBar_) {
            scanProgressBar_->setRange(0, total);
            scanProgressBar_->setValue(done);
        }
    });
    connect(worker, &TransitScanWorker::error, this, [this](const QString& message) {
        setStatusMessage(message);
    });
    connect(worker, &TransitScanWorker::finished, this, &MainWindow::handleTransitScanFinished);
    connect(scanThread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(scanThread_, &QThread::finished, scanThread_, &QObject::deleteLater);

    scanThread_->start();
    if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Scan) {
        refreshTransitScanTab();
    }
}

void MainWindow::handleTransitScanCancel() {
    if (!transitScanRunning_ || !scanWorker_) {
        return;
    }
    requestWorkerCancel(scanWorker_);
    if (scanStatusLabel_) {
        scanStatusLabel_->setText("Stopping...");
    }
    if (scanCancelButton_) {
        scanCancelButton_->setEnabled(false);
    }
}

void MainWindow::handleTransitScanResultActivated(int row, int column) {
    Q_UNUSED(column);
    if (row < 0 || row >= transitScanDisplayOrder_.size()) {
        return;
    }
    const int resultIndex = transitScanDisplayOrder_[row];
    if (resultIndex < 0 || resultIndex >= transitScanResults_.size()) {
        return;
    }

    QString tzLabel = currentInput_.timezone.trimmed();
    if (tzLabel.isEmpty()) {
        tzLabel = "UTC";
    }
    QTimeZone tz;
    QString normLabel;
    QString tzErr;
    if (!parseTimezoneInput(tzLabel, &tz, &normLabel, &tzErr)) {
        setStatusMessage(tzErr);
        return;
    }

    const QTime scanTime = scanTimeEdit_ ? scanTimeEdit_->time() : QTime(12, 0, 0);
    const QDateTime localTime(transitScanResults_[resultIndex].date, scanTime, tz);
    if (!localTime.isValid()) {
        setStatusMessage("Invalid scan result time.");
        return;
    }

    NatalChart chart;
    QString err;
    if (!computeTransitChartAt(localTime, normLabel, &chart, &err)) {
        hasTransitScanSelection_ = false;
        updateLunationCopyButtonState();
        setStatusMessage(err);
        return;
    }

    currentTransitChart_ = chart;
    hasTransitChart_ = true;
    hasTransitScanSelection_ = true;
    lastTransitScanSelection_ = transitScanResults_[resultIndex];
    lastTransitScanSelectionLocal_ = localTime;
    lastTransitScanSelectionTzLabel_ = normLabel;
    transitPending_ = false;
    lastTransitCalculated_ = QDateTime::currentDateTime();
    updateTransitTargetLabels();

    if (chartWheel_) {
        if (transitMode_ == TransitMode::NatalOverlay && hasCurrentChart_) {
            chartWheel_->setShowAspects(true);
            chartWheel_->setOverlayLabel("Transit");
            chartWheel_->setOverlayCharts(currentChart_, chart, transitHouseSystem_, aspectOrbs_);
            chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
        } else {
            chartWheel_->setTransitChart(chart, transitHouseSystem_);
        }
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        chartWheel_->clearHighlight();
    }

    showTransitScanDetails(resultIndex);
    updateLunationCopyButtonState();
}

void MainWindow::handleTransitScanFinished() {
    transitScanRunning_ = false;
    QStringList warnings;
    if (scanRunButton_) {
        scanRunButton_->setEnabled(true);
    }
    if (scanCancelButton_) {
        scanCancelButton_->setEnabled(false);
    }
    if (scanWorker_) {
        auto* worker = qobject_cast<TransitScanWorker*>(scanWorker_);
        if (worker) {
            transitScanResults_ = worker->results();
            warnings = worker->warnings();
        }
        scanWorker_ = nullptr;
    }
    if (scanProgressBar_) {
        scanProgressBar_->setValue(scanProgressBar_->maximum());
    }
    if (scanStatusLabel_) {
        if (warnings.isEmpty()) {
            scanStatusLabel_->setText(QString("Done (%1 days)").arg(transitScanResults_.size()));
        } else {
            scanStatusLabel_->setText(QString("Done with warnings (%1 days)").arg(transitScanResults_.size()));
        }
    }
    if (!warnings.isEmpty() && statusBar()) {
        statusBar()->showMessage(QString("Computed with warnings: %1").arg(warnings.join("; ")), 15000);
    }
    if (scanThread_) {
        scanThread_->quit();
        scanThread_ = nullptr;
    }
    updateTransitScanResultsTable();
    if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Scan) {
        refreshTransitScanTab();
    }
}

void MainWindow::handleCopyAspects() {
    if (!hasCurrentChart_) {
        setStatusMessage("Load a natal chart first.");
        return;
    }
    if (activeTab_ == AppTab::Transits && !hasTransitChart_) {
        setStatusMessage("Calculate transits first to copy aspects.");
        return;
    }
    if (activeTab_ == AppTab::Progression && progressionView_ != ProgressionView::NatalOnly && !hasProgressionChart_) {
        setStatusMessage("Calculate progressions first to copy aspects.");
        return;
    }
    if (activeTab_ == AppTab::SolarReturn && !hasSolarChart_) {
        setStatusMessage("Calculate a solar return first to copy aspects.");
        return;
    }
    if (activeTab_ == AppTab::Relocation && !hasRelocationChart_) {
        setStatusMessage("Calculate relocation first to copy aspects.");
        return;
    }

    const QString text = buildAspectsClipboardText();
    if (text.isEmpty()) {
        setStatusMessage("No aspects available to copy.");
        return;
    }
    if (auto* clipboard = QApplication::clipboard()) {
        clipboard->setText(text);
    }
    setStatusMessage("Aspect matrix copied to clipboard.");
}

void MainWindow::handleCopyTransitSearchDetails() {
    const QString text = buildTransitSearchDetailsClipboardText();
    if (text.isEmpty()) {
        setStatusMessage("Select a search result first.");
        return;
    }
    if (auto* clipboard = QApplication::clipboard()) {
        clipboard->setText(text);
    }
    setStatusMessage("Transit placements copied to clipboard.");
}

void MainWindow::handleCopyTransitCalendarDetails() {
    const QString text = buildTransitCalendarDetailsClipboardText();
    if (text.isEmpty()) {
        setStatusMessage("Select a calendar result first.");
        return;
    }
    if (auto* clipboard = QApplication::clipboard()) {
        clipboard->setText(text);
    }
    setStatusMessage("Calendar event placements copied to clipboard.");
}

void MainWindow::handleCopyTransitConjunctionDetails() {
    const QString text = buildTransitConjunctionDetailsClipboardText();
    if (text.isEmpty()) {
        setStatusMessage("Select a conjunction result first.");
        return;
    }
    if (auto* clipboard = QApplication::clipboard()) {
        clipboard->setText(text);
    }
    setStatusMessage("Conjunction placements copied to clipboard.");
}

void MainWindow::handleCopyTransitScanDetails() {
    const QString text = buildTransitScanDetailsClipboardText();
    if (text.isEmpty()) {
        setStatusMessage("Select a scan result first.");
        return;
    }
    if (auto* clipboard = QApplication::clipboard()) {
        clipboard->setText(text);
    }
    setStatusMessage("Scan result report copied to clipboard.");
}

void MainWindow::handleCopyLunationDetails() {
    const QString text = buildLunationDetailsClipboardText();
    if (text.isEmpty()) {
        setStatusMessage("Select a lunation result first.");
        return;
    }
    if (auto* clipboard = QApplication::clipboard()) {
        clipboard->setText(text);
    }
    setStatusMessage("Lunation details copied to clipboard.");
}

void MainWindow::handleCopyReport() {
    if (!hasCurrentChart_) {
        setStatusMessage("Load a natal chart to generate the report.");
        return;
    }
    const QString text = buildNatalReportText();
    if (text.isEmpty()) {
        setStatusMessage("No report content to copy.");
        return;
    }
    if (auto* clipboard = QApplication::clipboard()) {
        clipboard->setText(text);
    }
    setStatusMessage("Natal report copied to clipboard.");
}

void MainWindow::updateLunationCopyButtonState() {
    if (!rightBottomCopyButton_) {
        return;
    }
    const bool inSearch = (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Search);
    if (inSearch) {
        rightBottomCopyButton_->setText("Copy Placements (Markdown)");
        rightBottomCopyButton_->setVisible(true);
        rightBottomCopyButton_->setEnabled(hasTransitSearchSelection_ && hasTransitChart_);
        return;
    }
    const bool inCalendar = (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Calendar);
    if (inCalendar) {
        rightBottomCopyButton_->setText("Copy Calendar Placements");
        rightBottomCopyButton_->setVisible(true);
        rightBottomCopyButton_->setEnabled(hasTransitCalendarSelection_ && hasTransitChart_);
        return;
    }
    const bool inConjunctions = (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Conjunctions);
    if (inConjunctions) {
        rightBottomCopyButton_->setText("Copy Conjunction Placements");
        rightBottomCopyButton_->setVisible(true);
        rightBottomCopyButton_->setEnabled(hasTransitConjunctionSelection_ && hasTransitChart_);
        return;
    }
    const bool inScan = (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Scan);
    if (inScan) {
        rightBottomCopyButton_->setText("Copy Scan Report");
        rightBottomCopyButton_->setVisible(true);
        rightBottomCopyButton_->setEnabled(hasTransitScanSelection_ && hasTransitChart_);
        return;
    }
    const bool inLunations = (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Lunations);
    const bool showingEventDetails = lunationBottomEventOrder_.isEmpty();
    if (inLunations) {
        rightBottomCopyButton_->setText("Copy Lunation Placements");
        rightBottomCopyButton_->setVisible(true);
        rightBottomCopyButton_->setEnabled(showingEventDetails && hasLunationSelection_ && hasTransitChart_);
        return;
    }
    rightBottomCopyButton_->setVisible(false);
    rightBottomCopyButton_->setEnabled(false);
}

QString MainWindow::buildTransitSearchDetailsClipboardText() const {
    if (!hasTransitSearchSelection_ || !hasTransitChart_) {
        return QString();
    }
    const TransitSearchResult& result = lastTransitSearchSelection_;
    const NatalChart& chart = currentTransitChart_;
    QStringList lines;
    lines << "Transit Search Result Placements";
    lines << QString("Local Time: %1").arg(result.timeLocal.toString("yyyy-MM-dd HH:mm:ss"));
    lines << QString("UTC Time: %1").arg(result.timeUtc.toString("yyyy-MM-dd HH:mm:ss"));
    lines << QString("Timezone: %1").arg(result.tzLabel);
    lines << QString("Planet: %1").arg(result.planet);
    lines << QString("Event: %1").arg(result.event);
    lines << QString("Sign/House: %1").arg(result.signHouse.isEmpty() ? "-" : result.signHouse);
    lines << QString("Aspect: %1").arg(result.aspect.isEmpty() ? "-" : result.aspect);
    lines << QString("Orb: %1").arg(result.hasOrb ? QString::number(result.orb, 'f', 2) : "-");
    lines << "";
    lines << "| Body | Degree | Sign | House | Motion |";
    lines << "| --- | --- | --- | --- | --- |";

    QMap<QString, BodyPosition> bodyMap;
    for (const auto& body : chart.bodies) {
        bodyMap.insert(body.name, body);
    }
    auto appendBody = [&](const BodyPosition& body) {
        const QString degree = formatDegOnly(body.longitude);
        const QString sign = signName(signIndex(body.longitude));
        const QString house = body.house > 0 ? QString::number(body.house) : "-";
        const QString motion = body.retrograde ? "R" : "D";
        lines << QString("| %1 | %2 | %3 | %4 | %5 |")
            .arg(body.name, degree, sign, house, motion);
    };
    for (const auto& name : tropicalBodyOrder()) {
        if (!bodyMap.contains(name)) {
            continue;
        }
        appendBody(bodyMap.value(name));
        bodyMap.remove(name);
    }
    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        appendBody(it.value());
    }

    if (!chart.fixedStars.isEmpty()) {
        QVector<FixedStarPosition> stars = chart.fixedStars;
        std::sort(stars.begin(), stars.end(), [](const FixedStarPosition& a, const FixedStarPosition& b) {
            return a.longitude < b.longitude;
        });
        lines << "";
        lines << "| Fixed Star | Degree | Sign | House |";
        lines << "| --- | --- | --- | --- |";
        for (const auto& star : stars) {
            lines << QString("| %1 | %2 | %3 | %4 |")
                .arg(star.name)
                .arg(formatDegOnly(star.longitude))
                .arg(signName(signIndex(star.longitude)))
                .arg(star.house > 0 ? QString::number(star.house) : "-");
        }
    }
    return lines.join("\n");
}

QString MainWindow::buildTransitCalendarDetailsClipboardText() const {
    if (!hasTransitCalendarSelection_ || !hasTransitChart_) {
        return QString();
    }
    const TransitCalendarEvent& result = lastTransitCalendarSelection_;
    const NatalChart& chart = currentTransitChart_;
    const QTimeZone displayTz = calendarTz_.isValid() ? calendarTz_ : QTimeZone::utc();
    const QDateTime localTime = result.timeUtc.toTimeZone(displayTz);
    const QString tzLabel = result.tzLabel.isEmpty() ? QString("UTC") : result.tzLabel;

    QStringList lines;
    lines << "Transit Calendar Event Placements";
    lines << QString("Local Time: %1").arg(localTime.toString("yyyy-MM-dd HH:mm:ss"));
    lines << QString("UTC Time: %1").arg(result.timeUtc.toString("yyyy-MM-dd HH:mm:ss"));
    lines << QString("Timezone: %1").arg(tzLabel);
    lines << QString("Planet: %1").arg(result.planet);
    lines << QString("Event: %1").arg(result.event);
    lines << QString("Sign/House: %1").arg(result.signHouse.isEmpty() ? "-" : result.signHouse);
    lines << QString("Longitude: %1").arg(formatDegInSign(result.longitude));
    lines << "";
    appendBodyPlacementsMarkdown(&lines, chart);
    appendFixedStarsMarkdown(&lines, chart);
    return lines.join("\n");
}

QString MainWindow::buildTransitConjunctionDetailsClipboardText() const {
    if (!hasTransitConjunctionSelection_ || !hasTransitChart_) {
        return QString();
    }
    const TransitConjunctionWindow& result = lastTransitConjunctionSelection_;
    const NatalChart& chart = currentTransitChart_;
    const QTimeZone displayTz = conjTz_.isValid() ? conjTz_ : QTimeZone::utc();
    const QDateTime localStart = result.startUtc.toTimeZone(displayTz);
    const QDateTime localEnd = result.endUtc.toTimeZone(displayTz);

    QStringList lines;
    lines << "Transit Conjunction Result Placements";
    lines << QString("Local Start: %1").arg(localStart.toString("yyyy-MM-dd HH:mm:ss"));
    lines << QString("Local End: %1").arg(localEnd.toString("yyyy-MM-dd HH:mm:ss"));
    lines << QString("UTC Start: %1").arg(result.startUtc.toString("yyyy-MM-dd HH:mm:ss"));
    lines << QString("UTC End: %1").arg(result.endUtc.toString("yyyy-MM-dd HH:mm:ss"));
    lines << QString("Timezone: %1").arg(result.tzLabel.isEmpty() ? "UTC" : result.tzLabel);
    lines << QString("Sign/House: %1").arg(result.bucketLabel);
    lines << QString("Count: %1").arg(result.clusterCount);
    lines << QString("Planets: %1").arg((result.orbClusterAtStart.isEmpty()
        ? result.planetsInBucketAtStart
        : result.orbClusterAtStart).join(", "));
    lines << QString("Span: %1").arg(QString::number(result.clusterSpanDeg, 'f', 2));
    lines << "";
    lines << "| Body | Degree | Sign | House | Motion |";
    lines << "| --- | --- | --- | --- | --- |";

    QMap<QString, BodyPosition> bodyMap;
    for (const auto& body : chart.bodies) {
        bodyMap.insert(body.name, body);
    }
    auto appendBody = [&](const BodyPosition& body) {
        const QString degree = formatDegOnly(body.longitude);
        const QString sign = signName(signIndex(body.longitude));
        const QString house = body.house > 0 ? QString::number(body.house) : "-";
        const QString motion = body.retrograde ? "R" : "D";
        lines << QString("| %1 | %2 | %3 | %4 | %5 |")
            .arg(body.name, degree, sign, house, motion);
    };
    for (const auto& name : tropicalBodyOrder()) {
        if (!bodyMap.contains(name)) {
            continue;
        }
        appendBody(bodyMap.value(name));
        bodyMap.remove(name);
    }
    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        appendBody(it.value());
    }

    if (!chart.fixedStars.isEmpty()) {
        QVector<FixedStarPosition> stars = chart.fixedStars;
        std::sort(stars.begin(), stars.end(), [](const FixedStarPosition& a, const FixedStarPosition& b) {
            return a.longitude < b.longitude;
        });
        lines << "";
        lines << "| Fixed Star | Degree | Sign | House |";
        lines << "| --- | --- | --- | --- |";
        for (const auto& star : stars) {
            lines << QString("| %1 | %2 | %3 | %4 |")
                .arg(star.name)
                .arg(formatDegOnly(star.longitude))
                .arg(signName(signIndex(star.longitude)))
                .arg(star.house > 0 ? QString::number(star.house) : "-");
        }
    }
    return lines.join("\n");
}

QString MainWindow::buildTransitScanDetailsClipboardText() const {
    if (!hasTransitScanSelection_ || !hasTransitChart_) {
        return QString();
    }
    const DayScanResult& result = lastTransitScanSelection_;
    const NatalChart& chart = currentTransitChart_;
    const QString tzLabel = lastTransitScanSelectionTzLabel_.isEmpty() ? QString("UTC") : lastTransitScanSelectionTzLabel_;
    const QString localLabel = lastTransitScanSelectionLocal_.isValid()
        ? lastTransitScanSelectionLocal_.toString("yyyy-MM-dd HH:mm:ss")
        : QString("-");
    const QString aspectsText = result.topAspects.isEmpty() ? "No strong aspects" : result.topAspects.join(" | ");

    QStringList lines;
    lines << "Transit Scan Day Placements";
    lines << QString("Date: %1").arg(result.date.toString("yyyy-MM-dd"));
    lines << QString("Local Time: %1").arg(localLabel);
    lines << QString("Timezone: %1").arg(tzLabel);
    lines << QString("Net Score: %1").arg(QString::number(result.net, 'f', 2));
    lines << QString("Support Score: %1").arg(QString::number(result.support, 'f', 2));
    lines << QString("Challenge Score: %1").arg(QString::number(result.challenge, 'f', 2));
    lines << QString("Solar Return Bias: %1").arg(QString::number(result.solarBias, 'f', 2));
    lines << QString("Aspect Count: %1").arg(result.aspectCount);
    lines << QString("Top Aspects: %1").arg(aspectsText);
    lines << "";
    appendBodyPlacementsMarkdown(&lines, chart);
    appendFixedStarsMarkdown(&lines, chart);
    return lines.join("\n");
}

QString MainWindow::buildLunationDetailsClipboardText() const {
    if (!hasLunationSelection_ || !hasTransitChart_) {
        return QString();
    }

    const LunationResult& result = lastLunationSelection_;
    const NatalChart& chart = currentTransitChart_;
    QStringList lines;
    lines << "Lunation Details";
    lines << QString("Event: %1").arg(result.event);
    if (!result.eclipseType.isEmpty()) {
        lines << QString("Eclipse Type: %1").arg(result.eclipseType);
    }
    lines << QString("Local Time: %1").arg(result.timeLocal.toString("yyyy-MM-dd HH:mm:ss"));
    lines << QString("UTC Time: %1").arg(result.timeUtc.toString("yyyy-MM-dd HH:mm:ss"));
    lines << QString("Timezone: %1").arg(result.tzLabel);
    lines << QString("Sun: %1").arg(formatDegInSign(result.sunLon));
    lines << QString("Moon: %1").arg(formatDegInSign(result.moonLon));
    if (!chart.fixedStars.isEmpty()) {
        auto nearestStarLine = [&](double lon) -> QString {
            const FixedStarPosition* bestStar = nullptr;
            double bestOrb = 999.0;
            for (const auto& star : chart.fixedStars) {
                const double orb = angularDiffAbs(lon, star.longitude);
                if (!bestStar || orb < bestOrb) {
                    bestStar = &star;
                    bestOrb = orb;
                }
            }
            if (!bestStar) {
                return "-";
            }
            return QString("%1 (orb %2 deg)")
                .arg(bestStar->name)
                .arg(QString::number(bestOrb, 'f', 2));
        };
        lines << QString("Sun nearest fixed star: %1").arg(nearestStarLine(result.sunLon));
        lines << QString("Moon nearest fixed star: %1").arg(nearestStarLine(result.moonLon));
    }
    lines << "";
    lines << "Moment Placements:";

    QMap<QString, BodyPosition> bodyMap;
    for (const auto& body : chart.bodies) {
        bodyMap.insert(body.name, body);
    }
    for (const auto& name : tropicalBodyOrder()) {
        if (!bodyMap.contains(name)) {
            continue;
        }
        const auto body = bodyMap.value(name);
        const QString motion = body.retrograde ? " R" : "";
        const QString house = body.house > 0 ? QString(" (H%1%2)").arg(body.house).arg(motion)
                                             : (motion.isEmpty() ? QString() : QString(" (%1)").arg(motion.trimmed()));
        lines << QString("%1: %2%3").arg(body.name, formatDegInSign(body.longitude), house);
        bodyMap.remove(name);
    }
    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        const auto& body = it.value();
        const QString motion = body.retrograde ? " R" : "";
        const QString house = body.house > 0 ? QString(" (H%1%2)").arg(body.house).arg(motion)
                                             : (motion.isEmpty() ? QString() : QString(" (%1)").arg(motion.trimmed()));
        lines << QString("%1: %2%3").arg(body.name, formatDegInSign(body.longitude), house);
    }

    return lines.join("\n");
}

void MainWindow::refreshNatalReport() {
    if (!reportText_) {
        return;
    }
    if (!hasCurrentChart_) {
        reportText_->clear();
        return;
    }
    reportText_->setPlainText(buildNatalReportText());
}

QString MainWindow::buildNatalReportText() const {
    if (!hasCurrentChart_) {
        return QString();
    }
    const NatalChart* chartPtr = &currentChart_;
    const NatalInput* inputPtr = &currentInput_;
    QString reportTitle = "Natal Report";
    QString localTimeLabel = "Birth time (Local)";
    QString utcTimeLabel = "Birth time (UTC)";
    QString modeContext;
    QVector<HouseCusp> placidusCusps = natalPlacidusCusps_;

    if (activeTab_ == AppTab::Progression) {
        reportTitle = "Progression Report";
        localTimeLabel = "Chart time (Local)";
        utcTimeLabel = "Chart time (UTC)";
        if (progressionView_ != ProgressionView::NatalOnly && hasProgressionChart_) {
            chartPtr = &currentProgressionChart_;
            inputPtr = &currentProgressionInput_;
            modeContext = (progressionView_ == ProgressionView::Overlay)
                ? "Progressed (overlay mode)"
                : "Progressed";
            placidusCusps = currentProgressionChart_.cusps;
        } else {
            modeContext = "Natal (progression natal-only)";
        }
    }

    const NatalChart& chart = *chartPtr;
    const NatalInput& input = *inputPtr;
    QStringList lines;
    auto addRow = [&](const QStringList& cols) {
        lines << cols.join('\t');
    };
    lines << reportTitle;
    lines << "";
    lines << "Summary:";
    addRow({"Field", "Value"});
    addRow({"Name", input.name.isEmpty() ? "-" : input.name});
    addRow({"Gender", genderToString(input.gender)});
    addRow({"Location", currentLocation_.isEmpty() ? "-" : currentLocation_});
    addRow({localTimeLabel, chart.localDateTime.toString("yyyy-MM-dd HH:mm:ss")});
    addRow({utcTimeLabel, chart.utcDateTime.toString("yyyy-MM-dd HH:mm:ss")});
    addRow({"Latitude", QString::number(input.latitude, 'f', 6)});
    addRow({"Longitude", QString::number(input.longitude, 'f', 6)});
    addRow({"Timezone", chart.timezoneLabel.isEmpty() ? "-" : chart.timezoneLabel});
    addRow({"House system (UI)", input.houseSystem == HouseSystem::Placidus ? "Placidus" : "Whole Sign"});
    if (!modeContext.isEmpty()) {
        addRow({"Mode Context", modeContext});
    }
    addRow({"Mode", "Tropical"});
    addRow({"Day/Night", chart.isDayChart ? "Day" : "Night"});
    lines << "";
    lines << "Angles:";
    addRow({"Angle", "Deg in Sign", "Sign"});
    const auto angleRows = collectAngleAndLotRows(chart);
    for (const auto& row : angleRows) {
        addRow({row.label, formatDegOnly(row.lon), signName(signIndex(row.lon))});
    }

    lines << "";
    lines << "Planets:";
    addRow({"Body", "Deg in Sign", "Sign", "House (Whole)", "House (Placidus)", "Motion", "Element", "Mode", "Dignity"});
    QMap<QString, BodyPosition> bodyMap;
    for (const auto& body : chart.bodies) {
        bodyMap.insert(body.name, body);
    }
    const bool hasPlacidusCusps = (placidusCusps.size() == 12);
    for (const auto& name : tropicalBodyOrder()) {
        if (!bodyMap.contains(name)) {
            continue;
        }
        const auto& body = bodyMap[name];
        const QString motion = body.retrograde ? "Retrograde" : "Direct";
        const int houseWhole = calcHouseForLongitude(body.longitude, {}, chart.angles.asc, HouseSystem::WholeSign);
        const int housePlacidus = hasPlacidusCusps
            ? calcHouseForLongitude(body.longitude, placidusCusps, chart.angles.asc, HouseSystem::Placidus)
            : 0;
        addRow({
            body.name,
            formatDegOnly(body.longitude),
            body.signName,
            QString::number(houseWhole),
            housePlacidus > 0 ? QString::number(housePlacidus) : "-",
            motion,
            body.element,
            body.mode,
            body.dignity
        });
    }

    lines << "";
    lines << "Fixed Stars:";
    if (chart.fixedStars.isEmpty()) {
        addRow({"Info", "No fixed star data."});
    } else {
        addRow({"Star", "Deg in Sign", "Sign", "House"});
        QVector<FixedStarPosition> stars = chart.fixedStars;
        std::sort(stars.begin(), stars.end(), [](const FixedStarPosition& a, const FixedStarPosition& b) {
            return a.longitude < b.longitude;
        });
        for (const auto& star : stars) {
            addRow({
                star.name,
                formatDegOnly(star.longitude),
                star.signName.isEmpty() ? signName(signIndex(star.longitude)) : star.signName,
                star.house > 0 ? QString::number(star.house) : "-"
            });
        }
    }

    lines << "";
    lines << "Houses (Whole Sign):";
    addRow({"House", "Sign"});
    const int ascIdx = signIndex(chart.angles.asc);
    for (int i = 0; i < 12; ++i) {
        const int signIdx = (ascIdx + i) % 12;
        addRow({QString::number(i + 1), signName(signIdx)});
    }

    lines << "";
    lines << "Houses (Placidus Cusps):";
    if (hasPlacidusCusps) {
        addRow({"House", "Cusp Deg", "Sign"});
        for (const auto& cusp : placidusCusps) {
            addRow({
                QString::number(cusp.number),
                formatDegOnly(cusp.longitude),
                cusp.signName
            });
        }
    } else {
        addRow({"Info", "Placidus cusps unavailable."});
    }

    return lines.join("\n");
}

QString MainWindow::buildAspectsClipboardText() const {
    QStringList lines;
    const QChar degSymbol(0x00B0);
    const QString prefixRelocation = "Relocation";
    const QString prefixNatal = QString::fromUtf8(u8"ɴᴀᴛᴀʟ");
    const QString prefixTransit = QString::fromUtf8(u8"ᴛʀᴀɴsɪᴛ");
    const QString prefixProgressed = QString::fromUtf8(u8"ᴘʀᴏɢʀᴇssᴇᴅ");
    const QString prefixSolar = QString::fromUtf8(u8"sᴏʟᴀʀ");
    auto formatOrb = [](double v) {
        return QString::number(v, 'f', 1);
    };
    auto houseSystemLabel = [](HouseSystem system) {
        return system == HouseSystem::Placidus ? "Placidus" : "Whole Sign";
    };
    auto formatCell = [&](const QString& label, double orb) {
        return QString("%1 (%2%3)").arg(label).arg(formatOrb(orb)).arg(degSymbol);
    };

    QString contextLabel = "Aspect Matrix";
    HouseSystem contextHouseSystem = currentInput_.houseSystem;

    if (activeTab_ == AppTab::Natal) {
        contextLabel = "Natal aspects";
        lines << QString("Birth time (Local): %1").arg(currentChart_.localDateTime.toString("yyyy-MM-dd HH:mm:ss"));
        lines << QString("Timezone: %1").arg(currentChart_.timezoneLabel);
    } else if (activeTab_ == AppTab::Transits) {
        auto overlayLabel = [&]() {
            switch (transitAspectView_) {
                case TransitAspectView::TransitTransit:
                    return QString("Transit-Transit");
                case TransitAspectView::NatalNatal:
                    return QString("Natal-Natal");
                case TransitAspectView::TransitNatal:
                default:
                    return QString("Transit-Natal");
            }
        };
        if (transitSubTab_ == TransitSubTab::Lunations && hasLunationSelection_) {
            if (transitMode_ == TransitMode::NatalOverlay) {
                contextLabel = QString("Transits (Lunations: %1, Overlay: %2)")
                    .arg(lastLunationSelection_.event, overlayLabel());
            } else {
                contextLabel = QString("Transits (Lunations: %1, Transit-only)").arg(lastLunationSelection_.event);
                contextHouseSystem = transitHouseSystem_;
            }
            lines << QString("Lunation event: %1").arg(lastLunationSelection_.event);
            lines << QString("Lunation time: %1 (%2)")
                .arg(lastLunationSelection_.timeLocal.toString("yyyy-MM-dd HH:mm:ss"))
                .arg(lastLunationSelection_.tzLabel);
            if (!lastLunationSelection_.eclipseType.isEmpty()) {
                lines << QString("Eclipse type: %1").arg(lastLunationSelection_.eclipseType);
            }
            lines << QString("Sun: %1").arg(formatDegInSign(lastLunationSelection_.sunLon));
            lines << QString("Moon: %1").arg(formatDegInSign(lastLunationSelection_.moonLon));
        } else {
            if (transitMode_ == TransitMode::NatalOverlay) {
                switch (transitAspectView_) {
                    case TransitAspectView::TransitTransit:
                        contextLabel = "Transits (Overlay: Transit-Transit)";
                        break;
                    case TransitAspectView::NatalNatal:
                        contextLabel = "Transits (Overlay: Natal-Natal)";
                        break;
                    case TransitAspectView::TransitNatal:
                    default:
                        contextLabel = "Transits (Overlay: Transit-Natal)";
                        break;
                }
            } else {
                contextLabel = "Transits (Transit-only)";
                contextHouseSystem = transitHouseSystem_;
            }
            const QDateTime target = transitSelectedLocal();
            if (target.isValid()) {
                lines << QString("Transit target: %1 (%2)")
                    .arg(target.toString("yyyy-MM-dd hh:mm:ss AP"))
                    .arg(transitTimezoneLabel());
            }
        }
    } else if (activeTab_ == AppTab::Progression) {
        if (progressionView_ == ProgressionView::NatalOnly) {
            contextLabel = "Progression (Natal-only)";
        } else if (progressionView_ == ProgressionView::Overlay) {
            contextLabel = "Progression (Overlay: Progressed-Natal)";
            contextHouseSystem = currentProgressionInput_.houseSystem;
        } else {
            contextLabel = "Progression (Progressed-only)";
            contextHouseSystem = currentProgressionInput_.houseSystem;
        }
        const QDateTime target = progressionTargetLocal();
        if (target.isValid()) {
            lines << QString("Progression target: %1 (%2)")
                .arg(target.toString("yyyy-MM-dd hh:mm:ss AP"))
                .arg(progressionTimezoneLabel());
        }
    } else if (activeTab_ == AppTab::SolarReturn) {
        if (solarAspectView_ == SolarAspectView::SolarNatal) {
            contextLabel = "Solar Return (Solar-Natal)";
        } else {
            contextLabel = "Solar Return";
        }
        contextHouseSystem = currentSolarInput_.houseSystem;
        if (solarYearSpin_) {
            lines << QString("Solar return year: %1").arg(solarYearSpin_->value());
        }
        if (!currentSolarLocation_.isEmpty()) {
            lines << QString("Solar location: %1").arg(currentSolarLocation_);
        }
        if (hasSolarChart_) {
            lines << QString("Solar return time: %1 (%2)")
                .arg(currentSolarChart_.localDateTime.toString("yyyy-MM-dd HH:mm:ss"))
                .arg(currentSolarChart_.timezoneLabel);
        }
    } else if (activeTab_ == AppTab::Relocation) {
        if (relocationAspectView_ == RelocationAspectView::RelocationNatal) {
            contextLabel = "Relocation (Relocation-Natal)";
        } else {
            contextLabel = "Relocation";
        }
        contextHouseSystem = currentRelocationInput_.houseSystem;
        if (!currentRelocationLocation_.isEmpty()) {
            lines << QString("Relocation location: %1").arg(currentRelocationLocation_);
        }
        if (hasRelocationChart_) {
            lines << QString("Relocation time: %1 (%2)")
                .arg(currentRelocationChart_.localDateTime.toString("yyyy-MM-dd HH:mm:ss"))
                .arg(currentRelocationChart_.timezoneLabel);
        }
    }
    lines.prepend(QString("Context: %1").arg(contextLabel));
    lines << QString("House system: %1").arg(houseSystemLabel(contextHouseSystem));
    lines << QString("Orbs: Conjunction %1%6, Sextile %2%6, Square %3%6, Trine %4%6, Opposition %5%6")
        .arg(formatOrb(aspectOrbs_.conjunction))
        .arg(formatOrb(aspectOrbs_.sextile))
        .arg(formatOrb(aspectOrbs_.square))
        .arg(formatOrb(aspectOrbs_.trine))
        .arg(formatOrb(aspectOrbs_.opposition))
        .arg(degSymbol);
    if (aspectDisplayMaxOrb_ > 0.0) {
        lines << QString("Max orb filter: %1%2").arg(formatOrb(aspectDisplayMaxOrb_)).arg(degSymbol);
    } else {
        lines << "Max orb filter: none";
    }
    lines << "";

    auto appendMatrix = [&](const QStringList& rowNames, const QStringList& colNames,
                            const QStringList& rowLabels, const QStringList& colLabels,
                            const std::function<QString(const QString&, const QString&)>& cellFor) {
        if (rowNames.isEmpty() || colNames.isEmpty()) {
            lines << "No aspects available.";
            return;
        }
        auto escapeCell = [](const QString& value) {
            QString out = value;
            out.replace('|', "\\|");
            out.replace('\n', ' ');
            return out;
        };
        const QStringList useRowLabels =
            (!rowLabels.isEmpty() && rowLabels.size() == rowNames.size()) ? rowLabels : rowNames;
        const QStringList useColLabels =
            (!colLabels.isEmpty() && colLabels.size() == colNames.size()) ? colLabels : colNames;
        QStringList headerCells;
        headerCells << "";
        for (const auto& name : useColLabels) {
            headerCells << escapeCell(name);
        }
        lines << QString("| %1 |").arg(headerCells.join(" | "));
        QStringList separator;
        separator.reserve(headerCells.size());
        for (int i = 0; i < headerCells.size(); ++i) {
            separator << "---";
        }
        lines << QString("| %1 |").arg(separator.join(" | "));
        for (int rowIndex = 0; rowIndex < rowNames.size(); ++rowIndex) {
            const QString& rowName = rowNames[rowIndex];
            QStringList rowCells;
            rowCells << escapeCell(useRowLabels.value(rowIndex, rowName));
            for (const auto& colName : colNames) {
                rowCells << escapeCell(cellFor(rowName, colName));
            }
            lines << QString("| %1 |").arg(rowCells.join(" | "));
        }
    };

    auto appendChartMatrix = [&](const NatalChart& chart, const QString& prefix) {
        const auto& grid = chart.aspects;
        if (grid.bodyOrder.isEmpty() || grid.cells.isEmpty()) {
            lines << "No aspects available.";
            return;
        }
        QStringList names;
        names.reserve(grid.bodyOrder.size());
        for (const auto& name : grid.bodyOrder) {
            if (isAsteroidBody(name) && (!includeAsteroidAspects_ || !isAsteroidVisible(name))) {
                continue;
            }
            names << name;
        }
        QStringList labels;
        labels.reserve(names.size());
        for (const auto& name : names) {
            labels << (prefix.isEmpty() ? name : QString("%1 %2").arg(prefix, name));
        }
        appendMatrix(names, names, labels, labels, [&](const QString& rowName, const QString& colName) -> QString {
            const int r = names.indexOf(rowName);
            const int c = names.indexOf(colName);
            if (r < 0 || c < 0 || r == c) {
                return QString();
            }
            if (r >= grid.cells.size() || c >= grid.cells[r].size()) {
                return QString();
            }
            const auto& cell = grid.cells[r][c];
            if (!cell.hasAspect) {
                return QString();
            }
            if (aspectDisplayMaxOrb_ > 0.0 && cell.orb > aspectDisplayMaxOrb_) {
                return QString();
            }
            return formatCell(cell.label, cell.orb);
        });
    };

    auto appendOverlayMatrix = [&](const NatalChart& rowChart, const NatalChart& colChart, bool includeAngles,
                                   const QString& rowPrefix, const QString& colPrefix) {
        QMap<QString, double> rowMap;
        for (const auto& body : rowChart.bodies) {
            rowMap.insert(body.name, body.longitude);
        }
        QMap<QString, double> colMap;
        for (const auto& body : colChart.bodies) {
            colMap.insert(body.name, body.longitude);
        }
        if (includeAngles) {
            colMap.insert("Ascendant", colChart.angles.asc);
            colMap.insert("Midheaven", colChart.angles.mc);
            colMap.insert("Descendant", colChart.angles.desc);
            colMap.insert("IC", colChart.angles.ic);
        }

        QStringList rowNames;
        QStringList colNames;
        for (const auto& name : tropicalBodyOrder()) {
            if (isAsteroidBody(name) && (!includeAsteroidAspects_ || !isAsteroidVisible(name))) {
                continue;
            }
            if (rowMap.contains(name)) {
                rowNames.push_back(name);
            }
            if (colMap.contains(name)) {
                colNames.push_back(name);
            }
        }
        QStringList rowLabels;
        rowLabels.reserve(rowNames.size());
        for (const auto& name : rowNames) {
            rowLabels << (rowPrefix.isEmpty() ? name : QString("%1 %2").arg(rowPrefix, name));
        }
        QStringList colLabels;
        colLabels.reserve(colNames.size());
        for (const auto& name : colNames) {
            colLabels << (colPrefix.isEmpty() ? name : QString("%1 %2").arg(colPrefix, name));
        }
        appendMatrix(rowNames, colNames, rowLabels, colLabels, [&](const QString& rowName, const QString& colName) -> QString {
            if (!rowMap.contains(rowName) || !colMap.contains(colName)) {
                return QString();
            }
            const double diff = angularDiff(rowMap.value(rowName), colMap.value(colName));
            QString label;
            double orb = 0.0;
            double maxOrb = 0.0;
            if (!aspectForDiff(diff, aspectOrbs_, &label, &orb, &maxOrb)) {
                return QString();
            }
            if (aspectDisplayMaxOrb_ > 0.0 && orb > aspectDisplayMaxOrb_) {
                return QString();
            }
            return formatCell(label, orb);
        });
    };

    if (activeTab_ == AppTab::Natal) {
        appendChartMatrix(currentChart_, prefixNatal);
    } else if (activeTab_ == AppTab::Transits) {
        if (transitMode_ == TransitMode::NatalOverlay) {
            switch (transitAspectView_) {
                case TransitAspectView::TransitTransit:
                    appendChartMatrix(currentTransitChart_, prefixTransit);
                    break;
                case TransitAspectView::NatalNatal:
                    appendChartMatrix(currentChart_, prefixNatal);
                    break;
                case TransitAspectView::TransitNatal:
                default:
                    appendOverlayMatrix(currentTransitChart_, currentChart_, true, prefixTransit, prefixNatal);
                    break;
            }
        } else {
            appendChartMatrix(currentTransitChart_, prefixTransit);
        }
    } else if (activeTab_ == AppTab::Progression) {
        if (progressionView_ == ProgressionView::NatalOnly) {
            appendChartMatrix(currentChart_, prefixNatal);
        } else if (progressionView_ == ProgressionView::Overlay) {
            appendOverlayMatrix(currentProgressionChart_, currentChart_, true, prefixProgressed, prefixNatal);
        } else {
            appendChartMatrix(currentProgressionChart_, prefixProgressed);
        }
    } else if (activeTab_ == AppTab::SolarReturn) {
        if (solarAspectView_ == SolarAspectView::SolarNatal) {
            appendOverlayMatrix(currentSolarChart_, currentChart_, false, prefixSolar, prefixNatal);
        } else {
            appendChartMatrix(currentSolarChart_, prefixSolar);
        }
    } else if (activeTab_ == AppTab::Relocation) {
        if (relocationAspectView_ == RelocationAspectView::RelocationNatal) {
            appendOverlayMatrix(currentRelocationChart_, currentChart_, true, prefixRelocation, prefixNatal);
        } else {
            appendChartMatrix(currentRelocationChart_, prefixRelocation);
        }
    }
    return lines.join("\n");
}

void MainWindow::applyTransitSearchResult(const TransitSearchResult& result) {
    hasTransitSearchSelection_ = false;
    NatalChart chart;
    QString err;
    if (!computeTransitChartAt(result.timeLocal, result.tzLabel, &chart, &err)) {
        updateLunationCopyButtonState();
        setStatusMessage(err);
        return;
    }
    currentTransitChart_ = chart;
    hasTransitChart_ = true;
    hasTransitSearchSelection_ = true;
    lastTransitSearchSelection_ = result;
    transitPending_ = false;
    lastTransitCalculated_ = QDateTime::currentDateTime();

    if (chartWheel_) {
        if (transitMode_ == TransitMode::NatalOverlay && hasCurrentChart_) {
            chartWheel_->setShowAspects(true);
            chartWheel_->setOverlayLabel("Transit");
            chartWheel_->setOverlayCharts(currentChart_, chart, transitHouseSystem_, aspectOrbs_);
            chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
        } else {
            chartWheel_->setTransitChart(chart, transitHouseSystem_);
        }
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        chartWheel_->setHighlight(result.planet, true, result.event, QColor("#f0c24b"));
    }
    showTransitSearchDetails(result);
}

void MainWindow::updateTransitSearchVisibility() {
    if (activeTab_ != AppTab::Transits) {
        if (chartWheel_) {
            chartWheel_->clearHighlight();
        }
        updateLunationCopyButtonState();
        return;
    }
    const bool inSearch = (transitSubTab_ == TransitSubTab::Search);
    const bool inCalendar = (transitSubTab_ == TransitSubTab::Calendar);
    const bool inConjunctions = (transitSubTab_ == TransitSubTab::Conjunctions);
    const bool inScan = (transitSubTab_ == TransitSubTab::Scan);
    const bool inProfections = (transitSubTab_ == TransitSubTab::Profections);
    const bool inLunations = (transitSubTab_ == TransitSubTab::Lunations);
    if (rightTopDock_) {
        if (inSearch) {
            rightTopDock_->setWindowTitle("Search Results");
        } else if (inCalendar) {
            rightTopDock_->setWindowTitle("Calendar Events");
        } else if (inConjunctions) {
            rightTopDock_->setWindowTitle("Conjunction Results");
        } else if (inScan) {
            rightTopDock_->setWindowTitle("Scan Results");
        } else if (inProfections) {
            rightTopDock_->setWindowTitle("Activated Points");
        } else if (inLunations) {
            rightTopDock_->setWindowTitle("Lunation Results");
        } else {
            rightTopDock_->setWindowTitle("Transits");
        }
    }
    if (rightBottomDock_) {
        if (inSearch) {
            rightBottomDock_->setWindowTitle("Result Details");
        } else if (inCalendar) {
            rightBottomDock_->setWindowTitle("Event Details");
        } else if (inConjunctions) {
            rightBottomDock_->setWindowTitle("Conjunction Details");
        } else if (inScan) {
            rightBottomDock_->setWindowTitle("Scan Details");
        } else if (inProfections) {
            rightBottomDock_->setWindowTitle("Topical Analysis");
        } else if (inLunations) {
            rightBottomDock_->setWindowTitle("Lunation Details");
        } else {
            rightBottomDock_->setWindowTitle("Ingress Countdown");
        }
    }
    if (inSearch) {
        showTransitSearchResults();
    } else if (inCalendar) {
        showTransitCalendarResults();
    } else if (inConjunctions) {
        showTransitConjunctionResults();
    } else if (inScan) {
        refreshTransitScanTab();
    } else if (inProfections) {
        refreshTransitProfectionTab();
    } else if (inLunations) {
        showLunationResults();
    } else if (activeTab_ == AppTab::Transits) {
        refreshTransitsTab();
    }
    if (!inSearch && !inCalendar && !inConjunctions && chartWheel_) {
        chartWheel_->clearHighlight();
    }
    if (inLunations) {
        updateLunationModeAvailability();
    }
    updateLunationCopyButtonState();

    const QString eventType = searchEventCombo_ ? searchEventCombo_->currentText() : QString();
    const bool isSignEvent = eventType.contains("Sign", Qt::CaseInsensitive);
    const bool isHouseEvent = eventType.contains("House", Qt::CaseInsensitive);
    const bool isAspectEvent = eventType.contains("Aspect", Qt::CaseInsensitive);
    const bool isDegreeEvent = eventType.contains("Degree", Qt::CaseInsensitive);
    const bool isStationEvent = eventType.contains("Station", Qt::CaseInsensitive);
    const bool canUseNatalTargets = (transitMode_ == TransitMode::NatalOverlay && hasCurrentChart_);
    const bool showNatalTarget = canUseNatalTargets && (isAspectEvent || isHouseEvent);
    const bool findNext = searchModeNextRadio_ && searchModeNextRadio_->isChecked();
    const bool findPrev = searchModePrevRadio_ && searchModePrevRadio_->isChecked();
    const bool findMode = findNext || findPrev;

    if (searchSignCombo_) {
        searchSignCombo_->setEnabled(isSignEvent);
    }
    if (searchHouseCombo_) {
        searchHouseCombo_->setEnabled(isHouseEvent);
    }
    if (searchAspectCombo_) {
        searchAspectCombo_->setEnabled(isAspectEvent);
    }
    if (searchOrbSpin_) {
        searchOrbSpin_->setEnabled(isAspectEvent || isDegreeEvent);
    }
    if (searchRangeModeCombo_) {
        searchRangeModeCombo_->setEnabled(!findMode);
    }
    if (searchEndDateEdit_) {
        searchEndDateEdit_->setEnabled(!findMode);
    }
    if (searchEndTimeEdit_) {
        searchEndTimeEdit_->setEnabled(!findMode);
    }
    if (searchTargetLabel_) {
        searchTargetLabel_->setText(isHouseEvent ? "House Of Natal" : "Natal Target");
        searchTargetLabel_->setVisible(showNatalTarget);
    }
    if (searchTargetCombo_) {
        searchTargetCombo_->setEnabled(showNatalTarget);
        searchTargetCombo_->setVisible(showNatalTarget);
    }
    if (searchDegreeLabel_) {
        searchDegreeLabel_->setVisible(isDegreeEvent);
    }
    if (searchDegreeSpin_) {
        searchDegreeSpin_->setEnabled(isDegreeEvent);
        searchDegreeSpin_->setVisible(isDegreeEvent);
    }
    if (searchDegreeSignLabel_) {
        searchDegreeSignLabel_->setVisible(isDegreeEvent);
    }
    if (searchDegreeSignCombo_) {
        searchDegreeSignCombo_->setEnabled(isDegreeEvent);
        searchDegreeSignCombo_->setVisible(isDegreeEvent);
    }
    if (isStationEvent) {
        if (searchSignCombo_) searchSignCombo_->setEnabled(false);
        if (searchHouseCombo_) searchHouseCombo_->setEnabled(false);
        if (searchAspectCombo_) searchAspectCombo_->setEnabled(false);
        if (searchOrbSpin_) searchOrbSpin_->setEnabled(false);
        if (searchTargetCombo_) searchTargetCombo_->setEnabled(false);
        if (searchDegreeSpin_) searchDegreeSpin_->setEnabled(false);
        if (searchDegreeSignCombo_) searchDegreeSignCombo_->setEnabled(false);
    }
}

// Astrocartography: guarded implementation so core app builds without QtLocation.
void MainWindow::updateAstrocartographyModeUi() {
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    const bool geodeticActive = (astroModeCombo_ && astroModeCombo_->currentIndex() == 0);
    if (astroModeHintLabel_) {
        astroModeHintLabel_->setVisible(!geodeticActive);
    }
    if (geodeticGroup_) {
        geodeticGroup_->setEnabled(geodeticActive);
    }
    if (geodeticOrbCombo_) {
        const bool orbEnabled = geodeticActive && geodeticOrbRadio_ && geodeticOrbRadio_->isChecked();
        geodeticOrbCombo_->setEnabled(orbEnabled);
    }
    if (!geodeticActive && geodeticStatusLabel_) {
        geodeticStatusLabel_->setText("Astrocartography mode will use loaded charts.");
    }
#else
    if (geodeticStatusLabel_) {
        geodeticStatusLabel_->setText("Astrocartography disabled (QtLocation not installed).");
    }
#endif
}

// Astrocartography: map update entry-point (kept guarded).
void MainWindow::updateAstrocartographyView() {
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    if (activeTab_ != AppTab::Astrocartography) {
        return;
    }
    updateAstrocartographyModeUi();
    if (!worldMapView_) {
        return;
    }
    if (!worldMapReady_ || !worldMapRoot_) {
        if (geodeticStatusLabel_) {
            geodeticStatusLabel_->setText("Loading map...");
        }
        return;
    }
    const bool geodeticActive = (astroModeCombo_ && astroModeCombo_->currentIndex() == 0);
    if (!geodeticActive) {
        setWorldMapOverlays({}, {});
        if (rightTopTable_ && rightBottomTable_) {
            setupTable(rightTopTable_, {"Info"}, 1);
            rightTopTable_->setItem(0, 0, makeCell("Astrocartography lines are not available yet."));
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Switch to Geodetic mode to view transit meridians."));
        }
        return;
    }
    updateGeodeticOverlays();
#else
    return;
#endif
}

// Geodetic overlays: depends on QtLocation/QML map.
void MainWindow::updateGeodeticOverlays() {
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    if (activeTab_ != AppTab::Astrocartography) {
        return;
    }
    if (!worldMapReady_ || !worldMapRoot_) {
        return;
    }
    if (!swe_.isLoaded() || ephePath_.isEmpty()) {
        setWorldMapOverlays({}, {});
        if (geodeticStatusLabel_) {
            geodeticStatusLabel_->setText("Swiss Ephemeris not loaded.");
        }
        if (rightTopTable_ && rightBottomTable_) {
            setupTable(rightTopTable_, {"Info"}, 1);
            rightTopTable_->setItem(0, 0, makeCell("Load Swiss Ephemeris data to view geodetic lines."));
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Place ephemeris files in the ephe folder."));
        }
        return;
    }

    const QDateTime local = transitSelectedLocal();
    if (!local.isValid()) {
        setWorldMapOverlays({}, {});
        if (geodeticStatusLabel_) {
            geodeticStatusLabel_->setText("Invalid transit time.");
        }
        return;
    }
    const QString tzLabel = transitTimezoneLabel();
    if (geodeticTimeLabel_) {
        geodeticTimeLabel_->setText(QString("Using transit time: %1 (%2)")
            .arg(local.toString("yyyy-MM-dd HH:mm:ss AP"), tzLabel));
    }

    const QDateTime utc = local.toUTC();
    const double hourDec = utc.time().hour() + utc.time().minute() / 60.0 + utc.time().second() / 3600.0
        + utc.time().msec() / 3600000.0;
    const double jd = swe_.julianDay(utc.date().year(), utc.date().month(), utc.date().day(), hourDec, SE_GREG_CAL);

    QStringList bodies = selectedCheckableItems(geodeticPlanetCombo_);
    if (bodies.isEmpty()) {
        setWorldMapOverlays({}, {});
        if (geodeticStatusLabel_) {
            geodeticStatusLabel_->setText("Select planets to display.");
        }
        if (rightTopTable_ && rightBottomTable_) {
            setupTable(rightTopTable_, {"Info"}, 1);
            rightTopTable_->setItem(0, 0, makeCell("No geodetic lines (no planets selected)."));
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Select planets in the Geodetic panel."));
        }
        return;
    }

    const bool exact = geodeticExactRadio_ && geodeticExactRadio_->isChecked();
    double orb = 0.0;
    if (!exact && geodeticOrbCombo_) {
        orb = geodeticOrbCombo_->currentData().toDouble();
    }

    QVariantList lineOverlays;
    QVariantList bandOverlays;

    struct GeoLineInfo {
        QString name;
        double longitude = 0.0;
        double displayLon = 0.0;
    };
    QVector<GeoLineInfo> linesInfo;
    linesInfo.reserve(bodies.size());

    auto addBandSegment = [&](double startLon, double endLon, const QColor& baseColor) {
        if (startLon > endLon) {
            std::swap(startLon, endLon);
        }
        QVariantList path;
        path << QVariant::fromValue(QGeoCoordinate(-85.0, startLon));
        path << QVariant::fromValue(QGeoCoordinate(85.0, startLon));
        path << QVariant::fromValue(QGeoCoordinate(85.0, endLon));
        path << QVariant::fromValue(QGeoCoordinate(-85.0, endLon));
        QVariantMap band;
        QColor fill = baseColor;
        fill.setAlphaF(0.18);
        QColor border = baseColor;
        border.setAlphaF(0.5);
        band["path"] = path;
        band["fillColor"] = fill.name(QColor::HexArgb);
        band["borderColor"] = border.name(QColor::HexArgb);
        band["borderWidth"] = 1;
        bandOverlays << band;
    };

    for (int i = 0; i < bodies.size(); ++i) {
        const QString name = bodies[i];
        const int bodyId = bodyIdForName(name);
        if (bodyId < 0) {
            continue;
        }
        QString calcErr;
        double lon = 0.0;
        if (!swe_.calcUt(jd, bodyId, 0, &lon, &calcErr)) {
            continue;
        }
        lon = normalizeDegrees(lon);
        if (name == "South Node") {
            lon = normalizeDegrees(lon + 180.0);
        }

        double displayLon = lon;
        if (displayLon > 180.0) {
            displayLon -= 360.0;
        }

        const QColor lineColor = geodeticColorForIndex(i);
        QVariantList path;
        path << QVariant::fromValue(QGeoCoordinate(-85.0, displayLon));
        path << QVariant::fromValue(QGeoCoordinate(85.0, displayLon));
        QVariantMap line;
        line["path"] = path;
        line["color"] = lineColor.name(QColor::HexArgb);
        line["width"] = 2;
        line["label"] = name;
        lineOverlays << line;

        if (orb > 0.0) {
            const double minLon = displayLon - orb;
            const double maxLon = displayLon + orb;
            if (minLon >= -180.0 && maxLon <= 180.0) {
                addBandSegment(minLon, maxLon, lineColor);
            } else if (minLon < -180.0) {
                addBandSegment(-180.0, maxLon, lineColor);
                addBandSegment(minLon + 360.0, 180.0, lineColor);
            } else if (maxLon > 180.0) {
                addBandSegment(minLon, 180.0, lineColor);
                addBandSegment(-180.0, maxLon - 360.0, lineColor);
            }
        }

        linesInfo.push_back({name, lon, displayLon});
    }

    setWorldMapOverlays(lineOverlays, bandOverlays);
    if (geodeticStatusLabel_) {
        geodeticStatusLabel_->setText(QString("Updated (%1 lines)").arg(linesInfo.size()));
    }

    if (rightTopTable_) {
        setupTable(rightTopTable_, {"Body", "Geodetic Degree", "Earth Lon"}, linesInfo.size());
        for (int i = 0; i < linesInfo.size(); ++i) {
            const auto& info = linesInfo[i];
            rightTopTable_->setItem(i, 0, makeCell(info.name));
            rightTopTable_->setItem(i, 1, makeCell(formatDegInSign(info.longitude), Qt::AlignRight | Qt::AlignVCenter));
            rightTopTable_->setItem(i, 2, makeCell(QString::number(info.displayLon, 'f', 2) + "°", Qt::AlignRight | Qt::AlignVCenter));
        }
    }
    if (rightBottomTable_) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Geodetic lines update from the current transit time."));
    }
#else
    return;
#endif
}

// Map bridge: writes overlays to QML root object.
void MainWindow::setWorldMapOverlays(const QVariantList& lineOverlays, const QVariantList& bandOverlays) {
#if defined(DRACOVED_ENABLE_ASTRO_MAP)
    if (!worldMapRoot_) {
        return;
    }
    worldMapRoot_->setProperty("lineOverlays", lineOverlays);
    worldMapRoot_->setProperty("bandOverlays", bandOverlays);
    worldMapRoot_->setProperty("markerOverlays", QVariantList{});
#else
    Q_UNUSED(lineOverlays);
    Q_UNUSED(bandOverlays);
#endif
}

void MainWindow::updateLunationModeAvailability() {
    const bool useRange = lunationModeRangeRadio_ && lunationModeRangeRadio_->isChecked();
    const bool useDegreeRange = lunationDegreeRangeCheck_ && lunationDegreeRangeCheck_->isChecked();
    if (lunationStartYearSpin_) {
        lunationStartYearSpin_->setEnabled(useRange);
    }
    if (lunationEndYearSpin_) {
        lunationEndYearSpin_->setEnabled(useRange);
    }
    if (lunationDegreeRangeStartSpin_) {
        lunationDegreeRangeStartSpin_->setEnabled(useDegreeRange);
    }
    if (lunationDegreeRangeEndSpin_) {
        lunationDegreeRangeEndSpin_->setEnabled(useDegreeRange);
    }
    if (lunationTimezoneLabel_) {
        QString tzLabel = currentInput_.timezone.trimmed();
        if (tzLabel.isEmpty()) {
            tzLabel = "UTC";
        }
        QTimeZone tz;
        QString normLabel;
        QString tzErr;
        if (!parseTimezoneInput(tzLabel, &tz, &normLabel, &tzErr)) {
            normLabel = "UTC";
        }
        lunationTimezoneLabel_->setText(QString("Timezone: %1").arg(normLabel));
    }
    updateLunationAnalysisAvailability();
}

void MainWindow::updateConjunctionModeAvailability() {
    const bool useRange = conjModeRangeRadio_ && conjModeRangeRadio_->isChecked();
    if (conjStartYearSpin_) {
        conjStartYearSpin_->setEnabled(useRange);
    }
    if (conjEndYearSpin_) {
        conjEndYearSpin_->setEnabled(useRange);
    }
    if (conjReferenceLabel_) {
        const QDateTime target = transitSelectedLocal();
        const QString tzLabel = transitTimezoneLabel();
        const QString targetText = target.isValid()
            ? target.toString("yyyy-MM-dd hh:mm:ss AP")
            : QString("-");
        conjReferenceLabel_->setText(QString("Reference: %1 (%2)").arg(targetText, tzLabel));
    }

    const int selectedPlanets = selectedCheckableItems(conjPlanetCombo_).size();
    const bool exactPairMode = (conjCountSpin_ && conjCountSpin_->value() == 2 && selectedPlanets == 2);
    if (conjUseOrbCheck_) {
        if (exactPairMode && conjUseOrbCheck_->isChecked()) {
            const QSignalBlocker blocker(conjUseOrbCheck_);
            conjUseOrbCheck_->setChecked(false);
        }
        conjUseOrbCheck_->setEnabled(!exactPairMode);
        conjUseOrbCheck_->setText(exactPairMode ? "Use orb span (disabled in exact pair mode)" : "Use orb span");
        conjUseOrbCheck_->setToolTip(exactPairMode
            ? "Exact two-planet conjunction mode always uses a 0 degree span."
            : "Expand conjunction windows by allowing an orb span.");
    }
    if (conjOrbSpin_) {
        const bool useOrb = !exactPairMode && conjUseOrbCheck_ && conjUseOrbCheck_->isChecked();
        conjOrbSpin_->setEnabled(useOrb);
        conjOrbSpin_->setToolTip(exactPairMode
            ? "Exact two-planet conjunction mode ignores orb span."
            : QString());
    }
}

void MainWindow::updateLunationAnalysisAvailability() {
    if (!lunationAnalysisCombo_) {
        return;
    }

    const int modeIndex = lunationAnalysisCombo_->currentIndex();
    if (modeIndex == 1) {
        lunationAnalysisMode_ = LunationAnalysisMode::RepeatedDegrees;
    } else if (modeIndex == 2) {
        lunationAnalysisMode_ = LunationAnalysisMode::TargetDegree;
    } else {
        lunationAnalysisMode_ = LunationAnalysisMode::List;
    }

    const bool analysisActive = (lunationAnalysisMode_ != LunationAnalysisMode::List);
    const bool targetMode = (lunationAnalysisMode_ == LunationAnalysisMode::TargetDegree);

    if (lunationModeRangeRadio_ && lunationModePrevRadio_ && lunationModeNextRadio_) {
        if (analysisActive) {
            const QSignalBlocker blockRange(lunationModeRangeRadio_);
            lunationModeRangeRadio_->setChecked(true);
            lunationModePrevRadio_->setEnabled(false);
            lunationModeNextRadio_->setEnabled(false);
        } else {
            lunationModePrevRadio_->setEnabled(true);
            lunationModeNextRadio_->setEnabled(true);
        }
    }

    if (lunationStartYearSpin_ && lunationEndYearSpin_) {
        const bool enableYears = analysisActive || (lunationModeRangeRadio_ && lunationModeRangeRadio_->isChecked());
        lunationStartYearSpin_->setEnabled(enableYears);
        lunationEndYearSpin_->setEnabled(enableYears);
    }

    if (lunationDegreeExactRadio_) {
        lunationDegreeExactRadio_->setEnabled(analysisActive);
    }
    if (lunationDegreeOrbRadio_) {
        lunationDegreeOrbRadio_->setEnabled(analysisActive);
    }
    if (lunationOrbCombo_) {
        const bool orbEnabled = analysisActive && lunationDegreeOrbRadio_ && lunationDegreeOrbRadio_->isChecked();
        lunationOrbCombo_->setEnabled(orbEnabled);
    }
    if (lunationMatchCombo_) {
        lunationMatchCombo_->setEnabled(analysisActive);
    }
    if (lunationSignModeCombo_) {
        lunationSignModeCombo_->setEnabled(true);
    }
    if (lunationSignCombo_) {
        lunationSignCombo_->setEnabled(true);
    }

    const bool hasNatal = hasCurrentChart_;
    if (lunationHouseModeCombo_) {
        lunationHouseModeCombo_->setEnabled(analysisActive && hasNatal);
    }
    if (lunationHouseCombo_) {
        lunationHouseCombo_->setEnabled(analysisActive && hasNatal);
    }
    if (lunationAnalysisHintLabel_) {
        lunationAnalysisHintLabel_->setVisible(analysisActive && !hasNatal);
    }

    if (lunationMatchCombo_ && !hasNatal) {
        const int matchIndex = lunationMatchCombo_->currentIndex();
        if (matchIndex == static_cast<int>(LunationMatchMode::DegreeHouse)
            || matchIndex == static_cast<int>(LunationMatchMode::DegreeSignHouse)) {
            lunationMatchCombo_->setCurrentIndex(static_cast<int>(LunationMatchMode::Degree));
        }
    }

    if (lunationTargetDegSpin_) {
        lunationTargetDegSpin_->setEnabled(analysisActive && targetMode);
    }
    if (lunationTargetMinSpin_) {
        lunationTargetMinSpin_->setEnabled(analysisActive && targetMode);
    }
    if (lunationTargetSecSpin_) {
        lunationTargetSecSpin_->setEnabled(analysisActive && targetMode);
    }

    if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Lunations && !lunationRunning_) {
        lunationAutoApplied_ = false;
        showLunationResults();
    }
}

void MainWindow::syncTransitProfectionAgeFromTransitDate() {
    if (!profectionAgeSpin_ || !hasCurrentChart_) {
        return;
    }
    const QDateTime referenceLocal = transitSelectedLocal();
    if (!referenceLocal.isValid()) {
        return;
    }
    const int ageYears = completedYearsBetween(currentChart_.localDateTime.date(), referenceLocal.date());
    const QSignalBlocker blocker(profectionAgeSpin_);
    profectionAgeSpin_->setValue(ageYears);
}

void MainWindow::refreshTransitProfectionTab() {
    if (activeTab_ != AppTab::Transits || transitSubTab_ != TransitSubTab::Profections) {
        return;
    }
    if (!rightTopTable_ || !rightBottomTable_) {
        return;
    }

    const QDateTime referenceLocal = transitSelectedLocal();
    const QString tzLabel = transitTimezoneLabel();
    const QString referenceText = referenceLocal.isValid()
        ? referenceLocal.toString("yyyy-MM-dd hh:mm:ss AP")
        : QString("-");
    if (profectionReferenceLabel_) {
        profectionReferenceLabel_->setText(QString("Reference: %1 (%2)").arg(referenceText, tzLabel));
    }

    if (!hasCurrentChart_) {
        if (profectionStatusLabel_) {
            profectionStatusLabel_->setText("Load a natal chart first.");
        }
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Load a natal chart to evaluate annual profections."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Topical activation appears here after a natal chart is loaded."));
        return;
    }
    if (!referenceLocal.isValid()) {
        if (profectionStatusLabel_) {
            profectionStatusLabel_->setText("Invalid transit reference.");
        }
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Set a valid transit date/time first."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Reference date/time is invalid."));
        return;
    }

    const int derivedAge = completedYearsBetween(currentChart_.localDateTime.date(), referenceLocal.date());
    if (profectionAgeSpin_ && profectionAgeSpin_->value() == 0 && derivedAge > 0) {
        const QSignalBlocker blocker(profectionAgeSpin_);
        profectionAgeSpin_->setValue(derivedAge);
    }
    const int ageYears = profectionAgeSpin_ ? profectionAgeSpin_->value() : derivedAge;
    const int ageMod = ((ageYears % 12) + 12) % 12;

    NatalChart transitChart;
    QString transitErr;
    if (!computeTransitChart(referenceLocal, tzLabel, &transitChart, &transitErr)) {
        if (profectionStatusLabel_) {
            profectionStatusLabel_->setText("Transit data unavailable.");
        }
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Unable to compute transit triggers for this reference time."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell(transitErr.isEmpty() ? "Unknown transit calculation error." : transitErr));
        return;
    }

    QMap<QString, BodyPosition> natalBodies;
    for (const auto& body : currentChart_.bodies) {
        natalBodies.insert(body.name, body);
    }

    double natalSunLon = 0.0;
    const bool hasNatalSun = findBodyLongitude(currentChart_, "Sun", &natalSunLon);

    struct StartPointDef {
        QString label;
        double lon = 0.0;
        bool valid = false;
    };

    double fortuneLon = currentChart_.partOfFortune;
    bool hasFortune = currentChart_.hasPartOfFortune;
    if (findBodyLongitude(currentChart_, "Part of Fortune", &fortuneLon)) {
        hasFortune = true;
    }
    double spiritLon = 0.0;
    const bool hasSpirit = findBodyLongitude(currentChart_, "Lot of Spirit", &spiritLon);
    double sunLon = 0.0;
    const bool hasSun = findBodyLongitude(currentChart_, "Sun", &sunLon);
    double moonLon = 0.0;
    const bool hasMoon = findBodyLongitude(currentChart_, "Moon", &moonLon);

    QVector<StartPointDef> startPoints = {
        {"Ascendant", currentChart_.angles.asc, true},
        {"Part of Fortune", fortuneLon, hasFortune},
        {"Sun", sunLon, hasSun},
        {"Moon", moonLon, hasMoon},
        {"Lot of Spirit", spiritLon, hasSpirit},
        {"Midheaven", currentChart_.angles.mc, true},
    };

    auto loadTransitPlanet = [&](const QString& planetName, double* outLon, int* outSign) {
        double lon = 0.0;
        if (!findBodyLongitude(transitChart, planetName, &lon)) {
            return false;
        }
        if (outLon) {
            *outLon = lon;
        }
        if (outSign) {
            *outSign = signIndex(lon);
        }
        return true;
    };

    double marsLon = 0.0;
    double saturnLon = 0.0;
    double jupiterLon = 0.0;
    double venusLon = 0.0;
    int marsSign = -1;
    int saturnSign = -1;
    int jupiterSign = -1;
    int venusSign = -1;
    const bool hasTransitMars = loadTransitPlanet("Mars", &marsLon, &marsSign);
    const bool hasTransitSaturn = loadTransitPlanet("Saturn", &saturnLon, &saturnSign);
    const bool hasTransitJupiter = loadTransitPlanet("Jupiter", &jupiterLon, &jupiterSign);
    const bool hasTransitVenus = loadTransitPlanet("Venus", &venusLon, &venusSign);

    struct ActivationRow {
        QString startLabel;
        QString natalSign;
        QString signOfYear;
        int signOfYearIndex = -1;
        QString lord;
        int lordHouse = 0;
        bool hasLord = false;
        bool lordRetrograde = false;
        bool lordCombust = false;
        QString natalPromise;
        QString transitTone;
        QString triggerSummary;
        QStringList triggerDetails;
        bool hasMaleficInSign = false;
        bool hasMaleficPressure = false;
        bool hasBeneficSupport = false;
        bool hasMaleficToLord = false;
    };

    QVector<ActivationRow> rows;
    rows.reserve(startPoints.size());
    for (const auto& point : startPoints) {
        if (!point.valid) {
            continue;
        }
        ActivationRow row;
        row.startLabel = point.label;
        const int natalSignIndex = signIndex(point.lon);
        row.natalSign = signName(natalSignIndex);
        row.signOfYearIndex = (natalSignIndex + ageMod) % 12;
        row.signOfYear = signName(row.signOfYearIndex);
        row.lord = traditionalRulerForSign(row.signOfYearIndex);

        double natalLordLon = 0.0;
        if (!row.lord.isEmpty() && natalBodies.contains(row.lord)) {
            const BodyPosition lordBody = natalBodies.value(row.lord);
            row.hasLord = true;
            row.lordHouse = lordBody.house;
            row.lordRetrograde = lordBody.retrograde;
            natalLordLon = lordBody.longitude;
            row.lordCombust = (row.lord != "Sun" && hasNatalSun && angularDiffAbs(natalLordLon, natalSunLon) <= 15.0);
        }

        if (!row.hasLord) {
            row.natalPromise = "Lord unavailable";
        } else {
            if (row.lordHouse == 1 || row.lordHouse == 4 || row.lordHouse == 7 || row.lordHouse == 10 || row.lordHouse == 11) {
                row.natalPromise = "Active/prominent";
            } else if (row.lordHouse == 6 || row.lordHouse == 8 || row.lordHouse == 12) {
                row.natalPromise = "Difficult/loss-prone";
            } else {
                row.natalPromise = "Mixed/moderate";
            }
            if (row.lordRetrograde) {
                row.natalPromise += ", retrograde";
            }
            if (row.lordCombust) {
                row.natalPromise += ", combust";
            }
        }

        auto addMaleficSignTrigger = [&](const QString& name, bool planetValid, int planetSign) {
            if (!planetValid) {
                return;
            }
            if (planetSign == row.signOfYearIndex) {
                row.hasMaleficInSign = true;
                row.triggerDetails.push_back(QString("%1 in sign of year").arg(name));
            } else if (isSquareOrOppSign(row.signOfYearIndex, planetSign)) {
                row.hasMaleficPressure = true;
                row.triggerDetails.push_back(QString("%1 square/opposition to sign of year").arg(name));
            }
        };
        auto addBeneficSignTrigger = [&](const QString& name, bool planetValid, int planetSign) {
            if (!planetValid) {
                return;
            }
            if (isConjOrTrineSign(row.signOfYearIndex, planetSign)) {
                row.hasBeneficSupport = true;
                row.triggerDetails.push_back(QString("%1 conjunction/trine to sign of year").arg(name));
            }
        };
        addMaleficSignTrigger("Mars", hasTransitMars, marsSign);
        addMaleficSignTrigger("Saturn", hasTransitSaturn, saturnSign);
        addBeneficSignTrigger("Jupiter", hasTransitJupiter, jupiterSign);
        addBeneficSignTrigger("Venus", hasTransitVenus, venusSign);

        if (row.hasLord) {
            auto addMaleficLordHit = [&](const QString& name, bool planetValid, double transitLon) {
                if (!planetValid) {
                    return;
                }
                QString aspectLabel;
                double orb = 0.0;
                if (hardAspectToLongitude(transitLon, natalLordLon, 3.0, &aspectLabel, &orb)) {
                    row.hasMaleficToLord = true;
                    row.triggerDetails.push_back(
                        QString("%1 %2 natal lord (%3° orb)")
                            .arg(name)
                            .arg(aspectLabel)
                            .arg(QString::number(orb, 'f', 1)));
                }
            };
            addMaleficLordHit("Mars", hasTransitMars, marsLon);
            addMaleficLordHit("Saturn", hasTransitSaturn, saturnLon);
        }

        if (row.hasMaleficInSign || row.hasMaleficToLord) {
            row.transitTone = "Critical";
        } else if (row.hasMaleficPressure && row.hasBeneficSupport) {
            row.transitTone = "Mixed";
        } else if (row.hasMaleficPressure) {
            row.transitTone = "Challenging";
        } else if (row.hasBeneficSupport) {
            row.transitTone = "Supportive";
        } else {
            row.transitTone = "Quiet";
        }
        row.triggerSummary = row.triggerDetails.isEmpty() ? "No major trigger" : row.triggerDetails.join("; ");
        rows.push_back(row);
    }

    if (rows.isEmpty()) {
        if (profectionStatusLabel_) {
            profectionStatusLabel_->setText("No start points available.");
        }
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Unable to resolve required points (Asc, Fortune, Sun, Moon, Spirit)."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Load/recompute the natal chart and try again."));
        return;
    }

    setupTable(rightTopTable_, {"Start Point", "Natal Sign", "Sign of Year", "Lord", "Lord House", "Natal Promise", "Transit Tone", "Triggers"}, rows.size());
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const auto& row = rows[rowIndex];
        rightTopTable_->setItem(rowIndex, 0, makeCell(row.startLabel));
        rightTopTable_->setItem(rowIndex, 1, makeCell(row.natalSign));
        rightTopTable_->setItem(rowIndex, 2, makeCell(row.signOfYear));
        rightTopTable_->setItem(rowIndex, 3, makeCell(row.lord.isEmpty() ? "-" : row.lord));
        rightTopTable_->setItem(rowIndex, 4, makeCell(row.lordHouse > 0 ? QString::number(row.lordHouse) : "-", Qt::AlignCenter));
        rightTopTable_->setItem(rowIndex, 5, makeCell(row.natalPromise));
        auto* toneItem = makeCell(row.transitTone);
        if (row.transitTone == "Supportive") {
            toneItem->setForeground(QColor("#69c36d"));
        } else if (row.transitTone == "Critical" || row.transitTone == "Challenging") {
            toneItem->setForeground(QColor("#e05555"));
        } else if (row.transitTone == "Mixed") {
            toneItem->setForeground(QColor("#d4a24a"));
        }
        rightTopTable_->setItem(rowIndex, 6, toneItem);
        auto* triggerItem = makeCell(row.triggerSummary);
        triggerItem->setToolTip(row.triggerSummary);
        rightTopTable_->setItem(rowIndex, 7, triggerItem);
    }
    rightTopTable_->resizeRowsToContents();

    auto findRow = [&](const QString& label) -> const ActivationRow* {
        for (const auto& row : rows) {
            if (row.startLabel == label) {
                return &row;
            }
        }
        return nullptr;
    };

    auto formatTopicAssessment = [&](const ActivationRow* row, bool wealthRule) -> QString {
        if (!row) {
            return "Unavailable";
        }
        QString text = QString("%1 year in %2; lord %3")
            .arg(row->startLabel)
            .arg(row->signOfYear)
            .arg(row->lord);
        if (row->lordHouse > 0) {
            text += QString(" in house %1").arg(row->lordHouse);
        }
        text += QString(" (%1). ").arg(row->natalPromise);
        if (wealthRule) {
            if (row->lordHouse == 2) {
                text += "2nd-house emphasis suggests gain/asset focus. ";
            } else if (row->lordHouse == 8) {
                text += "8th-house emphasis suggests loss/debt/shared-resource pressure. ";
            }
        }
        text += QString("Transit tone: %1.").arg(row->transitTone);
        return text;
    };

    const ActivationRow* ascRow = findRow("Ascendant");
    const ActivationRow* fortuneRow = findRow("Part of Fortune");
    const ActivationRow* spiritRow = findRow("Lot of Spirit");
    const ActivationRow* mcRow = findRow("Midheaven");
    const ActivationRow* sunRow = findRow("Sun");
    const ActivationRow* moonRow = findRow("Moon");

    struct TopicRow {
        QString topic;
        QString assessment;
        QString triggers;
    };
    QVector<TopicRow> topicRows;
    topicRows.push_back({
        "Reference",
        QString("Age %1 (mod 12 = %2) at %3.")
            .arg(ageYears)
            .arg(ageMod)
            .arg(referenceLocal.toString("yyyy-MM-dd hh:mm:ss AP")),
        QString("Timezone: %1").arg(tzLabel),
    });
    topicRows.push_back({
        "Health/Life (Asc)",
        formatTopicAssessment(ascRow, false),
        ascRow ? ascRow->triggerSummary : "Unavailable",
    });
    topicRows.push_back({
        "Career/Rank (Spirit)",
        formatTopicAssessment(spiritRow, false),
        spiritRow ? spiritRow->triggerSummary : "Unavailable",
    });
    topicRows.push_back({
        "Career/Rank (Midheaven)",
        formatTopicAssessment(mcRow, false),
        mcRow ? mcRow->triggerSummary : "Unavailable",
    });
    topicRows.push_back({
        "Wealth/Body (Fortune)",
        formatTopicAssessment(fortuneRow, true),
        fortuneRow ? fortuneRow->triggerSummary : "Unavailable",
    });
    topicRows.push_back({
        "Solar/Lunar Annuals",
        QString("Sun annual: %1 | Moon annual: %2")
            .arg(sunRow ? sunRow->signOfYear : QString("N/A"))
            .arg(moonRow ? moonRow->signOfYear : QString("N/A")),
        QString("Sun tone: %1 | Moon tone: %2")
            .arg(sunRow ? sunRow->transitTone : QString("N/A"))
            .arg(moonRow ? moonRow->transitTone : QString("N/A")),
    });
    topicRows.push_back({
        "Method Notes",
        "Combust threshold 15 degrees from natal Sun; malefic-to-lord hard aspect orb 3 degrees.",
        "Rules: angular/11th active; 6/8/12 difficult; benefic conjunction-trine helps.",
    });

    setupTable(rightBottomTable_, {"Topic", "Assessment", "Triggers"}, topicRows.size());
    for (int rowIndex = 0; rowIndex < topicRows.size(); ++rowIndex) {
        const auto& topic = topicRows[rowIndex];
        rightBottomTable_->setItem(rowIndex, 0, makeCell(topic.topic));
        auto* assessmentItem = makeCell(topic.assessment);
        assessmentItem->setToolTip(topic.assessment);
        rightBottomTable_->setItem(rowIndex, 1, assessmentItem);
        auto* triggerItem = makeCell(topic.triggers);
        triggerItem->setToolTip(topic.triggers);
        rightBottomTable_->setItem(rowIndex, 2, triggerItem);
    }
    rightBottomTable_->setWordWrap(true);
    rightBottomTable_->resizeRowsToContents();

    if (profectionStatusLabel_) {
        profectionStatusLabel_->setText(
            QString("Updated: age %1, %2")
                .arg(ageYears)
                .arg(referenceLocal.toString("yyyy-MM-dd")));
    }
}

void MainWindow::refreshTransitScanTab() {
    if (activeTab_ != AppTab::Transits || transitSubTab_ != TransitSubTab::Scan) {
        return;
    }
    if (!rightTopTable_ || !rightBottomTable_) {
        return;
    }
    if (transitScanRunning_) {
        hasTransitScanSelection_ = false;
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Scanning..."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Scan in progress."));
        updateLunationCopyButtonState();
        return;
    }
    if (transitScanResults_.isEmpty()) {
        hasTransitScanSelection_ = false;
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Run a scan to see best/worst days."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("No scan results yet."));
        updateLunationCopyButtonState();
        return;
    }
    updateTransitScanResultsTable();
    if (!transitScanDisplayOrder_.isEmpty()) {
        int targetRow = 0;
        if (hasTransitScanSelection_) {
            for (int row = 0; row < transitScanDisplayOrder_.size(); ++row) {
                const int idx = transitScanDisplayOrder_[row];
                if (idx < 0 || idx >= transitScanResults_.size()) {
                    continue;
                }
                if (transitScanResults_[idx].date == lastTransitScanSelection_.date) {
                    targetRow = row;
                    break;
                }
            }
        }
        const int targetIndex = transitScanDisplayOrder_[targetRow];
        const bool hasMatchingSelection = hasTransitScanSelection_
            && targetIndex >= 0
            && targetIndex < transitScanResults_.size()
            && lastTransitScanSelection_.date == transitScanResults_[targetIndex].date;
        if (!hasMatchingSelection || !hasTransitChart_) {
            handleTransitScanResultActivated(targetRow, 0);
        } else {
            showTransitScanDetails(targetIndex);
        }
        if (rightTopTable_) {
            rightTopTable_->selectRow(targetRow);
        }
    }
}

void MainWindow::updateTransitScanResultsTable() {
    if (!rightTopTable_) {
        return;
    }
    if (transitScanResults_.isEmpty()) {
        hasTransitScanSelection_ = false;
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("No scan results yet."));
        updateLunationCopyButtonState();
        return;
    }
    QVector<int> indices;
    indices.reserve(transitScanResults_.size());
    for (int i = 0; i < transitScanResults_.size(); ++i) {
        indices.push_back(i);
    }
    const bool bestFirst = !scanSortCombo_ || scanSortCombo_->currentIndex() == 0;
    std::sort(indices.begin(), indices.end(), [this, bestFirst](int a, int b) {
        const double na = transitScanResults_[a].net;
        const double nb = transitScanResults_[b].net;
        if (bestFirst) {
            return na > nb;
        }
        return na < nb;
    });
    int topCount = scanTopCountSpin_ ? scanTopCountSpin_->value() : 100;
    if (topCount > indices.size()) {
        topCount = indices.size();
    }
    transitScanDisplayOrder_.clear();
    transitScanDisplayOrder_.reserve(topCount);
    setupTable(rightTopTable_, {"Date", "Net", "Support", "Challenge", "Aspects"}, topCount);
    for (int row = 0; row < topCount; ++row) {
        const int idx = indices[row];
        transitScanDisplayOrder_.push_back(idx);
        const auto& result = transitScanResults_[idx];
        rightTopTable_->setItem(row, 0, makeCell(result.date.toString("yyyy-MM-dd")));
        auto* netItem = makeCell(QString::number(result.net, 'f', 2), Qt::AlignRight | Qt::AlignVCenter);
        if (result.net > 0.01) {
            netItem->setForeground(QColor("#69c36d"));
        } else if (result.net < -0.01) {
            netItem->setForeground(QColor("#e05555"));
        }
        rightTopTable_->setItem(row, 1, netItem);
        rightTopTable_->setItem(row, 2, makeCell(QString::number(result.support, 'f', 2), Qt::AlignRight | Qt::AlignVCenter));
        rightTopTable_->setItem(row, 3, makeCell(QString::number(result.challenge, 'f', 2), Qt::AlignRight | Qt::AlignVCenter));
        rightTopTable_->setItem(row, 4, makeCell(QString::number(result.aspectCount), Qt::AlignCenter));
    }
}

void MainWindow::showTransitScanDetails(int index) {
    if (!rightBottomTable_ || index < 0 || index >= transitScanResults_.size()) {
        return;
    }
    const auto& result = transitScanResults_[index];
    const bool matchesSelectedMoment = hasTransitScanSelection_
        && lastTransitScanSelection_.date == result.date;
    if (!hasTransitChart_ || !matchesSelectedMoment) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Select a scan result to load transit placements."));
        updateLunationCopyButtonState();
        return;
    }

    const QVector<BodyPosition> orderedBodies = orderedBodiesForDetails(currentTransitChart_);
    const QString aspectsText = result.topAspects.isEmpty() ? "No strong aspects" : result.topAspects.join(" | ");
    const QString localTimeText = lastTransitScanSelectionLocal_.isValid()
        ? lastTransitScanSelectionLocal_.toString("yyyy-MM-dd HH:mm:ss")
        : QString("-");
    const QString tzLabel = lastTransitScanSelectionTzLabel_.isEmpty() ? QString("UTC") : lastTransitScanSelectionTzLabel_;
    const int summaryRows = 9;
    setupDetailTable(rightBottomTable_, {"Item", "Value"}, summaryRows + 1 + orderedBodies.size());
    if (auto* header = rightBottomTable_->horizontalHeader()) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::Stretch);
    }

    int row = 0;
    rightBottomTable_->setItem(row, 0, makeCell("Date"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.date.toString("yyyy-MM-dd")));
    rightBottomTable_->setItem(row, 0, makeCell("Local Time"));
    rightBottomTable_->setItem(row++, 1, makeCell(localTimeText));
    rightBottomTable_->setItem(row, 0, makeCell("Timezone"));
    rightBottomTable_->setItem(row++, 1, makeCell(tzLabel));
    rightBottomTable_->setItem(row, 0, makeCell("Net Score"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString::number(result.net, 'f', 2)));
    rightBottomTable_->setItem(row, 0, makeCell("Support Score"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString::number(result.support, 'f', 2)));
    rightBottomTable_->setItem(row, 0, makeCell("Challenge Score"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString::number(result.challenge, 'f', 2)));
    rightBottomTable_->setItem(row, 0, makeCell("Solar Return Bias"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString::number(result.solarBias, 'f', 2)));
    rightBottomTable_->setItem(row, 0, makeCell("Aspect Count"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString::number(result.aspectCount)));
    rightBottomTable_->setItem(row, 0, makeCell("Top Aspects"));
    auto* aspectsCell = makeCell(aspectsText);
    aspectsCell->setToolTip(aspectsText);
    rightBottomTable_->setItem(row++, 1, aspectsCell);

    rightBottomTable_->setItem(row, 0, makeCell("Placements"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString("%1 bodies").arg(orderedBodies.size())));
    for (const auto& body : orderedBodies) {
        const QString value = QString("%1 %2 | House %3 | %4")
            .arg(formatDegOnly(body.longitude))
            .arg(signName(signIndex(body.longitude)))
            .arg(body.house > 0 ? QString::number(body.house) : "-")
            .arg(body.retrograde ? "R" : "D");
        rightBottomTable_->setItem(row, 0, makeCell(body.name));
        rightBottomTable_->setItem(row++, 1, makeCell(value));
    }
    rightBottomTable_->resizeRowsToContents();
    updateLunationCopyButtonState();
}

void MainWindow::updateTransitSearchTargets() {
    if (!searchEventCombo_) {
        return;
    }
    const QString currentEvent = searchEventCombo_->currentText();
    const QStringList desiredEvents = (transitMode_ == TransitMode::TransitOnly)
        ? QStringList({"Sign Ingress", "Sign Egress", "Degree Hit", "Station"})
        : QStringList({"House Ingress", "House Egress", "Aspect to Natal", "Degree Hit"});
    bool rebuild = (searchEventCombo_->count() != desiredEvents.size());
    if (!rebuild) {
        for (int i = 0; i < desiredEvents.size(); ++i) {
            if (searchEventCombo_->itemText(i) != desiredEvents[i]) {
                rebuild = true;
                break;
            }
        }
    }
    if (rebuild) {
        QSignalBlocker blocker(searchEventCombo_);
        searchEventCombo_->clear();
        searchEventCombo_->addItems(desiredEvents);
        int newIndex = searchEventCombo_->findText(currentEvent);
        if (newIndex < 0) {
            newIndex = 0;
        }
        searchEventCombo_->setCurrentIndex(newIndex);
    }

    if (searchTargetCombo_) {
        QSignalBlocker targetBlocker(searchTargetCombo_);
        searchTargetCombo_->clear();
        const QString eventType = searchEventCombo_->currentText();
        const bool isAspectEvent = eventType.contains("Aspect", Qt::CaseInsensitive);
        const bool isHouseEvent = eventType.contains("House", Qt::CaseInsensitive);
        if ((isAspectEvent || isHouseEvent) && hasCurrentChart_ && transitMode_ == TransitMode::NatalOverlay) {
            searchTargetCombo_->addItem("Any");
            auto addTarget = [&](const QString& name, double lon) {
                QString label = name;
                int house = 0;
                if (isHouseEvent) {
                    house = calcHouseForLongitude(lon, natalPlacidusCusps_, currentChart_.angles.asc, transitHouseSystem_);
                    label = QString("%1 (House %2)").arg(name).arg(house);
                }
                const int index = searchTargetCombo_->count();
                searchTargetCombo_->addItem(label);
                if (isHouseEvent && house > 0) {
                    searchTargetCombo_->setItemData(index, house, Qt::UserRole);
                }
            };
            for (const auto& body : currentChart_.bodies) {
                addTarget(body.name, body.longitude);
            }
            if (isAspectEvent) {
                for (const auto& star : currentChart_.fixedStars) {
                    addTarget(star.name, star.longitude);
                }
            }
            addTarget("Ascendant", currentChart_.angles.asc);
            addTarget("Midheaven", currentChart_.angles.mc);
            addTarget("Descendant", currentChart_.angles.desc);
            addTarget("IC", currentChart_.angles.ic);
        }
    }
    updateTransitSearchVisibility();
}

void MainWindow::runTransitSearch() {
    if (searchRunning_) {
        return;
    }
    if (ephePath_.isEmpty()) {
        setStatusMessage("Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.");
        return;
    }
    if (!searchStartDateEdit_ || !searchStartTimeEdit_ || !searchEndDateEdit_ || !searchEndTimeEdit_) {
        return;
    }
    QTimeZone tz;
    QString tzLabel;
    QString tzErr;
    const QString tzText = searchTimezoneEdit_ ? searchTimezoneEdit_->text().trimmed() : QString("UTC");
    if (!parseTimezoneInput(tzText, &tz, &tzLabel, &tzErr)) {
        setStatusMessage(tzErr);
        return;
    }

    const QDateTime startLocal(searchStartDateEdit_->date(), searchStartTimeEdit_->time(), tz);
    if (!startLocal.isValid()) {
        setStatusMessage("Invalid search range.");
        return;
    }

    SearchParams params;
    params.tz = tz;
    params.tzLabel = tzLabel;
    params.ephePath = ephePath_;
    params.dllSearchPaths = sweSearchPaths();
    params.overlayMode = (transitMode_ == TransitMode::NatalOverlay);
    const QString requestedEventType = searchEventCombo_ ? searchEventCombo_->currentText() : QString();
    const bool requestedDegreeEvent = requestedEventType.contains("Degree", Qt::CaseInsensitive);
    params.houseSystem = transitHouseSystem_;
    params.hasNatal = hasCurrentChart_;
    params.natalAsc = currentChart_.angles.asc;
    params.natalCusps.clear();
    params.natalCusps.reserve(natalPlacidusCusps_.size());
    for (const auto& cusp : natalPlacidusCusps_) {
        params.natalCusps.push_back(normalizeDegrees(cusp.longitude));
    }

    if (params.overlayMode && !hasCurrentChart_ && !requestedDegreeEvent) {
        setStatusMessage("Load a natal chart before running overlay searches.");
        return;
    }

    if (transitUseNatalLocation_ && transitUseNatalLocation_->isChecked() && hasCurrentChart_) {
        params.latitude = currentInput_.latitude;
        params.longitude = currentInput_.longitude;
    } else {
        params.latitude = transitLatSpin_ ? transitLatSpin_->value() : currentInput_.latitude;
        params.longitude = transitLonSpin_ ? transitLonSpin_->value() : currentInput_.longitude;
    }

    const bool findNext = searchModeNextRadio_ && searchModeNextRadio_->isChecked();
    const bool findPrev = searchModePrevRadio_ && searchModePrevRadio_->isChecked();
    if (findNext || findPrev) {
        const int horizonDays = 365 * 50;
        const QDateTime anchorUtc = startLocal.toUTC();
        if (findNext) {
            params.findMode = SearchFindMode::Next;
            params.direction = SearchDirection::Forward;
            params.startUtc = anchorUtc;
            params.endUtc = anchorUtc.addDays(horizonDays);
        } else {
            params.findMode = SearchFindMode::Previous;
            params.direction = SearchDirection::Backward;
            params.endUtc = anchorUtc;
            params.startUtc = anchorUtc.addDays(-horizonDays);
        }
    } else {
        const QDateTime endLocal(searchEndDateEdit_->date(), searchEndTimeEdit_->time(), tz);
        if (!endLocal.isValid()) {
            setStatusMessage("Invalid search range.");
            return;
        }
        params.findMode = SearchFindMode::Range;
        params.startUtc = startLocal.toUTC();
        params.endUtc = endLocal.toUTC();
        const int rangeMode = searchRangeModeCombo_ ? searchRangeModeCombo_->currentIndex() : 0;
        if (rangeMode == 1) {
            params.direction = SearchDirection::Forward;
        } else if (rangeMode == 2) {
            params.direction = SearchDirection::Backward;
        } else {
            params.direction = SearchDirection::WithinRange;
        }
    }

    const QString eventType = requestedEventType;
    if (eventType.contains("Sign Ingress", Qt::CaseInsensitive)) {
        params.eventType = SearchEventType::SignIngress;
    } else if (eventType.contains("Sign Egress", Qt::CaseInsensitive)) {
        params.eventType = SearchEventType::SignEgress;
    } else if (eventType.contains("House Ingress", Qt::CaseInsensitive)) {
        params.eventType = SearchEventType::HouseIngress;
    } else if (eventType.contains("House Egress", Qt::CaseInsensitive)) {
        params.eventType = SearchEventType::HouseEgress;
    } else if (eventType.contains("Degree", Qt::CaseInsensitive)) {
        params.eventType = SearchEventType::DegreeHit;
    } else if (eventType.contains("Aspect", Qt::CaseInsensitive)) {
        params.eventType = SearchEventType::Aspect;
    } else {
        params.eventType = SearchEventType::Station;
    }

    if (params.eventType == SearchEventType::Aspect || params.eventType == SearchEventType::DegreeHit) {
        const bool isDegreeHit = (params.eventType == SearchEventType::DegreeHit);
        const QString aspectLabel = isDegreeHit
            ? QString("Conjunction")
            : (searchAspectCombo_ ? searchAspectCombo_->currentText() : QString("Conjunction"));
        params.aspectLabel = aspectLabel;
        params.aspectAngle = isDegreeHit ? 0.0 : aspectAngleForLabel(aspectLabel);
        params.orb = searchOrbSpin_ ? searchOrbSpin_->value() : 0.0;
        params.aspectMode = (params.orb <= 0.01) ? AspectMode::Exact : AspectMode::WithinOrb;

        QMap<QString, double> targets;
        if (isDegreeHit) {
            const int signIdx = searchDegreeSignCombo_ ? searchDegreeSignCombo_->currentIndex() : 0;
            const double deg = searchDegreeSpin_ ? searchDegreeSpin_->value() : 0.0;
            const double targetLon = normalizeDegrees(static_cast<double>(std::max(0, signIdx)) * 30.0 + deg);
            const QString targetName = QString("%1 %2").arg(transitcalc::formatDegreeDms(deg), signName(signIdx));
            targets.insert(targetName, targetLon);
            params.natalTargets = targets;
            params.targetNames = {targetName};
        } else {
            for (const auto& body : currentChart_.bodies) {
                targets.insert(body.name, body.longitude);
            }
            for (const auto& star : currentChart_.fixedStars) {
                targets.insert(star.name, star.longitude);
            }
            targets.insert("Ascendant", currentChart_.angles.asc);
            targets.insert("Midheaven", currentChart_.angles.mc);
            targets.insert("Descendant", currentChart_.angles.desc);
            targets.insert("IC", currentChart_.angles.ic);
            params.natalTargets = targets;

            if (searchTargetCombo_) {
                const QString targetText = searchTargetCombo_->currentText();
                const QString parsedTarget = aspectTargetFromLabel(targetText);
                const QString target = parsedTarget.isEmpty() ? targetText : parsedTarget;
                if (!target.isEmpty() && !target.startsWith("Any", Qt::CaseInsensitive)) {
                    params.targetNames = {target};
                }
            }
            if (params.targetNames.isEmpty()) {
                params.targetNames = targets.keys();
            }
        }
    } else if (params.eventType == SearchEventType::SignIngress || params.eventType == SearchEventType::SignEgress) {
        if (searchSignCombo_ && searchSignCombo_->currentIndex() > 0) {
            params.signFilter = searchSignCombo_->currentIndex() - 1;
        }
    } else if (params.eventType == SearchEventType::HouseIngress || params.eventType == SearchEventType::HouseEgress) {
        if (searchHouseCombo_ && searchHouseCombo_->currentIndex() > 0) {
            params.houseFilter = searchHouseCombo_->currentText().toInt();
        }
    }

    params.transitPlanets = selectedTransitPlanets(searchTransitPlanetCombo_);
    if (params.transitPlanets.isEmpty()) {
        setStatusMessage("Select at least one transit planet.");
        return;
    }

    transitSearchResults_.clear();
    hasTransitSearchSelection_ = false;
    showTransitSearchResults();
    searchAutoApplied_ = false;

    searchThread_ = new QThread(this);
    searchWorker_ = new SearchWorker(params);
    searchWorker_->moveToThread(searchThread_);
    connect(searchThread_, &QThread::started, searchWorker_, &SearchWorker::run);
    connect(searchWorker_, &SearchWorker::progressUpdate, this, [this](int, const QString& status) {
        if (searchStatusLabel_) {
            searchStatusLabel_->setText(status);
        }
    });
    connect(searchWorker_, &SearchWorker::resultFound, this, [this](const TransitSearchResult& result) {
        transitSearchResults_.push_back(result);
        if (transitSubTab_ == TransitSubTab::Search) {
            showTransitSearchResults();
        }
        const bool findMode = (searchModeNextRadio_ && searchModeNextRadio_->isChecked())
            || (searchModePrevRadio_ && searchModePrevRadio_->isChecked());
        if (findMode) {
            applyTransitSearchResult(result);
            if (rightTopTable_) {
                rightTopTable_->selectRow(0);
            }
            searchAutoApplied_ = true;
        }
    });
    connect(searchWorker_, &SearchWorker::finished, this, [this](bool cancelled, const QString& error) {
        searchRunning_ = false;
        QStringList warnings;
        if (searchWorker_) {
            warnings = searchWorker_->warnings();
        }
        if (searchStatusLabel_) {
            if (!error.isEmpty()) {
                searchStatusLabel_->setText("Error");
                setStatusMessage(error);
            } else if (cancelled) {
                searchStatusLabel_->setText("Cancelled");
            } else if (!warnings.isEmpty()) {
                searchStatusLabel_->setText("Done (warnings)");
            } else {
                searchStatusLabel_->setText("Done");
            }
        }
        if (error.isEmpty() && !warnings.isEmpty() && statusBar()) {
            statusBar()->showMessage(QString("Computed with warnings: %1").arg(warnings.join("; ")), 15000);
        }
        if (searchRunButton_) {
            searchRunButton_->setEnabled(true);
        }
        if (searchStopButton_) {
            searchStopButton_->setEnabled(false);
        }
        if (searchThread_) {
            searchThread_->quit();
            searchThread_->deleteLater();
            searchThread_ = nullptr;
        }
        if (searchWorker_) {
            searchWorker_->deleteLater();
            searchWorker_ = nullptr;
        }
        showTransitSearchResults();
    });
    searchRunning_ = true;
    if (searchRunButton_) {
        searchRunButton_->setEnabled(false);
    }
    if (searchStopButton_) {
        searchStopButton_->setEnabled(true);
    }
    if (searchStatusLabel_) {
        searchStatusLabel_->setText("Searching...");
    }
    searchThread_->start();
}

void MainWindow::showTransitSearchResults() {
    if (!rightTopTable_) {
        return;
    }
    if (transitSearchResults_.isEmpty()) {
        hasTransitSearchSelection_ = false;
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell(searchRunning_ ? "Searching..." : "No results yet."));
        if (rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Select a result to view details."));
        }
        updateLunationCopyButtonState();
        return;
    }
    std::sort(transitSearchResults_.begin(), transitSearchResults_.end(), [](const TransitSearchResult& a, const TransitSearchResult& b) {
        return a.timeUtc < b.timeUtc;
    });

    setupTable(rightTopTable_, {"Date/Time", "Planet", "Event", "Sign/House", "Aspect+Orb"}, transitSearchResults_.size());
    if (auto* header = rightTopTable_->horizontalHeader()) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(4, QHeaderView::Stretch);
    }
    for (int i = 0; i < transitSearchResults_.size(); ++i) {
        const auto& res = transitSearchResults_[i];
        rightTopTable_->setItem(i, 0, makeCell(res.timeLocal.toString("MMMM d yyyy, h:mm AP")));
        rightTopTable_->setItem(i, 1, makeCell(res.planet));
        rightTopTable_->setItem(i, 2, makeCell(res.event));
        rightTopTable_->setItem(i, 3, makeCell(res.signHouse.isEmpty() ? "-" : res.signHouse));
        QString aspectText = res.aspect;
        if (res.hasOrb) {
            aspectText = QString("%1 (%2 deg)").arg(res.aspect, QString::number(res.orb, 'f', 2));
        }
        rightTopTable_->setItem(i, 4, makeCell(aspectText.isEmpty() ? "-" : aspectText));
    }
    if (!transitSearchResults_.isEmpty()) {
        showTransitSearchDetails(transitSearchResults_.front());
        if (!searchAutoApplied_ && !searchRunning_) {
            applyTransitSearchResult(transitSearchResults_.front());
            if (rightTopTable_) {
                rightTopTable_->selectRow(0);
            }
            searchAutoApplied_ = true;
        }
    }
    updateLunationCopyButtonState();
}

void MainWindow::showTransitCalendarResults() {
    if (!rightTopTable_) {
        return;
    }
    if (transitCalendarEvents_.isEmpty()) {
        hasTransitCalendarSelection_ = false;
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell(calendarRunning_ ? "Computing calendar..." : "No calendar events yet."));
        if (rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Select a calendar event to view details."));
        }
        transitCalendarDisplayOrder_.clear();
        updateLunationCopyButtonState();
        return;
    }

    const QTimeZone displayTz = calendarTz_.isValid() ? calendarTz_ : QTimeZone::utc();
    const int monthFilter = calendarMonthCombo_ ? calendarMonthCombo_->currentData().toInt() : 0;
    const QStringList selectedPlanetList = selectedCheckableItems(calendarPlanetCombo_);
    const QSet<QString> selectedPlanets(selectedPlanetList.begin(), selectedPlanetList.end());
    const bool filterByPlanetSelection = (calendarPlanetCombo_ != nullptr);
    const bool showIngress = !calendarShowIngressCheck_ || calendarShowIngressCheck_->isChecked();
    const bool showEgress = !calendarShowEgressCheck_ || calendarShowEgressCheck_->isChecked();
    const bool showStation = !calendarShowStationCheck_ || calendarShowStationCheck_->isChecked();
    const bool showShadow = !calendarShowShadowCheck_ || calendarShowShadowCheck_->isChecked();

    auto eventAllowed = [showIngress, showEgress, showStation, showShadow](const QString& eventText) {
        if (eventText.contains("Ingress", Qt::CaseInsensitive)) {
            return showIngress;
        }
        if (eventText.contains("Egress", Qt::CaseInsensitive)) {
            return showEgress;
        }
        if (eventText.startsWith("Station", Qt::CaseInsensitive)) {
            return showStation;
        }
        if (eventText.contains("shadow", Qt::CaseInsensitive)) {
            return showShadow;
        }
        return true;
    };

    transitCalendarDisplayOrder_.clear();
    transitCalendarDisplayOrder_.reserve(transitCalendarEvents_.size());
    for (int i = 0; i < transitCalendarEvents_.size(); ++i) {
        const auto& event = transitCalendarEvents_[i];
        if (filterByPlanetSelection && !selectedPlanets.contains(event.planet)) {
            continue;
        }
        if (!eventAllowed(event.event)) {
            continue;
        }
        const auto local = transitCalendarEvents_[i].timeUtc.toTimeZone(displayTz);
        if (monthFilter > 0 && local.date().month() != monthFilter) {
            continue;
        }
        transitCalendarDisplayOrder_.push_back(i);
    }

    if (transitCalendarDisplayOrder_.isEmpty()) {
        hasTransitCalendarSelection_ = false;
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("No events match the current month/planet/event filters."));
        if (rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Adjust month, planet, or event filters to see results."));
        }
        updateLunationCopyButtonState();
        return;
    }

    setupTable(rightTopTable_, {"Date", "Time", "Planet", "Event", "Sign/House", "Longitude"}, transitCalendarDisplayOrder_.size());
    rightTopTable_->verticalHeader()->setDefaultSectionSize(24);
    auto* header = rightTopTable_->horizontalHeader();
    if (header) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(5, QHeaderView::Stretch);
    }
    for (int row = 0; row < transitCalendarDisplayOrder_.size(); ++row) {
        const int idx = transitCalendarDisplayOrder_[row];
        if (idx < 0 || idx >= transitCalendarEvents_.size()) {
            continue;
        }
        const auto& event = transitCalendarEvents_[idx];
        const QDateTime local = event.timeUtc.toTimeZone(displayTz);
        rightTopTable_->setItem(row, 0, makeCell(local.toString("ddd, MMM d, yyyy")));
        rightTopTable_->setItem(row, 1, makeCell(local.toString("hh:mm AP"), Qt::AlignHCenter | Qt::AlignVCenter));
        rightTopTable_->setItem(row, 2, makeCell(event.planet));
        rightTopTable_->setItem(row, 3, makeCell(event.event));
        rightTopTable_->setItem(row, 4, makeCell(event.signHouse.isEmpty() ? "-" : event.signHouse));
        rightTopTable_->setItem(row, 5, makeCell(formatDegInSign(event.longitude)));
    }

    if (!transitCalendarDisplayOrder_.isEmpty()) {
        int targetRow = 0;
        if (hasTransitCalendarSelection_) {
            for (int row = 0; row < transitCalendarDisplayOrder_.size(); ++row) {
                const int idx = transitCalendarDisplayOrder_[row];
                if (idx < 0 || idx >= transitCalendarEvents_.size()) {
                    continue;
                }
                const auto& event = transitCalendarEvents_[idx];
                if (event.timeUtc == lastTransitCalendarSelection_.timeUtc
                    && event.planet == lastTransitCalendarSelection_.planet
                    && event.event == lastTransitCalendarSelection_.event) {
                    targetRow = row;
                    break;
                }
            }
        }
        const int targetIndex = transitCalendarDisplayOrder_[targetRow];
        if (targetIndex >= 0 && targetIndex < transitCalendarEvents_.size()) {
            const auto& targetEvent = transitCalendarEvents_[targetIndex];
            const bool hasMatchingSelection = hasTransitCalendarSelection_
                && lastTransitCalendarSelection_.timeUtc == targetEvent.timeUtc
                && lastTransitCalendarSelection_.planet == targetEvent.planet
                && lastTransitCalendarSelection_.event == targetEvent.event;
            if (!calendarRunning_ && (!hasMatchingSelection || !hasTransitChart_)) {
                handleTransitCalendarResultActivated(targetRow, 0);
            } else {
                showTransitCalendarDetails(targetEvent);
            }
            if (rightTopTable_) {
                rightTopTable_->selectRow(targetRow);
            }
        }
    }
    updateLunationCopyButtonState();
}

void MainWindow::showTransitCalendarDetails(const TransitCalendarEvent& result) {
    if (!rightBottomTable_) {
        return;
    }
    const bool matchesSelectedMoment = hasTransitCalendarSelection_
        && lastTransitCalendarSelection_.timeUtc == result.timeUtc
        && lastTransitCalendarSelection_.planet == result.planet
        && lastTransitCalendarSelection_.event == result.event;
    if (!hasTransitChart_ || !matchesSelectedMoment) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Select a calendar result to load transit placements."));
        updateLunationCopyButtonState();
        return;
    }

    const QTimeZone displayTz = calendarTz_.isValid() ? calendarTz_ : QTimeZone::utc();
    const QDateTime localTime = result.timeUtc.toTimeZone(displayTz);
    const QString tzLabel = result.tzLabel.isEmpty() ? QString("UTC") : result.tzLabel;
    const QVector<BodyPosition> orderedBodies = orderedBodiesForDetails(currentTransitChart_);
    const int summaryRows = 7;
    setupDetailTable(rightBottomTable_, {"Item", "Value"}, summaryRows + 1 + orderedBodies.size());
    if (auto* header = rightBottomTable_->horizontalHeader()) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::Stretch);
    }
    int row = 0;
    rightBottomTable_->setItem(row, 0, makeCell("Local Time"));
    rightBottomTable_->setItem(row++, 1, makeCell(localTime.toString("yyyy-MM-dd HH:mm:ss")));
    rightBottomTable_->setItem(row, 0, makeCell("UTC Time"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.timeUtc.toString("yyyy-MM-dd HH:mm:ss")));
    rightBottomTable_->setItem(row, 0, makeCell("Timezone"));
    rightBottomTable_->setItem(row++, 1, makeCell(tzLabel));
    rightBottomTable_->setItem(row, 0, makeCell("Planet"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.planet));
    rightBottomTable_->setItem(row, 0, makeCell("Event"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.event));
    rightBottomTable_->setItem(row, 0, makeCell("Sign/House"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.signHouse.isEmpty() ? "-" : result.signHouse));
    rightBottomTable_->setItem(row, 0, makeCell("Longitude"));
    rightBottomTable_->setItem(row++, 1, makeCell(formatDegInSign(result.longitude)));

    rightBottomTable_->setItem(row, 0, makeCell("Placements"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString("%1 bodies").arg(orderedBodies.size())));
    for (const auto& body : orderedBodies) {
        const QString value = QString("%1 %2 | House %3 | %4")
            .arg(formatDegOnly(body.longitude))
            .arg(signName(signIndex(body.longitude)))
            .arg(body.house > 0 ? QString::number(body.house) : "-")
            .arg(body.retrograde ? "R" : "D");
        rightBottomTable_->setItem(row, 0, makeCell(body.name));
        rightBottomTable_->setItem(row++, 1, makeCell(value));
    }
    rightBottomTable_->resizeRowsToContents();
    updateLunationCopyButtonState();
}

void MainWindow::showTransitConjunctionResults() {
    if (!rightTopTable_) {
        return;
    }
    if (conjRunning_) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Searching for conjunctions..."));
        if (rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Search in progress."));
        }
        transitConjunctionDisplayOrder_.clear();
        hasTransitConjunctionSelection_ = false;
        updateLunationCopyButtonState();
        return;
    }
    if (transitConjunctionResults_.isEmpty()) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Run the conjunction finder to see results."));
        if (rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("No conjunction results yet."));
        }
        transitConjunctionDisplayOrder_.clear();
        hasTransitConjunctionSelection_ = false;
        updateLunationCopyButtonState();
        return;
    }

    transitConjunctionDisplayOrder_.clear();
    if (conjFindMode_ == ConjunctionFindMode::Range) {
        for (int i = 0; i < transitConjunctionResults_.size(); ++i) {
            transitConjunctionDisplayOrder_.push_back(i);
        }
    } else {
        int selectedIndex = -1;
        for (int i = 0; i < transitConjunctionResults_.size(); ++i) {
            const auto& res = transitConjunctionResults_[i];
            if (conjAnchorUtc_.isValid() && conjAnchorUtc_ >= res.startUtc && conjAnchorUtc_ <= res.endUtc) {
                selectedIndex = i;
                break;
            }
        }
        if (selectedIndex < 0) {
            if (conjFindMode_ == ConjunctionFindMode::Next) {
                for (int i = 0; i < transitConjunctionResults_.size(); ++i) {
                    if (transitConjunctionResults_[i].startUtc > conjAnchorUtc_) {
                        selectedIndex = i;
                        break;
                    }
                }
            } else if (conjFindMode_ == ConjunctionFindMode::Previous) {
                for (int i = transitConjunctionResults_.size() - 1; i >= 0; --i) {
                    if (transitConjunctionResults_[i].endUtc < conjAnchorUtc_) {
                        selectedIndex = i;
                        break;
                    }
                }
            }
        }
        if (selectedIndex >= 0) {
            transitConjunctionDisplayOrder_.push_back(selectedIndex);
        }
    }

    if (transitConjunctionDisplayOrder_.isEmpty()) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("No conjunction windows found for the selected range."));
        if (rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Try a different range or lower N."));
        }
        hasTransitConjunctionSelection_ = false;
        updateLunationCopyButtonState();
        return;
    }

    const QTimeZone displayTz = conjTz_.isValid() ? conjTz_ : QTimeZone::utc();
    setupTable(rightTopTable_, {"Start Date", "Start Time", "End Date", "End Time", "Sign/House", "Count", "Planets", "Span"},
               transitConjunctionDisplayOrder_.size());
    rightTopTable_->verticalHeader()->setDefaultSectionSize(24);
    if (auto* header = rightTopTable_->horizontalHeader()) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(6, QHeaderView::Stretch);
        header->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    }

    for (int row = 0; row < transitConjunctionDisplayOrder_.size(); ++row) {
        const int idx = transitConjunctionDisplayOrder_[row];
        if (idx < 0 || idx >= transitConjunctionResults_.size()) {
            continue;
        }
        const auto& res = transitConjunctionResults_[idx];
        const QDateTime localStart = res.startUtc.toTimeZone(displayTz);
        const QDateTime localEnd = res.endUtc.toTimeZone(displayTz);
        const QStringList planets = res.orbClusterAtStart.isEmpty() ? res.planetsInBucketAtStart : res.orbClusterAtStart;
        rightTopTable_->setItem(row, 0, makeCell(localStart.toString("ddd, MMM d, yyyy")));
        rightTopTable_->setItem(row, 1, makeCell(localStart.toString("hh:mm AP"), Qt::AlignHCenter | Qt::AlignVCenter));
        rightTopTable_->setItem(row, 2, makeCell(localEnd.toString("ddd, MMM d, yyyy")));
        rightTopTable_->setItem(row, 3, makeCell(localEnd.toString("hh:mm AP"), Qt::AlignHCenter | Qt::AlignVCenter));
        rightTopTable_->setItem(row, 4, makeCell(res.bucketLabel));
        rightTopTable_->setItem(row, 5, makeCell(QString::number(res.clusterCount)));
        rightTopTable_->setItem(row, 6, makeCell(planets.join(", ")));
        const bool instantExact = (res.startUtc == res.endUtc);
        rightTopTable_->setItem(row, 7, makeCell((res.clusterSpanDeg > 0.0 || instantExact)
            ? QString::number(res.clusterSpanDeg, 'f', 2) + "°"
            : "-"));
    }

    if (!transitConjunctionDisplayOrder_.isEmpty()) {
        const int firstIndex = transitConjunctionDisplayOrder_.front();
        if (firstIndex >= 0 && firstIndex < transitConjunctionResults_.size()) {
            showTransitConjunctionDetails(transitConjunctionResults_[firstIndex]);
            if (!conjAutoApplied_) {
                handleTransitConjunctionResultActivated(0, 0);
                if (rightTopTable_) {
                    rightTopTable_->selectRow(0);
                }
                conjAutoApplied_ = true;
            }
        }
    }
    updateLunationCopyButtonState();
}

void MainWindow::showTransitConjunctionDetails(const TransitConjunctionWindow& result) {
    if (!rightBottomTable_) {
        return;
    }
    const bool matchesSelectedMoment = hasTransitConjunctionSelection_
        && lastTransitConjunctionSelection_.startUtc == result.startUtc
        && lastTransitConjunctionSelection_.endUtc == result.endUtc
        && lastTransitConjunctionSelection_.bucketLabel == result.bucketLabel
        && lastTransitConjunctionSelection_.planetsInBucketAtStart == result.planetsInBucketAtStart
        && lastTransitConjunctionSelection_.orbClusterAtStart == result.orbClusterAtStart;
    if (!hasTransitChart_ || !matchesSelectedMoment) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Select a conjunction result to load transit placements."));
        updateLunationCopyButtonState();
        return;
    }

    QMap<QString, BodyPosition> bodyMap;
    for (const auto& body : currentTransitChart_.bodies) {
        bodyMap.insert(body.name, body);
    }
    QVector<BodyPosition> orderedBodies;
    orderedBodies.reserve(bodyMap.size());
    for (const auto& name : tropicalBodyOrder()) {
        if (!bodyMap.contains(name)) {
            continue;
        }
        orderedBodies.push_back(bodyMap.value(name));
        bodyMap.remove(name);
    }
    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        orderedBodies.push_back(it.value());
    }

    setupTable(rightBottomTable_, {"Body", "Degree", "Sign", "House", "Motion"}, orderedBodies.size());
    if (auto* header = rightBottomTable_->horizontalHeader()) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(4, QHeaderView::Stretch);
    }

    const QTimeZone displayTz = conjTz_.isValid() ? conjTz_ : QTimeZone::utc();
    const QDateTime localStart = result.startUtc.toTimeZone(displayTz);
    const QDateTime localEnd = result.endUtc.toTimeZone(displayTz);
    const QString tzLabel = result.tzLabel.isEmpty() ? QString("UTC") : result.tzLabel;
    const QStringList clusterPlanets = result.orbClusterAtStart.isEmpty()
        ? result.planetsInBucketAtStart
        : result.orbClusterAtStart;
    const QString summary = QString("Local %1 - %2 | UTC %3 - %4 | %5 | %6 | %7 | Span %8 deg")
        .arg(localStart.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(localEnd.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(result.startUtc.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(result.endUtc.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(tzLabel)
        .arg(result.bucketLabel)
        .arg(clusterPlanets.join(", "))
        .arg(QString::number(result.clusterSpanDeg, 'f', 2));
    rightBottomTable_->setToolTip(summary);

    for (int row = 0; row < orderedBodies.size(); ++row) {
        const auto& body = orderedBodies[row];
        const QString degree = formatDegOnly(body.longitude);
        const QString sign = signName(signIndex(body.longitude));
        const QString house = body.house > 0 ? QString::number(body.house) : "-";
        const QString motion = body.retrograde ? "R" : "D";
        rightBottomTable_->setItem(row, 0, makeCell(body.name));
        rightBottomTable_->setItem(row, 1, makeCell(degree, Qt::AlignRight | Qt::AlignVCenter));
        rightBottomTable_->setItem(row, 2, makeCell(sign));
        rightBottomTable_->setItem(row, 3, makeCell(house, Qt::AlignCenter));
        rightBottomTable_->setItem(row, 4, makeCell(motion, Qt::AlignCenter));
    }
    updateLunationCopyButtonState();
}
void MainWindow::showTransitSearchDetails(const TransitSearchResult& result) {
    if (!rightBottomTable_) {
        return;
    }
    const bool matchesSelectedMoment = hasTransitSearchSelection_
        && lastTransitSearchSelection_.timeUtc == result.timeUtc
        && lastTransitSearchSelection_.planet == result.planet
        && lastTransitSearchSelection_.event == result.event;
    if (!hasTransitChart_ || !matchesSelectedMoment) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Select a result to load transit placements."));
        updateLunationCopyButtonState();
        return;
    }

    QMap<QString, BodyPosition> bodyMap;
    for (const auto& body : currentTransitChart_.bodies) {
        bodyMap.insert(body.name, body);
    }
    QVector<BodyPosition> orderedBodies;
    orderedBodies.reserve(bodyMap.size());
    for (const auto& name : tropicalBodyOrder()) {
        if (!bodyMap.contains(name)) {
            continue;
        }
        orderedBodies.push_back(bodyMap.value(name));
        bodyMap.remove(name);
    }
    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        orderedBodies.push_back(it.value());
    }

    setupTable(rightBottomTable_, {"Body", "Degree", "Sign", "House", "Motion"}, orderedBodies.size());
    if (auto* header = rightBottomTable_->horizontalHeader()) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(4, QHeaderView::Stretch);
    }

    const QString summary = QString("Local %1 | UTC %2 | %3 | %4 | %5")
        .arg(result.timeLocal.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(result.timeUtc.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(result.planet)
        .arg(result.event)
        .arg(result.signHouse.isEmpty() ? "-" : result.signHouse);
    rightBottomTable_->setToolTip(summary);

    for (int row = 0; row < orderedBodies.size(); ++row) {
        const auto& body = orderedBodies[row];
        const QString degree = formatDegOnly(body.longitude);
        const QString sign = signName(signIndex(body.longitude));
        const QString house = body.house > 0 ? QString::number(body.house) : "-";
        const QString motion = body.retrograde ? "R" : "D";
        rightBottomTable_->setItem(row, 0, makeCell(body.name));
        rightBottomTable_->setItem(row, 1, makeCell(degree, Qt::AlignRight | Qt::AlignVCenter));
        rightBottomTable_->setItem(row, 2, makeCell(sign));
        rightBottomTable_->setItem(row, 3, makeCell(house, Qt::AlignCenter));
        rightBottomTable_->setItem(row, 4, makeCell(motion, Qt::AlignCenter));
    }
    updateLunationCopyButtonState();
}

void MainWindow::runLunationSearch() {
    if (lunationRunning_) {
        return;
    }
    if (ephePath_.isEmpty()) {
        setStatusMessage("Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.");
        return;
    }
    if (!lunationNewMoonCheck_ || !lunationFullMoonCheck_ || !lunationSolarEclipseCheck_ || !lunationLunarEclipseCheck_) {
        return;
    }
    const bool includeNew = lunationNewMoonCheck_->isChecked();
    const bool includeFull = lunationFullMoonCheck_->isChecked();
    const bool includeSolar = lunationSolarEclipseCheck_->isChecked();
    const bool includeLunar = lunationLunarEclipseCheck_->isChecked();
    if (!includeNew && !includeFull && !includeSolar && !includeLunar) {
        setStatusMessage("Select at least one event type to search.");
        return;
    }

    QString tzLabel = currentInput_.timezone.trimmed();
    if (tzLabel.isEmpty()) {
        tzLabel = "UTC";
    }
    QTimeZone tz;
    QString normLabel;
    QString tzErr;
    if (!parseTimezoneInput(tzLabel, &tz, &normLabel, &tzErr)) {
        setStatusMessage(tzErr);
        return;
    }

    LunationParams params;
    params.tz = tz;
    params.tzLabel = normLabel;
    params.ephePath = ephePath_;
    params.dllSearchPaths = sweSearchPaths();
    params.includeNewMoon = includeNew;
    params.includeFullMoon = includeFull;
    params.includeSolarEclipse = includeSolar;
    params.includeLunarEclipse = includeLunar;
    params.useDegreeRange = lunationDegreeRangeCheck_ && lunationDegreeRangeCheck_->isChecked();
    if (params.useDegreeRange) {
        params.degreeRangeStart = lunationDegreeRangeStartSpin_ ? lunationDegreeRangeStartSpin_->value() : 0.0;
        params.degreeRangeEnd = lunationDegreeRangeEndSpin_ ? lunationDegreeRangeEndSpin_->value() : 29.99;
    }

    if (lunationModeRangeRadio_ && lunationModeRangeRadio_->isChecked()) {
        params.findMode = LunationFindMode::Range;
        int startYear = lunationStartYearSpin_ ? lunationStartYearSpin_->value() : QDate::currentDate().year();
        int endYear = lunationEndYearSpin_ ? lunationEndYearSpin_->value() : startYear;
        if (startYear > endYear) {
            const int tmp = startYear;
            startYear = endYear;
            endYear = tmp;
            if (lunationStartYearSpin_) {
                lunationStartYearSpin_->setValue(startYear);
            }
            if (lunationEndYearSpin_) {
                lunationEndYearSpin_->setValue(endYear);
            }
        }
        const QDate startDate(startYear, 1, 1);
        const QDate endDate(endYear, 12, 31);
        if (!startDate.isValid() || !endDate.isValid()) {
            setStatusMessage("Invalid year range.");
            return;
        }
        const QDateTime startLocal(startDate, QTime(0, 0, 0), tz);
        const QDateTime endLocal(endDate, QTime(23, 59, 59), tz);
        params.startUtc = startLocal.toUTC();
        params.endUtc = endLocal.toUTC();
    } else if (lunationModePrevRadio_ && lunationModePrevRadio_->isChecked()) {
        params.findMode = LunationFindMode::Previous;
        const QDateTime nowLocal = QDateTime::currentDateTimeUtc().toTimeZone(tz);
        params.startUtc = nowLocal.toUTC();
    } else {
        params.findMode = LunationFindMode::Next;
        const QDateTime nowLocal = QDateTime::currentDateTimeUtc().toTimeZone(tz);
        params.startUtc = nowLocal.toUTC();
    }

    lunationRunning_ = true;
    lunationAutoApplied_ = false;
    lunationResults_.clear();
    lunationDegreeGroups_.clear();
    lunationDegreeGroupDisplayOrder_.clear();
    lunationListDisplayOrder_.clear();
    lunationAnalysisEventOrder_.clear();
    lunationSelectedGroupIndex_ = -1;
    hasLunationSelection_ = false;

    if (lunationRunButton_) {
        lunationRunButton_->setEnabled(false);
    }
    if (lunationStopButton_) {
        lunationStopButton_->setEnabled(true);
    }
    if (lunationStatusLabel_) {
        lunationStatusLabel_->setText("Searching...");
    }
    if (transitSubTab_ == TransitSubTab::Lunations) {
        showLunationResults();
    }

    auto* worker = new LunationWorker(params);
    lunationWorker_ = worker;
    lunationThread_ = new QThread(this);
    worker->moveToThread(lunationThread_);

    connect(lunationThread_, &QThread::started, worker, &LunationWorker::run);
    connect(worker, &LunationWorker::progressUpdate, this, [this](int percent, const QString& status) {
        if (!lunationStatusLabel_) {
            return;
        }
        if (percent >= 0) {
            lunationStatusLabel_->setText(QString("%1 (%2%)").arg(status).arg(percent));
        } else {
            lunationStatusLabel_->setText(status);
        }
    });
    connect(worker, &LunationWorker::finished, this, [this](bool cancelled, const QString& error) {
        lunationRunning_ = false;
        if (lunationRunButton_) {
            lunationRunButton_->setEnabled(true);
        }
        if (lunationStopButton_) {
            lunationStopButton_->setEnabled(false);
        }
        if (lunationWorker_) {
            auto* worker = qobject_cast<LunationWorker*>(lunationWorker_);
            if (worker) {
                lunationResults_ = worker->results();
            }
            lunationWorker_->deleteLater();
            lunationWorker_ = nullptr;
        }
        if (lunationThread_) {
            lunationThread_->quit();
            lunationThread_->deleteLater();
            lunationThread_ = nullptr;
        }
        if (lunationStatusLabel_) {
            if (!error.isEmpty()) {
                lunationStatusLabel_->setText("Error");
                setStatusMessage(error);
            } else if (cancelled) {
                lunationStatusLabel_->setText("Cancelled");
            } else {
                lunationStatusLabel_->setText(QString("Done (%1)").arg(lunationResults_.size()));
            }
        }
        if (transitSubTab_ == TransitSubTab::Lunations) {
            showLunationResults();
        }
    });

    lunationThread_->start();
}

void MainWindow::showLunationResults() {
    if (!rightTopTable_ || !rightBottomTable_) {
        return;
    }
    lunationBottomEventOrder_.clear();
    lunationListDisplayOrder_.clear();
    if (lunationRunning_) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Searching..."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Search in progress."));
        updateLunationCopyButtonState();
        return;
    }
    if (lunationResults_.isEmpty()) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Run a lunation search to see results."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("No lunation results yet."));
        updateLunationCopyButtonState();
        return;
    }

    std::sort(lunationResults_.begin(), lunationResults_.end(), [](const LunationResult& a, const LunationResult& b) {
        return a.timeUtc < b.timeUtc;
    });

    if (lunationAnalysisMode_ != LunationAnalysisMode::List) {
        showLunationAnalysisResults();
        updateLunationCopyButtonState();
        return;
    }

    const int signModeIndex = lunationSignModeCombo_ ? lunationSignModeCombo_->currentIndex() : 0;
    const LunationFilterMode signMode = static_cast<LunationFilterMode>(signModeIndex);
    const QStringList selectedSigns = selectedCheckableItems(lunationSignCombo_);
    QSet<int> signFilter;
    if (!selectedSigns.isEmpty()) {
        const QStringList signNames = zodiacSigns();
        for (const auto& name : selectedSigns) {
            const int idx = signNames.indexOf(name);
            if (idx >= 0) {
                signFilter.insert(idx);
            }
        }
    }
    auto signFilterAllows = [&](int signIdx) {
        if (signMode == LunationFilterMode::Any || signFilter.isEmpty()) {
            return true;
        }
        if (signMode == LunationFilterMode::OnlySelected) {
            return signFilter.contains(signIdx);
        }
        if (signMode == LunationFilterMode::ExcludeSelected) {
            return !signFilter.contains(signIdx);
        }
        return true;
    };

    lunationListDisplayOrder_.reserve(lunationResults_.size());
    for (int i = 0; i < lunationResults_.size(); ++i) {
        const auto& res = lunationResults_[i];
        const int signIdx = signIndex(res.moonLon);
        if (!signFilterAllows(signIdx)) {
            continue;
        }
        lunationListDisplayOrder_.push_back(i);
    }

    if (lunationListDisplayOrder_.isEmpty()) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("No lunations match the selected sign filter."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Adjust sign filters or reset to Any."));
        hasLunationSelection_ = false;
        updateLunationCopyButtonState();
        return;
    }

    setupTable(rightTopTable_, {"Date", "Time", "Event", "Sun", "Moon", "Eclipse"}, lunationListDisplayOrder_.size());
    for (int row = 0; row < lunationListDisplayOrder_.size(); ++row) {
        const int idx = lunationListDisplayOrder_[row];
        if (idx < 0 || idx >= lunationResults_.size()) {
            continue;
        }
        const auto& res = lunationResults_[idx];
        rightTopTable_->setItem(row, 0, makeCell(res.timeLocal.toString("ddd, MMM d, yyyy")));
        rightTopTable_->setItem(row, 1, makeCell(res.timeLocal.toString("hh:mm AP"), Qt::AlignHCenter | Qt::AlignVCenter));
        rightTopTable_->setItem(row, 2, makeCell(res.event));
        rightTopTable_->setItem(row, 3, makeCell(formatDegInSign(res.sunLon)));
        rightTopTable_->setItem(row, 4, makeCell(formatDegInSign(res.moonLon)));
        rightTopTable_->setItem(row, 5, makeCell(res.eclipseType.isEmpty() ? "-" : res.eclipseType));
    }
    const int firstIndex = lunationListDisplayOrder_.front();
    if (firstIndex >= 0 && firstIndex < lunationResults_.size()) {
        showLunationDetails(lunationResults_[firstIndex]);
        if (!lunationAutoApplied_ && canApplyLunationResult(nullptr)) {
            applyLunationResult(lunationResults_[firstIndex]);
            if (rightTopTable_) {
                rightTopTable_->selectRow(0);
            }
            lunationAutoApplied_ = true;
        }
    }
    updateLunationCopyButtonState();
}

void MainWindow::buildLunationDegreeGroups() {
    lunationDegreeGroups_.clear();
    lunationDegreeGroupDisplayOrder_.clear();
    lunationAnalysisEventOrder_.clear();
    lunationBottomEventOrder_.clear();
    lunationSelectedGroupIndex_ = -1;

    if (lunationResults_.isEmpty()) {
        return;
    }

    const bool hasNatal = hasCurrentChart_;
    const int matchIndex = lunationMatchCombo_ ? lunationMatchCombo_->currentIndex() : 0;
    const LunationMatchMode matchMode = static_cast<LunationMatchMode>(matchIndex);
    const bool includeSign = (matchMode == LunationMatchMode::DegreeSign || matchMode == LunationMatchMode::DegreeSignHouse);
    const bool includeHouse = hasNatal && (matchMode == LunationMatchMode::DegreeHouse || matchMode == LunationMatchMode::DegreeSignHouse);

    const int signModeIndex = lunationSignModeCombo_ ? lunationSignModeCombo_->currentIndex() : 0;
    const LunationFilterMode signMode = static_cast<LunationFilterMode>(signModeIndex);
    const int houseModeIndex = lunationHouseModeCombo_ ? lunationHouseModeCombo_->currentIndex() : 0;
    const LunationFilterMode houseMode = static_cast<LunationFilterMode>(houseModeIndex);

    const QStringList selectedSigns = selectedCheckableItems(lunationSignCombo_);
    QSet<int> signFilter;
    if (!selectedSigns.isEmpty()) {
        const QStringList signNames = zodiacSigns();
        for (const auto& name : selectedSigns) {
            const int idx = signNames.indexOf(name);
            if (idx >= 0) {
                signFilter.insert(idx);
            }
        }
    }

    const QStringList selectedHouses = selectedCheckableItems(lunationHouseCombo_);
    QSet<int> houseFilter;
    if (!selectedHouses.isEmpty()) {
        for (const auto& text : selectedHouses) {
            bool ok = false;
            const int value = text.toInt(&ok);
            if (ok && value >= 1 && value <= 12) {
                houseFilter.insert(value);
            }
        }
    }

    auto signFilterAllows = [&](int signIdx) {
        if (signMode == LunationFilterMode::Any || signFilter.isEmpty()) {
            return true;
        }
        if (signMode == LunationFilterMode::OnlySelected) {
            return signFilter.contains(signIdx);
        }
        if (signMode == LunationFilterMode::ExcludeSelected) {
            return !signFilter.contains(signIdx);
        }
        return true;
    };
    auto houseFilterAllows = [&](int house) {
        if (!hasNatal || house <= 0) {
            return (houseMode != LunationFilterMode::OnlySelected);
        }
        if (houseMode == LunationFilterMode::Any || houseFilter.isEmpty()) {
            return true;
        }
        if (houseMode == LunationFilterMode::OnlySelected) {
            return houseFilter.contains(house);
        }
        if (houseMode == LunationFilterMode::ExcludeSelected) {
            return !houseFilter.contains(house);
        }
        return true;
    };

    struct EventInfo {
        int index = -1;
        double degree = 0.0;
        int signIndex = -1;
        int house = 0;
    };

    QVector<EventInfo> events;
    events.reserve(lunationResults_.size());
    for (int i = 0; i < lunationResults_.size(); ++i) {
        const auto& res = lunationResults_[i];
        const int signIdx = signIndex(res.moonLon);
        const double degree = degInSign(res.moonLon);
        int house = 0;
        if (hasNatal) {
            house = calcHouseForLongitude(res.moonLon, natalPlacidusCusps_, currentChart_.angles.asc, currentInput_.houseSystem);
        }
        if (!signFilterAllows(signIdx)) {
            continue;
        }
        if (!houseFilterAllows(house)) {
            continue;
        }
        EventInfo info;
        info.index = i;
        info.degree = degree;
        info.signIndex = signIdx;
        info.house = house;
        events.push_back(info);
    }

    if (lunationAnalysisMode_ == LunationAnalysisMode::TargetDegree) {
        const int degVal = lunationTargetDegSpin_ ? lunationTargetDegSpin_->value() : 0;
        const int minVal = lunationTargetMinSpin_ ? lunationTargetMinSpin_->value() : 0;
        const int secVal = lunationTargetSecSpin_ ? lunationTargetSecSpin_->value() : 0;
        const double targetDeg = static_cast<double>(degVal) + minVal / 60.0 + secVal / 3600.0;
        const bool exact = lunationDegreeExactRadio_ && lunationDegreeExactRadio_->isChecked();
        double orb = 0.0;
        if (!exact && lunationOrbCombo_) {
            orb = lunationOrbCombo_->currentData().toDouble();
        }

        const int targetArcsec = static_cast<int>(std::llround(targetDeg * 3600.0));
        for (const auto& info : events) {
            if (exact) {
                const int arcsec = static_cast<int>(std::llround(info.degree * 3600.0));
                if (arcsec == targetArcsec) {
                    lunationAnalysisEventOrder_.push_back(info.index);
                }
            } else {
                if (std::fabs(info.degree - targetDeg) <= orb) {
                    lunationAnalysisEventOrder_.push_back(info.index);
                }
            }
        }
        return;
    }

    if (lunationAnalysisMode_ != LunationAnalysisMode::RepeatedDegrees) {
        return;
    }

    const bool exact = lunationDegreeExactRadio_ && lunationDegreeExactRadio_->isChecked();
    double orb = 0.0;
    if (!exact && lunationOrbCombo_) {
        orb = lunationOrbCombo_->currentData().toDouble();
    }

    if (exact) {
        QMap<QString, int> groupIndexMap;
        for (const auto& info : events) {
            const int arcsec = static_cast<int>(std::llround(info.degree * 3600.0));
            const int signKey = includeSign ? info.signIndex : -1;
            const int houseKey = includeHouse ? info.house : 0;
            const QString key = QString("%1|%2|%3").arg(arcsec).arg(signKey).arg(houseKey);
            if (!groupIndexMap.contains(key)) {
                LunationDegreeGroup group;
                group.degree = info.degree;
                group.signIndex = signKey;
                group.house = houseKey;
                group.eventIndices.push_back(info.index);
                groupIndexMap.insert(key, lunationDegreeGroups_.size());
                lunationDegreeGroups_.push_back(group);
            } else {
                lunationDegreeGroups_[groupIndexMap.value(key)].eventIndices.push_back(info.index);
            }
        }
    } else {
        QMap<QString, QVector<EventInfo>> buckets;
        for (const auto& info : events) {
            const int signKey = includeSign ? info.signIndex : -1;
            const int houseKey = includeHouse ? info.house : 0;
            const QString key = QString("%1|%2").arg(signKey).arg(houseKey);
            buckets[key].push_back(info);
        }
        for (auto it = buckets.begin(); it != buckets.end(); ++it) {
            auto bucket = it.value();
            std::sort(bucket.begin(), bucket.end(), [](const EventInfo& a, const EventInfo& b) {
                return a.degree < b.degree;
            });
            int startIndex = 0;
            while (startIndex < bucket.size()) {
                double startDeg = bucket[startIndex].degree;
                int endIndex = startIndex;
                while (endIndex + 1 < bucket.size()) {
                    const double nextDeg = bucket[endIndex + 1].degree;
                    if (nextDeg - startDeg <= orb) {
                        endIndex++;
                        continue;
                    }
                    break;
                }
                LunationDegreeGroup group;
                double sumDeg = 0.0;
                for (int i = startIndex; i <= endIndex; ++i) {
                    group.eventIndices.push_back(bucket[i].index);
                    sumDeg += bucket[i].degree;
                }
                group.degree = sumDeg / static_cast<double>(group.eventIndices.size());
                const QStringList parts = it.key().split("|");
                if (parts.size() == 2) {
                    group.signIndex = parts[0].toInt();
                    group.house = parts[1].toInt();
                }
                lunationDegreeGroups_.push_back(group);
                startIndex = endIndex + 1;
            }
        }
    }

    for (int i = 0; i < lunationDegreeGroups_.size(); ++i) {
        if (lunationDegreeGroups_[i].eventIndices.size() >= 2) {
            lunationDegreeGroupDisplayOrder_.push_back(i);
        }
    }
    std::sort(lunationDegreeGroupDisplayOrder_.begin(), lunationDegreeGroupDisplayOrder_.end(),
              [this](int a, int b) {
        const int countA = lunationDegreeGroups_[a].eventIndices.size();
        const int countB = lunationDegreeGroups_[b].eventIndices.size();
        if (countA != countB) {
            return countA > countB;
        }
        return lunationDegreeGroups_[a].degree < lunationDegreeGroups_[b].degree;
    });
}

void MainWindow::showLunationAnalysisResults() {
    if (!rightTopTable_ || !rightBottomTable_) {
        return;
    }

    buildLunationDegreeGroups();

    if (lunationAnalysisMode_ == LunationAnalysisMode::TargetDegree) {
        if (lunationAnalysisEventOrder_.isEmpty()) {
            setupTable(rightTopTable_, {"Info"}, 1);
            rightTopTable_->setItem(0, 0, makeCell("No matching events found."));
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("No event details to display."));
            updateLunationCopyButtonState();
            return;
        }
        lunationBottomEventOrder_.clear();
        setupTable(rightTopTable_, {"Date", "Time", "Event", "Moon Deg", "Sign", "House", "Eclipse"}, lunationAnalysisEventOrder_.size());
        for (int row = 0; row < lunationAnalysisEventOrder_.size(); ++row) {
            const int idx = lunationAnalysisEventOrder_[row];
            if (idx < 0 || idx >= lunationResults_.size()) {
                continue;
            }
            const auto& res = lunationResults_[idx];
            rightTopTable_->setItem(row, 0, makeCell(res.timeLocal.toString("ddd, MMM d, yyyy")));
            rightTopTable_->setItem(row, 1, makeCell(res.timeLocal.toString("hh:mm AP"), Qt::AlignHCenter | Qt::AlignVCenter));
            rightTopTable_->setItem(row, 2, makeCell(res.event));
            rightTopTable_->setItem(row, 3, makeCell(formatDegOnly(res.moonLon)));
            rightTopTable_->setItem(row, 4, makeCell(signName(signIndex(res.moonLon))));
            QString houseLabel = "-";
            if (hasCurrentChart_) {
                const int house = calcHouseForLongitude(res.moonLon, natalPlacidusCusps_, currentChart_.angles.asc, currentInput_.houseSystem);
                if (house > 0) {
                    houseLabel = QString::number(house);
                }
            }
            rightTopTable_->setItem(row, 5, makeCell(houseLabel));
            rightTopTable_->setItem(row, 6, makeCell(res.eclipseType.isEmpty() ? "-" : res.eclipseType));
        }
        const int firstIdx = lunationAnalysisEventOrder_.front();
        if (firstIdx >= 0 && firstIdx < lunationResults_.size()) {
            showLunationDetails(lunationResults_[firstIdx]);
            if (!lunationAutoApplied_ && canApplyLunationResult(nullptr)) {
                applyLunationResult(lunationResults_[firstIdx]);
                if (rightTopTable_) {
                    rightTopTable_->selectRow(0);
                }
                lunationAutoApplied_ = true;
            }
        }
        updateLunationCopyButtonState();
        return;
    }

    if (lunationDegreeGroupDisplayOrder_.isEmpty()) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("No repeated degrees found."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("No group details to display."));
        updateLunationCopyButtonState();
        return;
    }
    lunationBottomEventOrder_.clear();

    setupTable(rightTopTable_, {"Degree", "Count", "Sign", "House", "Types"}, lunationDegreeGroupDisplayOrder_.size());
    for (int row = 0; row < lunationDegreeGroupDisplayOrder_.size(); ++row) {
        const int groupIndex = lunationDegreeGroupDisplayOrder_[row];
        const auto& group = lunationDegreeGroups_[groupIndex];
        const QString degreeLabel = formatDegreeDms(group.degree);
        rightTopTable_->setItem(row, 0, makeCell(degreeLabel));
        rightTopTable_->setItem(row, 1, makeCell(QString::number(group.eventIndices.size()), Qt::AlignCenter));

        QSet<int> signSet;
        QSet<int> houseSet;
        QSet<QString> types;
        for (int idx : group.eventIndices) {
            if (idx < 0 || idx >= lunationResults_.size()) {
                continue;
            }
            const auto& res = lunationResults_[idx];
            signSet.insert(signIndex(res.moonLon));
            if (hasCurrentChart_) {
                const int house = calcHouseForLongitude(res.moonLon, natalPlacidusCusps_, currentChart_.angles.asc, currentInput_.houseSystem);
                if (house > 0) {
                    houseSet.insert(house);
                }
            }
            types.insert(res.event);
        }
        QString signLabel = "-";
        if (!signSet.isEmpty()) {
            if (signSet.size() == 1) {
                signLabel = signName(*signSet.begin());
            } else {
                signLabel = "Mixed";
            }
        }
        QString houseLabel = "-";
        if (hasCurrentChart_ && !houseSet.isEmpty()) {
            if (houseSet.size() == 1) {
                houseLabel = QString::number(*houseSet.begin());
            } else {
                houseLabel = "Mixed";
            }
        }
        rightTopTable_->setItem(row, 2, makeCell(signLabel));
        rightTopTable_->setItem(row, 3, makeCell(houseLabel));

        QStringList typeList = types.values();
        std::sort(typeList.begin(), typeList.end());
        const QString typeLabel = typeList.isEmpty() ? "-" : typeList.join(" | ");
        auto* typeItem = makeCell(typeLabel);
        typeItem->setToolTip(typeLabel);
        rightTopTable_->setItem(row, 4, typeItem);
    }

    showLunationGroupDetails(lunationDegreeGroupDisplayOrder_.front());
    if (rightTopTable_) {
        rightTopTable_->selectRow(0);
    }
    updateLunationCopyButtonState();
}

void MainWindow::showLunationGroupDetails(int groupIndex) {
    if (!rightBottomTable_) {
        return;
    }
    if (groupIndex < 0 || groupIndex >= lunationDegreeGroups_.size()) {
        updateLunationCopyButtonState();
        return;
    }
    lunationSelectedGroupIndex_ = groupIndex;
    const auto& group = lunationDegreeGroups_[groupIndex];
    lunationBottomEventOrder_.clear();
    lunationBottomEventOrder_.reserve(group.eventIndices.size());
    QVector<int> indices = group.eventIndices;
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        if (a < 0 || a >= lunationResults_.size()) return false;
        if (b < 0 || b >= lunationResults_.size()) return true;
        return lunationResults_[a].timeUtc < lunationResults_[b].timeUtc;
    });
    setupTable(rightBottomTable_, {"Date", "Time", "Event", "Moon Deg", "Sign", "House"}, indices.size());
    for (int row = 0; row < indices.size(); ++row) {
        const int idx = indices[row];
        if (idx < 0 || idx >= lunationResults_.size()) {
            continue;
        }
        lunationBottomEventOrder_.push_back(idx);
        const auto& res = lunationResults_[idx];
        rightBottomTable_->setItem(row, 0, makeCell(res.timeLocal.toString("ddd, MMM d, yyyy")));
        rightBottomTable_->setItem(row, 1, makeCell(res.timeLocal.toString("hh:mm AP"), Qt::AlignHCenter | Qt::AlignVCenter));
        rightBottomTable_->setItem(row, 2, makeCell(res.event));
        rightBottomTable_->setItem(row, 3, makeCell(formatDegOnly(res.moonLon)));
        rightBottomTable_->setItem(row, 4, makeCell(signName(signIndex(res.moonLon))));
        QString houseLabel = "-";
        if (hasCurrentChart_) {
            const int house = calcHouseForLongitude(res.moonLon, natalPlacidusCusps_, currentChart_.angles.asc, currentInput_.houseSystem);
            if (house > 0) {
                houseLabel = QString::number(house);
            }
        }
        rightBottomTable_->setItem(row, 5, makeCell(houseLabel));
    }
    updateLunationCopyButtonState();
}

void MainWindow::showLunationDetails(const LunationResult& result) {
    if (!rightBottomTable_) {
        return;
    }
    lunationBottomEventOrder_.clear();
    const bool hasEclipse = !result.eclipseType.isEmpty();
    const bool hasMomentPlacements = hasTransitChart_
        && hasLunationSelection_
        && lastLunationSelection_.timeUtc == result.timeUtc
        && lastLunationSelection_.event == result.event;
    const bool hasFixedStarInfo = hasMomentPlacements && !currentTransitChart_.fixedStars.isEmpty();
    const int placementRows = hasMomentPlacements ? currentTransitChart_.bodies.size() : 0;
    const int fixedStarRows = hasFixedStarInfo ? 2 : 0;
    const int totalRows = (hasEclipse ? 7 : 6) + fixedStarRows + (hasMomentPlacements ? 1 + placementRows : 1);
    setupTable(rightBottomTable_, {"Item", "Value"}, totalRows);
    int row = 0;
    rightBottomTable_->setItem(row, 0, makeCell("Local Time"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.timeLocal.toString("yyyy-MM-dd HH:mm:ss")));
    rightBottomTable_->setItem(row, 0, makeCell("UTC Time"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.timeUtc.toString("yyyy-MM-dd HH:mm:ss")));
    rightBottomTable_->setItem(row, 0, makeCell("Timezone"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.tzLabel));
    rightBottomTable_->setItem(row, 0, makeCell("Event"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.event));
    if (hasEclipse) {
        rightBottomTable_->setItem(row, 0, makeCell("Eclipse Type"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.eclipseType));
    }
    rightBottomTable_->setItem(row, 0, makeCell("Sun"));
    rightBottomTable_->setItem(row++, 1, makeCell(formatDegInSign(result.sunLon)));
    rightBottomTable_->setItem(row, 0, makeCell("Moon"));
    rightBottomTable_->setItem(row++, 1, makeCell(formatDegInSign(result.moonLon)));
    if (hasFixedStarInfo) {
        auto nearestStarLine = [&](double lon) -> QString {
            const FixedStarPosition* bestStar = nullptr;
            double bestOrb = 999.0;
            for (const auto& star : currentTransitChart_.fixedStars) {
                const double orb = angularDiffAbs(lon, star.longitude);
                if (!bestStar || orb < bestOrb) {
                    bestStar = &star;
                    bestOrb = orb;
                }
            }
            if (!bestStar) {
                return "-";
            }
            return QString("%1 (orb %2 deg)")
                .arg(bestStar->name)
                .arg(QString::number(bestOrb, 'f', 2));
        };
        rightBottomTable_->setItem(row, 0, makeCell("Sun fixed star"));
        rightBottomTable_->setItem(row++, 1, makeCell(nearestStarLine(result.sunLon)));
        rightBottomTable_->setItem(row, 0, makeCell("Moon fixed star"));
        rightBottomTable_->setItem(row++, 1, makeCell(nearestStarLine(result.moonLon)));
    }

    if (!hasMomentPlacements) {
        rightBottomTable_->setItem(row, 0, makeCell("Placements"));
        rightBottomTable_->setItem(row++, 1, makeCell("Select this event to load full moment placements."));
        updateLunationCopyButtonState();
        return;
    }

    rightBottomTable_->setItem(row, 0, makeCell("Placements"));
    rightBottomTable_->setItem(row++, 1, makeCell("Deg in sign (House, Motion)"));

    QMap<QString, BodyPosition> bodyMap;
    for (const auto& body : currentTransitChart_.bodies) {
        bodyMap.insert(body.name, body);
    }
    for (const auto& name : tropicalBodyOrder()) {
        if (!bodyMap.contains(name)) {
            continue;
        }
        const auto body = bodyMap.value(name);
        QString suffix;
        if (body.house > 0) {
            suffix = QString(" (H%1").arg(body.house);
            if (body.retrograde) {
                suffix += ", R";
            } else {
                suffix += ", D";
            }
            suffix += ")";
        } else if (body.retrograde) {
            suffix = " (R)";
        } else {
            suffix = " (D)";
        }
        rightBottomTable_->setItem(row, 0, makeCell(body.name));
        rightBottomTable_->setItem(row++, 1, makeCell(formatDegInSign(body.longitude) + suffix));
        bodyMap.remove(name);
    }
    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        const auto& body = it.value();
        QString suffix;
        if (body.house > 0) {
            suffix = QString(" (H%1").arg(body.house);
            if (body.retrograde) {
                suffix += ", R";
            } else {
                suffix += ", D";
            }
            suffix += ")";
        } else if (body.retrograde) {
            suffix = " (R)";
        } else {
            suffix = " (D)";
        }
        rightBottomTable_->setItem(row, 0, makeCell(body.name));
        rightBottomTable_->setItem(row++, 1, makeCell(formatDegInSign(body.longitude) + suffix));
    }
    updateLunationCopyButtonState();
}

void MainWindow::applyLunationResult(const LunationResult& result) {
    QString precheckErr;
    if (!canApplyLunationResult(&precheckErr)) {
        if (!precheckErr.isEmpty()) {
            setStatusMessage(precheckErr);
        }
        return;
    }
    NatalChart chart;
    QString err;
    if (!computeTransitChartAt(result.timeLocal, result.tzLabel, &chart, &err)) {
        setStatusMessage(err);
        return;
    }
    currentTransitChart_ = chart;
    hasTransitChart_ = true;
    transitPending_ = false;
    lastTransitCalculated_ = QDateTime::currentDateTime();
    hasLunationSelection_ = true;
    lastLunationSelection_ = result;

    auto populateOverlayAspects = [&](const NatalChart& transitChart) {
        switch (transitAspectView_) {
            case TransitAspectView::TransitTransit:
                populateAspects(transitChart);
                break;
            case TransitAspectView::NatalNatal:
                populateAspects(currentChart_);
                break;
            case TransitAspectView::TransitNatal:
            default:
                populateTransitAspectsOverlay(transitChart, currentChart_);
                break;
        }
    };

    const bool overlayMode = (transitMode_ == TransitMode::NatalOverlay && hasCurrentChart_);
    if (overlayMode) {
        if (chartWheel_) {
            chartWheel_->setShowAspects(true);
            chartWheel_->setOverlayLabel("Transit");
            chartWheel_->setOverlayCharts(currentChart_, chart, transitHouseSystem_, aspectOrbs_);
            chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
            chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        }
        populateOverlayAspects(chart);
    } else {
        if (chartWheel_) {
            chartWheel_->setTransitChart(chart, transitHouseSystem_);
            chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        }
        populateAspects(chart);
    }
    showLunationDetails(result);
}

bool MainWindow::computeTransitChartAt(const QDateTime& localTime, const QString& tzLabel, NatalChart* out, QString* error) {
    return computeTransitChart(localTime, tzLabel, out, error);
}

bool MainWindow::canApplyLunationResult(QString* error) const {
    if (!swe_.isLoaded()) {
        if (error) {
            *error = "Swiss Ephemeris is not loaded.";
        }
        return false;
    }
    if (ephePath_.isEmpty()) {
        if (error) {
            *error = "Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.";
        }
        return false;
    }
    if (transitMode_ == TransitMode::NatalOverlay && !hasCurrentChart_) {
        if (error) {
            *error = "Load a natal chart first to compute transits.";
        }
        return false;
    }
    if (transitMode_ == TransitMode::TransitOnly && !hasCurrentChart_) {
        const QString loc = transitLocationEdit_ ? transitLocationEdit_->text().trimmed() : QString();
        const double lat = transitLatSpin_ ? transitLatSpin_->value() : 0.0;
        const double lon = transitLonSpin_ ? transitLonSpin_->value() : 0.0;
        if (loc.isEmpty() && std::abs(lat) < 0.0001 && std::abs(lon) < 0.0001) {
            if (error) {
                *error = "Set transit location or lat/long for transit-only mode.";
            }
            return false;
        }
    }
    return true;
}

bool MainWindow::isSolarTechniqueTabActive() const {
    if (activeTab_ != AppTab::SolarReturn) {
        return false;
    }
    if (!tabs_ || !solarTechniquePanel_) {
        return false;
    }
    return tabs_->currentWidget() == solarTechniquePanel_;
}

void MainWindow::updateSolarTechniqueDockTitles() {
    if (!rightTopDock_ || !rightBottomDock_) {
        return;
    }
    if (activeTab_ == AppTab::SolarReturn && isSolarTechniqueTabActive()) {
        rightTopDock_->setWindowTitle("SR Technique Hits");
        rightBottomDock_->setWindowTitle("SR Technique Details");
        return;
    }
    if (activeTab_ == AppTab::SolarReturn) {
        rightTopDock_->setWindowTitle("Solar Return");
        rightBottomDock_->setWindowTitle("Solar-Natal");
    }
}

void MainWindow::handleProgressionNow() {
    if (!progressionDateEdit_ || !progressionTimeEdit_) {
        return;
    }
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = progressionTimezoneEdit_ ? progressionTimezoneEdit_->text().trimmed() : QString("UTC");
    if (!parseTimezoneInput(tzText, &tz, &label, &err)) {
        tz = QTimeZone::utc();
        label = "UTC";
    }
    const QDateTime nowLocal = QDateTime::currentDateTimeUtc().toTimeZone(tz);
    progressionDateEdit_->setDate(nowLocal.date());
    progressionTimeEdit_->setTime(nowLocal.time());
    if (progressionTimezoneEdit_) {
        progressionTimezoneEdit_->setText(label);
    }
    updateProgressionTimezoneStatus();
    markProgressionPending();
}

void MainWindow::handleProgressionCalculate() {
    if (!hasCurrentChart_) {
        setStatusMessage("Load a natal chart first to compute progressions.");
        return;
    }
    if (ephePath_.isEmpty()) {
        setStatusMessage("Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.");
        return;
    }
    NatalChart chart;
    QString err;
    const QDateTime targetLocal = progressionTargetLocal();
    const QString tzLabel = progressionTimezoneLabel();
    if (!computeProgressionChart(targetLocal, tzLabel, &chart, &err)) {
        setStatusMessage(err);
        return;
    }
    currentProgressionChart_ = chart;
    hasProgressionChart_ = true;
    progressionPending_ = false;
    lastProgressionCalculated_ = QDateTime::currentDateTime();

    currentProgressionInput_ = currentInput_;
    currentProgressionInput_.date = chart.localDateTime.date();
    currentProgressionInput_.time = chart.localDateTime.time();
    currentProgressionInput_.timezone = chart.timezoneLabel;
    currentProgressionInput_.aspectOrbs = aspectOrbs_;

    updateProgressionStatusLabels();
    if (activeTab_ == AppTab::Progression) {
        refreshProgressionView();
    }
    refreshNatalReport();
}

void MainWindow::handleProgressionViewChanged() {
    if (progressionViewNatalRadio_ && progressionViewNatalRadio_->isChecked()) {
        progressionView_ = ProgressionView::NatalOnly;
    } else if (progressionViewOverlayRadio_ && progressionViewOverlayRadio_->isChecked()) {
        progressionView_ = ProgressionView::Overlay;
    } else {
        progressionView_ = ProgressionView::ProgressedOnly;
    }
    if (activeTab_ == AppTab::Progression) {
        refreshProgressionView();
    }
    refreshNatalReport();
}

void MainWindow::showProgressionPlaceholder() {
    const QString message = hasCurrentChart_
        ? (progressionPending_ ? "Pending changes. Click Calculate Progression."
                               : "Enter target date/time and click Calculate Progression.")
        : "Load a natal chart to compute progressions.";
    if (summaryTable_) {
        setupTable(summaryTable_, {"Info"}, 1);
        summaryTable_->setItem(0, 0, makeCell(message));
    }
    if (anglesTable_) {
        setupTable(anglesTable_, {"Info"}, 1);
        anglesTable_->setItem(0, 0, makeCell(message));
    }
    if (planetsTable_) {
        setupTable(planetsTable_, {"Info"}, 1);
        planetsTable_->setItem(0, 0, makeCell(message));
    }
    if (fixedStarsTable_) {
        setupTable(fixedStarsTable_, {"Info"}, 1);
        fixedStarsTable_->setItem(0, 0, makeCell(message));
    }
    if (housesTable_) {
        setupTable(housesTable_, {"Info"}, 1);
        housesTable_->setItem(0, 0, makeCell(message));
    }
    if (aspectsTable_) {
        setupTable(aspectsTable_, {}, 0);
    }
    aspectTriangleEnabled_ = false;
    clearAspectHover();
    if (chartWheel_) {
        chartWheel_->clearChart();
    }
    if (rightTopTable_) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell(message));
    }
    if (rightBottomTable_) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell(message));
    }
}

void MainWindow::refreshProgressionView() {
    if (activeTab_ != AppTab::Progression) {
        return;
    }
    if (!hasCurrentChart_) {
        showProgressionPlaceholder();
        return;
    }

    const bool showNatal = (progressionView_ == ProgressionView::NatalOnly);
    const bool overlay = (progressionView_ == ProgressionView::Overlay);

    if (showNatal) {
        populateSummary(currentChart_, currentInput_, currentLocation_);
        populateAngles(currentChart_);
        populatePlanets(currentChart_);
        populateFixedStars(currentChart_);
        populateHouses(currentChart_, currentInput_.houseSystem);
        populateAspects(currentChart_);
        if (chartWheel_) {
            chartWheel_->setChart(currentChart_, currentInput_.houseSystem);
            chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        }
    } else {
        if (!hasProgressionChart_) {
            showProgressionPlaceholder();
            return;
        }
        populateSummary(currentProgressionChart_, currentProgressionInput_, currentLocation_);
        populateAngles(currentProgressionChart_);
        populatePlanets(currentProgressionChart_);
        populateFixedStars(currentProgressionChart_);
        populateHouses(currentProgressionChart_, currentProgressionInput_.houseSystem);
        if (chartWheel_) {
            if (overlay) {
                chartWheel_->setOverlayLabel("Progressed");
                chartWheel_->setShowAspects(true);
                chartWheel_->setOverlayCharts(currentChart_, currentProgressionChart_, currentProgressionInput_.houseSystem, aspectOrbs_);
                chartWheel_->setOverlayAspectScopes(true, false, false);
            } else {
                chartWheel_->setChart(currentProgressionChart_, currentProgressionInput_.houseSystem);
            }
            chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        }
        if (overlay) {
            populateProgressedAspectsOverlay(currentProgressionChart_, currentChart_);
        } else {
            populateAspects(currentProgressionChart_);
        }
    }
    if (rightTopTable_) {
        const QString info = showNatal ? "Natal chart shown in left panels."
                                       : "Progression chart shown in left panels.";
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell(info));
    }
    if (rightBottomTable_) {
        const QString detail = showNatal ? "Use View options to switch to progressed charts."
                                         : "Use View options to compare natal and progressed charts.";
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell(detail));
    }
    updateProgressionStatusLabels();
    updateChartLegend();
}

void MainWindow::markProgressionPending() {
    progressionPending_ = true;
    updateProgressionStatusLabels();
}

void MainWindow::updateProgressionStatusLabels() {
    if (!progressionStatusLabel_ || !progressionLastLabel_) {
        return;
    }
    if (progressionPending_) {
        progressionStatusLabel_->setText("Pending changes");
        progressionStatusLabel_->setStyleSheet("color: #d4a24a;");
    } else {
        progressionStatusLabel_->setText("Up to date");
        progressionStatusLabel_->setStyleSheet("color: #69c36d;");
    }
    if (lastProgressionCalculated_.isValid()) {
        progressionLastLabel_->setText(QString("Last calculated: %1")
            .arg(lastProgressionCalculated_.toString("yyyy-MM-dd HH:mm:ss")));
    } else {
        progressionLastLabel_->setText("Last calculated: -");
    }
    if (progressionCalculateButton_) {
        progressionCalculateButton_->setEnabled(hasCurrentChart_ && progressionPending_);
    }
}

void MainWindow::updateProgressionTimezoneStatus() {
    if (!progressionTimezoneStatus_) {
        return;
    }
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = progressionTimezoneEdit_ ? progressionTimezoneEdit_->text().trimmed() : QString("UTC");
    if (parseTimezoneInput(tzText, &tz, &label, &err)) {
        progressionTimezoneStatus_->setText("OK");
        progressionTimezoneStatus_->setStyleSheet("color: #69c36d;");
    } else {
        progressionTimezoneStatus_->setText("Invalid");
        progressionTimezoneStatus_->setStyleSheet("color: #e05555;");
    }
}

QDateTime MainWindow::progressionTargetLocal() const {
    if (!progressionDateEdit_ || !progressionTimeEdit_) {
        return QDateTime();
    }
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = progressionTimezoneEdit_ ? progressionTimezoneEdit_->text().trimmed() : QString("UTC");
    if (!parseTimezoneInput(tzText, &tz, &label, &err)) {
        tz = QTimeZone::utc();
    }
    return QDateTime(progressionDateEdit_->date(), progressionTimeEdit_->time(), tz);
}

QString MainWindow::progressionTimezoneLabel() const {
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = progressionTimezoneEdit_ ? progressionTimezoneEdit_->text().trimmed() : QString("UTC");
    if (!parseTimezoneInput(tzText, &tz, &label, &err)) {
        return "UTC";
    }
    return label;
}

bool MainWindow::computeProgressionChart(const QDateTime& localTime, const QString& tzLabel, NatalChart* out, QString* error) {
    if (!hasCurrentChart_) {
        if (error) {
            *error = "Load a natal chart first to compute progressions.";
        }
        return false;
    }
    if (!out) {
        return false;
    }
    NatalInput input = currentInput_;
    input.aspectOrbs = aspectOrbs_;
    const bool ok = progressionEngine_.compute(input, localTime, tzLabel, out, error);
    if (ok && out && !out->warnings.isEmpty() && statusBar()) {
        statusBar()->showMessage(QString("Computed with warnings: %1").arg(out->warnings.join("; ")), 12000);
    }
    return ok;
}

void MainWindow::updateAspectScopeTabs() {
    if (!aspectScopeTabs_) {
        return;
    }
    auto ensureTabs = [this](const QStringList& labels) {
        while (aspectScopeTabs_->count() > labels.size()) {
            aspectScopeTabs_->removeTab(aspectScopeTabs_->count() - 1);
        }
        while (aspectScopeTabs_->count() < labels.size()) {
            aspectScopeTabs_->addTab(labels[aspectScopeTabs_->count()]);
        }
        for (int i = 0; i < labels.size(); ++i) {
            if (aspectScopeTabs_->tabText(i) != labels[i]) {
                aspectScopeTabs_->setTabText(i, labels[i]);
            }
        }
    };

    const bool showTransitTabs = (activeTab_ == AppTab::Transits && transitMode_ == TransitMode::NatalOverlay);
    const bool showSolarTabs = (activeTab_ == AppTab::SolarReturn && !isSolarTechniqueTabActive());
    const bool showRelocationTabs = (activeTab_ == AppTab::Relocation);
    const bool showTabs = showTransitTabs || showSolarTabs || showRelocationTabs;
    aspectScopeTabs_->setVisible(showTabs);
    if (showTransitTabs) {
        ensureTabs({"Transit-Natal", "Transit-Transit", "Natal-Natal"});
        aspectScopeTabs_->setCurrentIndex(static_cast<int>(transitAspectView_));
    } else if (showSolarTabs) {
        ensureTabs({"Solar Return", "Solar-Natal"});
        aspectScopeTabs_->setCurrentIndex(static_cast<int>(solarAspectView_));
    } else if (showRelocationTabs) {
        ensureTabs({"Relocation", "Relocation-Natal"});
        aspectScopeTabs_->setCurrentIndex(static_cast<int>(relocationAspectView_));
    }
}

void MainWindow::updateChartLegend() {
    if (!chartLegendLabel_) {
        return;
    }
    bool showLegend = false;
    QString label;
    if (activeTab_ == AppTab::Transits && transitMode_ == TransitMode::NatalOverlay) {
        showLegend = true;
        label = "Natal (inner) / Transit (outer)";
    } else if (activeTab_ == AppTab::Progression && progressionView_ == ProgressionView::Overlay) {
        showLegend = true;
        label = "Natal (inner) / Progressed (outer)";
    } else if (activeTab_ == AppTab::Relocation && relocationOverlayCheck_ && relocationOverlayCheck_->isChecked()) {
        showLegend = true;
        label = "Natal (inner) / Relocation (outer)";
    }
    if (showLegend) {
        chartLegendLabel_->setText(label);
    }
    chartLegendLabel_->setVisible(showLegend);
}

void MainWindow::markTransitPending() {
    transitPending_ = true;
    updateTransitTargetLabels();
    if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Profections) {
        refreshTransitProfectionTab();
    }
}

void MainWindow::markSolarPending() {
    solarPending_ = true;
    updateSolarStatusLabels();
    refreshSolarTechniqueView();
}

void MainWindow::markRelocationPending() {
    relocationPending_ = true;
    updateRelocationStatusLabels();
}

void MainWindow::applyTransitCalculation() {
    transitPending_ = false;
    lastTransitCalculated_ = QDateTime::currentDateTime();
    refreshTransitsTab();
    updateTransitTargetLabels();
    if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Profections) {
        refreshTransitProfectionTab();
    }
}

void MainWindow::updateTransitTargetLabels() {
    if (!transitTargetLabel_ || !transitStatusLabel_ || !transitLastLabel_) {
        return;
    }
    const QDateTime target = transitSelectedLocal();
    const QString tzLabel = transitTimezoneLabel();
    const QString targetText = target.isValid()
        ? target.toString("yyyy-MM-dd hh:mm:ss AP")
        : QString("-");
    transitTargetLabel_->setText(QString("Transit target: %1 (%2)").arg(targetText, tzLabel));
    if (conjReferenceLabel_) {
        conjReferenceLabel_->setText(QString("Reference: %1 (%2)").arg(targetText, tzLabel));
    }
    if (profectionReferenceLabel_) {
        profectionReferenceLabel_->setText(QString("Reference: %1 (%2)").arg(targetText, tzLabel));
    }
    if (transitPending_) {
        transitStatusLabel_->setText("Pending changes");
        transitStatusLabel_->setStyleSheet("color: #d4a24a;");
    } else {
        transitStatusLabel_->setText("Up to date");
        transitStatusLabel_->setStyleSheet("color: #69c36d;");
    }
    if (lastTransitCalculated_.isValid()) {
        transitLastLabel_->setText(QString("Last calculated: %1")
            .arg(lastTransitCalculated_.toString("yyyy-MM-dd HH:mm:ss")));
    } else {
        transitLastLabel_->setText("Last calculated: -");
    }
    if (transitCalculateButton_) {
        transitCalculateButton_->setEnabled(transitPending_);
    }
}

void MainWindow::updateSolarStatusLabels() {
    if (!solarStatusLabel_ || !solarLastLabel_) {
        return;
    }
    if (solarPending_) {
        solarStatusLabel_->setText("Pending changes");
        solarStatusLabel_->setStyleSheet("color: #d4a24a;");
    } else {
        solarStatusLabel_->setText("Up to date");
        solarStatusLabel_->setStyleSheet("color: #69c36d;");
    }
    if (lastSolarCalculated_.isValid()) {
        solarLastLabel_->setText(QString("Last calculated: %1")
            .arg(lastSolarCalculated_.toString("yyyy-MM-dd HH:mm:ss")));
    } else {
        solarLastLabel_->setText("Last calculated: -");
    }
    if (solarCalculateButton_) {
        solarCalculateButton_->setEnabled(solarPending_);
    }
}

void MainWindow::updateRelocationStatusLabels() {
    if (!relocationStatusLabel_ || !relocationLastLabel_) {
        return;
    }
    if (relocationPending_) {
        relocationStatusLabel_->setText("Pending changes");
        relocationStatusLabel_->setStyleSheet("color: #d4a24a;");
    } else {
        relocationStatusLabel_->setText("Up to date");
        relocationStatusLabel_->setStyleSheet("color: #69c36d;");
    }
    if (lastRelocationCalculated_.isValid()) {
        relocationLastLabel_->setText(QString("Last calculated: %1")
            .arg(lastRelocationCalculated_.toString("yyyy-MM-dd HH:mm:ss")));
    } else {
        relocationLastLabel_->setText("Last calculated: -");
    }
    if (relocationCalculateButton_) {
        relocationCalculateButton_->setEnabled(hasCurrentChart_ && relocationPending_);
    }
}

void MainWindow::updateTransitTimezoneStatus() {
    if (!transitTimezoneStatus_) {
        return;
    }
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = transitTimezoneEdit_ ? transitTimezoneEdit_->text().trimmed() : QString("UTC");
    if (parseTimezoneInput(tzText, &tz, &label, &err)) {
        transitTimezoneStatus_->setText("OK");
        transitTimezoneStatus_->setStyleSheet("color: #69c36d;");
    } else {
        transitTimezoneStatus_->setText("Invalid");
        transitTimezoneStatus_->setStyleSheet("color: #e05555;");
    }
}

void MainWindow::updateTransitLocationAvailability() {
    if (!transitUseNatalLocation_) {
        return;
    }
    const bool hasNatal = hasCurrentChart_;
    if (!hasNatal && transitUseNatalLocation_->isChecked()) {
        QSignalBlocker blocker(transitUseNatalLocation_);
        transitUseNatalLocation_->setChecked(false);
    }
    transitUseNatalLocation_->setEnabled(hasNatal);
    const bool useNatal = hasNatal && transitUseNatalLocation_->isChecked();
    if (transitLocationEdit_) {
        transitLocationEdit_->setEnabled(!useNatal);
    }
    if (transitGeocodeButton_) {
        transitGeocodeButton_->setEnabled(!useNatal);
    }
    if (transitLatSpin_) {
        transitLatSpin_->setEnabled(!useNatal);
    }
    if (transitLonSpin_) {
        transitLonSpin_->setEnabled(!useNatal);
    }
}

void MainWindow::syncTransitLocationFromNatal() {
    if (!hasCurrentChart_) {
        return;
    }
    if (transitLocationEdit_) {
        transitLocationEdit_->setText(currentLocation_);
    }
    if (transitLatSpin_) {
        transitLatSpin_->setValue(currentInput_.latitude);
    }
    if (transitLonSpin_) {
        transitLonSpin_->setValue(currentInput_.longitude);
    }
    if (transitTimezoneEdit_ && !currentInput_.timezone.isEmpty()) {
        transitTimezoneEdit_->setText(currentInput_.timezone);
        updateTransitTimezoneStatus();
    }
}

void MainWindow::handleTransitGeocode() {
    if (!net_) {
        setStatusMessage("Network manager not available.");
        return;
    }
    const QString queryText = transitLocationEdit_ ? transitLocationEdit_->text().trimmed() : QString();
    if (queryText.isEmpty()) {
        setStatusMessage("Enter a place name to geocode.");
        return;
    }
    QUrl url("https://nominatim.openstreetmap.org/search");
    QUrlQuery query;
    query.addQueryItem("format", "json");
    query.addQueryItem("limit", "1");
    query.addQueryItem("q", queryText);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "DracoVedCpp/0.1");
    auto* reply = net_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(QString("Geocoding failed: %1").arg(reply->errorString()));
            return;
        }
        const auto payload = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            setStatusMessage("Unable to parse geocoding response.");
            return;
        }
        const QJsonArray arr = doc.array();
        if (arr.isEmpty() || !arr[0].isObject()) {
            setStatusMessage("No results found for that location.");
            return;
        }
        const QJsonObject obj = arr[0].toObject();
        bool okLat = false;
        bool okLon = false;
        const double lat = obj.value("lat").toString().toDouble(&okLat);
        const double lon = obj.value("lon").toString().toDouble(&okLon);
        if (!okLat || !okLon) {
            setStatusMessage("Geocoding response missing coordinates.");
            return;
        }
        if (transitLatSpin_) {
            transitLatSpin_->setValue(lat);
        }
        if (transitLonSpin_) {
            transitLonSpin_->setValue(lon);
        }
        fetchTransitTimezoneForCoords(lat, lon);
        markTransitPending();
    });
}

void MainWindow::fetchTransitTimezoneForCoords(double lat, double lon) {
    if (!net_) {
        return;
    }
    QUrl url("https://api.open-meteo.com/v1/forecast");
    QUrlQuery query;
    query.addQueryItem("latitude", QString::number(lat, 'f', 6));
    query.addQueryItem("longitude", QString::number(lon, 'f', 6));
    query.addQueryItem("current", "temperature_2m");
    query.addQueryItem("timezone", "auto");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "DracoVedCpp/0.1");
    auto* reply = net_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(QString("Timezone lookup failed: %1").arg(reply->errorString()));
            return;
        }
        const auto payload = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            setStatusMessage("Unable to parse timezone response.");
            return;
        }
        const QJsonObject obj = doc.object();
        const QString tzName = obj.value("timezone").toString().trimmed();
        if (!tzName.isEmpty()) {
            if (transitTimezoneEdit_) {
                transitTimezoneEdit_->setText(tzName);
                updateTransitTimezoneStatus();
            }
            return;
        }
        const int offsetSeconds = obj.value("utc_offset_seconds").toInt();
        if (offsetSeconds != 0 && transitTimezoneEdit_) {
            const int totalMinutes = offsetSeconds / 60;
            const int hours = totalMinutes / 60;
            const int minutes = std::abs(totalMinutes % 60);
            const QString sign = hours >= 0 ? "+" : "-";
            const QString label = QString("UTC%1%2:%3")
                .arg(sign)
                .arg(QString::number(std::abs(hours)).rightJustified(2, '0'))
                .arg(QString::number(minutes).rightJustified(2, '0'));
            transitTimezoneEdit_->setText(label);
            updateTransitTimezoneStatus();
        }
    });
}

void MainWindow::updateSolarLocationAvailability() {
    if (!solarUseNatalRadio_ || !solarUseCustomRadio_) {
        return;
    }
    const bool hasNatal = hasCurrentChart_;
    if (!hasNatal && solarUseNatalRadio_->isChecked()) {
        QSignalBlocker blocker(solarUseNatalRadio_);
        solarUseNatalRadio_->setChecked(false);
        solarUseCustomRadio_->setChecked(true);
    }
    solarUseNatalRadio_->setEnabled(hasNatal);
    const bool useNatal = hasNatal && solarUseNatalRadio_->isChecked();
    if (solarLocationEdit_) {
        solarLocationEdit_->setEnabled(!useNatal);
    }
    if (solarGeocodeButton_) {
        solarGeocodeButton_->setEnabled(!useNatal);
    }
    if (solarLatSpin_) {
        solarLatSpin_->setEnabled(!useNatal);
    }
    if (solarLonSpin_) {
        solarLonSpin_->setEnabled(!useNatal);
    }
}

void MainWindow::updateSolarTimezoneStatus() {
    if (!solarTimezoneStatus_) {
        return;
    }
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = solarTimezoneEdit_ ? solarTimezoneEdit_->text().trimmed() : QString("UTC");
    if (parseTimezoneInput(tzText, &tz, &label, &err)) {
        solarTimezoneStatus_->setText("OK");
        solarTimezoneStatus_->setStyleSheet("color: #69c36d;");
    } else {
        solarTimezoneStatus_->setText("Invalid");
        solarTimezoneStatus_->setStyleSheet("color: #e05555;");
    }
}

void MainWindow::updateRelocationTimezoneStatus() {
    if (!relocationTimezoneStatus_) {
        return;
    }
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = relocationTimezoneEdit_ ? relocationTimezoneEdit_->text().trimmed() : QString("UTC");
    if (parseTimezoneInput(tzText, &tz, &label, &err)) {
        relocationTimezoneStatus_->setText("OK");
        relocationTimezoneStatus_->setStyleSheet("color: #69c36d;");
    } else {
        relocationTimezoneStatus_->setText("Invalid");
        relocationTimezoneStatus_->setStyleSheet("color: #e05555;");
    }
}

void MainWindow::syncSolarLocationFromNatal() {
    if (!hasCurrentChart_) {
        return;
    }
    if (!solarUseNatalRadio_ || !solarUseNatalRadio_->isChecked()) {
        return;
    }
    if (solarLocationEdit_) {
        solarLocationEdit_->setText(currentLocation_);
    }
    if (solarLatSpin_) {
        solarLatSpin_->setValue(currentInput_.latitude);
    }
    if (solarLonSpin_) {
        solarLonSpin_->setValue(currentInput_.longitude);
    }
    if (solarTimezoneEdit_ && !currentInput_.timezone.isEmpty()) {
        solarTimezoneEdit_->setText(currentInput_.timezone);
        updateSolarTimezoneStatus();
    }
}

void MainWindow::handleSolarGeocode() {
    if (!net_) {
        setStatusMessage("Network manager not available.");
        return;
    }
    const QString queryText = solarLocationEdit_ ? solarLocationEdit_->text().trimmed() : QString();
    if (queryText.isEmpty()) {
        setStatusMessage("Enter a place name to geocode.");
        return;
    }
    QUrl url("https://nominatim.openstreetmap.org/search");
    QUrlQuery query;
    query.addQueryItem("format", "json");
    query.addQueryItem("limit", "1");
    query.addQueryItem("q", queryText);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "DracoVedCpp/0.1");
    auto* reply = net_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(QString("Geocoding failed: %1").arg(reply->errorString()));
            return;
        }
        const auto payload = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            setStatusMessage("Unable to parse geocoding response.");
            return;
        }
        const QJsonArray arr = doc.array();
        if (arr.isEmpty() || !arr[0].isObject()) {
            setStatusMessage("No results found for that location.");
            return;
        }
        const QJsonObject obj = arr[0].toObject();
        bool okLat = false;
        bool okLon = false;
        const double lat = obj.value("lat").toString().toDouble(&okLat);
        const double lon = obj.value("lon").toString().toDouble(&okLon);
        if (!okLat || !okLon) {
            setStatusMessage("Geocoding response missing coordinates.");
            return;
        }
        if (solarLatSpin_) {
            solarLatSpin_->setValue(lat);
        }
        if (solarLonSpin_) {
            solarLonSpin_->setValue(lon);
        }
        fetchSolarTimezoneForCoords(lat, lon);
        markSolarPending();
    });
}

void MainWindow::fetchSolarTimezoneForCoords(double lat, double lon) {
    if (!net_) {
        return;
    }
    QUrl url("https://api.open-meteo.com/v1/forecast");
    QUrlQuery query;
    query.addQueryItem("latitude", QString::number(lat, 'f', 6));
    query.addQueryItem("longitude", QString::number(lon, 'f', 6));
    query.addQueryItem("current", "temperature_2m");
    query.addQueryItem("timezone", "auto");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "DracoVedCpp/0.1");
    auto* reply = net_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(QString("Timezone lookup failed: %1").arg(reply->errorString()));
            return;
        }
        const auto payload = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            setStatusMessage("Unable to parse timezone response.");
            return;
        }
        const QJsonObject obj = doc.object();
        const QString tzName = obj.value("timezone").toString().trimmed();
        if (!tzName.isEmpty()) {
            if (solarTimezoneEdit_) {
                solarTimezoneEdit_->setText(tzName);
                updateSolarTimezoneStatus();
            }
            return;
        }
        const int offsetSeconds = obj.value("utc_offset_seconds").toInt();
        if (offsetSeconds != 0 && solarTimezoneEdit_) {
            const int totalMinutes = offsetSeconds / 60;
            const int hours = totalMinutes / 60;
            const int minutes = std::abs(totalMinutes % 60);
            const QString sign = hours >= 0 ? "+" : "-";
            const QString label = QString("UTC%1%2:%3")
                .arg(sign)
                .arg(QString::number(std::abs(hours)).rightJustified(2, '0'))
                .arg(QString::number(minutes).rightJustified(2, '0'));
            solarTimezoneEdit_->setText(label);
            updateSolarTimezoneStatus();
        }
    });
}

void MainWindow::handleRelocationGeocode() {
    if (!net_) {
        setStatusMessage("Network manager not available.");
        return;
    }
    const QString queryText = relocationLocationEdit_ ? relocationLocationEdit_->text().trimmed() : QString();
    if (queryText.isEmpty()) {
        setStatusMessage("Enter a place name to geocode.");
        return;
    }
    QUrl url("https://nominatim.openstreetmap.org/search");
    QUrlQuery query;
    query.addQueryItem("format", "json");
    query.addQueryItem("limit", "1");
    query.addQueryItem("q", queryText);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "DracoVedCpp/0.1");
    auto* reply = net_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(QString("Geocoding failed: %1").arg(reply->errorString()));
            return;
        }
        const auto payload = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            setStatusMessage("Unable to parse geocoding response.");
            return;
        }
        const QJsonArray arr = doc.array();
        if (arr.isEmpty() || !arr[0].isObject()) {
            setStatusMessage("No results found for that location.");
            return;
        }
        const QJsonObject obj = arr[0].toObject();
        bool okLat = false;
        bool okLon = false;
        const double lat = obj.value("lat").toString().toDouble(&okLat);
        const double lon = obj.value("lon").toString().toDouble(&okLon);
        if (!okLat || !okLon) {
            setStatusMessage("Geocoding response missing coordinates.");
            return;
        }
        if (relocationLatSpin_) {
            relocationLatSpin_->setValue(lat);
        }
        if (relocationLonSpin_) {
            relocationLonSpin_->setValue(lon);
        }
        fetchRelocationTimezoneForCoords(lat, lon);
        markRelocationPending();
    });
}

void MainWindow::fetchRelocationTimezoneForCoords(double lat, double lon) {
    if (!net_) {
        return;
    }
    QUrl url("https://api.open-meteo.com/v1/forecast");
    QUrlQuery query;
    query.addQueryItem("latitude", QString::number(lat, 'f', 6));
    query.addQueryItem("longitude", QString::number(lon, 'f', 6));
    query.addQueryItem("current", "temperature_2m");
    query.addQueryItem("timezone", "auto");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "DracoVedCpp/0.1");
    auto* reply = net_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(QString("Timezone lookup failed: %1").arg(reply->errorString()));
            return;
        }
        const auto payload = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            setStatusMessage("Unable to parse timezone response.");
            return;
        }
        const QJsonObject obj = doc.object();
        const QString tzName = obj.value("timezone").toString().trimmed();
        if (!tzName.isEmpty()) {
            if (relocationTimezoneEdit_) {
                relocationTimezoneEdit_->setText(tzName);
                updateRelocationTimezoneStatus();
            }
            return;
        }
        const int offsetSeconds = obj.value("utc_offset_seconds").toInt();
        if (offsetSeconds != 0 && relocationTimezoneEdit_) {
            const int totalMinutes = offsetSeconds / 60;
            const int hours = totalMinutes / 60;
            const int minutes = std::abs(totalMinutes % 60);
            const QString sign = hours >= 0 ? "+" : "-";
            const QString label = QString("UTC%1%2:%3")
                .arg(sign)
                .arg(QString::number(std::abs(hours)).rightJustified(2, '0'))
                .arg(QString::number(minutes).rightJustified(2, '0'));
            relocationTimezoneEdit_->setText(label);
            updateRelocationTimezoneStatus();
        }
    });
}

QDateTime MainWindow::transitSelectedLocal() const {
    if (!transitDateEdit_ || !transitTimeEdit_) {
        return QDateTime();
    }
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = transitTimezoneEdit_ ? transitTimezoneEdit_->text().trimmed() : QString("UTC");
    if (!parseTimezoneInput(tzText, &tz, &label, &err)) {
        tz = QTimeZone::utc();
    }
    return QDateTime(transitDateEdit_->date(), transitTimeEdit_->time(), tz);
}

QString MainWindow::transitTimezoneLabel() const {
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = transitTimezoneEdit_ ? transitTimezoneEdit_->text().trimmed() : QString("UTC");
    if (!parseTimezoneInput(tzText, &tz, &label, &err)) {
        return "UTC";
    }
    return label;
}

NatalInput MainWindow::transitInputFor(const QDateTime& localTime, const QString& tzLabel) const {
    NatalInput input;
    if (hasCurrentChart_) {
        input = currentInput_;
    }
    input.name = "Transit";
    input.date = localTime.date();
    input.time = localTime.time();
    input.timezone = tzLabel;
    input.houseSystem = transitHouseSystem_;
    input.aspectOrbs = aspectOrbs_;
    if (input.fixedStars.isEmpty()) {
        input.fixedStars = fixedStarCatalog();
    }
    if (transitUseNatalLocation_ && transitUseNatalLocation_->isChecked() && hasCurrentChart_) {
        input.latitude = currentInput_.latitude;
        input.longitude = currentInput_.longitude;
    } else {
        input.latitude = transitLatSpin_ ? transitLatSpin_->value() : currentInput_.latitude;
        input.longitude = transitLonSpin_ ? transitLonSpin_->value() : currentInput_.longitude;
    }
    return input;
}

bool MainWindow::computeTransitChart(const QDateTime& localTime, const QString& tzLabel, NatalChart* out, QString* error) {
    if (!hasCurrentChart_ && transitMode_ == TransitMode::NatalOverlay) {
        if (error) {
            *error = "Load a natal chart first to compute transits.";
        }
        return false;
    }
    if (!hasCurrentChart_ && transitMode_ == TransitMode::TransitOnly) {
        const QString loc = transitLocationEdit_ ? transitLocationEdit_->text().trimmed() : QString();
        const double lat = transitLatSpin_ ? transitLatSpin_->value() : 0.0;
        const double lon = transitLonSpin_ ? transitLonSpin_->value() : 0.0;
        if (loc.isEmpty() && std::abs(lat) < 0.0001 && std::abs(lon) < 0.0001) {
            if (error) {
                *error = "Set transit location or lat/long for transit-only mode.";
            }
            return false;
        }
    }
    if (!out) {
        return false;
    }
    const NatalInput input = transitInputFor(localTime, tzLabel);
    const bool ok = engine_.compute(input, out, error);
    if (ok && out && !out->warnings.isEmpty() && statusBar()) {
        statusBar()->showMessage(QString("Computed with warnings: %1").arg(out->warnings.join("; ")), 12000);
    }
    return ok;
}

bool MainWindow::solarReturnTimeUtc(int year, const QString& tzLabel, double targetLon, QDateTime* outUtc, QDateTime* outLocal, QString* error) {
    if (!hasCurrentChart_) {
        if (error) {
            *error = "Load a natal chart first to compute solar return.";
        }
        return false;
    }
    QTimeZone tz;
    QString normLabel;
    QString tzErr;
    if (!parseTimezoneInput(tzLabel, &tz, &normLabel, &tzErr)) {
        if (error) {
            *error = tzErr;
        }
        return false;
    }

    const int month = currentInput_.date.month();
    const int day = currentInput_.date.day();
    QDate baseDate(year, month, day);
    if (!baseDate.isValid()) {
        baseDate = QDate(year, month, 1).addMonths(1).addDays(-1);
    }
    QDateTime baseLocal(baseDate, currentInput_.time, tz);
    if (!baseLocal.isValid()) {
        baseLocal = QDateTime(baseDate, QTime(12, 0, 0), tz);
        if (!baseLocal.isValid()) {
            if (error) {
                *error = "Invalid solar return base date/time.";
            }
            return false;
        }
    }
    const QDateTime baseUtc = baseLocal.toUTC();
    const double target = normalizeDegrees(targetLon);

    auto sunLongitudeAtUtc = [&](const QDateTime& utc, double* outLon) -> bool {
        double hourDec = utc.time().hour() + utc.time().minute() / 60.0 + utc.time().second() / 3600.0
            + utc.time().msec() / 3600000.0;
        const double jd = swe_.julianDay(utc.date().year(), utc.date().month(), utc.date().day(), hourDec, SE_GREG_CAL);
        QString calcErr;
        double lon = 0.0;
        if (!swe_.calcUt(jd, SE_SUN, 0, &lon, &calcErr)) {
            if (error) {
                *error = QString("Failed to compute Sun longitude: %1").arg(calcErr);
            }
            return false;
        }
        if (outLon) {
            *outLon = normalizeDegrees(lon);
        }
        return true;
    };

    double baseLon = 0.0;
    if (!sunLongitudeAtUtc(baseUtc, &baseLon)) {
        return false;
    }
    double baseDiff = angularDiffSigned(baseLon, target);
    if (std::fabs(baseDiff) < 1e-6) {
        if (outUtc) {
            *outUtc = baseUtc;
        }
        if (outLocal) {
            *outLocal = baseUtc.toTimeZone(tz);
        }
        return true;
    }

    const int stepHours = 6;
    const int rangeDays = 7;
    const int maxSteps = (rangeDays * 24) / stepHours;

    auto findBracket = [&](int direction, QDateTime* lo, QDateTime* hi, double* diffLo, double* diffHi) -> bool {
        QDateTime prevTime = baseUtc;
        double prevDiff = baseDiff;
        for (int i = 1; i <= maxSteps; ++i) {
            const QDateTime nextTime = baseUtc.addSecs(direction * i * stepHours * 3600);
            double lon = 0.0;
            if (!sunLongitudeAtUtc(nextTime, &lon)) {
                return false;
            }
            const double diff = angularDiffSigned(lon, target);
            if ((prevDiff <= 0.0 && diff >= 0.0) || (prevDiff >= 0.0 && diff <= 0.0)) {
                if (direction > 0) {
                    *lo = prevTime;
                    *hi = nextTime;
                    *diffLo = prevDiff;
                    *diffHi = diff;
                } else {
                    *lo = nextTime;
                    *hi = prevTime;
                    *diffLo = diff;
                    *diffHi = prevDiff;
                }
                return true;
            }
            prevTime = nextTime;
            prevDiff = diff;
        }
        return false;
    };

    QDateTime lo;
    QDateTime hi;
    double diffLo = 0.0;
    double diffHi = 0.0;
    bool bracketFound = findBracket(1, &lo, &hi, &diffLo, &diffHi);
    if (!bracketFound) {
        bracketFound = findBracket(-1, &lo, &hi, &diffLo, &diffHi);
    }
    if (!bracketFound) {
        if (error) {
            *error = "Unable to find solar return time within +/- 7 days of the natal date.";
        }
        return false;
    }

    for (int i = 0; i < 32; ++i) {
        const QDateTime mid = midTimeUtc(lo, hi);
        double lon = 0.0;
        if (!sunLongitudeAtUtc(mid, &lon)) {
            return false;
        }
        const double diff = angularDiffSigned(lon, target);
        if ((diffLo <= 0.0 && diff >= 0.0) || (diffLo >= 0.0 && diff <= 0.0)) {
            hi = mid;
            diffHi = diff;
        } else {
            lo = mid;
            diffLo = diff;
        }
        if (lo.secsTo(hi) <= 1) {
            break;
        }
    }

    if (outUtc) {
        *outUtc = hi;
    }
    if (outLocal) {
        *outLocal = hi.toTimeZone(tz);
    }
    return true;
}

bool MainWindow::computeSolarReturnChart(int year, const QString& tzLabel, double targetLon, const QString& locationName,
                                         double lat, double lon, NatalChart* out, QString* error) {
    if (!out) {
        return false;
    }
    if (!swe_.isLoaded()) {
        if (error) {
            *error = "Swiss Ephemeris is not loaded.";
        }
        return false;
    }
    if (ephePath_.isEmpty()) {
        if (error) {
            *error = "Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.";
        }
        return false;
    }
    swe_.setEphePath(ephePath_);

    QDateTime local;
    if (!solarReturnTimeUtc(year, tzLabel, targetLon, nullptr, &local, error)) {
        return false;
    }

    QTimeZone tz;
    QString normLabel;
    QString tzErr;
    if (!parseTimezoneInput(tzLabel, &tz, &normLabel, &tzErr)) {
        if (error) {
            *error = tzErr;
        }
        return false;
    }

    NatalInput input;
    if (hasCurrentChart_) {
        input = currentInput_;
    }
    input.name = QString("Solar Return %1").arg(year);
    input.date = local.date();
    input.time = local.time();
    input.timezone = normLabel;
    input.latitude = lat;
    input.longitude = lon;
    input.houseSystem = currentInput_.houseSystem;
    input.aspectOrbs = aspectOrbs_;

    NatalChart chart;
    if (!engine_.compute(input, &chart, error)) {
        return false;
    }

    *out = chart;
    currentSolarInput_ = input;
    currentSolarLocation_ = locationName;
    return true;
}

void MainWindow::refreshNatalTransitsPanels() {
    if (activeTab_ != AppTab::Natal) {
        return;
    }
    if (!hasCurrentChart_) {
        if (rightTopTable_) {
            setupTable(rightTopTable_, {"Info"}, 1);
            rightTopTable_->setItem(0, 0, makeCell("Load a natal chart to view transits."));
        }
        if (rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Load a natal chart to view ingress."));
        }
        return;
    }
    const QString tzLabel = currentInput_.timezone.isEmpty() ? QString("UTC") : currentInput_.timezone;
    QTimeZone tz;
    QString normLabel;
    QString err;
    if (!parseTimezoneInput(tzLabel, &tz, &normLabel, &err)) {
        tz = QTimeZone::utc();
        normLabel = "UTC";
    }
    const QDateTime nowLocal = QDateTime::currentDateTimeUtc().toTimeZone(tz);
    NatalInput input = currentInput_;
    input.date = nowLocal.date();
    input.time = nowLocal.time();
    input.timezone = normLabel;
    input.aspectOrbs = aspectOrbs_;
    NatalChart transitChart;
    if (!engine_.compute(input, &transitChart, &err)) {
        setStatusMessage(err);
        return;
    }
    populateCurrentTransits(transitChart, currentChart_);
    populateIngressCountdown(transitChart, input);
}

void MainWindow::handleSolarCalculate() {
    if (!hasCurrentChart_) {
        setStatusMessage("Load a natal chart first to compute solar return.");
        return;
    }
    if (ephePath_.isEmpty()) {
        setStatusMessage("Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.");
        return;
    }
    double natalSunLon = 0.0;
    if (!findBodyLongitude(currentChart_, "Sun", &natalSunLon)) {
        setStatusMessage("Unable to locate natal Sun longitude.");
        return;
    }

    const int year = solarYearSpin_ ? solarYearSpin_->value() : QDate::currentDate().year();
    const QString tzLabel = solarTimezoneEdit_ ? solarTimezoneEdit_->text().trimmed() : QString("UTC");
    const bool useNatal = solarUseNatalRadio_ && solarUseNatalRadio_->isChecked();
    const QString locationName = useNatal
        ? currentLocation_
        : (solarLocationEdit_ ? solarLocationEdit_->text().trimmed() : QString());
    const double lat = useNatal
        ? currentInput_.latitude
        : (solarLatSpin_ ? solarLatSpin_->value() : currentInput_.latitude);
    const double lon = useNatal
        ? currentInput_.longitude
        : (solarLonSpin_ ? solarLonSpin_->value() : currentInput_.longitude);

    if (!useNatal && locationName.isEmpty() && std::abs(lat) < 0.0001 && std::abs(lon) < 0.0001) {
        setStatusMessage("Set a solar return location or coordinates.");
        return;
    }

    NatalChart chart;
    QString err;
    if (!computeSolarReturnChart(year, tzLabel, natalSunLon, locationName, lat, lon, &chart, &err)) {
        setStatusMessage(err);
        return;
    }
    currentSolarChart_ = chart;
    hasSolarChart_ = true;
    solarPending_ = false;
    lastSolarCalculated_ = QDateTime::currentDateTime();
    updateSolarStatusLabels();
    if (activeTab_ == AppTab::SolarReturn) {
        refreshSolarReturnView();
        refreshSolarTechniqueView();
    }
}

void MainWindow::handleRelocationCalculate() {
    if (!hasCurrentChart_) {
        setStatusMessage("Load a natal chart first to compute relocation.");
        return;
    }
    if (ephePath_.isEmpty()) {
        setStatusMessage("Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.");
        return;
    }
    QTimeZone tz;
    QString tzLabel;
    QString tzErr;
    const QString tzText = relocationTimezoneEdit_ ? relocationTimezoneEdit_->text().trimmed() : QString("UTC");
    if (!parseTimezoneInput(tzText, &tz, &tzLabel, &tzErr)) {
        setStatusMessage(tzErr);
        return;
    }

    QDateTime natalUtc = currentChart_.utcDateTime;
    if (!natalUtc.isValid()) {
        QTimeZone natalTz;
        QString natalLabel;
        QString natalErr;
        if (!parseTimezoneInput(currentInput_.timezone, &natalTz, &natalLabel, &natalErr)) {
            natalTz = QTimeZone::utc();
        }
        QDateTime natalLocal(currentInput_.date, currentInput_.time, natalTz);
        natalUtc = natalLocal.toUTC();
    }
    if (!natalUtc.isValid()) {
        setStatusMessage("Invalid natal date/time.");
        return;
    }

    const double lat = relocationLatSpin_ ? relocationLatSpin_->value() : currentInput_.latitude;
    const double lon = relocationLonSpin_ ? relocationLonSpin_->value() : currentInput_.longitude;
    const QString locationName = relocationLocationEdit_ ? relocationLocationEdit_->text().trimmed() : QString();
    if (locationName.isEmpty() && std::abs(lat) < 0.0001 && std::abs(lon) < 0.0001) {
        setStatusMessage("Set a relocation location or coordinates.");
        return;
    }

    if (relocationPlacidusRadio_ && relocationPlacidusRadio_->isChecked()) {
        relocationHouseSystem_ = HouseSystem::Placidus;
    } else {
        relocationHouseSystem_ = HouseSystem::WholeSign;
    }

    const QDateTime relocationLocal = natalUtc.toTimeZone(tz);
    NatalInput input = currentInput_;
    input.date = relocationLocal.date();
    input.time = relocationLocal.time();
    input.timezone = tzLabel;
    input.latitude = lat;
    input.longitude = lon;
    input.houseSystem = relocationHouseSystem_;
    input.aspectOrbs = aspectOrbs_;

    NatalChart chart;
    QString err;
    if (!engine_.compute(input, &chart, &err)) {
        setStatusMessage(err);
        return;
    }

    currentRelocationChart_ = chart;
    currentRelocationInput_ = input;
    currentRelocationLocation_ = locationName;
    hasRelocationChart_ = true;
    relocationPending_ = false;
    lastRelocationCalculated_ = QDateTime::currentDateTime();
    updateRelocationStatusLabels();
    if (activeTab_ == AppTab::Relocation) {
        refreshRelocationView();
    }
}

void MainWindow::showSolarPlaceholder() {
    const QString message = hasCurrentChart_
        ? "Enter inputs and click Calculate Solar Return."
        : "Load a natal chart to compute solar return.";
    if (summaryTable_) {
        setupTable(summaryTable_, {"Info"}, 1);
        summaryTable_->setItem(0, 0, makeCell(message));
    }
    if (anglesTable_) {
        setupTable(anglesTable_, {"Info"}, 1);
        anglesTable_->setItem(0, 0, makeCell(message));
    }
    if (planetsTable_) {
        setupTable(planetsTable_, {"Info"}, 1);
        planetsTable_->setItem(0, 0, makeCell(message));
    }
    if (fixedStarsTable_) {
        setupTable(fixedStarsTable_, {"Info"}, 1);
        fixedStarsTable_->setItem(0, 0, makeCell(message));
    }
    if (housesTable_) {
        setupTable(housesTable_, {"Info"}, 1);
        housesTable_->setItem(0, 0, makeCell(message));
    }
    if (aspectsTable_) {
        setupTable(aspectsTable_, {}, 0);
    }
    aspectTriangleEnabled_ = false;
    clearAspectHover();
    if (chartWheel_) {
        chartWheel_->clearChart();
    }
    if (rightTopTable_) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell(message));
    }
    if (rightBottomTable_) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell(message));
    }
}

void MainWindow::refreshSolarReturnView() {
    if (activeTab_ != AppTab::SolarReturn) {
        return;
    }
    if (!hasCurrentChart_ || !hasSolarChart_) {
        showSolarPlaceholder();
        return;
    }
    populateSummary(currentSolarChart_, currentSolarInput_, currentSolarLocation_);
    populateAngles(currentSolarChart_);
    populatePlanets(currentSolarChart_);
    populateFixedStars(currentSolarChart_);
    populateHouses(currentSolarChart_, currentSolarInput_.houseSystem);
    if (chartWheel_) {
        chartWheel_->setChart(currentSolarChart_, currentSolarInput_.houseSystem);
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
    }
    switch (solarAspectView_) {
        case SolarAspectView::SolarNatal:
            populateSolarNatalAspectsOverlay(currentSolarChart_, currentChart_);
            break;
        case SolarAspectView::SolarReturn:
        default:
            populateAspects(currentSolarChart_);
            break;
    }
}

void MainWindow::showRelocationPlaceholder() {
    const QString message = hasCurrentChart_
        ? (relocationPending_ ? "Pending changes. Click Calculate Relocation."
                              : "Enter relocation inputs and click Calculate Relocation.")
        : "Load a natal chart to compute relocation.";
    if (summaryTable_) {
        setupTable(summaryTable_, {"Info"}, 1);
        summaryTable_->setItem(0, 0, makeCell(message));
    }
    if (anglesTable_) {
        setupTable(anglesTable_, {"Info"}, 1);
        anglesTable_->setItem(0, 0, makeCell(message));
    }
    if (planetsTable_) {
        setupTable(planetsTable_, {"Info"}, 1);
        planetsTable_->setItem(0, 0, makeCell(message));
    }
    if (fixedStarsTable_) {
        setupTable(fixedStarsTable_, {"Info"}, 1);
        fixedStarsTable_->setItem(0, 0, makeCell(message));
    }
    if (housesTable_) {
        setupTable(housesTable_, {"Info"}, 1);
        housesTable_->setItem(0, 0, makeCell(message));
    }
    if (aspectsTable_) {
        setupTable(aspectsTable_, {}, 0);
    }
    aspectTriangleEnabled_ = false;
    clearAspectHover();
    if (chartWheel_) {
        chartWheel_->clearChart();
    }
    if (rightTopTable_) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell(message));
    }
    if (rightBottomTable_) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell(message));
    }
}

void MainWindow::refreshRelocationView() {
    if (activeTab_ != AppTab::Relocation) {
        return;
    }
    if (!hasCurrentChart_ || !hasRelocationChart_) {
        showRelocationPlaceholder();
        return;
    }
    populateSummary(currentRelocationChart_, currentRelocationInput_, currentRelocationLocation_);
    populateAngles(currentRelocationChart_);
    populatePlanets(currentRelocationChart_);
    populateFixedStars(currentRelocationChart_);
    populateHouses(currentRelocationChart_, currentRelocationInput_.houseSystem);
    if (chartWheel_) {
        if (relocationOverlayCheck_ && relocationOverlayCheck_->isChecked()) {
            chartWheel_->setOverlayLabel("Relocation");
            chartWheel_->setShowAspects(true);
            chartWheel_->setOverlayCharts(currentChart_, currentRelocationChart_, currentRelocationInput_.houseSystem, aspectOrbs_);
            chartWheel_->setOverlayAspectScopes(true, false, false);
        } else {
            chartWheel_->setChart(currentRelocationChart_, currentRelocationInput_.houseSystem);
        }
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
    }
    switch (relocationAspectView_) {
        case RelocationAspectView::RelocationNatal:
            populateRelocationNatalAspectsOverlay(currentRelocationChart_, currentChart_);
            break;
        case RelocationAspectView::Relocation:
        default:
            populateAspects(currentRelocationChart_);
            break;
    }
    if (rightTopTable_) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Relocation chart shown in left panels."));
    }
    if (rightBottomTable_) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Use Aspect Scope to compare Relocation and Natal charts."));
    }
    updateRelocationStatusLabels();
    updateChartLegend();
}

void MainWindow::refreshSolarTechniqueView() {
    if (!isSolarTechniqueTabActive()) {
        return;
    }
    auto setInfo = [&](const QString& message) {
        if (solarTechniqueRangeLabel_) {
            solarTechniqueRangeLabel_->setText(message);
        }
        if (rightTopTable_) {
            setupTable(rightTopTable_, {"Info"}, 1);
            rightTopTable_->setItem(0, 0, makeCell(message));
        }
        if (rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell(message));
        }
    };

    updateSolarTechniqueDockTitles();

    if (!hasCurrentChart_) {
        setInfo("Load a natal chart to use the SR technique.");
        return;
    }
    if (!hasSolarChart_) {
        setInfo("Calculate Solar Return to use the SR technique.");
        return;
    }
    if (solarPending_) {
        setInfo("Pending changes. Click Calculate Solar Return.");
        return;
    }
    if (!solarTechniqueDateEdit_ || !solarTechniqueOrbSpin_) {
        return;
    }

    const bool includeNatal = solarTechniqueNatalCheck_ && solarTechniqueNatalCheck_->isChecked();
    const bool includeSolar = solarTechniqueSolarCheck_ && solarTechniqueSolarCheck_->isChecked();
    if (!includeNatal && !includeSolar) {
        setInfo("Select Natal and/or Solar Return targets.");
        return;
    }

    double natalSunLon = 0.0;
    if (!findBodyLongitude(currentChart_, "Sun", &natalSunLon)) {
        setInfo("Natal Sun longitude not found.");
        return;
    }

    QString tzLabel = currentSolarInput_.timezone;
    if (tzLabel.trimmed().isEmpty() && solarTimezoneEdit_) {
        tzLabel = solarTimezoneEdit_->text().trimmed();
    }
    if (tzLabel.trimmed().isEmpty()) {
        tzLabel = "UTC";
    }

    const int solarYear = solarYearSpin_ ? solarYearSpin_->value() : currentSolarInput_.date.year();
    QDateTime startLocal;
    QDateTime nextLocal;
    QString err;
    if (!solarReturnTimeUtc(solarYear, tzLabel, natalSunLon, nullptr, &startLocal, &err)) {
        setInfo(err.isEmpty() ? "Unable to compute solar return date." : err);
        return;
    }
    if (!solarReturnTimeUtc(solarYear + 1, tzLabel, natalSunLon, nullptr, &nextLocal, &err)) {
        setInfo(err.isEmpty() ? "Unable to compute next solar return date." : err);
        return;
    }

    QDate startDate = startLocal.date();
    QDate endDate = nextLocal.date().addDays(-1);
    if (!endDate.isValid() || endDate < startDate) {
        endDate = startDate;
    }
    const qint64 totalDays = std::max<qint64>(1, static_cast<qint64>(startDate.daysTo(endDate)) + 1);
    if (solarTechniqueRangeLabel_) {
        solarTechniqueRangeLabel_->setText(QString("%1 -> %2 (%3 days)")
                                               .arg(startDate.toString("yyyy-MM-dd"))
                                               .arg(endDate.toString("yyyy-MM-dd"))
                                               .arg(totalDays));
    }

    {
        const QSignalBlocker blocker(solarTechniqueDateEdit_);
        solarTechniqueDateEdit_->setMinimumDate(startDate);
        solarTechniqueDateEdit_->setMaximumDate(endDate);
        const QDate currentDate = solarTechniqueDateEdit_->date();
        if (currentDate < startDate || currentDate > endDate) {
            solarTechniqueDateEdit_->setDate(startDate);
        }
    }

    const QDate selectedDate = solarTechniqueDateEdit_->date();
    int dayIndex = startDate.daysTo(selectedDate);
    if (dayIndex < 0) {
        dayIndex = 0;
    }
    if (dayIndex >= totalDays) {
        dayIndex = static_cast<int>(totalDays - 1);
    }

    const double srAsc = currentSolarChart_.angles.asc;

    AspectOrbs orbs;
    const double orbValue = std::max(0.1, solarTechniqueOrbSpin_->value());
    orbs.conjunction = orbValue;
    orbs.sextile = orbValue;
    orbs.square = orbValue;
    orbs.trine = orbValue;
    orbs.opposition = orbValue;

    struct Hit {
        QString text;
        double orb = 0.0;
        bool supportive = false;
        bool challenging = false;
    };

    struct DayRow {
        QDate date;
        double dailyLon = 0.0;
        QString dailyLabel;
        QStringList support;
        QStringList challenge;
        QStringList neutral;
        int supportCount = 0;
        int challengeCount = 0;
        int netScore = 0;
    };

    auto formatHit = [&](const QString& aspectLabel, const QString& scopeLabel, const QString& targetLabel, double orb) {
        return QString("%1 %2 %3 (%4°)")
            .arg(aspectLabel)
            .arg(scopeLabel)
            .arg(targetLabel)
            .arg(QString::number(orb, 'f', 2));
    };

    auto collectHitsForLon = [&](double lon, QStringList* supportOut, QStringList* challengeOut,
                                 QStringList* neutralOut, int* supportOutCount, int* challengeOutCount) {
        QVector<Hit> hits;
        auto addHitsFromChart = [&](const NatalChart& chart, const QString& scopeLabel) {
            auto handleTarget = [&](const QString& name, double targetLon) {
                const double diff = angularDiffAbs(lon, targetLon);
                QString label;
                double orb = 0.0;
                double maxOrb = 0.0;
                if (!aspectForDiff(diff, orbs, &label, &orb, &maxOrb)) {
                    return;
                }
                const bool supportive = (label == "Trine" || label == "Sextile");
                const bool challenging = (label == "Square" || label == "Opposition");
                const QString text = formatHit(label, scopeLabel, aspectHeaderLabel(name), orb);
                hits.push_back({text, orb, supportive, challenging});
            };

            for (const auto& body : chart.bodies) {
                handleTarget(body.name, body.longitude);
            }
            handleTarget("Ascendant", chart.angles.asc);
            handleTarget("Midheaven", chart.angles.mc);
            handleTarget("Descendant", chart.angles.desc);
            handleTarget("IC", chart.angles.ic);
        };

        if (includeNatal) {
            addHitsFromChart(currentChart_, "Natal");
        }
        if (includeSolar) {
            addHitsFromChart(currentSolarChart_, "Solar");
        }

        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
            if (a.orb != b.orb) {
                return a.orb < b.orb;
            }
            return a.text < b.text;
        });

        int supportCount = 0;
        int challengeCount = 0;
        for (const auto& hit : hits) {
            if (hit.supportive) {
                ++supportCount;
                supportOut->push_back(hit.text);
            } else if (hit.challenging) {
                ++challengeCount;
                challengeOut->push_back(hit.text);
            } else {
                neutralOut->push_back(hit.text);
            }
        }
        if (supportOutCount) {
            *supportOutCount = supportCount;
        }
        if (challengeOutCount) {
            *challengeOutCount = challengeCount;
        }
    };

    QVector<DayRow> days;
    days.reserve(static_cast<int>(totalDays));
    for (qint64 i = 0; i < totalDays; ++i) {
        DayRow row;
        row.date = startDate.addDays(static_cast<int>(i));
        row.dailyLon = normalizeDegrees(srAsc + static_cast<double>(i));
        row.dailyLabel = formatDegOnly(row.dailyLon);
        collectHitsForLon(row.dailyLon, &row.support, &row.challenge, &row.neutral,
                          &row.supportCount, &row.challengeCount);
        row.netScore = row.supportCount - row.challengeCount;
        days.push_back(row);
    }

    auto summarizeList = [](const QStringList& items) {
        if (items.isEmpty()) {
            return QString("-");
        }
        const int maxItems = 3;
        if (items.size() <= maxItems) {
            return items.join(", ");
        }
        return QString("%1 (+%2 more)").arg(items.mid(0, maxItems).join(", ")).arg(items.size() - maxItems);
    };

    if (rightTopTable_) {
        const int rows = days.size();
        setupTable(rightTopTable_, {"Date", "Daily Degree", "Support", "Challenge", "Neutral"}, rows);
        const QColor goodColor("#1f8c78");
        const QColor badColor("#d24b4b");
        for (int i = 0; i < rows; ++i) {
            const auto& day = days[i];
            auto* dateItem = makeCell(day.date.toString("yyyy-MM-dd"));
            dateItem->setData(Qt::UserRole, day.date);
            rightTopTable_->setItem(i, 0, dateItem);
            rightTopTable_->setItem(i, 1, makeCell(day.dailyLabel));

            const QString supportText = summarizeList(day.support);
            const QString challengeText = summarizeList(day.challenge);
            const QString neutralText = summarizeList(day.neutral);
            auto* supportItem = makeCell(supportText);
            auto* challengeItem = makeCell(challengeText);
            auto* neutralItem = makeCell(neutralText);
            supportItem->setForeground(goodColor);
            challengeItem->setForeground(badColor);
            supportItem->setToolTip(day.support.join("\n"));
            challengeItem->setToolTip(day.challenge.join("\n"));
            neutralItem->setToolTip(day.neutral.join("\n"));
            rightTopTable_->setItem(i, 2, supportItem);
            rightTopTable_->setItem(i, 3, challengeItem);
            rightTopTable_->setItem(i, 4, neutralItem);
        }
        rightTopTable_->setWordWrap(true);
        rightTopTable_->resizeRowsToContents();
        if (dayIndex >= 0 && dayIndex < rows) {
            rightTopTable_->selectRow(dayIndex);
        }
    }

    if (rightBottomTable_) {
        const int maxIndex = days.isEmpty() ? 0 : static_cast<int>(days.size() - 1);
        const int safeIndex = std::clamp(dayIndex, 0, maxIndex);
        const DayRow& selected = days.isEmpty() ? DayRow{} : days[safeIndex];
        const int totalHits = selected.support.size() + selected.challenge.size() + selected.neutral.size();
        const int dayNumber = safeIndex + 1;
        const QString targetLabel = includeNatal && includeSolar ? "Natal + Solar"
            : (includeNatal ? "Natal" : "Solar Return");
        const QString rangeText = QString("%1 -> %2")
            .arg(startDate.toString("yyyy-MM-dd"))
            .arg(endDate.toString("yyyy-MM-dd"));
        const int topN = std::min<int>(solarTechniqueTopSpin_ ? solarTechniqueTopSpin_->value() : 20, days.size());
        const int rankingRows = topN + 1;
        const int rows = 12 + rankingRows;
        setupTable(rightBottomTable_, {"Item", "Value"}, rows);
        int row = 0;
        rightBottomTable_->setItem(row, 0, makeCell("Date"));
        rightBottomTable_->setItem(row++, 1, makeCell(selected.date.isValid() ? selected.date.toString("yyyy-MM-dd") : "-"));
        rightBottomTable_->setItem(row, 0, makeCell("SR Year Range"));
        rightBottomTable_->setItem(row++, 1, makeCell(rangeText));
        rightBottomTable_->setItem(row, 0, makeCell("Day #"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString("%1 / %2").arg(dayNumber).arg(totalDays)));
        rightBottomTable_->setItem(row, 0, makeCell("Daily Degree"));
        rightBottomTable_->setItem(row++, 1, makeCell(selected.dailyLabel.isEmpty() ? "-" : selected.dailyLabel));
        rightBottomTable_->setItem(row, 0, makeCell("SR Asc Start"));
        rightBottomTable_->setItem(row++, 1, makeCell(formatDegOnly(srAsc)));
        rightBottomTable_->setItem(row, 0, makeCell("Orb"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(orbValue, 'f', 2) + "°"));
        rightBottomTable_->setItem(row, 0, makeCell("Targets"));
        rightBottomTable_->setItem(row++, 1, makeCell(targetLabel));
        rightBottomTable_->setItem(row, 0, makeCell("Support Aspects"));
        rightBottomTable_->setItem(row++, 1, makeCell(selected.support.isEmpty() ? "-" : selected.support.join("\n")));
        rightBottomTable_->setItem(row, 0, makeCell("Challenge Aspects"));
        rightBottomTable_->setItem(row++, 1, makeCell(selected.challenge.isEmpty() ? "-" : selected.challenge.join("\n")));
        rightBottomTable_->setItem(row, 0, makeCell("Neutral Aspects"));
        rightBottomTable_->setItem(row++, 1, makeCell(selected.neutral.isEmpty() ? "-" : selected.neutral.join("\n")));
        rightBottomTable_->setItem(row, 0, makeCell("Hits (Support/Challenge)"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString("%1 (%2 / %3)").arg(totalHits).arg(selected.supportCount).arg(selected.challengeCount)));
        rightBottomTable_->setItem(row, 0, makeCell("Net Tone"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(selected.netScore)));

        rightBottomTable_->setItem(row, 0, makeCell("Top Results"));
        QString metricLabel = "Net";
        if (solarTechniqueRankMetricCombo_) {
            const int metricIndex = solarTechniqueRankMetricCombo_->currentIndex();
            if (metricIndex == 1) {
                metricLabel = "Support";
            } else if (metricIndex == 2) {
                metricLabel = "Challenge";
            }
        }
        QString orderLabel = "High -> Low";
        if (solarTechniqueRankOrderCombo_ && solarTechniqueRankOrderCombo_->currentIndex() == 1) {
            orderLabel = "Low -> High";
        }
        rightBottomTable_->setItem(row++, 1, makeCell(QString("%1 (%2)").arg(metricLabel, orderLabel)));

        QVector<int> indices;
        indices.reserve(days.size());
        for (int i = 0; i < days.size(); ++i) {
            indices.push_back(i);
        }
        const int metricIndex = solarTechniqueRankMetricCombo_ ? solarTechniqueRankMetricCombo_->currentIndex() : 0;
        const bool ascending = solarTechniqueRankOrderCombo_ && solarTechniqueRankOrderCombo_->currentIndex() == 1;
        auto metricValue = [&](const DayRow& day) {
            if (metricIndex == 1) {
                return day.supportCount;
            }
            if (metricIndex == 2) {
                return day.challengeCount;
            }
            return day.netScore;
        };
        std::sort(indices.begin(), indices.end(), [&](int a, int b) {
            const int va = metricValue(days[a]);
            const int vb = metricValue(days[b]);
            if (va != vb) {
                return ascending ? va < vb : va > vb;
            }
            return days[a].date < days[b].date;
        });

        const QColor goodColor("#1f8c78");
        const QColor badColor("#d24b4b");
        for (int i = 0; i < topN; ++i) {
            const DayRow& day = days[indices[i]];
            const QString value = QString("%1 | Net %2 | Support %3 | Challenge %4")
                .arg(day.date.toString("yyyy-MM-dd"))
                .arg(day.netScore)
                .arg(day.supportCount)
                .arg(day.challengeCount);
            rightBottomTable_->setItem(row, 0, makeCell(QString("#%1").arg(i + 1)));
            auto* item = makeCell(value);
            if (day.netScore > 0) {
                item->setForeground(goodColor);
            } else if (day.netScore < 0) {
                item->setForeground(badColor);
            }
            rightBottomTable_->setItem(row++, 1, item);
        }
        rightBottomTable_->setWordWrap(true);
        rightBottomTable_->resizeRowsToContents();
    }
}

void MainWindow::refreshTransitsTab() {
    if (activeTab_ != AppTab::Transits) {
        return;
    }
    const bool inOverview = (transitSubTab_ == TransitSubTab::Overview);
    if (!hasCurrentChart_ && transitMode_ == TransitMode::NatalOverlay) {
        if (inOverview && rightTopTable_) {
            setupTable(rightTopTable_, {"Info"}, 1);
            rightTopTable_->setItem(0, 0, makeCell("Load a natal chart to use transits."));
        }
        if (inOverview && rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Load a natal chart to use transits."));
        }
        if (aspectsTable_) {
            setupTable(aspectsTable_, {}, 0);
        }
        if (chartWheel_) {
            chartWheel_->clearChart();
        }
        return;
    }
    if (!hasCurrentChart_ && transitMode_ == TransitMode::TransitOnly) {
        if (inOverview && rightTopTable_) {
            setupTable(rightTopTable_, {"Info"}, 1);
            rightTopTable_->setItem(0, 0, makeCell("Enter transit inputs and click Calculate."));
        }
        if (inOverview && rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Ingress countdown will appear after calculation."));
        }
        if (aspectsTable_) {
            setupTable(aspectsTable_, {}, 0);
        }
        if (chartWheel_) {
            chartWheel_->clearChart();
        }
        return;
    }

    auto populateOverlayAspects = [&](const NatalChart& transitChart) {
        switch (transitAspectView_) {
            case TransitAspectView::TransitTransit:
                populateAspects(transitChart);
                break;
            case TransitAspectView::NatalNatal:
                populateAspects(currentChart_);
                break;
            case TransitAspectView::TransitNatal:
            default:
                populateTransitAspectsOverlay(transitChart, currentChart_);
                break;
        }
    };

    auto applyTransitView = [&](const NatalChart& transitChart, const QDateTime& local, const QString& tzLabel) {
        if (transitMode_ == TransitMode::NatalOverlay) {
            if (chartWheel_) {
                chartWheel_->setShowAspects(true);
                chartWheel_->setOverlayLabel("Transit");
                chartWheel_->setOverlayCharts(currentChart_, transitChart, transitHouseSystem_, aspectOrbs_);
                chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
                chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
            }
            populateOverlayAspects(transitChart);
            if (inOverview) {
                populateTransitList(transitChart, true);
            }
        } else {
            if (chartWheel_) {
                chartWheel_->setTransitChart(transitChart, transitHouseSystem_);
                chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
            }
            populateAspects(transitChart);
            if (inOverview) {
                populateTransitList(transitChart, false);
            }
        }
        if (inOverview) {
            populateIngressCountdown(transitChart, transitInputFor(local, tzLabel));
        }
    };

    if (transitPending_) {
        updateTransitTargetLabels();
        if (!hasTransitChart_) {
            if (inOverview && rightTopTable_) {
                setupTable(rightTopTable_, {"Info"}, 1);
                rightTopTable_->setItem(0, 0, makeCell("Pending changes. Click Calculate Transits."));
            }
            if (inOverview && rightBottomTable_) {
                setupTable(rightBottomTable_, {"Info"}, 1);
                rightBottomTable_->setItem(0, 0, makeCell("Pending changes. Click Calculate Transits."));
            }
            if (inOverview && aspectsTable_) {
                setupTable(aspectsTable_, {}, 0);
            }
            if (chartWheel_ && hasCurrentChart_ && transitMode_ == TransitMode::NatalOverlay) {
                chartWheel_->setChart(currentChart_, currentInput_.houseSystem);
                chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
            }
            return;
        }
        applyTransitView(currentTransitChart_, transitSelectedLocal(), transitTimezoneLabel());
        return;
    }

    const QDateTime local = transitSelectedLocal();
    const QString tzLabel = transitTimezoneLabel();
    NatalChart transitChart;
    QString err;
    if (!computeTransitChart(local, tzLabel, &transitChart, &err)) {
        setStatusMessage(err);
        return;
    }
    currentTransitChart_ = transitChart;
    hasTransitChart_ = true;
    applyTransitView(transitChart, local, tzLabel);
    updateTransitTargetLabels();
}

void MainWindow::handleSaveProfile() {
    if (!hasCurrentChart_) {
        setStatusMessage("Load or create a chart before saving a profile.");
        return;
    }

    const QString defaultName = !currentProfileName_.isEmpty()
        ? currentProfileName_
        : (currentInput_.name.isEmpty() ? "Profile" : currentInput_.name);

    bool ok = false;
    QString profileName = QInputDialog::getText(
        this,
        "Save Profile",
        "Profile name:",
        QLineEdit::Normal,
        defaultName,
        &ok);
    if (!ok) {
        return;
    }
    saveProfileByName(profileName, true);
}

void MainWindow::handleLoadProfile() {
    const QStringList profiles = listProfiles();
    if (profiles.isEmpty()) {
        refreshProfileToolbar();
        setStatusMessage("No profiles found.");
        return;
    }

    bool ok = false;
    QString profileName = QInputDialog::getItem(
        this,
        "Open Profile",
        "Profile:",
        profiles,
        0,
        false,
        &ok);
    if (!ok) {
        return;
    }
    loadProfileByName(profileName);
}

void MainWindow::handleDeleteProfile() {
    const QStringList profiles = listProfiles();
    if (profiles.isEmpty()) {
        refreshProfileToolbar();
        setStatusMessage("No profiles found.");
        return;
    }

    bool ok = false;
    QString profileName = QInputDialog::getItem(
        this,
        "Delete Profile",
        "Profile:",
        profiles,
        0,
        false,
        &ok);
    if (!ok) {
        return;
    }
    profileName = profileName.trimmed();
    if (profileName.isEmpty()) {
        setStatusMessage("Select a profile to delete.");
        return;
    }

    const QString filePath = profileFilePath(profileName);
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        setStatusMessage("Profile file not found.");
        return;
    }

    const auto result = QMessageBox::question(
        this,
        "Delete profile",
        QString("Delete profile \"%1\"?").arg(profileName),
        QMessageBox::Yes | QMessageBox::No);
    if (result != QMessageBox::Yes) {
        return;
    }
    if (!QFile::remove(filePath)) {
        setStatusMessage("Unable to delete profile.");
        return;
    }
    if (currentProfileName_ == profileName) {
        currentProfileName_.clear();
    }
    refreshProfileToolbar();
}

static void setupTable(QTableWidget* table, const QStringList& headers, int rows) {
    table->clear();
    table->setColumnCount(headers.size());
    table->setRowCount(rows);
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setWordWrap(false);
    table->horizontalHeader()->setHighlightSections(false);
    table->verticalHeader()->setDefaultSectionSize(20);
}

static void setupDetailTable(QTableWidget* table, const QStringList& headers, int rows) {
    if (!table) {
        return;
    }
    setupTable(table, headers, rows);
    table->setWordWrap(true);
    if (auto* vertical = table->verticalHeader()) {
        vertical->setSectionResizeMode(QHeaderView::ResizeToContents);
    }
}

static QTableWidgetItem* makeCell(const QString& text, Qt::Alignment align) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(align);
    return item;
}

static double angularDiff(double a, double b) {
    double d = std::fmod((a - b + 540.0), 360.0) - 180.0;
    return std::fabs(d);
}

static QString aspectSymbolForLabel(const QString& label) {
    if (label == "Conjunction") return QString(QChar(0x260C));
    if (label == "Sextile") return QString(QChar(0x2736));
    if (label == "Square") return QString(QChar(0x25A1));
    if (label == "Trine") return QString(QChar(0x25B3));
    if (label == "Opposition") return QString(QChar(0x260D));
    return "";
}

static QString aspectTargetFromLabel(const QString& text) {
    return transitcalc::aspectTargetFromLabel(text);
}

static bool findAngleLongitude(const NatalChart& chart, const QString& name, double* outLon) {
    return transitcalc::findAngleLongitude(chart, name, outLon);
}

static bool aspectForDiff(double diff, const AspectOrbs& orbs, QString* outLabel, double* outOrb, double* outMaxOrb) {
    return transitcalc::aspectForDiff(diff, orbs, outLabel, outOrb, outMaxOrb);
}

static QString abbrevForName(const QString& name) {
    return transitcalc::abbrevForName(name);
}

static bool findBodyLongitude(const NatalChart& chart, const QString& name, double* outLon) {
    return transitcalc::findBodyLongitude(chart, name, outLon);
}

static int calcHouseForLongitude(double lon, const QVector<HouseCusp>& cusps, double asc, HouseSystem system) {
    return transitcalc::calcHouseForLongitude(lon, cusps, asc, system);
}

void MainWindow::populateSummary(const NatalChart& chart, const NatalInput& input, const QString& location) {
    QStringList headers = {"Item", "Value"};
    setupTable(summaryTable_, headers, 11);
    int r = 0;
    summaryTable_->setItem(r, 0, makeCell("Name"));
    summaryTable_->setItem(r++, 1, makeCell(input.name.isEmpty() ? "-" : input.name));
    summaryTable_->setItem(r, 0, makeCell("Gender"));
    summaryTable_->setItem(r++, 1, makeCell(genderToString(input.gender)));
    summaryTable_->setItem(r, 0, makeCell("Location"));
    summaryTable_->setItem(r++, 1, makeCell(location.isEmpty() ? "-" : location));
    summaryTable_->setItem(r, 0, makeCell("Birth time (Local)"));
    summaryTable_->setItem(r++, 1, makeCell(chart.localDateTime.toString("yyyy-MM-dd HH:mm:ss")));
    summaryTable_->setItem(r, 0, makeCell("Birth time (UTC)"));
    summaryTable_->setItem(r++, 1, makeCell(chart.utcDateTime.toString("yyyy-MM-dd HH:mm:ss")));
    summaryTable_->setItem(r, 0, makeCell("Latitude"));
    summaryTable_->setItem(r++, 1, makeCell(QString::number(input.latitude, 'f', 6)));
    summaryTable_->setItem(r, 0, makeCell("Longitude"));
    summaryTable_->setItem(r++, 1, makeCell(QString::number(input.longitude, 'f', 6)));
    summaryTable_->setItem(r, 0, makeCell("Timezone"));
    summaryTable_->setItem(r++, 1, makeCell(chart.timezoneLabel));
    summaryTable_->setItem(r, 0, makeCell("House system"));
    summaryTable_->setItem(r++, 1, makeCell(input.houseSystem == HouseSystem::Placidus ? "Placidus" : "Whole Sign"));
    summaryTable_->setItem(r, 0, makeCell("Mode"));
    summaryTable_->setItem(r++, 1, makeCell("Tropical"));
    summaryTable_->setItem(r, 0, makeCell("Day/Night"));
    summaryTable_->setItem(r++, 1, makeCell(chart.isDayChart ? "Day" : "Night"));
}

void MainWindow::populateAngles(const NatalChart& chart) {
    QStringList headers = {"Angle", "Deg in Sign", "Sign"};
    const auto rows = collectAngleAndLotRows(chart);
    setupTable(anglesTable_, headers, static_cast<int>(rows.size()));
    for (int i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        anglesTable_->setItem(i, 0, makeCell(row.label));
        anglesTable_->setItem(i, 1, makeCell(formatDegOnly(row.lon), Qt::AlignRight | Qt::AlignVCenter));
        anglesTable_->setItem(i, 2, makeCell(signName(signIndex(row.lon))));
    }
}

void MainWindow::populatePlanets(const NatalChart& chart) {
    QStringList headers = {"Body", "Deg in Sign", "Sign", "House", "Retro", "Element", "Mode", "Dignity"};
    setupTable(planetsTable_, headers, chart.bodies.size());

    QMap<QString, BodyPosition> map;
    for (const auto& body : chart.bodies) {
        map.insert(body.name, body);
    }

    QStringList order = tropicalBodyOrder();
    int row = 0;
    for (const auto& name : order) {
        if (!map.contains(name)) {
            continue;
        }
        const auto& body = map[name];
        planetsTable_->setItem(row, 0, makeCell(body.name));
        planetsTable_->setItem(row, 1, makeCell(formatDegOnly(body.longitude), Qt::AlignRight | Qt::AlignVCenter));
        planetsTable_->setItem(row, 2, makeCell(body.signName));
        planetsTable_->setItem(row, 3, makeCell(QString::number(body.house), Qt::AlignCenter));
        planetsTable_->setItem(row, 4, makeCell(body.retrograde ? "R" : "D", Qt::AlignCenter));
        planetsTable_->setItem(row, 5, makeCell(body.element));
        planetsTable_->setItem(row, 6, makeCell(body.mode));
        planetsTable_->setItem(row, 7, makeCell(body.dignity));
        row++;
    }
    planetsTable_->setRowCount(row);
}

void MainWindow::populateFixedStars(const NatalChart& chart) {
    if (!fixedStarsTable_) {
        return;
    }
    if (chart.fixedStars.isEmpty()) {
        setupTable(fixedStarsTable_, {"Info"}, 1);
        fixedStarsTable_->setItem(0, 0, makeCell("No fixed star data available for this chart."));
        return;
    }

    QVector<FixedStarPosition> rows = chart.fixedStars;
    std::sort(rows.begin(), rows.end(), [](const FixedStarPosition& a, const FixedStarPosition& b) {
        return a.longitude < b.longitude;
    });

    setupTable(fixedStarsTable_, {"Star", "Deg in Sign", "Sign", "House", "Visible"}, rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& star = rows[i];
        fixedStarsTable_->setItem(i, 0, makeCell(star.name));
        fixedStarsTable_->setItem(i, 1, makeCell(formatDegOnly(star.longitude), Qt::AlignRight | Qt::AlignVCenter));
        fixedStarsTable_->setItem(i, 2, makeCell(star.signName.isEmpty() ? signName(signIndex(star.longitude)) : star.signName));
        fixedStarsTable_->setItem(i, 3, makeCell(star.house > 0 ? QString::number(star.house) : "-", Qt::AlignCenter));
        auto* visibleItem = makeCell(isFixedStarVisible(star.name) ? "Yes" : "No", Qt::AlignCenter);
        if (!isFixedStarVisible(star.name)) {
            visibleItem->setForeground(QColor("#8a8a8a"));
        }
        fixedStarsTable_->setItem(i, 4, visibleItem);
    }
}

void MainWindow::populateHouses(const NatalChart& chart, HouseSystem system) {
    if (system == HouseSystem::Placidus && !chart.cusps.isEmpty()) {
        setupTable(housesTable_, {"House", "Cusp Deg", "Sign"}, chart.cusps.size());
        for (int i = 0; i < chart.cusps.size(); ++i) {
            const auto& cusp = chart.cusps[i];
            housesTable_->setItem(i, 0, makeCell(QString::number(cusp.number), Qt::AlignCenter));
            housesTable_->setItem(i, 1, makeCell(formatDegOnly(cusp.longitude), Qt::AlignRight | Qt::AlignVCenter));
            housesTable_->setItem(i, 2, makeCell(cusp.signName));
        }
        return;
    }

    int ascIdx = signIndex(chart.angles.asc);
    setupTable(housesTable_, {"House", "Sign"}, 12);
    for (int i = 0; i < 12; ++i) {
        int signIdx = (ascIdx + i) % 12;
        housesTable_->setItem(i, 0, makeCell(QString::number(i + 1), Qt::AlignCenter));
        housesTable_->setItem(i, 1, makeCell(signName(signIdx)));
    }
}

void MainWindow::populateAspects(const NatalChart& chart) {
    const auto& grid = chart.aspects;
    QVector<int> indices;
    indices.reserve(grid.bodyOrder.size());
    for (int i = 0; i < grid.bodyOrder.size(); ++i) {
        if (isAsteroidBody(grid.bodyOrder[i]) && (!includeAsteroidAspects_ || !isAsteroidVisible(grid.bodyOrder[i]))) {
            continue;
        }
        indices.push_back(i);
    }
    const int n = indices.size();
    if (n <= 0) {
        setupTable(aspectsTable_, {}, 0);
        aspectTriangleEnabled_ = false;
        return;
    }
    aspectsTable_->clear();
    aspectsTable_->clearSpans();
    aspectsTable_->setRowCount(n);
    aspectsTable_->setColumnCount(n);
    aspectsTable_->verticalHeader()->setVisible(true);
    aspectsTable_->horizontalHeader()->setVisible(false);
    QStringList headers;
    headers.reserve(n);
    for (int i = 0; i < n; ++i) {
        const int src = indices[i];
        headers.push_back(src >= 0 && src < grid.bodyOrder.size()
            ? aspectHeaderLabel(grid.bodyOrder[src])
            : "?");
    }
    aspectsTable_->setHorizontalHeaderLabels(headers);
    aspectsTable_->setVerticalHeaderLabels(headers);
    aspectsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    aspectsTable_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    aspectsTable_->setShowGrid(true);
    aspectsTable_->setAlternatingRowColors(false);
    aspectsTable_->setSelectionMode(QAbstractItemView::NoSelection);
    aspectsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    aspectsTable_->verticalHeader()->setDefaultSectionSize(18);
    if (aspectHeaderMode_ == AspectHeaderMode::Glyphs) {
        QFont glyphFont = aspectsTable_->font();
        glyphFont.setFamily("Segoe UI Symbol");
        glyphFont.setPointSize(9);
        aspectsTable_->setFont(glyphFont);
        aspectsTable_->horizontalHeader()->setFont(glyphFont);
        aspectsTable_->verticalHeader()->setFont(glyphFont);
    } else {
        QFont baseFont = aspectsTable_->font();
        aspectsTable_->setFont(baseFont);
        aspectsTable_->horizontalHeader()->setFont(baseFont);
        aspectsTable_->verticalHeader()->setFont(baseFont);
    }
    for (int i = 0; i < n; ++i) {
        const int src = indices[i];
        if (auto* item = aspectsTable_->horizontalHeaderItem(i)) {
            if (src >= 0 && src < grid.bodyOrder.size()) {
                item->setToolTip(grid.bodyOrder[src]);
            }
        }
        if (auto* item = aspectsTable_->verticalHeaderItem(i)) {
            if (src >= 0 && src < grid.bodyOrder.size()) {
                item->setToolTip(grid.bodyOrder[src]);
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const int srcI = indices[i];
            const int srcJ = indices[j];
            if (srcI < 0 || srcJ < 0
                || srcI >= grid.cells.size() || srcJ >= grid.cells[srcI].size()) {
                aspectsTable_->setItem(i, j, makeCell(""));
                continue;
            }
            const auto& cell = grid.cells[srcI][srcJ];
            if (cell.hasAspect && !cell.symbol.isEmpty()) {
                if (aspectDisplayMaxOrb_ > 0.0 && cell.orb > aspectDisplayMaxOrb_) {
                    aspectsTable_->setItem(i, j, makeCell(""));
                    continue;
                }
                const QString text = QString("%1 %2")
                    .arg(cell.symbol)
                    .arg(QString::number(cell.orb, 'f', 1));
                auto* item = makeCell(text, Qt::AlignCenter);
                if (srcI < grid.bodyOrder.size() && srcJ < grid.bodyOrder.size()) {
                    const QString tooltip = QString("%1 vs %2: %3 (orb %4 deg)")
                        .arg(grid.bodyOrder[srcI])
                        .arg(grid.bodyOrder[srcJ])
                        .arg(cell.label)
                        .arg(QString::number(cell.orb, 'f', 2));
                    item->setToolTip(tooltip);
                }
                if (cell.label == "Square" || cell.label == "Opposition") {
                    item->setForeground(QColor("#e05555"));
                } else if (cell.label == "Trine" || cell.label == "Sextile") {
                    item->setForeground(QColor("#4aa3ff"));
                }
                aspectsTable_->setItem(i, j, item);
            } else {
                aspectsTable_->setItem(i, j, makeCell(""));
            }
        }
    }
    for (int i = 0; i < n; ++i) {
        const int src = indices[i];
        if (src < 0 || src >= grid.bodyOrder.size()) {
            continue;
        }
        auto* diag = aspectsTable_->item(i, i);
        if (!diag) {
            diag = makeCell("", Qt::AlignCenter);
            aspectsTable_->setItem(i, i, diag);
        }
        diag->setText(aspectHeaderLabel(grid.bodyOrder[src]));
        diag->setTextAlignment(Qt::AlignCenter);
        diag->setToolTip(grid.bodyOrder[src]);
        QFont diagFont = diag->font();
        diagFont.setBold(true);
        diag->setFont(diagFont);
        diag->setForeground(QBrush());
        diag->setFlags(Qt::ItemIsEnabled);
    }

    aspectTriangleEnabled_ = true;
    applyAspectTriangle(n);
    clearAspectHover();
}

void MainWindow::populateTransitAspectsOverlay(const NatalChart& transitChart, const NatalChart& natalChart) {
    if (!aspectsTable_) {
        return;
    }
    aspectTriangleEnabled_ = false;
    QMap<QString, double> transitMap;
    for (const auto& body : transitChart.bodies) {
        transitMap.insert(body.name, body.longitude);
    }
    QMap<QString, double> natalMap;
    for (const auto& body : natalChart.bodies) {
        natalMap.insert(body.name, body.longitude);
    }
    natalMap.insert("Ascendant", natalChart.angles.asc);
    natalMap.insert("Midheaven", natalChart.angles.mc);
    natalMap.insert("Descendant", natalChart.angles.desc);
    natalMap.insert("IC", natalChart.angles.ic);

    QStringList rowNames;
    QStringList colNames;
    for (const auto& name : tropicalBodyOrder()) {
        if (isAsteroidBody(name) && (!includeAsteroidAspects_ || !isAsteroidVisible(name))) {
            continue;
        }
        if (transitMap.contains(name)) {
            rowNames.push_back(name);
        }
        if (natalMap.contains(name)) {
            colNames.push_back(name);
        }
    }

    const int rows = rowNames.size();
    const int cols = colNames.size();
    aspectsTable_->clear();
    aspectsTable_->clearSpans();
    aspectsTable_->setRowCount(rows);
    aspectsTable_->setColumnCount(cols);
    aspectsTable_->verticalHeader()->setVisible(true);
    aspectsTable_->horizontalHeader()->setVisible(true);

    QStringList rowHeaders;
    QStringList colHeaders;
    rowHeaders.reserve(rows);
    colHeaders.reserve(cols);
    for (const auto& name : rowNames) {
        rowHeaders.push_back(aspectHeaderLabel(name));
    }
    for (const auto& name : colNames) {
        colHeaders.push_back(aspectHeaderLabel(name));
    }
    aspectsTable_->setHorizontalHeaderLabels(colHeaders);
    aspectsTable_->setVerticalHeaderLabels(rowHeaders);
    aspectsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    aspectsTable_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    aspectsTable_->setShowGrid(true);
    aspectsTable_->setAlternatingRowColors(false);
    aspectsTable_->setSelectionMode(QAbstractItemView::NoSelection);
    aspectsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    aspectsTable_->verticalHeader()->setDefaultSectionSize(18);
    if (aspectHeaderMode_ == AspectHeaderMode::Glyphs) {
        QFont glyphFont = aspectsTable_->font();
        glyphFont.setFamily("Segoe UI Symbol");
        glyphFont.setPointSize(9);
        aspectsTable_->setFont(glyphFont);
        aspectsTable_->horizontalHeader()->setFont(glyphFont);
        aspectsTable_->verticalHeader()->setFont(glyphFont);
    } else {
        QFont baseFont = aspectsTable_->font();
        aspectsTable_->setFont(baseFont);
        aspectsTable_->horizontalHeader()->setFont(baseFont);
        aspectsTable_->verticalHeader()->setFont(baseFont);
    }

    for (int r = 0; r < rows; ++r) {
        if (auto* item = aspectsTable_->verticalHeaderItem(r)) {
            item->setToolTip("Transit " + rowNames[r]);
        }
    }
    for (int c = 0; c < cols; ++c) {
        if (auto* item = aspectsTable_->horizontalHeaderItem(c)) {
            item->setToolTip("Natal " + colNames[c]);
        }
    }

    for (int r = 0; r < rows; ++r) {
        const QString& tName = rowNames[r];
        const double tLon = transitMap.value(tName);
        for (int c = 0; c < cols; ++c) {
            const QString& nName = colNames[c];
            const double nLon = natalMap.value(nName);
            const double diff = angularDiff(tLon, nLon);
            QString label;
            double orb = 0.0;
            double maxOrb = 0.0;
            if (aspectForDiff(diff, aspectOrbs_, &label, &orb, &maxOrb)) {
                if (aspectDisplayMaxOrb_ > 0.0 && orb > aspectDisplayMaxOrb_) {
                    aspectsTable_->setItem(r, c, makeCell(""));
                    continue;
                }
                const QString text = QString("%1 %2")
                    .arg(aspectSymbolForLabel(label))
                    .arg(QString::number(orb, 'f', 1));
                auto* item = makeCell(text, Qt::AlignCenter);
                const QString tooltip = QString("Transit %1 vs Natal %2: %3 (orb %4 deg)")
                    .arg(tName)
                    .arg(nName)
                    .arg(label)
                    .arg(QString::number(orb, 'f', 2));
                item->setToolTip(tooltip);
                if (label == "Square" || label == "Opposition") {
                    item->setForeground(QColor("#e05555"));
                } else if (label == "Trine" || label == "Sextile") {
                    item->setForeground(QColor("#4aa3ff"));
                }
                aspectsTable_->setItem(r, c, item);
            } else {
                aspectsTable_->setItem(r, c, makeCell(""));
            }
        }
    }
    clearAspectHover();
}

void MainWindow::populateProgressedAspectsOverlay(const NatalChart& progressedChart, const NatalChart& natalChart) {
    if (!aspectsTable_) {
        return;
    }
    aspectTriangleEnabled_ = false;
    QMap<QString, double> progressedMap;
    for (const auto& body : progressedChart.bodies) {
        progressedMap.insert(body.name, body.longitude);
    }
    QMap<QString, double> natalMap;
    for (const auto& body : natalChart.bodies) {
        natalMap.insert(body.name, body.longitude);
    }
    natalMap.insert("Ascendant", natalChart.angles.asc);
    natalMap.insert("Midheaven", natalChart.angles.mc);
    natalMap.insert("Descendant", natalChart.angles.desc);
    natalMap.insert("IC", natalChart.angles.ic);

    QStringList rowNames;
    QStringList colNames;
    for (const auto& name : tropicalBodyOrder()) {
        if (isAsteroidBody(name) && (!includeAsteroidAspects_ || !isAsteroidVisible(name))) {
            continue;
        }
        if (progressedMap.contains(name)) {
            rowNames.push_back(name);
        }
        if (natalMap.contains(name)) {
            colNames.push_back(name);
        }
    }

    const int rows = rowNames.size();
    const int cols = colNames.size();
    aspectsTable_->clear();
    aspectsTable_->clearSpans();
    aspectsTable_->setRowCount(rows);
    aspectsTable_->setColumnCount(cols);
    aspectsTable_->verticalHeader()->setVisible(true);
    aspectsTable_->horizontalHeader()->setVisible(true);

    QStringList rowHeaders;
    QStringList colHeaders;
    rowHeaders.reserve(rows);
    colHeaders.reserve(cols);
    for (const auto& name : rowNames) {
        rowHeaders.push_back(aspectHeaderLabel(name));
    }
    for (const auto& name : colNames) {
        colHeaders.push_back(aspectHeaderLabel(name));
    }
    aspectsTable_->setHorizontalHeaderLabels(colHeaders);
    aspectsTable_->setVerticalHeaderLabels(rowHeaders);
    aspectsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    aspectsTable_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    aspectsTable_->setShowGrid(true);
    aspectsTable_->setAlternatingRowColors(false);
    aspectsTable_->setSelectionMode(QAbstractItemView::NoSelection);
    aspectsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    aspectsTable_->verticalHeader()->setDefaultSectionSize(18);
    if (aspectHeaderMode_ == AspectHeaderMode::Glyphs) {
        QFont glyphFont = aspectsTable_->font();
        glyphFont.setFamily("Segoe UI Symbol");
        glyphFont.setPointSize(9);
        aspectsTable_->setFont(glyphFont);
        aspectsTable_->horizontalHeader()->setFont(glyphFont);
        aspectsTable_->verticalHeader()->setFont(glyphFont);
    } else {
        QFont baseFont = aspectsTable_->font();
        aspectsTable_->setFont(baseFont);
        aspectsTable_->horizontalHeader()->setFont(baseFont);
        aspectsTable_->verticalHeader()->setFont(baseFont);
    }

    for (int r = 0; r < rows; ++r) {
        if (auto* item = aspectsTable_->verticalHeaderItem(r)) {
            item->setToolTip("Progressed " + rowNames[r]);
        }
    }
    for (int c = 0; c < cols; ++c) {
        if (auto* item = aspectsTable_->horizontalHeaderItem(c)) {
            item->setToolTip("Natal " + colNames[c]);
        }
    }

    for (int r = 0; r < rows; ++r) {
        const QString& pName = rowNames[r];
        const double pLon = progressedMap.value(pName);
        for (int c = 0; c < cols; ++c) {
            const QString& nName = colNames[c];
            const double nLon = natalMap.value(nName);
            const double diff = angularDiff(pLon, nLon);
            QString label;
            double orb = 0.0;
            double maxOrb = 0.0;
            if (aspectForDiff(diff, aspectOrbs_, &label, &orb, &maxOrb)) {
                if (aspectDisplayMaxOrb_ > 0.0 && orb > aspectDisplayMaxOrb_) {
                    aspectsTable_->setItem(r, c, makeCell(""));
                    continue;
                }
                const QString text = QString("%1 %2")
                    .arg(aspectSymbolForLabel(label))
                    .arg(QString::number(orb, 'f', 1));
                auto* item = makeCell(text, Qt::AlignCenter);
                const QString tooltip = QString("Progressed %1 vs Natal %2: %3 (orb %4 deg)")
                    .arg(pName)
                    .arg(nName)
                    .arg(label)
                    .arg(QString::number(orb, 'f', 2));
                item->setToolTip(tooltip);
                if (label == "Square" || label == "Opposition") {
                    item->setForeground(QColor("#e05555"));
                } else if (label == "Trine" || label == "Sextile") {
                    item->setForeground(QColor("#4aa3ff"));
                }
                aspectsTable_->setItem(r, c, item);
            } else {
                aspectsTable_->setItem(r, c, makeCell(""));
            }
        }
    }
    clearAspectHover();
}

void MainWindow::populateSolarNatalAspectsOverlay(const NatalChart& solarChart, const NatalChart& natalChart) {
    if (!aspectsTable_) {
        return;
    }
    aspectTriangleEnabled_ = false;
    QMap<QString, double> solarMap;
    for (const auto& body : solarChart.bodies) {
        solarMap.insert(body.name, body.longitude);
    }
    QMap<QString, double> natalMap;
    for (const auto& body : natalChart.bodies) {
        natalMap.insert(body.name, body.longitude);
    }

    QStringList rowNames;
    QStringList colNames;
    for (const auto& name : tropicalBodyOrder()) {
        if (isAsteroidBody(name) && (!includeAsteroidAspects_ || !isAsteroidVisible(name))) {
            continue;
        }
        if (solarMap.contains(name)) {
            rowNames.push_back(name);
        }
        if (natalMap.contains(name)) {
            colNames.push_back(name);
        }
    }

    const int rows = rowNames.size();
    const int cols = colNames.size();
    aspectsTable_->clear();
    aspectsTable_->clearSpans();
    aspectsTable_->setRowCount(rows);
    aspectsTable_->setColumnCount(cols);
    aspectsTable_->verticalHeader()->setVisible(true);
    aspectsTable_->horizontalHeader()->setVisible(true);

    QStringList rowHeaders;
    QStringList colHeaders;
    rowHeaders.reserve(rows);
    colHeaders.reserve(cols);
    for (const auto& name : rowNames) {
        rowHeaders.push_back(QString("Solar %1").arg(aspectHeaderLabel(name)));
    }
    for (const auto& name : colNames) {
        colHeaders.push_back(QString("Natal %1").arg(aspectHeaderLabel(name)));
    }
    aspectsTable_->setHorizontalHeaderLabels(colHeaders);
    aspectsTable_->setVerticalHeaderLabels(rowHeaders);
    aspectsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    aspectsTable_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    aspectsTable_->setShowGrid(true);
    aspectsTable_->setAlternatingRowColors(false);
    aspectsTable_->setSelectionMode(QAbstractItemView::NoSelection);
    aspectsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    aspectsTable_->verticalHeader()->setDefaultSectionSize(18);
    if (aspectHeaderMode_ == AspectHeaderMode::Glyphs) {
        QFont glyphFont = aspectsTable_->font();
        glyphFont.setFamily("Segoe UI Symbol");
        glyphFont.setPointSize(9);
        aspectsTable_->setFont(glyphFont);
        aspectsTable_->horizontalHeader()->setFont(glyphFont);
        aspectsTable_->verticalHeader()->setFont(glyphFont);
    } else {
        QFont baseFont = aspectsTable_->font();
        aspectsTable_->setFont(baseFont);
        aspectsTable_->horizontalHeader()->setFont(baseFont);
        aspectsTable_->verticalHeader()->setFont(baseFont);
    }

    for (int r = 0; r < rows; ++r) {
        if (auto* item = aspectsTable_->verticalHeaderItem(r)) {
            item->setToolTip("Solar " + rowNames[r]);
        }
    }
    for (int c = 0; c < cols; ++c) {
        if (auto* item = aspectsTable_->horizontalHeaderItem(c)) {
            item->setToolTip("Natal " + colNames[c]);
        }
    }

    for (int r = 0; r < rows; ++r) {
        const QString& sName = rowNames[r];
        const double sLon = solarMap.value(sName);
        for (int c = 0; c < cols; ++c) {
            const QString& nName = colNames[c];
            const double nLon = natalMap.value(nName);
            const double diff = angularDiff(sLon, nLon);
            QString label;
            double orb = 0.0;
            double maxOrb = 0.0;
            if (aspectForDiff(diff, aspectOrbs_, &label, &orb, &maxOrb)) {
                if (aspectDisplayMaxOrb_ > 0.0 && orb > aspectDisplayMaxOrb_) {
                    aspectsTable_->setItem(r, c, makeCell(""));
                    continue;
                }
                const QString text = QString("%1 %2")
                    .arg(aspectSymbolForLabel(label))
                    .arg(QString::number(orb, 'f', 1));
                auto* item = makeCell(text, Qt::AlignCenter);
                const QString tooltip = QString("Solar %1 vs Natal %2: %3 (orb %4 deg)")
                    .arg(sName)
                    .arg(nName)
                    .arg(label)
                    .arg(QString::number(orb, 'f', 2));
                item->setToolTip(tooltip);
                if (label == "Square" || label == "Opposition") {
                    item->setForeground(QColor("#e05555"));
                } else if (label == "Trine" || label == "Sextile") {
                    item->setForeground(QColor("#4aa3ff"));
                }
                aspectsTable_->setItem(r, c, item);
            } else {
                aspectsTable_->setItem(r, c, makeCell(""));
            }
        }
    }
    clearAspectHover();
}

void MainWindow::populateRelocationNatalAspectsOverlay(const NatalChart& relocationChart, const NatalChart& natalChart) {
    if (!aspectsTable_) {
        return;
    }
    aspectTriangleEnabled_ = false;
    QMap<QString, double> relocationMap;
    for (const auto& body : relocationChart.bodies) {
        relocationMap.insert(body.name, body.longitude);
    }
    relocationMap.insert("Ascendant", relocationChart.angles.asc);
    relocationMap.insert("Midheaven", relocationChart.angles.mc);
    relocationMap.insert("Descendant", relocationChart.angles.desc);
    relocationMap.insert("IC", relocationChart.angles.ic);

    QMap<QString, double> natalMap;
    for (const auto& body : natalChart.bodies) {
        natalMap.insert(body.name, body.longitude);
    }
    natalMap.insert("Ascendant", natalChart.angles.asc);
    natalMap.insert("Midheaven", natalChart.angles.mc);
    natalMap.insert("Descendant", natalChart.angles.desc);
    natalMap.insert("IC", natalChart.angles.ic);

    QStringList rowNames;
    QStringList colNames;
    for (const auto& name : tropicalBodyOrder()) {
        if (isAsteroidBody(name) && (!includeAsteroidAspects_ || !isAsteroidVisible(name))) {
            continue;
        }
        if (relocationMap.contains(name)) {
            rowNames.push_back(name);
        }
        if (natalMap.contains(name)) {
            colNames.push_back(name);
        }
    }

    const int rows = rowNames.size();
    const int cols = colNames.size();
    aspectsTable_->clear();
    aspectsTable_->clearSpans();
    aspectsTable_->setRowCount(rows);
    aspectsTable_->setColumnCount(cols);
    aspectsTable_->verticalHeader()->setVisible(true);
    aspectsTable_->horizontalHeader()->setVisible(true);

    QStringList rowHeaders;
    QStringList colHeaders;
    rowHeaders.reserve(rows);
    colHeaders.reserve(cols);
    for (const auto& name : rowNames) {
        rowHeaders.push_back(QString("Relocation %1").arg(aspectHeaderLabel(name)));
    }
    for (const auto& name : colNames) {
        colHeaders.push_back(QString("Natal %1").arg(aspectHeaderLabel(name)));
    }
    aspectsTable_->setHorizontalHeaderLabels(colHeaders);
    aspectsTable_->setVerticalHeaderLabels(rowHeaders);
    aspectsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    aspectsTable_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    aspectsTable_->setShowGrid(true);
    aspectsTable_->setAlternatingRowColors(false);
    aspectsTable_->setSelectionMode(QAbstractItemView::NoSelection);
    aspectsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    aspectsTable_->verticalHeader()->setDefaultSectionSize(18);
    if (aspectHeaderMode_ == AspectHeaderMode::Glyphs) {
        QFont glyphFont = aspectsTable_->font();
        glyphFont.setFamily("Segoe UI Symbol");
        glyphFont.setPointSize(9);
        aspectsTable_->setFont(glyphFont);
        aspectsTable_->horizontalHeader()->setFont(glyphFont);
        aspectsTable_->verticalHeader()->setFont(glyphFont);
    } else {
        QFont baseFont = aspectsTable_->font();
        aspectsTable_->setFont(baseFont);
        aspectsTable_->horizontalHeader()->setFont(baseFont);
        aspectsTable_->verticalHeader()->setFont(baseFont);
    }

    for (int r = 0; r < rows; ++r) {
        if (auto* item = aspectsTable_->verticalHeaderItem(r)) {
            item->setToolTip("Relocation " + rowNames[r]);
        }
    }
    for (int c = 0; c < cols; ++c) {
        if (auto* item = aspectsTable_->horizontalHeaderItem(c)) {
            item->setToolTip("Natal " + colNames[c]);
        }
    }

    for (int r = 0; r < rows; ++r) {
        const QString& sName = rowNames[r];
        const double sLon = relocationMap.value(sName);
        for (int c = 0; c < cols; ++c) {
            const QString& nName = colNames[c];
            const double nLon = natalMap.value(nName);
            const double diff = angularDiff(sLon, nLon);
            QString label;
            double orb = 0.0;
            double maxOrb = 0.0;
            if (aspectForDiff(diff, aspectOrbs_, &label, &orb, &maxOrb)) {
                if (aspectDisplayMaxOrb_ > 0.0 && orb > aspectDisplayMaxOrb_) {
                    aspectsTable_->setItem(r, c, makeCell(""));
                    continue;
                }
                const QString text = QString("%1 %2")
                    .arg(aspectSymbolForLabel(label))
                    .arg(QString::number(orb, 'f', 1));
                auto* item = makeCell(text, Qt::AlignCenter);
                const QString tooltip = QString("Relocation %1 vs Natal %2: %3 (orb %4 deg)")
                    .arg(sName)
                    .arg(nName)
                    .arg(label)
                    .arg(QString::number(orb, 'f', 2));
                item->setToolTip(tooltip);
                if (label == "Square" || label == "Opposition") {
                    item->setForeground(QColor("#e05555"));
                } else if (label == "Trine" || label == "Sextile") {
                    item->setForeground(QColor("#4aa3ff"));
                }
                aspectsTable_->setItem(r, c, item);
            } else {
                aspectsTable_->setItem(r, c, makeCell(""));
            }
        }
    }
    clearAspectHover();
}

void MainWindow::populateTransitList(const NatalChart& transitChart, bool overlayMode) {
    if (!rightTopTable_) {
        return;
    }
    const QStringList headers = overlayMode
        ? QStringList{"Transit", "Deg", "Sign", "Natal House"}
        : QStringList{"Transit", "Deg", "Sign", "House"};
    setupTable(rightTopTable_, headers, transitChart.bodies.size());
    QMap<QString, BodyPosition> map;
    for (const auto& body : transitChart.bodies) {
        map.insert(body.name, body);
    }

    int row = 0;
    for (const auto& name : tropicalBodyOrder()) {
        if (!map.contains(name)) {
            continue;
        }
        const auto& body = map[name];
        rightTopTable_->setItem(row, 0, makeCell(body.name));
        rightTopTable_->setItem(row, 1, makeCell(formatDegOnly(body.longitude), Qt::AlignRight | Qt::AlignVCenter));
        rightTopTable_->setItem(row, 2, makeCell(body.signName));
        const int house = overlayMode
            ? calcHouseForLongitude(body.longitude, natalPlacidusCusps_, currentChart_.angles.asc, transitHouseSystem_)
            : body.house;
        rightTopTable_->setItem(row, 3, makeCell(QString::number(house), Qt::AlignCenter));
        row++;
    }
    rightTopTable_->setRowCount(row);
}

void MainWindow::populateCurrentTransits(const NatalChart& transitChart, const NatalChart& natalChart) {
    if (!rightTopTable_) {
        return;
    }
    struct TransitHit {
        QString tName;
        QString nName;
        QString label;
        double orb;
    };
    QVector<TransitHit> hits;
    QMap<QString, double> natalMap;
    for (const auto& body : natalChart.bodies) {
        natalMap.insert(body.name, body.longitude);
    }
    natalMap.insert("Ascendant", natalChart.angles.asc);
    natalMap.insert("Midheaven", natalChart.angles.mc);
    natalMap.insert("Descendant", natalChart.angles.desc);
    natalMap.insert("IC", natalChart.angles.ic);

    for (const auto& tBody : transitChart.bodies) {
        for (auto it = natalMap.constBegin(); it != natalMap.constEnd(); ++it) {
            const double diff = angularDiff(tBody.longitude, it.value());
            QString label;
            double orb = 0.0;
            double maxOrb = 0.0;
            if (!aspectForDiff(diff, aspectOrbs_, &label, &orb, &maxOrb)) {
                continue;
            }
            if (orb > 2.0) {
                continue;
            }
            hits.push_back({tBody.name, it.key(), label, orb});
        }
    }

    std::sort(hits.begin(), hits.end(), [](const TransitHit& a, const TransitHit& b) {
        return a.orb < b.orb;
    });

    setupTable(rightTopTable_, {"Transit", "Aspect", "Natal", "Orb"}, hits.size());
    for (int i = 0; i < hits.size(); ++i) {
        const auto& hit = hits[i];
        rightTopTable_->setItem(i, 0, makeCell(hit.tName));
        auto* aspectCell = makeCell(hit.label, Qt::AlignCenter);
        if (hit.label == "Square" || hit.label == "Opposition") {
            aspectCell->setForeground(QColor("#e05555"));
        } else if (hit.label == "Trine" || hit.label == "Sextile") {
            aspectCell->setForeground(QColor("#4aa3ff"));
        }
        rightTopTable_->setItem(i, 1, aspectCell);
        rightTopTable_->setItem(i, 2, makeCell(hit.nName));
        rightTopTable_->setItem(i, 3, makeCell(QString::number(hit.orb, 'f', 2), Qt::AlignRight | Qt::AlignVCenter));
    }
}

void MainWindow::populateIngressCountdown(const NatalChart& transitChart, const NatalInput& transitInput) {
    if (!rightBottomTable_) {
        return;
    }
    struct IngressRow {
        QString body;
        QString nextSign;
        QString countdown;
        QString timeLabel;
    };
    QVector<IngressRow> rows;
    const QStringList bodies = {"Moon", "Mercury", "Venus", "Mars"};

    QMap<QString, double> bodyNow;
    for (const auto& body : transitChart.bodies) {
        bodyNow.insert(body.name, body.longitude);
    }

    QTimeZone tz;
    QString tzLabel;
    QString err;
    if (!parseTimezoneInput(transitInput.timezone, &tz, &tzLabel, &err)) {
        tz = QTimeZone::utc();
        tzLabel = "UTC";
    }

    const QDateTime startLocal = transitChart.localDateTime.isValid()
        ? transitChart.localDateTime
        : QDateTime(transitInput.date, transitInput.time, tz);
    const QDateTime startUtc = startLocal.toUTC();
    const QString cacheKey = QString("%1|%2|%3|%4|%5")
        .arg(startUtc.toString(Qt::ISODate))
        .arg(tzLabel)
        .arg(QString::number(transitInput.latitude, 'f', 6))
        .arg(QString::number(transitInput.longitude, 'f', 6))
        .arg(ephePath_);
    static QString s_lastCacheKey;
    static QVector<IngressRow> s_cachedRows;

    if (s_lastCacheKey == cacheKey && !s_cachedRows.isEmpty()) {
        rows = s_cachedRows;
    } else {
        auto bodyLongitudeAtUtc = [this](const QString& bodyName, const QDateTime& utc, double* outLon) -> bool {
            if (!outLon) {
                return false;
            }
            const int bodyId = bodyIdForName(bodyName);
            if (bodyId < 0) {
                return false;
            }
            const QDateTime t = utc.toUTC();
            if (!t.isValid()) {
                return false;
            }
            const double hourDec = t.time().hour()
                + t.time().minute() / 60.0
                + t.time().second() / 3600.0
                + t.time().msec() / 3600000.0;
            const double jd = swe_.julianDay(t.date().year(), t.date().month(), t.date().day(), hourDec, SE_GREG_CAL);
            QString calcErr;
            double lon = 0.0;
            if (!swe_.calcUt(jd, bodyId, 0, &lon, &calcErr)) {
                return false;
            }
            *outLon = normalizeDegrees(lon);
            return true;
        };

        for (const auto& bodyName : bodies) {
            if (!bodyNow.contains(bodyName)) {
                rows.push_back({bodyName, "-", "-", "-"});
                continue;
            }
            const int currentSign = signIndex(bodyNow.value(bodyName));
            int stepMinutes = 360;
            int maxDays = 60;
            if (bodyName == "Moon") {
                stepMinutes = 60;
                maxDays = 7;
            } else if (bodyName == "Mars") {
                stepMinutes = 720;
                maxDays = 120;
            }

            QDateTime prevUtc = startUtc;
            QDateTime nextUtc = startUtc;
            bool found = false;
            const int maxSteps = std::max(1, (maxDays * 24 * 60) / stepMinutes);
            for (int i = 0; i < maxSteps; ++i) {
                nextUtc = nextUtc.addSecs(stepMinutes * 60);
                double lon = 0.0;
                if (!bodyLongitudeAtUtc(bodyName, nextUtc, &lon)) {
                    break;
                }
                if (signIndex(lon) != currentSign) {
                    found = true;
                    break;
                }
                prevUtc = nextUtc;
            }

            if (!found) {
                rows.push_back({bodyName, "-", "-", "-"});
                continue;
            }

            QDateTime loUtc = prevUtc;
            QDateTime hiUtc = nextUtc;
            for (int i = 0; i < 14; ++i) {
                const qint64 span = loUtc.secsTo(hiUtc);
                if (span <= 1) {
                    break;
                }
                const QDateTime midUtc = loUtc.addSecs(span / 2);
                double lon = 0.0;
                if (!bodyLongitudeAtUtc(bodyName, midUtc, &lon)) {
                    break;
                }
                if (signIndex(lon) == currentSign) {
                    loUtc = midUtc;
                } else {
                    hiUtc = midUtc;
                }
            }

            double ingressLon = 0.0;
            if (!bodyLongitudeAtUtc(bodyName, hiUtc, &ingressLon)) {
                rows.push_back({bodyName, "-", "-", "-"});
                continue;
            }

            const int nextSign = signIndex(ingressLon);
            const QString nextSignName = signName(nextSign);
            const qint64 seconds = startUtc.secsTo(hiUtc);
            const int days = static_cast<int>(seconds / 86400);
            const int hours = static_cast<int>((seconds % 86400) / 3600);
            const int minutes = static_cast<int>((seconds % 3600) / 60);
            QString countdown;
            if (days > 0) {
                countdown = QString("%1d %2h").arg(days).arg(hours);
            } else if (hours > 0) {
                countdown = QString("%1h %2m").arg(hours).arg(minutes);
            } else {
                countdown = QString("%1m").arg(minutes);
            }
            const QString timeLabel = hiUtc.toTimeZone(tz).toString("yyyy-MM-dd HH:mm");
            rows.push_back({bodyName, nextSignName, countdown, timeLabel});
        }

        s_lastCacheKey = cacheKey;
        s_cachedRows = rows;
    }

    setupTable(rightBottomTable_, {"Body", "Next Sign", "In", "Time"}, rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        rightBottomTable_->setItem(i, 0, makeCell(row.body));
        rightBottomTable_->setItem(i, 1, makeCell(row.nextSign));
        rightBottomTable_->setItem(i, 2, makeCell(row.countdown, Qt::AlignRight | Qt::AlignVCenter));
        rightBottomTable_->setItem(i, 3, makeCell(row.timeLabel));
    }
}

}  // namespace dracoved




