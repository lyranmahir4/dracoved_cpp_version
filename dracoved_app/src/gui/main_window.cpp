#include "main_window.h"
#include "astro_map_widget.h"
#include "aspect_orbs_dialog.h"
#include "chart_setup_dialog.h"
#include "chart_manager_dialog.h"
#include "chart_wheel_widget.h"
#include "collapsible_section.h"
#include "row_hover_delegate.h"
#include "planetary_hours_controller.h"
#include "zodiacal_releasing_controller.h"
#include "geodetic_equivalents_controller.h"
#include "preferences_dialog.h"
#include "return_calculation_service.h"
#include "return_finder_controller.h"
#include "transit_calc_service.h"
#include "transit_workers.h"

#include "../core/fixed_stars.h"
#include "../core/formatting.h"
#include <QAbstractItemView>
#include <QAbstractSpinBox>
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
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonArray>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMap>
#include <QHash>
#include <QLocale>
#include <QSet>
#include <QSignalBlocker>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <cmath>
#include <algorithm>
#include <limits>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSaveFile>
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
#include <QPair>
#include <QPainter>
#include <QPixmap>
#include <QTextEdit>
#include <QTextCursor>
#include <QSvgRenderer>
#include <functional>
#include <atomic>

#include "../core/timezone_utils.h"

namespace dracoved {

static void setupTable(QTableWidget* table, const QStringList& headers, int rows);
static void setupDetailTable(QTableWidget* table, const QStringList& headers, int rows);
static QTableWidgetItem* makeCell(const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter);
static int calcHouseForLongitude(double lon, const QVector<HouseCusp>& cusps, double asc, HouseSystem system);
static double angularDiff(double a, double b);
static QString aspectSymbolForLabel(const QString& label);
static QString aspectTargetFromLabel(const QString& text);
static bool findBodyLongitude(const NatalChart& chart, const QString& name, double* outLon);
static QString ordinalHouseLabel(int house);
static bool findAngleLongitude(const NatalChart& chart, const QString& name, double* outLon);
static QString abbrevForName(const QString& name);
static bool aspectForDiff(double diff, const AspectOrbs& orbs, QString* outLabel, double* outOrb, double* outMaxOrb);

namespace {

QIcon tintedSvgIcon(const QString& resourcePath, const QColor& requestedColor, int logicalSize = 17) {
    if (resourcePath.isEmpty() || logicalSize <= 0) {
        return {};
    }
    const QColor color = requestedColor.isValid() ? requestedColor : QColor(Qt::black);
    const QString cacheKey = QString("%1|%2|%3")
        .arg(resourcePath, color.name(QColor::HexArgb))
        .arg(logicalSize);
    static QHash<QString, QIcon> cache;
    const auto cached = cache.constFind(cacheKey);
    if (cached != cache.cend()) {
        return cached.value();
    }

    QFile svgFile(resourcePath);
    if (!svgFile.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray svgData = svgFile.readAll();
    svgData.replace("currentColor", color.name(QColor::HexRgb).toUtf8());
    QSvgRenderer renderer(svgData);
    if (!renderer.isValid()) {
        return {};
    }

    constexpr int renderScale = 2;
    QPixmap pixmap(logicalSize * renderScale, logicalSize * renderScale);
    pixmap.setDevicePixelRatio(renderScale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(1.0, 1.0, logicalSize - 2.0, logicalSize - 2.0));
    painter.end();

    const QIcon icon(pixmap);
    cache.insert(cacheKey, icon);
    return icon;
}

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
    return isLunarNodeName(name);
}

static bool isSolarTechniquePlanetName(const QString& name) {
    return name == "Sun" || name == "Moon" || name == "Mercury" || name == "Venus"
        || name == "Mars" || name == "Jupiter" || name == "Saturn" || name == "Uranus"
        || name == "Neptune" || name == "Pluto";
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

static int bodyIdForName(const QString& name, LunarNodeType genericNodeType = LunarNodeType::Mean) {
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
    if (isLunarNodeName(name)) {
        return lunarNodeTypeForName(name, genericNodeType) == LunarNodeType::True
            ? SE_TRUE_NODE
            : SE_MEAN_NODE;
    }
    if (name == "Lilith") return SE_MEAN_APOG;
    return -1;
}

static int calcFlagsForInput(const NatalInput& input) {
    return (input.zodiacSystem == ZodiacSystem::Sidereal) ? SEFLG_SIDEREAL : 0;
}

static void applyZodiacModeToSwe(SwissEph* swe, const NatalInput& input) {
    if (!swe) {
        return;
    }
    if (input.zodiacSystem == ZodiacSystem::Sidereal) {
        swe->setSidMode(siderealAyanamsaSwissMode(input.siderealAyanamsa));
    }
}

static QString zodiacModeSummary(const NatalInput& input) {
    if (input.zodiacSystem == ZodiacSystem::Sidereal) {
        return QString("Sidereal (%1)").arg(siderealAyanamsaToString(input.siderealAyanamsa));
    }
    return "Tropical";
}

static QString formatDailyMotion(double speed, bool hasSpeed) {
    if (!hasSpeed || !std::isfinite(speed)) {
        return "-";
    }
    double arcSeconds = std::abs(speed) * 3600.0;
    const int degrees = static_cast<int>(arcSeconds / 3600.0);
    arcSeconds -= degrees * 3600.0;
    const int minutes = static_cast<int>(arcSeconds / 60.0);
    const double seconds = arcSeconds - minutes * 60.0;
    return QString("%1%2 deg %3 min %4 sec / day (%5)")
        .arg(speed >= 0.0 ? "+" : "-")
        .arg(degrees)
        .arg(QString::number(minutes).rightJustified(2, '0'))
        .arg(QString::number(seconds, 'f', 2).rightJustified(5, '0'))
        .arg(speed >= 0.0 ? "direct" : "retrograde");
}

static HouseSystem lunationHouseSystemForInput(const NatalInput& input) {
    // Vedic lunation analysis uses whole-sign houses in sidereal mode.
    if (input.zodiacSystem == ZodiacSystem::Sidereal) {
        return HouseSystem::WholeSign;
    }
    return input.houseSystem;
}

static bool isComputableBody(const QString& name) {
    return bodyIdForName(name) >= 0;
}

static QStringList transitCalculableBodyOrder() {
    QStringList bodies;
    for (const auto& name : tropicalBodyOrder()) {
        if (name == "North Node" || name == "South Node") {
            continue;
        }
        if (isAngleName(name) || isDerivedPointName(name)) {
            continue;
        }
        if (!isComputableBody(name)) {
            continue;
        }
        bodies.push_back(name);
    }
    const int lilithIndex = bodies.indexOf("Lilith");
    const int insertAt = lilithIndex >= 0 ? lilithIndex : bodies.size();
    bodies.insert(insertAt, "Mean North Node");
    bodies.insert(insertAt + 1, "Mean South Node");
    bodies.insert(insertAt + 2, "True North Node");
    bodies.insert(insertAt + 3, "True South Node");
    return bodies;
}

static QStringList geodeticBodyOrder() {
    QStringList bodies;
    for (const auto& name : tropicalBodyOrder()) {
        if (name == "North Node" || name == "South Node") {
            continue;
        }
        if (isAngleName(name)) {
            continue;
        }
        if (isDerivedPointName(name) || isAsteroidBody(name)) {
            continue;
        }
        bodies.push_back(name);
    }
    const int lilithIndex = bodies.indexOf("Lilith");
    const int insertAt = lilithIndex >= 0 ? lilithIndex : bodies.size();
    bodies.insert(insertAt, "Mean North Node");
    bodies.insert(insertAt + 1, "Mean South Node");
    bodies.insert(insertAt + 2, "True North Node");
    bodies.insert(insertAt + 3, "True South Node");
    return bodies;
}

static QStringList solarPlacementFinderPlanetOrder() {
    return {
        "Sun",
        "Moon",
        "Mercury",
        "Venus",
        "Mars",
        "Jupiter",
        "Saturn",
        "Uranus",
        "Neptune",
        "Pluto",
        "North Node",
        "South Node",
        "Mean North Node",
        "Mean South Node",
        "True North Node",
        "True South Node",
        "Lilith",
    };
}

static QStringList solarPlacementFinderConjunctionTargets() {
    return {
        "None",
        "Ascendant",
        "Descendant",
        "Midheaven",
        "IC",
        "Any Angle",
    };
}

static QColor astrocartographyColorForBody(const QString& name) {
    if (name == "Sun") return QColor("#ffd200");          // yellow
    if (name == "Moon") return QColor("#1746b3");         // dark blue
    if (name == "Mercury") return QColor("#37c96b");      // light green
    if (name == "Venus") return QColor("#087a2c");        // dark green
    if (name == "Mars") return QColor("#ff0000");         // pure red
    if (name == "Jupiter") return QColor("#ff8a00");      // orange
    if (name == "Saturn") return QColor("#7a3f12");       // brown
    if (name == "Uranus") return QColor("#28a8ff");       // light blue
    if (name == "Neptune") return QColor("#006fba");      // ocean blue
    if (name == "Pluto") return QColor("#111111");        // black
    if (isNorthLunarNodeName(name)) return QColor("#8b42c6");   // violet
    if (isLunarNodeName(name)) return QColor("#5b2d90");        // dark violet
    if (name == "Lilith") return QColor("#5a5a5a");       // charcoal
    return QColor("#2c3e50");
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
    const int itemCount = std::max(0, model->rowCount() - 1);
    QString label;
    if (selected.isEmpty()) {
        label = "None";
    } else if (itemCount > 0 && selected.size() == itemCount) {
        label = QString("All (%1)").arg(itemCount);
    } else if (selected.size() <= 3) {
        label = selected.join(", ");
    } else {
        label = QString("%1 selected").arg(selected.size());
    }
    const QString tooltip = selected.isEmpty() ? QString("No items selected") : selected.join(", ");
    const QSignalBlocker blocker(combo);
    combo->setToolTip(tooltip);
    if (auto* edit = combo->lineEdit()) {
        edit->setText(label);
        edit->setToolTip(tooltip);
        edit->setCursorPosition(0);
    } else {
        combo->setCurrentText(label);
    }
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
    for (const auto& name : bodyOrderForLunarNodePolicy(chart.lunarNodePolicy)) {
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
            .arg(lunarNodeDisplayName(body.name, chart.lunarNodePolicy))
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
    setupUi();
    setupConnections();
    qApp->installEventFilter(this);
    calendarRecomputeTimer_ = new QTimer(this);
    calendarRecomputeTimer_->setSingleShot(true);
    calendarRecomputeTimer_->setInterval(250);
    connect(calendarRecomputeTimer_, &QTimer::timeout, this, [this]() {
        if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Calendar) {
            handleTransitCalendarRun();
        }
    });
    searchResultsRefreshTimer_ = new QTimer(this);
    searchResultsRefreshTimer_->setSingleShot(true);
    searchResultsRefreshTimer_->setInterval(200);
    connect(searchResultsRefreshTimer_, &QTimer::timeout, this, [this]() {
        if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Search) {
            showTransitSearchResults();
        }
    });
    astroHoverTimer_ = new QTimer(this);
    astroHoverTimer_->setSingleShot(true);
    astroHoverTimer_->setInterval(70);
    connect(astroHoverTimer_, &QTimer::timeout, this, &MainWindow::flushAstroHoverPreview);
    loadUiState();
    syncLunarNodeResearchSelectionDefaults();
    applyTheme(theme_);

    ephePath_ = findEphePath();
    if (!ephePath_.isEmpty()) {
        engine_.setEphePath(ephePath_);
        progressionEngine_.setEphePath(ephePath_);
    }
    if (returnFinderController_) {
        returnFinderController_->setRuntimePaths(ephePath_, sweSearchPaths());
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
    refreshWindowTitle();
    resize(1400, 900);

    QFont base = font();
    base.setPointSize(9);
    setFont(base);

    setupDockLayout();
    setupMenuBar();
    applyTheme(theme_);
}

void MainWindow::refreshWindowTitle() {
    if (!hasCurrentChart_) {
        setWindowTitle("DracoVed - Untitled");
        return;
    }

    const QString date = currentInput_.date.isValid()
        ? QLocale::c().toString(currentInput_.date, "MMM d yyyy")
        : QString("Unknown Date");
    const QString name = currentInput_.name.trimmed().isEmpty()
        ? QString("Untitled")
        : currentInput_.name.trimmed();
    setWindowTitle(QString("DracoVed - %1 - %2").arg(date, name));
}


void MainWindow::setupDockLayout() {
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(6, 6, 6, 6);
    centralLayout->setSpacing(4);

    mainTabBar_ = new QTabBar(central);
    auto addMainTab = [this](const QString& label, AppTab tab) {
        const int index = mainTabBar_->addTab(label);
        mainTabBar_->setTabData(index, static_cast<int>(tab));
    };
    addMainTab("Natal", AppTab::Natal);
    addMainTab("Transits", AppTab::Transits);
    addMainTab("Progression", AppTab::Progression);
    addMainTab("Zodiacal Releasing", AppTab::ZodiacalReleasing);
    addMainTab("Solar Return", AppTab::SolarReturn);
    addMainTab("Lunar Return", AppTab::LunarReturn);
    addMainTab("Return Finder", AppTab::ReturnFinder);
    addMainTab("Planetary Hours", AppTab::PlanetaryHours);
    addMainTab("Lunations", AppTab::Lunations);
    addMainTab("Relocation", AppTab::Relocation);
    addMainTab("Astrocartography", AppTab::Astrocartography);
    addMainTab("Geodetic Equivalents", AppTab::GeodeticEquivalents);
    mainTabBar_->setExpanding(false);
    mainTabBar_->setDrawBase(false);
    mainTabBar_->setMovable(false);
    mainTabBar_->setCurrentIndex(0);

    profileToolbarFrame_ = new QFrame(central);
    profileToolbarFrame_->setObjectName("profileQuickBar");
    auto* profileToolbarLayout = new QVBoxLayout(profileToolbarFrame_);
    profileToolbarLayout->setContentsMargins(8, 6, 8, 6);
    profileToolbarLayout->setSpacing(4);
    auto* profileRow = new QWidget(profileToolbarFrame_);
    auto* profileRowLayout = new QHBoxLayout(profileRow);
    profileRowLayout->setContentsMargins(0, 0, 0, 0);
    profileRowLayout->setSpacing(6);
    auto* zodiacRow = new QWidget(profileToolbarFrame_);
    auto* zodiacRowLayout = new QHBoxLayout(zodiacRow);
    zodiacRowLayout->setContentsMargins(0, 0, 0, 0);
    zodiacRowLayout->setSpacing(6);
    auto* profileLabel = new QLabel("Charts", profileToolbarFrame_);
    profileToolbarCombo_ = new QComboBox(profileToolbarFrame_);
    profileToolbarCombo_->setMinimumWidth(180);
    profileToolbarCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    profileToolbarCombo_->setToolTip("Select a chart from your saved chart library.");
    profileToolbarNewButton_ = new QToolButton(profileToolbarFrame_);
    profileToolbarNewButton_->setText("New Chart");
    profileToolbarNewButton_->setToolTip("Create a new chart from birth data (Ctrl+N).");
    profileToolbarLoadButton_ = new QToolButton(profileToolbarFrame_);
    profileToolbarLoadButton_->setText("Load Chart");
    profileToolbarLoadButton_->setToolTip("Load the selected saved chart (Ctrl+O).");
    profileToolbarManageButton_ = new QToolButton(profileToolbarFrame_);
    profileToolbarManageButton_->setText("Manage Charts");
    profileToolbarManageButton_->setToolTip("Open the Chart Manager to organize saved charts.");
    profileToolbarSaveButton_ = new QToolButton(profileToolbarFrame_);
    profileToolbarSaveButton_->setText("Save");
    profileToolbarSaveButton_->setToolTip("Save this chart's birth data and settings to your chart library (Ctrl+S).");
    profileToolbarEditButton_ = new QToolButton(profileToolbarFrame_);
    profileToolbarEditButton_->setText("Edit");
    profileToolbarEditButton_->setToolTip("Edit the current chart's birth data and recalculate it.");
    profileToolbarDeleteButton_ = new QToolButton(profileToolbarFrame_);
    profileToolbarDeleteButton_->setText("Delete");
    profileToolbarDeleteButton_->setToolTip("Delete the selected chart from your saved chart library.");
    auto* zodiacLabel = new QLabel("Zodiac", profileToolbarFrame_);
    zodiacToolbarTropicalRadio_ = new QRadioButton("Tropical", profileToolbarFrame_);
    zodiacToolbarSiderealRadio_ = new QRadioButton("Sidereal", profileToolbarFrame_);
    auto* ayanamsaLabel = new QLabel("Ayanamsa", profileToolbarFrame_);
    zodiacToolbarAyanamsaCombo_ = new QComboBox(profileToolbarFrame_);
    zodiacToolbarAyanamsaCombo_->setMinimumWidth(110);
    zodiacToolbarAyanamsaCombo_->addItem(siderealAyanamsaToString(SiderealAyanamsa::Lahiri), static_cast<int>(SiderealAyanamsa::Lahiri));
    zodiacToolbarAyanamsaCombo_->addItem(siderealAyanamsaToString(SiderealAyanamsa::Raman), static_cast<int>(SiderealAyanamsa::Raman));
    zodiacToolbarAyanamsaCombo_->addItem(siderealAyanamsaToString(SiderealAyanamsa::Krishnamurti), static_cast<int>(SiderealAyanamsa::Krishnamurti));
    zodiacToolbarAyanamsaCombo_->addItem(siderealAyanamsaToString(SiderealAyanamsa::FaganBradley), static_cast<int>(SiderealAyanamsa::FaganBradley));
    zodiacToolbarAyanamsaCombo_->addItem(siderealAyanamsaToString(SiderealAyanamsa::Yukteshwar), static_cast<int>(SiderealAyanamsa::Yukteshwar));
    zodiacToolbarAyanamsaCombo_->addItem(siderealAyanamsaToString(SiderealAyanamsa::TrueCitra), static_cast<int>(SiderealAyanamsa::TrueCitra));
    zodiacToolbarAyanamsaCombo_->addItem(siderealAyanamsaToString(SiderealAyanamsa::TrueRevati), static_cast<int>(SiderealAyanamsa::TrueRevati));
    zodiacToolbarTropicalRadio_->setChecked(true);
    zodiacToolbarAyanamsaCombo_->setCurrentIndex(0);
    zodiacToolbarAyanamsaCombo_->setEnabled(false);
    nodeSettingsButton_ = new QToolButton(profileToolbarFrame_);
    nodeSettingsButton_->setText("Nodes: Mean");
    nodeSettingsButton_->setToolTip("Current lunar-node calculation. Click to open Preferences.");
    nodeSettingsButton_->setCursor(Qt::PointingHandCursor);
    connect(nodeSettingsButton_, &QToolButton::clicked, this, &MainWindow::handlePreferences);
    profileToolbarNewButton_->setCursor(Qt::PointingHandCursor);
    profileToolbarLoadButton_->setCursor(Qt::PointingHandCursor);
    profileToolbarManageButton_->setCursor(Qt::PointingHandCursor);
    profileToolbarSaveButton_->setCursor(Qt::PointingHandCursor);
    profileToolbarEditButton_->setCursor(Qt::PointingHandCursor);
    profileToolbarDeleteButton_->setCursor(Qt::PointingHandCursor);
    profileToolbarStateLabel_ = new QLabel(profileToolbarFrame_);
    profileToolbarStateLabel_->setObjectName("profileQuickState");
    profileToolbarStateLabel_->setMinimumWidth(0);
    profileToolbarStateLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    profileRowLayout->addWidget(profileLabel);
    profileRowLayout->addWidget(profileToolbarCombo_, 1);
    profileRowLayout->addWidget(profileToolbarNewButton_);
    profileRowLayout->addWidget(profileToolbarLoadButton_);
    profileRowLayout->addWidget(profileToolbarManageButton_);
    profileRowLayout->addWidget(profileToolbarSaveButton_);
    profileRowLayout->addWidget(profileToolbarEditButton_);
    profileRowLayout->addSpacing(8);
    profileRowLayout->addWidget(profileToolbarDeleteButton_);
    profileRowLayout->addSpacing(8);
    profileRowLayout->addWidget(profileToolbarStateLabel_, 1);

    zodiacRowLayout->addWidget(zodiacLabel);
    zodiacRowLayout->addWidget(zodiacToolbarTropicalRadio_);
    zodiacRowLayout->addWidget(zodiacToolbarSiderealRadio_);
    zodiacRowLayout->addWidget(ayanamsaLabel);
    zodiacRowLayout->addWidget(zodiacToolbarAyanamsaCombo_);
    zodiacRowLayout->addSpacing(10);
    zodiacRowLayout->addWidget(nodeSettingsButton_);
    zodiacRowLayout->addStretch(1);

    profileToolbarLayout->addWidget(profileRow);
    profileToolbarLayout->addWidget(zodiacRow);

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
    transitAspectGridToggleButton_ = new QToolButton(chartHeader);
    transitAspectGridToggleButton_->setText("Grid");
    transitAspectGridToggleButton_->setCheckable(true);
    transitAspectGridToggleButton_->setChecked(true);
    transitAspectGridToggleButton_->setToolTip(
        "Show or hide the Transit aspect matrix grid");
    transitAspectGridToggleButton_->setVisible(false);
    zoomOutButton_ = new QToolButton(chartHeader);
    zoomOutButton_->setText("-");
    zoomResetButton_ = new QToolButton(chartHeader);
    zoomResetButton_->setText("0");
    zoomInButton_ = new QToolButton(chartHeader);
    zoomInButton_->setText("+");
    chartHeaderLayout->addWidget(chartTitleLabel_);
    chartHeaderLayout->addWidget(chartLegendLabel_);
    chartHeaderLayout->addStretch();
    chartHeaderLayout->addWidget(transitAspectGridToggleButton_);
    chartHeaderLayout->addWidget(zoomOutButton_);
    chartHeaderLayout->addWidget(zoomResetButton_);
    chartHeaderLayout->addWidget(zoomInButton_);
    chartHeaderLayout->addWidget(chartSettingsButton_);

    chartWheel_ = new ChartWheelWidget(chartPanel);
    chartWheel_->setMinimumWidth(520);

    chartLayout->addWidget(chartHeader);
    centerStack_ = new QStackedWidget(chartPanel);
    chartViewPanel_ = new QWidget(centerStack_);
    auto* chartViewLayout = new QGridLayout(chartViewPanel_);
    chartViewLayout->setContentsMargins(0, 0, 0, 0);
    chartWorkspaceSplitter_ = new QSplitter(Qt::Horizontal, chartViewPanel_);
    chartWorkspaceSplitter_->setChildrenCollapsible(false);
    chartWorkspaceSplitter_->setHandleWidth(5);
    chartWheelHost_ = new QWidget(chartWorkspaceSplitter_);
    auto* chartWheelLayout = new QGridLayout(chartWheelHost_);
    chartWheelLayout->setContentsMargins(0, 0, 0, 0);
    chartWheelLayout->addWidget(chartWheel_, 0, 0);
    chartWorkspaceSplitter_->addWidget(chartWheelHost_);
    chartWorkspaceSplitter_->setStretchFactor(0, 1);
    chartViewLayout->addWidget(chartWorkspaceSplitter_, 0, 0);

    aspectOrbQuickPanel_ = new QFrame(chartWheelHost_);
    aspectOrbQuickPanel_->setObjectName("aspectOrbQuickPanel");
    aspectOrbQuickPanel_->setAttribute(Qt::WA_StyledBackground, true);
    aspectOrbQuickPanel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* aspectOrbQuickLayout = new QHBoxLayout(aspectOrbQuickPanel_);
    aspectOrbQuickLayout->setContentsMargins(8, 6, 8, 6);
    aspectOrbQuickLayout->setSpacing(4);
    auto* aspectOrbQuickLabel = new QLabel("Aspect Orb", aspectOrbQuickPanel_);
    aspectOrbPreset1Button_ = new QToolButton(aspectOrbQuickPanel_);
    aspectOrbPreset2Button_ = new QToolButton(aspectOrbQuickPanel_);
    aspectOrbPreset3Button_ = new QToolButton(aspectOrbQuickPanel_);
    const QList<QPair<QToolButton*, QString>> aspectOrbPresets = {
        {aspectOrbPreset1Button_, "1°"},
        {aspectOrbPreset2Button_, "2°"},
        {aspectOrbPreset3Button_, "3°"}
    };
    for (const auto& preset : aspectOrbPresets) {
        preset.first->setText(preset.second);
        preset.first->setCheckable(true);
        preset.first->setFixedWidth(34);
        preset.first->setToolTip(
            QString("Show aspect lines with an orb of %1 or less").arg(preset.second));
    }
    aspectOrbCustomSpin_ = new QDoubleSpinBox(aspectOrbQuickPanel_);
    aspectOrbCustomSpin_->setRange(0.0, 15.0);
    aspectOrbCustomSpin_->setDecimals(1);
    aspectOrbCustomSpin_->setSingleStep(0.5);
    aspectOrbCustomSpin_->setSuffix("°");
    aspectOrbCustomSpin_->setSpecialValueText("All");
    aspectOrbCustomSpin_->setKeyboardTracking(false);
    aspectOrbCustomSpin_->setFixedWidth(72);
    aspectOrbCustomSpin_->setToolTip(
        "Custom maximum aspect-line orb. Select All to remove the display filter.");
    aspectOrbQuickLayout->addWidget(aspectOrbQuickLabel);
    aspectOrbQuickLayout->addWidget(aspectOrbPreset1Button_);
    aspectOrbQuickLayout->addWidget(aspectOrbPreset2Button_);
    aspectOrbQuickLayout->addWidget(aspectOrbPreset3Button_);
    aspectOrbQuickLayout->addWidget(aspectOrbCustomSpin_);
    chartWheelLayout->addWidget(
        aspectOrbQuickPanel_, 0, 0, Qt::AlignRight | Qt::AlignBottom);
    centerStack_->addWidget(chartViewPanel_);

    worldMapPanel_ = new QWidget(centerStack_);
    auto* worldMapLayout = new QVBoxLayout(worldMapPanel_);
    worldMapLayout->setContentsMargins(0, 0, 0, 0);
    astroMapWidget_ = new AstroMapWidget(worldMapPanel_);
    worldMapLayout->addWidget(astroMapWidget_, 1);
    centerStack_->addWidget(worldMapPanel_);
    returnFinderController_ = new ReturnFinderController(this);
    centerStack_->addWidget(returnFinderController_->workspaceWidget());
    planetaryHoursController_ = new PlanetaryHoursController(&swe_, net_, this);
    centerStack_->addWidget(planetaryHoursController_->workspaceWidget());
    zodiacalReleasingController_ = new ZodiacalReleasingController(this);
    centerStack_->addWidget(zodiacalReleasingController_->workspaceWidget());
    geodeticEquivalentsController_ =
        new GeodeticEquivalentsController(&swe_, this);
    centerStack_->addWidget(
        geodeticEquivalentsController_->workspaceWidget());
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
    aspectDelegate_ = new AspectMatrixDelegate(aspectsTable_);
    aspectsTable_->setItemDelegate(aspectDelegate_);
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
    reportOptionsButton_ = new QToolButton(reportHeader);
    reportOptionsButton_->setText("Report: Basic");
    reportOptionsButton_->setPopupMode(QToolButton::InstantPopup);
    reportOptionsButton_->setToolTip("Choose a report preset or customize exported sections.");
    reportOptionsButton_->setVisible(false);
    auto* reportOptionsMenu = new QMenu(reportOptionsButton_);
    reportOptionsButton_->setMenu(reportOptionsMenu);

    solarReportBasicPresetAction_ = reportOptionsMenu->addAction("Use Basic Report");
    solarReportBasicPresetAction_->setToolTip(
        "Core chart data, core lots, and tight aspects without extended research data.");
    solarReportFullPresetAction_ = reportOptionsMenu->addAction("Use Full Report");
    solarReportFullPresetAction_->setToolTip(
        "All calculated bodies, lots, sections, fixed stars, and configured aspect orbs.");
    reportOptionsMenu->addSeparator();

    auto addReportToggle = [reportOptionsMenu](const QString& text) {
        auto* action = reportOptionsMenu->addAction(text);
        action->setCheckable(true);
        return action;
    };
    solarReportAnnualProfectionAction_ = addReportToggle("Annual profection");
    solarReportNatalPositionsAction_ = addReportToggle("Natal positions");
    solarReportSolarPositionsAction_ = addReportToggle("Solar Return positions");
    solarReportHouseCuspsAction_ = addReportToggle("House cusp tables");
    solarReportHouseOverlaysAction_ = addReportToggle("Cross-chart house overlays");
    solarReportSolarNatalAspectsAction_ = addReportToggle("Solar Return-Natal aspects");
    solarReportSolarSolarAspectsAction_ = addReportToggle("Solar Return-Solar Return aspects");
    solarReportNatalNatalAspectsAction_ = addReportToggle("Natal-Natal aspects");
    reportOptionsMenu->addSeparator();
    solarReportMinorBodiesAction_ = addReportToggle("Minor bodies (Chiron, asteroids, Lilith)");
    solarReportDailyMotionAction_ = addReportToggle("Daily motion");
    solarReportDignitiesAction_ = addReportToggle("Essential dignity");
    solarReportFixedStarsAction_ = addReportToggle("Fixed stars");

    auto* lotsMenu = reportOptionsMenu->addMenu("Arabic Lots");
    auto* lotsGroup = new QActionGroup(lotsMenu);
    lotsGroup->setExclusive(true);
    solarReportNoLotsAction_ = lotsMenu->addAction("None");
    solarReportCoreLotsAction_ = lotsMenu->addAction("Core: Fortune, Spirit, Eros");
    solarReportAllLotsAction_ = lotsMenu->addAction("All calculated lots");
    for (auto* action : {solarReportNoLotsAction_, solarReportCoreLotsAction_, solarReportAllLotsAction_}) {
        action->setCheckable(true);
        lotsGroup->addAction(action);
    }

    auto* aspectScopeMenu = reportOptionsMenu->addMenu("Aspect Scope");
    auto* aspectScopeGroup = new QActionGroup(aspectScopeMenu);
    aspectScopeGroup->setExclusive(true);
    solarReportTightAspectsAction_ = aspectScopeMenu->addAction("Tight - maximum 3 degrees");
    solarReportStandardAspectsAction_ = aspectScopeMenu->addAction("Standard - maximum 6 degrees");
    solarReportConfiguredAspectsAction_ = aspectScopeMenu->addAction("Use configured aspect orbs");
    for (auto* action : {solarReportTightAspectsAction_, solarReportStandardAspectsAction_,
                         solarReportConfiguredAspectsAction_}) {
        action->setCheckable(true);
        aspectScopeGroup->addAction(action);
    }

    connect(solarReportBasicPresetAction_, &QAction::triggered,
            this, &MainWindow::applySolarReportBasicPreset);
    connect(solarReportFullPresetAction_, &QAction::triggered,
            this, &MainWindow::applySolarReportFullPreset);
    auto connectReportToggle = [this](QAction* action, bool SolarReportOptions::*field) {
        connect(action, &QAction::triggered, this, [this, action, field]() {
            solarReportOptions_.*field = action->isChecked();
            markSolarReportOptionsCustom();
        });
    };
    connectReportToggle(solarReportAnnualProfectionAction_, &SolarReportOptions::includeAnnualProfection);
    connectReportToggle(solarReportNatalPositionsAction_, &SolarReportOptions::includeNatalPositions);
    connectReportToggle(solarReportSolarPositionsAction_, &SolarReportOptions::includeSolarPositions);
    connectReportToggle(solarReportHouseCuspsAction_, &SolarReportOptions::includeHouseCusps);
    connectReportToggle(solarReportHouseOverlaysAction_, &SolarReportOptions::includeHouseOverlays);
    connectReportToggle(solarReportSolarNatalAspectsAction_, &SolarReportOptions::includeSolarNatalAspects);
    connectReportToggle(solarReportSolarSolarAspectsAction_, &SolarReportOptions::includeSolarSolarAspects);
    connectReportToggle(solarReportNatalNatalAspectsAction_, &SolarReportOptions::includeNatalNatalAspects);
    connectReportToggle(solarReportMinorBodiesAction_, &SolarReportOptions::includeMinorBodies);
    connectReportToggle(solarReportDailyMotionAction_, &SolarReportOptions::includeDailyMotion);
    connectReportToggle(solarReportDignitiesAction_, &SolarReportOptions::includeDignities);
    connectReportToggle(solarReportFixedStarsAction_, &SolarReportOptions::includeFixedStars);
    connect(solarReportNoLotsAction_, &QAction::triggered, this, [this]() {
        solarReportOptions_.lotScope = SolarReportLotScope::None;
        markSolarReportOptionsCustom();
    });
    connect(solarReportCoreLotsAction_, &QAction::triggered, this, [this]() {
        solarReportOptions_.lotScope = SolarReportLotScope::Core;
        markSolarReportOptionsCustom();
    });
    connect(solarReportAllLotsAction_, &QAction::triggered, this, [this]() {
        solarReportOptions_.lotScope = SolarReportLotScope::All;
        markSolarReportOptionsCustom();
    });
    connect(solarReportTightAspectsAction_, &QAction::triggered, this, [this]() {
        solarReportOptions_.aspectScope = SolarReportAspectScope::Tight;
        markSolarReportOptionsCustom();
    });
    connect(solarReportStandardAspectsAction_, &QAction::triggered, this, [this]() {
        solarReportOptions_.aspectScope = SolarReportAspectScope::Standard;
        markSolarReportOptionsCustom();
    });
    connect(solarReportConfiguredAspectsAction_, &QAction::triggered, this, [this]() {
        solarReportOptions_.aspectScope = SolarReportAspectScope::Configured;
        markSolarReportOptionsCustom();
    });

    reportCopyButton_ = new QPushButton("Copy Report", reportHeader);
    reportHeaderLayout->addWidget(reportOptionsButton_);
    reportHeaderLayout->addStretch();
    reportHeaderLayout->addWidget(reportCopyButton_);
    reportText_ = new QTextEdit(reportPanel_);
    reportText_->setReadOnly(true);
    reportText_->setPlaceholderText("Load a natal chart to generate the report.");
    reportLayout->addWidget(reportHeader);
    reportLayout->addWidget(reportText_, 1);
    tabs_->addTab(reportPanel_, "Report");

    auto* solarTechniquePage = new QWidget();
    solarTechniquePanel_ = solarTechniquePage;
    auto* techniqueLayout = new QVBoxLayout(solarTechniquePage);
    techniqueLayout->setContentsMargins(0, 0, 0, 0);
    techniqueLayout->setSpacing(5);
    techniqueLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    auto* techniqueIntro = new QLabel("SR Ascendant = Day 1. Move 1° per day from the SR date to the next SR date.", solarTechniquePanel_);
    techniqueIntro->setWordWrap(true);
    techniqueIntro->setObjectName("hintLabel");
    techniqueIntro->setText("Move the SR Ascendant 1 degree per day through the selected technique range.");
    techniqueLayout->addWidget(techniqueIntro);

    auto* techniqueRangeGroup = new QGroupBox("Technique Range", solarTechniquePanel_);
    auto* techniqueRangeLayout = new QVBoxLayout(techniqueRangeGroup);
    solarTechniqueRangeLabel_ = new QLabel("Calculate Solar Return to load the technique range.", techniqueRangeGroup);
    solarTechniqueRangeLabel_->setObjectName("hintLabel");
    techniqueRangeLayout->addWidget(solarTechniqueRangeLabel_);
    techniqueLayout->addWidget(techniqueRangeGroup);

    auto* techniqueModeGroup = new QGroupBox("Tick Counting", solarTechniquePanel_);
    auto* techniqueModeLayout = new QGridLayout(techniqueModeGroup);
    techniqueModeLayout->setHorizontalSpacing(8);
    techniqueModeLayout->setVerticalSpacing(6);
    solarTechniqueModeCombo_ = new QComboBox(techniqueModeGroup);
    solarTechniqueModeCombo_->addItem("SR Start Date (Loaded Chart)",
                                      static_cast<int>(SolarTechniqueCountingMode::SRStartDate));
    solarTechniqueModeCombo_->addItem("Symbolic January 1",
                                      static_cast<int>(SolarTechniqueCountingMode::SymbolicJanuaryFirst));
    solarTechniqueModeCombo_->setToolTip(
        "SR Start Date begins from the loaded Solar Return's local date. "
        "Symbolic January 1 treats the SR Ascendant as January 1 of the selected year.");
    techniqueModeLayout->addWidget(new QLabel("Mode", techniqueModeGroup), 0, 0);
    techniqueModeLayout->addWidget(solarTechniqueModeCombo_, 0, 1);
    techniqueLayout->addWidget(techniqueModeGroup);

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

    auto* techniqueBodyGroup = new QGroupBox("Technique Bodies", solarTechniquePanel_);
    auto* techniqueBodyLayout = new QGridLayout(techniqueBodyGroup);
    techniqueBodyLayout->setHorizontalSpacing(8);
    techniqueBodyLayout->setVerticalSpacing(6);
    techniqueBodyLayout->setColumnStretch(1, 1);
    solarTechniqueBodyPresetCombo_ = new QComboBox(techniqueBodyGroup);
    solarTechniqueBodyPresetCombo_->addItem("Core (Planets + Nodes + Angles)",
        static_cast<int>(SolarTechniqueBodyPreset::Core));
    solarTechniqueBodyPresetCombo_->addItem("Core + Lots",
        static_cast<int>(SolarTechniqueBodyPreset::CoreWithLots));
    solarTechniqueBodyPresetCombo_->addItem("Full Chart Bodies",
        static_cast<int>(SolarTechniqueBodyPreset::FullChartBodies));
    solarTechniqueBodyPresetCombo_->addItem("Custom",
        static_cast<int>(SolarTechniqueBodyPreset::Custom));
    solarTechniqueBodyPresetCombo_->setToolTip(
        "Core uses the main planets, lunar nodes, and the four angles. "
        "Use Full Chart Bodies to reproduce the broader chart-wide scoring.");
    auto* techniqueBodyChecks = new QWidget(techniqueBodyGroup);
    auto* techniqueBodyChecksLayout = new QGridLayout(techniqueBodyChecks);
    techniqueBodyChecksLayout->setContentsMargins(0, 0, 0, 0);
    techniqueBodyChecksLayout->setHorizontalSpacing(12);
    techniqueBodyChecksLayout->setVerticalSpacing(4);
    solarTechniqueBodyPlanetsCheck_ = new QCheckBox("Planets", techniqueBodyChecks);
    solarTechniqueBodyNodesCheck_ = new QCheckBox("Nodes", techniqueBodyChecks);
    solarTechniqueBodyAnglesCheck_ = new QCheckBox("Angles", techniqueBodyChecks);
    solarTechniqueBodyLotsCheck_ = new QCheckBox("Arabic Lots", techniqueBodyChecks);
    solarTechniqueBodyAsteroidsCheck_ = new QCheckBox("Asteroids", techniqueBodyChecks);
    solarTechniqueBodyLilithCheck_ = new QCheckBox("Lilith", techniqueBodyChecks);
    solarTechniqueBodyVertexCheck_ = new QCheckBox("Vertex", techniqueBodyChecks);
    techniqueBodyChecksLayout->addWidget(solarTechniqueBodyPlanetsCheck_, 0, 0);
    techniqueBodyChecksLayout->addWidget(solarTechniqueBodyLotsCheck_, 0, 1);
    techniqueBodyChecksLayout->addWidget(solarTechniqueBodyNodesCheck_, 1, 0);
    techniqueBodyChecksLayout->addWidget(solarTechniqueBodyAsteroidsCheck_, 1, 1);
    techniqueBodyChecksLayout->addWidget(solarTechniqueBodyAnglesCheck_, 2, 0);
    techniqueBodyChecksLayout->addWidget(solarTechniqueBodyLilithCheck_, 2, 1);
    techniqueBodyChecksLayout->addWidget(solarTechniqueBodyVertexCheck_, 3, 0);
    techniqueBodyLayout->addWidget(new QLabel("Preset", techniqueBodyGroup), 0, 0);
    techniqueBodyLayout->addWidget(solarTechniqueBodyPresetCombo_, 0, 1);
    techniqueBodyLayout->addWidget(techniqueBodyChecks, 1, 0, 1, 2);
    techniqueLayout->addWidget(techniqueBodyGroup);
    applySolarTechniqueBodyPreset(SolarTechniqueBodyPreset::Core, false);

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

    auto* solarTechniqueScroll = new QScrollArea(tabs_);
    solarTechniqueScroll->setWidgetResizable(true);
    solarTechniqueScroll->setFrameShape(QFrame::NoFrame);
    solarTechniqueScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    solarTechniqueScroll->setWidget(solarTechniquePage);
    solarTechniquePanel_ = solarTechniqueScroll;
    tabs_->addTab(solarTechniquePanel_, "Technique");
    tabs_->setTabVisible(tabs_->indexOf(solarTechniquePanel_), false);

    auto* solarPlacementFinderPage = new QWidget();
    solarPlacementFinderPanel_ = solarPlacementFinderPage;
    auto* finderLayout = new QVBoxLayout(solarPlacementFinderPage);
    finderLayout->setContentsMargins(0, 0, 0, 0);
    finderLayout->setSpacing(5);
    finderLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

    auto* finderRangeGroup = new QGroupBox("Year Range", solarPlacementFinderPanel_);
    auto* finderRangeLayout = new QGridLayout(finderRangeGroup);
    finderRangeLayout->setHorizontalSpacing(8);
    finderRangeLayout->setVerticalSpacing(6);
    const int currentSolarFinderYear = QDate::currentDate().year();
    solarFinderStartYearSpin_ = new QSpinBox(finderRangeGroup);
    solarFinderStartYearSpin_->setRange(1800, 2399);
    solarFinderStartYearSpin_->setValue(std::clamp(currentSolarFinderYear - 5, 1800, 2399));
    solarFinderEndYearSpin_ = new QSpinBox(finderRangeGroup);
    solarFinderEndYearSpin_->setRange(1800, 2399);
    solarFinderEndYearSpin_->setValue(std::clamp(currentSolarFinderYear + 5, 1800, 2399));
    finderRangeLayout->addWidget(new QLabel("Start Year", finderRangeGroup), 0, 0);
    finderRangeLayout->addWidget(solarFinderStartYearSpin_, 0, 1);
    finderRangeLayout->addWidget(new QLabel("End Year", finderRangeGroup), 1, 0);
    finderRangeLayout->addWidget(solarFinderEndYearSpin_, 1, 1);
    finderLayout->addWidget(finderRangeGroup);

    auto* finderFilterGroup = new QGroupBox("Filter", solarPlacementFinderPanel_);
    auto* finderFilterLayout = new QGridLayout(finderFilterGroup);
    finderFilterLayout->setHorizontalSpacing(8);
    finderFilterLayout->setVerticalSpacing(6);
    solarFinderModeCombo_ = new QComboBox(finderFilterGroup);
    solarFinderModeCombo_->addItem("Single Planet");
    solarFinderModeCombo_->addItem("Stellium");
    solarFinderModeCombo_->addItem("House Ruler");
    solarFinderModeCombo_->addItem("Profection Lord");

    solarFinderPlanetCombo_ = new QComboBox(finderFilterGroup);
    for (const auto& bodyName : solarPlacementFinderPlanetOrder()) {
        solarFinderPlanetCombo_->addItem(bodyName);
    }
    solarFinderPlanetCombo_->setCurrentText("Sun");

    solarFinderPlanet2Combo_ = new QComboBox(finderFilterGroup);
    solarFinderPlanet2Combo_->addItem("None");
    for (const auto& bodyName : solarPlacementFinderPlanetOrder()) {
        solarFinderPlanet2Combo_->addItem(bodyName);
    }
    solarFinderPlanet2Combo_->setCurrentText("None");
    solarFinderPlanet2Combo_->setToolTip("Optional second planet (OR): a year matches if either planet meets the criteria.");

    solarFinderStelliumCountSpin_ = new QSpinBox(finderFilterGroup);
    solarFinderStelliumCountSpin_->setRange(2, 10);
    solarFinderStelliumCountSpin_->setValue(3);
    solarFinderStelliumCountSpin_->setSuffix(" planets");
    solarFinderStelliumCountSpin_->setToolTip("Minimum number of planets in one house to count as a stellium.");
    solarFinderStelliumCountSpin_->setEnabled(false);

    solarFinderHouseCombo_ = new QComboBox(finderFilterGroup);
    solarFinderHouseCombo_->addItem("Any house", 0);
    for (int house = 1; house <= 12; ++house) {
        solarFinderHouseCombo_->addItem(QString::number(house), house);
    }
    solarFinderHouseCombo_->setCurrentIndex(1);
    solarFinderHouseSystemCombo_ = new QComboBox(finderFilterGroup);
    solarFinderHouseSystemCombo_->addItem("Whole Sign", static_cast<int>(SolarPlacementFinderHouseMode::WholeSign));
    solarFinderHouseSystemCombo_->addItem("Placidus", static_cast<int>(SolarPlacementFinderHouseMode::Placidus));
    solarFinderHouseSystemCombo_->addItem("Both (OR)", static_cast<int>(SolarPlacementFinderHouseMode::Both));
    solarFinderHouseSystemCombo_->addItem("Both (AND)", static_cast<int>(SolarPlacementFinderHouseMode::BothAnd));
    solarFinderConjunctionTargetCombo_ = new QComboBox(finderFilterGroup);
    for (const auto& targetName : solarPlacementFinderConjunctionTargets()) {
        solarFinderConjunctionTargetCombo_->addItem(targetName);
    }
    solarFinderConjunctionTargetCombo_->setCurrentText("None");
    solarFinderConjunctionOrbSpin_ = new QDoubleSpinBox(finderFilterGroup);
    solarFinderConjunctionOrbSpin_->setRange(0.1, 15.0);
    solarFinderConjunctionOrbSpin_->setDecimals(2);
    solarFinderConjunctionOrbSpin_->setSingleStep(0.1);
    solarFinderConjunctionOrbSpin_->setValue(1.0);
    solarFinderConjunctionOrbSpin_->setSuffix(" deg");
    solarFinderRulerHouseCombo_ = new QComboBox(finderFilterGroup);
    for (int house = 1; house <= 12; ++house) {
        solarFinderRulerHouseCombo_->addItem(QString::number(house), house);
    }
    solarFinderRulerHouseCombo_->setCurrentIndex(6);  // 7th house default
    solarFinderRulerHouseCombo_->setToolTip("Find returns where the ruler of this house falls in the target House.");
    solarFinderRulerHouseCombo_->setEnabled(false);
    solarFinderRulerSchemeCombo_ = new QComboBox(finderFilterGroup);
    solarFinderRulerSchemeCombo_->addItem("Traditional", 0);
    solarFinderRulerSchemeCombo_->addItem("Modern", 1);
    solarFinderRulerSchemeCombo_->setToolTip("Traditional: classical domicile rulers. Modern: Scorpio->Pluto, Aquarius->Uranus, Pisces->Neptune.");
    solarFinderRulerSchemeCombo_->setEnabled(false);
    finderFilterLayout->addWidget(new QLabel("Search Type", finderFilterGroup), 0, 0);
    finderFilterLayout->addWidget(solarFinderModeCombo_, 0, 1);
    finderFilterLayout->addWidget(new QLabel("Planet", finderFilterGroup), 1, 0);
    finderFilterLayout->addWidget(solarFinderPlanetCombo_, 1, 1);
    finderFilterLayout->addWidget(new QLabel("or Planet", finderFilterGroup), 2, 0);
    finderFilterLayout->addWidget(solarFinderPlanet2Combo_, 2, 1);
    finderFilterLayout->addWidget(new QLabel("Min Planets", finderFilterGroup), 3, 0);
    finderFilterLayout->addWidget(solarFinderStelliumCountSpin_, 3, 1);
    finderFilterLayout->addWidget(new QLabel("Ruler of House", finderFilterGroup), 4, 0);
    finderFilterLayout->addWidget(solarFinderRulerHouseCombo_, 4, 1);
    finderFilterLayout->addWidget(new QLabel("Rulership", finderFilterGroup), 5, 0);
    finderFilterLayout->addWidget(solarFinderRulerSchemeCombo_, 5, 1);
    finderFilterLayout->addWidget(new QLabel("House", finderFilterGroup), 6, 0);
    finderFilterLayout->addWidget(solarFinderHouseCombo_, 6, 1);
    finderFilterLayout->addWidget(new QLabel("House System", finderFilterGroup), 7, 0);
    finderFilterLayout->addWidget(solarFinderHouseSystemCombo_, 7, 1);
    finderFilterLayout->addWidget(new QLabel("Conjunction", finderFilterGroup), 8, 0);
    finderFilterLayout->addWidget(solarFinderConjunctionTargetCombo_, 8, 1);
    finderFilterLayout->addWidget(new QLabel("Conj. Orb", finderFilterGroup), 9, 0);
    finderFilterLayout->addWidget(solarFinderConjunctionOrbSpin_, 9, 1);
    finderLayout->addWidget(finderFilterGroup);

    auto* finderRunGroup = new QGroupBox("Run", solarPlacementFinderPanel_);
    auto* finderRunLayout = new QHBoxLayout(finderRunGroup);
    solarFinderRunButton_ = new QPushButton("Find Matching Years", finderRunGroup);
    solarFinderStatusLabel_ = new QLabel("Idle", finderRunGroup);
    solarFinderStatusLabel_->setObjectName("hintLabel");
    finderRunLayout->addWidget(solarFinderRunButton_);
    finderRunLayout->addStretch();
    finderRunLayout->addWidget(solarFinderStatusLabel_);
    finderLayout->addWidget(finderRunGroup);
    finderLayout->addStretch();

    auto* solarPlacementFinderScroll = new QScrollArea(tabs_);
    solarPlacementFinderScroll->setWidgetResizable(true);
    solarPlacementFinderScroll->setFrameShape(QFrame::NoFrame);
    solarPlacementFinderScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    solarPlacementFinderScroll->setWidget(solarPlacementFinderPage);
    solarPlacementFinderPanel_ = solarPlacementFinderScroll;
    tabs_->addTab(solarPlacementFinderPanel_, "Placement Finder");
    tabs_->setTabVisible(tabs_->indexOf(solarPlacementFinderPanel_), false);

    auto* lunarPlacementFinderPage = new QWidget();
    lunarPlacementFinderPanel_ = lunarPlacementFinderPage;
    auto* lunarFinderLayout = new QVBoxLayout(lunarPlacementFinderPage);
    lunarFinderLayout->setContentsMargins(0, 0, 0, 0);
    lunarFinderLayout->setSpacing(5);
    lunarFinderLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

    auto* lunarFinderRangeGroup = new QGroupBox("Date Range", lunarPlacementFinderPanel_);
    auto* lunarFinderRangeLayout = new QGridLayout(lunarFinderRangeGroup);
    lunarFinderRangeLayout->setHorizontalSpacing(8);
    lunarFinderRangeLayout->setVerticalSpacing(6);
    lunarFinderStartDateEdit_ = new QDateEdit(lunarFinderRangeGroup);
    lunarFinderStartDateEdit_->setCalendarPopup(true);
    lunarFinderStartDateEdit_->setDisplayFormat("yyyy-MM-dd");
    lunarFinderStartDateEdit_->setDateRange(QDate(1800, 1, 1), QDate(2399, 12, 31));
    lunarFinderStartDateEdit_->setDate(QDate::currentDate().addMonths(-6));
    lunarFinderEndDateEdit_ = new QDateEdit(lunarFinderRangeGroup);
    lunarFinderEndDateEdit_->setCalendarPopup(true);
    lunarFinderEndDateEdit_->setDisplayFormat("yyyy-MM-dd");
    lunarFinderEndDateEdit_->setDateRange(QDate(1800, 1, 1), QDate(2399, 12, 31));
    lunarFinderEndDateEdit_->setDate(QDate::currentDate().addMonths(6));
    lunarFinderRangeLayout->addWidget(new QLabel("Start Date", lunarFinderRangeGroup), 0, 0);
    lunarFinderRangeLayout->addWidget(lunarFinderStartDateEdit_, 0, 1);
    lunarFinderRangeLayout->addWidget(new QLabel("End Date", lunarFinderRangeGroup), 1, 0);
    lunarFinderRangeLayout->addWidget(lunarFinderEndDateEdit_, 1, 1);
    lunarFinderLayout->addWidget(lunarFinderRangeGroup);

    auto* lunarFinderFilterGroup = new QGroupBox("Filter", lunarPlacementFinderPanel_);
    auto* lunarFinderFilterLayout = new QGridLayout(lunarFinderFilterGroup);
    lunarFinderFilterLayout->setHorizontalSpacing(8);
    lunarFinderFilterLayout->setVerticalSpacing(6);
    lunarFinderModeCombo_ = new QComboBox(lunarFinderFilterGroup);
    lunarFinderModeCombo_->addItem("Single Planet");
    lunarFinderModeCombo_->addItem("Stellium");
    lunarFinderModeCombo_->addItem("House Ruler");
    lunarFinderModeCombo_->addItem("Profection Lord");
    lunarFinderPlanetCombo_ = new QComboBox(lunarFinderFilterGroup);
    for (const auto& bodyName : solarPlacementFinderPlanetOrder()) {
        lunarFinderPlanetCombo_->addItem(bodyName);
    }
    lunarFinderPlanetCombo_->setCurrentText("Moon");
    lunarFinderPlanet2Combo_ = new QComboBox(lunarFinderFilterGroup);
    lunarFinderPlanet2Combo_->addItem("None");
    for (const auto& bodyName : solarPlacementFinderPlanetOrder()) {
        lunarFinderPlanet2Combo_->addItem(bodyName);
    }
    lunarFinderPlanet2Combo_->setCurrentText("None");
    lunarFinderPlanet2Combo_->setToolTip("Optional second planet (OR): a return matches if either planet meets the criteria.");
    lunarFinderStelliumCountSpin_ = new QSpinBox(lunarFinderFilterGroup);
    lunarFinderStelliumCountSpin_->setRange(2, 10);
    lunarFinderStelliumCountSpin_->setValue(3);
    lunarFinderStelliumCountSpin_->setSuffix(" planets");
    lunarFinderStelliumCountSpin_->setToolTip("Minimum number of planets in one house to count as a stellium.");
    lunarFinderStelliumCountSpin_->setEnabled(false);
    lunarFinderHouseCombo_ = new QComboBox(lunarFinderFilterGroup);
    lunarFinderHouseCombo_->addItem("Any house", 0);
    for (int house = 1; house <= 12; ++house) {
        lunarFinderHouseCombo_->addItem(QString::number(house), house);
    }
    lunarFinderHouseCombo_->setCurrentIndex(1);
    lunarFinderHouseSystemCombo_ = new QComboBox(lunarFinderFilterGroup);
    lunarFinderHouseSystemCombo_->addItem("Whole Sign", static_cast<int>(SolarPlacementFinderHouseMode::WholeSign));
    lunarFinderHouseSystemCombo_->addItem("Placidus", static_cast<int>(SolarPlacementFinderHouseMode::Placidus));
    lunarFinderHouseSystemCombo_->addItem("Both (OR)", static_cast<int>(SolarPlacementFinderHouseMode::Both));
    lunarFinderHouseSystemCombo_->addItem("Both (AND)", static_cast<int>(SolarPlacementFinderHouseMode::BothAnd));
    lunarFinderConjunctionTargetCombo_ = new QComboBox(lunarFinderFilterGroup);
    for (const auto& targetName : solarPlacementFinderConjunctionTargets()) {
        lunarFinderConjunctionTargetCombo_->addItem(targetName);
    }
    lunarFinderConjunctionTargetCombo_->setCurrentText("None");
    lunarFinderConjunctionOrbSpin_ = new QDoubleSpinBox(lunarFinderFilterGroup);
    lunarFinderConjunctionOrbSpin_->setRange(0.1, 15.0);
    lunarFinderConjunctionOrbSpin_->setDecimals(2);
    lunarFinderConjunctionOrbSpin_->setSingleStep(0.1);
    lunarFinderConjunctionOrbSpin_->setValue(1.0);
    lunarFinderConjunctionOrbSpin_->setSuffix(" deg");
    lunarFinderRulerHouseCombo_ = new QComboBox(lunarFinderFilterGroup);
    for (int house = 1; house <= 12; ++house) {
        lunarFinderRulerHouseCombo_->addItem(QString::number(house), house);
    }
    lunarFinderRulerHouseCombo_->setCurrentIndex(6);  // 7th house default
    lunarFinderRulerHouseCombo_->setToolTip("Find returns where the ruler of this house falls in the target House.");
    lunarFinderRulerHouseCombo_->setEnabled(false);
    lunarFinderRulerSchemeCombo_ = new QComboBox(lunarFinderFilterGroup);
    lunarFinderRulerSchemeCombo_->addItem("Traditional", 0);
    lunarFinderRulerSchemeCombo_->addItem("Modern", 1);
    lunarFinderRulerSchemeCombo_->setToolTip("Traditional: classical domicile rulers. Modern: Scorpio->Pluto, Aquarius->Uranus, Pisces->Neptune.");
    lunarFinderRulerSchemeCombo_->setEnabled(false);
    lunarFinderFilterLayout->addWidget(new QLabel("Search Type", lunarFinderFilterGroup), 0, 0);
    lunarFinderFilterLayout->addWidget(lunarFinderModeCombo_, 0, 1);
    lunarFinderFilterLayout->addWidget(new QLabel("Planet", lunarFinderFilterGroup), 1, 0);
    lunarFinderFilterLayout->addWidget(lunarFinderPlanetCombo_, 1, 1);
    lunarFinderFilterLayout->addWidget(new QLabel("or Planet", lunarFinderFilterGroup), 2, 0);
    lunarFinderFilterLayout->addWidget(lunarFinderPlanet2Combo_, 2, 1);
    lunarFinderFilterLayout->addWidget(new QLabel("Min Planets", lunarFinderFilterGroup), 3, 0);
    lunarFinderFilterLayout->addWidget(lunarFinderStelliumCountSpin_, 3, 1);
    lunarFinderFilterLayout->addWidget(new QLabel("Ruler of House", lunarFinderFilterGroup), 4, 0);
    lunarFinderFilterLayout->addWidget(lunarFinderRulerHouseCombo_, 4, 1);
    lunarFinderFilterLayout->addWidget(new QLabel("Rulership", lunarFinderFilterGroup), 5, 0);
    lunarFinderFilterLayout->addWidget(lunarFinderRulerSchemeCombo_, 5, 1);
    lunarFinderFilterLayout->addWidget(new QLabel("House", lunarFinderFilterGroup), 6, 0);
    lunarFinderFilterLayout->addWidget(lunarFinderHouseCombo_, 6, 1);
    lunarFinderFilterLayout->addWidget(new QLabel("House System", lunarFinderFilterGroup), 7, 0);
    lunarFinderFilterLayout->addWidget(lunarFinderHouseSystemCombo_, 7, 1);
    lunarFinderFilterLayout->addWidget(new QLabel("Conjunction", lunarFinderFilterGroup), 8, 0);
    lunarFinderFilterLayout->addWidget(lunarFinderConjunctionTargetCombo_, 8, 1);
    lunarFinderFilterLayout->addWidget(new QLabel("Conj. Orb", lunarFinderFilterGroup), 9, 0);
    lunarFinderFilterLayout->addWidget(lunarFinderConjunctionOrbSpin_, 9, 1);
    lunarFinderLayout->addWidget(lunarFinderFilterGroup);

    auto* lunarFinderRunGroup = new QGroupBox("Run", lunarPlacementFinderPanel_);
    auto* lunarFinderRunLayout = new QHBoxLayout(lunarFinderRunGroup);
    lunarFinderRunButton_ = new QPushButton("Find Matching Returns", lunarFinderRunGroup);
    lunarFinderStatusLabel_ = new QLabel("Idle", lunarFinderRunGroup);
    lunarFinderStatusLabel_->setObjectName("hintLabel");
    lunarFinderRunLayout->addWidget(lunarFinderRunButton_);
    lunarFinderRunLayout->addStretch();
    lunarFinderRunLayout->addWidget(lunarFinderStatusLabel_);
    lunarFinderLayout->addWidget(lunarFinderRunGroup);
    lunarFinderLayout->addStretch();

    auto* lunarPlacementFinderScroll = new QScrollArea(tabs_);
    lunarPlacementFinderScroll->setWidgetResizable(true);
    lunarPlacementFinderScroll->setFrameShape(QFrame::NoFrame);
    lunarPlacementFinderScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lunarPlacementFinderScroll->setWidget(lunarPlacementFinderPage);
    lunarPlacementFinderPanel_ = lunarPlacementFinderScroll;
    tabs_->addTab(lunarPlacementFinderPanel_, "LR Finder");
    tabs_->setTabVisible(tabs_->indexOf(lunarPlacementFinderPanel_), false);

    auto* dataPanel = new QFrame(this);
    dataPanel->setObjectName("dataPanel");
    auto* dataLayout = new QVBoxLayout(dataPanel);
    dataLayout->setContentsMargins(6, 6, 6, 6);

    progressionControls_ = new QWidget(dataPanel);
    auto* progressionLayout = new QVBoxLayout(progressionControls_);
    progressionLayout->setContentsMargins(0, 0, 0, 0);
    progressionLayout->setSpacing(5);

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

    auto* progressedLunarReturnGroup = new QGroupBox("Progressed Lunar Return", progressionControls_);
    auto* progressedLunarReturnLayout = new QVBoxLayout(progressedLunarReturnGroup);
    auto* progressedLunarReturnHint = new QLabel(
        "Find the exact moment when the secondary progressed Moon returns to the natal Moon. "
        "The progression target above is used as the search anchor.",
        progressedLunarReturnGroup);
    progressedLunarReturnHint->setObjectName("hintLabel");
    progressedLunarReturnHint->setWordWrap(true);
    auto* progressedLunarReturnButtons = new QHBoxLayout();
    progressionLunarReturnPreviousButton_ = new QPushButton("\u2190 Previous Return", progressedLunarReturnGroup);
    progressionLunarReturnNextButton_ = new QPushButton("Next Return \u2192", progressedLunarReturnGroup);
    progressionLunarReturnPreviousButton_->setToolTip(
        "Find the previous postnatal progressed lunar return before the target moment.");
    progressionLunarReturnNextButton_->setToolTip(
        "Find the next progressed lunar return after the target moment.");
    progressedLunarReturnButtons->addWidget(progressionLunarReturnPreviousButton_);
    progressedLunarReturnButtons->addWidget(progressionLunarReturnNextButton_);
    progressionLunarReturnStatusLabel_ = new QLabel("No progressed lunar return selected.", progressedLunarReturnGroup);
    progressionLunarReturnStatusLabel_->setObjectName("hintLabel");
    progressionLunarReturnStatusLabel_->setWordWrap(true);
    progressedLunarReturnLayout->addWidget(progressedLunarReturnHint);
    progressedLunarReturnLayout->addLayout(progressedLunarReturnButtons);
    progressedLunarReturnLayout->addWidget(progressionLunarReturnStatusLabel_);

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
    progressionLayout->addWidget(progressedLunarReturnGroup);
    progressionLayout->addWidget(progressionRunGroup);
    progressionLayout->addStretch();
    progressionControls_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    progressionControls_->setVisible(false);

    solarControls_ = new QWidget(dataPanel);
    auto* solarLayout = new QVBoxLayout(solarControls_);
    solarLayout->setContentsMargins(0, 0, 0, 0);
    solarLayout->setSpacing(5);

    auto* solarYearGroup = new QGroupBox("Solar Return", solarControls_);
    auto* solarYearLayout = new QGridLayout(solarYearGroup);
    solarYearLayout->setHorizontalSpacing(8);
    solarYearLayout->setVerticalSpacing(6);
    solarYearLayout->setColumnStretch(1, 1);
    solarYearSpin_ = new QSpinBox(solarYearGroup);
    solarYearSpin_->setRange(1800, 2399);
    solarYearSpin_->setValue(QDate::currentDate().year());
    solarPreviousButton_ = new QPushButton("\u2190 Previous", solarYearGroup);
    solarNowButton_ = new QPushButton("Now", solarYearGroup);
    solarNextButton_ = new QPushButton("Next \u2192", solarYearGroup);
    solarPreviousButton_->setToolTip("Calculate the previous year's solar return.");
    solarNowButton_->setToolTip(
        "Load the solar return currently in effect (the latest return at or before the current moment).");
    solarNextButton_->setToolTip("Calculate the next year's solar return.");
    solarTimezoneEdit_ = new QLineEdit(solarYearGroup);
    solarTimezoneEdit_->setPlaceholderText("Timezone (e.g., Asia/Dhaka)");
    solarTimezoneEdit_->setText("UTC");
    solarTimezoneStatus_ = new QLabel("OK", solarYearGroup);
    solarTimezoneStatus_->setMinimumWidth(40);
    solarYearLayout->addWidget(new QLabel("Year", solarYearGroup), 0, 0);
    solarYearLayout->addWidget(solarYearSpin_, 0, 1, 1, 2);
    auto* solarNavigationRow = new QWidget(solarYearGroup);
    auto* solarNavigationLayout = new QHBoxLayout(solarNavigationRow);
    solarNavigationLayout->setContentsMargins(0, 0, 0, 0);
    solarNavigationLayout->setSpacing(6);
    solarNavigationLayout->addWidget(solarPreviousButton_, 1);
    solarNavigationLayout->addWidget(solarNowButton_, 1);
    solarNavigationLayout->addWidget(solarNextButton_, 1);
    solarYearLayout->addWidget(solarNavigationRow, 1, 0, 1, 3);
    solarYearLayout->addWidget(new QLabel("Timezone", solarYearGroup), 2, 0);
    solarYearLayout->addWidget(solarTimezoneEdit_, 2, 1);
    solarYearLayout->addWidget(solarTimezoneStatus_, 2, 2);

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
    auto* solarFinderShortcutButton = new QPushButton("Open Return Finder", solarRunGroup);
    solarFinderShortcutButton->setToolTip("Open the dedicated Return Finder with Solar Returns selected.");
    solarStatusLabel_ = new QLabel("Pending changes", solarRunGroup);
    solarStatusLabel_->setObjectName("hintLabel");
    solarLastLabel_ = new QLabel("Last calculated: -", solarRunGroup);
    solarLastLabel_->setObjectName("hintLabel");
    solarRunLayout->addWidget(solarCalculateButton_);
    solarRunLayout->addWidget(solarFinderShortcutButton);
    solarRunLayout->addStretch();
    solarRunLayout->addWidget(solarStatusLabel_);
    solarRunLayout->addWidget(solarLastLabel_);

    solarLayout->addWidget(solarYearGroup);
    solarLayout->addWidget(solarLocationGroup);
    solarLayout->addWidget(solarRunGroup);
    solarLayout->addStretch();
    connect(solarFinderShortcutButton, &QPushButton::clicked, this, [this]() {
        if (returnFinderController_) returnFinderController_->setReturnType(ReturnFinderType::Solar);
        for (int i = 0; mainTabBar_ && i < mainTabBar_->count(); ++i) {
            if (mainTabBar_->tabData(i).toInt() == static_cast<int>(AppTab::ReturnFinder)) {
                mainTabBar_->setCurrentIndex(i);
                break;
            }
        }
    });
    solarControls_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    solarControls_->setVisible(false);

    lunarControls_ = new QWidget(dataPanel);
    auto* lunarLayout = new QVBoxLayout(lunarControls_);
    lunarLayout->setContentsMargins(0, 0, 0, 0);
    lunarLayout->setSpacing(5);

    auto* lunarReturnGroup = new QGroupBox("Lunar Return", lunarControls_);
    auto* lunarReturnLayout = new QGridLayout(lunarReturnGroup);
    lunarReturnLayout->setHorizontalSpacing(8);
    lunarReturnLayout->setVerticalSpacing(6);
    lunarAnchorDateEdit_ = new QDateEdit(lunarReturnGroup);
    lunarAnchorDateEdit_->setCalendarPopup(true);
    lunarAnchorDateEdit_->setDisplayFormat("yyyy-MM-dd");
    lunarAnchorDateEdit_->setDateRange(QDate(1800, 1, 1), QDate(2399, 12, 31));
    lunarAnchorDateEdit_->setDate(QDate::currentDate());
    lunarAnchorDateEdit_->setToolTip("Find the lunar return occurring on or after this date.");
    lunarTimezoneEdit_ = new QLineEdit(lunarReturnGroup);
    lunarTimezoneEdit_->setPlaceholderText("Timezone (e.g., Asia/Dhaka)");
    lunarTimezoneEdit_->setText("UTC");
    lunarTimezoneStatus_ = new QLabel("OK", lunarReturnGroup);
    lunarTimezoneStatus_->setMinimumWidth(40);
    lunarPrevButton_ = new QPushButton("\u2190 Previous", lunarReturnGroup);
    lunarNowButton_ = new QPushButton("Now", lunarReturnGroup);
    lunarNextButton_ = new QPushButton("Next \u2192", lunarReturnGroup);
    lunarPrevButton_->setToolTip("Previous lunar return (~27.3 days earlier).");
    lunarNowButton_->setToolTip(
        "Load the lunar return currently in effect (the latest return at or before the current moment).");
    lunarNextButton_->setToolTip("Next lunar return (~27.3 days later).");
    lunarReturnLayout->addWidget(new QLabel("On/after date", lunarReturnGroup), 0, 0);
    lunarReturnLayout->addWidget(lunarAnchorDateEdit_, 0, 1);
    lunarReturnLayout->addWidget(new QLabel("Timezone", lunarReturnGroup), 1, 0);
    lunarReturnLayout->addWidget(lunarTimezoneEdit_, 1, 1);
    lunarReturnLayout->addWidget(lunarTimezoneStatus_, 1, 2);
    auto* lunarNavigationRow = new QWidget(lunarReturnGroup);
    auto* lunarNavigationLayout = new QHBoxLayout(lunarNavigationRow);
    lunarNavigationLayout->setContentsMargins(0, 0, 0, 0);
    lunarNavigationLayout->setSpacing(6);
    lunarNavigationLayout->addWidget(lunarPrevButton_, 1);
    lunarNavigationLayout->addWidget(lunarNowButton_, 1);
    lunarNavigationLayout->addWidget(lunarNextButton_, 1);
    lunarReturnLayout->addWidget(lunarNavigationRow, 2, 0, 1, 3);

    auto* lunarLocationGroup = new QGroupBox("Location", lunarControls_);
    auto* lunarLocationLayout = new QGridLayout(lunarLocationGroup);
    lunarLocationLayout->setHorizontalSpacing(8);
    lunarLocationLayout->setVerticalSpacing(6);
    lunarLocationLayout->setColumnStretch(1, 1);
    lunarLocationLayout->setColumnStretch(3, 1);
    lunarUseNatalRadio_ = new QRadioButton("Use natal location", lunarLocationGroup);
    lunarUseCustomRadio_ = new QRadioButton("Use custom location", lunarLocationGroup);
    lunarUseNatalRadio_->setChecked(true);
    lunarLocationEdit_ = new QLineEdit(lunarLocationGroup);
    lunarLocationEdit_->setPlaceholderText("Location");
    lunarGeocodeButton_ = new QPushButton("Geocode", lunarLocationGroup);
    lunarLatSpin_ = new QDoubleSpinBox(lunarLocationGroup);
    lunarLonSpin_ = new QDoubleSpinBox(lunarLocationGroup);
    lunarLatSpin_->setRange(-90.0, 90.0);
    lunarLonSpin_->setRange(-180.0, 180.0);
    lunarLatSpin_->setDecimals(6);
    lunarLonSpin_->setDecimals(6);
    lunarLatSpin_->setSingleStep(0.01);
    lunarLonSpin_->setSingleStep(0.01);
    lunarLocationLayout->addWidget(lunarUseNatalRadio_, 0, 0, 1, 2);
    lunarLocationLayout->addWidget(lunarUseCustomRadio_, 0, 2, 1, 2);
    lunarLocationLayout->addWidget(new QLabel("Location", lunarLocationGroup), 1, 0);
    lunarLocationLayout->addWidget(lunarLocationEdit_, 1, 1, 1, 2);
    lunarLocationLayout->addWidget(lunarGeocodeButton_, 1, 3);
    lunarLocationLayout->addWidget(new QLabel("Latitude", lunarLocationGroup), 2, 0);
    lunarLocationLayout->addWidget(lunarLatSpin_, 2, 1);
    lunarLocationLayout->addWidget(new QLabel("Longitude", lunarLocationGroup), 2, 2);
    lunarLocationLayout->addWidget(lunarLonSpin_, 2, 3);

    auto* lunarRunGroup = new QGroupBox("Run", lunarControls_);
    auto* lunarRunLayout = new QHBoxLayout(lunarRunGroup);
    lunarCalculateButton_ = new QPushButton("Find Lunar Return", lunarRunGroup);
    auto* lunarFinderShortcutButton = new QPushButton("Open Return Finder", lunarRunGroup);
    lunarFinderShortcutButton->setToolTip("Open the dedicated Return Finder with Lunar Returns selected.");
    lunarStatusLabel_ = new QLabel("Pending changes", lunarRunGroup);
    lunarStatusLabel_->setObjectName("hintLabel");
    lunarLastLabel_ = new QLabel("Last calculated: -", lunarRunGroup);
    lunarLastLabel_->setObjectName("hintLabel");
    lunarRunLayout->addWidget(lunarCalculateButton_);
    lunarRunLayout->addWidget(lunarFinderShortcutButton);
    lunarRunLayout->addStretch();
    lunarRunLayout->addWidget(lunarStatusLabel_);
    lunarRunLayout->addWidget(lunarLastLabel_);

    lunarLayout->addWidget(lunarReturnGroup);
    lunarLayout->addWidget(lunarLocationGroup);
    lunarLayout->addWidget(lunarRunGroup);
    lunarLayout->addStretch();
    connect(lunarFinderShortcutButton, &QPushButton::clicked, this, [this]() {
        if (returnFinderController_) returnFinderController_->setReturnType(ReturnFinderType::Lunar);
        for (int i = 0; mainTabBar_ && i < mainTabBar_->count(); ++i) {
            if (mainTabBar_->tabData(i).toInt() == static_cast<int>(AppTab::ReturnFinder)) {
                mainTabBar_->setCurrentIndex(i);
                break;
            }
        }
    });
    lunarControls_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    lunarControls_->setVisible(false);

    relocationControls_ = new QWidget(dataPanel);
    auto* relocationLayout = new QVBoxLayout(relocationControls_);
    relocationLayout->setContentsMargins(0, 0, 0, 0);
    relocationLayout->setSpacing(5);

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
    relocationControls_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    relocationControls_->setVisible(false);

    dataLayout->addWidget(progressionControls_);
    dataLayout->addWidget(solarControls_);
    dataLayout->addWidget(lunarControls_);
    dataLayout->addWidget(relocationControls_);
    tabs_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    dataLayout->addWidget(tabs_, 1);

    transitPanel_ = new QFrame(this);
    transitPanel_->setObjectName("dataPanel");
    transitPanel_->setMinimumWidth(0);
    transitPanel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* transitLayout = new QVBoxLayout(transitPanel_);
    transitLayout->setContentsMargins(6, 6, 6, 6);
    transitLayout->setSpacing(6);

    transitSubTabBar_ = new QTabBar(transitPanel_);
    transitSubTabBar_->addTab("Overview");
    transitSubTabBar_->addTab("Search");
    transitSubTabBar_->addTab("Aspect Peaks");
    transitSubTabBar_->addTab("Calendar");
    transitSubTabBar_->addTab("Conjunctions");
    transitSubTabBar_->addTab("Best Days");
    transitSubTabBar_->addTab("Profections");
    transitSubTabBar_->setExpanding(false);
    transitSubTabBar_->setDrawBase(false);
    transitSubTabBar_->setUsesScrollButtons(true);
    transitSubTabBar_->setElideMode(Qt::ElideRight);
    transitSubTabBar_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    transitSubTabBar_->setCurrentIndex(0);
    transitLayout->addWidget(transitSubTabBar_);

    auto* modeGroup = new QGroupBox("Mode", transitPanel_);
    auto* modeLayout = new QVBoxLayout(modeGroup);
    transitOverlayRadio_ = new QRadioButton("Natal Transits (overlay)", modeGroup);
    transitOnlyRadio_ = new QRadioButton("Transit Only", modeGroup);
    transitOverlayRadio_->setChecked(true);
    modeLayout->addWidget(transitOverlayRadio_);
    modeLayout->addWidget(transitOnlyRadio_);
    modeGroup->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* targetGroup = new QGroupBox("Transit Target", transitPanel_);
    auto* targetLayout = new QVBoxLayout(targetGroup);
    transitTargetLabel_ = new QLabel("Transit target: -", targetGroup);
    transitStatusLabel_ = new QLabel("Pending changes", targetGroup);
    transitLastLabel_ = new QLabel("Last calculated: -", targetGroup);
    for (QLabel* label : {transitTargetLabel_, transitStatusLabel_, transitLastLabel_}) {
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    targetLayout->addWidget(transitTargetLabel_);
    targetLayout->addWidget(transitStatusLabel_);
    targetLayout->addWidget(transitLastLabel_);
    targetGroup->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Maximum);

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

    auto* dayNavigationRow = new QHBoxLayout();
    dayNavigationRow->setSpacing(6);
    auto* weekNavigationRow = new QHBoxLayout();
    weekNavigationRow->setSpacing(6);
    transitMinusWeekButton_ = new QPushButton("-1 Week", timeGroup);
    transitMinusDayButton_ = new QPushButton("-1 Day", timeGroup);
    transitNowButton_ = new QPushButton("Now", timeGroup);
    transitPlusDayButton_ = new QPushButton("+1 Day", timeGroup);
    transitPlusWeekButton_ = new QPushButton("+1 Week", timeGroup);
    transitMinusWeekButton_->setToolTip("Previous week (Space+Shift+Left)");
    transitMinusDayButton_->setToolTip("Previous day (Space+Left)");
    transitNowButton_->setToolTip("Current date and time (Space+Home)");
    transitPlusDayButton_->setToolTip("Next day (Space+Right)");
    transitPlusWeekButton_->setToolTip("Next week (Space+Shift+Right)");
    for (QPushButton* button : {transitMinusWeekButton_, transitMinusDayButton_, transitNowButton_,
                                transitPlusDayButton_, transitPlusWeekButton_}) {
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    dayNavigationRow->addWidget(transitMinusDayButton_, 1);
    dayNavigationRow->addWidget(transitNowButton_, 1);
    dayNavigationRow->addWidget(transitPlusDayButton_, 1);
    weekNavigationRow->addWidget(transitMinusWeekButton_, 1);
    weekNavigationRow->addWidget(transitPlusWeekButton_, 1);
    timeLayout->addLayout(dayNavigationRow, 3, 0, 1, 3);
    timeLayout->addLayout(weekNavigationRow, 4, 0, 1, 3);

    auto* transitShortcutHint = new QLabel(
        "Keyboard: hold Space + Left/Right for a day; add Shift for a week; Space+Home for now.",
        timeGroup);
    transitShortcutHint->setObjectName("hintLabel");
    transitShortcutHint->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    transitShortcutHint->setWordWrap(true);
    transitShortcutHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    timeLayout->addWidget(transitShortcutHint, 5, 0, 1, 3);
    timeGroup->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Maximum);

    transitCalculateButton_ = new QPushButton("Calculate Transits", transitPanel_);
    transitCalculateButton_->setDefault(true);

    auto* locationGroup = new QGroupBox("Location", transitPanel_);
    auto* locationLayout = new QGridLayout(locationGroup);
    locationLayout->setHorizontalSpacing(8);
    locationLayout->setVerticalSpacing(6);
    locationLayout->setColumnStretch(1, 1);
    locationLayout->setColumnStretch(2, 0);
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
    transitLocationEdit_->setMinimumWidth(0);
    transitLatSpin_->setMinimumWidth(0);
    transitLonSpin_->setMinimumWidth(0);
    transitLocationEdit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    transitLatSpin_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    transitLonSpin_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    locationLayout->addWidget(transitUseNatalLocation_, 0, 0, 1, 3);
    locationLayout->addWidget(new QLabel("Location", locationGroup), 1, 0);
    locationLayout->addWidget(transitLocationEdit_, 1, 1);
    locationLayout->addWidget(transitGeocodeButton_, 1, 2);
    locationLayout->addWidget(new QLabel("Latitude", locationGroup), 2, 0);
    locationLayout->addWidget(transitLatSpin_, 2, 1, 1, 2);
    locationLayout->addWidget(new QLabel("Longitude", locationGroup), 3, 0);
    locationLayout->addWidget(transitLonSpin_, 3, 1, 1, 2);
    locationGroup->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Maximum);

    auto* houseGroup = new QGroupBox("House System", transitPanel_);
    auto* houseLayout = new QHBoxLayout(houseGroup);
    transitWholeRadio_ = new QRadioButton("Whole Sign", houseGroup);
    transitPlacidusRadio_ = new QRadioButton("Placidus", houseGroup);
    transitWholeRadio_->setChecked(true);
    houseLayout->addWidget(transitWholeRadio_);
    houseLayout->addWidget(transitPlacidusRadio_);
    houseLayout->addStretch();
    houseGroup->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* commonPanel = new QWidget(transitPanel_);
    auto* commonLayout = new QVBoxLayout(commonPanel);
    commonLayout->setContentsMargins(0, 0, 0, 0);
    commonLayout->setSpacing(6);
    auto* commonRow = new QHBoxLayout();
    commonRow->setContentsMargins(0, 0, 0, 0);
    commonRow->setSpacing(6);
    commonRow->addWidget(modeGroup, 1);
    commonRow->addWidget(houseGroup, 1);
    commonLayout->addLayout(commonRow);
    commonPanel->setMinimumWidth(0);
    commonPanel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Maximum);
    transitLayout->addWidget(commonPanel);

    transitPanelStack_ = new QStackedWidget(transitPanel_);
    transitPanelStack_->setMinimumWidth(0);
    transitPanelStack_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    transitOverviewPanel_ = new QWidget(transitPanelStack_);
    auto* transitOverviewLayout = new QVBoxLayout(transitOverviewPanel_);
    transitOverviewLayout->setContentsMargins(0, 0, 0, 0);
    transitOverviewLayout->setSpacing(5);
    transitOverviewLayout->addWidget(targetGroup);
    transitOverviewLayout->addWidget(timeGroup);
    transitOverviewLayout->addWidget(locationGroup);
    transitOverviewLayout->addWidget(transitCalculateButton_);
    transitOverviewLayout->addStretch();
    transitOverviewPanel_->setMinimumWidth(0);
    transitOverviewPanel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    transitSearchPanel_ = new QWidget(transitPanelStack_);
    auto* transitSearchLayout = new QVBoxLayout(transitSearchPanel_);
    transitSearchLayout->setContentsMargins(0, 0, 0, 0);
    transitSearchLayout->setSpacing(5);

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
        const bool checkedByDefault = !isAsteroidBody(name)
            && (!isLunarNodeName(name) || name.startsWith("Mean "));
        item->setData(checkedByDefault ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        transitPlanetModel->appendRow(item);
    }
    searchTransitPlanetCombo_->setModel(transitPlanetModel);
    searchTransitPlanetCombo_->setCurrentIndex(0);
    updateTransitPlanetComboLabel(searchTransitPlanetCombo_);
    searchTargetCombo_ = new QComboBox(searchFilterGroup);
    searchTargetLabel_ = new QLabel("Natal Target", searchFilterGroup);
    searchAspectCombo_ = new QComboBox(searchFilterGroup);
    searchAspectCombo_->addItems({
        "Conjunction",
        "Sextile",
        "Square",
        "Trine",
        "Opposition",
        "Any Major Aspect",
    });
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
    calendarLayout->setSpacing(5);

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
        const bool checkedByDefault = !isAsteroidBody(name) && (name != "Moon")
            && (!isLunarNodeName(name) || name.startsWith("Mean "));
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
    conjLayout->setSpacing(5);

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
        const bool checkedByDefault = !isAsteroidBody(name) && (name != "Moon")
            && (!isLunarNodeName(name) || name.startsWith("Mean "));
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
    conjIncludeMoonCheck_ = new QCheckBox("Include Moon", conjParamGroup);
    conjIncludeMoonCheck_->setChecked(true);
    conjUniqueFirstCheck_ = new QCheckBox("First-time unique only (sign + degree)", conjParamGroup);
    conjUniqueDegreeCombo_ = new QComboBox(conjParamGroup);
    conjUniqueDegreeCombo_->addItem("1.0°", 1.0);
    conjUniqueDegreeCombo_->addItem("0.5°", 0.5);
    conjUniqueDegreeCombo_->addItem("0.1°", 0.1);
    conjUniqueDegreeCombo_->setCurrentIndex(0);
    conjUniqueDegreeCombo_->setEnabled(false);
    conjParamLayout->addWidget(new QLabel("Min Planets (N)", conjParamGroup), 0, 0);
    conjParamLayout->addWidget(conjCountSpin_, 0, 1);
    conjParamLayout->addWidget(conjUseOrbCheck_, 1, 0);
    conjParamLayout->addWidget(conjOrbSpin_, 1, 1);
    conjParamLayout->addWidget(conjIncludeMoonCheck_, 2, 0, 1, 2);
    conjParamLayout->addWidget(conjUniqueFirstCheck_, 3, 0, 1, 2);
    conjParamLayout->addWidget(new QLabel("Degree Step", conjParamGroup), 4, 0);
    conjParamLayout->addWidget(conjUniqueDegreeCombo_, 4, 1);

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
    scanLayout->setSpacing(5);

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

    auto* scanWeightsSection = new CollapsibleSection("Combined Weights (advanced)", false, transitScanPanel);
    QWidget* scanWeightsGroup = scanWeightsSection->contentWidget();
    auto* scanWeightsLayout = new QGridLayout();
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
    scanWeightsSection->setContentLayout(scanWeightsLayout);

    auto* scanScoringSection = new CollapsibleSection("Scoring (advanced)", false, transitScanPanel);
    QWidget* scanScoringGroup = scanScoringSection->contentWidget();
    auto* scanScoringLayout = new QGridLayout();
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
    scanScoringSection->setContentLayout(scanScoringLayout);

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
    scanLayout->addWidget(scanWeightsSection);
    scanLayout->addWidget(scanScoringSection);
    scanLayout->addWidget(scanTimeGroup);
    scanLayout->addWidget(scanRunGroup);
    scanLayout->addWidget(scanResultsGroup);
    scanLayout->addStretch();

    transitProfectionPanel_ = new QWidget(transitPanelStack_);
    auto* profectionLayout = new QVBoxLayout(transitProfectionPanel_);
    profectionLayout->setContentsMargins(0, 0, 0, 0);
    profectionLayout->setSpacing(5);

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
    lunationLayout->setSpacing(5);

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
    lunationEclipseRuleCombo_ = new QComboBox(lunationModeGroup);
    lunationEclipseRuleCombo_->addItem("Astronomical (Swiss)", static_cast<int>(LunationEclipseRule::AstronomicalSwiss));
    lunationEclipseRuleCombo_->addItem("Strict Vedic (whole-sign nodes)", static_cast<int>(LunationEclipseRule::StrictVedicWholeSign));
    lunationEclipseRuleCombo_->setToolTip("Choose how Solar/Lunar Eclipse events are classified in sidereal mode.");

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
    lunationModeLayout->addWidget(new QLabel("Eclipse Rule", lunationModeGroup), 1, 0);
    lunationModeLayout->addWidget(lunationEclipseRuleCombo_, 1, 1, 1, 2);
    lunationModeLayout->addWidget(new QLabel("Start Year", lunationModeGroup), 2, 0);
    lunationModeLayout->addWidget(lunationStartYearSpin_, 2, 1);
    lunationModeLayout->addWidget(new QLabel("End Year", lunationModeGroup), 3, 0);
    lunationModeLayout->addWidget(lunationEndYearSpin_, 3, 1);
    lunationModeLayout->addWidget(lunationDegreeRangeCheck_, 4, 0, 1, 3);
    lunationModeLayout->addWidget(lunationDegreeRangeRow, 5, 1, 1, 2);
    lunationModeLayout->addWidget(new QLabel("Timezone", lunationModeGroup), 6, 0);
    lunationModeLayout->addWidget(lunationTimezoneLabel_, 6, 1, 1, 2);
    lunationModeLayout->addWidget(lunationRefLabel, 7, 0, 1, 3);

    auto* lunationAnalysisSection = new CollapsibleSection("Degree Analysis (advanced)", false, transitLunationPanel_);
    QWidget* lunationAnalysisGroup = lunationAnalysisSection->contentWidget();
    auto* lunationAnalysisLayout = new QGridLayout();
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
    lunationSignModeCombo_->setToolTip("Sign filter applies Moon sign of each lunation event.");
    lunationSignCombo_ = new QComboBox(lunationAnalysisGroup);
    lunationSignCombo_->setEditable(true);
    lunationSignCombo_->setToolTip("Sign filter applies Moon sign of each lunation event.");
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
        item->setData((!isLunarNodeName(name) || name.startsWith("Mean "))
                          ? Qt::Checked : Qt::Unchecked,
                      Qt::CheckStateRole);
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
    lunationAnalysisSection->setContentLayout(lunationAnalysisLayout);

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

    auto* lunationConjGroup = new QGroupBox("Planet Involvement (optional)", transitLunationPanel_);
    auto* lunationConjLayout = new QGridLayout(lunationConjGroup);
    lunationConjLayout->setHorizontalSpacing(8);
    lunationConjLayout->setVerticalSpacing(6);
    lunationConjLayout->setColumnStretch(1, 1);
    lunationPlanetConjCheck_ = new QCheckBox("Require planet conjunction", lunationConjGroup);
    lunationPlanetConjCheck_->setToolTip(
        "Only keep lunations where the chosen planet is conjunct one of the selected points "
        "(Sun / Moon / Nodes) within the orb.");
    lunationPlanetCombo_ = new QComboBox(lunationConjGroup);
    for (const auto& bodyName : solarPlacementFinderPlanetOrder()) {
        lunationPlanetCombo_->addItem(bodyName);
    }
    lunationPlanetCombo_->setCurrentText("Saturn");
    lunationConjSunCheck_ = new QCheckBox("Sun", lunationConjGroup);
    lunationConjMoonCheck_ = new QCheckBox("Moon", lunationConjGroup);
    lunationConjNorthNodeCheck_ = new QCheckBox("North Node", lunationConjGroup);
    lunationConjSouthNodeCheck_ = new QCheckBox("South Node", lunationConjGroup);
    lunationConjSunCheck_->setChecked(true);
    lunationConjMoonCheck_->setChecked(true);
    lunationConjOrbSpin_ = new QDoubleSpinBox(lunationConjGroup);
    lunationConjOrbSpin_->setRange(0.1, 15.0);
    lunationConjOrbSpin_->setDecimals(2);
    lunationConjOrbSpin_->setSingleStep(0.25);
    lunationConjOrbSpin_->setValue(3.0);
    lunationConjOrbSpin_->setSuffix(QString(QChar(0x00B0)));
    auto* lunationConjTargetRow = new QWidget(lunationConjGroup);
    auto* lunationConjTargetLayout = new QHBoxLayout(lunationConjTargetRow);
    lunationConjTargetLayout->setContentsMargins(0, 0, 0, 0);
    lunationConjTargetLayout->setSpacing(8);
    lunationConjTargetLayout->addWidget(lunationConjSunCheck_);
    lunationConjTargetLayout->addWidget(lunationConjMoonCheck_);
    lunationConjTargetLayout->addWidget(lunationConjNorthNodeCheck_);
    lunationConjTargetLayout->addWidget(lunationConjSouthNodeCheck_);
    lunationConjTargetLayout->addStretch();
    lunationConjLayout->addWidget(lunationPlanetConjCheck_, 0, 0, 1, 2);
    lunationConjLayout->addWidget(new QLabel("Planet", lunationConjGroup), 1, 0);
    lunationConjLayout->addWidget(lunationPlanetCombo_, 1, 1);
    lunationConjLayout->addWidget(new QLabel("Conjunct", lunationConjGroup), 2, 0);
    lunationConjLayout->addWidget(lunationConjTargetRow, 2, 1);
    lunationConjLayout->addWidget(new QLabel("Orb", lunationConjGroup), 3, 0);
    lunationConjLayout->addWidget(lunationConjOrbSpin_, 3, 1);

    auto updateLunationConjEnabled = [this]() {
        const bool on = lunationPlanetConjCheck_ && lunationPlanetConjCheck_->isChecked();
        if (lunationPlanetCombo_) lunationPlanetCombo_->setEnabled(on);
        if (lunationConjSunCheck_) lunationConjSunCheck_->setEnabled(on);
        if (lunationConjMoonCheck_) lunationConjMoonCheck_->setEnabled(on);
        if (lunationConjNorthNodeCheck_) lunationConjNorthNodeCheck_->setEnabled(on);
        if (lunationConjSouthNodeCheck_) lunationConjSouthNodeCheck_->setEnabled(on);
        if (lunationConjOrbSpin_) lunationConjOrbSpin_->setEnabled(on);
    };
    connect(lunationPlanetConjCheck_, &QCheckBox::toggled, this, [updateLunationConjEnabled](bool) {
        updateLunationConjEnabled();
    });
    updateLunationConjEnabled();

    lunationLayout->addWidget(lunationEventGroup);
    lunationLayout->addWidget(lunationModeGroup);
    lunationLayout->addWidget(lunationConjGroup);
    lunationLayout->addWidget(lunationAnalysisSection);

    auto* lunationChartGroup = new QGroupBox("Chart", transitLunationPanel_);
    auto* lunationChartLayout = new QVBoxLayout(lunationChartGroup);
    lunationChartLayout->setContentsMargins(8, 6, 8, 6);
    lunationOverlayCheck_ = new QCheckBox("Overlay natal chart", lunationChartGroup);
    lunationOverlayCheck_->setChecked(true);
    lunationOverlayCheck_->setToolTip("Show the lunation moment overlaid on the natal chart (off = standalone moment chart).");
    lunationChartLayout->addWidget(lunationOverlayCheck_);
    lunationLayout->addWidget(lunationChartGroup);

    lunationLayout->addWidget(lunationRunGroup);
    lunationLayout->addStretch();

    transitPanelStack_->addWidget(transitOverviewPanel_);
    transitPanelStack_->addWidget(transitSearchPanel_);
    transitPanelStack_->addWidget(transitCalendarPanel_);
    transitPanelStack_->addWidget(transitConjunctionPanel_);
    transitPanelStack_->addWidget(transitScanPanel);
    transitAspectPeakPanel_ = createTransitAspectPeakPanel(transitPanelStack_);
    transitPanelStack_->addWidget(transitAspectPeakPanel_);
    transitPanelStack_->addWidget(transitProfectionPanel_);

    transitLayout->addWidget(transitPanelStack_);

// Astrocartography panel controls use the built-in OpenStreetMap tile widget.
    astrocartographyPanel_ = new QWidget(this);
    astrocartographyPanel_->setObjectName("dataPanel");
    auto* astroLayout = new QVBoxLayout(astrocartographyPanel_);
    astroLayout->setContentsMargins(6, 6, 6, 6);
    astroLayout->setSpacing(8);

    geodeticGroup_ = new QGroupBox("Astrocartography Lines", astrocartographyPanel_);
    auto* geodeticLayout = new QGridLayout(geodeticGroup_);
    geodeticLayout->setHorizontalSpacing(8);
    geodeticLayout->setVerticalSpacing(6);
    geodeticLayout->setColumnStretch(1, 1);

    astroSourceCombo_ = new QComboBox(geodeticGroup_);
    astroSourceCombo_->addItem("Natal chart", static_cast<int>(AstroSourceMode::Natal));
    astroSourceCombo_->addItem("Progressed chart - now", static_cast<int>(AstroSourceMode::ProgressedNow));
    astroSourceCombo_->addItem("Progressed chart - custom time", static_cast<int>(AstroSourceMode::ProgressedCustom));
    astroSourceCombo_->setToolTip("Chart source used for astrocartography lines and clicked-location relocation previews.");

    astroProgressionTargetRow_ = new QWidget(geodeticGroup_);
    auto* astroProgressionTargetLayout = new QGridLayout(astroProgressionTargetRow_);
    astroProgressionTargetLayout->setContentsMargins(0, 0, 0, 0);
    astroProgressionTargetLayout->setHorizontalSpacing(6);
    astroProgressionTargetLayout->setVerticalSpacing(4);
    astroProgressionTargetLayout->setColumnStretch(1, 1);
    astroProgressionDateEdit_ = new QDateEdit(astroProgressionTargetRow_);
    astroProgressionDateEdit_->setCalendarPopup(true);
    astroProgressionDateEdit_->setDisplayFormat("yyyy-MM-dd");
    astroProgressionTimeEdit_ = new QTimeEdit(astroProgressionTargetRow_);
    astroProgressionTimeEdit_->setDisplayFormat("hh:mm:ss AP");
    astroProgressionTimezoneEdit_ = new QLineEdit(astroProgressionTargetRow_);
    astroProgressionTimezoneEdit_->setPlaceholderText("Timezone");
    astroProgressionTimezoneStatus_ = new QLabel("OK", astroProgressionTargetRow_);
    astroProgressionTimezoneStatus_->setMinimumWidth(40);
    astroProgressionNowButton_ = new QPushButton("Now", astroProgressionTargetRow_);
    astroProgressionNowButton_->setToolTip("Fill the custom progression target with the current time.");
    const QDateTime astroNowLocal = QDateTime::currentDateTime();
    astroProgressionDateEdit_->setDate(astroNowLocal.date());
    astroProgressionTimeEdit_->setTime(astroNowLocal.time());
    const QByteArray astroTzId = QTimeZone::systemTimeZoneId();
    astroProgressionTimezoneEdit_->setText(astroTzId.isEmpty() ? "UTC" : QString::fromUtf8(astroTzId));
    astroProgressionTargetLayout->addWidget(astroProgressionDateEdit_, 0, 0);
    astroProgressionTargetLayout->addWidget(astroProgressionTimeEdit_, 0, 1);
    astroProgressionTargetLayout->addWidget(astroProgressionNowButton_, 0, 2);
    astroProgressionTargetLayout->addWidget(astroProgressionTimezoneEdit_, 1, 0, 1, 2);
    astroProgressionTargetLayout->addWidget(astroProgressionTimezoneStatus_, 1, 2);
    astroProgressionTargetLabel_ = new QLabel("Progression", geodeticGroup_);

    geodeticPlanetCombo_ = new QComboBox(geodeticGroup_);
    geodeticPlanetCombo_->setEditable(true);
    geodeticPlanetCombo_->setInsertPolicy(QComboBox::NoInsert);
    geodeticPlanetCombo_->setMinimumContentsLength(18);
    geodeticPlanetCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
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

    auto* angleRow = new QWidget(geodeticGroup_);
    auto* angleLayout = new QHBoxLayout(angleRow);
    angleLayout->setContentsMargins(0, 0, 0, 0);
    angleLayout->setSpacing(8);
    astroLineAcCheck_ = new QCheckBox("AC", angleRow);
    astroLineDcCheck_ = new QCheckBox("DC", angleRow);
    astroLineMcCheck_ = new QCheckBox("MC", angleRow);
    astroLineIcCheck_ = new QCheckBox("IC", angleRow);
    for (auto* check : {astroLineAcCheck_, astroLineDcCheck_, astroLineMcCheck_, astroLineIcCheck_}) {
        check->setChecked(true);
        angleLayout->addWidget(check);
    }
    angleLayout->addStretch();

    auto* astroAspectRow = new QWidget(geodeticGroup_);
    auto* astroAspectLayout = new QHBoxLayout(astroAspectRow);
    astroAspectLayout->setContentsMargins(0, 0, 0, 0);
    astroAspectLayout->setSpacing(8);
    astroHarmoniousAspectsCheck_ = new QCheckBox("w. harmonious aspects", astroAspectRow);
    astroDisharmoniousAspectsCheck_ = new QCheckBox("w. disharmonious aspects", astroAspectRow);
    astroHarmoniousAspectsCheck_->setToolTip("Show optional lines where source-chart planets form sextile or trine aspects to relocated angles.");
    astroDisharmoniousAspectsCheck_->setToolTip("Show optional lines where source-chart planets form square or opposition aspects to relocated angles.");
    astroHarmoniousAspectsCheck_->setChecked(false);
    astroDisharmoniousAspectsCheck_->setChecked(false);
    astroAspectLayout->addWidget(astroHarmoniousAspectsCheck_);
    astroAspectLayout->addWidget(astroDisharmoniousAspectsCheck_);
    astroAspectLayout->addStretch();

    astroClickedHouseCombo_ = new QComboBox(geodeticGroup_);
    astroClickedHouseCombo_->addItem("Whole Sign", static_cast<int>(HouseSystem::WholeSign));
    astroClickedHouseCombo_->addItem("Placidus", static_cast<int>(HouseSystem::Placidus));
    astroClickedHouseCombo_->setToolTip("House system used when recalculating the chart for a clicked map location.");

    geodeticTimeLabel_ = new QLabel("Using loaded chart", geodeticGroup_);
    geodeticTimeLabel_->setObjectName("hintLabel");
    geodeticTimeLabel_->setWordWrap(true);
    geodeticRefreshButton_ = new QPushButton("Refresh Map", geodeticGroup_);
    astroWorldButton_ = new QPushButton("World View", geodeticGroup_);
    astroBirthplaceButton_ = new QPushButton("Birthplace", geodeticGroup_);
    geodeticStatusLabel_ = new QLabel("Load a natal chart to draw lines.", geodeticGroup_);
    geodeticStatusLabel_->setObjectName("hintLabel");

    auto* mapButtonRow = new QWidget(geodeticGroup_);
    auto* mapButtonLayout = new QHBoxLayout(mapButtonRow);
    mapButtonLayout->setContentsMargins(0, 0, 0, 0);
    mapButtonLayout->setSpacing(6);
    mapButtonLayout->addWidget(geodeticRefreshButton_);
    mapButtonLayout->addWidget(astroWorldButton_);
    mapButtonLayout->addWidget(astroBirthplaceButton_);
    mapButtonLayout->addStretch();

    geodeticLayout->addWidget(new QLabel("Source", geodeticGroup_), 0, 0);
    geodeticLayout->addWidget(astroSourceCombo_, 0, 1);
    geodeticLayout->addWidget(astroProgressionTargetLabel_, 1, 0);
    geodeticLayout->addWidget(astroProgressionTargetRow_, 1, 1);
    geodeticLayout->addWidget(new QLabel("Planets", geodeticGroup_), 2, 0);
    geodeticLayout->addWidget(geodeticPlanetCombo_, 2, 1);
    geodeticLayout->addWidget(new QLabel("Lines", geodeticGroup_), 3, 0);
    geodeticLayout->addWidget(angleRow, 3, 1);
    geodeticLayout->addWidget(new QLabel("Aspects", geodeticGroup_), 4, 0);
    geodeticLayout->addWidget(astroAspectRow, 4, 1);
    geodeticLayout->addWidget(new QLabel("Clicked chart", geodeticGroup_), 5, 0);
    geodeticLayout->addWidget(astroClickedHouseCombo_, 5, 1);
    geodeticLayout->addWidget(geodeticTimeLabel_, 6, 0, 1, 2);
    geodeticLayout->addWidget(mapButtonRow, 7, 0, 1, 2);
    geodeticLayout->addWidget(geodeticStatusLabel_, 8, 0, 1, 2);

    astroLayout->addWidget(geodeticGroup_);

    astroPreviewGroup_ = new QGroupBox("Relocation Preview", astrocartographyPanel_);
    auto* astroPreviewLayout = new QVBoxLayout(astroPreviewGroup_);
    astroPreviewLayout->setContentsMargins(6, 6, 6, 6);
    astroPreviewLayout->setSpacing(4);
    astroPreviewWheel_ = new ChartWheelWidget(astroPreviewGroup_);
    astroPreviewWheel_->setObjectName("astroPreviewWheel");
    astroPreviewWheel_->setMinimumSize(220, 220);
    astroPreviewWheel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    astroPreviewWheel_->setShowAspects(false);
    astroPreviewWheel_->setShowLots(false);
    astroPreviewWheel_->setShowDerivedPoints(false);
    astroPreviewWheel_->setShowFixedStars(false);
    astroPreviewWheel_->setShowAsteroids(false);
    astroPreviewWheel_->setTickDensity(ChartWheelWidget::TickDensity::Minimal);
    astroPreviewWheel_->setFontScale(0.72);
    astroPreviewWheel_->setZoom(0.94);
    astroPreviewWheel_->setTheme(buildChartTheme(theme_));
    astroPreviewWheel_->clearChart();
    astroPreviewStatusLabel_ = new QLabel("Click a map point to preview its relocation chart.", astroPreviewGroup_);
    astroPreviewStatusLabel_->setObjectName("hintLabel");
    astroPreviewStatusLabel_->setWordWrap(true);
    astroPreviewLayout->addWidget(astroPreviewWheel_, 1);
    astroPreviewLayout->addWidget(astroPreviewStatusLabel_, 0);
    astroPreviewLayout->setStretch(0, 1);
    astroPreviewLayout->setStretch(1, 0);

    astroLayout->addWidget(astroPreviewGroup_, 1);
    astroLayout->addStretch(0);

    dataStack_ = new QStackedWidget(this);
    dataStack_->addWidget(dataPanel);
    // Transit controls are a tall stack of filter groups; wrap them in a scroll
    // area so nothing clips on a laptop screen (keeps every control reachable).
    auto* transitScroll = new QScrollArea(this);
    transitScroll->setWidgetResizable(true);
    transitScroll->setFrameShape(QFrame::NoFrame);
    transitScroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    transitScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    transitScroll->setWidget(transitPanel_);
    if (auto* horizontalBar = transitScroll->horizontalScrollBar()) {
        horizontalBar->setValue(0);
        connect(horizontalBar, &QScrollBar::valueChanged, transitScroll,
                [horizontalBar](int value) {
            if (value != 0) horizontalBar->setValue(0);
        });
    }
    dataStack_->addWidget(transitScroll);

    // Lunations is its own main tab; wrap its control panel in a scroll area and
    // register it as a dedicated data-stack page (index captured for switching).
    auto* lunationsScroll = new QScrollArea(this);
    lunationsScroll->setWidgetResizable(true);
    lunationsScroll->setFrameShape(QFrame::NoFrame);
    lunationsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lunationsScroll->setWidget(transitLunationPanel_);
    lunationsPanel_ = lunationsScroll;
    lunationsDataStackIndex_ = dataStack_->addWidget(lunationsScroll);
    astrocartographyDataStackIndex_ = dataStack_->addWidget(astrocartographyPanel_);
    if (returnFinderController_) {
        returnFinderDataStackIndex_ = dataStack_->addWidget(returnFinderController_->filtersWidget());
    }
    if (planetaryHoursController_) {
        planetaryHoursDataStackIndex_ = dataStack_->addWidget(planetaryHoursController_->filtersWidget());
    }
    if (zodiacalReleasingController_) {
        zodiacalReleasingDataStackIndex_ = dataStack_->addWidget(
            zodiacalReleasingController_->filtersWidget());
    }
    if (geodeticEquivalentsController_) {
        geodeticEquivalentsDataStackIndex_ = dataStack_->addWidget(
            geodeticEquivalentsController_->filtersWidget());
    }

    aspectsPanel_ = new QFrame(this);
    aspectsPanel_->setObjectName("aspectsPanel");
    auto* aspectsLayout = new QVBoxLayout(aspectsPanel_);
    aspectsLayout->setContentsMargins(6, 6, 6, 6);
    aspectsLayout->setSpacing(4);
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
    aspectsHeaderLayout->setSpacing(4);
    auto* aspectsTitle = new QLabel("Aspect Grid", aspectsHeader);
    aspectsTitle->setObjectName("sectionTitle");
    aspectGridSettingsButton_ = new QToolButton(aspectsHeader);
    aspectGridSettingsButton_->setText(QString(QChar(0x2699)));
    aspectGridSettingsButton_->setToolTip("Aspect grid body visibility");
    aspectGridSettingsButton_->setAutoRaise(false);
    aspectsCopyButton_ = new QPushButton("Copy Aspects", aspectsHeader);
    aspectsHeaderLayout->addWidget(aspectsTitle);
    aspectsHeaderLayout->addWidget(aspectScopeTabs_);
    aspectsHeaderLayout->addStretch();
    aspectsHeaderLayout->addWidget(aspectGridSettingsButton_);
    aspectsHeaderLayout->addWidget(aspectsCopyButton_);
    aspectsLayout->addWidget(aspectsHeader);
    aspectsTable_->setObjectName("aspectsTable");
    aspectsTable_->setFrameShape(QFrame::NoFrame);
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
    aspectsPanel_->setMinimumWidth(260);

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
    rightTopLayout->setSpacing(6);
    transitListFilterPanel_ = new QWidget(rightTopPanel);
    auto* transitListFilterLayout = new QHBoxLayout(transitListFilterPanel_);
    transitListFilterLayout->setContentsMargins(0, 0, 0, 0);
    transitListFilterLayout->setSpacing(6);
    transitListFilterCombo_ = new QComboBox(transitListFilterPanel_);
    transitListFilterCombo_->addItem("Main Planets", 0);
    transitListFilterCombo_->addItem("Lunar Nodes", 4);
    transitListFilterCombo_->addItem("Planets + Points (No Lots)", 1);
    transitListFilterCombo_->addItem("Arabic Lots Only", 2);
    transitListFilterCombo_->addItem("Everything", 3);
    transitListFilterCombo_->setToolTip(
        "Choose which bodies appear in the Transits Overview list. "
        "Lunar Nodes shows their exact position and daily motion. "
        "This does not change the chart wheel.");
    transitListFilterLayout->addWidget(new QLabel("Show", transitListFilterPanel_));
    transitListFilterLayout->addWidget(transitListFilterCombo_, 1);
    transitListFilterPanel_->setVisible(false);
    rightTopLayout->addWidget(transitListFilterPanel_);
    rightTopLayout->addWidget(rightTopTable_);

    rightTopDock_ = new QDockWidget("Transits", this);
    rightTopDock_->setObjectName("dock_right_top");
    rightTopDock_->setWidget(rightTopPanel);
    rightTopDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::RightDockWidgetArea, rightTopDock_);

    transitAspectsTable_ = new QTableWidget(this);
    auto* transitAspectsPanel = new QFrame(this);
    transitAspectsPanel->setObjectName("dataPanel");
    auto* transitAspectsLayout = new QVBoxLayout(transitAspectsPanel);
    transitAspectsLayout->setContentsMargins(6, 6, 6, 6);
    transitAspectsLayout->setSpacing(6);
    auto* transitAspectsHeader = new QWidget(transitAspectsPanel);
    auto* transitAspectsHeaderLayout = new QHBoxLayout(transitAspectsHeader);
    transitAspectsHeaderLayout->setContentsMargins(0, 0, 0, 0);
    transitAspectsHeaderLayout->setSpacing(6);
    transitAspectsCountLabel_ = new QLabel("No current calculation", transitAspectsHeader);
    transitAspectsCountLabel_->setObjectName("hintLabel");
    transitAspectsCopyButton_ = new QPushButton("Copy", transitAspectsHeader);
    transitAspectsCopyButton_->setEnabled(false);
    transitAspectsHeaderLayout->addWidget(transitAspectsCountLabel_, 1);
    transitAspectsHeaderLayout->addWidget(transitAspectsCopyButton_);
    transitAspectsLayout->addWidget(transitAspectsHeader);
    transitAspectsLayout->addWidget(transitAspectsTable_, 1);

    transitAspectsDock_ = new QDockWidget("Aspects in Effect", this);
    transitAspectsDock_->setObjectName("dock_transit_aspects");
    transitAspectsDock_->setWidget(transitAspectsPanel);
    transitAspectsDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::RightDockWidgetArea, transitAspectsDock_);

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
    splitDockWidget(rightTopDock_, transitAspectsDock_, Qt::Vertical);
    splitDockWidget(transitAspectsDock_, rightBottomDock_, Qt::Vertical);
    transitAspectsDock_->setVisible(false);

    const auto dockFeatures = QDockWidget::DockWidgetMovable
        | QDockWidget::DockWidgetFloatable
        | QDockWidget::DockWidgetClosable;
    dataDock_->setFeatures(dockFeatures);
    rightTopDock_->setFeatures(dockFeatures);
    transitAspectsDock_->setFeatures(dockFeatures);
    rightBottomDock_->setFeatures(dockFeatures);

    resizeDocks({rightTopDock_, transitAspectsDock_, rightBottomDock_}, {380, 260, 260}, Qt::Vertical);
    resizeDocks({dataDock_, rightTopDock_}, {420, 300}, Qt::Horizontal);

    defaultDockState_ = saveState();
    refreshProfileToolbar();
    syncZodiacToolbarControls();

    updateTransitLocationAvailability();
    updateTransitTimezoneStatus();
    markTransitPending();
    updateAstrocartographyModeUi();
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu("&File");
    newChartAction_ = fileMenu->addAction("New Chart...");
    openChartAction_ = fileMenu->addAction("Load Chart...");
    manageChartsAction_ = fileMenu->addAction("Chart Manager...");
    saveChartAction_ = fileMenu->addAction("Save Chart");
    fileMenu->addSeparator();
    editChartAction_ = fileMenu->addAction("Edit Current Chart...");
    fileMenu->addSeparator();
    deleteChartAction_ = fileMenu->addAction("Delete Chart...");
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction("Exit");

    newChartAction_->setShortcut(QKeySequence::New);
    openChartAction_->setShortcut(QKeySequence::Open);
    saveChartAction_->setShortcut(QKeySequence::Save);

    connect(newChartAction_, &QAction::triggered, this, &MainWindow::handleNewChart);
    connect(openChartAction_, &QAction::triggered, this, &MainWindow::handleLoadProfile);
    connect(manageChartsAction_, &QAction::triggered, this, &MainWindow::showChartManager);
    connect(saveChartAction_, &QAction::triggered, this, &MainWindow::handleSaveProfile);
    connect(editChartAction_, &QAction::triggered, this, &MainWindow::handleEditChart);
    connect(deleteChartAction_, &QAction::triggered, this, &MainWindow::handleDeleteProfile);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    refreshProfileToolbar();

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
        if (hasCurrentChart_ && currentInput_.houseSystem != system) {
            auto input = currentInput_;
            input.houseSystem = system;
            if (computeChart(input, currentLocation_)) {
                setCurrentChartModified(true);
            }
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

    auto* settingsMenu = menuBar()->addMenu("&Settings");
    auto* preferencesAction = settingsMenu->addAction("Preferences...");
    preferencesAction->setShortcut(QKeySequence("Ctrl+,"));
    connect(preferencesAction, &QAction::triggered, this, &MainWindow::handlePreferences);

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
    themeCremeAction_ = themeMenu->addAction("Creme");
    themeDarkAction_ = themeMenu->addAction("Dark");
    themeLightAction_->setCheckable(true);
    themeCremeAction_->setCheckable(true);
    themeDarkAction_->setCheckable(true);
    themeGroup->addAction(themeLightAction_);
    themeGroup->addAction(themeCremeAction_);
    themeGroup->addAction(themeDarkAction_);
    connect(themeLightAction_, &QAction::triggered, this, [this]() {
        applyTheme(ThemeMode::Light);
        saveUiState();
    });
    connect(themeCremeAction_, &QAction::triggered, this, [this]() {
        applyTheme(ThemeMode::Creme);
        saveUiState();
    });
    connect(themeDarkAction_, &QAction::triggered, this, [this]() {
        applyTheme(ThemeMode::Dark);
        saveUiState();
    });

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* aboutAction = helpMenu->addAction("About");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "About DracoVed", "DracoVed C++ Prototype\nNatal + Transit Workstation");
    });
}

QString MainWindow::buildStyleSheet(ThemeMode mode) const {
    if (mode == ThemeMode::Creme) {
        // Palette:
        //   base bg:      #f5efe6  warm parchment
        //   surface:      #ede3d6  slightly deeper creme panel
        //   panel border: #d6c9b6  warm taupe border
        //   header bg:    #e6d9c8  warm header strip
        //   text:         #3a2e22  deep warm brown
        //   accent:       #8b5e3c  warm terracotta / sienna
        //   hover accent: #a0714f
        //   input bg:     #faf4ec  near-white warm
        //   selection:    #ddd0bc
        //   grid lines:   #cfc0a8
        return
            "QMainWindow { background-color: #f5efe6; color: #3a2e22; }"
            "QDialog { background-color: #f5efe6; color: #3a2e22; }"
            "QWidget { color: #3a2e22; }"
            "QMenuBar { background-color: #ede3d6; color: #3a2e22; border-bottom: 1px solid #d6c9b6; }"
            "QMenuBar::item:selected { background-color: #ddd0bc; }"
            "QLineEdit, QDateEdit, QTimeEdit, QComboBox, QDoubleSpinBox, QSpinBox {"
            "  background-color: #faf4ec; border: 1px solid #c9b89e; padding: 2px 6px; border-radius: 3px; color: #3a2e22;"
            "}"
            "QDateEdit, QTimeEdit, QDoubleSpinBox, QSpinBox { padding-right: 20px; }"
            "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {"
            "  subcontrol-origin: border; width: 16px; border-left: 1px solid #c9b89e; background-color: #ede3d6;"
            "}"
            "QAbstractSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 3px; }"
            "QAbstractSpinBox::down-button { subcontrol-position: bottom right; border-top: 1px solid #c9b89e; border-bottom-right-radius: 3px; }"
            "QAbstractSpinBox::up-arrow, QAbstractSpinBox::down-arrow { width: 8px; height: 8px; }"
            "QLineEdit:focus, QDateEdit:focus, QTimeEdit:focus, QComboBox:focus, QDoubleSpinBox:focus, QSpinBox:focus {"
            "  border: 1px solid #8b5e3c;"
            "}"
            "QAbstractItemView {"
            "  background-color: #faf4ec; color: #3a2e22; selection-background-color: #e3d4bf; selection-color: #2a2016;"
            "  outline: none;"
            "}"
            "QAbstractItemView::item { padding: 3px 7px; border: none; }"
            "QAbstractItemView::item:hover { background-color: #efe6d6; }"
            "QTableView::item:hover { background: transparent; }"
            "QAbstractItemView::item:selected { background-color: #e3d4bf; color: #2a2016; }"
            "QPushButton { background-color: #ede3d6; border: 1px solid #c9b89e; padding: 4px 10px; border-radius: 3px; color: #3a2e22; }"
            "QPushButton:hover { border: 1px solid #8b5e3c; background-color: #e4d8c8; }"
            "QPushButton:pressed { background-color: #d8cab5; }"
            "QToolButton { background-color: #ede3d6; border: 1px solid #c9b89e; padding: 2px 6px; border-radius: 3px; color: #3a2e22; }"
            "QToolButton:hover { border: 1px solid #8b5e3c; background-color: #e4d8c8; }"
            "QToolButton:pressed { background-color: #d8cab5; }"
            "QDockWidget { background-color: #ede3d6; color: #3a2e22; }"
            "QDockWidget::title { background-color: #e6d9c8; border: 1px solid #d6c9b6; padding: 4px 8px; color: #3a2e22; }"
            "QFrame#profileQuickBar { background-color: #f0e8d8; border: 1px solid #d6c9b6; border-radius: 6px; }"
            "QLabel#profileQuickState { color: #7a6550; border: none; }"
            "QFrame#profileQuickBar QToolButton { padding: 3px 10px; border-radius: 4px; }"
            "QFrame#profileQuickBar QToolButton:hover { background-color: #e4d8c8; border: 1px solid #a0714f; }"
            "QFrame#profileQuickBar QToolButton:pressed { background-color: #d8cab5; }"
            "QFrame#profileQuickBar QToolButton:disabled { color: #b0a090; border-color: #d6c9b6; }"
            "QFrame#aspectOrbQuickPanel { background-color: #faf4ec; border: 1px solid #d6c9b6; border-radius: 6px; }"
            "QFrame#aspectOrbQuickPanel QToolButton:checked { background-color: #d8cab5; border: 1px solid #8b5e3c; font-weight: 600; }"
            "QFrame#dataPanel, QFrame#aspectsPanel, QWidget#chartPlaceholder {"
            "  background-color: #faf4ec; border: 1px solid #d6c9b6; border-radius: 6px;"
            "}"
            "QHeaderView::section { background-color: #ede3d6; color: #6b5240; border: none; border-bottom: 1px solid #cdbb9f; padding: 4px 7px; font-weight: 600; }"
            "QTableWidget { background-color: #faf4ec; alternate-background-color: #faf4ec; gridline-color: #e8dec9; color: #3a2e22; }"
            "QTableWidget#aspectsTable QHeaderView::section { background-color: #dfd0bc; color: #3a2e22; font-weight: 600; padding: 2px 5px; border: 1px solid #c9b89e; }"
            "QTabWidget::pane { border: 1px solid #d6c9b6; top: -1px; }"
            "QTabBar::tab { background: #e6d9c8; padding: 6px 10px; border: 1px solid #d6c9b6; border-bottom: none; color: #3a2e22; }"
            "QTabBar::tab:selected { background: #faf4ec; border-color: #8b5e3c; }"
            "QTabBar::tab:hover { background: #ede3d6; }"
            "QMenu { background-color: #faf4ec; color: #3a2e22; border: 1px solid #c9b89e; }"
            "QMenu::item { padding: 6px 24px 6px 24px; }"
            "QMenu::item:selected { background-color: #ddd0bc; }"
            "QMenu::separator { height: 1px; background: #d6c9b6; margin: 4px 8px; }"
            "QMenu::item:disabled { color: #b0a090; }"
            "QGroupBox { border: 1px solid #e2d6c2; border-radius: 5px; margin-top: 9px; padding-top: 4px; color: #3a2e22; }"
            "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 8px; padding: 0 4px; color: #8a6f54; }"
            "QRadioButton { spacing: 8px; color: #3a2e22; }"
            "QCheckBox { color: #3a2e22; }"
            "QLabel { color: #3a2e22; }"
            "QSplitter::handle { background-color: #d6c9b6; }"
            "QScrollBar:vertical { background: #ede3d6; width: 10px; }"
            "QScrollBar::handle:vertical { background: #c9b89e; border-radius: 5px; min-height: 20px; }"
            "QScrollBar::handle:vertical:hover { background: #a0714f; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
            "QScrollBar:horizontal { background: #ede3d6; height: 10px; }"
            "QScrollBar::handle:horizontal { background: #c9b89e; border-radius: 5px; min-width: 20px; }"
            "QScrollBar::handle:horizontal:hover { background: #a0714f; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
            "QProgressBar { background-color: #ede3d6; border: 1px solid #c9b89e; border-radius: 3px; }"
            "QProgressBar::chunk { background-color: #8b5e3c; border-radius: 3px; }"
            "QLabel#hintLabel { color: #7a6550; }";
    }
    if (mode == ThemeMode::Dark) {
        return
            "QMainWindow { background-color: #0b0c0d; color: #e2e2e2; }"
            "QDialog { background-color: #0b0c0d; color: #e2e2e2; }"
            "QWidget { color: #e2e2e2; }"
            "QMenuBar { background-color: #0f1112; color: #e2e2e2; }"
            "QMenuBar::item:selected { background-color: #1f2326; }"
            "QLineEdit, QDateEdit, QTimeEdit, QComboBox, QDoubleSpinBox {"
            "  background-color: #141618; border: 1px solid #2a2d30; padding: 2px 6px; border-radius: 3px;"
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
            "  background-color: #141618; color: #e2e2e2; selection-background-color: #243038; selection-color: #ffffff;"
            "  outline: none;"
            "}"
            "QAbstractItemView::item { padding: 3px 7px; border: none; }"
            "QAbstractItemView::item:hover { background-color: #1c2024; }"
            "QTableView::item:hover { background: transparent; }"
            "QAbstractItemView::item:selected { background-color: #243038; color: #ffffff; }"
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
            "QFrame#aspectOrbQuickPanel { background-color: #141618; border: 1px solid #2a2d30; border-radius: 6px; }"
            "QFrame#aspectOrbQuickPanel QToolButton:checked { background-color: #30373d; border: 1px solid #b14040; font-weight: 600; }"
            "QFrame#dataPanel, QFrame#aspectsPanel, QWidget#chartPlaceholder {"
            "  background-color: #0f1112; border: 1px solid #202326; border-radius: 6px;"
            "}"
            "QHeaderView::section { background-color: #15181b; color: #9aa3a8; border: none; border-bottom: 1px solid #262b2f; padding: 4px 7px; font-weight: 600; }"
            "QTableWidget { background-color: #0f1112; alternate-background-color: #0f1112; gridline-color: #1c1f22; color: #e2e2e2; }"
            "QTableWidget#aspectsTable QHeaderView::section { background-color: #161a1d; color: #c8cdd1; font-weight: 600; padding: 2px 5px; border: 1px solid #252a2e; }"
            "QTabWidget::pane { border: 1px solid #202326; top: -1px; }"
            "QTabBar::tab { background: #141618; padding: 6px 10px; border: 1px solid #202326; border-bottom: none; }"
            "QTabBar::tab:selected { background: #1b1f22; border-color: #b14040; }"
            "QMenu { background-color: #141618; color: #e2e2e2; border: 1px solid #2a2d30; }"
            "QMenu::item { padding: 6px 24px 6px 24px; }"
            "QMenu::item:selected { background-color: #1f2326; }"
            "QMenu::separator { height: 1px; background: #2a2d30; margin: 4px 8px; }"
            "QMenu::item:disabled { color: #5a5f63; }"
            "QGroupBox { border: 1px solid #242a2e; border-radius: 5px; margin-top: 9px; padding-top: 4px; }"
            "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 8px; padding: 0 4px; color: #8b949b; }"
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
        "  background-color: #ffffff; border: 1px solid #c9c9c9; padding: 2px 6px; border-radius: 3px;"
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
        "  background-color: #ffffff; color: #1b1b1b; selection-background-color: #dce9f5; selection-color: #11304a;"
        "  outline: none;"
        "}"
        "QAbstractItemView::item { padding: 3px 7px; border: none; }"
        "QAbstractItemView::item:hover { background-color: #f0f3f6; }"
        "QTableView::item:hover { background: transparent; }"
        "QAbstractItemView::item:selected { background-color: #dce9f5; color: #11304a; }"
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
        "QFrame#aspectOrbQuickPanel { background-color: #ffffff; border: 1px solid #d6d6d6; border-radius: 6px; }"
        "QFrame#aspectOrbQuickPanel QToolButton:checked { background-color: #dce9f5; border: 1px solid #4e82ad; font-weight: 600; }"
        "QFrame#dataPanel, QFrame#aspectsPanel, QWidget#chartPlaceholder {"
        "  background-color: #ffffff; border: 1px solid #d6d6d6; border-radius: 6px;"
        "}"
        "QHeaderView::section { background-color: #f5f6f8; color: #5a6470; border: none; border-bottom: 1px solid #dfe1e4; padding: 4px 7px; font-weight: 600; }"
        "QTableWidget { background-color: #ffffff; alternate-background-color: #ffffff; gridline-color: #ededed; color: #1b1b1b; }"
        "QTableWidget#aspectsTable QHeaderView::section { background-color: #ececec; color: #1b1b1b; font-weight: 600; padding: 2px 5px; border: 1px solid #d0d0d0; }"
        "QTabWidget::pane { border: 1px solid #d6d6d6; top: -1px; }"
        "QTabBar::tab { background: #f1f1f1; padding: 6px 10px; border: 1px solid #d6d6d6; border-bottom: none; }"
        "QTabBar::tab:selected { background: #ffffff; border-color: #b14040; }"
        "QMenu { background-color: #ffffff; color: #1b1b1b; border: 1px solid #c9c9c9; }"
        "QMenu::item { padding: 6px 24px 6px 24px; }"
        "QMenu::item:selected { background-color: #e6e6e6; }"
        "QMenu::separator { height: 1px; background: #d6d6d6; margin: 4px 8px; }"
        "QMenu::item:disabled { color: #8a8a8a; }"
        "QGroupBox { border: 1px solid #e2e4e7; border-radius: 5px; margin-top: 9px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 8px; padding: 0 4px; color: #6b7480; }"
        "QRadioButton { spacing: 8px; }"
        "QLabel#hintLabel { color: #6f6f6f; }";
}

ChartWheelTheme MainWindow::buildChartTheme(ThemeMode mode) const {
    if (mode == ThemeMode::Creme) {
        return ChartWheelTheme{
            QColor("#f5efe6"),          // background — warm parchment
            QColor("#d6c9b6"),          // ringOuter — taupe border
            QColor("#ede3d6"),          // ringZodiac — creme ring
            QColor("#e6d9c8"),          // ringHouseOuter
            QColor("#ede3d6"),          // ringHouseInner
            QColor("#7a6550"),          // placeholderText — warm grey-brown
            QColor("#c9b89e"),          // tick — warm tan
            QColor("#c4b09a"),          // signBoundary
            QColor("#5a3e28"),          // signGlyph — deep warm brown
            QColor("#b8a48a"),          // houseLine — muted tan
            QColor("#6b5240"),          // houseLabel — medium brown
            QColor("#ede3d6"),          // transitRing
            QColor(245, 239, 230, 210), // aspectSymbolBg — warm parchment translucent
            QColor("#b0a090"),          // aspectLineNeutral — muted warm grey
            QColor("#8b9eb5"),          // aspectLineTransitTransit — muted slate blue
            QColor("#a08898"),          // aspectLineNatalNatal — muted mauve
            QColor("#3a2e22"),          // body — deep warm brown text
            QColor("#4a3828"),          // natalBody
            QColor("#5c3a20"),          // transitBody — warm sienna
            QColor("#fde8d0"),          // elementFire — soft peach
            QColor("#e8f0e0"),          // elementEarth — soft sage
            QColor("#ddeaf5"),          // elementAir — soft sky
            QColor("#ecddf5"),          // elementWater — soft lavender
            QColor("#d6c9b6"),          // aspectInnerCircle
            QColor("#b14040"),          // retrogradeIndicator — keep red
            QColor("#8b5e3c"),          // angularHouseLabel — terracotta
            QColor("#f1e8d8"),          // transitLaneBand — soft warm tint
        };
    }
    if (mode == ThemeMode::Dark) {
        return ChartWheelTheme{
            QColor("#0f1112"),          // background
            QColor("#202326"),          // ringOuter
            QColor("#1f2225"),          // ringZodiac
            QColor("#24282b"),          // ringHouseOuter
            QColor("#1b1f22"),          // ringHouseInner
            QColor("#6a6f73"),          // placeholderText
            QColor("#2b2f33"),          // tick
            QColor("#2f3337"),          // signBoundary
            QColor("#b14040"),          // signGlyph
            QColor("#3a3f44"),          // houseLine
            QColor("#9aa0a6"),          // houseLabel
            QColor("#23272b"),          // transitRing
            QColor(11, 12, 13, 200),    // aspectSymbolBg
            QColor("#c0c0c0"),          // aspectLineNeutral
            QColor("#98a9bf"),          // aspectLineTransitTransit
            QColor("#7c7f84"),          // aspectLineNatalNatal
            QColor("#e6e6e6"),          // body
            QColor("#cfd3d6"),          // natalBody
            QColor("#e6e6e6"),          // transitBody
            QColor("#2a1a0a"),          // elementFire — dark ember
            QColor("#0f1f10"),          // elementEarth — dark forest
            QColor("#0a1525"),          // elementAir — dark navy
            QColor("#1a0a25"),          // elementWater — dark indigo
            QColor("#2b2f33"),          // aspectInnerCircle
            QColor("#b14040"),          // retrogradeIndicator
            QColor("#9aa0a6"),          // angularHouseLabel
            QColor("#191d21"),          // transitLaneBand — faint raised panel
        };
    }
    // Light
    return ChartWheelTheme{
        QColor("#ffffff"),          // background
        QColor("#d7d7d7"),          // ringOuter
        QColor("#e2e2e2"),          // ringZodiac
        QColor("#dedede"),          // ringHouseOuter
        QColor("#e8e8e8"),          // ringHouseInner
        QColor("#8a8a8a"),          // placeholderText
        QColor("#d0d0d0"),          // tick
        QColor("#cfcfcf"),          // signBoundary
        QColor("#5c6470"),          // signGlyph — muted slate (red looked harsh in light)
        QColor("#c0c0c0"),          // houseLine
        QColor("#6f6f6f"),          // houseLabel
        QColor("#d0d0d0"),          // transitRing
        QColor(255, 255, 255, 235), // aspectSymbolBg — pure white (no grey shadow on white bg)
        QColor("#6f7378"),          // aspectLineNeutral
        QColor("#6a7a90"),          // aspectLineTransitTransit
        QColor("#7a7e83"),          // aspectLineNatalNatal
        QColor("#2b2b2b"),          // body
        QColor("#2b2b2b"),          // natalBody
        QColor("#1f1f1f"),          // transitBody
        QColor("#fbf3ec"),          // elementFire — barely-there peach
        QColor("#eef4ed"),          // elementEarth — barely-there sage
        QColor("#ebf1f7"),          // elementAir — barely-there sky
        QColor("#f2edf6"),          // elementWater — barely-there lavender
        QColor("#d0d0d0"),          // aspectInnerCircle
        QColor("#b14040"),          // retrogradeIndicator
        QColor("#6f6f6f"),          // angularHouseLabel
        QColor("#eef2f7"),          // transitLaneBand — faint cool tint
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
        chartWheel_->setTheme(buildChartTheme(mode));
    }
    if (astroPreviewWheel_) {
        astroPreviewWheel_->setTheme(buildChartTheme(mode));
    }
    if (aspectDelegate_) {
        aspectDelegate_->setMatrixPalette(buildAspectMatrixPalette(mode));
        if (aspectsTable_) {
            if (auto* vp = aspectsTable_->viewport()) {
                vp->update();
            }
        }
    }
    if (themeLightAction_) {
        themeLightAction_->setChecked(mode == ThemeMode::Light);
    }
    if (themeCremeAction_) {
        themeCremeAction_->setChecked(mode == ThemeMode::Creme);
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
    LunarNodePolicy nodePolicy = defaultLunarNodePolicy_;
    if (activeTab_ == AppTab::Transits && hasTransitChart_) {
        nodePolicy = currentTransitChart_.lunarNodePolicy;
    } else if (activeTab_ == AppTab::Progression && progressionView_ != ProgressionView::NatalOnly
               && hasProgressionChart_) {
        nodePolicy = currentProgressionChart_.lunarNodePolicy;
    } else if (activeTab_ == AppTab::SolarReturn && hasSolarChart_) {
        nodePolicy = currentSolarChart_.lunarNodePolicy;
    } else if (activeTab_ == AppTab::LunarReturn && hasLunarChart_) {
        nodePolicy = currentLunarChart_.lunarNodePolicy;
    } else if (activeTab_ == AppTab::Relocation && hasRelocationChart_) {
        nodePolicy = currentRelocationChart_.lunarNodePolicy;
    } else if (hasCurrentChart_) {
        nodePolicy = currentChart_.lunarNodePolicy;
    }
    const QString displayName = lunarNodeDisplayName(name, nodePolicy);
    switch (aspectHeaderMode_) {
        case AspectHeaderMode::Glyphs: {
            QString glyph = bodyGlyph(name);
            if (glyph.trimmed().isEmpty() || glyph == "?") {
                glyph = abbrevForName(name);
            }
            return glyph;
        }
        case AspectHeaderMode::Full:
            return displayName;
        case AspectHeaderMode::Abbrev:
        default:
            if (isLunarNodeName(name) && (name == "North Node" || name == "South Node")) {
                const QString prefix = effectivePrimaryNodeType(nodePolicy) == LunarNodeType::True ? "t" : "m";
                return prefix + abbrevForName(name);
            }
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
    if (activeTab_ == AppTab::LunarReturn) {
        if (!hasCurrentChart_ || !hasLunarChart_) {
            setupTable(aspectsTable_, {}, 0);
            return;
        }
        switch (lunarAspectView_) {
            case LunarAspectView::LunarNatal:
                populateCrossAspectsOverlay(currentLunarChart_, currentChart_, "Lunar");
                break;
            case LunarAspectView::LunarReturn:
            default:
                populateAspects(currentLunarChart_);
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

void MainWindow::applyAspectTableFont() {
    if (!aspectsTable_) {
        return;
    }
    if (aspectHeaderMode_ == AspectHeaderMode::Glyphs) {
        QFont glyphFont = aspectsTable_->font();
        glyphFont.setFamily("Segoe UI Symbol");
        glyphFont.setPointSize(9);
        aspectsTable_->setFont(glyphFont);
        aspectsTable_->horizontalHeader()->setFont(glyphFont);
        aspectsTable_->verticalHeader()->setFont(glyphFont);
    } else {
        QFont baseFont = aspectsTable_->font();
        baseFont.setFamily(QString());
        baseFont.setPointSize(-1);
        aspectsTable_->setFont(baseFont);
        aspectsTable_->horizontalHeader()->setFont(baseFont);
        aspectsTable_->verticalHeader()->setFont(baseFont);
    }
}

bool MainWindow::isBodyVisibleInAspectGrid(const QString& name) const {
    if (isAsteroidBody(name)) {
        return aspectGridFilter_.showAsteroids && isAsteroidVisible(name);
    }
    if (isArabicLotName(name)) {
        // On Transits the grid is a scanning tool in a narrow column beside the
        // wheel. All 95 Arabic Lots turn it into a ~110x110 matrix that can only
        // be read by scrolling, which is what makes it unusable there. The other
        // tabs keep the user's "Show Arabic Lots" choice, where the grid is a
        // roomier reference table in the left dock.
        if (activeTab_ == AppTab::Transits) {
            return false;
        }
        return aspectGridFilter_.showLots;
    }
    if (name == "Vertex") {
        return aspectGridFilter_.showDerivedPoints;
    }
    if (name == "Lilith") {
        return aspectGridFilter_.showLilith;
    }
    if (isLunarNodeName(name)) {
        return aspectGridFilter_.showNodes;
    }
    if (name == "Ascendant" || name == "Midheaven" || name == "Descendant" || name == "IC") {
        return aspectGridFilter_.showAngles;
    }
    return true;
}

void MainWindow::handleAspectGridSettings() {
    if (!aspectGridSettingsButton_) {
        return;
    }
    QMenu menu(this);
    auto makeToggle = [&](const QString& label, bool current, auto setter) {
        auto* action = menu.addAction(label);
        action->setCheckable(true);
        action->setChecked(current);
        connect(action, &QAction::toggled, this, [this, setter](bool checked) {
            setter(checked);
            saveUiState();
            refreshAspectsForHeaderMode();
        });
    };
    makeToggle("Show Planets (Sun–Pluto)", true, [](bool) {});  // always on, informational
    menu.actions().last()->setEnabled(false);
    menu.addSeparator();
    makeToggle("Show Nodes (☊ ☋)", aspectGridFilter_.showNodes,
        [this](bool v) { aspectGridFilter_.showNodes = v; });
    makeToggle("Show Lilith", aspectGridFilter_.showLilith,
        [this](bool v) { aspectGridFilter_.showLilith = v; });
    makeToggle("Show Asteroids", aspectGridFilter_.showAsteroids,
        [this](bool v) { aspectGridFilter_.showAsteroids = v; });
    menu.addSeparator();
    makeToggle("Show Arabic Lots", aspectGridFilter_.showLots,
        [this](bool v) { aspectGridFilter_.showLots = v; });
    makeToggle("Show Derived Points (Vertex)", aspectGridFilter_.showDerivedPoints,
        [this](bool v) { aspectGridFilter_.showDerivedPoints = v; });
    menu.addSeparator();
    makeToggle("Show Angles (AC/MC/DC/IC)", aspectGridFilter_.showAngles,
        [this](bool v) { aspectGridFilter_.showAngles = v; });
    menu.exec(aspectGridSettingsButton_->mapToGlobal(
        QPoint(0, aspectGridSettingsButton_->height())));
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
    const int rows = aspectsTable_->rowCount();
    const int cols = aspectsTable_->columnCount();
    if (row >= rows || column >= cols) {
        return;
    }

    aspectHoverRow_ = row;
    aspectHoverCol_ = column;
    if (aspectDelegate_) {
        aspectDelegate_->setHoveredCell(row, column);
    }
    if (auto* vp = aspectsTable_->viewport()) {
        vp->update();
    }
}

void MainWindow::clearAspectHover() {
    if (!aspectsTable_) {
        return;
    }
    if (aspectHoverRow_ < 0 && aspectHoverCol_ < 0) {
        return;
    }
    aspectHoverRow_ = -1;
    aspectHoverCol_ = -1;
    if (aspectDelegate_) {
        aspectDelegate_->clearHover();
    }
    if (auto* vp = aspectsTable_->viewport()) {
        vp->update();
    }
}
void MainWindow::setupConnections() {
    auto syncConjunctionCountWithSelection = [this]() {
        if (!conjCountSpin_) {
            return;
        }
        QStringList selectedPlanetsList = selectedCheckableItems(conjPlanetCombo_);
        if (conjIncludeMoonCheck_ && !conjIncludeMoonCheck_->isChecked()) {
            selectedPlanetsList.removeAll("Moon");
        }
        const int selectedPlanets = selectedPlanetsList.size();
        const int clampedCount = std::max(2, selectedPlanets);
        const QSignalBlocker blocker(conjCountSpin_);
        conjCountSpin_->setMinimum(2);
        conjCountSpin_->setMaximum(clampedCount);
        conjCountSpin_->setValue(clampedCount);
    };

    if (mainTabBar_) {
        connect(mainTabBar_, &QTabBar::currentChanged, this, &MainWindow::handleMainTabChanged);
    }
    if (returnFinderController_) {
        connect(returnFinderController_, &ReturnFinderController::selectionChanged,
                this, &MainWindow::refreshReturnFinderDocks);
        connect(returnFinderController_, &ReturnFinderController::summaryChanged,
                this, &MainWindow::refreshReturnFinderDocks);
        connect(returnFinderController_, &ReturnFinderController::openResultRequested,
                this, &MainWindow::handleReturnFinderOpen);
        connect(returnFinderController_, &ReturnFinderController::statusMessage,
                this, [this](const QString& message) { setStatusMessage(message); });
    }
    if (planetaryHoursController_) {
        connect(planetaryHoursController_, &PlanetaryHoursController::resultChanged,
                this, &MainWindow::refreshPlanetaryHoursDocks);
        connect(planetaryHoursController_, &PlanetaryHoursController::selectedHourChanged,
                this, &MainWindow::refreshPlanetaryHoursDocks);
        connect(planetaryHoursController_, &PlanetaryHoursController::statusMessage,
                this, [this](const QString& message) { setStatusMessage(message); });
    }
    if (zodiacalReleasingController_) {
        connect(zodiacalReleasingController_, &ZodiacalReleasingController::selectionChanged,
                this, &MainWindow::refreshZodiacalReleasingDocks);
        connect(zodiacalReleasingController_, &ZodiacalReleasingController::timelineChanged,
                this, &MainWindow::refreshZodiacalReleasingDocks);
        connect(zodiacalReleasingController_, &ZodiacalReleasingController::statusMessage,
                this, [this](const QString& message) { setStatusMessage(message); });
    }
    if (geodeticEquivalentsController_) {
        connect(geodeticEquivalentsController_,
                &GeodeticEquivalentsController::resultChanged,
                this, &MainWindow::refreshGeodeticEquivalentsDocks);
        connect(geodeticEquivalentsController_,
                &GeodeticEquivalentsController::statusMessage,
                this, [this](const QString& message) {
                    setStatusMessage(message);
                });
    }
    if (profileToolbarNewButton_) {
        connect(profileToolbarNewButton_, &QToolButton::clicked, this, &MainWindow::handleNewChart);
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
    if (profileToolbarManageButton_) {
        connect(profileToolbarManageButton_, &QToolButton::clicked, this, &MainWindow::showChartManager);
    }
    if (profileToolbarSaveButton_) {
        connect(profileToolbarSaveButton_, &QToolButton::clicked, this, &MainWindow::handleSaveProfile);
    }
    if (profileToolbarEditButton_) {
        connect(profileToolbarEditButton_, &QToolButton::clicked, this, &MainWindow::handleEditChart);
    }
    if (profileToolbarDeleteButton_) {
        connect(profileToolbarDeleteButton_, &QToolButton::clicked, this, [this]() {
            const QString selected = profileToolbarCombo_
                ? profileToolbarCombo_->currentData().toString().trimmed()
                : QString();
            if (!selected.isEmpty()) {
                deleteProfileByName(selected);
                return;
            }
            handleDeleteProfile();
        });
    }
    if (profileToolbarCombo_) {
        connect(profileToolbarCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            const QString selected = profileToolbarCombo_->currentData().toString().trimmed();
            const bool hasSelection = !selected.isEmpty();
            if (profileToolbarLoadButton_) {
                profileToolbarLoadButton_->setEnabled(hasSelection);
            }
            if (profileToolbarDeleteButton_) {
                profileToolbarDeleteButton_->setEnabled(hasSelection);
            }
            if (openChartAction_) {
                openChartAction_->setEnabled(hasSelection || !listProfiles().isEmpty());
            }
            if (deleteChartAction_) {
                deleteChartAction_->setEnabled(hasSelection || !listProfiles().isEmpty());
            }
        });
    }
    if (zodiacToolbarTropicalRadio_) {
        connect(zodiacToolbarTropicalRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            if (!checked) {
                return;
            }
            applyZodiacToolbarSelection(true);
        });
    }
    if (zodiacToolbarSiderealRadio_) {
        connect(zodiacToolbarSiderealRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            if (!checked) {
                return;
            }
            applyZodiacToolbarSelection(true);
        });
    }
    if (zodiacToolbarAyanamsaCombo_) {
        connect(zodiacToolbarAyanamsaCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            applyZodiacToolbarSelection(true);
        });
    }
    if (transitSubTabBar_) {
        connect(transitSubTabBar_, &QTabBar::currentChanged, this, &MainWindow::handleTransitSubTabChanged);
    }
    if (transitAspectGridToggleButton_) {
        connect(transitAspectGridToggleButton_, &QToolButton::toggled, this,
                [this](bool checked) {
            transitAspectGridVisible_ = checked;
            updateTransitAspectGridVisibility();
            setStatusMessage(checked
                ? "Transit aspect matrix shown."
                : "Transit aspect matrix hidden.");
        });
    }
    if (transitListFilterCombo_) {
        connect(transitListFilterCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
            if (activeTab_ != AppTab::Transits || transitSubTab_ != TransitSubTab::Overview) {
                return;
            }
            if (hasTransitChart_) {
                populateTransitList(currentTransitChart_, transitMode_ == TransitMode::NatalOverlay);
            } else {
                refreshTransitsTab();
            }
        });
    }
    if (transitAspectsCopyButton_) {
        connect(transitAspectsCopyButton_, &QPushButton::clicked, this, [this]() {
            if (!transitAspectsTable_ || transitAspectsTable_->rowCount() <= 0) {
                setStatusMessage("No transit aspects are available to copy.");
                return;
            }
            auto cellText = [this](int row, int column) {
                if (auto* item = transitAspectsTable_->item(row, column)) {
                    QString value = item->text();
                    value.replace('|', "\\|");
                    return value;
                }
                return QString("-");
            };
            QStringList lines;
            lines << "# Transit Aspects in Effect";
            if (hasTransitChart_ && currentTransitChart_.localDateTime.isValid()) {
                lines << QString("- Moment: %1 (%2)")
                    .arg(currentTransitChart_.localDateTime.toString("MMMM d yyyy, h:mm:ss AP"),
                         currentTransitChart_.timezoneLabel);
            }
            lines << QString("- Mode: %1")
                .arg(transitMode_ == TransitMode::NatalOverlay
                    ? "Natal + transits" : "Transit only");
            lines << QString("- House system: %1")
                .arg(transitHouseSystem_ == HouseSystem::Placidus
                    ? "Placidus" : "Whole Sign");
            lines << "";
            // The table is subject / symbol / target / orb / motion. Read the
            // live headers so the export stays in step with the displayed
            // columns (they differ between overlay and transit-only mode).
            const int columnCount = transitAspectsTable_->columnCount();
            if (columnCount < 5) {
                setStatusMessage("No transit aspects are available to copy.");
                return;
            }
            auto headerText = [this](int column, const QString& fallback) {
                if (auto* item = transitAspectsTable_->horizontalHeaderItem(column)) {
                    const QString label = item->text().trimmed();
                    if (!label.isEmpty()) {
                        return label;
                    }
                }
                return fallback;
            };
            lines << QString("| %1 | Aspect | %2 | Orb | Motion |")
                .arg(headerText(0, "From"), headerText(2, "To"));
            lines << "|---|:---:|---|---:|:---|";
            for (int row = 0; row < transitAspectsTable_->rowCount(); ++row) {
                lines << QString("| %1 | %2 | %3 | %4 | %5 |")
                    .arg(cellText(row, 0), cellText(row, 1), cellText(row, 2),
                         cellText(row, 3), cellText(row, 4));
            }
            QApplication::clipboard()->setText(lines.join('\n'));
            setStatusMessage("Transit aspects copied as a Markdown table.");
        });
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
    connect(aspectOrbPreset1Button_, &QToolButton::clicked, this, [this]() {
        applyAspectDisplayMaxOrb(1.0);
    });
    connect(aspectOrbPreset2Button_, &QToolButton::clicked, this, [this]() {
        applyAspectDisplayMaxOrb(2.0);
    });
    connect(aspectOrbPreset3Button_, &QToolButton::clicked, this, [this]() {
        applyAspectDisplayMaxOrb(3.0);
    });
    connect(aspectOrbCustomSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                applyAspectDisplayMaxOrb(value);
            });
    if (transitOverlayRadio_) {
        connect(transitOverlayRadio_, &QRadioButton::toggled, this, &MainWindow::handleTransitModeChanged);
    }
    if (transitOnlyRadio_) {
        connect(transitOnlyRadio_, &QRadioButton::toggled, this, &MainWindow::handleTransitModeChanged);
    }
    if (transitWholeRadio_) {
        connect(transitWholeRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                applyTransitHouseSystem(HouseSystem::WholeSign);
            }
        });
    }
    if (transitPlacidusRadio_) {
        connect(transitPlacidusRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                applyTransitHouseSystem(HouseSystem::Placidus);
            }
        });
    }
    if (transitMinusWeekButton_) {
        connect(transitMinusWeekButton_, &QPushButton::clicked, this, [this]() {
            handleTransitShiftDays(-7);
        });
    }
    if (transitMinusDayButton_) {
        connect(transitMinusDayButton_, &QPushButton::clicked, this, [this]() {
            handleTransitShiftDays(-1);
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
    if (progressionLunarReturnPreviousButton_) {
        connect(progressionLunarReturnPreviousButton_, &QPushButton::clicked,
                this, [this]() { handleProgressedLunarReturn(-1); });
    }
    if (progressionLunarReturnNextButton_) {
        connect(progressionLunarReturnNextButton_, &QPushButton::clicked,
                this, [this]() { handleProgressedLunarReturn(+1); });
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
    if (solarPreviousButton_) {
        connect(solarPreviousButton_, &QPushButton::clicked, this, [this]() {
            handleSolarShiftYear(-1);
        });
    }
    if (solarNowButton_) {
        connect(solarNowButton_, &QPushButton::clicked, this, &MainWindow::handleSolarNow);
    }
    if (solarNextButton_) {
        connect(solarNextButton_, &QPushButton::clicked, this, [this]() {
            handleSolarShiftYear(+1);
        });
    }
    if (lunarAnchorDateEdit_) {
        connect(lunarAnchorDateEdit_, &QDateEdit::dateChanged, this, [this](const QDate&) {
            markLunarPending();
        });
    }
    if (lunarUseNatalRadio_) {
        connect(lunarUseNatalRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            Q_UNUSED(checked);
            updateLunarLocationAvailability();
            syncLunarLocationFromNatal();
            markLunarPending();
        });
    }
    if (lunarUseCustomRadio_) {
        connect(lunarUseCustomRadio_, &QRadioButton::toggled, this, [this](bool checked) {
            Q_UNUSED(checked);
            updateLunarLocationAvailability();
            markLunarPending();
        });
    }
    if (lunarLocationEdit_) {
        connect(lunarLocationEdit_, &QLineEdit::editingFinished, this, &MainWindow::markLunarPending);
    }
    if (lunarLatSpin_) {
        connect(lunarLatSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::markLunarPending);
    }
    if (lunarLonSpin_) {
        connect(lunarLonSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::markLunarPending);
    }
    if (lunarTimezoneEdit_) {
        connect(lunarTimezoneEdit_, &QLineEdit::editingFinished, this, [this]() {
            updateLunarTimezoneStatus();
            markLunarPending();
        });
        connect(lunarTimezoneEdit_, &QLineEdit::textChanged, this, &MainWindow::updateLunarTimezoneStatus);
    }
    if (lunarGeocodeButton_) {
        connect(lunarGeocodeButton_, &QPushButton::clicked, this, &MainWindow::handleLunarGeocode);
    }
    if (lunarCalculateButton_) {
        connect(lunarCalculateButton_, &QPushButton::clicked, this, &MainWindow::handleLunarCalculate);
    }
    if (lunarPrevButton_) {
        connect(lunarPrevButton_, &QPushButton::clicked, this, &MainWindow::handleLunarPrev);
    }
    if (lunarNowButton_) {
        connect(lunarNowButton_, &QPushButton::clicked, this, &MainWindow::handleLunarNow);
    }
    if (lunarNextButton_) {
        connect(lunarNextButton_, &QPushButton::clicked, this, &MainWindow::handleLunarNext);
    }
    if (lunarFinderRunButton_) {
        connect(lunarFinderRunButton_, &QPushButton::clicked, this, &MainWindow::handleLunarPlacementFinderRun);
    }
    if (lunarFinderStartDateEdit_) {
        connect(lunarFinderStartDateEdit_, &QDateEdit::dateChanged, this,
                [this](const QDate&) { markLunarPlacementFinderStale(); });
    }
    if (lunarFinderEndDateEdit_) {
        connect(lunarFinderEndDateEdit_, &QDateEdit::dateChanged, this,
                [this](const QDate&) { markLunarPlacementFinderStale(); });
    }
    if (lunarFinderModeCombo_) {
        connect(lunarFinderModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    updateLunarFinderModeAvailability();
                    markLunarPlacementFinderStale();
                });
    }
    if (lunarFinderPlanetCombo_) {
        connect(lunarFinderPlanetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { markLunarPlacementFinderStale(); });
    }
    if (lunarFinderStelliumCountSpin_) {
        connect(lunarFinderStelliumCountSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { markLunarPlacementFinderStale(); });
    }
    if (lunarFinderRulerHouseCombo_) {
        connect(lunarFinderRulerHouseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { markLunarPlacementFinderStale(); });
    }
    if (lunarFinderRulerSchemeCombo_) {
        connect(lunarFinderRulerSchemeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { markLunarPlacementFinderStale(); });
    }
    if (lunarFinderPlanet2Combo_) {
        connect(lunarFinderPlanet2Combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { markLunarPlacementFinderStale(); });
    }
    if (lunarFinderHouseCombo_) {
        connect(lunarFinderHouseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { markLunarPlacementFinderStale(); });
    }
    if (lunarFinderHouseSystemCombo_) {
        connect(lunarFinderHouseSystemCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { markLunarPlacementFinderStale(); });
    }
    if (lunarFinderConjunctionTargetCombo_) {
        connect(lunarFinderConjunctionTargetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { markLunarPlacementFinderStale(); });
    }
    if (lunarFinderConjunctionOrbSpin_) {
        connect(lunarFinderConjunctionOrbSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double) { markLunarPlacementFinderStale(); });
    }
    if (solarFinderRunButton_) {
        connect(solarFinderRunButton_, &QPushButton::clicked, this, &MainWindow::handleSolarPlacementFinderRun);
    }
    if (solarFinderStartYearSpin_) {
        connect(solarFinderStartYearSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &MainWindow::markSolarPlacementFinderStale);
    }
    if (solarFinderEndYearSpin_) {
        connect(solarFinderEndYearSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &MainWindow::markSolarPlacementFinderStale);
    }
    if (solarFinderPlanetCombo_) {
        connect(solarFinderPlanetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::markSolarPlacementFinderStale);
    }
    if (solarFinderHouseCombo_) {
        connect(solarFinderHouseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::markSolarPlacementFinderStale);
    }
    if (solarFinderHouseSystemCombo_) {
        connect(solarFinderHouseSystemCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::markSolarPlacementFinderStale);
    }
    if (solarFinderConjunctionTargetCombo_) {
        connect(solarFinderConjunctionTargetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    markSolarPlacementFinderStale();
                });
    }
    if (solarFinderConjunctionOrbSpin_) {
        connect(solarFinderConjunctionOrbSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &MainWindow::markSolarPlacementFinderStale);
    }
    if (solarFinderModeCombo_) {
        connect(solarFinderModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    updateSolarFinderModeAvailability();
                    markSolarPlacementFinderStale();
                });
    }
    if (solarFinderStelliumCountSpin_) {
        connect(solarFinderStelliumCountSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &MainWindow::markSolarPlacementFinderStale);
    }
    if (solarFinderRulerHouseCombo_) {
        connect(solarFinderRulerHouseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { markSolarPlacementFinderStale(); });
    }
    if (solarFinderRulerSchemeCombo_) {
        connect(solarFinderRulerSchemeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { markSolarPlacementFinderStale(); });
    }
    if (solarFinderPlanet2Combo_) {
        connect(solarFinderPlanet2Combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { markSolarPlacementFinderStale(); });
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
    if (solarTechniqueModeCombo_) {
        connect(solarTechniqueModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::refreshSolarTechniqueView);
    }
    if (solarTechniqueBodyPresetCombo_) {
        connect(solarTechniqueBodyPresetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int) {
                    if (solarTechniqueUpdatingBodyControls_ || !solarTechniqueBodyPresetCombo_) {
                        return;
                    }
                    const int presetValue = solarTechniqueBodyPresetCombo_->currentData().toInt();
                    const auto preset = static_cast<SolarTechniqueBodyPreset>(presetValue);
                    if (preset == SolarTechniqueBodyPreset::Custom) {
                        syncSolarTechniqueBodyPresetSelection(true);
                        return;
                    }
                    applySolarTechniqueBodyPreset(preset, true);
                });
    }
    if (solarTechniqueOrbSpin_) {
        connect(solarTechniqueOrbSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &MainWindow::refreshSolarTechniqueView);
    }
    if (solarTechniqueBodyPlanetsCheck_) {
        connect(solarTechniqueBodyPlanetsCheck_, &QCheckBox::toggled, this,
                [this](bool) { syncSolarTechniqueBodyPresetSelection(true); });
    }
    if (solarTechniqueBodyNodesCheck_) {
        connect(solarTechniqueBodyNodesCheck_, &QCheckBox::toggled, this,
                [this](bool) { syncSolarTechniqueBodyPresetSelection(true); });
    }
    if (solarTechniqueBodyAnglesCheck_) {
        connect(solarTechniqueBodyAnglesCheck_, &QCheckBox::toggled, this,
                [this](bool) { syncSolarTechniqueBodyPresetSelection(true); });
    }
    if (solarTechniqueBodyLotsCheck_) {
        connect(solarTechniqueBodyLotsCheck_, &QCheckBox::toggled, this,
                [this](bool) { syncSolarTechniqueBodyPresetSelection(true); });
    }
    if (solarTechniqueBodyAsteroidsCheck_) {
        connect(solarTechniqueBodyAsteroidsCheck_, &QCheckBox::toggled, this,
                [this](bool) { syncSolarTechniqueBodyPresetSelection(true); });
    }
    if (solarTechniqueBodyLilithCheck_) {
        connect(solarTechniqueBodyLilithCheck_, &QCheckBox::toggled, this,
                [this](bool) { syncSolarTechniqueBodyPresetSelection(true); });
    }
    if (solarTechniqueBodyVertexCheck_) {
        connect(solarTechniqueBodyVertexCheck_, &QCheckBox::toggled, this,
                [this](bool) { syncSolarTechniqueBodyPresetSelection(true); });
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
            if (tabs_->currentWidget() == reportPanel_) {
                refreshNatalReport();
            }
            if (activeTab_ == AppTab::SolarReturn) {
                updateAspectScopeTabs();
                updateSolarTechniqueDockTitles();
                refreshSolarTechniqueView();
                refreshSolarPlacementFinderView();
                return;
            }
            if (activeTab_ == AppTab::LunarReturn) {
                updateAspectScopeTabs();
                updateLunarReturnDockTitles();
                refreshLunarPlacementFinderView();
                refreshLunarReturnView();
                return;
            }
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
// Astrocartography UI signals.
    if (astroSourceCombo_) {
        connect(astroSourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            updateAstroSourceUi();
            updateAstrocartographyView();
        });
    }
    if (astroProgressionDateEdit_) {
        connect(astroProgressionDateEdit_, &QDateEdit::dateChanged, this, [this](const QDate&) {
            if (astroSourceMode() == AstroSourceMode::ProgressedCustom) {
                updateAstrocartographyView();
            }
        });
    }
    if (astroProgressionTimeEdit_) {
        connect(astroProgressionTimeEdit_, &QTimeEdit::timeChanged, this, [this](const QTime&) {
            if (astroSourceMode() == AstroSourceMode::ProgressedCustom) {
                updateAstrocartographyView();
            }
        });
    }
    if (astroProgressionTimezoneEdit_) {
        connect(astroProgressionTimezoneEdit_, &QLineEdit::editingFinished, this, [this]() {
            updateAstroSourceUi();
            if (astroSourceMode() != AstroSourceMode::Natal) {
                updateAstrocartographyView();
            }
        });
        connect(astroProgressionTimezoneEdit_, &QLineEdit::textChanged, this, [this](const QString&) {
            updateAstroSourceUi();
        });
    }
    if (astroProgressionNowButton_) {
        connect(astroProgressionNowButton_, &QPushButton::clicked, this, &MainWindow::handleAstroProgressionNow);
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
    for (auto* check : {astroLineAcCheck_, astroLineDcCheck_, astroLineMcCheck_, astroLineIcCheck_}) {
        if (check) {
            connect(check, &QCheckBox::toggled, this, [this](bool) {
                updateAstrocartographyView();
            });
        }
    }
    for (auto* check : {astroHarmoniousAspectsCheck_, astroDisharmoniousAspectsCheck_}) {
        if (check) {
            connect(check, &QCheckBox::toggled, this, [this](bool) {
                updateAstrocartographyView();
            });
        }
    }
    if (geodeticRefreshButton_) {
        connect(geodeticRefreshButton_, &QPushButton::clicked, this, &MainWindow::updateAstrocartographyView);
    }
    if (astroWorldButton_) {
        connect(astroWorldButton_, &QPushButton::clicked, this, [this]() {
            if (astroMapWidget_) {
                astroMapWidget_->zoomToWorld();
            }
        });
    }
    if (astroBirthplaceButton_) {
        connect(astroBirthplaceButton_, &QPushButton::clicked, this, [this]() {
            if (astroMapWidget_ && hasCurrentChart_) {
                astroMapWidget_->centerOn(currentInput_.latitude, currentInput_.longitude, 4);
            }
        });
    }
    if (astroClickedHouseCombo_) {
        connect(astroClickedHouseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (hasAstroSelectedLocation_) {
                refreshAstroClickedLocationView();
            }
        });
    }
    if (astroMapWidget_) {
        connect(astroMapWidget_, &AstroMapWidget::mapClicked, this, &MainWindow::handleAstroMapClicked);
        connect(astroMapWidget_, &AstroMapWidget::mapHovered, this, &MainWindow::handleAstroMapHovered);
        connect(astroMapWidget_, &AstroMapWidget::mapHoverCleared, this, &MainWindow::clearAstroHoverPreview);
    }
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
    if (conjIncludeMoonCheck_) {
        connect(conjIncludeMoonCheck_, &QCheckBox::toggled, this, [this, syncConjunctionCountWithSelection](bool) {
            syncConjunctionCountWithSelection();
            updateConjunctionModeAvailability();
        });
    }
    if (conjUniqueFirstCheck_) {
        connect(conjUniqueFirstCheck_, &QCheckBox::toggled, this, [this](bool) {
            updateConjunctionModeAvailability();
        });
    }
    if (conjUniqueDegreeCombo_) {
        connect(conjUniqueDegreeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
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
            connect(model, &QStandardItemModel::itemChanged, this, [this, model, syncConjunctionCountWithSelection](QStandardItem* item) {
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
                syncConjunctionCountWithSelection();
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
    if (lunationOverlayCheck_) {
        connect(lunationOverlayCheck_, &QCheckBox::toggled, this, [this](bool checked) {
            lunationOverlay_ = checked;
            updateChartLegend();
            if (inLunationsView() && hasLunationSelection_ && canApplyLunationResult(nullptr)) {
                applyLunationResult(lastLunationSelection_);
            }
        });
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
    if (lunationEclipseRuleCombo_) {
        connect(lunationEclipseRuleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateLunationModeAvailability);
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
            } else if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::AspectPeaks) {
                handleCopyTransitAspectPeakDetails();
            } else if (inLunationsView()) {
                handleCopyLunationDetails();
            }
        });
    }
    if (aspectGridSettingsButton_) {
        connect(aspectGridSettingsButton_, &QToolButton::clicked, this, &MainWindow::handleAspectGridSettings);
    }
    if (aspectsTable_) {
        connect(aspectsTable_, &QTableWidget::cellEntered, this, &MainWindow::updateAspectHover);
    }
    // Clean full-row hover highlight for the data/results tables (the aspect
    // matrix keeps its own delegate).
    for (QTableWidget* hoverTable : {summaryTable_, anglesTable_, planetsTable_,
                                     fixedStarsTable_, housesTable_, rightTopTable_, rightBottomTable_}) {
        if (hoverTable) {
            hoverTable->setItemDelegate(new RowHoverDelegate(hoverTable, hoverTable));
        }
    }
    if (rightTopTable_) {
        connect(rightTopTable_, &QTableWidget::cellClicked, this, [this](int row, int column) {
            Q_UNUSED(column);
            if (activeTab_ == AppTab::SolarReturn && isSolarPlacementFinderTabActive()) {
                handleSolarPlacementFinderResultActivated(row, column);
                return;
            }
            if (activeTab_ == AppTab::LunarReturn && isLunarPlacementFinderTabActive()) {
                handleLunarPlacementFinderResultActivated(row, column);
                return;
            }
            if (inLunationsView()) {
                handleLunationResultActivated(row, column);
                return;
            }
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
            } else if (transitSubTab_ == TransitSubTab::AspectPeaks) {
                handleTransitAspectPeakResultActivated(row, column);
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
            if (!inLunationsView()) {
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
        updateTransitSearchVisibility();
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
    if (transitAspectsDock_) {
        transitAspectsDock_->setFeatures(features);
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
    defaultLunarNodePolicy_.mode = lunarNodeModeFromString(
        settings.value("calculations/lunar_nodes/mode", "Mean").toString());
    defaultLunarNodePolicy_.primary = lunarNodeTypeFromString(
        settings.value("calculations/lunar_nodes/primary", "Mean").toString());
    if (defaultLunarNodePolicy_.mode == LunarNodeMode::MeanOnly) {
        defaultLunarNodePolicy_.primary = LunarNodeType::Mean;
    } else if (defaultLunarNodePolicy_.mode == LunarNodeMode::TrueOnly) {
        defaultLunarNodePolicy_.primary = LunarNodeType::True;
    }
    defaultHouseSystem_ = settings.value("calculations/default_house_system", 0).toInt() == 1
        ? HouseSystem::Placidus
        : HouseSystem::WholeSign;
    const QString releasingPoint =
        settings.value("calculations/zodiacal_releasing/release_point", "Spirit").toString();
    if (releasingPoint.compare("Fortune", Qt::CaseInsensitive) == 0) {
        defaultZodiacalReleasingSettings_.releasePoint = ZodiacalReleasingPoint::Fortune;
    } else if (releasingPoint.compare("Eros", Qt::CaseInsensitive) == 0) {
        defaultZodiacalReleasingSettings_.releasePoint = ZodiacalReleasingPoint::Eros;
    } else {
        defaultZodiacalReleasingSettings_.releasePoint = ZodiacalReleasingPoint::Spirit;
    }
    defaultZodiacalReleasingSettings_.timeKey =
        settings.value("calculations/zodiacal_releasing/time_key", "Traditional360").toString()
                .compare("Calendar3652425", Qt::CaseInsensitive) == 0
            ? ZodiacalReleasingTimeKey::Calendar3652425
            : ZodiacalReleasingTimeKey::Traditional360;
    defaultZodiacalReleasingSettings_.capricornYears =
        settings.value("calculations/zodiacal_releasing/capricorn_years", 27).toInt() == 30 ? 30 : 27;
    defaultZodiacalReleasingSettings_.applySameSignSpiritRule =
        settings.value("calculations/zodiacal_releasing/same_sign_spirit_rule", true).toBool();
    defaultZodiacalReleasingSettings_.maximumAge = qBound(
        1, settings.value("calculations/zodiacal_releasing/maximum_age", 120).toInt(), 300);
    defaultZodiacalReleasingSettings_.maximumLevel = qBound(
        1, settings.value("calculations/zodiacal_releasing/maximum_level", 4).toInt(), 4);
    if (zodiacalReleasingController_) {
        zodiacalReleasingController_->setDefaults(defaultZodiacalReleasingSettings_);
    }
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

    const int themeValue = settings.value("ui/theme", static_cast<int>(ThemeMode::Light)).toInt();
    if (themeValue >= 0 && themeValue <= static_cast<int>(ThemeMode::Creme)) {
        theme_ = static_cast<ThemeMode>(themeValue);
    }

    overlayAspectsTransitNatal_ = settings.value("chart/overlay_aspects/transit_natal", true).toBool();
    overlayAspectsTransitTransit_ = settings.value("chart/overlay_aspects/transit_transit", false).toBool();
    overlayAspectsNatalNatal_ = settings.value("chart/overlay_aspects/natal_natal", false).toBool();
    aspectDisplayMaxOrb_ = settings.value("chart/overlay_aspects/max_orb", 0.0).toDouble();
    solarReportOptions_.preset = static_cast<SolarReportPreset>(qBound(
        0, settings.value("solar/report/preset", static_cast<int>(SolarReportPreset::Basic)).toInt(), 2));
    solarReportOptions_.lotScope = static_cast<SolarReportLotScope>(qBound(
        0, settings.value("solar/report/lot_scope", static_cast<int>(SolarReportLotScope::Core)).toInt(), 2));
    solarReportOptions_.aspectScope = static_cast<SolarReportAspectScope>(qBound(
        0, settings.value("solar/report/aspect_scope", static_cast<int>(SolarReportAspectScope::Tight)).toInt(), 2));
    solarReportOptions_.includeAnnualProfection =
        settings.value("solar/report/include_annual_profection", true).toBool();
    solarReportOptions_.includeNatalPositions =
        settings.value("solar/report/include_natal_positions", true).toBool();
    solarReportOptions_.includeSolarPositions =
        settings.value("solar/report/include_solar_positions", true).toBool();
    solarReportOptions_.includeHouseCusps =
        settings.value("solar/report/include_house_cusps", true).toBool();
    solarReportOptions_.includeHouseOverlays =
        settings.value("solar/report/include_house_overlays", true).toBool();
    solarReportOptions_.includeSolarNatalAspects =
        settings.value("solar/report/include_solar_natal_aspects", true).toBool();
    solarReportOptions_.includeSolarSolarAspects =
        settings.value("solar/report/include_solar_solar_aspects", true).toBool();
    solarReportOptions_.includeNatalNatalAspects =
        settings.value("solar/report/include_natal_natal_aspects", false).toBool();
    solarReportOptions_.includeMinorBodies =
        settings.value("solar/report/include_minor_bodies", false).toBool();
    solarReportOptions_.includeDailyMotion =
        settings.value("solar/report/include_daily_motion", false).toBool();
    solarReportOptions_.includeDignities =
        settings.value("solar/report/include_dignities", true).toBool();
    solarReportOptions_.includeFixedStars =
        settings.value("solar/report/include_fixed_stars", false).toBool();
    updateSolarReportOptionsUi();
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

    aspectGridFilter_.showNodes          = settings.value("aspects/grid/show_nodes", true).toBool();
    aspectGridFilter_.showLilith         = settings.value("aspects/grid/show_lilith", false).toBool();
    aspectGridFilter_.showLots           = settings.value("aspects/grid/show_lots", true).toBool();
    aspectGridFilter_.showDerivedPoints  = settings.value("aspects/grid/show_derived_points", true).toBool();
    aspectGridFilter_.showAsteroids      = settings.value("aspects/grid/show_asteroids", false).toBool();
    aspectGridFilter_.showAngles         = settings.value("aspects/grid/show_angles", true).toBool();
    if (!settings.value("aspects/grid/default_visibility_v2", false).toBool()) {
        aspectGridFilter_.showLots = true;
        aspectGridFilter_.showDerivedPoints = true;
        settings.setValue("aspects/grid/show_lots", true);
        settings.setValue("aspects/grid/show_derived_points", true);
        settings.setValue("aspects/grid/default_visibility_v2", true);
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

    int savedTabId = static_cast<int>(AppTab::Natal);
    if (settings.contains("ui/main_tab_id")) {
        savedTabId = settings.value("ui/main_tab_id").toInt();
    } else {
        const int legacyIndex = settings.value("ui/main_tab", 0).toInt();
        const QVector<AppTab> legacyTabs = {
            AppTab::Natal, AppTab::Transits, AppTab::Progression, AppTab::SolarReturn,
            AppTab::LunarReturn, AppTab::Lunations, AppTab::Relocation, AppTab::Astrocartography,
        };
        if (legacyIndex >= 0 && legacyIndex < legacyTabs.size()) {
            savedTabId = static_cast<int>(legacyTabs[legacyIndex]);
        }
    }
    if (mainTabBar_) {
        for (int i = 0; i < mainTabBar_->count(); ++i) {
            if (mainTabBar_->tabData(i).toInt() == savedTabId) {
                mainTabBar_->setCurrentIndex(i);
                break;
            }
        }
    }
    const int mode = settings.value("ui/transit_mode", 0).toInt();
    transitMode_ = (mode == 1) ? TransitMode::TransitOnly : TransitMode::NatalOverlay;
    const int transitHouse = settings.value("ui/transit_house_system", 0).toInt();
    transitHouseSystem_ = (transitHouse == 1) ? HouseSystem::Placidus : HouseSystem::WholeSign;
    const int aspectView = settings.value("ui/transit_aspect_view", 0).toInt();
    if (aspectView >= 0 && aspectView <= 2) {
        transitAspectView_ = static_cast<TransitAspectView>(aspectView);
    }
    transitAspectGridVisible_ =
        settings.value("ui/transit_aspect_grid_visible", true).toBool();
    if (transitAspectGridToggleButton_) {
        transitAspectGridToggleButton_->setChecked(transitAspectGridVisible_);
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
    if (conjIncludeMoonCheck_) {
        conjIncludeMoonCheck_->setChecked(settings.value("transit/conj_include_moon", true).toBool());
    }
    if (conjUniqueFirstCheck_) {
        conjUniqueFirstCheck_->setChecked(settings.value("transit/conj_unique_first", false).toBool());
    }
    if (conjUniqueDegreeCombo_) {
        const double requestedStep = settings.value("transit/conj_unique_degree_step", 1.0).toDouble();
        int bestIndex = 0;
        double bestDiff = std::numeric_limits<double>::max();
        for (int i = 0; i < conjUniqueDegreeCombo_->count(); ++i) {
            const double step = conjUniqueDegreeCombo_->itemData(i).toDouble();
            const double diff = std::fabs(step - requestedStep);
            if (diff < bestDiff) {
                bestDiff = diff;
                bestIndex = i;
            }
        }
        conjUniqueDegreeCombo_->setCurrentIndex(bestIndex);
    }
    updateConjunctionModeAvailability();
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
    if (astroSourceCombo_) {
        int sourceMode = settings.value("astro/source_mode", static_cast<int>(AstroSourceMode::Natal)).toInt();
        if (sourceMode < static_cast<int>(AstroSourceMode::Natal)
            || sourceMode > static_cast<int>(AstroSourceMode::ProgressedCustom)) {
            sourceMode = static_cast<int>(AstroSourceMode::Natal);
        }
        const int idx = astroSourceCombo_->findData(sourceMode);
        if (idx >= 0) {
            const QSignalBlocker blocker(astroSourceCombo_);
            astroSourceCombo_->setCurrentIndex(idx);
        }
    }
    if (astroProgressionDateEdit_) {
        const QDate defaultDate = astroProgressionDateEdit_->date().isValid() ? astroProgressionDateEdit_->date() : QDate::currentDate();
        astroProgressionDateEdit_->setDate(settings.value("astro/progression_target_date", defaultDate).toDate());
    }
    if (astroProgressionTimeEdit_) {
        const QTime defaultTime = astroProgressionTimeEdit_->time().isValid() ? astroProgressionTimeEdit_->time() : QTime::currentTime();
        astroProgressionTimeEdit_->setTime(settings.value("astro/progression_target_time", defaultTime).toTime());
    }
    if (astroProgressionTimezoneEdit_) {
        astroProgressionTimezoneEdit_->setText(settings.value("astro/progression_timezone", astroProgressionTimezoneEdit_->text()).toString());
    }
    updateAstroSourceUi();
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
    const bool lunarUseNatalLocation = settings.value("lunar/use_natal_location", true).toBool();
    if (lunarUseNatalRadio_ && lunarUseCustomRadio_) {
        lunarUseNatalRadio_->setChecked(lunarUseNatalLocation);
        lunarUseCustomRadio_->setChecked(!lunarUseNatalLocation);
    }
    if (lunarLocationEdit_) {
        lunarLocationEdit_->setText(settings.value("lunar/location", lunarLocationEdit_->text()).toString());
    }
    if (lunarLatSpin_) {
        lunarLatSpin_->setValue(settings.value("lunar/lat", lunarLatSpin_->value()).toDouble());
    }
    if (lunarLonSpin_) {
        lunarLonSpin_->setValue(settings.value("lunar/lon", lunarLonSpin_->value()).toDouble());
    }
    if (lunarTimezoneEdit_) {
        lunarTimezoneEdit_->setText(settings.value("lunar/timezone", lunarTimezoneEdit_->text()).toString());
    }
    if (lunarAnchorDateEdit_) {
        const QString isoDate = settings.value("lunar/anchor_date").toString();
        const QDate savedDate = QDate::fromString(isoDate, Qt::ISODate);
        if (savedDate.isValid()) {
            lunarAnchorDateEdit_->setDate(savedDate);
        }
    }
    const int lunarView = settings.value("lunar/aspect_view", 0).toInt();
    if (lunarView >= 0 && lunarView <= 1) {
        lunarAspectView_ = static_cast<LunarAspectView>(lunarView);
    }
    updateLunarTimezoneStatus();
    if (lunarFinderStartDateEdit_) {
        const QDate d = QDate::fromString(settings.value("lunar/finder_start_date").toString(), Qt::ISODate);
        if (d.isValid()) {
            lunarFinderStartDateEdit_->setDate(d);
        }
    }
    if (lunarFinderEndDateEdit_) {
        const QDate d = QDate::fromString(settings.value("lunar/finder_end_date").toString(), Qt::ISODate);
        if (d.isValid()) {
            lunarFinderEndDateEdit_->setDate(d);
        }
    }
    if (lunarFinderPlanetCombo_) {
        const QString planet = settings.value("lunar/finder_planet", lunarFinderPlanetCombo_->currentText()).toString();
        const int idx = lunarFinderPlanetCombo_->findText(planet);
        if (idx >= 0) {
            lunarFinderPlanetCombo_->setCurrentIndex(idx);
        }
    }
    if (lunarFinderPlanet2Combo_) {
        const QString planet2 = settings.value("lunar/finder_planet2", "None").toString();
        const int idx = lunarFinderPlanet2Combo_->findText(planet2);
        if (idx >= 0) {
            lunarFinderPlanet2Combo_->setCurrentIndex(idx);
        }
    }
    if (lunarFinderStelliumCountSpin_) {
        lunarFinderStelliumCountSpin_->setValue(settings.value("lunar/finder_stellium_min",
            lunarFinderStelliumCountSpin_->value()).toInt());
    }
    if (lunarFinderHouseCombo_) {
        const int finderHouse = settings.value("lunar/finder_house", 1).toInt();
        const int idx = lunarFinderHouseCombo_->findData(finderHouse);
        if (idx >= 0) {
            lunarFinderHouseCombo_->setCurrentIndex(idx);
        }
    }
    if (lunarFinderHouseSystemCombo_) {
        int finderMode = settings.value("lunar/finder_house_mode",
            static_cast<int>(SolarPlacementFinderHouseMode::WholeSign)).toInt();
        if (finderMode < static_cast<int>(SolarPlacementFinderHouseMode::WholeSign)
            || finderMode > static_cast<int>(SolarPlacementFinderHouseMode::BothAnd)) {
            finderMode = static_cast<int>(SolarPlacementFinderHouseMode::WholeSign);
        }
        const int idx = lunarFinderHouseSystemCombo_->findData(finderMode);
        if (idx >= 0) {
            lunarFinderHouseSystemCombo_->setCurrentIndex(idx);
        }
    }
    if (lunarFinderConjunctionTargetCombo_) {
        const QString target = settings.value("lunar/finder_conjunction_target",
            lunarFinderConjunctionTargetCombo_->currentText()).toString();
        const int idx = lunarFinderConjunctionTargetCombo_->findText(target);
        if (idx >= 0) {
            lunarFinderConjunctionTargetCombo_->setCurrentIndex(idx);
        }
    }
    if (lunarFinderConjunctionOrbSpin_) {
        lunarFinderConjunctionOrbSpin_->setValue(settings.value("lunar/finder_conjunction_orb",
            lunarFinderConjunctionOrbSpin_->value()).toDouble());
    }
    if (lunarFinderRulerHouseCombo_) {
        const int h = settings.value("lunar/finder_ruler_house", 7).toInt();
        const int idx = lunarFinderRulerHouseCombo_->findData(h);
        if (idx >= 0) {
            lunarFinderRulerHouseCombo_->setCurrentIndex(idx);
        }
    }
    if (lunarFinderRulerSchemeCombo_) {
        const int s = settings.value("lunar/finder_ruler_scheme", 0).toInt();
        const int idx = lunarFinderRulerSchemeCombo_->findData(s);
        if (idx >= 0) {
            lunarFinderRulerSchemeCombo_->setCurrentIndex(idx);
        }
    }
    if (lunarFinderModeCombo_) {
        const int modeIdx = settings.value("lunar/finder_search_mode", 0).toInt();
        if (modeIdx >= 0 && modeIdx < lunarFinderModeCombo_->count()) {
            lunarFinderModeCombo_->setCurrentIndex(modeIdx);
        }
    }
    updateLunarFinderModeAvailability();
    if (solarTechniqueModeCombo_) {
        int techniqueCountingMode = settings.value("solar/technique_count_mode",
            static_cast<int>(SolarTechniqueCountingMode::SRStartDate)).toInt();
        if (techniqueCountingMode < static_cast<int>(SolarTechniqueCountingMode::SRStartDate)
            || techniqueCountingMode > static_cast<int>(SolarTechniqueCountingMode::SymbolicJanuaryFirst)) {
            techniqueCountingMode = static_cast<int>(SolarTechniqueCountingMode::SRStartDate);
        }
        const int idx = solarTechniqueModeCombo_->findData(techniqueCountingMode);
        if (idx >= 0) {
            const QSignalBlocker blocker(solarTechniqueModeCombo_);
            solarTechniqueModeCombo_->setCurrentIndex(idx);
        }
    }
    if (solarTechniqueBodyPresetCombo_) {
        int techniqueBodyPreset = settings.value("solar/technique_body_preset",
            static_cast<int>(SolarTechniqueBodyPreset::Core)).toInt();
        if (techniqueBodyPreset < static_cast<int>(SolarTechniqueBodyPreset::Core)
            || techniqueBodyPreset > static_cast<int>(SolarTechniqueBodyPreset::Custom)) {
            techniqueBodyPreset = static_cast<int>(SolarTechniqueBodyPreset::Core);
        }
        const auto preset = static_cast<SolarTechniqueBodyPreset>(techniqueBodyPreset);
        if (preset == SolarTechniqueBodyPreset::Custom) {
            solarTechniqueUpdatingBodyControls_ = true;
            if (solarTechniqueBodyPlanetsCheck_) {
                const QSignalBlocker blocker(solarTechniqueBodyPlanetsCheck_);
                solarTechniqueBodyPlanetsCheck_->setChecked(
                    settings.value("solar/technique_body_planets", true).toBool());
            }
            if (solarTechniqueBodyNodesCheck_) {
                const QSignalBlocker blocker(solarTechniqueBodyNodesCheck_);
                solarTechniqueBodyNodesCheck_->setChecked(
                    settings.value("solar/technique_body_nodes", true).toBool());
            }
            if (solarTechniqueBodyAnglesCheck_) {
                const QSignalBlocker blocker(solarTechniqueBodyAnglesCheck_);
                solarTechniqueBodyAnglesCheck_->setChecked(
                    settings.value("solar/technique_body_angles", true).toBool());
            }
            if (solarTechniqueBodyLotsCheck_) {
                const QSignalBlocker blocker(solarTechniqueBodyLotsCheck_);
                solarTechniqueBodyLotsCheck_->setChecked(
                    settings.value("solar/technique_body_lots", false).toBool());
            }
            if (solarTechniqueBodyAsteroidsCheck_) {
                const QSignalBlocker blocker(solarTechniqueBodyAsteroidsCheck_);
                solarTechniqueBodyAsteroidsCheck_->setChecked(
                    settings.value("solar/technique_body_asteroids", false).toBool());
            }
            if (solarTechniqueBodyLilithCheck_) {
                const QSignalBlocker blocker(solarTechniqueBodyLilithCheck_);
                solarTechniqueBodyLilithCheck_->setChecked(
                    settings.value("solar/technique_body_lilith", false).toBool());
            }
            if (solarTechniqueBodyVertexCheck_) {
                const QSignalBlocker blocker(solarTechniqueBodyVertexCheck_);
                solarTechniqueBodyVertexCheck_->setChecked(
                    settings.value("solar/technique_body_vertex", false).toBool());
            }
            solarTechniqueUpdatingBodyControls_ = false;
            syncSolarTechniqueBodyPresetSelection(false);
        } else {
            applySolarTechniqueBodyPreset(preset, false);
        }
    }
    if (solarFinderStartYearSpin_) {
        solarFinderStartYearSpin_->setValue(settings.value("solar/finder_start_year", solarFinderStartYearSpin_->value()).toInt());
    }
    if (solarFinderEndYearSpin_) {
        solarFinderEndYearSpin_->setValue(settings.value("solar/finder_end_year", solarFinderEndYearSpin_->value()).toInt());
    }
    if (solarFinderPlanetCombo_) {
        const QString finderPlanet = settings.value("solar/finder_planet", solarFinderPlanetCombo_->currentText()).toString();
        const int idx = solarFinderPlanetCombo_->findText(finderPlanet);
        if (idx >= 0) {
            solarFinderPlanetCombo_->setCurrentIndex(idx);
        }
    }
    if (solarFinderPlanet2Combo_) {
        const QString planet2 = settings.value("solar/finder_planet2", "None").toString();
        const int idx = solarFinderPlanet2Combo_->findText(planet2);
        if (idx >= 0) {
            solarFinderPlanet2Combo_->setCurrentIndex(idx);
        }
    }
    if (solarFinderHouseCombo_) {
        const int finderHouse = settings.value("solar/finder_house", 1).toInt();
        const int idx = solarFinderHouseCombo_->findData(finderHouse);
        if (idx >= 0) {
            solarFinderHouseCombo_->setCurrentIndex(idx);
        }
    }
    if (solarFinderHouseSystemCombo_) {
        int finderMode = settings.value("solar/finder_house_mode",
            static_cast<int>(SolarPlacementFinderHouseMode::WholeSign)).toInt();
        if (finderMode < static_cast<int>(SolarPlacementFinderHouseMode::WholeSign)
            || finderMode > static_cast<int>(SolarPlacementFinderHouseMode::BothAnd)) {
            finderMode = static_cast<int>(SolarPlacementFinderHouseMode::WholeSign);
        }
        const int idx = solarFinderHouseSystemCombo_->findData(finderMode);
        if (idx >= 0) {
            solarFinderHouseSystemCombo_->setCurrentIndex(idx);
        }
    }
    if (solarFinderConjunctionTargetCombo_) {
        const QString target = settings.value("solar/finder_conjunction_target",
            solarFinderConjunctionTargetCombo_->currentText()).toString();
        const int idx = solarFinderConjunctionTargetCombo_->findText(target);
        if (idx >= 0) {
            solarFinderConjunctionTargetCombo_->setCurrentIndex(idx);
        }
    }
    if (solarFinderConjunctionOrbSpin_) {
        solarFinderConjunctionOrbSpin_->setValue(settings.value("solar/finder_conjunction_orb",
            solarFinderConjunctionOrbSpin_->value()).toDouble());
    }
    if (solarFinderStelliumCountSpin_) {
        solarFinderStelliumCountSpin_->setValue(settings.value("solar/finder_stellium_min",
            solarFinderStelliumCountSpin_->value()).toInt());
    }
    if (solarFinderRulerHouseCombo_) {
        const int h = settings.value("solar/finder_ruler_house", 7).toInt();
        const int idx = solarFinderRulerHouseCombo_->findData(h);
        if (idx >= 0) {
            solarFinderRulerHouseCombo_->setCurrentIndex(idx);
        }
    }
    if (solarFinderRulerSchemeCombo_) {
        const int s = settings.value("solar/finder_ruler_scheme", 0).toInt();
        const int idx = solarFinderRulerSchemeCombo_->findData(s);
        if (idx >= 0) {
            solarFinderRulerSchemeCombo_->setCurrentIndex(idx);
        }
    }
    if (solarFinderModeCombo_) {
        const int modeIdx = settings.value("solar/finder_search_mode", 0).toInt();
        if (modeIdx >= 0 && modeIdx < solarFinderModeCombo_->count()) {
            solarFinderModeCombo_->setCurrentIndex(modeIdx);
        }
    }
    updateSolarFinderModeAvailability();
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
    syncLunarNodeToolbarControl();
}

void MainWindow::saveUiState() {
    QSettings settings;
    settings.setValue("calculations/lunar_nodes/mode", lunarNodeModeToString(defaultLunarNodePolicy_.mode));
    settings.setValue("calculations/lunar_nodes/primary", lunarNodeTypeToString(defaultLunarNodePolicy_.primary));
    settings.setValue("calculations/default_house_system", defaultHouseSystem_ == HouseSystem::Placidus ? 1 : 0);
    QString releasingPoint = "Spirit";
    if (defaultZodiacalReleasingSettings_.releasePoint == ZodiacalReleasingPoint::Fortune) {
        releasingPoint = "Fortune";
    } else if (defaultZodiacalReleasingSettings_.releasePoint == ZodiacalReleasingPoint::Eros) {
        releasingPoint = "Eros";
    }
    settings.setValue("calculations/zodiacal_releasing/release_point", releasingPoint);
    settings.setValue("calculations/zodiacal_releasing/time_key",
                      defaultZodiacalReleasingSettings_.timeKey == ZodiacalReleasingTimeKey::Calendar3652425
                          ? "Calendar3652425" : "Traditional360");
    settings.setValue("calculations/zodiacal_releasing/capricorn_years",
                      defaultZodiacalReleasingSettings_.capricornYears);
    settings.setValue("calculations/zodiacal_releasing/same_sign_spirit_rule",
                      defaultZodiacalReleasingSettings_.applySameSignSpiritRule);
    settings.setValue("calculations/zodiacal_releasing/maximum_age",
                      defaultZodiacalReleasingSettings_.maximumAge);
    settings.setValue("calculations/zodiacal_releasing/maximum_level",
                      defaultZodiacalReleasingSettings_.maximumLevel);
    settings.setValue("ui/dock_state", saveState());
    settings.setValue("ui/layout_locked", layoutLocked_);
    if (leftSplitter_ && leftSplitter_->count() > 1) {
        settings.setValue("ui/left_splitter", leftSplitter_->saveState());
    }
    if (mainTabBar_) {
        settings.setValue("ui/main_tab", mainTabBar_->currentIndex());
        settings.setValue("ui/main_tab_id", mainTabBar_->tabData(mainTabBar_->currentIndex()).toInt());
    }
    settings.setValue("ui/transit_mode", transitMode_ == TransitMode::TransitOnly ? 1 : 0);
    settings.setValue("ui/transit_house_system", transitHouseSystem_ == HouseSystem::Placidus ? 1 : 0);
    settings.setValue("ui/transit_aspect_view", static_cast<int>(transitAspectView_));
    settings.setValue("ui/transit_aspect_grid_visible", transitAspectGridVisible_);
    if (conjIncludeMoonCheck_) {
        settings.setValue("transit/conj_include_moon", conjIncludeMoonCheck_->isChecked());
    }
    if (conjUniqueFirstCheck_) {
        settings.setValue("transit/conj_unique_first", conjUniqueFirstCheck_->isChecked());
    }
    if (conjUniqueDegreeCombo_) {
        settings.setValue("transit/conj_unique_degree_step", conjUniqueDegreeCombo_->currentData().toDouble());
    }
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
    settings.setValue("solar/report/preset", static_cast<int>(solarReportOptions_.preset));
    settings.setValue("solar/report/lot_scope", static_cast<int>(solarReportOptions_.lotScope));
    settings.setValue("solar/report/aspect_scope", static_cast<int>(solarReportOptions_.aspectScope));
    settings.setValue("solar/report/include_annual_profection", solarReportOptions_.includeAnnualProfection);
    settings.setValue("solar/report/include_natal_positions", solarReportOptions_.includeNatalPositions);
    settings.setValue("solar/report/include_solar_positions", solarReportOptions_.includeSolarPositions);
    settings.setValue("solar/report/include_house_cusps", solarReportOptions_.includeHouseCusps);
    settings.setValue("solar/report/include_house_overlays", solarReportOptions_.includeHouseOverlays);
    settings.setValue("solar/report/include_solar_natal_aspects", solarReportOptions_.includeSolarNatalAspects);
    settings.setValue("solar/report/include_solar_solar_aspects", solarReportOptions_.includeSolarSolarAspects);
    settings.setValue("solar/report/include_natal_natal_aspects", solarReportOptions_.includeNatalNatalAspects);
    settings.setValue("solar/report/include_minor_bodies", solarReportOptions_.includeMinorBodies);
    settings.setValue("solar/report/include_daily_motion", solarReportOptions_.includeDailyMotion);
    settings.setValue("solar/report/include_dignities", solarReportOptions_.includeDignities);
    settings.setValue("solar/report/include_fixed_stars", solarReportOptions_.includeFixedStars);
    settings.setValue("ui/theme", static_cast<int>(theme_));
    settings.setValue("aspects/grid/show_nodes",           aspectGridFilter_.showNodes);
    settings.setValue("aspects/grid/show_lilith",          aspectGridFilter_.showLilith);
    settings.setValue("aspects/grid/show_lots",            aspectGridFilter_.showLots);
    settings.setValue("aspects/grid/show_derived_points",  aspectGridFilter_.showDerivedPoints);
    settings.setValue("aspects/grid/show_asteroids",       aspectGridFilter_.showAsteroids);
    settings.setValue("aspects/grid/show_angles",          aspectGridFilter_.showAngles);
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
    if (astroSourceCombo_) {
        settings.setValue("astro/source_mode", astroSourceCombo_->currentData().toInt());
    }
    if (astroProgressionDateEdit_) {
        settings.setValue("astro/progression_target_date", astroProgressionDateEdit_->date());
    }
    if (astroProgressionTimeEdit_) {
        settings.setValue("astro/progression_target_time", astroProgressionTimeEdit_->time());
    }
    if (astroProgressionTimezoneEdit_) {
        settings.setValue("astro/progression_timezone", astroProgressionTimezoneEdit_->text());
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
    if (lunarAnchorDateEdit_) {
        settings.setValue("lunar/anchor_date", lunarAnchorDateEdit_->date().toString(Qt::ISODate));
    }
    if (lunarUseNatalRadio_) {
        settings.setValue("lunar/use_natal_location", lunarUseNatalRadio_->isChecked());
    }
    if (lunarLocationEdit_) {
        settings.setValue("lunar/location", lunarLocationEdit_->text());
    }
    if (lunarLatSpin_) {
        settings.setValue("lunar/lat", lunarLatSpin_->value());
    }
    if (lunarLonSpin_) {
        settings.setValue("lunar/lon", lunarLonSpin_->value());
    }
    if (lunarTimezoneEdit_) {
        settings.setValue("lunar/timezone", lunarTimezoneEdit_->text());
    }
    settings.setValue("lunar/aspect_view", static_cast<int>(lunarAspectView_));
    if (lunarFinderStartDateEdit_) {
        settings.setValue("lunar/finder_start_date", lunarFinderStartDateEdit_->date().toString(Qt::ISODate));
    }
    if (lunarFinderEndDateEdit_) {
        settings.setValue("lunar/finder_end_date", lunarFinderEndDateEdit_->date().toString(Qt::ISODate));
    }
    if (lunarFinderModeCombo_) {
        settings.setValue("lunar/finder_search_mode", lunarFinderModeCombo_->currentIndex());
    }
    if (lunarFinderPlanetCombo_) {
        settings.setValue("lunar/finder_planet", lunarFinderPlanetCombo_->currentText());
    }
    if (lunarFinderPlanet2Combo_) {
        settings.setValue("lunar/finder_planet2", lunarFinderPlanet2Combo_->currentText());
    }
    if (lunarFinderStelliumCountSpin_) {
        settings.setValue("lunar/finder_stellium_min", lunarFinderStelliumCountSpin_->value());
    }
    if (lunarFinderHouseCombo_) {
        settings.setValue("lunar/finder_house", lunarFinderHouseCombo_->currentData().toInt());
    }
    if (lunarFinderHouseSystemCombo_) {
        settings.setValue("lunar/finder_house_mode", lunarFinderHouseSystemCombo_->currentData().toInt());
    }
    if (lunarFinderRulerHouseCombo_) {
        settings.setValue("lunar/finder_ruler_house", lunarFinderRulerHouseCombo_->currentData().toInt());
    }
    if (lunarFinderRulerSchemeCombo_) {
        settings.setValue("lunar/finder_ruler_scheme", lunarFinderRulerSchemeCombo_->currentData().toInt());
    }
    if (lunarFinderConjunctionTargetCombo_) {
        settings.setValue("lunar/finder_conjunction_target", lunarFinderConjunctionTargetCombo_->currentText());
    }
    if (lunarFinderConjunctionOrbSpin_) {
        settings.setValue("lunar/finder_conjunction_orb", lunarFinderConjunctionOrbSpin_->value());
    }
    if (solarTechniqueModeCombo_) {
        settings.setValue("solar/technique_count_mode", solarTechniqueModeCombo_->currentData().toInt());
    }
    if (solarTechniqueBodyPresetCombo_) {
        settings.setValue("solar/technique_body_preset", solarTechniqueBodyPresetCombo_->currentData().toInt());
    }
    if (solarTechniqueBodyPlanetsCheck_) {
        settings.setValue("solar/technique_body_planets", solarTechniqueBodyPlanetsCheck_->isChecked());
    }
    if (solarTechniqueBodyNodesCheck_) {
        settings.setValue("solar/technique_body_nodes", solarTechniqueBodyNodesCheck_->isChecked());
    }
    if (solarTechniqueBodyAnglesCheck_) {
        settings.setValue("solar/technique_body_angles", solarTechniqueBodyAnglesCheck_->isChecked());
    }
    if (solarTechniqueBodyLotsCheck_) {
        settings.setValue("solar/technique_body_lots", solarTechniqueBodyLotsCheck_->isChecked());
    }
    if (solarTechniqueBodyAsteroidsCheck_) {
        settings.setValue("solar/technique_body_asteroids", solarTechniqueBodyAsteroidsCheck_->isChecked());
    }
    if (solarTechniqueBodyLilithCheck_) {
        settings.setValue("solar/technique_body_lilith", solarTechniqueBodyLilithCheck_->isChecked());
    }
    if (solarTechniqueBodyVertexCheck_) {
        settings.setValue("solar/technique_body_vertex", solarTechniqueBodyVertexCheck_->isChecked());
    }
    if (solarFinderStartYearSpin_) {
        settings.setValue("solar/finder_start_year", solarFinderStartYearSpin_->value());
    }
    if (solarFinderEndYearSpin_) {
        settings.setValue("solar/finder_end_year", solarFinderEndYearSpin_->value());
    }
    if (solarFinderPlanetCombo_) {
        settings.setValue("solar/finder_planet", solarFinderPlanetCombo_->currentText());
    }
    if (solarFinderPlanet2Combo_) {
        settings.setValue("solar/finder_planet2", solarFinderPlanet2Combo_->currentText());
    }
    if (solarFinderHouseCombo_) {
        settings.setValue("solar/finder_house", solarFinderHouseCombo_->currentData().toInt());
    }
    if (solarFinderHouseSystemCombo_) {
        settings.setValue("solar/finder_house_mode", solarFinderHouseSystemCombo_->currentData().toInt());
    }
    if (solarFinderConjunctionTargetCombo_) {
        settings.setValue("solar/finder_conjunction_target", solarFinderConjunctionTargetCombo_->currentText());
    }
    if (solarFinderConjunctionOrbSpin_) {
        settings.setValue("solar/finder_conjunction_orb", solarFinderConjunctionOrbSpin_->value());
    }
    if (solarFinderModeCombo_) {
        settings.setValue("solar/finder_search_mode", solarFinderModeCombo_->currentIndex());
    }
    if (solarFinderStelliumCountSpin_) {
        settings.setValue("solar/finder_stellium_min", solarFinderStelliumCountSpin_->value());
    }
    if (solarFinderRulerHouseCombo_) {
        settings.setValue("solar/finder_ruler_house", solarFinderRulerHouseCombo_->currentData().toInt());
    }
    if (solarFinderRulerSchemeCombo_) {
        settings.setValue("solar/finder_ruler_scheme", solarFinderRulerSchemeCombo_->currentData().toInt());
    }
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
    if (!confirmUnsavedChartChanges()) {
        event->ignore();
        return;
    }

    saveUiState();

    requestWorkerCancel(searchWorker_);
    requestWorkerCancel(calendarWorker_);
    requestWorkerCancel(conjWorker_);
    requestWorkerCancel(lunationWorker_);
    requestWorkerCancel(scanWorker_);
    requestWorkerCancel(aspectPeakWorker_);

    bool allStopped = true;
    if (returnFinderController_ && !returnFinderController_->shutdown(4000)) {
        allStopped = false;
        if (statusBar()) statusBar()->showMessage("Waiting for Return Finder search to stop...", 5000);
    }
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
    waitForThread(aspectPeakThread_, "aspect peak search");

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

    if (event->type() == QEvent::ApplicationDeactivate
        || event->type() == QEvent::WindowDeactivate) {
        transitSpaceNavigationHeld_ = false;
    }

    if (event->type() == QEvent::KeyRelease) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Space && transitSpaceNavigationHeld_) {
            transitSpaceNavigationHeld_ = false;
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress
        && activeTab_ == AppTab::Transits
        && QApplication::activeWindow() == this
        && !QApplication::activeModalWidget()
        && !QApplication::activePopupWidget()) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        QWidget* focus = QApplication::focusWidget();
        auto* focusedTextEdit = qobject_cast<QTextEdit*>(focus);
        auto* focusedCombo = qobject_cast<QComboBox*>(focus);
        const bool editableFocus = qobject_cast<QLineEdit*>(focus)
            || qobject_cast<QAbstractSpinBox*>(focus)
            || (focusedTextEdit && !focusedTextEdit->isReadOnly())
            || (focusedCombo && focusedCombo->isEditable());
        if (editableFocus) {
            transitSpaceNavigationHeld_ = false;
            return QMainWindow::eventFilter(obj, event);
        }

        const auto modifiers = keyEvent->modifiers();
        const bool hasDisallowedModifier = modifiers.testFlag(Qt::ControlModifier)
            || modifiers.testFlag(Qt::AltModifier)
            || modifiers.testFlag(Qt::MetaModifier);
        if (keyEvent->key() == Qt::Key_Space && !hasDisallowedModifier) {
            transitSpaceNavigationHeld_ = true;
            return true;
        }
        if (transitSpaceNavigationHeld_ && !hasDisallowedModifier) {
            if (keyEvent->isAutoRepeat()) {
                return true;
            }
            const bool byWeek = modifiers.testFlag(Qt::ShiftModifier);
            if (keyEvent->key() == Qt::Key_Left) {
                handleTransitShiftDays(byWeek ? -7 : -1);
                return true;
            }
            if (keyEvent->key() == Qt::Key_Right) {
                handleTransitShiftDays(byWeek ? 7 : 1);
                return true;
            }
            if (keyEvent->key() == Qt::Key_Home) {
                handleTransitNow();
                return true;
            }
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
    } else if (activeTab_ == AppTab::LunarReturn) {
        refreshLunarReturnView();
    } else if (activeTab_ == AppTab::Lunations) {
        refreshLunationsTab();
    } else if (activeTab_ == AppTab::Relocation) {
        refreshRelocationView();
    } else if (hasCurrentChart_) {
        populateAspects(currentChart_);
    }
}

void MainWindow::applyAspectDisplayMaxOrb(double maxOrb, bool markCustom) {
    const double boundedOrb = std::isfinite(maxOrb)
        ? std::clamp(maxOrb, 0.0, 15.0)
        : 0.0;
    aspectDisplayMaxOrb_ = boundedOrb;
    if (markCustom) {
        markChartReadabilityCustom();
    }
    if (chartWheel_) {
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
    }
    if (activeTab_ == AppTab::Transits
        && transitSubTab_ == TransitSubTab::Overview
        && hasTransitChart_) {
        populateTransitAspectsInEffect(
            currentTransitChart_, transitMode_ == TransitMode::NatalOverlay);
    }
    // populateAspectMatrix applies this orb as a display filter while building
    // the cells, so the grid keeps whatever it last rendered unless it is
    // rebuilt here. Without this the orb buttons moved the wheel and the
    // "Aspects in Effect" list but left the matrix stale.
    refreshAspectMatrixForCurrentView();
    syncAspectOrbQuickControls();
}

void MainWindow::refreshAspectMatrixForCurrentView() {
    // Re-render the aspect grid from charts that have already been computed;
    // nothing here recalculates ephemeris.
    if (activeTab_ == AppTab::Transits) {
        // The Lunations sub-tab drives the grid from the selected lunation, not
        // from the transit chart, so re-apply that selection instead.
        if (transitSubTab_ == TransitSubTab::Lunations && hasLunationSelection_) {
            if (canApplyLunationResult(nullptr)) {
                applyLunationResult(lastLunationSelection_);
            }
            return;
        }
        if (!hasTransitChart_ || transitPending_) {
            return;
        }
        if (transitMode_ != TransitMode::NatalOverlay) {
            populateAspects(currentTransitChart_);
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
        return;
    }
    if (activeTab_ == AppTab::Progression) {
        refreshProgressionView();
    } else if (activeTab_ == AppTab::SolarReturn) {
        refreshSolarReturnView();
    } else if (activeTab_ == AppTab::LunarReturn) {
        refreshLunarReturnView();
    } else if (activeTab_ == AppTab::Lunations) {
        refreshLunationsTab();
    } else if (activeTab_ == AppTab::Relocation) {
        refreshRelocationView();
    } else if (hasCurrentChart_) {
        populateAspects(currentChart_);
    }
}

void MainWindow::syncAspectOrbQuickControls() {
    if (!aspectOrbQuickPanel_) {
        return;
    }

    if (aspectOrbCustomSpin_) {
        const QSignalBlocker blocker(aspectOrbCustomSpin_);
        aspectOrbCustomSpin_->setValue(aspectDisplayMaxOrb_);
    }
    const auto syncPreset = [this](QToolButton* button, double orb) {
        if (!button) {
            return;
        }
        const QSignalBlocker blocker(button);
        button->setChecked(aspectDisplayMaxOrb_ > 0.0
            && std::fabs(aspectDisplayMaxOrb_ - orb) < 0.01);
    };
    syncPreset(aspectOrbPreset1Button_, 1.0);
    syncPreset(aspectOrbPreset2Button_, 2.0);
    syncPreset(aspectOrbPreset3Button_, 3.0);

    const bool aspectLinesVisible = chartWheel_ && chartWheel_->showAspects();
    aspectOrbQuickPanel_->setEnabled(aspectLinesVisible);
    aspectOrbQuickPanel_->setToolTip(aspectLinesVisible
        ? "Quickly limit displayed aspects by their maximum orb."
        : "Enable aspect lines from the chart settings menu to use this filter.");
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
    applyAspectDisplayMaxOrb(aspectDisplayMaxOrb_, false);
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
    const auto system = activeTab_ == AppTab::Transits
        ? transitHouseSystem_
        : (hasCurrentChart_ ? currentInput_.houseSystem : defaultHouseSystem_);
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

    QAction* toggleDegrees = menu.addAction("Show body and angle degrees");
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
        const auto selected = (action == housePlacidus)
            ? HouseSystem::Placidus
            : HouseSystem::WholeSign;
        if (activeTab_ == AppTab::Transits) {
            applyTransitHouseSystem(selected);
        } else {
            defaultHouseSystem_ = selected;
            if (hasCurrentChart_ && currentInput_.houseSystem != selected) {
                auto input = currentInput_;
                input.houseSystem = selected;
                if (computeChart(input, currentLocation_)) {
                    setCurrentChartModified(true);
                }
            }
        }
    } else if (action == toggleAspects) {
        markChartReadabilityCustom();
        chartWheel_->setShowAspects(toggleAspects->isChecked());
        syncAspectOrbQuickControls();
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
        } else if (activeTab_ == AppTab::LunarReturn) {
            refreshLunarReturnView();
        } else if (activeTab_ == AppTab::Lunations) {
            refreshLunationsTab();
        } else if (activeTab_ == AppTab::Relocation) {
            refreshRelocationView();
        } else {
            if (hasCurrentChart_) {
                populateAspects(currentChart_);
            }
        }
    } else if (orbActions.contains(action)) {
        double selectedOrb = aspectDisplayMaxOrb_;
        if (action == orbAll) {
            selectedOrb = 0.0;
        } else if (action == orb1) {
            selectedOrb = 1.0;
        } else if (action == orb2) {
            selectedOrb = 2.0;
        } else if (action == orb3) {
            selectedOrb = 3.0;
        } else if (action == orb4) {
            selectedOrb = 4.0;
        } else if (action == orb6) {
            selectedOrb = 6.0;
        }
        applyAspectDisplayMaxOrb(selectedOrb);
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

void MainWindow::setCurrentChartModified(bool modified) {
    currentChartModified_ = hasCurrentChart_ && modified;
    refreshProfileToolbar();
}

bool MainWindow::saveCurrentChart() {
    if (!hasCurrentChart_) {
        setStatusMessage("Load or create a chart before saving it.");
        return false;
    }

    if (!currentProfileName_.trimmed().isEmpty()) {
        return saveProfileByName(currentProfileName_, false);
    }

    const QString defaultName = currentInput_.name.trimmed().isEmpty()
        ? QString("New Chart")
        : currentInput_.name.trimmed();
    bool ok = false;
    const QString chartName = QInputDialog::getText(
        this,
        "Save Chart",
        "Chart name:",
        QLineEdit::Normal,
        defaultName,
        &ok);
    if (!ok) {
        return false;
    }
    return saveProfileByName(chartName, true);
}

bool MainWindow::confirmUnsavedChartChanges() {
    if (!hasCurrentChart_ || !currentChartModified_) {
        return true;
    }

    const QString chartName = currentProfileName_.trimmed().isEmpty()
        ? QString("Unsaved Chart")
        : currentProfileName_;
    QMessageBox prompt(this);
    prompt.setIcon(QMessageBox::Warning);
    prompt.setWindowTitle("Unsaved chart changes");
    prompt.setText(QString("Save changes to \"%1\" before continuing?").arg(chartName));
    prompt.setInformativeText("Unsaved changes will be lost if you choose Discard.");
    prompt.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    prompt.setDefaultButton(QMessageBox::Save);

    const int result = prompt.exec();
    if (result == QMessageBox::Save) {
        return saveCurrentChart();
    }
    if (result == QMessageBox::Discard) {
        return true;
    }
    return false;
}

bool MainWindow::deleteProfileByName(const QString& profileName) {
    const QString normalized = profileName.trimmed();
    if (normalized.isEmpty()) {
        setStatusMessage("Select a saved chart to delete.");
        return false;
    }

    const QString filePath = profileFilePath(normalized);
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        setStatusMessage("Saved chart file not found.");
        return false;
    }

    const auto result = QMessageBox::question(
        this,
        "Delete chart",
        QString("Delete the saved chart \"%1\"?\n\nThe chart file will be permanently removed.").arg(normalized),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (result != QMessageBox::Yes) {
        return false;
    }
    if (!QFile::remove(filePath)) {
        setStatusMessage("Unable to delete the saved chart.");
        return false;
    }

    const bool deletedCurrentChart = (currentProfileName_ == normalized);
    if (deletedCurrentChart) {
        currentProfileName_.clear();
        currentChartModified_ = hasCurrentChart_;
    }
    refreshProfileToolbar();
    setStatusMessage(deletedCurrentChart
        ? QString("Chart \"%1\" deleted. The current chart remains open as unsaved.").arg(normalized)
        : QString("Chart \"%1\" deleted.").arg(normalized));
    return true;
}

bool MainWindow::renameProfileByName(const QString& profileName) {
    const QString oldName = profileName.trimmed();
    const QString oldPath = profileFilePath(oldName);
    if (oldName.isEmpty() || oldPath.isEmpty() || !QFileInfo::exists(oldPath)) {
        setStatusMessage("Select a valid saved chart to rename.");
        return false;
    }

    bool ok = false;
    QString newName = QInputDialog::getText(
        this,
        "Rename Chart",
        "New chart name:",
        QLineEdit::Normal,
        oldName,
        &ok).trimmed();
    if (!ok) {
        return false;
    }
    newName = sanitizeProfileName(newName);
    if (newName.isEmpty()) {
        setStatusMessage("Chart name cannot be empty or contain only invalid characters.");
        return false;
    }
    if (newName == oldName) {
        return false;
    }

    const QString newPath = profileFilePath(newName);
    if (newPath.isEmpty()) {
        setStatusMessage("Unable to create a file name for this chart.");
        return false;
    }
    if (QFileInfo::exists(newPath)) {
        setStatusMessage(QString("A saved chart named \"%1\" already exists.").arg(newName));
        return false;
    }

    QFile source(oldPath);
    if (!source.open(QIODevice::ReadOnly)) {
        setStatusMessage(QString("Unable to read chart before renaming: %1").arg(source.errorString()));
        return false;
    }
    const QByteArray sourceData = source.readAll();
    source.close();

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(sourceData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setStatusMessage("The saved chart file is invalid and cannot be renamed safely.");
        return false;
    }
    QJsonObject object = document.object();
    object["profile_name"] = newName;
    const QByteArray renamedData = QJsonDocument(object).toJson(QJsonDocument::Indented);

    QSaveFile destination(newPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        setStatusMessage(QString("Unable to rename chart: %1").arg(destination.errorString()));
        return false;
    }
    if (destination.write(renamedData) != renamedData.size() || !destination.commit()) {
        setStatusMessage(QString("Unable to rename chart: %1").arg(destination.errorString()));
        return false;
    }
    if (!QFile::remove(oldPath)) {
        QFile::remove(newPath);
        setStatusMessage("Unable to remove the old chart file; rename was cancelled.");
        return false;
    }

    if (currentProfileName_ == oldName) {
        currentProfileName_ = newName;
    }
    refreshProfileToolbar();
    setStatusMessage(QString("Chart \"%1\" renamed to \"%2\".").arg(oldName, newName));
    return true;
}

void MainWindow::showChartManager() {
    auto savedChartEntries = [this]() {
        QVector<SavedChartEntry> entries;
        const QStringList profiles = listProfiles();
        entries.reserve(profiles.size());

        auto displayValue = [](const QString& value) {
            const QString trimmed = value.trimmed();
            return trimmed.isEmpty() ? QString("-") : trimmed;
        };

        for (const QString& profileName : profiles) {
            SavedChartEntry entry;
            entry.profileName = profileName;
            entry.personName = "-";
            entry.date = "-";
            entry.location = "-";
            entry.zodiac = "-";
            entry.houseSystem = "-";

            QFile file(profileFilePath(profileName));
            if (!file.open(QIODevice::ReadOnly)) {
                entry.personName = "Unreadable chart file";
                entries.push_back(entry);
                continue;
            }
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
            file.close();
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                entry.personName = "Invalid chart file";
                entries.push_back(entry);
                continue;
            }

            const QJsonObject object = document.object();
            entry.personName = displayValue(object.value("name").toString());
            entry.date = displayValue(object.value("date").toString());
            entry.location = displayValue(object.value("location").toString());
            entry.zodiac = displayValue(object.value("zodiac_system").toString());
            entry.houseSystem = displayValue(object.value("house_system").toString());
            entries.push_back(entry);
        }
        return entries;
    };

    while (true) {
        ChartManagerDialog dialog(savedChartEntries(), this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        const auto action = dialog.selectedAction();
        const QString selectedName = dialog.selectedProfileName();
        switch (action) {
            case ChartManagerDialog::Action::NewChart:
                if (!confirmUnsavedChartChanges()) {
                    break;
                }
                if (openChartSetupDialog(true)) {
                    saveCurrentChart();
                }
                return;
            case ChartManagerDialog::Action::LoadChart:
                if (loadProfileByName(selectedName)) {
                    return;
                }
                break;
            case ChartManagerDialog::Action::EditChart:
                if (loadProfileByName(selectedName)) {
                    if (openChartSetupDialog(false)) {
                        saveCurrentChart();
                    }
                    return;
                }
                break;
            case ChartManagerDialog::Action::RenameChart:
                renameProfileByName(selectedName);
                break;
            case ChartManagerDialog::Action::DeleteChart:
                deleteProfileByName(selectedName);
                break;
            case ChartManagerDialog::Action::None:
            default:
                return;
        }
    }
}

bool MainWindow::saveProfileByName(const QString& profileName, bool promptOverwrite) {
    if (!hasCurrentChart_) {
        setStatusMessage("Load or create a chart before saving it.");
        return false;
    }

    QString normalized = profileName.trimmed();
    if (normalized.isEmpty()) {
        setStatusMessage("Chart name cannot be empty.");
        return false;
    }
    const QString safeName = sanitizeProfileName(normalized);
    if (safeName.isEmpty()) {
        setStatusMessage("Chart name contains only invalid characters.");
        return false;
    }
    normalized = safeName;
    const QString filePath = profileFilePath(normalized);

    if (promptOverwrite && QFileInfo::exists(filePath) && normalized != currentProfileName_) {
        const auto overwrite = QMessageBox::question(
            this,
            "Replace saved chart",
            QString("A chart named \"%1\" already exists. Replace it?").arg(normalized),
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
    obj["zodiac_system"] = zodiacSystemToString(currentInput_.zodiacSystem);
    obj["sidereal_ayanamsa"] = siderealAyanamsaToString(currentInput_.siderealAyanamsa);
    obj["lunar_node_mode"] = lunarNodeModeToString(currentInput_.lunarNodePolicy.mode);
    obj["lunar_node_primary"] = lunarNodeTypeToString(effectivePrimaryNodeType(currentInput_.lunarNodePolicy));
    obj["lunar_node_uses_app_default"] = currentInput_.useDefaultLunarNodePolicy;
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
    obj["version"] = 4;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatusMessage(QString("Unable to save chart: %1").arg(file.errorString()));
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();

    currentProfileName_ = normalized;
    currentChartModified_ = false;
    refreshProfileToolbar();
    setStatusMessage(QString("Chart \"%1\" saved.").arg(normalized));
    return true;
}

bool MainWindow::loadProfileByName(const QString& profileName) {
    QString normalized = profileName.trimmed();
    if (normalized.isEmpty()) {
        setStatusMessage("Select a saved chart to load.");
        return false;
    }

    const QString filePath = profileFilePath(normalized);
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        setStatusMessage("Saved chart file not found.");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatusMessage(QString("Unable to load chart: %1").arg(file.errorString()));
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        setStatusMessage("Saved chart file is not valid JSON.");
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
    input.zodiacSystem = zodiacSystemFromString(obj.value("zodiac_system").toString());
    input.siderealAyanamsa = siderealAyanamsaFromString(obj.value("sidereal_ayanamsa").toString());
    if (obj.contains("lunar_node_mode")) {
        input.lunarNodePolicy.mode = lunarNodeModeFromString(obj.value("lunar_node_mode").toString());
        input.lunarNodePolicy.primary = lunarNodeTypeFromString(obj.value("lunar_node_primary").toString());
        input.useDefaultLunarNodePolicy = obj.value("lunar_node_uses_app_default").toBool(false);
        if (input.useDefaultLunarNodePolicy) {
            input.lunarNodePolicy = defaultLunarNodePolicy_;
        } else if (input.lunarNodePolicy.mode == LunarNodeMode::MeanOnly) {
            input.lunarNodePolicy.primary = LunarNodeType::Mean;
        } else if (input.lunarNodePolicy.mode == LunarNodeMode::TrueOnly) {
            input.lunarNodePolicy.primary = LunarNodeType::True;
        }
    } else {
        // Version 3 and older had no selectable node model. The application
        // default remains Mean for compatibility, but once the user changes
        // that default these legacy charts should follow it instead of being
        // silently pinned to Mean forever.
        input.lunarNodePolicy = defaultLunarNodePolicy_;
        input.useDefaultLunarNodePolicy = true;
    }
    if (!input.date.isValid()) {
        setStatusMessage("Saved chart date is invalid.");
        return false;
    }
    if (!input.time.isValid()) {
        setStatusMessage("Saved chart time is invalid.");
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

    if (!confirmUnsavedChartChanges()) {
        return false;
    }

    // Top-bar zodiac controls are authoritative for all chart calculations.
    applyZodiacToolbarSelection(false);
    input.zodiacSystem = currentInput_.zodiacSystem;
    input.siderealAyanamsa = currentInput_.siderealAyanamsa;

    const QString location = obj.value("location").toString();
    if (!computeChart(input, location)) {
        return false;
    }

    currentProfileName_ = normalized;
    currentChartModified_ = false;
    defaultHouseSystem_ = input.houseSystem;
    refreshProfileToolbar();
    setStatusMessage(QString("Chart \"%1\" loaded.").arg(normalized));
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
        profileToolbarCombo_->addItem("Select saved chart...", QString());
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
    const bool hasSelection = !selected.isEmpty();
    const bool hasSavedCharts = !profiles.isEmpty();
    const bool canSave = hasCurrentChart_
        && (currentChartModified_ || currentProfileName_.trimmed().isEmpty());

    if (profileToolbarNewButton_) {
        profileToolbarNewButton_->setEnabled(true);
    }
    if (profileToolbarManageButton_) {
        profileToolbarManageButton_->setEnabled(true);
    }
    if (profileToolbarLoadButton_) {
        profileToolbarLoadButton_->setEnabled(hasSelection);
    }
    if (profileToolbarDeleteButton_) {
        profileToolbarDeleteButton_->setEnabled(hasSelection);
    }
    if (profileToolbarSaveButton_) {
        profileToolbarSaveButton_->setEnabled(canSave);
    }
    if (profileToolbarEditButton_) {
        profileToolbarEditButton_->setEnabled(hasCurrentChart_);
    }

    if (newChartAction_) {
        newChartAction_->setEnabled(true);
    }
    if (manageChartsAction_) {
        manageChartsAction_->setEnabled(true);
    }
    if (openChartAction_) {
        openChartAction_->setEnabled(hasSavedCharts);
    }
    if (saveChartAction_) {
        saveChartAction_->setEnabled(canSave);
    }
    if (editChartAction_) {
        editChartAction_->setEnabled(hasCurrentChart_);
    }
    if (deleteChartAction_) {
        deleteChartAction_->setEnabled(hasSavedCharts);
    }

    if (profileToolbarStateLabel_) {
        QString stateText;
        QString stateTooltip;
        if (!hasCurrentChart_) {
            stateText = "No chart loaded";
            stateTooltip = "Create a new chart or open one from your saved chart library.";
        } else if (currentProfileName_.trimmed().isEmpty()) {
            stateText = "Current: Unsaved Chart";
            stateTooltip = "This chart has not been saved to your chart library.";
        } else if (currentChartModified_) {
            stateText = QString("Current: %1 - Modified").arg(currentProfileName_);
            stateTooltip = "This chart has unsaved changes.";
        } else {
            stateText = QString("Current: %1 - Saved").arg(currentProfileName_);
            stateTooltip = "This chart is saved and has no pending changes.";
        }
        profileToolbarStateLabel_->setText(stateText);
        profileToolbarStateLabel_->setToolTip(stateTooltip);
    }
}

void MainWindow::syncZodiacToolbarControls() {
    if (!zodiacToolbarTropicalRadio_ || !zodiacToolbarSiderealRadio_ || !zodiacToolbarAyanamsaCombo_) {
        return;
    }
    syncingZodiacToolbar_ = true;
    const QSignalBlocker tropicalBlocker(zodiacToolbarTropicalRadio_);
    const QSignalBlocker siderealBlocker(zodiacToolbarSiderealRadio_);
    const QSignalBlocker ayanamsaBlocker(zodiacToolbarAyanamsaCombo_);

    const bool sidereal = currentInput_.zodiacSystem == ZodiacSystem::Sidereal;
    zodiacToolbarSiderealRadio_->setChecked(sidereal);
    zodiacToolbarTropicalRadio_->setChecked(!sidereal);
    const int idx = zodiacToolbarAyanamsaCombo_->findData(static_cast<int>(currentInput_.siderealAyanamsa));
    zodiacToolbarAyanamsaCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    zodiacToolbarAyanamsaCombo_->setEnabled(sidereal);
    syncingZodiacToolbar_ = false;
}

void MainWindow::syncLunarNodeToolbarControl() {
    if (!nodeSettingsButton_) {
        return;
    }
    const LunarNodePolicy policy = hasCurrentChart_
        ? currentInput_.lunarNodePolicy
        : defaultLunarNodePolicy_;
    QString concise;
    if (policy.mode == LunarNodeMode::Both) {
        concise = QString("Both (%1 primary)").arg(lunarNodeTypeToString(effectivePrimaryNodeType(policy)));
    } else {
        concise = lunarNodeModeToString(policy.mode);
    }
    nodeSettingsButton_->setText(QString("Nodes: %1").arg(concise));
    const QString source = hasCurrentChart_
        ? (currentInput_.useDefaultLunarNodePolicy ? "application default" : "chart override")
        : "application default for new charts";
    nodeSettingsButton_->setToolTip(QString("%1; %2. Click to open Preferences.")
                                        .arg(lunarNodePolicySummary(policy), source));
    if (lunationConjNorthNodeCheck_) {
        lunationConjNorthNodeCheck_->setText(lunarNodeDisplayName("North Node", policy));
        lunationConjNorthNodeCheck_->setToolTip("Uses the current chart's primary lunar-node model.");
    }
    if (lunationConjSouthNodeCheck_) {
        lunationConjSouthNodeCheck_->setText(lunarNodeDisplayName("South Node", policy));
        lunationConjSouthNodeCheck_->setToolTip("Uses the current chart's primary lunar-node model.");
    }
}

void MainWindow::syncLunarNodeResearchSelectionDefaults() {
    const LunarNodePolicy policy = defaultLunarNodePolicy_;
    const QVector<QComboBox*> selectors = {
        searchTransitPlanetCombo_, calendarPlanetCombo_, conjPlanetCombo_, geodeticPlanetCombo_
    };
    for (QComboBox* combo : selectors) {
        auto* model = combo ? qobject_cast<QStandardItemModel*>(combo->model()) : nullptr;
        if (!model) {
            continue;
        }
        const QSignalBlocker blocker(model);
        bool allChecked = true;
        for (int row = 1; row < model->rowCount(); ++row) {
            QStandardItem* item = model->item(row);
            if (!item) {
                allChecked = false;
                continue;
            }
            if (isLunarNodeName(item->text())) {
                const LunarNodeType type = lunarNodeTypeForName(item->text(), LunarNodeType::Mean);
                item->setCheckState(lunarNodePolicyIncludes(policy, type)
                                        ? Qt::Checked : Qt::Unchecked);
            }
            allChecked = allChecked && item->checkState() == Qt::Checked;
        }
        if (QStandardItem* allItem = model->item(0)) {
            allItem->setCheckState(allChecked ? Qt::Checked : Qt::Unchecked);
        }
        updateCheckableComboLabel(combo);
    }
}

void MainWindow::applyZodiacToolbarSelection(bool recomputeIfChartLoaded) {
    if (syncingZodiacToolbar_ || !zodiacToolbarAyanamsaCombo_) {
        return;
    }
    const bool sidereal = zodiacToolbarSiderealRadio_ && zodiacToolbarSiderealRadio_->isChecked();
    const ZodiacSystem selectedSystem = sidereal ? ZodiacSystem::Sidereal : ZodiacSystem::Tropical;
    const SiderealAyanamsa selectedAyanamsa =
        static_cast<SiderealAyanamsa>(zodiacToolbarAyanamsaCombo_->currentData().toInt());

    if (zodiacToolbarAyanamsaCombo_) {
        zodiacToolbarAyanamsaCombo_->setEnabled(sidereal);
    }

    const bool changed =
        currentInput_.zodiacSystem != selectedSystem
        || currentInput_.siderealAyanamsa != selectedAyanamsa;

    currentInput_.zodiacSystem = selectedSystem;
    currentInput_.siderealAyanamsa = selectedAyanamsa;
    updateLunationModeAvailability();

    if (recomputeIfChartLoaded && hasCurrentChart_ && changed) {
        if (computeChart(currentInput_, currentLocation_)) {
            setCurrentChartModified(true);
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
    } else if (activeTab_ == AppTab::LunarReturn) {
        refreshLunarReturnView();
    } else if (activeTab_ == AppTab::Lunations) {
        refreshLunationsTab();
    } else if (activeTab_ == AppTab::Relocation) {
        refreshRelocationView();
    } else if (activeTab_ == AppTab::Transits) {
        refreshTransitsTab();
    } else if (hasCurrentChart_) {
        populateFixedStars(currentChart_);
    }
}



bool MainWindow::openChartSetupDialog(bool newChart) {
    ChartSetupDialog dialog(net_, this);
    dialog.setDefaultHouseSystem(defaultHouseSystem_);
    dialog.setDefaultLunarNodePolicy(defaultLunarNodePolicy_);
    if (!newChart && hasCurrentChart_) {
        dialog.setInput(currentInput_, currentLocation_);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    auto input = dialog.input();
    input.zodiacSystem = currentInput_.zodiacSystem;
    input.siderealAyanamsa = currentInput_.siderealAyanamsa;
    const QString location = dialog.locationName();
    if (!computeChart(input, location)) {
        return false;
    }
    if (newChart) {
        currentProfileName_.clear();
    }
    setCurrentChartModified(true);
    setStatusMessage(newChart ? "Unsaved chart created." : "Chart updated. Save to keep these changes.");
    return true;
}

bool MainWindow::computeChart(const NatalInput& input, const QString& location) {
    if (aspectPeakWorker_) {
        aspectPeakWorker_->setProperty("discardResults", true);
        requestWorkerCancel(aspectPeakWorker_);
    }
    if (ephePath_.isEmpty()) {
        setCriticalMessage("Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.");
        return false;
    }

    NatalInput effectiveInput = input;
    if (effectiveInput.useDefaultLunarNodePolicy) {
        effectiveInput.lunarNodePolicy = defaultLunarNodePolicy_;
    }
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
        // We only need the Placidus house cusps here (for transit house math),
        // so skip the expensive Lots/syzygy, fixed stars and aspect grid.
        TropicalComputeOptions cuspOptions;
        cuspOptions.includeArabicLots = false;
        cuspOptions.includeFixedStars = false;
        cuspOptions.includeAspectGrid = false;
        if (engine_.compute(placidusInput, cuspOptions, &placidusChart, &cuspErr)) {
            natalPlacidusCusps_ = placidusChart.cusps;
        } else {
            natalPlacidusCusps_.clear();
        }
    }
    currentInput_ = effectiveInput;
    currentLocation_ = location;
    currentChart_ = chart;
    hasCurrentChart_ = true;
    transitAspectPeakResults_.clear();
    transitAspectPeakDisplayOrder_.clear();
    hasTransitAspectPeakSelection_ = false;
    refreshWindowTitle();
    if (returnFinderController_) {
        returnFinderController_->setNatalContext(currentInput_, currentChart_, currentLocation_);
    }
    if (planetaryHoursController_) {
        planetaryHoursController_->setNatalContext(currentInput_, currentLocation_);
    }
    if (zodiacalReleasingController_) {
        zodiacalReleasingController_->setNatalContext(
            currentInput_, currentChart_, currentLocation_);
    }
    syncZodiacToolbarControls();
    syncLunarNodeToolbarControl();
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
    syncLunarLocationFromNatal();
    updateLunarLocationAvailability();
    updateLunarTimezoneStatus();
    markLunarPending();
    hasProgressionChart_ = false;
    if (progressionTimezoneEdit_ && !effectiveInput.timezone.isEmpty()) {
        progressionTimezoneEdit_->setText(effectiveInput.timezone);
        updateProgressionTimezoneStatus();
    }
    if (astroProgressionTimezoneEdit_ && !effectiveInput.timezone.isEmpty()) {
        astroProgressionTimezoneEdit_->setText(effectiveInput.timezone);
        updateAstroSourceUi();
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
    } else if (activeTab_ == AppTab::SolarReturn) {
        // A natal recompute (e.g., switching Tropical/Sidereal) must re-derive the
        // solar return in the new zodiac so the chart, technique and finder views
        // all reflect the selected mode instead of staying on the old one.
        if (hasSolarChart_) {
            QString srErr;
            const int srYear = solarYearSpin_ ? solarYearSpin_->value()
                : (currentSolarChart_.localDateTime.isValid()
                       ? currentSolarChart_.localDateTime.date().year()
                       : QDate::currentDate().year());
            if (!applySolarReturnYear(srYear, &srErr)) {
                showSolarPlaceholder();
            }
        } else {
            showSolarPlaceholder();
        }
    } else if (activeTab_ == AppTab::LunarReturn) {
        // Mirror the solar-return behaviour: a natal recompute (e.g. switching
        // Tropical/Sidereal) must re-derive the lunar return in the new zodiac.
        // The return instant itself is invariant under the zodiac mode, and the
        // anchor date is kept on the displayed return's date, so re-finding from
        // that date reproduces the same return recomputed in the new mode.
        if (hasLunarChart_) {
            QString lrErr;
            if (!applyLunarReturnAnchor(+1, true, &lrErr)) {
                showLunarPlaceholder();
            }
        } else {
            showLunarPlaceholder();
        }
    } else if (activeTab_ == AppTab::ReturnFinder) {
        refreshReturnFinderDocks();
    } else if (activeTab_ == AppTab::PlanetaryHours) {
        if (planetaryHoursController_) planetaryHoursController_->refresh();
        refreshPlanetaryHoursDocks();
    } else if (activeTab_ == AppTab::ZodiacalReleasing) {
        refreshZodiacalReleasingDocks();
    } else if (activeTab_ == AppTab::GeodeticEquivalents) {
        refreshGeodeticEquivalentsDocks();
    } else if (activeTab_ == AppTab::Lunations) {
        // A natal recompute (e.g. Tropical/Sidereal switch) changes the displayed
        // moment chart; re-apply the current selection so the wheel/aspects match
        // the new zodiac. (The event list itself should be re-run by the user.)
        if (hasLunationSelection_ && canApplyLunationResult(nullptr)) {
            applyLunationResult(lastLunationSelection_);
        } else {
            refreshLunationsTab();
        }
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
    if (!confirmUnsavedChartChanges()) {
        return;
    }
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

void MainWindow::handlePreferences() {
    PreferencesData current;
    current.lunarNodePolicy = defaultLunarNodePolicy_;
    current.hasCurrentChart = hasCurrentChart_;
    current.currentChartUsesDefaultNodePolicy = !hasCurrentChart_
        || currentInput_.useDefaultLunarNodePolicy;
    current.currentChartNodePolicy = hasCurrentChart_
        ? currentInput_.lunarNodePolicy
        : defaultLunarNodePolicy_;
    current.applyNodePolicyToCurrentChart = hasCurrentChart_;
    current.defaultHouseSystem = defaultHouseSystem_;
    current.aspectOrbs = aspectOrbs_;
    current.zodiacalReleasing = defaultZodiacalReleasingSettings_;
    current.themeMode = static_cast<int>(theme_);
    if (chartWheel_) {
        current.showAspects = chartWheel_->showAspects();
        current.showTicks = chartWheel_->showTicks();
        current.showDegrees = chartWheel_->showDegrees();
        current.showAspectSymbols = chartWheel_->showAspectSymbols();
        current.tickDensity = static_cast<int>(chartWheel_->tickDensity());
        current.fontScale = chartWheel_->fontScale();
    }

    PreferencesDialog dialog(current, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const PreferencesData updated = dialog.preferences();
    const bool nodePolicyChanged = updated.lunarNodePolicy.mode != defaultLunarNodePolicy_.mode
        || updated.lunarNodePolicy.primary != defaultLunarNodePolicy_.primary;
    const bool currentChartWillAdoptDefault = hasCurrentChart_
        && updated.applyNodePolicyToCurrentChart
        && (!currentInput_.useDefaultLunarNodePolicy
            || currentInput_.lunarNodePolicy.mode != updated.lunarNodePolicy.mode
            || currentInput_.lunarNodePolicy.primary != updated.lunarNodePolicy.primary);
    const bool currentChartAlreadyUsesDefault = hasCurrentChart_
        && currentInput_.useDefaultLunarNodePolicy;
    const bool orbsChanged = updated.aspectOrbs.conjunction != aspectOrbs_.conjunction
        || updated.aspectOrbs.sextile != aspectOrbs_.sextile
        || updated.aspectOrbs.square != aspectOrbs_.square
        || updated.aspectOrbs.trine != aspectOrbs_.trine
        || updated.aspectOrbs.opposition != aspectOrbs_.opposition;
    const bool wheelAppearanceChanged = chartWheel_
        && (updated.showAspects != chartWheel_->showAspects()
            || updated.showTicks != chartWheel_->showTicks()
            || updated.showDegrees != chartWheel_->showDegrees()
            || updated.showAspectSymbols != chartWheel_->showAspectSymbols()
            || updated.tickDensity != static_cast<int>(chartWheel_->tickDensity())
            || std::fabs(updated.fontScale - chartWheel_->fontScale()) > 1e-9);

    defaultLunarNodePolicy_ = updated.lunarNodePolicy;
    defaultHouseSystem_ = updated.defaultHouseSystem;
    defaultZodiacalReleasingSettings_ = updated.zodiacalReleasing;
    if (zodiacalReleasingController_) {
        zodiacalReleasingController_->setDefaults(defaultZodiacalReleasingSettings_);
    }
    aspectOrbs_ = updated.aspectOrbs;
    applyTheme(static_cast<ThemeMode>(qBound(0, updated.themeMode, 2)));
    if (wheelAppearanceChanged) {
        chartReadabilityPreset_ = ChartReadabilityPreset::Custom;
    }
    if (chartWheel_) {
        chartWheel_->setShowAspects(updated.showAspects);
        chartWheel_->setShowTicks(updated.showTicks);
        chartWheel_->setShowDegrees(updated.showDegrees);
        chartWheel_->setShowAspectSymbols(updated.showAspectSymbols);
        chartWheel_->setTickDensity(static_cast<ChartWheelWidget::TickDensity>(qBound(0, updated.tickDensity, 2)));
        chartWheel_->setFontScale(updated.fontScale);
    }
    syncAspectOrbQuickControls();
    saveUiState();
    syncLunarNodeToolbarControl();
    if (nodePolicyChanged) {
        syncLunarNodeResearchSelectionDefaults();
    }

    const bool currentChartNeedsNodeRecalculation = currentChartWillAdoptDefault
        || (nodePolicyChanged && currentChartAlreadyUsesDefault);
    if (currentChartNeedsNodeRecalculation || (orbsChanged && hasCurrentChart_)) {
        requestWorkerCancel(searchWorker_);
        requestWorkerCancel(calendarWorker_);
        requestWorkerCancel(conjWorker_);
        requestWorkerCancel(lunationWorker_);
        requestWorkerCancel(scanWorker_);
        if (returnFinderController_) {
            returnFinderController_->cancelSearch();
            returnFinderController_->markResultsStale();
        }
        auto input = currentInput_;
        if (currentChartWillAdoptDefault) {
            input.useDefaultLunarNodePolicy = true;
            input.lunarNodePolicy = defaultLunarNodePolicy_;
        } else if (input.useDefaultLunarNodePolicy) {
            input.lunarNodePolicy = defaultLunarNodePolicy_;
        }
        if (!computeChart(input, currentLocation_)) {
            return;
        }
        if (currentChartWillAdoptDefault) {
            setCurrentChartModified(true);
        }
    }

    if (nodePolicyChanged && hasCurrentChart_ && !updated.applyNodePolicyToCurrentChart
        && !currentInput_.useDefaultLunarNodePolicy) {
        setStatusMessage(QString("Preferences saved. Current chart keeps its %1 override.")
                             .arg(lunarNodePolicySummary(currentInput_.lunarNodePolicy)));
    } else if (currentChartNeedsNodeRecalculation) {
        setStatusMessage(QString("Preferences saved. Current chart now uses %1.")
                             .arg(lunarNodePolicySummary(currentInput_.lunarNodePolicy)));
    } else {
        setStatusMessage("Preferences saved.");
    }
}

void MainWindow::updateTransitWorkspaceLayout() {
    if (!leftSplitter_ || !chartWorkspaceSplitter_ || !aspectsPanel_) {
        return;
    }

    const bool useTransitWorkspace = (activeTab_ == AppTab::Transits);
    if (useTransitWorkspace == transitWorkspaceLayoutActive_) {
        return;
    }

    if (useTransitWorkspace) {
        if (leftSplitter_->count() > 1) {
            const QList<int> sizes = leftSplitter_->sizes();
            if (sizes.size() == 2 && sizes[0] > 0 && sizes[1] > 0) {
                nonTransitLeftSplitterSizes_ = sizes;
            }
        }
        aspectsPanel_->setMinimumHeight(0);
        chartWorkspaceSplitter_->addWidget(aspectsPanel_);
        chartWorkspaceSplitter_->setStretchFactor(0, 1);
        chartWorkspaceSplitter_->setStretchFactor(1, 0);
        chartWorkspaceSplitter_->setSizes({760, 300});
        leftSplitter_->setStretchFactor(0, 1);
    } else {
        aspectsPanel_->setMinimumHeight(220);
        leftSplitter_->addWidget(aspectsPanel_);
        leftSplitter_->setStretchFactor(0, 1);
        leftSplitter_->setStretchFactor(1, 1);
        if (nonTransitLeftSplitterSizes_.size() == 2
            && nonTransitLeftSplitterSizes_[0] > 0
            && nonTransitLeftSplitterSizes_[1] > 0) {
            leftSplitter_->setSizes(nonTransitLeftSplitterSizes_);
        } else {
            leftSplitter_->setSizes({520, 280});
        }
        chartWorkspaceSplitter_->setStretchFactor(0, 1);
    }

    transitWorkspaceLayoutActive_ = useTransitWorkspace;
    updateTransitAspectGridVisibility();
}

void MainWindow::updateTransitAspectGridVisibility() {
    if (!aspectsPanel_ || !chartWorkspaceSplitter_) {
        return;
    }

    const bool inTransitWorkspace = (activeTab_ == AppTab::Transits);
    if (transitAspectGridToggleButton_) {
        transitAspectGridToggleButton_->setVisible(inTransitWorkspace);
        if (transitAspectGridToggleButton_->isChecked() != transitAspectGridVisible_) {
            transitAspectGridToggleButton_->setChecked(transitAspectGridVisible_);
        }
    }
    if (!inTransitWorkspace) {
        return;
    }

    if (!transitAspectGridVisible_) {
        const QList<int> sizes = chartWorkspaceSplitter_->sizes();
        if (sizes.size() == 2 && sizes[0] > 0 && sizes[1] > 0) {
            transitWorkspaceSplitterSizes_ = sizes;
        }
        aspectsPanel_->setVisible(false);
        return;
    }

    aspectsPanel_->setVisible(true);
    if (transitWorkspaceSplitterSizes_.size() == 2
        && transitWorkspaceSplitterSizes_[0] > 0
        && transitWorkspaceSplitterSizes_[1] > 0) {
        chartWorkspaceSplitter_->setSizes(transitWorkspaceSplitterSizes_);
    } else {
        chartWorkspaceSplitter_->setSizes({760, 300});
    }
}

void MainWindow::handleMainTabChanged(int index) {
    activeTab_ = AppTab::Natal;
    if (mainTabBar_ && index >= 0 && index < mainTabBar_->count()) {
        bool ok = false;
        const int value = mainTabBar_->tabData(index).toInt(&ok);
        if (ok && value >= static_cast<int>(AppTab::Natal)
            && value <= static_cast<int>(AppTab::GeodeticEquivalents)) {
            activeTab_ = static_cast<AppTab>(value);
        }
    }
    if (dataStack_) {
        if (activeTab_ == AppTab::Transits) {
            dataStack_->setCurrentIndex(1);
        } else if (activeTab_ == AppTab::Lunations && lunationsDataStackIndex_ >= 0) {
            dataStack_->setCurrentIndex(lunationsDataStackIndex_);
        } else if (activeTab_ == AppTab::Astrocartography && astrocartographyDataStackIndex_ >= 0) {
            dataStack_->setCurrentIndex(astrocartographyDataStackIndex_);
        } else if (activeTab_ == AppTab::ReturnFinder && returnFinderDataStackIndex_ >= 0) {
            dataStack_->setCurrentIndex(returnFinderDataStackIndex_);
        } else if (activeTab_ == AppTab::PlanetaryHours && planetaryHoursDataStackIndex_ >= 0) {
            dataStack_->setCurrentIndex(planetaryHoursDataStackIndex_);
        } else if (activeTab_ == AppTab::ZodiacalReleasing
                   && zodiacalReleasingDataStackIndex_ >= 0) {
            dataStack_->setCurrentIndex(zodiacalReleasingDataStackIndex_);
        } else if (activeTab_ == AppTab::GeodeticEquivalents
                   && geodeticEquivalentsDataStackIndex_ >= 0) {
            dataStack_->setCurrentIndex(geodeticEquivalentsDataStackIndex_);
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
    if (lunarControls_) {
        lunarControls_->setVisible(activeTab_ == AppTab::LunarReturn);
    }
    if (relocationControls_) {
        relocationControls_->setVisible(activeTab_ == AppTab::Relocation);
    }
    if (tabs_) {
        const bool showSolarTools = (activeTab_ == AppTab::SolarReturn);
        auto updateSolarToolTabVisibility = [this, showSolarTools](QWidget* panel) {
            if (!tabs_ || !panel) {
                return;
            }
            const int index = tabs_->indexOf(panel);
            if (index < 0) {
                return;
            }
            tabs_->setTabVisible(index, showSolarTools);
            if (!showSolarTools && tabs_->currentWidget() == panel) {
                tabs_->setCurrentIndex(0);
            }
        };
        updateSolarToolTabVisibility(solarTechniquePanel_);
        if (solarPlacementFinderPanel_) {
            const int finderIndex = tabs_->indexOf(solarPlacementFinderPanel_);
            if (finderIndex >= 0) tabs_->setTabVisible(finderIndex, false);
        }
        if (lunarPlacementFinderPanel_) {
            const int finderIndex = tabs_->indexOf(lunarPlacementFinderPanel_);
            if (finderIndex >= 0) tabs_->setTabVisible(finderIndex, false);
        }
    }

    const bool astroActive = (activeTab_ == AppTab::Astrocartography);
    const bool returnFinderActive = (activeTab_ == AppTab::ReturnFinder);
    const bool planetaryHoursActive = (activeTab_ == AppTab::PlanetaryHours);
    const bool zodiacalReleasingActive = (activeTab_ == AppTab::ZodiacalReleasing);
    const bool geodeticEquivalentsActive =
        (activeTab_ == AppTab::GeodeticEquivalents);
    if (planetaryHoursController_) {
        planetaryHoursController_->setActive(planetaryHoursActive);
    }
    if (zodiacalReleasingController_) {
        zodiacalReleasingController_->setActive(zodiacalReleasingActive);
    }
    if (geodeticEquivalentsController_) {
        geodeticEquivalentsController_->setActive(geodeticEquivalentsActive);
    }
    if (dataDock_) {
        dataDock_->setWindowTitle(returnFinderActive ? "Return Finder Filters"
            : (planetaryHoursActive ? "Planetary Hours Controls"
                : (zodiacalReleasingActive ? "Zodiacal Releasing Controls"
                    : (geodeticEquivalentsActive ? "Geodetic Equivalents Controls"
                        : (activeTab_ == AppTab::Transits ? "Transit Setup" : "Chart Data")))));
    }
    updateTransitWorkspaceLayout();
    if (centerStack_) {
        if (astroActive && worldMapPanel_) {
            centerStack_->setCurrentWidget(worldMapPanel_);
        } else if (returnFinderActive && returnFinderController_) {
            centerStack_->setCurrentWidget(returnFinderController_->workspaceWidget());
        } else if (planetaryHoursActive && planetaryHoursController_) {
            centerStack_->setCurrentWidget(planetaryHoursController_->workspaceWidget());
        } else if (zodiacalReleasingActive && zodiacalReleasingController_) {
            centerStack_->setCurrentWidget(zodiacalReleasingController_->workspaceWidget());
        } else if (geodeticEquivalentsActive && geodeticEquivalentsController_) {
            centerStack_->setCurrentWidget(
                geodeticEquivalentsController_->workspaceWidget());
        } else {
            centerStack_->setCurrentWidget(chartViewPanel_);
        }
    }
    if (chartTitleLabel_) {
        chartTitleLabel_->setText(astroActive ? "Astrocartography Map"
            : (returnFinderActive ? "Return Finder Results"
                : (planetaryHoursActive ? "Planetary Hours"
                    : (zodiacalReleasingActive ? "Zodiacal Releasing Timeline"
                        : (geodeticEquivalentsActive ? "Geodetic Equivalents Map"
                            : "Chart Wheel")))));
    }
    const bool showChartControls = !astroActive && !returnFinderActive
        && !planetaryHoursActive && !zodiacalReleasingActive
        && !geodeticEquivalentsActive;
    if (chartSettingsButton_) {
        chartSettingsButton_->setVisible(showChartControls);
    }
    if (transitAspectGridToggleButton_) {
        transitAspectGridToggleButton_->setVisible(
            showChartControls && activeTab_ == AppTab::Transits);
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
        aspectsPanel_->setVisible(!astroActive && !returnFinderActive
            && !planetaryHoursActive && !zodiacalReleasingActive
            && !geodeticEquivalentsActive
            && (activeTab_ != AppTab::Transits || transitAspectGridVisible_));
    }
    updateTransitAspectGridVisibility();
    updateTransitListFilterVisibility();

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
        refreshSolarPlacementFinderView();
    } else if (activeTab_ == AppTab::LunarReturn) {
        updateLunarReturnDockTitles();
        updateLunarLocationAvailability();
        syncLunarLocationFromNatal();
        updateLunarTimezoneStatus();
        updateLunarStatusLabels();
        refreshLunarReturnView();
        refreshLunarPlacementFinderView();
    } else if (activeTab_ == AppTab::ReturnFinder) {
        if (rightTopDock_) rightTopDock_->setWindowTitle("Return Finder Details");
        if (rightBottomDock_) rightBottomDock_->setWindowTitle("Search Summary");
        refreshReturnFinderDocks();
    } else if (activeTab_ == AppTab::PlanetaryHours) {
        if (rightTopDock_) rightTopDock_->setWindowTitle("Selected Planetary Hour");
        if (rightBottomDock_) rightBottomDock_->setWindowTitle("Planetary Day Summary");
        refreshPlanetaryHoursDocks();
    } else if (activeTab_ == AppTab::ZodiacalReleasing) {
        if (rightTopDock_) rightTopDock_->setWindowTitle("Releasing Period Details");
        if (rightBottomDock_) rightBottomDock_->setWindowTitle("Releasing Method Summary");
        refreshZodiacalReleasingDocks();
    } else if (activeTab_ == AppTab::GeodeticEquivalents) {
        if (rightTopDock_) {
            rightTopDock_->setWindowTitle("Selected Geodetic Location");
        }
        if (rightBottomDock_) {
            rightBottomDock_->setWindowTitle("Geodetic Transit Summary");
        }
        refreshGeodeticEquivalentsDocks();
    } else if (activeTab_ == AppTab::Lunations) {
        if (rightTopDock_) {
            rightTopDock_->setWindowTitle("Lunations");
        }
        if (rightBottomDock_) {
            rightBottomDock_->setWindowTitle("Lunation Details");
        }
        refreshLunationsTab();
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
    } else if (activeTab_ == AppTab::Astrocartography) {
        if (rightTopDock_) {
            rightTopDock_->setWindowTitle("Astrocartography Lines");
        }
        if (rightBottomDock_) {
            rightBottomDock_->setWindowTitle("Map Details");
        }
        updateAstrocartographyModeUi();
        updateAstrocartographyView();
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
    syncAspectOrbQuickControls();
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

NatalChart MainWindow::natalChartForTransitDisplay() const {
    NatalChart displayChart = currentChart_;
    if (transitHouseSystem_ == HouseSystem::Placidus) {
        if (natalPlacidusCusps_.size() == 12) {
            displayChart.cusps = natalPlacidusCusps_;
        } else {
            displayChart.cusps.clear();
        }
    }
    return displayChart;
}

void MainWindow::applyTransitHouseSystem(HouseSystem system) {
    auto syncControls = [this](HouseSystem selected) {
        if (transitWholeRadio_) {
            const QSignalBlocker blocker(transitWholeRadio_);
            transitWholeRadio_->setChecked(selected == HouseSystem::WholeSign);
        }
        if (transitPlacidusRadio_) {
            const QSignalBlocker blocker(transitPlacidusRadio_);
            transitPlacidusRadio_->setChecked(selected == HouseSystem::Placidus);
        }
    };

    if (system == HouseSystem::Placidus
        && transitMode_ == TransitMode::NatalOverlay
        && hasCurrentChart_
        && natalPlacidusCusps_.size() != 12) {
        syncControls(transitHouseSystem_);
        if (statusBar()) {
            statusBar()->showMessage(
                "Placidus cusps are unavailable for the loaded natal chart.", 8000);
        }
        return;
    }

    transitHouseSystem_ = system;
    syncControls(system);
    refreshTransitsTab();
    updateTransitSearchTargets();
    if (transitSubTab_ == TransitSubTab::Calendar
        && calendarIncludeHousesCheck_
        && calendarIncludeHousesCheck_->isChecked()) {
        handleTransitCalendarRun();
    }
    if (transitSubTab_ == TransitSubTab::Conjunctions
        && conjBucketHouseRadio_
        && conjBucketHouseRadio_->isChecked()) {
        handleTransitConjunctionRun();
    }
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
        if (isSolarTechniqueTabActive() || isSolarPlacementFinderTabActive()) {
            return;
        }
        if (index < 0 || index > 1) {
            return;
        }
        solarAspectView_ = static_cast<SolarAspectView>(index);
        refreshSolarReturnView();
        return;
    }
    if (activeTab_ == AppTab::LunarReturn) {
        if (index < 0 || index > 1) {
            return;
        }
        lunarAspectView_ = static_cast<LunarAspectView>(index);
        refreshLunarReturnView();
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
        } else if (hasTransitChart_ && !transitPending_) {
            // A view switch only changes which aspect grid is shown — re-render
            // from the cached transit chart instead of recomputing it.
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
        transitSubTab_ = TransitSubTab::AspectPeaks;
    } else if (index == 3) {
        transitSubTab_ = TransitSubTab::Calendar;
    } else if (index == 4) {
        transitSubTab_ = TransitSubTab::Conjunctions;
    } else if (index == 5) {
        transitSubTab_ = TransitSubTab::Scan;
    } else if (index == 6) {
        transitSubTab_ = TransitSubTab::Profections;
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
            case TransitSubTab::AspectPeaks:
                stackIndex = 5;
                break;
            case TransitSubTab::Profections:
                stackIndex = 6;
                break;
            case TransitSubTab::Lunations:
                // Lunations is now its own main tab; this sub-tab value is no
                // longer reachable here. Keep a safe fallback for switch coverage.
                stackIndex = 0;
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
    updateTransitListFilterVisibility();
    updateTransitSearchTargets();
    updateTransitSearchVisibility();
}

void MainWindow::handleTransitSearchRun() {
    runTransitSearch();
}

void MainWindow::scheduleTransitSearchResultsRefresh() {
    searchResultsDirty_ = true;
    if (!searchResultsRefreshTimer_) {
        return;
    }
    if (!searchResultsRefreshTimer_->isActive()) {
        searchResultsRefreshTimer_->start();
    }
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
    params.zodiacSystem = currentInput_.zodiacSystem;
    params.siderealAyanamsa = currentInput_.siderealAyanamsa;
    params.lunarNodePolicy = currentInput_.lunarNodePolicy;
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
            chartWheel_->setOverlayCharts(natalChartForTransitDisplay(), chart, transitHouseSystem_, aspectOrbs_);
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
    const bool includeMoon = !(conjIncludeMoonCheck_ && !conjIncludeMoonCheck_->isChecked());
    if (!includeMoon) {
        planets.removeAll("Moon");
    }
    if (planets.isEmpty()) {
        setStatusMessage(includeMoon
            ? "Select at least one conjunction planet."
            : "Select at least one conjunction planet after Moon exclusion.");
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
    const bool uniqueFirstOnly = conjUniqueFirstCheck_ && conjUniqueFirstCheck_->isChecked();
    double uniqueDegreeStep = 1.0;
    if (conjUniqueDegreeCombo_) {
        const double uiStep = conjUniqueDegreeCombo_->currentData().toDouble();
        if (uiStep > 0.0) {
            uniqueDegreeStep = uiStep;
        }
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
    if (uniqueFirstOnly && conjFindMode_ != ConjunctionFindMode::Range) {
        setStatusMessage("First-time unique conjunctions require Year Range mode.");
        if (conjStatusLabel_) {
            conjStatusLabel_->setText("Set Year Range");
        }
        return;
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
    const QDateTime requestedStartUtc = startUtc;
    const QDateTime requestedEndUtc = endUtc;
    if (uniqueFirstOnly) {
        const QDateTime uniqueScanStartLocal(QDate(1800, 1, 1), QTime(0, 0, 0), tz);
        if (!uniqueScanStartLocal.isValid()) {
            setStatusMessage("Invalid unique conjunction scan start.");
            return;
        }
        startUtc = uniqueScanStartLocal.toUTC();
        endUtc = requestedEndUtc;
    }

    ConjunctionParams params;
    params.startUtc = startUtc;
    params.endUtc = endUtc;
    params.tz = tz;
    params.tzLabel = tzLabel;
    params.ephePath = ephePath_;
    params.dllSearchPaths = sweSearchPaths();
    params.zodiacSystem = currentInput_.zodiacSystem;
    params.siderealAyanamsa = currentInput_.siderealAyanamsa;
    params.lunarNodePolicy = currentInput_.lunarNodePolicy;
    params.planetNames = planets;
    params.minCount = minCount;
    const bool exactPairMode = (minCount == 2 && planets.size() == 2);
    params.useOrb = !exactPairMode && (conjUseOrbCheck_ && conjUseOrbCheck_->isChecked());
    params.orbDeg = params.useOrb && conjOrbSpin_ ? conjOrbSpin_->value() : 0.0;
    params.bucketByHouse = bucketByHouse;
    params.houseSystem = transitHouseSystem_;
    params.uniqueFirstOnly = uniqueFirstOnly;
    params.uniqueDegreeStep = std::max(0.01, uniqueDegreeStep);
    params.uniqueWindowStartUtc = requestedStartUtc;
    params.uniqueWindowEndUtc = requestedEndUtc;
    if (bucketByHouse) {
        params.natalAsc = currentChart_.angles.asc;
        params.natalCusps.reserve(natalPlacidusCusps_.size());
        for (const auto& cusp : natalPlacidusCusps_) {
            params.natalCusps.push_back(normalizeDegrees(cusp.longitude));
        }
    }

    conjLastRunUniqueFirst_ = uniqueFirstOnly;
    conjLastRunIncludeMoon_ = includeMoon;
    conjLastRunUniqueDegreeStep_ = params.uniqueDegreeStep;
    transitConjunctionResults_.clear();
    transitConjunctionDisplayOrder_.clear();
    hasTransitConjunctionSelection_ = false;
    conjRestartPending_ = false;
    conjAutoApplied_ = false;
    conjRunning_ = true;
    if (conjStatusLabel_) {
        conjStatusLabel_->setText(uniqueFirstOnly ? "Computing unique-first..." : "Computing...");
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
            chartWheel_->setOverlayCharts(natalChartForTransitDisplay(), chart, transitHouseSystem_, aspectOrbs_);
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
            chartWheel_->setOverlayCharts(natalChartForTransitDisplay(), chart, transitHouseSystem_, aspectOrbs_);
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
    if (activeTab_ == AppTab::LunarReturn && !hasLunarChart_) {
        setStatusMessage("Calculate a lunar return first to copy aspects.");
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
    QString text;
    QString successMessage;
    const bool solarReport = activeTab_ == AppTab::SolarReturn;
    if (solarReport) {
        if (!hasCurrentChart_ || !hasSolarChart_) {
            setStatusMessage("Calculate a Solar Return before copying its report.");
            return;
        }
        if (solarPending_) {
            setStatusMessage("Solar Return inputs changed. Recalculate before copying the report.");
            return;
        }
        text = buildSolarReturnReportMarkdown();
        successMessage = "Solar Return Markdown report copied to clipboard.";
    } else {
        if (!hasCurrentChart_) {
            setStatusMessage("Load a natal chart to generate the report.");
            return;
        }
        text = buildNatalReportText();
        successMessage = "Natal report copied to clipboard.";
    }
    if (text.isEmpty()) {
        setStatusMessage("No report content to copy.");
        return;
    }
    if (auto* clipboard = QApplication::clipboard()) {
        auto* mimeData = new QMimeData();
        mimeData->setText(text);
        if (solarReport) {
            mimeData->setData("text/markdown", text.toUtf8());
        }
        clipboard->setMimeData(mimeData);
    }
    setStatusMessage(successMessage);
}

void MainWindow::applySolarReportBasicPreset() {
    solarReportOptions_ = SolarReportOptions{};
    updateSolarReportOptionsUi();
    refreshNatalReport();
    setStatusMessage("Solar Return report set to Basic.");
}

void MainWindow::applySolarReportFullPreset() {
    solarReportOptions_.preset = SolarReportPreset::Full;
    solarReportOptions_.lotScope = SolarReportLotScope::All;
    solarReportOptions_.aspectScope = SolarReportAspectScope::Configured;
    solarReportOptions_.includeAnnualProfection = true;
    solarReportOptions_.includeNatalPositions = true;
    solarReportOptions_.includeSolarPositions = true;
    solarReportOptions_.includeHouseCusps = true;
    solarReportOptions_.includeHouseOverlays = true;
    solarReportOptions_.includeSolarNatalAspects = true;
    solarReportOptions_.includeSolarSolarAspects = true;
    solarReportOptions_.includeNatalNatalAspects = true;
    solarReportOptions_.includeMinorBodies = true;
    solarReportOptions_.includeDailyMotion = true;
    solarReportOptions_.includeDignities = true;
    solarReportOptions_.includeFixedStars = true;
    updateSolarReportOptionsUi();
    refreshNatalReport();
    setStatusMessage("Solar Return report set to Full.");
}

void MainWindow::markSolarReportOptionsCustom() {
    solarReportOptions_.preset = SolarReportPreset::Custom;
    updateSolarReportOptionsUi();
    refreshNatalReport();
    setStatusMessage("Solar Return report options updated.");
}

void MainWindow::updateSolarReportOptionsUi() {
    if (reportOptionsButton_) {
        QString label = "Custom";
        if (solarReportOptions_.preset == SolarReportPreset::Basic) {
            label = "Basic";
        } else if (solarReportOptions_.preset == SolarReportPreset::Full) {
            label = "Full";
        }
        reportOptionsButton_->setText(QString("Report: %1").arg(label));
    }
    if (solarReportAnnualProfectionAction_) {
        solarReportAnnualProfectionAction_->setChecked(solarReportOptions_.includeAnnualProfection);
    }
    if (solarReportNatalPositionsAction_) {
        solarReportNatalPositionsAction_->setChecked(solarReportOptions_.includeNatalPositions);
    }
    if (solarReportSolarPositionsAction_) {
        solarReportSolarPositionsAction_->setChecked(solarReportOptions_.includeSolarPositions);
    }
    if (solarReportHouseCuspsAction_) {
        solarReportHouseCuspsAction_->setChecked(solarReportOptions_.includeHouseCusps);
    }
    if (solarReportHouseOverlaysAction_) {
        solarReportHouseOverlaysAction_->setChecked(solarReportOptions_.includeHouseOverlays);
    }
    if (solarReportSolarNatalAspectsAction_) {
        solarReportSolarNatalAspectsAction_->setChecked(solarReportOptions_.includeSolarNatalAspects);
    }
    if (solarReportSolarSolarAspectsAction_) {
        solarReportSolarSolarAspectsAction_->setChecked(solarReportOptions_.includeSolarSolarAspects);
    }
    if (solarReportNatalNatalAspectsAction_) {
        solarReportNatalNatalAspectsAction_->setChecked(solarReportOptions_.includeNatalNatalAspects);
    }
    if (solarReportMinorBodiesAction_) {
        solarReportMinorBodiesAction_->setChecked(solarReportOptions_.includeMinorBodies);
    }
    if (solarReportDailyMotionAction_) {
        solarReportDailyMotionAction_->setChecked(solarReportOptions_.includeDailyMotion);
    }
    if (solarReportDignitiesAction_) {
        solarReportDignitiesAction_->setChecked(solarReportOptions_.includeDignities);
    }
    if (solarReportFixedStarsAction_) {
        solarReportFixedStarsAction_->setChecked(solarReportOptions_.includeFixedStars);
    }
    if (solarReportNoLotsAction_) {
        solarReportNoLotsAction_->setChecked(solarReportOptions_.lotScope == SolarReportLotScope::None);
    }
    if (solarReportCoreLotsAction_) {
        solarReportCoreLotsAction_->setChecked(solarReportOptions_.lotScope == SolarReportLotScope::Core);
    }
    if (solarReportAllLotsAction_) {
        solarReportAllLotsAction_->setChecked(solarReportOptions_.lotScope == SolarReportLotScope::All);
    }
    if (solarReportTightAspectsAction_) {
        solarReportTightAspectsAction_->setChecked(
            solarReportOptions_.aspectScope == SolarReportAspectScope::Tight);
    }
    if (solarReportStandardAspectsAction_) {
        solarReportStandardAspectsAction_->setChecked(
            solarReportOptions_.aspectScope == SolarReportAspectScope::Standard);
    }
    if (solarReportConfiguredAspectsAction_) {
        solarReportConfiguredAspectsAction_->setChecked(
            solarReportOptions_.aspectScope == SolarReportAspectScope::Configured);
    }
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
    const bool inAspectPeaks = (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::AspectPeaks);
    if (inAspectPeaks) {
        rightBottomCopyButton_->setText("Copy Aspect Peak Report (Markdown)");
        rightBottomCopyButton_->setVisible(true);
        rightBottomCopyButton_->setEnabled(hasTransitAspectPeakSelection_ && hasTransitChart_);
        return;
    }
    const bool inLunations = inLunationsView();
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
            .arg(lunarNodeDisplayName(body.name, chart.lunarNodePolicy), degree, sign, house, motion);
    };
    for (const auto& name : bodyOrderForLunarNodePolicy(currentTransitChart_.lunarNodePolicy)) {
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
    if (result.uniqueFirstOccurrence) {
        lines << "First-Time Unique: Yes";
        lines << QString("Unique Signature: %1").arg(result.uniqueSignature.isEmpty() ? "-" : result.uniqueSignature);
    }
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
            .arg(lunarNodeDisplayName(body.name, chart.lunarNodePolicy), degree, sign, house, motion);
    };
    for (const auto& name : bodyOrderForLunarNodePolicy(currentTransitChart_.lunarNodePolicy)) {
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
    for (const auto& name : bodyOrderForLunarNodePolicy(chart.lunarNodePolicy)) {
        if (!bodyMap.contains(name)) {
            continue;
        }
        const auto body = bodyMap.value(name);
        const QString motion = body.retrograde ? " R" : "";
        const QString house = body.house > 0 ? QString(" (H%1%2)").arg(body.house).arg(motion)
                                             : (motion.isEmpty() ? QString() : QString(" (%1)").arg(motion.trimmed()));
        lines << QString("%1: %2%3")
            .arg(lunarNodeDisplayName(body.name, chart.lunarNodePolicy),
                 formatDegInSign(body.longitude), house);
        bodyMap.remove(name);
    }
    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        const auto& body = it.value();
        const QString motion = body.retrograde ? " R" : "";
        const QString house = body.house > 0 ? QString(" (H%1%2)").arg(body.house).arg(motion)
                                             : (motion.isEmpty() ? QString() : QString(" (%1)").arg(motion.trimmed()));
        lines << QString("%1: %2%3")
            .arg(lunarNodeDisplayName(body.name, chart.lunarNodePolicy),
                 formatDegInSign(body.longitude), house);
    }

    return lines.join("\n");
}

void MainWindow::refreshNatalReport() {
    if (!reportText_) {
        return;
    }
    auto setReportPreview = [this](const QString& text) {
        reportText_->setPlainText(text);
        reportText_->moveCursor(QTextCursor::Start);
        if (auto* verticalBar = reportText_->verticalScrollBar()) {
            verticalBar->setValue(verticalBar->minimum());
        }
        if (auto* horizontalBar = reportText_->horizontalScrollBar()) {
            horizontalBar->setValue(horizontalBar->minimum());
        }
    };
    if (activeTab_ == AppTab::SolarReturn) {
        if (reportOptionsButton_) {
            reportOptionsButton_->setVisible(true);
            reportOptionsButton_->setEnabled(true);
        }
        if (reportCopyButton_) {
            reportCopyButton_->setText("Copy Solar Return Report");
            reportCopyButton_->setToolTip(
                "Copy the Solar Return Markdown report using the selected report options.");
            reportCopyButton_->setEnabled(
                hasCurrentChart_ && hasSolarChart_ && !solarPending_);
        }
        if (!hasCurrentChart_ || !hasSolarChart_) {
            setReportPreview(
                "Calculate a Solar Return to prepare its report. You may configure "
                "the report options before calculating.");
            return;
        }
        if (solarPending_) {
            setReportPreview(
                "Solar Return inputs have changed. Recalculate the Solar Return "
                "before copying its report.");
            return;
        }
        const int year = currentSolarChart_.localDateTime.isValid()
            ? currentSolarChart_.localDateTime.date().year()
            : currentSolarInput_.date.year();
        const QString natalName = currentInput_.name.trimmed().isEmpty()
            ? QString("Untitled")
            : currentInput_.name.trimmed();
        QString preset = "Custom";
        if (solarReportOptions_.preset == SolarReportPreset::Basic) {
            preset = "Basic";
        } else if (solarReportOptions_.preset == SolarReportPreset::Full) {
            preset = "Full";
        }
        QString lots = "No Arabic Lots";
        if (solarReportOptions_.lotScope == SolarReportLotScope::Core) {
            lots = "Core Lots (Fortune, Spirit, Eros)";
        } else if (solarReportOptions_.lotScope == SolarReportLotScope::All) {
            lots = "All calculated Arabic Lots";
        }
        QString aspectScope = "Configured aspect orbs";
        if (solarReportOptions_.aspectScope == SolarReportAspectScope::Tight) {
            aspectScope = "Tight aspects (maximum 3 degrees)";
        } else if (solarReportOptions_.aspectScope == SolarReportAspectScope::Standard) {
            aspectScope = "Standard aspects (maximum 6 degrees)";
        }
        QStringList sections;
        if (solarReportOptions_.includeAnnualProfection) sections.push_back("annual profection");
        if (solarReportOptions_.includeNatalPositions) sections.push_back("natal positions");
        if (solarReportOptions_.includeSolarPositions) sections.push_back("Solar Return positions");
        if (solarReportOptions_.includeHouseCusps) sections.push_back("house cusps");
        if (solarReportOptions_.includeHouseOverlays) sections.push_back("house overlays");
        if (solarReportOptions_.includeSolarNatalAspects) sections.push_back("Solar Return-Natal aspects");
        if (solarReportOptions_.includeSolarSolarAspects) sections.push_back("Solar Return-Solar Return aspects");
        if (solarReportOptions_.includeNatalNatalAspects) sections.push_back("Natal-Natal aspects");
        if (solarReportOptions_.includeFixedStars) sections.push_back("fixed stars");
        if (sections.isEmpty()) sections.push_back("core event and calculation data only");

        setReportPreview(QString(
            "Solar Return Report - %1\n\n"
            "Natal chart: %2\n"
            "Exact return: %3 (%4)\n"
            "Return location: %5\n\n"
            "Report preset: %6\n"
            "Aspect scope: %7\n"
            "Lot scope: %8\n"
            "Minor bodies: %9\n"
            "Included sections: %10\n\n"
            "Use Report Options to customize the export, then click Copy Solar "
            "Return Report to copy the Markdown report.")
            .arg(year)
            .arg(natalName)
            .arg(currentSolarChart_.localDateTime.toString(
                "dddd, d MMMM yyyy, h:mm:ss AP"))
            .arg(currentSolarChart_.timezoneLabel)
            .arg(currentSolarLocation_.isEmpty() ? QString("-") : currentSolarLocation_)
            .arg(preset)
            .arg(aspectScope)
            .arg(lots)
            .arg(solarReportOptions_.includeMinorBodies ? "Included" : "Excluded")
            .arg(sections.join(", ")));
        return;
    }
    if (reportOptionsButton_) {
        reportOptionsButton_->setVisible(false);
    }
    if (reportCopyButton_) {
        reportCopyButton_->setText("Copy Report");
        reportCopyButton_->setToolTip(QString());
        reportCopyButton_->setEnabled(hasCurrentChart_);
    }
    if (!hasCurrentChart_) {
        reportText_->clear();
        return;
    }
    setReportPreview(buildNatalReportText());
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
    addRow({"Mode", zodiacModeSummary(input)});
    addRow({"Lunar nodes", lunarNodePolicySummary(chart.lunarNodePolicy)});
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
    for (const auto& name : bodyOrderForLunarNodePolicy(chart.lunarNodePolicy)) {
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
            lunarNodeDisplayName(body.name, chart.lunarNodePolicy),
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
    const QString prefixLunar = QString::fromUtf8(u8"ʟᴜɴᴀʀ");
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
    } else if (activeTab_ == AppTab::LunarReturn) {
        if (lunarAspectView_ == LunarAspectView::LunarNatal) {
            contextLabel = "Lunar Return (Lunar-Natal)";
        } else {
            contextLabel = "Lunar Return";
        }
        contextHouseSystem = currentLunarInput_.houseSystem;
        if (!currentLunarLocation_.isEmpty()) {
            lines << QString("Lunar location: %1").arg(currentLunarLocation_);
        }
        if (hasLunarChart_) {
            lines << QString("Lunar return time: %1 (%2)")
                .arg(currentLunarChart_.localDateTime.toString("yyyy-MM-dd HH:mm:ss"))
                .arg(currentLunarChart_.timezoneLabel);
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
        QStringList matrixOrder = bodyOrderForLunarNodePolicy(rowChart.lunarNodePolicy);
        for (const auto& name : bodyOrderForLunarNodePolicy(colChart.lunarNodePolicy)) {
            if (!matrixOrder.contains(name)) {
                matrixOrder.push_back(name);
            }
        }
        for (const auto& name : matrixOrder) {
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
    } else if (activeTab_ == AppTab::LunarReturn) {
        if (lunarAspectView_ == LunarAspectView::LunarNatal) {
            appendOverlayMatrix(currentLunarChart_, currentChart_, false, prefixLunar, prefixNatal);
        } else {
            appendChartMatrix(currentLunarChart_, prefixLunar);
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
    TropicalComputeOptions options;
    options.includeArabicLots = false;
    options.includeFixedStars = false;
    options.includeAspectGrid = false;
    if (!computeTransitChart(result.timeLocal, result.tzLabel, options, &chart, &err)) {
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

    if (chartWheel_) {
        if (transitMode_ == TransitMode::NatalOverlay && hasCurrentChart_) {
            chartWheel_->setShowAspects(true);
            chartWheel_->setOverlayLabel("Transit");
            chartWheel_->setOverlayCharts(natalChartForTransitDisplay(), chart, transitHouseSystem_, aspectOrbs_);
            chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
            populateOverlayAspects(chart);
        } else {
            chartWheel_->setTransitChart(chart, transitHouseSystem_);
            populateAspects(chart);
        }
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        chartWheel_->setHighlight(result.planet, true, result.event, QColor("#f0c24b"));
    } else if (transitMode_ == TransitMode::NatalOverlay && hasCurrentChart_) {
        populateOverlayAspects(chart);
    } else {
        populateAspects(chart);
    }
    showTransitSearchDetails(result);
}

void MainWindow::updateTransitSearchVisibility() {
    if (activeTab_ != AppTab::Transits) {
        if (transitAspectsDock_) {
            transitAspectsDock_->setVisible(false);
        }
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
    const bool inAspectPeaks = (transitSubTab_ == TransitSubTab::AspectPeaks);
    const bool inProfections = (transitSubTab_ == TransitSubTab::Profections);
    const bool inLunations = (transitSubTab_ == TransitSubTab::Lunations);
    const bool inOverview = (transitSubTab_ == TransitSubTab::Overview);
    if (transitAspectsDock_) {
        transitAspectsDock_->setVisible(inOverview);
        transitAspectsDock_->setWindowTitle("Aspects in Effect");
    }
    if (inOverview && rightTopDock_ && transitAspectsDock_ && rightBottomDock_) {
        resizeDocks({rightTopDock_, transitAspectsDock_, rightBottomDock_},
                    {360, 270, 250}, Qt::Vertical);
    }
    if (rightTopDock_) {
        if (inSearch) {
            rightTopDock_->setWindowTitle("Search Results");
        } else if (inCalendar) {
            rightTopDock_->setWindowTitle("Calendar Events");
        } else if (inConjunctions) {
            rightTopDock_->setWindowTitle("Conjunction Results");
        } else if (inScan) {
            rightTopDock_->setWindowTitle("Scan Results");
        } else if (inAspectPeaks) {
            rightTopDock_->setWindowTitle("Aspect Peak Results");
        } else if (inProfections) {
            rightTopDock_->setWindowTitle("Activated Points");
        } else if (inLunations) {
            rightTopDock_->setWindowTitle("Lunation Results");
        } else {
            rightTopDock_->setWindowTitle("Transit Positions");
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
        } else if (inAspectPeaks) {
            rightBottomDock_->setWindowTitle("Peak Aspect Details");
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
    } else if (inAspectPeaks) {
        refreshTransitAspectPeakTab();
    } else if (inProfections) {
        refreshTransitProfectionTab();
    } else if (inLunations) {
        showLunationResults();
    } else if (activeTab_ == AppTab::Transits) {
        refreshTransitsTab();
    }
    if (!inSearch && !inCalendar && !inConjunctions && !inAspectPeaks && chartWheel_) {
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

// Astrocartography map update helpers.
void MainWindow::updateAstrocartographyModeUi() {
    if (geodeticGroup_) {
        geodeticGroup_->setEnabled(true);
    }
    updateAstroSourceUi();
    if (geodeticTimeLabel_) {
        if (hasAstroSourceChart_) {
            geodeticTimeLabel_->setText(astroSourceLabel_);
        } else if (hasCurrentChart_) {
            geodeticTimeLabel_->setText("Choose a source, then refresh the map.");
        } else {
            geodeticTimeLabel_->setText("Load a natal chart to draw astrocartography lines.");
        }
    }
}

MainWindow::AstroSourceMode MainWindow::astroSourceMode() const {
    if (astroSourceCombo_) {
        const int value = astroSourceCombo_->currentData().toInt();
        if (value == static_cast<int>(AstroSourceMode::ProgressedNow)) {
            return AstroSourceMode::ProgressedNow;
        }
        if (value == static_cast<int>(AstroSourceMode::ProgressedCustom)) {
            return AstroSourceMode::ProgressedCustom;
        }
    }
    return AstroSourceMode::Natal;
}

void MainWindow::updateAstroSourceUi() {
    const AstroSourceMode mode = astroSourceMode();
    const bool progressed = (mode != AstroSourceMode::Natal);
    const bool custom = (mode == AstroSourceMode::ProgressedCustom);

    if (astroProgressionTargetLabel_) {
        astroProgressionTargetLabel_->setEnabled(progressed);
    }
    if (astroProgressionDateEdit_) {
        astroProgressionDateEdit_->setEnabled(custom);
    }
    if (astroProgressionTimeEdit_) {
        astroProgressionTimeEdit_->setEnabled(custom);
    }
    if (astroProgressionTimezoneEdit_) {
        astroProgressionTimezoneEdit_->setEnabled(progressed);
    }
    if (astroProgressionNowButton_) {
        astroProgressionNowButton_->setEnabled(custom);
    }
    if (astroProgressionTimezoneStatus_) {
        if (!progressed) {
            astroProgressionTimezoneStatus_->setText("-");
            astroProgressionTimezoneStatus_->setStyleSheet(QString());
        } else {
            QTimeZone tz;
            QString label;
            QString err;
            const QString tzText = astroProgressionTimezoneEdit_
                ? astroProgressionTimezoneEdit_->text().trimmed()
                : (currentInput_.timezone.isEmpty() ? QString("UTC") : currentInput_.timezone);
            if (parseTimezoneInput(tzText, &tz, &label, &err)) {
                astroProgressionTimezoneStatus_->setText("OK");
                astroProgressionTimezoneStatus_->setStyleSheet("color: #69c36d;");
            } else {
                astroProgressionTimezoneStatus_->setText("Invalid");
                astroProgressionTimezoneStatus_->setStyleSheet("color: #e05555;");
            }
        }
    }
}

void MainWindow::handleAstroProgressionNow() {
    if (!astroProgressionDateEdit_ || !astroProgressionTimeEdit_) {
        return;
    }
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = astroProgressionTimezoneEdit_
        ? astroProgressionTimezoneEdit_->text().trimmed()
        : (currentInput_.timezone.isEmpty() ? QString("UTC") : currentInput_.timezone);
    if (!parseTimezoneInput(tzText, &tz, &label, &err)) {
        tz = QTimeZone::utc();
        label = "UTC";
    }
    const QDateTime nowLocal = QDateTime::currentDateTimeUtc().toTimeZone(tz);
    const QSignalBlocker blockDate(astroProgressionDateEdit_);
    const QSignalBlocker blockTime(astroProgressionTimeEdit_);
    astroProgressionDateEdit_->setDate(nowLocal.date());
    astroProgressionTimeEdit_->setTime(nowLocal.time());
    if (astroProgressionTimezoneEdit_) {
        const QSignalBlocker blockTz(astroProgressionTimezoneEdit_);
        astroProgressionTimezoneEdit_->setText(label);
    }
    if (astroSourceCombo_) {
        const int idx = astroSourceCombo_->findData(static_cast<int>(AstroSourceMode::ProgressedCustom));
        if (idx >= 0) {
            const QSignalBlocker blockSource(astroSourceCombo_);
            astroSourceCombo_->setCurrentIndex(idx);
        }
    }
    updateAstroSourceUi();
    updateAstrocartographyView();
}

bool MainWindow::computeProgressionChartForInput(const NatalInput& input, const QDateTime& localTime,
                                                 const QString& tzLabel, NatalChart* out, QString* error) {
    if (!hasCurrentChart_) {
        if (error) {
            *error = "Load a natal chart first to compute progressions.";
        }
        return false;
    }
    if (!out) {
        return false;
    }
    NatalInput progressionInput = input;
    progressionInput.aspectOrbs = aspectOrbs_;
    const bool ok = progressionEngine_.compute(progressionInput, localTime, tzLabel, out, error);
    if (ok && out && !out->warnings.isEmpty() && statusBar()) {
        statusBar()->showMessage(QString("Computed with warnings: %1").arg(out->warnings.join("; ")), 12000);
    }
    return ok;
}

bool MainWindow::computeAstroSourceChart(double latitude, double longitude, HouseSystem houseSystem,
                                         NatalChart* outChart, NatalInput* outInput,
                                         QString* outSourceLabel, QString* error) {
    if (!hasCurrentChart_) {
        if (error) {
            *error = "Load a natal chart first.";
        }
        return false;
    }
    if (!outChart) {
        return false;
    }

    NatalInput input = currentInput_;
    input.latitude = latitude;
    input.longitude = longitude;
    input.houseSystem = houseSystem;
    input.aspectOrbs = aspectOrbs_;

    const AstroSourceMode mode = astroSourceMode();
    if (mode == AstroSourceMode::Natal) {
        const bool sameLocation = std::fabs(latitude - currentInput_.latitude) < 0.0000001
            && std::fabs(longitude - currentInput_.longitude) < 0.0000001;
        if (sameLocation && houseSystem == currentInput_.houseSystem) {
            *outChart = currentChart_;
        } else {
            TropicalComputeOptions options;
            options.includeArabicLots = false;
            options.includeFixedStars = false;
            options.includeAspectGrid = false;
            if (!engine_.compute(input, options, outChart, error)) {
                return false;
            }
        }
        if (outInput) {
            *outInput = input;
        }
        if (outSourceLabel) {
            *outSourceLabel = QString("Natal chart: %1 UTC")
                .arg(outChart->utcDateTime.toUTC().toString("yyyy-MM-dd HH:mm"));
        }
        return true;
    }

    QTimeZone tz;
    QString tzLabel;
    QString tzErr;
    const QString tzText = astroProgressionTimezoneEdit_
        ? astroProgressionTimezoneEdit_->text().trimmed()
        : (currentInput_.timezone.isEmpty() ? QString("UTC") : currentInput_.timezone);
    if (!parseTimezoneInput(tzText, &tz, &tzLabel, &tzErr)) {
        if (error) {
            *error = tzErr.isEmpty() ? "Invalid progression timezone." : tzErr;
        }
        return false;
    }

    QDateTime targetLocal;
    if (mode == AstroSourceMode::ProgressedNow) {
        targetLocal = QDateTime::currentDateTimeUtc().toTimeZone(tz);
    } else {
        const QDate date = astroProgressionDateEdit_ ? astroProgressionDateEdit_->date() : QDate::currentDate();
        const QTime time = astroProgressionTimeEdit_ ? astroProgressionTimeEdit_->time() : QTime::currentTime();
        targetLocal = QDateTime(date, time, tz);
    }
    if (!targetLocal.isValid()) {
        if (error) {
            *error = "Invalid progression target date/time.";
        }
        return false;
    }

    if (!computeProgressionChartForInput(input, targetLocal, tzLabel, outChart, error)) {
        return false;
    }
    if (outInput) {
        *outInput = input;
        outInput->date = outChart->localDateTime.date();
        outInput->time = outChart->localDateTime.time();
        outInput->timezone = outChart->timezoneLabel;
    }
    if (outSourceLabel) {
        const QString modeLabel = mode == AstroSourceMode::ProgressedNow
            ? QString("Progressed chart - now")
            : QString("Progressed chart - custom");
        *outSourceLabel = QString("%1: target %2 (%3), progressed %4 UTC")
            .arg(modeLabel)
            .arg(targetLocal.toString("yyyy-MM-dd HH:mm"))
            .arg(tzLabel)
            .arg(outChart->utcDateTime.toUTC().toString("yyyy-MM-dd HH:mm"));
    }
    return true;
}

void MainWindow::updateAstrocartographyView() {
    if (activeTab_ != AppTab::Astrocartography) {
        return;
    }
    updateAstrocartographyModeUi();
    if (!astroMapWidget_) {
        return;
    }
    astroHoverCache_.clear();
    clearAstroHoverPreview();

    auto showSingleInfo = [this](const QString& top, const QString& bottom) {
        if (rightTopTable_) {
            setupTable(rightTopTable_, {"Info"}, 1);
            rightTopTable_->setItem(0, 0, makeCell(top));
        }
        if (rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell(bottom));
        }
    };

    if (!hasCurrentChart_) {
        hasAstroSourceChart_ = false;
        astroSourceLabel_.clear();
        astroMapWidget_->clearLines();
        astroMapWidget_->clearSelectedLocation();
        hasAstroSelectedLocation_ = false;
        if (astroPreviewWheel_) {
            astroPreviewWheel_->clearChart();
        }
        if (astroPreviewStatusLabel_) {
            astroPreviewStatusLabel_->setText("Load a natal chart before previewing relocation charts.");
        }
        if (geodeticStatusLabel_) {
            geodeticStatusLabel_->setText("Load a natal chart to draw lines.");
        }
        showSingleInfo("No natal chart is loaded.", "Open or calculate a natal chart, then return to Astrocartography.");
        return;
    }
    if (!swe_.isLoaded() || ephePath_.isEmpty()) {
        hasAstroSourceChart_ = false;
        astroSourceLabel_.clear();
        astroMapWidget_->clearLines();
        astroMapWidget_->clearSelectedLocation();
        hasAstroSelectedLocation_ = false;
        if (astroPreviewWheel_) {
            astroPreviewWheel_->clearChart();
        }
        if (astroPreviewStatusLabel_) {
            astroPreviewStatusLabel_->setText("Swiss Ephemeris is needed before previewing relocation charts.");
        }
        if (geodeticStatusLabel_) {
            geodeticStatusLabel_->setText("Swiss Ephemeris not loaded.");
        }
        showSingleInfo("Swiss Ephemeris is not loaded.", "Check that swedll64.dll and ephe files are available.");
        return;
    }

    NatalChart sourceChart;
    NatalInput sourceInput;
    QString sourceLabel;
    QString sourceErr;
    if (!computeAstroSourceChart(currentInput_.latitude, currentInput_.longitude, currentInput_.houseSystem,
                                 &sourceChart, &sourceInput, &sourceLabel, &sourceErr)) {
        hasAstroSourceChart_ = false;
        astroSourceLabel_.clear();
        astroMapWidget_->clearLines();
        astroMapWidget_->clearSelectedLocation();
        hasAstroSelectedLocation_ = false;
        if (astroPreviewWheel_) {
            astroPreviewWheel_->clearChart();
        }
        if (astroPreviewStatusLabel_) {
            astroPreviewStatusLabel_->setText(sourceErr.isEmpty() ? "Unable to calculate the selected source chart." : sourceErr);
        }
        if (geodeticStatusLabel_) {
            geodeticStatusLabel_->setText(sourceErr.isEmpty() ? "Source chart calculation failed." : sourceErr);
        }
        showSingleInfo("Unable to calculate selected source chart.", sourceErr.isEmpty() ? "Check the Astrocartography source settings." : sourceErr);
        updateAstrocartographyModeUi();
        return;
    }
    astroSourceChart_ = sourceChart;
    astroSourceInput_ = sourceInput;
    astroSourceLabel_ = sourceLabel;
    hasAstroSourceChart_ = true;
    if (geodeticTimeLabel_) {
        geodeticTimeLabel_->setText(astroSourceLabel_);
    }

    const QStringList bodies = selectedCheckableItems(geodeticPlanetCombo_);
    const bool drawAc = !astroLineAcCheck_ || astroLineAcCheck_->isChecked();
    const bool drawDc = !astroLineDcCheck_ || astroLineDcCheck_->isChecked();
    const bool drawMc = !astroLineMcCheck_ || astroLineMcCheck_->isChecked();
    const bool drawIc = !astroLineIcCheck_ || astroLineIcCheck_->isChecked();
    const bool drawHarmonious = astroHarmoniousAspectsCheck_ && astroHarmoniousAspectsCheck_->isChecked();
    const bool drawDisharmonious = astroDisharmoniousAspectsCheck_ && astroDisharmoniousAspectsCheck_->isChecked();
    if (bodies.isEmpty() || (!drawAc && !drawDc && !drawMc && !drawIc)) {
        astroMapWidget_->clearLines();
        if (geodeticStatusLabel_) {
            geodeticStatusLabel_->setText("Select at least one planet and one line type.");
        }
        if (hasAstroSelectedLocation_) {
            refreshAstroClickedLocationView();
        } else {
            if (astroPreviewWheel_) {
                astroPreviewWheel_->clearChart();
            }
            if (astroPreviewStatusLabel_) {
                astroPreviewStatusLabel_->setText("Click a map point to preview its relocation chart.");
            }
            showSingleInfo("No lines selected.", "Choose planets and AC/DC/MC/IC line types in the left panel, or click the map to inspect a location.");
        }
        return;
    }

    const QDateTime utc = sourceChart.utcDateTime.toUTC();
    if (!utc.isValid()) {
        astroMapWidget_->clearLines();
        if (geodeticStatusLabel_) {
            geodeticStatusLabel_->setText("Selected source chart time is invalid.");
        }
        if (astroPreviewWheel_) {
            astroPreviewWheel_->clearChart();
        }
        if (astroPreviewStatusLabel_) {
            astroPreviewStatusLabel_->setText("Recalculate the selected source chart before previewing relocation charts.");
        }
        showSingleInfo("Invalid source chart time.", "Recalculate the selected source chart before drawing map lines.");
        return;
    }
    const double hourDec = utc.time().hour() + utc.time().minute() / 60.0 + utc.time().second() / 3600.0
        + utc.time().msec() / 3600000.0;
    const double jd = swe_.julianDay(utc.date().year(), utc.date().month(), utc.date().day(), hourDec, SE_GREG_CAL);
    applyZodiacModeToSwe(&swe_, sourceInput);

    auto normalizeLon = [](double lon) {
        double v = std::fmod(lon + 180.0, 360.0);
        if (v < 0.0) {
            v += 360.0;
        }
        return v - 180.0;
    };
    auto normalizeDeg = [](double deg) {
        double v = std::fmod(deg, 360.0);
        if (v < 0.0) {
            v += 360.0;
        }
        return v;
    };
    auto gmstDegrees = [](double julianDay) {
        const double t = (julianDay - 2451545.0) / 36525.0;
        double theta = 280.46061837 + 360.98564736629 * (julianDay - 2451545.0)
            + 0.000387933 * t * t - (t * t * t) / 38710000.0;
        theta = std::fmod(theta, 360.0);
        if (theta < 0.0) {
            theta += 360.0;
        }
        return theta;
    };
    constexpr double kAstroPi = 3.1415926535897932384626433832795;

    QVector<AstroMapLine> mapLines;
    QStringList warnings;
    const double gmst = gmstDegrees(jd);

    double obliquity = 23.4392911;
    QString epsErr;
    if (!swe_.calcUt(jd, SE_ECL_NUT, 0, &obliquity, &epsErr) && !epsErr.isEmpty()) {
        warnings.push_back(QString("Obliquity: %1").arg(epsErr));
    }
    double ayanamsa = 0.0;
    if (sourceInput.zodiacSystem == ZodiacSystem::Sidereal) {
        QString ayanErr;
        if (!swe_.getAyanamsaUt(jd, &ayanamsa, &ayanErr) && !ayanErr.isEmpty()) {
            warnings.push_back(QString("Ayanamsa: %1").arg(ayanErr));
        }
    }

    auto edgeGlyphForBody = [](const QString& name) {
        QString glyph = bodyGlyph(name).trimmed();
        if (glyph.isEmpty() || glyph == "?") {
            glyph = abbrevForName(name);
        }
        return glyph;
    };
    auto makeEdgeLabel = [&](const QString& bodyName, const QString& angleName, const QString& aspectName = QString()) {
        if (!bodySvgResourcePath(bodyName).isEmpty()) {
            return aspectName.isEmpty()
                ? angleName
                : QString("%1\n%2").arg(aspectSymbolForLabel(aspectName), angleName);
        }
        const QString glyph = edgeGlyphForBody(bodyName);
        if (aspectName.isEmpty()) {
            return QString("%1\n%2").arg(glyph, angleName);
        }
        return QString("%1 %2\n%3").arg(glyph, aspectSymbolForLabel(aspectName), angleName);
    };
    auto makeVerticalLine = [](const QString& label, const QString& edgeLabel,
                               const QString& symbolResourcePath, const QColor& color,
                               double lon, double width) {
        AstroMapLine line;
        line.label = label;
        line.edgeLabel = edgeLabel;
        line.symbolResourcePath = symbolResourcePath;
        line.color = color;
        line.width = width;
        line.lonLatPoints.push_back({lon, -85.0});
        line.lonLatPoints.push_back({lon, 85.0});
        return line;
    };
    auto makeHorizonLine = [&](const QString& label, const QString& edgeLabel,
                               const QString& symbolResourcePath, const QColor& color,
                               double raDeg, double decDeg, bool rising, double width, double latStep) {
        AstroMapLine line;
        line.label = label;
        line.edgeLabel = edgeLabel;
        line.symbolResourcePath = symbolResourcePath;
        line.color = color;
        line.width = width;
        const double ra = normalizeDeg(raDeg);
        const double decRad = decDeg * kAstroPi / 180.0;
        for (double lat = -85.0; lat <= 85.0; lat += latStep) {
            const double latRad = lat * kAstroPi / 180.0;
            const double cosH = -std::tan(latRad) * std::tan(decRad);
            if (cosH < -1.0 || cosH > 1.0) {
                continue;
            }
            const double h = std::acos(cosH) * 180.0 / kAstroPi;
            const double lst = rising ? (ra - h) : (ra + h);
            line.lonLatPoints.push_back({normalizeLon(lst - gmst), lat});
        }
        return line;
    };
    auto zodiacLongitudeToSkyLongitude = [&](double zodiacLongitude) {
        return normalizeDeg(zodiacLongitude + ayanamsa);
    };
    auto eclipticToEquatorial = [&](double eclipticLongitude, double* outRa, double* outDec) {
        const double lonRad = normalizeDeg(eclipticLongitude) * kAstroPi / 180.0;
        const double epsRad = obliquity * kAstroPi / 180.0;
        const double y = std::sin(lonRad) * std::cos(epsRad);
        const double x = std::cos(lonRad);
        double ra = std::atan2(y, x) * 180.0 / kAstroPi;
        if (ra < 0.0) {
            ra += 360.0;
        }
        const double dec = std::asin(std::sin(epsRad) * std::sin(lonRad)) * 180.0 / kAstroPi;
        if (outRa) {
            *outRa = ra;
        }
        if (outDec) {
            *outDec = dec;
        }
    };
    struct AstroAspectSpec {
        QString name;
        double angle = 0.0;
    };
    QVector<AstroAspectSpec> aspectSpecs;
    if (drawHarmonious) {
        aspectSpecs.push_back({"Sextile", 60.0});
        aspectSpecs.push_back({"Trine", 120.0});
    }
    if (drawDisharmonious) {
        aspectSpecs.push_back({"Square", 90.0});
        aspectSpecs.push_back({"Opposition", 180.0});
    }

    for (int i = 0; i < bodies.size(); ++i) {
        const QString bodyName = bodies[i];
        if (bodyIdForName(bodyName, effectivePrimaryNodeType(sourceInput.lunarNodePolicy)) < 0) {
            continue;
        }
        double planetLongitude = 0.0;
        if (!findBodyLongitude(sourceChart, bodyName, &planetLongitude)) {
            warnings.push_back(QString("%1: missing source longitude").arg(bodyName));
            continue;
        }

        const QColor color = astrocartographyColorForBody(bodyName);
        const double exactSkyLongitude = zodiacLongitudeToSkyLongitude(planetLongitude);
        double exactRa = 0.0;
        double exactDec = 0.0;
        eclipticToEquatorial(exactSkyLongitude, &exactRa, &exactDec);

        if (drawMc) {
            mapLines.push_back(makeVerticalLine(QString("%1 MC").arg(bodyName), makeEdgeLabel(bodyName, "MC"), bodySvgResourcePath(bodyName), color, normalizeLon(exactRa - gmst), 1.55));
        }
        if (drawIc) {
            mapLines.push_back(makeVerticalLine(QString("%1 IC").arg(bodyName), makeEdgeLabel(bodyName, "IC"), bodySvgResourcePath(bodyName), color, normalizeLon(exactRa + 180.0 - gmst), 1.55));
        }
        if (drawAc) {
            auto line = makeHorizonLine(QString("%1 AC").arg(bodyName), makeEdgeLabel(bodyName, "AC"), bodySvgResourcePath(bodyName), color, exactRa, exactDec, true, 1.55, 0.25);
            if (line.lonLatPoints.size() >= 2) {
                mapLines.push_back(line);
            }
        }
        if (drawDc) {
            auto line = makeHorizonLine(QString("%1 DC").arg(bodyName), makeEdgeLabel(bodyName, "DC"), bodySvgResourcePath(bodyName), color, exactRa, exactDec, false, 1.55, 0.25);
            if (line.lonLatPoints.size() >= 2) {
                mapLines.push_back(line);
            }
        }

        if (!aspectSpecs.isEmpty()) {
            for (const auto& spec : aspectSpecs) {
                QVector<double> offsets;
                if (std::fabs(spec.angle - 180.0) < 0.001) {
                    offsets.push_back(180.0);
                } else {
                    offsets.push_back(spec.angle);
                    offsets.push_back(-spec.angle);
                }
                for (const double offset : offsets) {
                    const double targetZodiacLongitude = normalizeDeg(planetLongitude + offset);
                    const double targetSkyLongitude = zodiacLongitudeToSkyLongitude(targetZodiacLongitude);
                    double targetRa = 0.0;
                    double targetDec = 0.0;
                    eclipticToEquatorial(targetSkyLongitude, &targetRa, &targetDec);
                    if (drawMc) {
                        mapLines.push_back(makeVerticalLine(QString("%1 %2 MC").arg(bodyName, spec.name),
                            makeEdgeLabel(bodyName, "MC", spec.name), bodySvgResourcePath(bodyName), color, normalizeLon(targetRa - gmst), 0.95));
                    }
                    if (drawIc) {
                        mapLines.push_back(makeVerticalLine(QString("%1 %2 IC").arg(bodyName, spec.name),
                            makeEdgeLabel(bodyName, "IC", spec.name), bodySvgResourcePath(bodyName), color, normalizeLon(targetRa + 180.0 - gmst), 0.95));
                    }
                    if (drawAc) {
                        auto line = makeHorizonLine(QString("%1 %2 AC").arg(bodyName, spec.name),
                            makeEdgeLabel(bodyName, "AC", spec.name), bodySvgResourcePath(bodyName), color, targetRa, targetDec, true, 0.95, 0.5);
                        if (line.lonLatPoints.size() >= 2) {
                            mapLines.push_back(line);
                        }
                    }
                    if (drawDc) {
                        auto line = makeHorizonLine(QString("%1 %2 DC").arg(bodyName, spec.name),
                            makeEdgeLabel(bodyName, "DC", spec.name), bodySvgResourcePath(bodyName), color, targetRa, targetDec, false, 0.95, 0.5);
                        if (line.lonLatPoints.size() >= 2) {
                            mapLines.push_back(line);
                        }
                    }
                }
            }
        }
    }

    astroMapWidget_->setLines(mapLines);
    if (geodeticStatusLabel_) {
        QString status = QString("Updated (%1 lines)").arg(mapLines.size());
        if (!warnings.isEmpty()) {
            status += QString("; %1 skipped").arg(warnings.size());
        }
        geodeticStatusLabel_->setText(status);
    }

    if (hasAstroSelectedLocation_) {
        refreshAstroClickedLocationView();
    } else {
        if (astroPreviewWheel_) {
            astroPreviewWheel_->clearChart();
        }
        if (astroPreviewStatusLabel_) {
            astroPreviewStatusLabel_->setText("Click a map point to preview its relocation chart.");
        }
        if (rightTopDock_) {
            rightTopDock_->setWindowTitle("Clicked Location Chart");
        }
        if (rightBottomDock_) {
            rightBottomDock_->setWindowTitle("Map Details");
        }
        if (rightTopTable_) {
            setupTable(rightTopTable_, {"Clicked Location"}, 2);
            rightTopTable_->setItem(0, 0, makeCell("Use Select mode, then click any map point."));
            rightTopTable_->setItem(1, 0, makeCell("This panel will show the relocated selected chart for that latitude/longitude."));
            rightTopTable_->setWordWrap(true);
            rightTopTable_->resizeRowsToContents();
        }
        if (rightBottomTable_) {
            QStringList details;
            details << QString("Source: %1").arg(astroSourceLabel_);
            details << QString("Birthplace: %1 (%2, %3)")
                .arg(currentLocation_.isEmpty() ? currentInput_.name : currentLocation_)
                .arg(currentInput_.latitude, 0, 'f', 4)
                .arg(currentInput_.longitude, 0, 'f', 4);
            details << QString("Source UTC time: %1").arg(utc.toString("yyyy-MM-dd HH:mm"));
            details << QString("Sidereal time: %1 deg").arg(gmst, 0, 'f', 2);
            details << QString("Zodiac setting: %1").arg(zodiacModeSummary(sourceInput));
            details << "Hover shows the local Ascendant sign.";
            details << "Select tool: click to inspect a location. Pan tool: drag the map.";
            if (!warnings.isEmpty()) {
                details << QString("Skipped: %1").arg(warnings.join("; "));
            }
            setupTable(rightBottomTable_, {"Map Details"}, details.size());
            for (int i = 0; i < details.size(); ++i) {
                rightBottomTable_->setItem(i, 0, makeCell(details[i]));
            }
            rightBottomTable_->setWordWrap(true);
            rightBottomTable_->resizeRowsToContents();
        }
    }
}

void MainWindow::updateGeodeticOverlays() {
    updateAstrocartographyView();
}

void MainWindow::setWorldMapOverlays(const QVariantList& lineOverlays, const QVariantList& bandOverlays) {
    Q_UNUSED(lineOverlays);
    Q_UNUSED(bandOverlays);
}

HouseSystem MainWindow::astroClickedHouseSystem() const {
    if (astroClickedHouseCombo_) {
        const int value = astroClickedHouseCombo_->currentData().toInt();
        return value == static_cast<int>(HouseSystem::Placidus) ? HouseSystem::Placidus : HouseSystem::WholeSign;
    }
    return HouseSystem::WholeSign;
}

QString MainWindow::astroHoverCacheKey(double latitude, double longitude, double* roundedLatitude, double* roundedLongitude) const {
    constexpr double kGrid = 20.0;  // 0.05 degree cells: stable enough for hover, small enough for sign/degree previews.
    const double lat = std::clamp(latitude, -85.0, 85.0);
    double lon = std::fmod(longitude + 180.0, 360.0);
    if (lon < 0.0) {
        lon += 360.0;
    }
    lon -= 180.0;
    const int latKey = qRound(lat * kGrid);
    const int lonKey = qRound(lon * kGrid);
    if (roundedLatitude) {
        *roundedLatitude = latKey / kGrid;
    }
    if (roundedLongitude) {
        *roundedLongitude = lonKey / kGrid;
    }
    const NatalChart& chart = hasAstroSourceChart_ ? astroSourceChart_ : currentChart_;
    const NatalInput& input = hasAstroSourceChart_ ? astroSourceInput_ : currentInput_;
    const qint64 chartKey = chart.utcDateTime.toUTC().toMSecsSinceEpoch();
    return QString("%1:%2:%3:%4:%5:%6")
        .arg(static_cast<int>(astroSourceMode()))
        .arg(chartKey)
        .arg(static_cast<int>(input.zodiacSystem))
        .arg(static_cast<int>(input.siderealAyanamsa))
        .arg(latKey)
        .arg(lonKey);
}

QString MainWindow::astroHoverInfoFor(double latitude, double longitude) {
    if (!hasCurrentChart_ || !swe_.isLoaded()) {
        return QString();
    }
    const NatalChart& chart = hasAstroSourceChart_ ? astroSourceChart_ : currentChart_;
    const NatalInput& input = hasAstroSourceChart_ ? astroSourceInput_ : currentInput_;
    const QDateTime utc = chart.utcDateTime.toUTC();
    if (!utc.isValid()) {
        return QString();
    }
    const double hourDec = utc.time().hour() + utc.time().minute() / 60.0 + utc.time().second() / 3600.0
        + utc.time().msec() / 3600000.0;
    const double jd = swe_.julianDay(utc.date().year(), utc.date().month(), utc.date().day(), hourDec, SE_GREG_CAL);
    double cusps[13] = {0};
    double ascmc[10] = {0};
    QString err;
    applyZodiacModeToSwe(&swe_, input);
    if (!swe_.housesEx(jd, calcFlagsForInput(input), latitude, longitude, 'P', cusps, ascmc, &err)) {
        return QString();
    }
    const double roundedAsc = normalizeDegrees(std::floor(normalizeDegrees(ascmc[0]) + 0.5));
    const int degree = static_cast<int>(std::floor(degInSign(roundedAsc) + 0.000001));
    return QString("Asc %1%2 %3")
        .arg(degree, 2, 10, QChar('0'))
        .arg(QChar(0x00B0))
        .arg(signName(signIndex(roundedAsc)));
}

void MainWindow::flushAstroHoverPreview() {
    if (!hasAstroPendingHover_) {
        return;
    }
    hasAstroPendingHover_ = false;
    const QString key = astroPendingHoverKey_;
    if (key.isEmpty()) {
        return;
    }

    QString info;
    const auto it = astroHoverCache_.constFind(key);
    if (it != astroHoverCache_.constEnd()) {
        info = it.value();
    } else {
        info = astroHoverInfoFor(astroPendingHoverLat_, astroPendingHoverLon_);
        if (astroHoverCache_.size() > 1000) {
            astroHoverCache_.clear();
        }
        astroHoverCache_.insert(key, info);
    }
    if (astroMapWidget_) {
        astroMapWidget_->setHoverInfo(info);
    }
}

void MainWindow::clearAstroHoverPreview() {
    hasAstroPendingHover_ = false;
    astroPendingHoverKey_.clear();
    if (astroHoverTimer_ && astroHoverTimer_->isActive()) {
        astroHoverTimer_->stop();
    }
    if (astroMapWidget_) {
        astroMapWidget_->setHoverInfo(QString());
    }
}

void MainWindow::handleAstroMapHovered(double latitude, double longitude) {
    if (!astroMapWidget_) {
        return;
    }

    double roundedLatitude = latitude;
    double roundedLongitude = longitude;
    const QString key = astroHoverCacheKey(latitude, longitude, &roundedLatitude, &roundedLongitude);
    const auto it = astroHoverCache_.constFind(key);
    if (it != astroHoverCache_.constEnd()) {
        hasAstroPendingHover_ = false;
        if (astroHoverTimer_ && astroHoverTimer_->isActive()) {
            astroHoverTimer_->stop();
        }
        astroMapWidget_->setHoverInfo(it.value());
        return;
    }

    astroPendingHoverKey_ = key;
    astroPendingHoverLat_ = roundedLatitude;
    astroPendingHoverLon_ = roundedLongitude;
    hasAstroPendingHover_ = true;
    if (astroHoverTimer_) {
        if (!astroHoverTimer_->isActive()) {
            astroHoverTimer_->start();
        }
    } else {
        flushAstroHoverPreview();
    }
}

void MainWindow::handleAstroMapClicked(double latitude, double longitude) {
    if (!hasCurrentChart_) {
        setStatusMessage("Load a natal chart before inspecting map locations.");
        return;
    }
    astroSelectedLat_ = latitude;
    astroSelectedLon_ = longitude;
    hasAstroSelectedLocation_ = true;
    if (astroMapWidget_) {
        const QString label = QString("%1, %2").arg(latitude, 0, 'f', 4).arg(longitude, 0, 'f', 4);
        astroMapWidget_->setSelectedLocation(latitude, longitude, label);
    }
    refreshAstroClickedLocationView();
}

void MainWindow::refreshAstroClickedLocationView() {
    if (!hasCurrentChart_ || !hasAstroSelectedLocation_) {
        return;
    }

    NatalInput input;
    NatalChart chart;
    QString sourceLabel;
    QString err;
    if (!computeAstroSourceChart(astroSelectedLat_, astroSelectedLon_, astroClickedHouseSystem(),
                                 &chart, &input, &sourceLabel, &err)) {
        if (astroPreviewWheel_) {
            astroPreviewWheel_->clearChart();
        }
        if (astroPreviewStatusLabel_) {
            astroPreviewStatusLabel_->setText(err.isEmpty() ? "Unable to preview this location." : err);
        }
        if (rightTopTable_) {
            setupTable(rightTopTable_, {"Clicked Location"}, 1);
            rightTopTable_->setItem(0, 0, makeCell(err.isEmpty() ? "Unable to calculate clicked location chart." : err));
        }
        if (geodeticStatusLabel_) {
            geodeticStatusLabel_->setText("Clicked location calculation failed.");
        }
        return;
    }
    astroSelectedChart_ = chart;
    const QString houseSystemLabel = input.houseSystem == HouseSystem::Placidus ? "Placidus" : "Whole Sign";

    if (astroPreviewWheel_) {
        astroPreviewWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        astroPreviewWheel_->setShowAspects(false);
        astroPreviewWheel_->setShowLots(false);
        astroPreviewWheel_->setShowDerivedPoints(false);
        astroPreviewWheel_->setShowFixedStars(false);
        astroPreviewWheel_->setShowAsteroids(false);
        astroPreviewWheel_->setTickDensity(ChartWheelWidget::TickDensity::Minimal);
        astroPreviewWheel_->setFontScale(0.72);
        astroPreviewWheel_->setZoom(0.94);
        astroPreviewWheel_->setTheme(buildChartTheme(theme_));
        astroPreviewWheel_->setChart(chart, input.houseSystem);
        astroPreviewWheel_->setChartNote(QString());
    }
    if (astroPreviewStatusLabel_) {
        astroPreviewStatusLabel_->setText(QString("%1, %2 | %3")
            .arg(astroSelectedLat_, 0, 'f', 4)
            .arg(astroSelectedLon_, 0, 'f', 4)
            .arg(houseSystemLabel));
    }

    if (geodeticStatusLabel_) {
        geodeticStatusLabel_->setText(QString("Selected %1, %2")
            .arg(astroSelectedLat_, 0, 'f', 4)
            .arg(astroSelectedLon_, 0, 'f', 4));
    }
    if (rightTopDock_) {
        rightTopDock_->setWindowTitle("Clicked Location Chart");
    }
    if (rightBottomDock_) {
        rightBottomDock_->setWindowTitle("Clicked Location Details");
    }

    if (rightTopTable_) {
        struct Row {
            QString point;
            double longitude = 0.0;
            QString sign;
            QString house;
        };
        QVector<Row> rows;
        rows.push_back({"Ascendant", chart.angles.asc, signName(signIndex(chart.angles.asc)), "-"});
        rows.push_back({"Midheaven", chart.angles.mc, signName(signIndex(chart.angles.mc)), "-"});
        rows.push_back({"Descendant", chart.angles.desc, signName(signIndex(chart.angles.desc)), "-"});
        rows.push_back({"IC", chart.angles.ic, signName(signIndex(chart.angles.ic)), "-"});

        QMap<QString, BodyPosition> bodyMap;
        for (const auto& body : chart.bodies) {
            bodyMap.insert(body.name, body);
        }
        QSet<QString> added;
        for (const auto& name : bodyOrderForLunarNodePolicy(chart.lunarNodePolicy)) {
            if (isArabicLotName(name) || !bodyMap.contains(name)) {
                continue;
            }
            const auto& body = bodyMap[name];
            rows.push_back({lunarNodeDisplayName(body.name, chart.lunarNodePolicy), body.longitude,
                            body.signName, body.house > 0 ? QString::number(body.house) : "-"});
            added.insert(name);
        }
        for (const auto& body : chart.bodies) {
            if (added.contains(body.name) || isArabicLotName(body.name)) {
                continue;
            }
            rows.push_back({lunarNodeDisplayName(body.name, chart.lunarNodePolicy), body.longitude,
                            body.signName, body.house > 0 ? QString::number(body.house) : "-"});
        }

        setupTable(rightTopTable_, {"Point", "Degree", "Sign", "House"}, rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            rightTopTable_->setItem(i, 0, makeCell(rows[i].point));
            rightTopTable_->setItem(i, 1, makeCell(formatDegOnly(rows[i].longitude), Qt::AlignRight | Qt::AlignVCenter));
            rightTopTable_->setItem(i, 2, makeCell(rows[i].sign));
            rightTopTable_->setItem(i, 3, makeCell(rows[i].house, Qt::AlignCenter));
        }
        rightTopTable_->resizeColumnsToContents();
    }

    if (rightBottomTable_) {
        QVector<QPair<QString, QString>> details;
        details.push_back({"Selected latitude", QString::number(astroSelectedLat_, 'f', 6)});
        details.push_back({"Selected longitude", QString::number(astroSelectedLon_, 'f', 6)});
        details.push_back({"Source", sourceLabel});
        details.push_back({"House system", houseSystemLabel});
        details.push_back({"Ascendant", formatDegInSign(chart.angles.asc)});
        details.push_back({"Midheaven", formatDegInSign(chart.angles.mc)});
        details.push_back({"Source UTC time", chart.utcDateTime.toUTC().toString("yyyy-MM-dd HH:mm")});
        details.push_back({"Original birthplace", QString("%1 (%2, %3)")
            .arg(currentLocation_.isEmpty() ? currentInput_.name : currentLocation_)
            .arg(currentInput_.latitude, 0, 'f', 4)
            .arg(currentInput_.longitude, 0, 'f', 4)});
        details.push_back({"Zodiac setting", zodiacModeSummary(input)});

        setupTable(rightBottomTable_, {"Item", "Value"}, details.size());
        for (int i = 0; i < details.size(); ++i) {
            rightBottomTable_->setItem(i, 0, makeCell(details[i].first));
            rightBottomTable_->setItem(i, 1, makeCell(details[i].second));
        }
        rightBottomTable_->resizeColumnsToContents();
    }
}

void MainWindow::updateLunationModeAvailability() {
    const bool useRange = lunationModeRangeRadio_ && lunationModeRangeRadio_->isChecked();
    const bool useDegreeRange = lunationDegreeRangeCheck_ && lunationDegreeRangeCheck_->isChecked();
    const bool siderealMode = (currentInput_.zodiacSystem == ZodiacSystem::Sidereal);
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
    if (lunationEclipseRuleCombo_) {
        if (!siderealMode) {
            const QSignalBlocker blocker(lunationEclipseRuleCombo_);
            const int idx = lunationEclipseRuleCombo_->findData(static_cast<int>(LunationEclipseRule::AstronomicalSwiss));
            if (idx >= 0) {
                lunationEclipseRuleCombo_->setCurrentIndex(idx);
            }
            lunationEclipseRuleCombo_->setEnabled(false);
            lunationEclipseRuleCombo_->setToolTip("Strict Vedic eclipse classification is available only in sidereal mode.");
        } else {
            lunationEclipseRuleCombo_->setEnabled(true);
            lunationEclipseRuleCombo_->setToolTip("Choose how Solar/Lunar Eclipse events are classified in sidereal mode.");
        }
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
    const bool uniqueOnly = conjUniqueFirstCheck_ && conjUniqueFirstCheck_->isChecked();
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
        QString reference = QString("Reference: %1 (%2)").arg(targetText, tzLabel);
        if (uniqueOnly && !useRange) {
            reference += " | Unique-first requires Year Range mode";
        }
        conjReferenceLabel_->setText(reference);
    }

    QStringList selectedPlanetsList = selectedCheckableItems(conjPlanetCombo_);
    if (conjIncludeMoonCheck_ && !conjIncludeMoonCheck_->isChecked()) {
        selectedPlanetsList.removeAll("Moon");
    }
    const int selectedPlanets = selectedPlanetsList.size();
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
    if (conjUniqueDegreeCombo_) {
        conjUniqueDegreeCombo_->setEnabled(uniqueOnly);
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
    rightTopTable_->setUpdatesEnabled(false);
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
    rightTopTable_->setUpdatesEnabled(true);
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
        rightBottomTable_->setItem(row, 0, makeCell(lunarNodeDisplayName(body.name, currentTransitChart_.lunarNodePolicy)));
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
                QString label = lunarNodeDisplayName(name, currentChart_.lunarNodePolicy);
                int house = 0;
                if (isHouseEvent) {
                    house = calcHouseForLongitude(lon, natalPlacidusCusps_, currentChart_.angles.asc, transitHouseSystem_);
                    label = QString("%1 (House %2)").arg(name).arg(house);
                }
                const int index = searchTargetCombo_->count();
                searchTargetCombo_->addItem(label);
                searchTargetCombo_->setItemData(index, name, Qt::UserRole + 1);
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
    params.zodiacSystem = currentInput_.zodiacSystem;
    params.siderealAyanamsa = currentInput_.siderealAyanamsa;
    params.lunarNodePolicy = currentInput_.lunarNodePolicy;
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
        params.anyMajorAspect = !isDegreeHit
            && aspectLabel.compare("Any Major Aspect", Qt::CaseInsensitive) == 0;
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
                const QString storedTarget = searchTargetCombo_->currentData(Qt::UserRole + 1).toString();
                const QString parsedTarget = aspectTargetFromLabel(targetText);
                const QString target = !storedTarget.isEmpty()
                    ? storedTarget
                    : (parsedTarget.isEmpty() ? targetText : parsedTarget);
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
    searchResultsDirty_ = false;
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
        scheduleTransitSearchResultsRefresh();
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
        if (searchResultsRefreshTimer_ && searchResultsRefreshTimer_->isActive()) {
            searchResultsRefreshTimer_->stop();
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
    if (searchResultsDirty_) {
        std::sort(transitSearchResults_.begin(), transitSearchResults_.end(), [](const TransitSearchResult& a, const TransitSearchResult& b) {
            return a.timeUtc < b.timeUtc;
        });
        searchResultsDirty_ = false;
    }

    rightTopTable_->setUpdatesEnabled(false);
    setupTable(rightTopTable_, {"Date/Time", "Planet", "Event", "Sign/House", "Aspect+Orb"}, transitSearchResults_.size());
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
    if (auto* header = rightTopTable_->horizontalHeader()) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(4, QHeaderView::Stretch);
    }
    rightTopTable_->setUpdatesEnabled(true);
    if (!transitSearchResults_.isEmpty()) {
        if (searchRunning_ && !hasTransitSearchSelection_) {
            if (rightBottomTable_) {
                setupTable(rightBottomTable_, {"Info"}, 1);
                rightBottomTable_->setItem(0, 0, makeCell("Search running. Select a result to load transit placements."));
            }
        } else {
            showTransitSearchDetails(transitSearchResults_.front());
        }
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

    rightTopTable_->setUpdatesEnabled(false);
    setupTable(rightTopTable_, {"Date", "Time", "Planet", "Event", "Sign/House", "Longitude"}, transitCalendarDisplayOrder_.size());
    rightTopTable_->verticalHeader()->setDefaultSectionSize(24);
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
    if (auto* header = rightTopTable_->horizontalHeader()) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(5, QHeaderView::Stretch);
    }
    rightTopTable_->setUpdatesEnabled(true);

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
        rightBottomTable_->setItem(row, 0, makeCell(lunarNodeDisplayName(body.name, currentTransitChart_.lunarNodePolicy)));
        rightBottomTable_->setItem(row++, 1, makeCell(value));
    }
    rightBottomTable_->resizeRowsToContents();
    updateLunationCopyButtonState();
}

void MainWindow::showTransitConjunctionResults() {
    if (!rightTopTable_) {
        return;
    }
    const bool uniqueMode = conjLastRunUniqueFirst_;
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
        rightTopTable_->setItem(0, 0, makeCell(uniqueMode
            ? "Run the conjunction finder to see first-time unique results."
            : "Run the conjunction finder to see results."));
        if (rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell(uniqueMode
                ? "No first-time unique conjunction results yet."
                : "No conjunction results yet."));
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
        rightTopTable_->setItem(0, 0, makeCell(uniqueMode
            ? "No first-time unique conjunction windows found for the selected range."
            : "No conjunction windows found for the selected range."));
        if (rightBottomTable_) {
            const QString uniqueHint = QString("Try a wider range, increase degree step (now %1 deg), lower N%2.")
                .arg(QString::number(conjLastRunUniqueDegreeStep_, 'f', 1))
                .arg(conjLastRunIncludeMoon_ ? "" : ", or include Moon");
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell(uniqueMode
                ? uniqueHint
                : "Try a different range or lower N."));
        }
        hasTransitConjunctionSelection_ = false;
        updateLunationCopyButtonState();
        return;
    }

    const QTimeZone displayTz = conjTz_.isValid() ? conjTz_ : QTimeZone::utc();
    const QStringList headers = uniqueMode
        ? QStringList{"Start Date", "Start Time", "End Date", "End Time", "Sign/House", "Count", "Planets", "Span", "Unique Signature"}
        : QStringList{"Start Date", "Start Time", "End Date", "End Time", "Sign/House", "Count", "Planets", "Span"};
    rightTopTable_->setUpdatesEnabled(false);
    setupTable(rightTopTable_, headers, transitConjunctionDisplayOrder_.size());
    rightTopTable_->verticalHeader()->setDefaultSectionSize(24);

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
        if (uniqueMode) {
            rightTopTable_->setItem(row, 8, makeCell(res.uniqueSignature.isEmpty() ? "-" : res.uniqueSignature));
        }
        const bool instantExact = (res.startUtc == res.endUtc);
        rightTopTable_->setItem(row, 7, makeCell((res.clusterSpanDeg > 0.0 || instantExact)
            ? QString::number(res.clusterSpanDeg, 'f', 2) + "°"
            : "-"));
    }
    if (auto* header = rightTopTable_->horizontalHeader()) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(6, uniqueMode ? QHeaderView::ResizeToContents : QHeaderView::Stretch);
        header->setSectionResizeMode(7, QHeaderView::ResizeToContents);
        if (uniqueMode) {
            header->setSectionResizeMode(8, QHeaderView::Stretch);
        }
    }
    rightTopTable_->setUpdatesEnabled(true);

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
    for (const auto& name : bodyOrderForLunarNodePolicy(currentTransitChart_.lunarNodePolicy)) {
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
    QString summary = QString("Local %1 - %2 | UTC %3 - %4 | %5 | %6 | %7 | Span %8 deg")
        .arg(localStart.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(localEnd.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(result.startUtc.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(result.endUtc.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(tzLabel)
        .arg(result.bucketLabel)
        .arg(clusterPlanets.join(", "))
        .arg(QString::number(result.clusterSpanDeg, 'f', 2));
    if (result.uniqueFirstOccurrence && !result.uniqueSignature.isEmpty()) {
        summary += QString(" | Unique Signature: %1").arg(result.uniqueSignature);
    }
    rightBottomTable_->setToolTip(summary);

    for (int row = 0; row < orderedBodies.size(); ++row) {
        const auto& body = orderedBodies[row];
        const QString degree = formatDegOnly(body.longitude);
        const QString sign = signName(signIndex(body.longitude));
        const QString house = body.house > 0 ? QString::number(body.house) : "-";
        const QString motion = body.retrograde ? "R" : "D";
        rightBottomTable_->setItem(row, 0, makeCell(lunarNodeDisplayName(body.name, currentTransitChart_.lunarNodePolicy)));
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
    for (const auto& name : bodyOrderForLunarNodePolicy(currentTransitChart_.lunarNodePolicy)) {
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
        rightBottomTable_->setItem(row, 0, makeCell(lunarNodeDisplayName(body.name, currentTransitChart_.lunarNodePolicy)));
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
    params.zodiacSystem = currentInput_.zodiacSystem;
    params.siderealAyanamsa = currentInput_.siderealAyanamsa;
    params.lunarNodePolicy = currentInput_.lunarNodePolicy;
    params.eclipseRule = LunationEclipseRule::AstronomicalSwiss;
    if (currentInput_.zodiacSystem == ZodiacSystem::Sidereal && lunationEclipseRuleCombo_) {
        params.eclipseRule = static_cast<LunationEclipseRule>(lunationEclipseRuleCombo_->currentData().toInt());
    }
    params.includeNewMoon = includeNew;
    params.includeFullMoon = includeFull;
    params.includeSolarEclipse = includeSolar;
    params.includeLunarEclipse = includeLunar;
    params.useDegreeRange = lunationDegreeRangeCheck_ && lunationDegreeRangeCheck_->isChecked();
    if (params.useDegreeRange) {
        params.degreeRangeStart = lunationDegreeRangeStartSpin_ ? lunationDegreeRangeStartSpin_->value() : 0.0;
        params.degreeRangeEnd = lunationDegreeRangeEndSpin_ ? lunationDegreeRangeEndSpin_->value() : 29.99;
    }

    params.requirePlanetConjunction = lunationPlanetConjCheck_ && lunationPlanetConjCheck_->isChecked();
    if (params.requirePlanetConjunction) {
        if (!lunationModeRangeRadio_ || !lunationModeRangeRadio_->isChecked()) {
            setStatusMessage("Planet conjunction filter needs Year Range mode "
                             "(Find Next/Previous returns a single event that the filter would usually exclude).");
            return;
        }
        params.conjunctionPlanet = lunationPlanetCombo_ ? lunationPlanetCombo_->currentText().trimmed() : QString();
        params.targetSun = lunationConjSunCheck_ && lunationConjSunCheck_->isChecked();
        params.targetMoon = lunationConjMoonCheck_ && lunationConjMoonCheck_->isChecked();
        params.targetNorthNode = lunationConjNorthNodeCheck_ && lunationConjNorthNodeCheck_->isChecked();
        params.targetSouthNode = lunationConjSouthNodeCheck_ && lunationConjSouthNodeCheck_->isChecked();
        params.conjunctionOrb = lunationConjOrbSpin_ ? lunationConjOrbSpin_->value() : 3.0;
        if (params.conjunctionPlanet.isEmpty()) {
            setStatusMessage("Select a planet for the conjunction filter.");
            return;
        }
        if (!params.targetSun && !params.targetMoon && !params.targetNorthNode && !params.targetSouthNode) {
            setStatusMessage("Select at least one conjunction target (Sun/Moon/Node).");
            return;
        }
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
    if (inLunationsView()) {
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
        if (inLunationsView()) {
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

    bool anyConjunction = false;
    for (int idx : lunationListDisplayOrder_) {
        if (idx >= 0 && idx < lunationResults_.size() && !lunationResults_[idx].conjunctionSummary.isEmpty()) {
            anyConjunction = true;
            break;
        }
    }

    QStringList listHeaders = {"Date", "Time", "Event", "Sun", "Moon", "Eclipse"};
    if (anyConjunction) {
        listHeaders << "Planet Hit";
    }
    setupTable(rightTopTable_, listHeaders, lunationListDisplayOrder_.size());
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
        if (anyConjunction) {
            rightTopTable_->setItem(row, 6, makeCell(res.conjunctionSummary.isEmpty() ? "-" : res.conjunctionSummary));
        }
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
    const HouseSystem lunationHouseSystem = lunationHouseSystemForInput(currentInput_);
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
            house = calcHouseForLongitude(res.moonLon, natalPlacidusCusps_, currentChart_.angles.asc, lunationHouseSystem);
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

    const HouseSystem lunationHouseSystem = lunationHouseSystemForInput(currentInput_);

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
                const int house = calcHouseForLongitude(res.moonLon, natalPlacidusCusps_, currentChart_.angles.asc, lunationHouseSystem);
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
                const int house = calcHouseForLongitude(res.moonLon, natalPlacidusCusps_, currentChart_.angles.asc, lunationHouseSystem);
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
    const HouseSystem lunationHouseSystem = lunationHouseSystemForInput(currentInput_);
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
            const int house = calcHouseForLongitude(res.moonLon, natalPlacidusCusps_, currentChart_.angles.asc, lunationHouseSystem);
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
    const bool hasConj = !result.conjunctionSummary.isEmpty();
    const bool hasMomentPlacements = hasTransitChart_
        && hasLunationSelection_
        && lastLunationSelection_.timeUtc == result.timeUtc
        && lastLunationSelection_.event == result.event;
    const bool hasFixedStarInfo = hasMomentPlacements && !currentTransitChart_.fixedStars.isEmpty();
    const int placementRows = hasMomentPlacements ? currentTransitChart_.bodies.size() : 0;
    const int fixedStarRows = hasFixedStarInfo ? 2 : 0;
    const int totalRows = (hasEclipse ? 7 : 6) + (hasConj ? 1 : 0) + fixedStarRows + (hasMomentPlacements ? 1 + placementRows : 1);
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
    if (hasConj) {
        rightBottomTable_->setItem(row, 0, makeCell("Planet Hit"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.conjunctionSummary));
    }
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
    for (const auto& name : bodyOrderForLunarNodePolicy(currentTransitChart_.lunarNodePolicy)) {
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
        rightBottomTable_->setItem(row, 0, makeCell(
            lunarNodeDisplayName(body.name, currentTransitChart_.lunarNodePolicy)));
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
        rightBottomTable_->setItem(row, 0, makeCell(
            lunarNodeDisplayName(body.name, currentTransitChart_.lunarNodePolicy)));
        rightBottomTable_->setItem(row++, 1, makeCell(formatDegInSign(body.longitude) + suffix));
    }
    updateLunationCopyButtonState();
}

bool MainWindow::inLunationsView() const {
    return activeTab_ == AppTab::Lunations;
}

void MainWindow::refreshLunationsTab() {
    if (activeTab_ != AppTab::Lunations) {
        return;
    }
    updateLunationModeAvailability();
    updateLunationAnalysisAvailability();
    if (!lunationRunning_) {
        lunationAutoApplied_ = false;
        showLunationResults();
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

    const bool overlayMode = (lunationOverlay_ && hasCurrentChart_);
    if (overlayMode) {
        if (chartWheel_) {
            chartWheel_->setShowAspects(true);
            chartWheel_->setOverlayLabel("Transit");
            chartWheel_->setOverlayCharts(natalChartForTransitDisplay(), chart, transitHouseSystem_, aspectOrbs_);
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
    // Per-click selection charts (Calendar/Conjunction/Scan/Lunation) only need
    // body positions, angles, and the aspect grid for display. Skipping Arabic
    // Lots (which runs the expensive prenatal-syzygy search) and fixed stars
    // keeps row selection fast and responsive, matching the Search tab.
    TropicalComputeOptions options;
    options.includeArabicLots = false;
    options.includeFixedStars = false;
    return computeTransitChart(localTime, tzLabel, options, out, error);
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
    if (lunationOverlay_ && !hasCurrentChart_) {
        if (error) {
            *error = "Load a natal chart first to overlay the lunation chart.";
        }
        return false;
    }
    if (!lunationOverlay_ && !hasCurrentChart_) {
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

bool MainWindow::isSolarPlacementFinderTabActive() const {
    if (activeTab_ != AppTab::SolarReturn) {
        return false;
    }
    if (!tabs_ || !solarPlacementFinderPanel_) {
        return false;
    }
    return tabs_->currentWidget() == solarPlacementFinderPanel_;
}

MainWindow::SolarTechniqueBodyFilter MainWindow::solarTechniqueBodyFilter() const {
    SolarTechniqueBodyFilter filter;
    filter.planets = solarTechniqueBodyPlanetsCheck_ ? solarTechniqueBodyPlanetsCheck_->isChecked() : true;
    filter.nodes = solarTechniqueBodyNodesCheck_ ? solarTechniqueBodyNodesCheck_->isChecked() : true;
    filter.angles = solarTechniqueBodyAnglesCheck_ ? solarTechniqueBodyAnglesCheck_->isChecked() : true;
    filter.lots = solarTechniqueBodyLotsCheck_ ? solarTechniqueBodyLotsCheck_->isChecked() : false;
    filter.asteroids = solarTechniqueBodyAsteroidsCheck_ ? solarTechniqueBodyAsteroidsCheck_->isChecked() : false;
    filter.lilith = solarTechniqueBodyLilithCheck_ ? solarTechniqueBodyLilithCheck_->isChecked() : false;
    filter.vertex = solarTechniqueBodyVertexCheck_ ? solarTechniqueBodyVertexCheck_->isChecked() : false;
    return filter;
}

MainWindow::SolarTechniqueBodyPreset MainWindow::solarTechniqueBodyPresetFromFilter(
    const SolarTechniqueBodyFilter& filter) const {
    const SolarTechniqueBodyFilter core;
    if (filter == core) {
        return SolarTechniqueBodyPreset::Core;
    }

    SolarTechniqueBodyFilter coreWithLots = core;
    coreWithLots.lots = true;
    if (filter == coreWithLots) {
        return SolarTechniqueBodyPreset::CoreWithLots;
    }

    SolarTechniqueBodyFilter fullChartBodies;
    fullChartBodies.planets = true;
    fullChartBodies.nodes = true;
    fullChartBodies.angles = true;
    fullChartBodies.lots = true;
    fullChartBodies.asteroids = true;
    fullChartBodies.lilith = true;
    fullChartBodies.vertex = true;
    if (filter == fullChartBodies) {
        return SolarTechniqueBodyPreset::FullChartBodies;
    }

    return SolarTechniqueBodyPreset::Custom;
}

void MainWindow::applySolarTechniqueBodyPreset(SolarTechniqueBodyPreset preset, bool refreshView) {
    if (preset == SolarTechniqueBodyPreset::Custom) {
        syncSolarTechniqueBodyPresetSelection(refreshView);
        return;
    }

    SolarTechniqueBodyFilter filter;
    switch (preset) {
        case SolarTechniqueBodyPreset::CoreWithLots:
            filter.lots = true;
            break;
        case SolarTechniqueBodyPreset::FullChartBodies:
            filter.lots = true;
            filter.asteroids = true;
            filter.lilith = true;
            filter.vertex = true;
            break;
        case SolarTechniqueBodyPreset::Core:
        case SolarTechniqueBodyPreset::Custom:
        default:
            break;
    }

    solarTechniqueUpdatingBodyControls_ = true;
    if (solarTechniqueBodyPresetCombo_) {
        const QSignalBlocker blocker(solarTechniqueBodyPresetCombo_);
        const int index = solarTechniqueBodyPresetCombo_->findData(static_cast<int>(preset));
        if (index >= 0) {
            solarTechniqueBodyPresetCombo_->setCurrentIndex(index);
        }
    }
    if (solarTechniqueBodyPlanetsCheck_) {
        const QSignalBlocker blocker(solarTechniqueBodyPlanetsCheck_);
        solarTechniqueBodyPlanetsCheck_->setChecked(filter.planets);
    }
    if (solarTechniqueBodyNodesCheck_) {
        const QSignalBlocker blocker(solarTechniqueBodyNodesCheck_);
        solarTechniqueBodyNodesCheck_->setChecked(filter.nodes);
    }
    if (solarTechniqueBodyAnglesCheck_) {
        const QSignalBlocker blocker(solarTechniqueBodyAnglesCheck_);
        solarTechniqueBodyAnglesCheck_->setChecked(filter.angles);
    }
    if (solarTechniqueBodyLotsCheck_) {
        const QSignalBlocker blocker(solarTechniqueBodyLotsCheck_);
        solarTechniqueBodyLotsCheck_->setChecked(filter.lots);
    }
    if (solarTechniqueBodyAsteroidsCheck_) {
        const QSignalBlocker blocker(solarTechniqueBodyAsteroidsCheck_);
        solarTechniqueBodyAsteroidsCheck_->setChecked(filter.asteroids);
    }
    if (solarTechniqueBodyLilithCheck_) {
        const QSignalBlocker blocker(solarTechniqueBodyLilithCheck_);
        solarTechniqueBodyLilithCheck_->setChecked(filter.lilith);
    }
    if (solarTechniqueBodyVertexCheck_) {
        const QSignalBlocker blocker(solarTechniqueBodyVertexCheck_);
        solarTechniqueBodyVertexCheck_->setChecked(filter.vertex);
    }
    solarTechniqueUpdatingBodyControls_ = false;

    if (refreshView) {
        refreshSolarTechniqueView();
    }
}

void MainWindow::syncSolarTechniqueBodyPresetSelection(bool refreshView) {
    if (solarTechniqueUpdatingBodyControls_) {
        return;
    }

    const SolarTechniqueBodyPreset preset = solarTechniqueBodyPresetFromFilter(solarTechniqueBodyFilter());
    solarTechniqueUpdatingBodyControls_ = true;
    if (solarTechniqueBodyPresetCombo_) {
        const QSignalBlocker blocker(solarTechniqueBodyPresetCombo_);
        const int index = solarTechniqueBodyPresetCombo_->findData(static_cast<int>(preset));
        if (index >= 0) {
            solarTechniqueBodyPresetCombo_->setCurrentIndex(index);
        }
    }
    solarTechniqueUpdatingBodyControls_ = false;

    if (refreshView) {
        refreshSolarTechniqueView();
    }
}

QString MainWindow::solarTechniqueBodyPresetLabel(SolarTechniqueBodyPreset preset) const {
    switch (preset) {
        case SolarTechniqueBodyPreset::Core:
            return "Core (Planets + Nodes + Angles)";
        case SolarTechniqueBodyPreset::CoreWithLots:
            return "Core + Lots";
        case SolarTechniqueBodyPreset::FullChartBodies:
            return "Full Chart Bodies";
        case SolarTechniqueBodyPreset::Custom:
        default:
            return "Custom";
    }
}

QString MainWindow::solarTechniqueBodySummary(const SolarTechniqueBodyFilter& filter) const {
    QStringList enabled;
    if (filter.planets) {
        enabled.push_back("Planets");
    }
    if (filter.nodes) {
        enabled.push_back("Nodes");
    }
    if (filter.angles) {
        enabled.push_back("Angles");
    }
    if (filter.lots) {
        enabled.push_back("Arabic Lots");
    }
    if (filter.asteroids) {
        enabled.push_back("Asteroids");
    }
    if (filter.lilith) {
        enabled.push_back("Lilith");
    }
    if (filter.vertex) {
        enabled.push_back("Vertex");
    }
    return enabled.isEmpty() ? "None" : enabled.join(", ");
}

bool MainWindow::solarTechniqueIncludesBodyName(const QString& name, const SolarTechniqueBodyFilter& filter) const {
    if (isSolarTechniquePlanetName(name)) {
        return filter.planets;
    }
    if (isNodeName(name)) {
        return filter.nodes;
    }
    if (isArabicLotName(name)) {
        return filter.lots;
    }
    if (isAsteroidBody(name)) {
        return filter.asteroids;
    }
    if (name == "Lilith") {
        return filter.lilith;
    }
    return false;
}

void MainWindow::updateSolarTechniqueDockTitles() {
    if (!rightTopDock_ || !rightBottomDock_) {
        return;
    }
    if (activeTab_ == AppTab::SolarReturn && isSolarPlacementFinderTabActive()) {
        rightTopDock_->setWindowTitle("SR Finder Results");
        rightBottomDock_->setWindowTitle("SR Finder Details");
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
    progressionIsLunarReturn_ = false;
    currentProgressedLunarReturn_ = {};
    if (progressionLunarReturnStatusLabel_) {
        progressionLunarReturnStatusLabel_->setText("No progressed lunar return selected.");
    }
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

void MainWindow::handleProgressedLunarReturn(int direction) {
    if (!hasCurrentChart_) {
        setStatusMessage("Load a natal chart first to calculate a progressed lunar return.");
        return;
    }
    if (ephePath_.isEmpty()) {
        setStatusMessage("Ephemeris folder not found. Place ephemeris files in an 'ephe' folder.");
        return;
    }
    if (!progressionDateEdit_ || !progressionTimeEdit_) return;

    const QString timezoneInput = progressionTimezoneEdit_
        ? progressionTimezoneEdit_->text().trimmed() : QString("UTC");
    const QDateTime anchorLocal = progressionTargetLocal();
    ProgressedLunarReturnEvent event;
    QString calculationError;
    if (!findProgressedLunarReturn(swe_, ephePath_, currentInput_, anchorLocal,
                                   timezoneInput, direction, &event, &calculationError)) {
        if (progressionLunarReturnStatusLabel_) {
            progressionLunarReturnStatusLabel_->setText(calculationError);
        }
        setStatusMessage(calculationError);
        return;
    }

    NatalChart chart;
    if (!computeProgressionChart(event.targetLocal, event.timezoneLabel,
                                 &chart, &calculationError)) {
        if (progressionLunarReturnStatusLabel_) {
            progressionLunarReturnStatusLabel_->setText(calculationError);
        }
        setStatusMessage(calculationError);
        return;
    }

    {
        const QSignalBlocker blockDate(progressionDateEdit_);
        const QSignalBlocker blockTime(progressionTimeEdit_);
        progressionDateEdit_->setDate(event.targetLocal.date());
        progressionTimeEdit_->setTime(event.targetLocal.time());
        if (progressionTimezoneEdit_) {
            const QSignalBlocker blockTimezone(progressionTimezoneEdit_);
            progressionTimezoneEdit_->setText(event.timezoneLabel);
        }
    }
    updateProgressionTimezoneStatus();

    currentProgressionChart_ = chart;
    hasProgressionChart_ = true;
    progressionPending_ = false;
    progressionIsLunarReturn_ = true;
    currentProgressedLunarReturn_ = event;
    lastProgressionCalculated_ = QDateTime::currentDateTime();
    currentProgressionInput_ = currentInput_;
    currentProgressionInput_.date = chart.localDateTime.date();
    currentProgressionInput_.time = chart.localDateTime.time();
    currentProgressionInput_.timezone = chart.timezoneLabel;
    currentProgressionInput_.aspectOrbs = aspectOrbs_;

    const QString returnDescription = QString("Progressed Lunar Return #%1 \u00B7 %2")
        .arg(event.returnNumber)
        .arg(event.targetLocal.toString("d MMM yyyy  h:mm:ss AP"));
    if (progressionLunarReturnStatusLabel_) {
        progressionLunarReturnStatusLabel_->setText(returnDescription);
    }
    if (progressionView_ == ProgressionView::NatalOnly
        && progressionViewProgressedRadio_) {
        progressionViewProgressedRadio_->setChecked(true);
    }
    updateProgressionStatusLabels();
    if (activeTab_ == AppTab::Progression) refreshProgressionView();
    refreshNatalReport();
    setStatusMessage(returnDescription);
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
    const bool showProgressedLunarReturn = progressionIsLunarReturn_
        && currentProgressedLunarReturn_.valid && !showNatal;
    if (rightTopDock_) {
        rightTopDock_->setWindowTitle(showProgressedLunarReturn
            ? "Progressed Lunar Return" : "Progression");
    }
    if (rightBottomDock_) {
        rightBottomDock_->setWindowTitle(showProgressedLunarReturn
            ? "Progressed Return-Natal" : "Progression Details");
    }

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
            if (showProgressedLunarReturn) {
                chartWheel_->setChartNote(QString("Progressed Lunar Return #%1 \u00B7 %2")
                    .arg(currentProgressedLunarReturn_.returnNumber)
                    .arg(currentProgressedLunarReturn_.targetLocal.toString(
                        "d MMM yyyy  h:mm:ss AP")));
            }
        }
        if (overlay) {
            populateProgressedAspectsOverlay(currentProgressionChart_, currentChart_);
        } else {
            populateAspects(currentProgressionChart_);
        }
    }
    auto bodyPlacementText = [](const NatalChart& chart, const QString& name) -> QString {
        for (const auto& b : chart.bodies) {
            if (b.name == name) {
                return QString("%1 %2 \u00B7 H%3%4")
                    .arg(b.signName, formatDegOnly(b.longitude))
                    .arg(b.house)
                    .arg(b.retrograde ? " R" : "");
            }
        }
        return QString("-");
    };
    Q_UNUSED(bodyPlacementText);

    if (showNatal || !hasProgressionChart_) {
        if (rightTopTable_) {
            setupTable(rightTopTable_, {"Info"}, 1);
            rightTopTable_->setItem(0, 0, makeCell("Natal chart shown in left panels."));
        }
        if (rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Use View options to switch to progressed charts."));
        }
    } else {
        const QDateTime target = progressionTargetLocal();
        const int age = target.isValid()
            ? completedYearsBetween(currentChart_.localDateTime.date(), target.date())
            : 0;

        // Right-top dock: progressed summary (the key secondary-progression points).
        if (rightTopTable_) {
            QVector<QPair<QString, QString>> rows;
            if (showProgressedLunarReturn) {
                rows.push_back({"Event", QString("Progressed Lunar Return #%1")
                    .arg(currentProgressedLunarReturn_.returnNumber)});
                rows.push_back({"Exact", currentProgressedLunarReturn_.targetLocal.toString(
                    "d MMM yyyy  h:mm:ss AP")});
                rows.push_back({"Timezone", currentProgressedLunarReturn_.timezoneLabel});
                rows.push_back({"Exact Orb", QString("%1\u00B0")
                    .arg(currentProgressedLunarReturn_.exactOrb, 0, 'f', 6)});
                rows.push_back({"Progressed Date", currentProgressionChart_.localDateTime.toString(
                    "d MMM yyyy  h:mm:ss AP")});
            } else if (target.isValid()) {
                rows.push_back({"Progressed To", target.toString("d MMM yyyy")});
            }
            rows.push_back({"Age", QString::number(age)});
            rows.push_back({"Method", "Secondary (day-for-a-year)"});
            rows.push_back({"", ""});
            rows.push_back({"Prog. Sun", bodyPlacementText(currentProgressionChart_, "Sun")});
            rows.push_back({"Prog. Moon", bodyPlacementText(currentProgressionChart_, "Moon")});
            if (showProgressedLunarReturn) {
                rows.push_back({"Natal Moon", bodyPlacementText(currentChart_, "Moon")});
            }
            rows.push_back({"Prog. Mercury", bodyPlacementText(currentProgressionChart_, "Mercury")});
            rows.push_back({"Prog. Venus", bodyPlacementText(currentProgressionChart_, "Venus")});
            rows.push_back({"Prog. Mars", bodyPlacementText(currentProgressionChart_, "Mars")});
            rows.push_back({"Prog. Ascendant", formatDegInSign(currentProgressionChart_.angles.asc)});
            rows.push_back({"Prog. Midheaven", formatDegInSign(currentProgressionChart_.angles.mc)});

            setupTable(rightTopTable_, {"Field", "Value"}, rows.size());
            if (auto* header = rightTopTable_->horizontalHeader()) {
                header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
                header->setSectionResizeMode(1, QHeaderView::Stretch);
            }
            for (int i = 0; i < rows.size(); ++i) {
                auto* keyItem = makeCell(rows[i].first);
                if (!rows[i].first.isEmpty()) {
                    QFont f = keyItem->font();
                    f.setBold(true);
                    keyItem->setFont(f);
                }
                rightTopTable_->setItem(i, 0, keyItem);
                rightTopTable_->setItem(i, 1, makeCell(rows[i].second));
            }
        }

        // Right-bottom dock: where each progressed body falls in the natal chart.
        if (rightBottomTable_) {
            const HouseSystem natalSystem = currentInput_.houseSystem;
            const double natalAsc = currentChart_.angles.asc;
            QVector<BodyPosition> listed;
            for (const auto& b : currentProgressionChart_.bodies) {
                if (isArabicLotName(b.name) || b.name == "Vertex" || isAsteroidBody(b.name)) {
                    continue;
                }
                listed.push_back(b);
            }
            rightBottomTable_->setUpdatesEnabled(false);
            setupTable(rightBottomTable_, {"Prog. Body", "Position", "In Natal House"}, listed.size());
            for (int i = 0; i < listed.size(); ++i) {
                const auto& b = listed[i];
                const int natalHouse = calcHouseForLongitude(b.longitude, natalPlacidusCusps_, natalAsc, natalSystem);
                rightBottomTable_->setItem(i, 0, makeCell(b.name + (b.retrograde ? " R" : "")));
                rightBottomTable_->setItem(i, 1, makeCell(QString("%1 %2").arg(b.signName, formatDegOnly(b.longitude))));
                rightBottomTable_->setItem(i, 2, makeCell(ordinalHouseLabel(natalHouse), Qt::AlignCenter));
            }
            if (auto* header = rightBottomTable_->horizontalHeader()) {
                header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
                header->setSectionResizeMode(1, QHeaderView::Stretch);
                header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            }
            rightBottomTable_->setUpdatesEnabled(true);
        }
    }
    updateProgressionStatusLabels();
    updateChartLegend();
}

void MainWindow::markProgressionPending() {
    progressionPending_ = true;
    if (progressionIsLunarReturn_) {
        progressionIsLunarReturn_ = false;
        currentProgressedLunarReturn_ = {};
        if (progressionLunarReturnStatusLabel_) {
            progressionLunarReturnStatusLabel_->setText(
                "Target changed; find the progressed lunar return again for exactitude.");
        }
    }
    updateProgressionStatusLabels();
}

void MainWindow::updateProgressionStatusLabels() {
    if (!progressionStatusLabel_ || !progressionLastLabel_) {
        return;
    }
    if (progressionPending_) {
        progressionStatusLabel_->setText("Pending changes");
        progressionStatusLabel_->setStyleSheet("color: #d4a24a;");
    } else if (progressionIsLunarReturn_ && currentProgressedLunarReturn_.valid) {
        progressionStatusLabel_->setText("Exact lunar return");
        progressionStatusLabel_->setStyleSheet("color: #69c36d;");
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
    if (progressionLunarReturnPreviousButton_) {
        progressionLunarReturnPreviousButton_->setEnabled(hasCurrentChart_);
    }
    if (progressionLunarReturnNextButton_) {
        progressionLunarReturnNextButton_->setEnabled(hasCurrentChart_);
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
    return computeProgressionChartForInput(currentInput_, localTime, tzLabel, out, error);
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
    const bool showSolarTabs = (activeTab_ == AppTab::SolarReturn
        && !isSolarTechniqueTabActive()
        && !isSolarPlacementFinderTabActive());
    const bool showLunarTabs = (activeTab_ == AppTab::LunarReturn
        && !isLunarPlacementFinderTabActive());
    const bool showRelocationTabs = (activeTab_ == AppTab::Relocation);
    const bool showTabs = showTransitTabs || showSolarTabs || showLunarTabs || showRelocationTabs;
    aspectScopeTabs_->setVisible(showTabs);
    if (showTransitTabs) {
        ensureTabs({"Transit-Natal", "Transit-Transit", "Natal-Natal"});
        aspectScopeTabs_->setCurrentIndex(static_cast<int>(transitAspectView_));
    } else if (showSolarTabs) {
        ensureTabs({"Solar Return", "Solar-Natal"});
        aspectScopeTabs_->setCurrentIndex(static_cast<int>(solarAspectView_));
    } else if (showLunarTabs) {
        ensureTabs({"Lunar Return", "Lunar-Natal"});
        aspectScopeTabs_->setCurrentIndex(static_cast<int>(lunarAspectView_));
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
    } else if (activeTab_ == AppTab::Lunations && lunationOverlay_ && hasCurrentChart_) {
        showLegend = true;
        label = "Natal (inner) / Lunation (outer)";
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

void MainWindow::markSolarPlacementFinderStale() {
    if (!solarPlacementFinderRan_) {
        return;
    }
    solarPlacementFinderStale_ = true;
    if (isSolarPlacementFinderTabActive()) {
        refreshSolarPlacementFinderView();
    }
}

void MainWindow::markSolarPending() {
    solarPending_ = true;
    updateSolarStatusLabels();
    markSolarPlacementFinderStale();
    refreshSolarTechniqueView();
    refreshNatalReport();
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
    const bool canNavigate = hasCurrentChart_ && solarYearSpin_;
    if (solarPreviousButton_) {
        solarPreviousButton_->setEnabled(
            canNavigate && solarYearSpin_->value() > solarYearSpin_->minimum());
    }
    if (solarNowButton_) {
        solarNowButton_->setEnabled(hasCurrentChart_);
    }
    if (solarNextButton_) {
        solarNextButton_->setEnabled(
            canNavigate && solarYearSpin_->value() < solarYearSpin_->maximum());
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
    } else {
        input.lunarNodePolicy = defaultLunarNodePolicy_;
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
    return computeTransitChart(localTime, tzLabel, TropicalComputeOptions{}, out, error);
}

bool MainWindow::computeTransitChart(const QDateTime& localTime, const QString& tzLabel,
                                     const TropicalComputeOptions& options,
                                     NatalChart* out, QString* error) {
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
    const bool ok = engine_.compute(input, options, out, error);
    if (ok && out && !out->warnings.isEmpty() && statusBar()) {
        statusBar()->showMessage(QString("Computed with warnings: %1").arg(out->warnings.join("; ")), 12000);
    }
    return ok;
}

bool MainWindow::resolveSolarReturnContext(QString* outTzLabel, QString* outLocationName, double* outLat, double* outLon,
                                           QString* error) const {
    if (!hasCurrentChart_) {
        if (error) {
            *error = "Load a natal chart first to compute solar return.";
        }
        return false;
    }
    QTimeZone tz;
    QString normLabel;
    QString tzErr;
    const QString tzText = solarTimezoneEdit_ ? solarTimezoneEdit_->text().trimmed() : QString("UTC");
    if (!parseTimezoneInput(tzText, &tz, &normLabel, &tzErr)) {
        if (error) {
            *error = tzErr;
        }
        return false;
    }
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
        if (error) {
            *error = "Set a solar return location or coordinates.";
        }
        return false;
    }
    if (outTzLabel) {
        *outTzLabel = normLabel;
    }
    if (outLocationName) {
        *outLocationName = locationName;
    }
    if (outLat) {
        *outLat = lat;
    }
    if (outLon) {
        *outLon = lon;
    }
    return true;
}

bool MainWindow::solarReturnTimeUtc(int year, const QString& tzLabel, double targetLon,
                                    QDateTime* outUtc, QDateTime* outLocal, QString* error) {
    if (!hasCurrentChart_) {
        if (error) *error = "Load a natal chart first to compute solar return.";
        return false;
    }
    return returncalc::solarReturnTimeUtc(swe_, currentInput_, year, tzLabel, targetLon,
                                          outUtc, outLocal, error);
}

bool MainWindow::computeSolarReturnChartPure(int year, const QString& tzLabel, double targetLon, const QString& locationName,
                                             double lat, double lon, HouseSystem houseSystem,
                                             NatalChart* outChart, NatalInput* outInput, QString* error,
                                             const TropicalComputeOptions& options) {
    Q_UNUSED(locationName);
    if (!outChart) {
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
    input.houseSystem = houseSystem;
    input.aspectOrbs = aspectOrbs_;

    if (!engine_.compute(input, options, outChart, error)) {
        return false;
    }
    if (outInput) {
        *outInput = input;
    }
    return true;
}

bool MainWindow::computeSolarReturnChart(int year, const QString& tzLabel, double targetLon, const QString& locationName,
                                         double lat, double lon, NatalChart* out, QString* error) {
    NatalInput computedInput;
    NatalChart computedChart;
    if (!computeSolarReturnChartPure(year, tzLabel, targetLon, locationName, lat, lon,
                                     currentInput_.houseSystem, &computedChart, &computedInput, error)) {
        return false;
    }
    if (out) {
        *out = computedChart;
    }
    currentSolarInput_ = computedInput;
    currentSolarLocation_ = locationName;
    return true;
}

bool MainWindow::applySolarReturnYear(int year, QString* error) {
    QString tzLabel;
    QString locationName;
    double lat = 0.0;
    double lon = 0.0;
    if (!resolveSolarReturnContext(&tzLabel, &locationName, &lat, &lon, error)) {
        return false;
    }
    double natalSunLon = 0.0;
    if (!findBodyLongitude(currentChart_, "Sun", &natalSunLon)) {
        if (error) {
            *error = "Unable to locate natal Sun longitude.";
        }
        return false;
    }
    NatalChart chart;
    if (!computeSolarReturnChart(year, tzLabel, natalSunLon, locationName, lat, lon, &chart, error)) {
        return false;
    }
    currentSolarChart_ = chart;
    hasSolarChart_ = true;
    solarPending_ = false;
    lastSolarCalculated_ = QDateTime::currentDateTime();
    updateSolarStatusLabels();
    if (activeTab_ == AppTab::SolarReturn) {
        refreshSolarReturnView();
        refreshSolarTechniqueView();
        refreshSolarPlacementFinderView();
    }
    refreshNatalReport();
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
    const int year = solarYearSpin_ ? solarYearSpin_->value() : QDate::currentDate().year();
    QString err;
    if (!applySolarReturnYear(year, &err)) {
        setStatusMessage(err);
        return;
    }
}

void MainWindow::handleSolarShiftYear(int delta) {
    if (!hasCurrentChart_ || !solarYearSpin_ || delta == 0) {
        return;
    }
    const int currentYear = solarYearSpin_->value();
    const int targetYear = std::clamp(
        currentYear + delta, solarYearSpin_->minimum(), solarYearSpin_->maximum());
    if (targetYear == currentYear) {
        setStatusMessage(delta < 0
            ? "Already at the earliest supported solar-return year."
            : "Already at the latest supported solar-return year.");
        return;
    }

    QString err;
    {
        const QSignalBlocker blocker(solarYearSpin_);
        solarYearSpin_->setValue(targetYear);
        if (!applySolarReturnYear(targetYear, &err)) {
            solarYearSpin_->setValue(currentYear);
            updateSolarStatusLabels();
            setStatusMessage(err);
            return;
        }
    }
    updateSolarStatusLabels();
    if (currentSolarChart_.localDateTime.isValid()) {
        setStatusMessage(QString("Solar return: %1")
            .arg(currentSolarChart_.localDateTime.toString("d MMM yyyy  h:mm AP")));
    }
}

void MainWindow::handleSolarNow() {
    QString tzLabel;
    QString err;
    if (!resolveSolarReturnContext(&tzLabel, nullptr, nullptr, nullptr, &err)) {
        setStatusMessage(err);
        return;
    }

    double natalSunLon = 0.0;
    if (!findBodyLongitude(currentChart_, "Sun", &natalSunLon)) {
        setStatusMessage("Unable to locate natal Sun longitude.");
        return;
    }

    QTimeZone tz;
    QString normalizedTimezone;
    if (!parseTimezoneInput(tzLabel, &tz, &normalizedTimezone, &err)) {
        setStatusMessage(err);
        return;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    int returnYear = nowUtc.toTimeZone(tz).date().year();
    QDateTime candidateUtc;
    if (!solarReturnTimeUtc(returnYear, normalizedTimezone, natalSunLon,
                            &candidateUtc, nullptr, &err)) {
        setStatusMessage(err);
        return;
    }
    if (candidateUtc > nowUtc) {
        --returnYear;
    }

    if (!applySolarReturnYear(returnYear, &err)) {
        setStatusMessage(err);
        return;
    }
    if (solarYearSpin_) {
        const QSignalBlocker blocker(solarYearSpin_);
        solarYearSpin_->setValue(returnYear);
    }
    if (currentSolarChart_.localDateTime.isValid()) {
        setStatusMessage(QString("Current solar return: %1")
            .arg(currentSolarChart_.localDateTime.toString("d MMM yyyy  h:mm AP")));
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

static QString solarProfectionRulerForSign(int idx) {
    static const char* rulers[12] = {
        "Mars", "Venus", "Mercury", "Moon", "Sun", "Mercury",
        "Venus", "Mars", "Jupiter", "Saturn", "Saturn", "Jupiter",
    };
    if (idx < 0 || idx >= 12) {
        return QString();
    }
    return QString::fromLatin1(rulers[idx]);
}

// Domicile ruler of a sign. Traditional uses the seven classical rulers;
// modern assigns Scorpio->Pluto, Aquarius->Uranus, Pisces->Neptune.
static QString houseRulerForSign(int idx, bool modern) {
    if (idx < 0 || idx >= 12) {
        return QString();
    }
    if (modern) {
        if (idx == 7) {
            return QStringLiteral("Pluto");    // Scorpio
        }
        if (idx == 10) {
            return QStringLiteral("Uranus");   // Aquarius
        }
        if (idx == 11) {
            return QStringLiteral("Neptune");  // Pisces
        }
    }
    return solarProfectionRulerForSign(idx);
}

static QString ordinalHouseLabel(int house) {
    static const char* names[13] = {
        "-", "1st", "2nd", "3rd", "4th", "5th", "6th",
        "7th", "8th", "9th", "10th", "11th", "12th",
    };
    if (house < 1 || house > 12) {
        return QString("-");
    }
    return QString::fromLatin1(names[house]);
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

    // --- Annual profection for the solar-return year ---
    const int birthYear = currentChart_.localDateTime.date().year();
    const int srYear = currentSolarChart_.localDateTime.isValid()
        ? currentSolarChart_.localDateTime.date().year()
        : birthYear;
    const int age = std::max(0, srYear - birthYear);
    const int ageMod = ((age % 12) + 12) % 12;
    const int profectedHouse = ageMod + 1;
    const int natalAscSign = signIndex(currentChart_.angles.asc);
    const int profectedSignIdx = (natalAscSign + ageMod) % 12;
    const QString profectedSign = signName(profectedSignIdx);
    const QString yearLord = solarProfectionRulerForSign(profectedSignIdx);

    if (chartWheel_) {
        chartWheel_->setChartNote(QString("Profection: %1 house \u00B7 %2 \u00B7 Lord %3")
                                      .arg(ordinalHouseLabel(profectedHouse), profectedSign, yearLord));
    }

    auto bodyPlacement = [](const NatalChart& chart, const QString& name) -> QString {
        for (const auto& b : chart.bodies) {
            if (b.name == name) {
                return QString("%1 \u00B7 H%2%3")
                    .arg(b.signName)
                    .arg(b.house)
                    .arg(b.retrograde ? " R" : "");
            }
        }
        return QString("-");
    };

    // Right-top dock: Solar Return summary + profection of the year.
    const bool ownsRightDocks = !isSolarTechniqueTabActive() && !isSolarPlacementFinderTabActive();
    if (rightTopTable_ && ownsRightDocks) {
        QVector<QPair<QString, QString>> rows;
        rows.push_back({"Solar Return", QString::number(srYear)});
        if (currentSolarChart_.localDateTime.isValid()) {
            rows.push_back({"Exact", currentSolarChart_.localDateTime.toString("d MMM yyyy  h:mm AP")});
        }
        if (!currentSolarLocation_.isEmpty()) {
            rows.push_back({"Location", currentSolarLocation_});
        }
        rows.push_back({"Sect", currentSolarChart_.isDayChart ? "Day chart" : "Night chart"});
        rows.push_back({"Lunar Nodes", lunarNodePolicySummary(currentSolarChart_.lunarNodePolicy)});
        rows.push_back({"SR Ascendant", formatDegInSign(currentSolarChart_.angles.asc)});
        rows.push_back({"SR Midheaven", formatDegInSign(currentSolarChart_.angles.mc)});
        rows.push_back({"", ""});
        rows.push_back({"Profection Age", QString::number(age)});
        rows.push_back({"Profected House", ordinalHouseLabel(profectedHouse)});
        rows.push_back({"Profected Sign", profectedSign});
        rows.push_back({"Lord of the Year", yearLord});
        rows.push_back({"Lord in Natal", bodyPlacement(currentChart_, yearLord)});
        rows.push_back({"Lord in SR", bodyPlacement(currentSolarChart_, yearLord)});

        setupTable(rightTopTable_, {"Field", "Value"}, rows.size());
        if (auto* header = rightTopTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::Stretch);
        }
        for (int i = 0; i < rows.size(); ++i) {
            auto* keyItem = makeCell(rows[i].first);
            if (!rows[i].first.isEmpty()) {
                QFont f = keyItem->font();
                f.setBold(true);
                keyItem->setForeground(QColor("#8a6f54"));
                keyItem->setFont(f);
            }
            rightTopTable_->setItem(i, 0, keyItem);
            rightTopTable_->setItem(i, 1, makeCell(rows[i].second));
        }
    }

    // Right-bottom dock: where each Solar Return body falls in the natal chart.
    if (rightBottomTable_ && ownsRightDocks) {
        const HouseSystem natalSystem = currentInput_.houseSystem;
        const double natalAsc = currentChart_.angles.asc;
        QVector<BodyPosition> listed;
        for (const auto& b : currentSolarChart_.bodies) {
            if (isArabicLotName(b.name) || b.name == "Vertex" || isAsteroidBody(b.name)) {
                continue;
            }
            listed.push_back(b);
        }
        setupTable(rightBottomTable_, {"SR Body", "SR Position", "In Natal House"}, listed.size());
        if (auto* header = rightBottomTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::Stretch);
            header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        }
        for (int i = 0; i < listed.size(); ++i) {
            const auto& b = listed[i];
            const int natalHouse = calcHouseForLongitude(b.longitude, natalPlacidusCusps_, natalAsc, natalSystem);
            rightBottomTable_->setItem(i, 0, makeCell(
                lunarNodeDisplayName(b.name, currentSolarChart_.lunarNodePolicy)
                + (b.retrograde ? " R" : "")));
            rightBottomTable_->setItem(i, 1, makeCell(QString("%1 %2").arg(b.signName, formatDegOnly(b.longitude))));
            rightBottomTable_->setItem(i, 2, makeCell(ordinalHouseLabel(natalHouse), Qt::AlignCenter));
        }
    }
}

// ===================== Lunar Return =====================

void MainWindow::markLunarPending() {
    lunarPending_ = true;
    updateLunarStatusLabels();
    markLunarPlacementFinderStale();
}

void MainWindow::updateLunarStatusLabels() {
    if (!lunarStatusLabel_ || !lunarLastLabel_) {
        return;
    }
    if (lunarPending_) {
        lunarStatusLabel_->setText("Pending changes");
        lunarStatusLabel_->setStyleSheet("color: #d4a24a;");
    } else {
        lunarStatusLabel_->setText("Up to date");
        lunarStatusLabel_->setStyleSheet("color: #69c36d;");
    }
    if (lastLunarCalculated_.isValid()) {
        lunarLastLabel_->setText(QString("Last calculated: %1")
            .arg(lastLunarCalculated_.toString("yyyy-MM-dd HH:mm:ss")));
    } else {
        lunarLastLabel_->setText("Last calculated: -");
    }
    if (lunarCalculateButton_) {
        lunarCalculateButton_->setEnabled(lunarPending_);
    }
    const bool canNavigate = hasLunarChart_ && currentLunarReturnUtc_.isValid();
    if (lunarPrevButton_) {
        lunarPrevButton_->setEnabled(canNavigate);
    }
    if (lunarNowButton_) {
        lunarNowButton_->setEnabled(hasCurrentChart_);
    }
    if (lunarNextButton_) {
        lunarNextButton_->setEnabled(canNavigate);
    }
}

void MainWindow::updateLunarLocationAvailability() {
    if (!lunarUseNatalRadio_ || !lunarUseCustomRadio_) {
        return;
    }
    const bool hasNatal = hasCurrentChart_;
    if (!hasNatal && lunarUseNatalRadio_->isChecked()) {
        QSignalBlocker blocker(lunarUseNatalRadio_);
        lunarUseNatalRadio_->setChecked(false);
        lunarUseCustomRadio_->setChecked(true);
    }
    lunarUseNatalRadio_->setEnabled(hasNatal);
    const bool useNatal = hasNatal && lunarUseNatalRadio_->isChecked();
    if (lunarLocationEdit_) {
        lunarLocationEdit_->setEnabled(!useNatal);
    }
    if (lunarGeocodeButton_) {
        lunarGeocodeButton_->setEnabled(!useNatal);
    }
    if (lunarLatSpin_) {
        lunarLatSpin_->setEnabled(!useNatal);
    }
    if (lunarLonSpin_) {
        lunarLonSpin_->setEnabled(!useNatal);
    }
}

void MainWindow::updateLunarTimezoneStatus() {
    if (!lunarTimezoneStatus_) {
        return;
    }
    QTimeZone tz;
    QString label;
    QString err;
    const QString tzText = lunarTimezoneEdit_ ? lunarTimezoneEdit_->text().trimmed() : QString("UTC");
    if (parseTimezoneInput(tzText, &tz, &label, &err)) {
        lunarTimezoneStatus_->setText("OK");
        lunarTimezoneStatus_->setStyleSheet("color: #69c36d;");
    } else {
        lunarTimezoneStatus_->setText("Invalid");
        lunarTimezoneStatus_->setStyleSheet("color: #e05555;");
    }
}

void MainWindow::syncLunarLocationFromNatal() {
    if (!hasCurrentChart_) {
        return;
    }
    if (!lunarUseNatalRadio_ || !lunarUseNatalRadio_->isChecked()) {
        return;
    }
    if (lunarLocationEdit_) {
        lunarLocationEdit_->setText(currentLocation_);
    }
    if (lunarLatSpin_) {
        lunarLatSpin_->setValue(currentInput_.latitude);
    }
    if (lunarLonSpin_) {
        lunarLonSpin_->setValue(currentInput_.longitude);
    }
    if (lunarTimezoneEdit_ && !currentInput_.timezone.isEmpty()) {
        lunarTimezoneEdit_->setText(currentInput_.timezone);
        updateLunarTimezoneStatus();
    }
}

void MainWindow::handleLunarGeocode() {
    if (!net_) {
        setStatusMessage("Network manager not available.");
        return;
    }
    const QString queryText = lunarLocationEdit_ ? lunarLocationEdit_->text().trimmed() : QString();
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
        if (lunarLatSpin_) {
            lunarLatSpin_->setValue(lat);
        }
        if (lunarLonSpin_) {
            lunarLonSpin_->setValue(lon);
        }
        fetchLunarTimezoneForCoords(lat, lon);
        markLunarPending();
    });
}

void MainWindow::fetchLunarTimezoneForCoords(double lat, double lon) {
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
            if (lunarTimezoneEdit_) {
                lunarTimezoneEdit_->setText(tzName);
                updateLunarTimezoneStatus();
            }
            return;
        }
        const int offsetSeconds = obj.value("utc_offset_seconds").toInt();
        if (offsetSeconds != 0 && lunarTimezoneEdit_) {
            const int totalMinutes = offsetSeconds / 60;
            const int hours = totalMinutes / 60;
            const int minutes = std::abs(totalMinutes % 60);
            const QString sign = hours >= 0 ? "+" : "-";
            const QString label = QString("UTC%1%2:%3")
                .arg(sign)
                .arg(QString::number(std::abs(hours)).rightJustified(2, '0'))
                .arg(QString::number(minutes).rightJustified(2, '0'));
            lunarTimezoneEdit_->setText(label);
            updateLunarTimezoneStatus();
        }
    });
}

bool MainWindow::resolveLunarReturnContext(QString* outTzLabel, QString* outLocationName, double* outLat, double* outLon,
                                           QString* error) const {
    if (!hasCurrentChart_) {
        if (error) {
            *error = "Load a natal chart first to compute lunar return.";
        }
        return false;
    }
    QTimeZone tz;
    QString normLabel;
    QString tzErr;
    const QString tzText = lunarTimezoneEdit_ ? lunarTimezoneEdit_->text().trimmed() : QString("UTC");
    if (!parseTimezoneInput(tzText, &tz, &normLabel, &tzErr)) {
        if (error) {
            *error = tzErr;
        }
        return false;
    }
    const bool useNatal = lunarUseNatalRadio_ && lunarUseNatalRadio_->isChecked();
    const QString locationName = useNatal
        ? currentLocation_
        : (lunarLocationEdit_ ? lunarLocationEdit_->text().trimmed() : QString());
    const double lat = useNatal
        ? currentInput_.latitude
        : (lunarLatSpin_ ? lunarLatSpin_->value() : currentInput_.latitude);
    const double lon = useNatal
        ? currentInput_.longitude
        : (lunarLonSpin_ ? lunarLonSpin_->value() : currentInput_.longitude);
    if (!useNatal && locationName.isEmpty() && std::abs(lat) < 0.0001 && std::abs(lon) < 0.0001) {
        if (error) {
            *error = "Set a lunar return location or coordinates.";
        }
        return false;
    }
    if (outTzLabel) {
        *outTzLabel = normLabel;
    }
    if (outLocationName) {
        *outLocationName = locationName;
    }
    if (outLat) {
        *outLat = lat;
    }
    if (outLon) {
        *outLon = lon;
    }
    return true;
}

bool MainWindow::lunarReturnTimeUtc(const QDateTime& anchorUtc, int direction, double targetLon,
                                    const QString& tzLabel, QDateTime* outUtc, QDateTime* outLocal, QString* error) {
    if (!hasCurrentChart_) {
        if (error) *error = "Load a natal chart first to compute lunar return.";
        return false;
    }
    return returncalc::lunarReturnTimeUtc(swe_, currentInput_, anchorUtc, direction, targetLon,
                                          tzLabel, outUtc, outLocal, error);
}

bool MainWindow::applyLunarReturnAnchor(int direction, bool fromAnchorDate, QString* error,
                                        const QDateTime& absoluteAnchorUtc) {
    QString tzLabel;
    QString locationName;
    double lat = 0.0;
    double lon = 0.0;
    if (!resolveLunarReturnContext(&tzLabel, &locationName, &lat, &lon, error)) {
        return false;
    }
    double natalMoonLon = 0.0;
    if (!findBodyLongitude(currentChart_, "Moon", &natalMoonLon)) {
        if (error) {
            *error = "Unable to locate natal Moon longitude.";
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

    QDateTime anchorUtc;
    int searchDir = (direction >= 0) ? 1 : -1;
    if (absoluteAnchorUtc.isValid()) {
        anchorUtc = absoluteAnchorUtc.toUTC();
    } else if (fromAnchorDate) {
        const QDate anchorDate = lunarAnchorDateEdit_ ? lunarAnchorDateEdit_->date() : QDate::currentDate();
        QDateTime anchorLocal(anchorDate, QTime(0, 0, 0), tz);
        if (!anchorLocal.isValid()) {
            anchorLocal = QDateTime(anchorDate, QTime(12, 0, 0), tz);
        }
        if (!anchorLocal.isValid()) {
            if (error) {
                *error = "Invalid lunar return anchor date.";
            }
            return false;
        }
        anchorUtc = anchorLocal.toUTC();
        searchDir = 1;  // always look forward from the chosen date
    } else {
        if (!currentLunarReturnUtc_.isValid()) {
            if (error) {
                *error = "Calculate a lunar return first.";
            }
            return false;
        }
        // Nudge ~1 day in the travel direction so the current return is not
        // re-detected (consecutive returns are ~27.3 days apart).
        anchorUtc = currentLunarReturnUtc_.addSecs(static_cast<qint64>(searchDir) * 24 * 3600);
    }

    QDateTime retUtc;
    QDateTime retLocal;
    if (!lunarReturnTimeUtc(anchorUtc, searchDir, natalMoonLon, normLabel, &retUtc, &retLocal, error)) {
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

    NatalInput input = currentInput_;
    input.name = QString("Lunar Return %1").arg(retLocal.toString("yyyy-MM-dd"));
    input.date = retLocal.date();
    input.time = retLocal.time();
    input.timezone = normLabel;
    input.latitude = lat;
    input.longitude = lon;
    input.houseSystem = currentInput_.houseSystem;
    input.aspectOrbs = aspectOrbs_;

    NatalChart chart;
    if (!engine_.compute(input, &chart, error)) {
        return false;
    }

    currentLunarChart_ = chart;
    currentLunarInput_ = input;
    currentLunarLocation_ = locationName;
    currentLunarReturnUtc_ = retUtc;
    hasLunarChart_ = true;
    lunarPending_ = false;
    lastLunarCalculated_ = QDateTime::currentDateTime();

    // Keep the anchor date on the displayed return's local date so navigation and
    // a Tropical/Sidereal re-derivation stay locked to the same moment.
    if (lunarAnchorDateEdit_) {
        const QSignalBlocker blocker(lunarAnchorDateEdit_);
        lunarAnchorDateEdit_->setDate(retLocal.date());
    }

    updateLunarStatusLabels();
    if (activeTab_ == AppTab::LunarReturn) {
        refreshLunarReturnView();
    }
    return true;
}

void MainWindow::handleLunarCalculate() {
    QString err;
    if (!applyLunarReturnAnchor(+1, true, &err)) {
        setStatusMessage(err);
        return;
    }
    if (currentLunarChart_.localDateTime.isValid()) {
        setStatusMessage(QString("Lunar return: %1")
            .arg(currentLunarChart_.localDateTime.toString("d MMM yyyy  h:mm AP")));
    }
}

void MainWindow::handleLunarPrev() {
    QString err;
    const bool ok = hasLunarChart_
        ? applyLunarReturnAnchor(-1, false, &err)
        : applyLunarReturnAnchor(+1, true, &err);
    if (!ok) {
        setStatusMessage(err);
    } else if (currentLunarChart_.localDateTime.isValid()) {
        setStatusMessage(QString("Lunar return: %1")
            .arg(currentLunarChart_.localDateTime.toString("d MMM yyyy  h:mm AP")));
    }
}

void MainWindow::handleLunarNow() {
    QString err;
    if (!applyLunarReturnAnchor(-1, false, &err, QDateTime::currentDateTimeUtc())) {
        setStatusMessage(err);
        return;
    }
    if (currentLunarChart_.localDateTime.isValid()) {
        setStatusMessage(QString("Current lunar return: %1")
            .arg(currentLunarChart_.localDateTime.toString("d MMM yyyy  h:mm AP")));
    }
}

void MainWindow::handleLunarNext() {
    QString err;
    const bool ok = hasLunarChart_
        ? applyLunarReturnAnchor(+1, false, &err)
        : applyLunarReturnAnchor(+1, true, &err);
    if (!ok) {
        setStatusMessage(err);
    } else if (currentLunarChart_.localDateTime.isValid()) {
        setStatusMessage(QString("Lunar return: %1")
            .arg(currentLunarChart_.localDateTime.toString("d MMM yyyy  h:mm AP")));
    }
}

void MainWindow::showLunarPlaceholder() {
    const QString message = hasCurrentChart_
        ? "Pick a date and click Find Lunar Return."
        : "Load a natal chart to compute lunar return.";
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
    if (rightTopTable_ && !isLunarPlacementFinderTabActive()) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell(message));
    }
    if (rightBottomTable_ && !isLunarPlacementFinderTabActive()) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell(message));
    }
}

void MainWindow::refreshLunarReturnView() {
    if (activeTab_ != AppTab::LunarReturn) {
        return;
    }
    if (!hasCurrentChart_ || !hasLunarChart_) {
        showLunarPlaceholder();
        return;
    }
    populateSummary(currentLunarChart_, currentLunarInput_, currentLunarLocation_);
    populateAngles(currentLunarChart_);
    populatePlanets(currentLunarChart_);
    populateFixedStars(currentLunarChart_);
    populateHouses(currentLunarChart_, currentLunarInput_.houseSystem);
    if (chartWheel_) {
        chartWheel_->setChart(currentLunarChart_, currentLunarInput_.houseSystem);
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
    }
    switch (lunarAspectView_) {
        case LunarAspectView::LunarNatal:
            populateCrossAspectsOverlay(currentLunarChart_, currentChart_, "Lunar");
            break;
        case LunarAspectView::LunarReturn:
        default:
            populateAspects(currentLunarChart_);
            break;
    }

    const QDateTime lrLocal = currentLunarChart_.localDateTime;
    if (chartWheel_ && lrLocal.isValid()) {
        chartWheel_->setChartNote(QString("Lunar Return \u00B7 %1")
                                      .arg(lrLocal.toString("d MMM yyyy  h:mm AP")));
    }

    auto bodyDetail = [](const NatalChart& chart, const QString& name) -> QString {
        for (const auto& b : chart.bodies) {
            if (b.name == name) {
                return QString("%1 %2 \u00B7 H%3%4")
                    .arg(b.signName, formatDegOnly(b.longitude))
                    .arg(b.house)
                    .arg(b.retrograde ? " R" : "");
            }
        }
        return QString("-");
    };

    // Right-top dock: lunar return summary.
    const bool ownsRightDocks = !isLunarPlacementFinderTabActive();
    if (rightTopTable_ && ownsRightDocks) {
        QVector<QPair<QString, QString>> rows;
        if (lrLocal.isValid()) {
            rows.push_back({"Lunar Return", lrLocal.toString("d MMM yyyy")});
            rows.push_back({"Exact", lrLocal.toString("d MMM yyyy  h:mm AP")});
        }
        if (!currentLunarLocation_.isEmpty()) {
            rows.push_back({"Location", currentLunarLocation_});
        }
        rows.push_back({"Sect", currentLunarChart_.isDayChart ? "Day chart" : "Night chart"});
        rows.push_back({"Lunar Nodes", lunarNodePolicySummary(currentLunarChart_.lunarNodePolicy)});
        rows.push_back({"LR Ascendant", formatDegInSign(currentLunarChart_.angles.asc)});
        rows.push_back({"LR Midheaven", formatDegInSign(currentLunarChart_.angles.mc)});
        rows.push_back({"", ""});
        rows.push_back({"LR Moon", bodyDetail(currentLunarChart_, "Moon")});
        rows.push_back({"Natal Moon", bodyDetail(currentChart_, "Moon")});

        setupTable(rightTopTable_, {"Field", "Value"}, rows.size());
        if (auto* header = rightTopTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::Stretch);
        }
        for (int i = 0; i < rows.size(); ++i) {
            auto* keyItem = makeCell(rows[i].first);
            if (!rows[i].first.isEmpty()) {
                QFont f = keyItem->font();
                f.setBold(true);
                keyItem->setForeground(QColor("#8a6f54"));
                keyItem->setFont(f);
            }
            rightTopTable_->setItem(i, 0, keyItem);
            rightTopTable_->setItem(i, 1, makeCell(rows[i].second));
        }
    }

    // Right-bottom dock: where each Lunar Return body falls in the natal chart.
    if (rightBottomTable_ && ownsRightDocks) {
        const HouseSystem natalSystem = currentInput_.houseSystem;
        const double natalAsc = currentChart_.angles.asc;
        QVector<BodyPosition> listed;
        for (const auto& b : currentLunarChart_.bodies) {
            if (isArabicLotName(b.name) || b.name == "Vertex" || isAsteroidBody(b.name)) {
                continue;
            }
            listed.push_back(b);
        }
        setupTable(rightBottomTable_, {"LR Body", "LR Position", "In Natal House"}, listed.size());
        if (auto* header = rightBottomTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::Stretch);
            header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        }
        for (int i = 0; i < listed.size(); ++i) {
            const auto& b = listed[i];
            const int natalHouse = calcHouseForLongitude(b.longitude, natalPlacidusCusps_, natalAsc, natalSystem);
            rightBottomTable_->setItem(i, 0, makeCell(
                lunarNodeDisplayName(b.name, currentLunarChart_.lunarNodePolicy)
                + (b.retrograde ? " R" : "")));
            rightBottomTable_->setItem(i, 1, makeCell(QString("%1 %2").arg(b.signName, formatDegOnly(b.longitude))));
            rightBottomTable_->setItem(i, 2, makeCell(ordinalHouseLabel(natalHouse), Qt::AlignCenter));
        }
    }
}

// ===================== Lunar Return Placement Finder =====================

bool MainWindow::isLunarPlacementFinderTabActive() const {
    if (activeTab_ != AppTab::LunarReturn) {
        return false;
    }
    if (!tabs_ || !lunarPlacementFinderPanel_) {
        return false;
    }
    return tabs_->currentWidget() == lunarPlacementFinderPanel_;
}

void MainWindow::markLunarPlacementFinderStale() {
    if (!lunarPlacementFinderRan_) {
        return;
    }
    lunarPlacementFinderStale_ = true;
    if (isLunarPlacementFinderTabActive()) {
        refreshLunarPlacementFinderView();
    }
}

void MainWindow::updateLunarReturnDockTitles() {
    if (!rightTopDock_ || !rightBottomDock_) {
        return;
    }
    if (activeTab_ == AppTab::LunarReturn && isLunarPlacementFinderTabActive()) {
        rightTopDock_->setWindowTitle("LR Finder Results");
        rightBottomDock_->setWindowTitle("LR Finder Details");
        return;
    }
    if (activeTab_ == AppTab::LunarReturn) {
        rightTopDock_->setWindowTitle("Lunar Return");
        rightBottomDock_->setWindowTitle("Lunar-Natal");
    }
}

void MainWindow::updateLunarFinderModeAvailability() {
    const int modeIdx = lunarFinderModeCombo_ ? lunarFinderModeCombo_->currentIndex() : 0;
    const bool single = (modeIdx == 0);
    const bool stellium = (modeIdx == 1);
    const bool ruler = (modeIdx == 2);
    const bool profection = (modeIdx == 3);
    if (lunarFinderPlanetCombo_) {
        lunarFinderPlanetCombo_->setEnabled(single);
    }
    if (lunarFinderPlanet2Combo_) {
        lunarFinderPlanet2Combo_->setEnabled(single);
    }
    if (lunarFinderStelliumCountSpin_) {
        lunarFinderStelliumCountSpin_->setEnabled(stellium);
    }
    if (lunarFinderRulerHouseCombo_) {
        lunarFinderRulerHouseCombo_->setEnabled(ruler);
    }
    if (lunarFinderRulerSchemeCombo_) {
        lunarFinderRulerSchemeCombo_->setEnabled(ruler || profection);
    }
    if (lunarFinderConjunctionTargetCombo_) {
        lunarFinderConjunctionTargetCombo_->setEnabled(single);
    }
    if (lunarFinderConjunctionOrbSpin_) {
        lunarFinderConjunctionOrbSpin_->setEnabled(single);
    }
}

void MainWindow::refreshLunarPlacementFinderView() {
    if (!isLunarPlacementFinderTabActive()) {
        return;
    }
    updateLunarReturnDockTitles();
    showLunarPlacementFinderResults();
}

void MainWindow::handleLunarPlacementFinderRun() {
    if (!lunarFinderStartDateEdit_ || !lunarFinderEndDateEdit_ || !lunarFinderPlanetCombo_
        || !lunarFinderHouseCombo_ || !lunarFinderHouseSystemCombo_ || !lunarFinderConjunctionTargetCombo_) {
        return;
    }
    if (lunarFinderStatusLabel_) {
        lunarFinderStatusLabel_->setText("Running");
    }

    QString tzLabel;
    QString locationName;
    double lat = 0.0;
    double lon = 0.0;
    QString err;
    if (!resolveLunarReturnContext(&tzLabel, &locationName, &lat, &lon, &err)) {
        if (lunarFinderStatusLabel_) {
            lunarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage(err);
        refreshLunarPlacementFinderView();
        return;
    }

    double natalMoonLon = 0.0;
    if (!findBodyLongitude(currentChart_, "Moon", &natalMoonLon)) {
        if (lunarFinderStatusLabel_) {
            lunarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage("Unable to locate natal Moon longitude.");
        refreshLunarPlacementFinderView();
        return;
    }

    QTimeZone tz;
    QString normLabel;
    QString tzErr;
    if (!parseTimezoneInput(tzLabel, &tz, &normLabel, &tzErr)) {
        if (lunarFinderStatusLabel_) {
            lunarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage(tzErr);
        refreshLunarPlacementFinderView();
        return;
    }

    QDate startDate = lunarFinderStartDateEdit_->date();
    QDate endDate = lunarFinderEndDateEdit_->date();
    if (startDate > endDate) {
        std::swap(startDate, endDate);
        const QSignalBlocker startBlock(lunarFinderStartDateEdit_);
        const QSignalBlocker endBlock(lunarFinderEndDateEdit_);
        lunarFinderStartDateEdit_->setDate(startDate);
        lunarFinderEndDateEdit_->setDate(endDate);
    }

    const bool stelliumMode = (lunarFinderModeCombo_ && lunarFinderModeCombo_->currentIndex() == 1);
    const bool rulerMode = (lunarFinderModeCombo_ && lunarFinderModeCombo_->currentIndex() == 2);
    const bool profectionMode = (lunarFinderModeCombo_ && lunarFinderModeCombo_->currentIndex() == 3);
    const int stelliumMin = lunarFinderStelliumCountSpin_ ? lunarFinderStelliumCountSpin_->value() : 3;
    const int rulerOfHouse = lunarFinderRulerHouseCombo_ ? lunarFinderRulerHouseCombo_->currentData().toInt() : 7;
    const bool rulerModern = (lunarFinderRulerSchemeCombo_ && lunarFinderRulerSchemeCombo_->currentData().toInt() == 1);

    const QString planetName = lunarFinderPlanetCombo_->currentText().trimmed();
    if (!stelliumMode && !rulerMode && !profectionMode && planetName.isEmpty()) {
        if (lunarFinderStatusLabel_) {
            lunarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage("Select a planet for the finder.");
        refreshLunarPlacementFinderView();
        return;
    }

    int targetHouse = lunarFinderHouseCombo_->currentData().toInt();
    const bool anyHouse = (targetHouse == 0);
    if (!anyHouse && (targetHouse < 1 || targetHouse > 12)) {
        if (lunarFinderStatusLabel_) {
            lunarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage("Select a valid house (1-12).");
        refreshLunarPlacementFinderView();
        return;
    }

    int modeValue = lunarFinderHouseSystemCombo_->currentData().toInt();
    if (modeValue < static_cast<int>(SolarPlacementFinderHouseMode::WholeSign)
        || modeValue > static_cast<int>(SolarPlacementFinderHouseMode::BothAnd)) {
        modeValue = static_cast<int>(SolarPlacementFinderHouseMode::WholeSign);
    }
    const SolarPlacementFinderHouseMode houseMode = static_cast<SolarPlacementFinderHouseMode>(modeValue);
    const QString conjunctionTarget = lunarFinderConjunctionTargetCombo_
        ? lunarFinderConjunctionTargetCombo_->currentText().trimmed()
        : QString("None");
    const bool useConjunction = (conjunctionTarget.compare("None", Qt::CaseInsensitive) != 0);
    const double conjunctionOrbLimit = useConjunction
        ? std::max(0.01, (lunarFinderConjunctionOrbSpin_ ? lunarFinderConjunctionOrbSpin_->value() : 1.0))
        : 0.0;

    if (!stelliumMode && !rulerMode && !profectionMode && anyHouse && !useConjunction) {
        if (lunarFinderStatusLabel_) {
            lunarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage("Single-planet search with \"Any house\" needs a conjunction-to-angle filter, "
                         "otherwise every return matches. Pick a house or set a conjunction target.");
        refreshLunarPlacementFinderView();
        return;
    }
    if ((rulerMode || profectionMode) && anyHouse) {
        if (lunarFinderStatusLabel_) {
            lunarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage("This search needs a specific target House (not \"Any house\").");
        refreshLunarPlacementFinderView();
        return;
    }

    if (!swe_.isLoaded() || ephePath_.isEmpty()) {
        if (lunarFinderStatusLabel_) {
            lunarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage("Swiss Ephemeris or ephemeris folder not available.");
        refreshLunarPlacementFinderView();
        return;
    }
    swe_.setEphePath(ephePath_);

    const HouseSystem computeSystem = (houseMode == SolarPlacementFinderHouseMode::WholeSign)
        ? HouseSystem::WholeSign
        : HouseSystem::Placidus;
    TropicalComputeOptions finderOptions;
    finderOptions.includeArabicLots = false;
    finderOptions.includeFixedStars = false;
    finderOptions.includeAspectGrid = false;

    QVector<LunarPlacementFinderResult> matches;
    QStringList warnings;
    int searchedCount = 0;
    int failedCount = 0;

    // Find the first lunar return on/after the start date, then step through each
    // consecutive return (~27.3 days apart) until past the end date.
    const QDateTime startAnchorLocal(startDate, QTime(0, 0, 0), tz);
    QDateTime anchorUtc = startAnchorLocal.isValid()
        ? startAnchorLocal.toUTC()
        : QDateTime(startDate, QTime(12, 0, 0), tz).toUTC();

    QDateTime curUtc;
    QDateTime curLocal;
    QString firstErr;
    bool haveReturn = lunarReturnTimeUtc(anchorUtc, +1, natalMoonLon, normLabel, &curUtc, &curLocal, &firstErr);

    const int maxReturns = 5000;
    int guard = 0;
    bool capped = false;
    while (haveReturn && curLocal.date() <= endDate) {
        if (guard >= maxReturns) {
            capped = true;
            break;
        }
        ++guard;
        ++searchedCount;

        const QString stamp = curLocal.toString("yyyy-MM-dd");
        QString rowWarning;
        NatalChart chart;
        NatalInput input = currentInput_;
        input.name = QString("Lunar Return %1").arg(stamp);
        input.date = curLocal.date();
        input.time = curLocal.time();
        input.timezone = normLabel;
        input.latitude = lat;
        input.longitude = lon;
        input.houseSystem = computeSystem;
        input.aspectOrbs = aspectOrbs_;

        QString computeErr;
        if (!engine_.compute(input, finderOptions, &chart, &computeErr)) {
            ++failedCount;
            warnings.push_back(QString("%1: %2").arg(stamp, computeErr));
        } else if (stelliumMode) {
            static const QStringList kStelliumBodies = {
                "Sun", "Moon", "Mercury", "Venus", "Mars",
                "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto"
            };
            const bool hasPlacidusCusps = (chart.cusps.size() == 12);
            bool stelliumBlocked = false;
            if (houseMode != SolarPlacementFinderHouseMode::WholeSign && !hasPlacidusCusps) {
                if (houseMode == SolarPlacementFinderHouseMode::Placidus) {
                    ++failedCount;
                    warnings.push_back(QString("%1: Placidus cusps unavailable.").arg(stamp));
                    stelliumBlocked = true;
                } else {
                    rowWarning = "Placidus cusps unavailable; matched by Whole Sign only.";
                    warnings.push_back(QString("%1: %2").arg(stamp, rowWarning));
                }
            }

            if (!stelliumBlocked) {
                QVector<int> wholeCounts(13, 0);
                QVector<int> placidusCounts(13, 0);
                QVector<QStringList> wholeBodies(13);
                QVector<QStringList> placidusBodies(13);
                for (const QString& bodyName : kStelliumBodies) {
                    double bLon = 0.0;
                    if (!findBodyLongitude(chart, bodyName, &bLon)) {
                        continue;
                    }
                    const int hw = calcHouseForLongitude(bLon, {}, chart.angles.asc, HouseSystem::WholeSign);
                    if (hw >= 1 && hw <= 12) {
                        ++wholeCounts[hw];
                        wholeBodies[hw].push_back(bodyName);
                    }
                    if (hasPlacidusCusps) {
                        const int hp = calcHouseForLongitude(bLon, chart.cusps, chart.angles.asc, HouseSystem::Placidus);
                        if (hp >= 1 && hp <= 12) {
                            ++placidusCounts[hp];
                            placidusBodies[hp].push_back(bodyName);
                        }
                    }
                }

                auto bestHouse = [&](const QVector<int>& counts) -> int {
                    if (!anyHouse) {
                        return targetHouse;
                    }
                    int best = 0;
                    int bestCount = -1;
                    for (int h = 1; h <= 12; ++h) {
                        if (counts[h] > bestCount) {
                            bestCount = counts[h];
                            best = h;
                        }
                    }
                    return best;
                };

                const bool checkWhole = (houseMode == SolarPlacementFinderHouseMode::WholeSign
                                         || houseMode == SolarPlacementFinderHouseMode::Both
                                         || houseMode == SolarPlacementFinderHouseMode::BothAnd);
                const bool checkPlacidus = hasPlacidusCusps
                                           && (houseMode == SolarPlacementFinderHouseMode::Placidus
                                               || houseMode == SolarPlacementFinderHouseMode::Both
                                               || houseMode == SolarPlacementFinderHouseMode::BothAnd);

                int wholeHouse = 0;
                int wholeCount = 0;
                QString wholeBodyStr;
                bool wholeMatched = false;
                if (checkWhole) {
                    wholeHouse = bestHouse(wholeCounts);
                    if (wholeHouse >= 1 && wholeHouse <= 12) {
                        wholeCount = wholeCounts[wholeHouse];
                        wholeBodyStr = wholeBodies[wholeHouse].join(", ");
                        wholeMatched = (wholeCount >= stelliumMin);
                    }
                }

                int placidusHouse = 0;
                int placidusCount = 0;
                QString placidusBodyStr;
                bool placidusMatched = false;
                if (checkPlacidus) {
                    placidusHouse = bestHouse(placidusCounts);
                    if (placidusHouse >= 1 && placidusHouse <= 12) {
                        placidusCount = placidusCounts[placidusHouse];
                        placidusBodyStr = placidusBodies[placidusHouse].join(", ");
                        placidusMatched = (placidusCount >= stelliumMin);
                    }
                }

                if (wholeMatched || placidusMatched) {
                    LunarPlacementFinderResult result;
                    result.localDateTime = chart.localDateTime;
                    result.returnUtc = curUtc;
                    result.isStellium = true;
                    result.matchedWhole = wholeMatched;
                    result.matchedPlacidus = placidusMatched;
                    result.stelliumHouseWhole = wholeHouse;
                    result.stelliumHousePlacidus = placidusHouse;
                    result.stelliumCountWhole = wholeCount;
                    result.stelliumCountPlacidus = placidusCount;
                    if (wholeMatched && (!placidusMatched || wholeCount >= placidusCount)) {
                        result.stelliumBodies = wholeBodyStr;
                    } else {
                        result.stelliumBodies = placidusBodyStr;
                    }
                    result.warning = rowWarning;
                    matches.push_back(result);
                }
            }
        } else if (rulerMode) {
            const int ascSign = signIndex(chart.angles.asc);
            const bool hasPlacidusCusps = (chart.cusps.size() == 12);
            const int signXWhole = (ascSign + (rulerOfHouse - 1)) % 12;
            const QString rulerWhole = houseRulerForSign(signXWhole, rulerModern);
            int rulerHouseWhole = 0;
            bool wholeMatch = false;
            double rlonW = 0.0;
            if (!rulerWhole.isEmpty() && findBodyLongitude(chart, rulerWhole, &rlonW)) {
                rulerHouseWhole = calcHouseForLongitude(rlonW, {}, chart.angles.asc, HouseSystem::WholeSign);
                wholeMatch = (rulerHouseWhole == targetHouse);
            }
            QString rulerPlac;
            int rulerHousePlac = 0;
            bool placMatch = false;
            bool rulerBlocked = false;
            if (hasPlacidusCusps) {
                const int signXPlac = signIndex(chart.cusps[rulerOfHouse - 1].longitude);
                rulerPlac = houseRulerForSign(signXPlac, rulerModern);
                double rlonP = 0.0;
                if (!rulerPlac.isEmpty() && findBodyLongitude(chart, rulerPlac, &rlonP)) {
                    rulerHousePlac = calcHouseForLongitude(rlonP, chart.cusps, chart.angles.asc, HouseSystem::Placidus);
                    placMatch = (rulerHousePlac == targetHouse);
                }
            } else if (houseMode == SolarPlacementFinderHouseMode::Placidus) {
                ++failedCount;
                warnings.push_back(QString("%1: Placidus cusps unavailable.").arg(stamp));
                rulerBlocked = true;
            } else if (houseMode == SolarPlacementFinderHouseMode::BothAnd) {
                ++failedCount;
                warnings.push_back(QString("%1: Placidus cusps unavailable; AND match not possible.").arg(stamp));
                rulerBlocked = true;
            } else if (houseMode == SolarPlacementFinderHouseMode::Both) {
                rowWarning = "Placidus cusps unavailable; matched by Whole Sign only.";
                warnings.push_back(QString("%1: %2").arg(stamp, rowWarning));
            }
            if (!rulerBlocked) {
                bool matchedRuler = false;
                switch (houseMode) {
                    case SolarPlacementFinderHouseMode::WholeSign:
                        matchedRuler = wholeMatch;
                        break;
                    case SolarPlacementFinderHouseMode::Placidus:
                        matchedRuler = placMatch;
                        break;
                    case SolarPlacementFinderHouseMode::Both:
                        matchedRuler = (wholeMatch || placMatch);
                        break;
                    case SolarPlacementFinderHouseMode::BothAnd:
                        matchedRuler = (wholeMatch && placMatch);
                        break;
                }
                if (matchedRuler) {
                    LunarPlacementFinderResult result;
                    result.localDateTime = chart.localDateTime;
                    result.returnUtc = curUtc;
                    result.isHouseRuler = true;
                    result.rulerOfHouse = rulerOfHouse;
                    result.rulerNameWhole = rulerWhole;
                    result.rulerNamePlacidus = rulerPlac;
                    result.bodyName = !rulerWhole.isEmpty() ? rulerWhole : rulerPlac;
                    result.houseWhole = rulerHouseWhole;
                    result.housePlacidus = rulerHousePlac;
                    result.matchedWhole = wholeMatch;
                    result.matchedPlacidus = placMatch;
                    result.warning = rowWarning;
                    matches.push_back(result);
                }
            }
        } else if (profectionMode) {
            const int birthYear = currentChart_.localDateTime.isValid()
                ? currentChart_.localDateTime.date().year()
                : curLocal.date().year();
            const int age = std::max(0, curLocal.date().year() - birthYear);
            const int ageMod = ((age % 12) + 12) % 12;
            const int profectedHouse = ageMod + 1;
            const int natalAscSign = signIndex(currentChart_.angles.asc);
            const int profectedSignIdx = (natalAscSign + ageMod) % 12;
            const QString lord = houseRulerForSign(profectedSignIdx, rulerModern);
            double lordLon = 0.0;
            bool profBlocked = false;
            if (lord.isEmpty() || !findBodyLongitude(chart, lord, &lordLon)) {
                ++failedCount;
                warnings.push_back(QString("%1: Lord of the year (%2) unavailable.").arg(stamp, lord));
                profBlocked = true;
            }
            int lordHouseWhole = 0;
            int lordHousePlac = 0;
            if (!profBlocked) {
                lordHouseWhole = calcHouseForLongitude(lordLon, {}, chart.angles.asc, HouseSystem::WholeSign);
                const bool hasPlac = (chart.cusps.size() == 12);
                if (hasPlac) {
                    lordHousePlac = calcHouseForLongitude(lordLon, chart.cusps, chart.angles.asc, HouseSystem::Placidus);
                } else if (houseMode == SolarPlacementFinderHouseMode::Placidus) {
                    ++failedCount;
                    warnings.push_back(QString("%1: Placidus cusps unavailable.").arg(stamp));
                    profBlocked = true;
                } else if (houseMode == SolarPlacementFinderHouseMode::BothAnd) {
                    ++failedCount;
                    warnings.push_back(QString("%1: Placidus cusps unavailable; AND match not possible.").arg(stamp));
                    profBlocked = true;
                } else if (houseMode == SolarPlacementFinderHouseMode::Both) {
                    rowWarning = "Placidus cusps unavailable; matched by Whole Sign only.";
                    warnings.push_back(QString("%1: %2").arg(stamp, rowWarning));
                }
            }
            if (!profBlocked) {
                const bool wholeMatch = (lordHouseWhole == targetHouse);
                const bool placMatch = (lordHousePlac == targetHouse);
                bool matchedLord = false;
                switch (houseMode) {
                    case SolarPlacementFinderHouseMode::WholeSign:
                        matchedLord = wholeMatch;
                        break;
                    case SolarPlacementFinderHouseMode::Placidus:
                        matchedLord = placMatch;
                        break;
                    case SolarPlacementFinderHouseMode::Both:
                        matchedLord = (wholeMatch || placMatch);
                        break;
                    case SolarPlacementFinderHouseMode::BothAnd:
                        matchedLord = (wholeMatch && placMatch);
                        break;
                }
                if (matchedLord) {
                    LunarPlacementFinderResult result;
                    result.localDateTime = chart.localDateTime;
                    result.returnUtc = curUtc;
                    result.isHouseRuler = true;
                    result.isProfectionLord = true;
                    result.rulerOfHouse = profectedHouse;
                    result.rulerNameWhole = lord;
                    result.rulerNamePlacidus = lord;
                    result.bodyName = lord;
                    result.houseWhole = lordHouseWhole;
                    result.housePlacidus = lordHousePlac;
                    result.matchedWhole = wholeMatch;
                    result.matchedPlacidus = placMatch;
                    result.warning = rowWarning;
                    matches.push_back(result);
                }
            }
        } else {
            QStringList finderPlanets;
            finderPlanets << planetName;
            {
                const QString planet2 = lunarFinderPlanet2Combo_ ? lunarFinderPlanet2Combo_->currentText().trimmed() : QString();
                if (!planet2.isEmpty() && planet2.compare("None", Qt::CaseInsensitive) != 0 && planet2 != planetName) {
                    finderPlanets << planet2;
                }
            }
            bool yearMatched = false;
            for (const QString& pName : finderPlanets) {
                if (yearMatched) {
                    break;
                }
                double bodyLon = 0.0;
                if (!findBodyLongitude(chart, pName, &bodyLon)) {
                    continue;
                }
                const int houseWhole = calcHouseForLongitude(bodyLon, {}, chart.angles.asc, HouseSystem::WholeSign);
                int housePlacidus = 0;
                const bool hasPlacidusCusps = (chart.cusps.size() == 12);
                bool placidusBlocked = false;
                if (hasPlacidusCusps) {
                    housePlacidus = calcHouseForLongitude(bodyLon, chart.cusps, chart.angles.asc, HouseSystem::Placidus);
                } else if (houseMode == SolarPlacementFinderHouseMode::Placidus) {
                    ++failedCount;
                    warnings.push_back(QString("%1: Placidus cusps unavailable.").arg(stamp));
                    placidusBlocked = true;
                } else if (houseMode == SolarPlacementFinderHouseMode::BothAnd) {
                    ++failedCount;
                    warnings.push_back(QString("%1: Placidus cusps unavailable; AND match not possible.").arg(stamp));
                    placidusBlocked = true;
                } else if (houseMode == SolarPlacementFinderHouseMode::Both) {
                    rowWarning = "Placidus cusps unavailable; matched by Whole Sign only.";
                    warnings.push_back(QString("%1: %2").arg(stamp, rowWarning));
                }

                if (!placidusBlocked) {
                    const bool matchedWhole = anyHouse ? (houseWhole >= 1 && houseWhole <= 12) : (houseWhole == targetHouse);
                    const bool matchedPlacidus = anyHouse ? (housePlacidus >= 1 && housePlacidus <= 12) : (housePlacidus == targetHouse);
                    bool matchedHouse = false;
                    switch (houseMode) {
                        case SolarPlacementFinderHouseMode::WholeSign:
                            matchedHouse = matchedWhole;
                            break;
                        case SolarPlacementFinderHouseMode::Placidus:
                            matchedHouse = matchedPlacidus;
                            break;
                        case SolarPlacementFinderHouseMode::Both:
                            matchedHouse = (matchedWhole || matchedPlacidus);
                            break;
                        case SolarPlacementFinderHouseMode::BothAnd:
                            matchedHouse = (matchedWhole && matchedPlacidus);
                            break;
                    }

                    if (matchedHouse) {
                        bool matchedConjunction = !useConjunction;
                        QString matchedAngleName;
                        double matchedConjunctionOrb = 0.0;
                        if (useConjunction) {
                            struct AngleCandidate {
                                QString name;
                                double lon = 0.0;
                            };
                            QVector<AngleCandidate> candidates;
                            if (conjunctionTarget.compare("Any Angle", Qt::CaseInsensitive) == 0) {
                                candidates = {
                                    {"Ascendant", chart.angles.asc},
                                    {"Descendant", chart.angles.desc},
                                    {"Midheaven", chart.angles.mc},
                                    {"IC", chart.angles.ic},
                                };
                            } else if (conjunctionTarget.compare("Ascendant", Qt::CaseInsensitive) == 0) {
                                candidates = {{"Ascendant", chart.angles.asc}};
                            } else if (conjunctionTarget.compare("Descendant", Qt::CaseInsensitive) == 0) {
                                candidates = {{"Descendant", chart.angles.desc}};
                            } else if (conjunctionTarget.compare("Midheaven", Qt::CaseInsensitive) == 0
                                       || conjunctionTarget.compare("MC", Qt::CaseInsensitive) == 0) {
                                candidates = {{"Midheaven", chart.angles.mc}};
                            } else if (conjunctionTarget.compare("IC", Qt::CaseInsensitive) == 0) {
                                candidates = {{"IC", chart.angles.ic}};
                            }
                            double bestOrb = 1e9;
                            QString bestName;
                            for (const auto& angle : candidates) {
                                const double orb = angularDiffAbs(bodyLon, angle.lon);
                                if (orb < bestOrb) {
                                    bestOrb = orb;
                                    bestName = angle.name;
                                }
                            }
                            if (!candidates.isEmpty()) {
                                matchedAngleName = bestName;
                                matchedConjunctionOrb = bestOrb;
                                matchedConjunction = (bestOrb <= conjunctionOrbLimit);
                            }
                        }

                        if (matchedConjunction) {
                            LunarPlacementFinderResult result;
                            result.localDateTime = chart.localDateTime;
                            result.returnUtc = curUtc;
                            result.bodyName = pName;
                            result.houseWhole = houseWhole;
                            result.housePlacidus = housePlacidus;
                            result.matchedWhole = matchedWhole;
                            result.matchedPlacidus = matchedPlacidus;
                            result.matchedConjunction = matchedConjunction;
                            result.matchedAngleName = matchedAngleName;
                            result.conjunctionOrb = matchedConjunctionOrb;
                            result.warning = rowWarning;
                            matches.push_back(result);
                            yearMatched = true;
                        }
                    }
                }
            }
        }

        QDateTime nextUtc;
        QDateTime nextLocal;
        QString nextErr;
        // Seed the next search ~24 days ahead (safely before the next return,
        // which is ~27.3 days out) to keep each bracket search short while
        // staying clear of the mid-cycle antipode.
        const QDateTime nextAnchor = curUtc.addSecs(static_cast<qint64>(24) * 24 * 3600);
        if (!lunarReturnTimeUtc(nextAnchor, +1, natalMoonLon, normLabel, &nextUtc, &nextLocal, &nextErr)) {
            break;
        }
        if (!nextUtc.isValid() || nextUtc <= curUtc) {
            break;  // safety against non-progress
        }
        curUtc = nextUtc;
        curLocal = nextLocal;
    }

    if (capped) {
        warnings.push_back(QString("Search capped at %1 returns; narrow the date range.").arg(maxReturns));
    }

    lunarPlacementFinderResults_ = matches;
    lunarPlacementFinderWarnings_ = warnings;
    lunarPlacementFinderRan_ = true;
    lunarPlacementFinderStale_ = false;
    lunarPlacementFinderSelectedIndex_ = lunarPlacementFinderResults_.isEmpty() ? -1 : 0;
    lunarPlacementFinderLastSearchedCount_ = searchedCount;
    lunarPlacementFinderLastFailedCount_ = failedCount;
    lunarPlacementFinderLastStartDate_ = startDate;
    lunarPlacementFinderLastEndDate_ = endDate;
    lunarPlacementFinderLastPlanet_ = planetName;
    lunarPlacementFinderLastHouse_ = targetHouse;
    lunarPlacementFinderLastHouseMode_ = houseMode;
    lunarPlacementFinderLastConjunctionTarget_ = conjunctionTarget;
    lunarPlacementFinderLastConjunctionOrb_ = conjunctionOrbLimit;
    lunarPlacementFinderLastStelliumMode_ = stelliumMode;
    lunarPlacementFinderLastStelliumMin_ = stelliumMin;
    lunarPlacementFinderLastAnyHouse_ = anyHouse;
    lunarPlacementFinderLastRulerMode_ = rulerMode;
    lunarPlacementFinderLastRulerOfHouse_ = rulerOfHouse;
    lunarPlacementFinderLastRulerModern_ = rulerModern;
    lunarPlacementFinderLastProfectionMode_ = profectionMode;

    if (lunarFinderStatusLabel_) {
        if (lunarPlacementFinderResults_.isEmpty()) {
            lunarFinderStatusLabel_->setText("No matches");
        } else if (!lunarPlacementFinderWarnings_.isEmpty()) {
            lunarFinderStatusLabel_->setText("Done with warnings");
        } else {
            lunarFinderStatusLabel_->setText("Done");
        }
    }

    setStatusMessage(QString("LR finder scanned %1 returns, matched %2, failed %3.")
                         .arg(searchedCount)
                         .arg(lunarPlacementFinderResults_.size())
                         .arg(failedCount));
    refreshLunarPlacementFinderView();
}

void MainWindow::showLunarPlacementFinderResults() {
    if (!rightTopTable_ || !rightBottomTable_) {
        return;
    }
    if (!hasCurrentChart_) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Load a natal chart to use the Lunar Return placement finder."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Finder details appear after running a search."));
        return;
    }
    if (!lunarPlacementFinderRan_) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Set range/filter and click Find Matching Returns."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Search results and run summary will appear here."));
        return;
    }
    if (lunarPlacementFinderResults_.isEmpty()) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("No matching lunar returns found for the current filter."));
        showLunarPlacementFinderDetails(-1);
        return;
    }

    auto matchedByLabel = [](const LunarPlacementFinderResult& result) {
        if (result.matchedWhole && result.matchedPlacidus) {
            return QString("Whole + Placidus");
        }
        if (result.matchedWhole) {
            return QString("Whole");
        }
        if (result.matchedPlacidus) {
            return QString("Placidus");
        }
        return QString("-");
    };
    auto conjunctionLabel = [this](const LunarPlacementFinderResult& result) {
        const bool conjunctionFiltered =
            (lunarPlacementFinderLastConjunctionTarget_.trimmed().compare("None", Qt::CaseInsensitive) != 0);
        if (!conjunctionFiltered) {
            return QString("-");
        }
        const QString angleName = result.matchedAngleName.isEmpty()
            ? lunarPlacementFinderLastConjunctionTarget_
            : result.matchedAngleName;
        return QString("%1 (%2 deg)").arg(angleName, QString::number(result.conjunctionOrb, 'f', 2));
    };

    if (lunarPlacementFinderLastStelliumMode_) {
        auto houseCell = [](int house, int count) {
            if (house < 1 || house > 12 || count <= 0) {
                return QString("-");
            }
            return QString("H%1 (%2)").arg(house).arg(count);
        };
        setupTable(rightTopTable_, {"LR Date/Time", "Stellium (Whole)", "Stellium (Placidus)", "Match", "Bodies"},
                   lunarPlacementFinderResults_.size());
        if (auto* header = rightTopTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(4, QHeaderView::Stretch);
        }
        for (int i = 0; i < lunarPlacementFinderResults_.size(); ++i) {
            const auto& result = lunarPlacementFinderResults_[i];
            rightTopTable_->setItem(i, 0, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm")));
            rightTopTable_->setItem(i, 1, makeCell(houseCell(result.stelliumHouseWhole, result.stelliumCountWhole), Qt::AlignCenter));
            rightTopTable_->setItem(i, 2, makeCell(houseCell(result.stelliumHousePlacidus, result.stelliumCountPlacidus), Qt::AlignCenter));
            rightTopTable_->setItem(i, 3, makeCell(matchedByLabel(result)));
            auto* bodiesItem = makeCell(result.stelliumBodies.isEmpty() ? "-" : result.stelliumBodies);
            if (!result.warning.isEmpty()) {
                bodiesItem->setToolTip(result.warning);
            }
            rightTopTable_->setItem(i, 4, bodiesItem);
        }
        const int maxIndex = std::max(0, static_cast<int>(lunarPlacementFinderResults_.size()) - 1);
        lunarPlacementFinderSelectedIndex_ = std::clamp(lunarPlacementFinderSelectedIndex_, 0, maxIndex);
        rightTopTable_->selectRow(lunarPlacementFinderSelectedIndex_);
        showLunarPlacementFinderDetails(lunarPlacementFinderSelectedIndex_);
        return;
    }

    if (lunarPlacementFinderLastRulerMode_ || lunarPlacementFinderLastProfectionMode_) {
        auto houseLabel = [](int house) {
            return (house >= 1 && house <= 12) ? QString("H%1").arg(house) : QString("-");
        };
        const QString rulerHeader = lunarPlacementFinderLastProfectionMode_ ? QString("Lord") : QString("Ruler");
        setupTable(rightTopTable_, {"LR Date/Time", rulerHeader, "In House (Whole)", "In House (Placidus)", "Match"},
                   lunarPlacementFinderResults_.size());
        if (auto* header = rightTopTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(4, QHeaderView::Stretch);
        }
        for (int i = 0; i < lunarPlacementFinderResults_.size(); ++i) {
            const auto& result = lunarPlacementFinderResults_[i];
            rightTopTable_->setItem(i, 0, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm")));
            QString rulerCell = result.rulerNameWhole;
            if (!result.rulerNamePlacidus.isEmpty() && result.rulerNamePlacidus != result.rulerNameWhole) {
                rulerCell = QString("%1 / %2").arg(result.rulerNameWhole.isEmpty() ? "-" : result.rulerNameWhole,
                                                   result.rulerNamePlacidus);
            }
            rightTopTable_->setItem(i, 1, makeCell(rulerCell.isEmpty() ? result.bodyName : rulerCell));
            rightTopTable_->setItem(i, 2, makeCell(houseLabel(result.houseWhole), Qt::AlignCenter));
            rightTopTable_->setItem(i, 3, makeCell(houseLabel(result.housePlacidus), Qt::AlignCenter));
            auto* matchItem = makeCell(matchedByLabel(result));
            if (!result.warning.isEmpty()) {
                matchItem->setToolTip(result.warning);
            }
            rightTopTable_->setItem(i, 4, matchItem);
        }
        const int maxIndex = std::max(0, static_cast<int>(lunarPlacementFinderResults_.size()) - 1);
        lunarPlacementFinderSelectedIndex_ = std::clamp(lunarPlacementFinderSelectedIndex_, 0, maxIndex);
        rightTopTable_->selectRow(lunarPlacementFinderSelectedIndex_);
        showLunarPlacementFinderDetails(lunarPlacementFinderSelectedIndex_);
        return;
    }

    setupTable(rightTopTable_, {"LR Date/Time", "Planet", "House (Whole)", "House (Placidus)",
                                "House Match", "Conjunction"},
               lunarPlacementFinderResults_.size());
    if (auto* header = rightTopTable_->horizontalHeader()) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(5, QHeaderView::Stretch);
    }
    for (int i = 0; i < lunarPlacementFinderResults_.size(); ++i) {
        const auto& result = lunarPlacementFinderResults_[i];
        rightTopTable_->setItem(i, 0, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm")));
        rightTopTable_->setItem(i, 1, makeCell(result.bodyName));
        rightTopTable_->setItem(i, 2, makeCell(result.houseWhole > 0 ? QString::number(result.houseWhole) : "-", Qt::AlignCenter));
        rightTopTable_->setItem(i, 3, makeCell(result.housePlacidus > 0 ? QString::number(result.housePlacidus) : "-", Qt::AlignCenter));
        auto* matchedByItem = makeCell(matchedByLabel(result));
        if (!result.warning.isEmpty()) {
            matchedByItem->setToolTip(result.warning);
        }
        rightTopTable_->setItem(i, 4, matchedByItem);
        auto* conjunctionItem = makeCell(conjunctionLabel(result));
        if (!result.warning.isEmpty()) {
            conjunctionItem->setToolTip(result.warning);
        }
        rightTopTable_->setItem(i, 5, conjunctionItem);
    }

    const int maxIndex = std::max(0, static_cast<int>(lunarPlacementFinderResults_.size()) - 1);
    lunarPlacementFinderSelectedIndex_ = std::clamp(lunarPlacementFinderSelectedIndex_, 0, maxIndex);
    rightTopTable_->selectRow(lunarPlacementFinderSelectedIndex_);
    showLunarPlacementFinderDetails(lunarPlacementFinderSelectedIndex_);
}

void MainWindow::showLunarPlacementFinderDetails(int index) {
    if (!rightBottomTable_) {
        return;
    }
    if (!lunarPlacementFinderRan_) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Run a finder search to view details."));
        return;
    }

    auto houseModeLabel = [](SolarPlacementFinderHouseMode mode) {
        switch (mode) {
            case SolarPlacementFinderHouseMode::Placidus:
                return QString("Placidus");
            case SolarPlacementFinderHouseMode::Both:
                return QString("Both (OR)");
            case SolarPlacementFinderHouseMode::BothAnd:
                return QString("Both (AND)");
            case SolarPlacementFinderHouseMode::WholeSign:
            default:
                return QString("Whole Sign");
        }
    };
    auto conjunctionFilterLabel = [this]() {
        const QString target = lunarPlacementFinderLastConjunctionTarget_.trimmed();
        if (target.isEmpty() || target.compare("None", Qt::CaseInsensitive) == 0) {
            return QString("None");
        }
        return QString("%1 (<= %2 deg)")
            .arg(target, QString::number(lunarPlacementFinderLastConjunctionOrb_, 'f', 2));
    };
    const bool conjunctionFiltered =
        (lunarPlacementFinderLastConjunctionTarget_.trimmed().compare("None", Qt::CaseInsensitive) != 0);

    QString warningsSummary = "-";
    if (!lunarPlacementFinderWarnings_.isEmpty()) {
        const int maxItems = 3;
        if (lunarPlacementFinderWarnings_.size() <= maxItems) {
            warningsSummary = lunarPlacementFinderWarnings_.join(" | ");
        } else {
            warningsSummary = QString("%1 (+%2 more)")
                .arg(lunarPlacementFinderWarnings_.mid(0, maxItems).join(" | "))
                .arg(lunarPlacementFinderWarnings_.size() - maxItems);
        }
    }

    const QString rangeLabel = QString("%1 -> %2")
        .arg(lunarPlacementFinderLastStartDate_.toString("yyyy-MM-dd"),
             lunarPlacementFinderLastEndDate_.toString("yyyy-MM-dd"));
    const QString stateLabel = lunarPlacementFinderStale_
        ? "Stale (filters/inputs changed)"
        : "Current";
    QString criteriaLabel;
    if (lunarPlacementFinderLastProfectionMode_) {
        criteriaLabel = QString("Lord of the Year (%1) in House %2")
                            .arg(lunarPlacementFinderLastRulerModern_ ? "Modern" : "Traditional")
                            .arg(lunarPlacementFinderLastHouse_);
    } else if (lunarPlacementFinderLastRulerMode_) {
        criteriaLabel = QString("Ruler of House %1 (%2) in House %3")
                            .arg(lunarPlacementFinderLastRulerOfHouse_)
                            .arg(lunarPlacementFinderLastRulerModern_ ? "Modern" : "Traditional")
                            .arg(lunarPlacementFinderLastHouse_);
    } else if (lunarPlacementFinderLastStelliumMode_) {
        criteriaLabel = QString("Stellium: >= %1 planets in %2")
                            .arg(lunarPlacementFinderLastStelliumMin_)
                            .arg(lunarPlacementFinderLastAnyHouse_ ? QString("a single house")
                                                                   : QString("House %1").arg(lunarPlacementFinderLastHouse_));
    } else {
        criteriaLabel = QString("%1 in %2").arg(lunarPlacementFinderLastPlanet_,
                            lunarPlacementFinderLastAnyHouse_ ? QString("Any house")
                                                              : QString("House %1").arg(lunarPlacementFinderLastHouse_));
    }
    const QString criteriaKeyLabel = lunarPlacementFinderLastProfectionMode_
        ? QString("Profection Criteria")
        : (lunarPlacementFinderLastRulerMode_
               ? QString("House-Ruler Criteria")
               : (lunarPlacementFinderLastStelliumMode_ ? QString("Stellium Criteria") : QString("Planet / House")));

    if (index < 0 || index >= lunarPlacementFinderResults_.size()) {
        setupTable(rightBottomTable_, {"Item", "Value"}, 9);
        int row = 0;
        rightBottomTable_->setItem(row, 0, makeCell("Run Range"));
        rightBottomTable_->setItem(row++, 1, makeCell(rangeLabel));
        rightBottomTable_->setItem(row, 0, makeCell(criteriaKeyLabel));
        rightBottomTable_->setItem(row++, 1, makeCell(criteriaLabel));
        rightBottomTable_->setItem(row, 0, makeCell("House Mode"));
        rightBottomTable_->setItem(row++, 1, makeCell(houseModeLabel(lunarPlacementFinderLastHouseMode_)));
        rightBottomTable_->setItem(row, 0, makeCell("Conjunction Filter"));
        rightBottomTable_->setItem(row++, 1, makeCell(conjunctionFilterLabel()));
        rightBottomTable_->setItem(row, 0, makeCell("Returns Scanned"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(lunarPlacementFinderLastSearchedCount_)));
        rightBottomTable_->setItem(row, 0, makeCell("Matched Returns"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(lunarPlacementFinderResults_.size())));
        rightBottomTable_->setItem(row, 0, makeCell("Failed Returns"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(lunarPlacementFinderLastFailedCount_)));
        rightBottomTable_->setItem(row, 0, makeCell("Warnings"));
        auto* warningItem = makeCell(warningsSummary);
        warningItem->setToolTip(lunarPlacementFinderWarnings_.join("\n"));
        rightBottomTable_->setItem(row++, 1, warningItem);
        rightBottomTable_->setItem(row, 0, makeCell("Result State"));
        rightBottomTable_->setItem(row++, 1, makeCell(stateLabel));
        rightBottomTable_->setWordWrap(true);
        rightBottomTable_->resizeRowsToContents();
        return;
    }

    const auto& result = lunarPlacementFinderResults_[index];

    if (result.isStellium) {
        auto stelliumHouseLabel = [](int house, int count) {
            if (house < 1 || house > 12 || count <= 0) {
                return QString("-");
            }
            return QString("House %1 (%2 planets)").arg(house).arg(count);
        };
        QString matchedBy = "-";
        if (result.matchedWhole && result.matchedPlacidus) {
            matchedBy = "Whole + Placidus";
        } else if (result.matchedWhole) {
            matchedBy = "Whole";
        } else if (result.matchedPlacidus) {
            matchedBy = "Placidus";
        }
        setupTable(rightBottomTable_, {"Item", "Value"}, 11);
        int row = 0;
        rightBottomTable_->setItem(row, 0, makeCell("LR Local Date/Time"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm:ss")));
        rightBottomTable_->setItem(row, 0, makeCell("Criteria"));
        rightBottomTable_->setItem(row++, 1, makeCell(criteriaLabel));
        rightBottomTable_->setItem(row, 0, makeCell("House Mode"));
        rightBottomTable_->setItem(row++, 1, makeCell(houseModeLabel(lunarPlacementFinderLastHouseMode_)));
        rightBottomTable_->setItem(row, 0, makeCell("Stellium (Whole)"));
        rightBottomTable_->setItem(row++, 1, makeCell(stelliumHouseLabel(result.stelliumHouseWhole, result.stelliumCountWhole)));
        rightBottomTable_->setItem(row, 0, makeCell("Stellium (Placidus)"));
        rightBottomTable_->setItem(row++, 1, makeCell(stelliumHouseLabel(result.stelliumHousePlacidus, result.stelliumCountPlacidus)));
        rightBottomTable_->setItem(row, 0, makeCell("Matched By"));
        rightBottomTable_->setItem(row++, 1, makeCell(matchedBy));
        rightBottomTable_->setItem(row, 0, makeCell("Bodies"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.stelliumBodies.isEmpty() ? "-" : result.stelliumBodies));
        rightBottomTable_->setItem(row, 0, makeCell("Run Range"));
        rightBottomTable_->setItem(row++, 1, makeCell(rangeLabel));
        rightBottomTable_->setItem(row, 0, makeCell("Matched Returns"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(lunarPlacementFinderResults_.size())));
        rightBottomTable_->setItem(row, 0, makeCell("Row Notes"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.warning.isEmpty() ? "-" : result.warning));
        rightBottomTable_->setWordWrap(true);
        rightBottomTable_->resizeRowsToContents();
        return;
    }

    if (result.isHouseRuler) {
        auto houseLabel = [](int house) {
            return (house >= 1 && house <= 12) ? QString("House %1").arg(house) : QString("-");
        };
        QString matchedBy = "-";
        if (result.matchedWhole && result.matchedPlacidus) {
            matchedBy = "Whole + Placidus";
        } else if (result.matchedWhole) {
            matchedBy = "Whole";
        } else if (result.matchedPlacidus) {
            matchedBy = "Placidus";
        }
        setupTable(rightBottomTable_, {"Item", "Value"}, 12);
        int row = 0;
        rightBottomTable_->setItem(row, 0, makeCell("LR Local Date/Time"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm:ss")));
        rightBottomTable_->setItem(row, 0, makeCell("Criteria"));
        rightBottomTable_->setItem(row++, 1, makeCell(criteriaLabel));
        rightBottomTable_->setItem(row, 0, makeCell("House Mode"));
        rightBottomTable_->setItem(row++, 1, makeCell(houseModeLabel(lunarPlacementFinderLastHouseMode_)));
        rightBottomTable_->setItem(row, 0, makeCell("Ruler (Whole)"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.rulerNameWhole.isEmpty() ? "-" : result.rulerNameWhole));
        rightBottomTable_->setItem(row, 0, makeCell("Ruler (Placidus)"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.rulerNamePlacidus.isEmpty() ? "-" : result.rulerNamePlacidus));
        rightBottomTable_->setItem(row, 0, makeCell("In House (Whole)"));
        rightBottomTable_->setItem(row++, 1, makeCell(houseLabel(result.houseWhole)));
        rightBottomTable_->setItem(row, 0, makeCell("In House (Placidus)"));
        rightBottomTable_->setItem(row++, 1, makeCell(houseLabel(result.housePlacidus)));
        rightBottomTable_->setItem(row, 0, makeCell("Matched By"));
        rightBottomTable_->setItem(row++, 1, makeCell(matchedBy));
        rightBottomTable_->setItem(row, 0, makeCell("Run Range"));
        rightBottomTable_->setItem(row++, 1, makeCell(rangeLabel));
        rightBottomTable_->setItem(row, 0, makeCell("Matched Returns"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(lunarPlacementFinderResults_.size())));
        rightBottomTable_->setItem(row, 0, makeCell("Row Notes"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.warning.isEmpty() ? "-" : result.warning));
        rightBottomTable_->setWordWrap(true);
        rightBottomTable_->resizeRowsToContents();
        return;
    }

    QString matchedBy = "-";
    if (result.matchedWhole && result.matchedPlacidus) {
        matchedBy = "Whole + Placidus";
    } else if (result.matchedWhole) {
        matchedBy = "Whole";
    } else if (result.matchedPlacidus) {
        matchedBy = "Placidus";
    }
    QString conjunctionMatch = "Not filtered";
    if (conjunctionFiltered) {
        const QString angleName = result.matchedAngleName.isEmpty()
            ? lunarPlacementFinderLastConjunctionTarget_
            : result.matchedAngleName;
        conjunctionMatch = QString("%1 (%2 deg)")
            .arg(angleName, QString::number(result.conjunctionOrb, 'f', 2));
    }

    setupTable(rightBottomTable_, {"Item", "Value"}, 14);
    int row = 0;
    rightBottomTable_->setItem(row, 0, makeCell("LR Local Date/Time"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm:ss")));
    rightBottomTable_->setItem(row, 0, makeCell("Planet / Target House"));
    rightBottomTable_->setItem(row++, 1, makeCell(criteriaLabel));
    rightBottomTable_->setItem(row, 0, makeCell("House Mode"));
    rightBottomTable_->setItem(row++, 1, makeCell(houseModeLabel(lunarPlacementFinderLastHouseMode_)));
    rightBottomTable_->setItem(row, 0, makeCell("House (Whole)"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.houseWhole > 0 ? QString::number(result.houseWhole) : "-"));
    rightBottomTable_->setItem(row, 0, makeCell("House (Placidus)"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.housePlacidus > 0 ? QString::number(result.housePlacidus) : "-"));
    rightBottomTable_->setItem(row, 0, makeCell("Matched By"));
    rightBottomTable_->setItem(row++, 1, makeCell(matchedBy));
    rightBottomTable_->setItem(row, 0, makeCell("Conjunction Filter"));
    rightBottomTable_->setItem(row++, 1, makeCell(conjunctionFilterLabel()));
    rightBottomTable_->setItem(row, 0, makeCell("Conjunction Match"));
    rightBottomTable_->setItem(row++, 1, makeCell(conjunctionMatch));
    rightBottomTable_->setItem(row, 0, makeCell("Run Range"));
    rightBottomTable_->setItem(row++, 1, makeCell(rangeLabel));
    rightBottomTable_->setItem(row, 0, makeCell("Returns Scanned"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString::number(lunarPlacementFinderLastSearchedCount_)));
    rightBottomTable_->setItem(row, 0, makeCell("Matched Returns"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString::number(lunarPlacementFinderResults_.size())));
    rightBottomTable_->setItem(row, 0, makeCell("Result State"));
    rightBottomTable_->setItem(row++, 1, makeCell(stateLabel));
    rightBottomTable_->setItem(row, 0, makeCell("Row Notes"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.warning.isEmpty() ? "-" : result.warning));
    rightBottomTable_->setWordWrap(true);
    rightBottomTable_->resizeRowsToContents();
}

void MainWindow::handleLunarPlacementFinderResultActivated(int row, int column) {
    Q_UNUSED(column);
    if (row < 0 || row >= lunarPlacementFinderResults_.size()) {
        return;
    }
    lunarPlacementFinderSelectedIndex_ = row;
    const QDateTime localDt = lunarPlacementFinderResults_[row].localDateTime;
    if (localDt.isValid() && lunarAnchorDateEdit_) {
        const QSignalBlocker blocker(lunarAnchorDateEdit_);
        lunarAnchorDateEdit_->setDate(localDt.date());
    }
    QString err;
    if (!applyLunarReturnAnchor(+1, true, &err)) {
        setStatusMessage(err);
        showLunarPlacementFinderDetails(row);
        return;
    }
    if (isLunarPlacementFinderTabActive()) {
        showLunarPlacementFinderResults();
        if (rightTopTable_ && row >= 0 && row < rightTopTable_->rowCount()) {
            rightTopTable_->selectRow(row);
        }
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
    // Right-top dock: relocated angles vs natal (relocation moves the angles/houses).
    if (rightTopTable_) {
        QVector<QPair<QString, QString>> rows;
        if (!currentRelocationLocation_.isEmpty()) {
            rows.push_back({"Location", currentRelocationLocation_});
        }
        rows.push_back({"House System",
                        currentRelocationInput_.houseSystem == HouseSystem::Placidus ? "Placidus" : "Whole Sign"});
        rows.push_back({"", ""});
        auto angleRow = [&](const QString& label, double reloc, double natal) {
            rows.push_back({label, QString("%1   (natal %2)").arg(formatDegInSign(reloc), formatDegInSign(natal))});
        };
        angleRow("Relocated ASC", currentRelocationChart_.angles.asc, currentChart_.angles.asc);
        angleRow("Relocated MC", currentRelocationChart_.angles.mc, currentChart_.angles.mc);
        angleRow("Relocated DESC", currentRelocationChart_.angles.desc, currentChart_.angles.desc);
        angleRow("Relocated IC", currentRelocationChart_.angles.ic, currentChart_.angles.ic);

        setupTable(rightTopTable_, {"Field", "Value"}, rows.size());
        if (auto* header = rightTopTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::Stretch);
        }
        for (int i = 0; i < rows.size(); ++i) {
            auto* keyItem = makeCell(rows[i].first);
            if (!rows[i].first.isEmpty()) {
                QFont f = keyItem->font();
                f.setBold(true);
                keyItem->setFont(f);
            }
            rightTopTable_->setItem(i, 0, keyItem);
            rightTopTable_->setItem(i, 1, makeCell(rows[i].second));
        }
    }

    // Right-bottom dock: each body's house at the relocated chart.
    if (rightBottomTable_) {
        QVector<BodyPosition> listed;
        for (const auto& b : currentRelocationChart_.bodies) {
            if (isArabicLotName(b.name) || b.name == "Vertex" || isAsteroidBody(b.name)) {
                continue;
            }
            listed.push_back(b);
        }
        rightBottomTable_->setUpdatesEnabled(false);
        setupTable(rightBottomTable_, {"Body", "Position", "Relocated House"}, listed.size());
        for (int i = 0; i < listed.size(); ++i) {
            const auto& b = listed[i];
            rightBottomTable_->setItem(i, 0, makeCell(b.name + (b.retrograde ? " R" : "")));
            rightBottomTable_->setItem(i, 1, makeCell(QString("%1 %2").arg(b.signName, formatDegOnly(b.longitude))));
            rightBottomTable_->setItem(i, 2, makeCell(ordinalHouseLabel(b.house), Qt::AlignCenter));
        }
        if (auto* header = rightBottomTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::Stretch);
            header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        }
        rightBottomTable_->setUpdatesEnabled(true);
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

    const SolarTechniqueBodyFilter bodyFilter = solarTechniqueBodyFilter();
    if (!bodyFilter.planets && !bodyFilter.nodes && !bodyFilter.angles && !bodyFilter.lots
        && !bodyFilter.asteroids && !bodyFilter.lilith && !bodyFilter.vertex) {
        setInfo("Enable at least one Technique Bodies category.");
        return;
    }
    const SolarTechniqueBodyPreset bodyPreset = solarTechniqueBodyPresetFromFilter(bodyFilter);
    const QString bodyPresetLabel = solarTechniqueBodyPresetLabel(bodyPreset);
    const QString bodySummary = solarTechniqueBodySummary(bodyFilter);

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

    auto currentSolarTechniqueCountingMode = [this]() {
        if (!solarTechniqueModeCombo_) {
            return SolarTechniqueCountingMode::SRStartDate;
        }
        const int modeValue = solarTechniqueModeCombo_->currentData().toInt();
        if (modeValue == static_cast<int>(SolarTechniqueCountingMode::SymbolicJanuaryFirst)) {
            return SolarTechniqueCountingMode::SymbolicJanuaryFirst;
        }
        return SolarTechniqueCountingMode::SRStartDate;
    };
    const SolarTechniqueCountingMode countingMode = currentSolarTechniqueCountingMode();
    const QString countingModeLabel =
        countingMode == SolarTechniqueCountingMode::SymbolicJanuaryFirst
            ? "Symbolic January 1"
            : "SR Start Date";

    const int solarYear = solarYearSpin_ ? solarYearSpin_->value() : currentSolarInput_.date.year();
    QDate startDate;
    QDate endDate;
    if (countingMode == SolarTechniqueCountingMode::SymbolicJanuaryFirst) {
        startDate = QDate(solarYear, 1, 1);
        endDate = QDate(solarYear, 12, 31);
    } else {
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

        startDate = startLocal.date();
        endDate = nextLocal.date().addDays(-1);
        if (!endDate.isValid() || endDate < startDate) {
            endDate = startDate;
        }
    }
    const qint64 totalDays = std::max<qint64>(1, static_cast<qint64>(startDate.daysTo(endDate)) + 1);
    if (solarTechniqueRangeLabel_) {
        solarTechniqueRangeLabel_->setText(QString("%1 | %2 -> %3 (%4 days)")
                                               .arg(countingModeLabel)
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
        QString preview;
        double orb = 0.0;
        bool supportive = false;
        bool challenging = false;
    };

    struct DayRow {
        QDate date;
        int dayNumber = 0;
        double dailyLon = 0.0;
        QString dailyLabel;
        QStringList support;
        QStringList challenge;
        QStringList neutral;
        int supportCount = 0;
        int challengeCount = 0;
        int neutralCount = 0;
        int netScore = 0;
        QString strongestHit;
        QString strongestPreview;
        int strongestTone = 0;
    };

    struct MonthRow {
        QDate monthStart;
        int positiveDays = 0;
        int negativeDays = 0;
        int neutralDays = 0;
        int dayBalance = 0;
        int totalSupport = 0;
        int totalChallenge = 0;
        int totalNeutral = 0;
        int totalNet = 0;
        QString strongestPreview;
        int strongestTone = 0;
        int strongestNetMagnitude = -1;
        int strongestHitCount = -1;
        QDate strongestDate;
    };

    auto formatHitPreview = [&](const QString& aspectLabel, const QString& scopeLabel, const QString& targetLabel) {
        return QString("%1 %2 %3")
            .arg(aspectLabel)
            .arg(scopeLabel)
            .arg(targetLabel);
    };

    auto formatHitText = [&](const QString& preview, double orb) {
        return QString("%1 (%2°)")
            .arg(preview)
            .arg(QString::number(orb, 'f', 2));
    };

    auto collectHitsForLon = [&](double lon) {
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
                const QString preview = formatHitPreview(label, scopeLabel, name);
                const QString text = formatHitText(preview, orb);
                hits.push_back({text, preview, orb, supportive, challenging});
            };

            bool hasPartOfFortuneBody = false;
            for (const auto& body : chart.bodies) {
                if (!solarTechniqueIncludesBodyName(body.name, bodyFilter)) {
                    continue;
                }
                if (body.name == "Part of Fortune") {
                    hasPartOfFortuneBody = true;
                }
                handleTarget(body.name, body.longitude);
            }
            if (bodyFilter.lots && chart.hasPartOfFortune && !hasPartOfFortuneBody) {
                handleTarget("Part of Fortune", chart.partOfFortune);
            }
            if (bodyFilter.angles) {
                handleTarget("Ascendant", chart.angles.asc);
                handleTarget("Midheaven", chart.angles.mc);
                handleTarget("Descendant", chart.angles.desc);
                handleTarget("IC", chart.angles.ic);
            }
            if (bodyFilter.vertex) {
                handleTarget("Vertex", chart.angles.vertex);
            }
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

        return hits;
    };

    QVector<DayRow> days;
    days.reserve(static_cast<int>(totalDays));
    for (qint64 i = 0; i < totalDays; ++i) {
        DayRow row;
        row.date = startDate.addDays(static_cast<int>(i));
        row.dayNumber = static_cast<int>(i) + 1;
        row.dailyLon = normalizeDegrees(srAsc + static_cast<double>(i));
        row.dailyLabel = formatDegInSign(row.dailyLon);
        const QVector<Hit> hits = collectHitsForLon(row.dailyLon);
        if (!hits.isEmpty()) {
            row.strongestHit = hits.first().text;
            row.strongestPreview = hits.first().preview;
            row.strongestTone = hits.first().supportive ? 1 : (hits.first().challenging ? -1 : 0);
        }
        for (const auto& hit : hits) {
            if (hit.supportive) {
                ++row.supportCount;
                row.support.push_back(hit.text);
            } else if (hit.challenging) {
                ++row.challengeCount;
                row.challenge.push_back(hit.text);
            } else {
                ++row.neutralCount;
                row.neutral.push_back(hit.text);
            }
        }
        row.netScore = row.supportCount - row.challengeCount;
        days.push_back(row);
    }

    QMap<QDate, MonthRow> monthMap;
    for (const auto& day : days) {
        if (!day.date.isValid()) {
            continue;
        }
        const QDate monthStart(day.date.year(), day.date.month(), 1);
        MonthRow& month = monthMap[monthStart];
        month.monthStart = monthStart;
        if (day.netScore > 0) {
            ++month.positiveDays;
        } else if (day.netScore < 0) {
            ++month.negativeDays;
        } else {
            ++month.neutralDays;
        }
        month.dayBalance = month.positiveDays - month.negativeDays;
        month.totalSupport += day.supportCount;
        month.totalChallenge += day.challengeCount;
        month.totalNeutral += day.neutralCount;
        month.totalNet += day.netScore;

        const int netMagnitude = std::abs(day.netScore);
        const int hitCount = day.supportCount + day.challengeCount + day.neutralCount;
        if (!day.strongestPreview.isEmpty()
            && (month.strongestPreview.isEmpty()
                || netMagnitude > month.strongestNetMagnitude
                || (netMagnitude == month.strongestNetMagnitude && hitCount > month.strongestHitCount)
                || (netMagnitude == month.strongestNetMagnitude && hitCount == month.strongestHitCount
                    && (!month.strongestDate.isValid() || day.date < month.strongestDate)))) {
            month.strongestPreview = day.strongestPreview;
            month.strongestTone = day.strongestTone;
            month.strongestNetMagnitude = netMagnitude;
            month.strongestHitCount = hitCount;
            month.strongestDate = day.date;
        }
    }

    QVector<MonthRow> months;
    months.reserve(monthMap.size());
    for (auto it = monthMap.cbegin(); it != monthMap.cend(); ++it) {
        months.push_back(it.value());
    }

    const auto signedCountLabel = [](int value) {
        return value > 0 ? QString("+%1").arg(value) : QString::number(value);
    };
    const auto shortDateLabel = [](const QDate& date) {
        return date.isValid() ? date.toString("ddd, MMM d") : QString("-");
    };
    const auto longDateLabel = [](const QDate& date) {
        return date.isValid() ? date.toString("ddd, MMM d, yyyy") : QString("-");
    };
    const auto monthLabel = [](const QDate& date) {
        return date.isValid() ? date.toString("MMMM yyyy") : QString("-");
    };
    const auto listTextOrNone = [](const QStringList& items) {
        return items.isEmpty() ? QString("None") : items.join("\n");
    };
    const auto toneColor = [](int tone) {
        if (tone > 0) {
            return QColor("#147a67");
        }
        if (tone < 0) {
            return QColor("#c4543b");
        }
        return QColor("#7b725f");
    };
    const QColor goodColor("#147a67");
    const QColor badColor("#c4543b");
    const QColor neutralColor("#7b725f");
    const QColor monthBreakColor("#efe5d2");

    if (rightTopTable_) {
        const int rows = days.size();
        setupTable(rightTopTable_, {"Date", "Day #", "Degree", "Net", "+", "-", "0", "Strongest Hit"}, rows);
        rightTopTable_->setWordWrap(false);
        rightTopTable_->setTextElideMode(Qt::ElideRight);
        rightTopTable_->verticalHeader()->setDefaultSectionSize(28);
        if (auto* header = rightTopTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(5, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(6, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(7, QHeaderView::Stretch);
        }
        for (int i = 0; i < rows; ++i) {
            const auto& day = days[i];
            const bool monthBreak = (i == 0)
                || day.date.month() != days[i - 1].date.month()
                || day.date.year() != days[i - 1].date.year();

            auto* dateItem = makeCell(shortDateLabel(day.date));
            dateItem->setData(Qt::UserRole, day.date);
            dateItem->setToolTip(longDateLabel(day.date));

            auto* dayNumberItem = makeCell(QString::number(day.dayNumber), Qt::AlignCenter);
            auto* degreeItem = makeCell(day.dailyLabel, Qt::AlignCenter);

            auto* netItem = makeCell(signedCountLabel(day.netScore), Qt::AlignCenter);
            QFont scoreFont = netItem->font();
            scoreFont.setBold(true);
            netItem->setFont(scoreFont);
            netItem->setForeground(toneColor(day.netScore));
            if (day.netScore > 0) {
                netItem->setBackground(QColor("#dcefe9"));
            } else if (day.netScore < 0) {
                netItem->setBackground(QColor("#f6dfda"));
            } else {
                netItem->setBackground(QColor("#ece7de"));
            }

            auto* supportItem = makeCell(QString::number(day.supportCount), Qt::AlignCenter);
            supportItem->setForeground(day.supportCount > 0 ? goodColor : neutralColor);
            supportItem->setToolTip(day.support.isEmpty() ? "No supportive hits." : day.support.join("\n"));

            auto* challengeItem = makeCell(QString::number(day.challengeCount), Qt::AlignCenter);
            challengeItem->setForeground(day.challengeCount > 0 ? badColor : neutralColor);
            challengeItem->setToolTip(day.challenge.isEmpty() ? "No challenging hits." : day.challenge.join("\n"));

            auto* neutralItem = makeCell(QString::number(day.neutralCount), Qt::AlignCenter);
            neutralItem->setForeground(day.neutralCount > 0 ? neutralColor : QColor("#a19684"));
            neutralItem->setToolTip(day.neutral.isEmpty() ? "No neutral hits." : day.neutral.join("\n"));

            auto* strongestItem = makeCell(day.strongestPreview.isEmpty() ? "No exact hits" : day.strongestPreview);
            strongestItem->setForeground(day.strongestPreview.isEmpty() ? QColor("#a19684") : toneColor(day.strongestTone));
            QStringList strongestTooltip;
            strongestTooltip << longDateLabel(day.date)
                             << QString("Degree: %1").arg(day.dailyLabel)
                             << QString("Net %1 | +%2 / -%3 / 0 %4")
                                    .arg(signedCountLabel(day.netScore))
                                    .arg(day.supportCount)
                                    .arg(day.challengeCount)
                                    .arg(day.neutralCount);
            if (!day.strongestHit.isEmpty()) {
                strongestTooltip << "" << QString("Strongest: %1").arg(day.strongestHit);
            }
            strongestItem->setToolTip(strongestTooltip.join("\n"));

            QVector<QTableWidgetItem*> rowItems = {
                dateItem, dayNumberItem, degreeItem, netItem,
                supportItem, challengeItem, neutralItem, strongestItem
            };
            if (monthBreak) {
                QFont dateFont = dateItem->font();
                dateFont.setBold(true);
                dateItem->setFont(dateFont);
                for (auto* item : rowItems) {
                    if (item) {
                        item->setBackground(monthBreakColor);
                    }
                }
                netItem->setBackground(day.netScore > 0 ? QColor("#dcefe9")
                    : (day.netScore < 0 ? QColor("#f6dfda") : QColor("#ece7de")));
            }

            for (int col = 0; col < rowItems.size(); ++col) {
                rightTopTable_->setItem(i, col, rowItems[col]);
            }
        }
        if (dayIndex >= 0 && dayIndex < rows) {
            rightTopTable_->selectRow(dayIndex);
        }
    }

    if (rightBottomTable_) {
        const int maxIndex = days.isEmpty() ? 0 : static_cast<int>(days.size() - 1);
        const int safeIndex = std::clamp(dayIndex, 0, maxIndex);
        const DayRow& selected = days.isEmpty() ? DayRow{} : days[safeIndex];
        const QString targetLabel = includeNatal && includeSolar ? "Natal + Solar"
            : (includeNatal ? "Natal" : "Solar Return");
        const QString rangeText = QString("%1 -> %2")
            .arg(startDate.toString("yyyy-MM-dd"))
            .arg(endDate.toString("yyyy-MM-dd"));
        const bool showBodySummary = bodyPreset == SolarTechniqueBodyPreset::Custom;
        const int topN = std::min<int>(solarTechniqueTopSpin_ ? solarTechniqueTopSpin_->value() : 20, days.size());
        const int topMonthN = months.size();
        const int rows = 10 + (showBodySummary ? 1 : 0) + topN + topMonthN;
        setupDetailTable(rightBottomTable_, {"Item", "Value"}, rows);
        if (auto* header = rightBottomTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::Stretch);
        }
        int row = 0;
        rightBottomTable_->setItem(row, 0, makeCell("Selected Day"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString("%1 | Day %2 / %3 | %4")
            .arg(longDateLabel(selected.date))
            .arg(selected.dayNumber > 0 ? selected.dayNumber : 1)
            .arg(totalDays)
            .arg(selected.dailyLabel.isEmpty() ? "-" : selected.dailyLabel)));
        rightBottomTable_->setItem(row, 0, makeCell("Technique Range"));
        rightBottomTable_->setItem(row++, 1, makeCell(rangeText));
        rightBottomTable_->setItem(row, 0, makeCell("Setup"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString("%1 | %2 | %3 | Orb %4°")
            .arg(countingModeLabel)
            .arg(bodyPresetLabel)
            .arg(targetLabel)
            .arg(QString::number(orbValue, 'f', 2))));
        if (showBodySummary) {
            rightBottomTable_->setItem(row, 0, makeCell("Body Categories"));
            rightBottomTable_->setItem(row++, 1, makeCell(bodySummary));
        }
        rightBottomTable_->setItem(row, 0, makeCell("Summary"));
        auto* summaryItem = makeCell(QString("Net %1 | Support %2 | Challenge %3 | Neutral %4")
            .arg(signedCountLabel(selected.netScore))
            .arg(selected.supportCount)
            .arg(selected.challengeCount)
            .arg(selected.neutralCount));
        summaryItem->setForeground(toneColor(selected.netScore));
        rightBottomTable_->setItem(row++, 1, summaryItem);
        rightBottomTable_->setItem(row, 0, makeCell("Strongest Hit"));
        auto* strongestDetailItem = makeCell(
            selected.strongestHit.isEmpty() ? "No exact hits for this day." : selected.strongestHit);
        strongestDetailItem->setForeground(
            selected.strongestHit.isEmpty() ? QColor("#a19684") : toneColor(selected.strongestTone));
        rightBottomTable_->setItem(row++, 1, strongestDetailItem);
        rightBottomTable_->setItem(row, 0, makeCell("Support Hits"));
        auto* supportListItem = makeCell(listTextOrNone(selected.support));
        supportListItem->setForeground(goodColor);
        rightBottomTable_->setItem(row++, 1, supportListItem);
        rightBottomTable_->setItem(row, 0, makeCell("Challenge Hits"));
        auto* challengeListItem = makeCell(listTextOrNone(selected.challenge));
        challengeListItem->setForeground(badColor);
        rightBottomTable_->setItem(row++, 1, challengeListItem);
        rightBottomTable_->setItem(row, 0, makeCell("Neutral Hits"));
        auto* neutralListItem = makeCell(listTextOrNone(selected.neutral));
        neutralListItem->setForeground(neutralColor);
        rightBottomTable_->setItem(row++, 1, neutralListItem);

        const int metricIndex = solarTechniqueRankMetricCombo_ ? solarTechniqueRankMetricCombo_->currentIndex() : 0;
        QString metricLabel = "Net";
        if (metricIndex == 1) {
            metricLabel = "Support";
        } else if (metricIndex == 2) {
            metricLabel = "Challenge";
        }
        QString orderLabel = "High -> Low";
        const bool ascending = solarTechniqueRankOrderCombo_ && solarTechniqueRankOrderCombo_->currentIndex() == 1;
        if (ascending) {
            orderLabel = "Low -> High";
        }
        rightBottomTable_->setItem(row, 0, makeCell("Day Ranking"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString("%1 ranked %2").arg(metricLabel, orderLabel)));

        QVector<int> indices;
        indices.reserve(days.size());
        for (int i = 0; i < days.size(); ++i) {
            indices.push_back(i);
        }
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

        for (int i = 0; i < topN; ++i) {
            const DayRow& day = days[indices[i]];
            QString value = QString("%1: Net %2, Support %3, Challenge %4, Neutral %5")
                .arg(longDateLabel(day.date))
                .arg(signedCountLabel(day.netScore))
                .arg(day.supportCount)
                .arg(day.challengeCount)
                .arg(day.neutralCount);
            if (!day.strongestPreview.isEmpty()) {
                value += QString(". Strongest: %1").arg(day.strongestPreview);
            }
            rightBottomTable_->setItem(row, 0, makeCell(QString("#%1").arg(i + 1)));
            auto* item = makeCell(value);
            item->setForeground(toneColor(day.netScore));
            rightBottomTable_->setItem(row++, 1, item);
        }

        rightBottomTable_->setItem(row, 0, makeCell("Month Ranking"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString("%1 ranked %2").arg(metricLabel, orderLabel)));

        QVector<int> monthIndices;
        monthIndices.reserve(months.size());
        for (int i = 0; i < months.size(); ++i) {
            monthIndices.push_back(i);
        }
        auto monthMetricValue = [&](const MonthRow& month) {
            if (metricIndex == 1) {
                return month.positiveDays;
            }
            if (metricIndex == 2) {
                return month.negativeDays;
            }
            return month.dayBalance;
        };
        std::sort(monthIndices.begin(), monthIndices.end(), [&](int a, int b) {
            const int va = monthMetricValue(months[a]);
            const int vb = monthMetricValue(months[b]);
            if (va != vb) {
                return ascending ? va < vb : va > vb;
            }
            if (metricIndex == 1 && months[a].totalSupport != months[b].totalSupport) {
                return ascending ? months[a].totalSupport < months[b].totalSupport
                                 : months[a].totalSupport > months[b].totalSupport;
            }
            if (metricIndex == 2 && months[a].totalChallenge != months[b].totalChallenge) {
                return ascending ? months[a].totalChallenge < months[b].totalChallenge
                                 : months[a].totalChallenge > months[b].totalChallenge;
            }
            if (months[a].totalNet != months[b].totalNet) {
                return ascending ? months[a].totalNet < months[b].totalNet
                                 : months[a].totalNet > months[b].totalNet;
            }
            return months[a].monthStart < months[b].monthStart;
        });

        for (int i = 0; i < topMonthN; ++i) {
            const MonthRow& month = months[monthIndices[i]];
            QString value = QString("%1: Positive days %2, Negative days %3, Neutral days %4, Net days %5, Total net %6")
                .arg(monthLabel(month.monthStart))
                .arg(month.positiveDays)
                .arg(month.negativeDays)
                .arg(month.neutralDays)
                .arg(signedCountLabel(month.dayBalance))
                .arg(signedCountLabel(month.totalNet));
            if (!month.strongestPreview.isEmpty()) {
                value += QString(". Strongest: %1").arg(month.strongestPreview);
            }
            rightBottomTable_->setItem(row, 0, makeCell(QString("#%1").arg(i + 1)));
            auto* item = makeCell(value);
            item->setForeground(toneColor(month.dayBalance));
            rightBottomTable_->setItem(row++, 1, item);
        }
        rightBottomTable_->resizeRowsToContents();
    }
}

void MainWindow::updateSolarFinderModeAvailability() {
    const int modeIdx = solarFinderModeCombo_ ? solarFinderModeCombo_->currentIndex() : 0;
    const bool single = (modeIdx == 0);
    const bool stellium = (modeIdx == 1);
    const bool ruler = (modeIdx == 2);
    const bool profection = (modeIdx == 3);
    if (solarFinderPlanetCombo_) {
        solarFinderPlanetCombo_->setEnabled(single);
    }
    if (solarFinderPlanet2Combo_) {
        solarFinderPlanet2Combo_->setEnabled(single);
    }
    if (solarFinderStelliumCountSpin_) {
        solarFinderStelliumCountSpin_->setEnabled(stellium);
    }
    if (solarFinderRulerHouseCombo_) {
        solarFinderRulerHouseCombo_->setEnabled(ruler);
    }
    if (solarFinderRulerSchemeCombo_) {
        solarFinderRulerSchemeCombo_->setEnabled(ruler || profection);
    }
    // Conjunction-to-angle only applies to single-planet searches.
    if (solarFinderConjunctionTargetCombo_) {
        solarFinderConjunctionTargetCombo_->setEnabled(single);
    }
    if (solarFinderConjunctionOrbSpin_) {
        solarFinderConjunctionOrbSpin_->setEnabled(single);
    }
}

void MainWindow::handleSolarPlacementFinderRun() {
    if (!solarFinderStartYearSpin_ || !solarFinderEndYearSpin_ || !solarFinderPlanetCombo_
        || !solarFinderHouseCombo_ || !solarFinderHouseSystemCombo_ || !solarFinderConjunctionTargetCombo_) {
        return;
    }
    if (solarFinderStatusLabel_) {
        solarFinderStatusLabel_->setText("Running");
    }

    QString tzLabel;
    QString locationName;
    double lat = 0.0;
    double lon = 0.0;
    QString err;
    if (!resolveSolarReturnContext(&tzLabel, &locationName, &lat, &lon, &err)) {
        if (solarFinderStatusLabel_) {
            solarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage(err);
        refreshSolarPlacementFinderView();
        return;
    }

    double natalSunLon = 0.0;
    if (!findBodyLongitude(currentChart_, "Sun", &natalSunLon)) {
        if (solarFinderStatusLabel_) {
            solarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage("Unable to locate natal Sun longitude.");
        refreshSolarPlacementFinderView();
        return;
    }

    int startYear = std::clamp(solarFinderStartYearSpin_->value(), 1800, 2399);
    int endYear = std::clamp(solarFinderEndYearSpin_->value(), 1800, 2399);
    if (startYear > endYear) {
        std::swap(startYear, endYear);
    }
    {
        const QSignalBlocker startBlock(solarFinderStartYearSpin_);
        const QSignalBlocker endBlock(solarFinderEndYearSpin_);
        solarFinderStartYearSpin_->setValue(startYear);
        solarFinderEndYearSpin_->setValue(endYear);
    }

    const bool stelliumMode = (solarFinderModeCombo_ && solarFinderModeCombo_->currentIndex() == 1);
    const bool rulerMode = (solarFinderModeCombo_ && solarFinderModeCombo_->currentIndex() == 2);
    const bool profectionMode = (solarFinderModeCombo_ && solarFinderModeCombo_->currentIndex() == 3);
    const int stelliumMin = solarFinderStelliumCountSpin_ ? solarFinderStelliumCountSpin_->value() : 3;
    const int rulerOfHouse = solarFinderRulerHouseCombo_ ? solarFinderRulerHouseCombo_->currentData().toInt() : 7;
    const bool rulerModern = (solarFinderRulerSchemeCombo_ && solarFinderRulerSchemeCombo_->currentData().toInt() == 1);

    const QString planetName = solarFinderPlanetCombo_->currentText().trimmed();
    if (!stelliumMode && !rulerMode && !profectionMode && planetName.isEmpty()) {
        if (solarFinderStatusLabel_) {
            solarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage("Select a planet for the finder.");
        refreshSolarPlacementFinderView();
        return;
    }

    // House combo carries the house number in its data role; data == 0 means
    // "Any house" (only meaningful for stellium searches, or single-planet with
    // a conjunction-to-angle filter that constrains the match).
    int targetHouse = solarFinderHouseCombo_->currentData().toInt();
    const bool anyHouse = (targetHouse == 0);
    if (!anyHouse && (targetHouse < 1 || targetHouse > 12)) {
        if (solarFinderStatusLabel_) {
            solarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage("Select a valid house (1-12).");
        refreshSolarPlacementFinderView();
        return;
    }

    int modeValue = solarFinderHouseSystemCombo_->currentData().toInt();
    if (modeValue < static_cast<int>(SolarPlacementFinderHouseMode::WholeSign)
        || modeValue > static_cast<int>(SolarPlacementFinderHouseMode::BothAnd)) {
        modeValue = static_cast<int>(SolarPlacementFinderHouseMode::WholeSign);
    }
    const SolarPlacementFinderHouseMode houseMode = static_cast<SolarPlacementFinderHouseMode>(modeValue);
    const QString conjunctionTarget = solarFinderConjunctionTargetCombo_
        ? solarFinderConjunctionTargetCombo_->currentText().trimmed()
        : QString("None");
    const bool useConjunction = (conjunctionTarget.compare("None", Qt::CaseInsensitive) != 0);
    const double conjunctionOrbLimit = useConjunction
        ? std::max(0.01, (solarFinderConjunctionOrbSpin_ ? solarFinderConjunctionOrbSpin_->value() : 1.0))
        : 0.0;

    if (!stelliumMode && !rulerMode && !profectionMode && anyHouse && !useConjunction) {
        if (solarFinderStatusLabel_) {
            solarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage("Single-planet search with \"Any house\" needs a conjunction-to-angle filter, "
                         "otherwise every year matches. Pick a house or set a conjunction target.");
        refreshSolarPlacementFinderView();
        return;
    }
    if ((rulerMode || profectionMode) && anyHouse) {
        if (solarFinderStatusLabel_) {
            solarFinderStatusLabel_->setText("Idle");
        }
        setStatusMessage("This search needs a specific target House (not \"Any house\").");
        refreshSolarPlacementFinderView();
        return;
    }

    QVector<SolarPlacementFinderResult> matches;
    QStringList warnings;
    const int searchedCount = endYear - startYear + 1;
    int failedCount = 0;
    matches.reserve(searchedCount);

    for (int year = startYear; year <= endYear; ++year) {
        QString yearErr;
        QString yearWarning;
        NatalChart chart;
        NatalInput unusedInput;
        const HouseSystem computeSystem = (houseMode == SolarPlacementFinderHouseMode::WholeSign)
            ? HouseSystem::WholeSign
            : HouseSystem::Placidus;
        // Finder only inspects body positions, houses and angles — skip the
        // expensive Lots/syzygy, fixed stars and aspect grid for each year.
        TropicalComputeOptions finderOptions;
        finderOptions.includeArabicLots = false;
        finderOptions.includeFixedStars = false;
        finderOptions.includeAspectGrid = false;
        if (!computeSolarReturnChartPure(year, tzLabel, natalSunLon, locationName, lat, lon,
                                         computeSystem, &chart, &unusedInput, &yearErr, finderOptions)) {
            ++failedCount;
            warnings.push_back(QString("%1: %2").arg(year).arg(yearErr));
            continue;
        }

        if (stelliumMode) {
            static const QStringList kStelliumBodies = {
                "Sun", "Moon", "Mercury", "Venus", "Mars",
                "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto"
            };
            const bool hasPlacidusCusps = (chart.cusps.size() == 12);
            if (houseMode != SolarPlacementFinderHouseMode::WholeSign && !hasPlacidusCusps) {
                if (houseMode == SolarPlacementFinderHouseMode::Placidus) {
                    ++failedCount;
                    warnings.push_back(QString("%1: Placidus cusps unavailable for this year.").arg(year));
                    continue;
                }
                yearWarning = "Placidus cusps unavailable; matched by Whole Sign only.";
                warnings.push_back(QString("%1: %2").arg(year).arg(yearWarning));
            }

            // Tally planets per house for both supported systems.
            QVector<int> wholeCounts(13, 0);
            QVector<int> placidusCounts(13, 0);
            QVector<QStringList> wholeBodies(13);
            QVector<QStringList> placidusBodies(13);
            for (const QString& bodyName : kStelliumBodies) {
                double lon = 0.0;
                if (!findBodyLongitude(chart, bodyName, &lon)) {
                    continue;
                }
                const int hw = calcHouseForLongitude(lon, {}, chart.angles.asc, HouseSystem::WholeSign);
                if (hw >= 1 && hw <= 12) {
                    ++wholeCounts[hw];
                    wholeBodies[hw].push_back(bodyName);
                }
                if (hasPlacidusCusps) {
                    const int hp = calcHouseForLongitude(lon, chart.cusps, chart.angles.asc, HouseSystem::Placidus);
                    if (hp >= 1 && hp <= 12) {
                        ++placidusCounts[hp];
                        placidusBodies[hp].push_back(bodyName);
                    }
                }
            }

            // Resolve the matched house for each system per houseMode.
            auto bestHouse = [&](const QVector<int>& counts) -> int {
                if (!anyHouse) {
                    return targetHouse;
                }
                int best = 0;
                int bestCount = -1;
                for (int h = 1; h <= 12; ++h) {
                    if (counts[h] > bestCount) {
                        bestCount = counts[h];
                        best = h;
                    }
                }
                return best;
            };

            const bool checkWhole = (houseMode == SolarPlacementFinderHouseMode::WholeSign
                                     || houseMode == SolarPlacementFinderHouseMode::Both
                                     || houseMode == SolarPlacementFinderHouseMode::BothAnd);
            const bool checkPlacidus = hasPlacidusCusps
                                       && (houseMode == SolarPlacementFinderHouseMode::Placidus
                                           || houseMode == SolarPlacementFinderHouseMode::Both
                                           || houseMode == SolarPlacementFinderHouseMode::BothAnd);

            int wholeHouse = 0;
            int wholeCount = 0;
            QString wholeBodyStr;
            bool wholeMatched = false;
            if (checkWhole) {
                wholeHouse = bestHouse(wholeCounts);
                if (wholeHouse >= 1 && wholeHouse <= 12) {
                    wholeCount = wholeCounts[wholeHouse];
                    wholeBodyStr = wholeBodies[wholeHouse].join(", ");
                    wholeMatched = (wholeCount >= stelliumMin);
                }
            }

            int placidusHouse = 0;
            int placidusCount = 0;
            QString placidusBodyStr;
            bool placidusMatched = false;
            if (checkPlacidus) {
                placidusHouse = bestHouse(placidusCounts);
                if (placidusHouse >= 1 && placidusHouse <= 12) {
                    placidusCount = placidusCounts[placidusHouse];
                    placidusBodyStr = placidusBodies[placidusHouse].join(", ");
                    placidusMatched = (placidusCount >= stelliumMin);
                }
            }

            if (!wholeMatched && !placidusMatched) {
                continue;
            }

            SolarPlacementFinderResult result;
            result.year = year;
            result.localDateTime = chart.localDateTime;
            result.isStellium = true;
            result.matchedWhole = wholeMatched;
            result.matchedPlacidus = placidusMatched;
            result.stelliumHouseWhole = wholeHouse;
            result.stelliumHousePlacidus = placidusHouse;
            result.stelliumCountWhole = wholeCount;
            result.stelliumCountPlacidus = placidusCount;
            // Prefer the system that matched (or the one with the bigger cluster).
            if (wholeMatched && (!placidusMatched || wholeCount >= placidusCount)) {
                result.stelliumBodies = wholeBodyStr;
            } else {
                result.stelliumBodies = placidusBodyStr;
            }
            result.warning = yearWarning;
            matches.push_back(result);
            continue;
        }

        if (rulerMode) {
            const int ascSign = signIndex(chart.angles.asc);
            const bool hasPlacidusCusps = (chart.cusps.size() == 12);

            // Whole-sign: the sign on house X is X signs from the rising sign.
            const int signXWhole = (ascSign + (rulerOfHouse - 1)) % 12;
            const QString rulerWhole = houseRulerForSign(signXWhole, rulerModern);
            int rulerHouseWhole = 0;
            bool wholeMatch = false;
            double rlonW = 0.0;
            if (!rulerWhole.isEmpty() && findBodyLongitude(chart, rulerWhole, &rlonW)) {
                rulerHouseWhole = calcHouseForLongitude(rlonW, {}, chart.angles.asc, HouseSystem::WholeSign);
                wholeMatch = (rulerHouseWhole == targetHouse);
            }

            QString rulerPlac;
            int rulerHousePlac = 0;
            bool placMatch = false;
            if (hasPlacidusCusps) {
                const int signXPlac = signIndex(chart.cusps[rulerOfHouse - 1].longitude);
                rulerPlac = houseRulerForSign(signXPlac, rulerModern);
                double rlonP = 0.0;
                if (!rulerPlac.isEmpty() && findBodyLongitude(chart, rulerPlac, &rlonP)) {
                    rulerHousePlac = calcHouseForLongitude(rlonP, chart.cusps, chart.angles.asc, HouseSystem::Placidus);
                    placMatch = (rulerHousePlac == targetHouse);
                }
            } else if (houseMode == SolarPlacementFinderHouseMode::Placidus) {
                ++failedCount;
                warnings.push_back(QString("%1: Placidus cusps unavailable for this year.").arg(year));
                continue;
            } else if (houseMode == SolarPlacementFinderHouseMode::BothAnd) {
                ++failedCount;
                warnings.push_back(QString("%1: Placidus cusps unavailable; AND match not possible.").arg(year));
                continue;
            } else if (houseMode == SolarPlacementFinderHouseMode::Both) {
                yearWarning = "Placidus cusps unavailable; matched by Whole Sign only.";
                warnings.push_back(QString("%1: %2").arg(year).arg(yearWarning));
            }

            bool matchedRuler = false;
            switch (houseMode) {
                case SolarPlacementFinderHouseMode::WholeSign:
                    matchedRuler = wholeMatch;
                    break;
                case SolarPlacementFinderHouseMode::Placidus:
                    matchedRuler = placMatch;
                    break;
                case SolarPlacementFinderHouseMode::Both:
                    matchedRuler = (wholeMatch || placMatch);
                    break;
                case SolarPlacementFinderHouseMode::BothAnd:
                    matchedRuler = (wholeMatch && placMatch);
                    break;
            }
            if (!matchedRuler) {
                continue;
            }

            SolarPlacementFinderResult result;
            result.year = year;
            result.localDateTime = chart.localDateTime;
            result.isHouseRuler = true;
            result.rulerOfHouse = rulerOfHouse;
            result.rulerNameWhole = rulerWhole;
            result.rulerNamePlacidus = rulerPlac;
            result.bodyName = !rulerWhole.isEmpty() ? rulerWhole : rulerPlac;
            result.houseWhole = rulerHouseWhole;
            result.housePlacidus = rulerHousePlac;
            result.matchedWhole = wholeMatch;
            result.matchedPlacidus = placMatch;
            result.warning = yearWarning;
            matches.push_back(result);
            continue;
        }

        if (profectionMode) {
            const int birthYear = currentChart_.localDateTime.isValid()
                ? currentChart_.localDateTime.date().year()
                : year;
            const int age = std::max(0, year - birthYear);
            const int ageMod = ((age % 12) + 12) % 12;
            const int profectedHouse = ageMod + 1;
            const int natalAscSign = signIndex(currentChart_.angles.asc);
            const int profectedSignIdx = (natalAscSign + ageMod) % 12;
            const QString lord = houseRulerForSign(profectedSignIdx, rulerModern);

            double lordLon = 0.0;
            if (lord.isEmpty() || !findBodyLongitude(chart, lord, &lordLon)) {
                ++failedCount;
                warnings.push_back(QString("%1: Lord of the year (%2) unavailable.").arg(year).arg(lord));
                continue;
            }
            const int lordHouseWhole = calcHouseForLongitude(lordLon, {}, chart.angles.asc, HouseSystem::WholeSign);
            int lordHousePlac = 0;
            const bool hasPlac = (chart.cusps.size() == 12);
            if (hasPlac) {
                lordHousePlac = calcHouseForLongitude(lordLon, chart.cusps, chart.angles.asc, HouseSystem::Placidus);
            } else if (houseMode == SolarPlacementFinderHouseMode::Placidus) {
                ++failedCount;
                warnings.push_back(QString("%1: Placidus cusps unavailable for this year.").arg(year));
                continue;
            } else if (houseMode == SolarPlacementFinderHouseMode::BothAnd) {
                ++failedCount;
                warnings.push_back(QString("%1: Placidus cusps unavailable; AND match not possible.").arg(year));
                continue;
            } else if (houseMode == SolarPlacementFinderHouseMode::Both) {
                yearWarning = "Placidus cusps unavailable; matched by Whole Sign only.";
                warnings.push_back(QString("%1: %2").arg(year).arg(yearWarning));
            }

            const bool wholeMatch = (lordHouseWhole == targetHouse);
            const bool placMatch = (lordHousePlac == targetHouse);
            bool matchedLord = false;
            switch (houseMode) {
                case SolarPlacementFinderHouseMode::WholeSign:
                    matchedLord = wholeMatch;
                    break;
                case SolarPlacementFinderHouseMode::Placidus:
                    matchedLord = placMatch;
                    break;
                case SolarPlacementFinderHouseMode::Both:
                    matchedLord = (wholeMatch || placMatch);
                    break;
                case SolarPlacementFinderHouseMode::BothAnd:
                    matchedLord = (wholeMatch && placMatch);
                    break;
            }
            if (!matchedLord) {
                continue;
            }

            SolarPlacementFinderResult result;
            result.year = year;
            result.localDateTime = chart.localDateTime;
            result.isHouseRuler = true;
            result.isProfectionLord = true;
            result.rulerOfHouse = profectedHouse;
            result.rulerNameWhole = lord;
            result.rulerNamePlacidus = lord;
            result.bodyName = lord;
            result.houseWhole = lordHouseWhole;
            result.housePlacidus = lordHousePlac;
            result.matchedWhole = wholeMatch;
            result.matchedPlacidus = placMatch;
            result.warning = yearWarning;
            matches.push_back(result);
            continue;
        }

        // Single-planet (mode 0), optionally OR'd with a second planet.
        QStringList finderPlanets;
        finderPlanets << planetName;
        {
            const QString planet2 = solarFinderPlanet2Combo_ ? solarFinderPlanet2Combo_->currentText().trimmed() : QString();
            if (!planet2.isEmpty() && planet2.compare("None", Qt::CaseInsensitive) != 0 && planet2 != planetName) {
                finderPlanets << planet2;
            }
        }
        const bool hasPlacidusCusps = (chart.cusps.size() == 12);
        if (!hasPlacidusCusps && houseMode == SolarPlacementFinderHouseMode::Placidus) {
            ++failedCount;
            warnings.push_back(QString("%1: Placidus cusps unavailable for this year.").arg(year));
            continue;
        }
        if (!hasPlacidusCusps && houseMode == SolarPlacementFinderHouseMode::BothAnd) {
            ++failedCount;
            warnings.push_back(QString("%1: Placidus cusps unavailable; AND match not possible.").arg(year));
            continue;
        }
        if (!hasPlacidusCusps && houseMode == SolarPlacementFinderHouseMode::Both) {
            yearWarning = "Placidus cusps unavailable; matched by Whole Sign only.";
            warnings.push_back(QString("%1: %2").arg(year).arg(yearWarning));
        }

        bool yearMatched = false;
        for (const QString& pName : finderPlanets) {
            if (yearMatched) {
                break;
            }
            double bodyLon = 0.0;
            if (!findBodyLongitude(chart, pName, &bodyLon)) {
                continue;
            }
            const int houseWhole = calcHouseForLongitude(bodyLon, {}, chart.angles.asc, HouseSystem::WholeSign);
            int housePlacidus = 0;
            if (hasPlacidusCusps) {
                housePlacidus = calcHouseForLongitude(bodyLon, chart.cusps, chart.angles.asc, HouseSystem::Placidus);
            }

        const bool matchedWhole = anyHouse ? (houseWhole >= 1 && houseWhole <= 12) : (houseWhole == targetHouse);
        const bool matchedPlacidus = anyHouse ? (housePlacidus >= 1 && housePlacidus <= 12) : (housePlacidus == targetHouse);
        bool matchedHouse = false;
        switch (houseMode) {
            case SolarPlacementFinderHouseMode::WholeSign:
                matchedHouse = matchedWhole;
                break;
            case SolarPlacementFinderHouseMode::Placidus:
                matchedHouse = matchedPlacidus;
                break;
            case SolarPlacementFinderHouseMode::Both:
                matchedHouse = (matchedWhole || matchedPlacidus);
                break;
            case SolarPlacementFinderHouseMode::BothAnd:
                matchedHouse = (matchedWhole && matchedPlacidus);
                break;
        }
        if (!matchedHouse) {
            continue;
        }

        bool matchedConjunction = !useConjunction;
        QString matchedAngleName;
        double matchedConjunctionOrb = 0.0;
        if (useConjunction) {
            auto diffToAngle = [&](const QString& angleName, double angleLon, QString* outName, double* outOrb) {
                const double diff = angularDiffAbs(bodyLon, angleLon);
                if (outName) {
                    *outName = angleName;
                }
                if (outOrb) {
                    *outOrb = diff;
                }
            };

            if (conjunctionTarget.compare("Any Angle", Qt::CaseInsensitive) == 0) {
                struct AngleCandidate {
                    QString name;
                    double lon = 0.0;
                };
                const QVector<AngleCandidate> angles = {
                    {"Ascendant", chart.angles.asc},
                    {"Descendant", chart.angles.desc},
                    {"Midheaven", chart.angles.mc},
                    {"IC", chart.angles.ic},
                };
                double bestOrb = 1e9;
                QString bestName;
                for (const auto& angle : angles) {
                    QString name;
                    double orb = 0.0;
                    diffToAngle(angle.name, angle.lon, &name, &orb);
                    if (orb < bestOrb) {
                        bestOrb = orb;
                        bestName = name;
                    }
                }
                matchedAngleName = bestName;
                matchedConjunctionOrb = bestOrb;
                matchedConjunction = (bestOrb <= conjunctionOrbLimit);
            } else {
                QString resolvedAngleName;
                double angleLon = 0.0;
                if (conjunctionTarget.compare("Ascendant", Qt::CaseInsensitive) == 0) {
                    resolvedAngleName = "Ascendant";
                    angleLon = chart.angles.asc;
                } else if (conjunctionTarget.compare("Descendant", Qt::CaseInsensitive) == 0) {
                    resolvedAngleName = "Descendant";
                    angleLon = chart.angles.desc;
                } else if (conjunctionTarget.compare("Midheaven", Qt::CaseInsensitive) == 0
                           || conjunctionTarget.compare("MC", Qt::CaseInsensitive) == 0) {
                    resolvedAngleName = "Midheaven";
                    angleLon = chart.angles.mc;
                } else if (conjunctionTarget.compare("IC", Qt::CaseInsensitive) == 0) {
                    resolvedAngleName = "IC";
                    angleLon = chart.angles.ic;
                } else {
                    ++failedCount;
                    warnings.push_back(QString("%1: Unknown conjunction target '%2'.").arg(year).arg(conjunctionTarget));
                    continue;
                }
                diffToAngle(resolvedAngleName, angleLon, &matchedAngleName, &matchedConjunctionOrb);
                matchedConjunction = (matchedConjunctionOrb <= conjunctionOrbLimit);
            }
        }
        if (!matchedConjunction) {
            continue;
        }

        SolarPlacementFinderResult result;
        result.year = year;
        result.localDateTime = chart.localDateTime;
        result.bodyName = pName;
        result.houseWhole = houseWhole;
        result.housePlacidus = housePlacidus;
        result.matchedWhole = matchedWhole;
        result.matchedPlacidus = matchedPlacidus;
        result.matchedConjunction = matchedConjunction;
        result.matchedAngleName = matchedAngleName;
        result.conjunctionOrb = matchedConjunctionOrb;
        result.warning = yearWarning;
        matches.push_back(result);
        yearMatched = true;
        }
    }

    solarPlacementFinderResults_ = matches;
    solarPlacementFinderWarnings_ = warnings;
    solarPlacementFinderRan_ = true;
    solarPlacementFinderStale_ = false;
    solarPlacementFinderSelectedIndex_ = solarPlacementFinderResults_.isEmpty() ? -1 : 0;
    solarPlacementFinderLastSearchedCount_ = searchedCount;
    solarPlacementFinderLastFailedCount_ = failedCount;
    solarPlacementFinderLastStartYear_ = startYear;
    solarPlacementFinderLastEndYear_ = endYear;
    solarPlacementFinderLastPlanet_ = planetName;
    solarPlacementFinderLastHouse_ = targetHouse;
    solarPlacementFinderLastHouseMode_ = houseMode;
    solarPlacementFinderLastConjunctionTarget_ = conjunctionTarget;
    solarPlacementFinderLastConjunctionOrb_ = conjunctionOrbLimit;
    solarPlacementFinderLastStelliumMode_ = stelliumMode;
    solarPlacementFinderLastStelliumMin_ = stelliumMin;
    solarPlacementFinderLastAnyHouse_ = anyHouse;
    solarPlacementFinderLastRulerMode_ = rulerMode;
    solarPlacementFinderLastRulerOfHouse_ = rulerOfHouse;
    solarPlacementFinderLastRulerModern_ = rulerModern;
    solarPlacementFinderLastProfectionMode_ = profectionMode;

    if (solarFinderStatusLabel_) {
        if (solarPlacementFinderResults_.isEmpty()) {
            solarFinderStatusLabel_->setText("No matches");
        } else if (!solarPlacementFinderWarnings_.isEmpty()) {
            solarFinderStatusLabel_->setText("Done with warnings");
        } else {
            solarFinderStatusLabel_->setText("Done");
        }
    }

    setStatusMessage(QString("SR finder scanned %1 years, matched %2, failed %3.")
                         .arg(searchedCount)
                         .arg(solarPlacementFinderResults_.size())
                         .arg(failedCount));
    refreshSolarPlacementFinderView();
}

void MainWindow::refreshSolarPlacementFinderView() {
    if (!isSolarPlacementFinderTabActive()) {
        return;
    }
    updateSolarTechniqueDockTitles();
    showSolarPlacementFinderResults();
}

void MainWindow::showSolarPlacementFinderResults() {
    if (!rightTopTable_ || !rightBottomTable_) {
        return;
    }
    if (!hasCurrentChart_) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Load a natal chart to use the Solar Return placement finder."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Finder details appear after running a search."));
        return;
    }
    if (!solarPlacementFinderRan_) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("Set range/filter and click Find Matching Years."));
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Search results and run summary will appear here."));
        return;
    }

    if (solarPlacementFinderResults_.isEmpty()) {
        setupTable(rightTopTable_, {"Info"}, 1);
        rightTopTable_->setItem(0, 0, makeCell("No matching years found for the current filter."));
        showSolarPlacementFinderDetails(-1);
        return;
    }

    auto matchedByLabel = [](const SolarPlacementFinderResult& result) {
        if (result.matchedWhole && result.matchedPlacidus) {
            return QString("Whole + Placidus");
        }
        if (result.matchedWhole) {
            return QString("Whole");
        }
        if (result.matchedPlacidus) {
            return QString("Placidus");
        }
        return QString("-");
    };
    auto conjunctionLabel = [this](const SolarPlacementFinderResult& result) {
        const bool conjunctionFiltered =
            (solarPlacementFinderLastConjunctionTarget_.trimmed().compare("None", Qt::CaseInsensitive) != 0);
        if (!conjunctionFiltered) {
            return QString("-");
        }
        const QString angleName = result.matchedAngleName.isEmpty()
            ? solarPlacementFinderLastConjunctionTarget_
            : result.matchedAngleName;
        return QString("%1 (%2 deg)").arg(angleName, QString::number(result.conjunctionOrb, 'f', 2));
    };

    if (solarPlacementFinderLastStelliumMode_) {
        auto houseCell = [](int house, int count) {
            if (house < 1 || house > 12 || count <= 0) {
                return QString("-");
            }
            return QString("H%1 (%2)").arg(house).arg(count);
        };
        setupTable(rightTopTable_, {"Year", "SR Local Date/Time", "Stellium (Whole)", "Stellium (Placidus)",
                                    "Match", "Bodies"},
                   solarPlacementFinderResults_.size());
        if (auto* header = rightTopTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(5, QHeaderView::Stretch);
        }
        for (int i = 0; i < solarPlacementFinderResults_.size(); ++i) {
            const auto& result = solarPlacementFinderResults_[i];
            auto* yearItem = makeCell(QString::number(result.year), Qt::AlignCenter);
            yearItem->setData(Qt::UserRole, result.year);
            rightTopTable_->setItem(i, 0, yearItem);
            rightTopTable_->setItem(i, 1, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm:ss")));
            rightTopTable_->setItem(i, 2, makeCell(houseCell(result.stelliumHouseWhole, result.stelliumCountWhole), Qt::AlignCenter));
            rightTopTable_->setItem(i, 3, makeCell(houseCell(result.stelliumHousePlacidus, result.stelliumCountPlacidus), Qt::AlignCenter));
            rightTopTable_->setItem(i, 4, makeCell(matchedByLabel(result)));
            auto* bodiesItem = makeCell(result.stelliumBodies.isEmpty() ? "-" : result.stelliumBodies);
            if (!result.warning.isEmpty()) {
                bodiesItem->setToolTip(result.warning);
            }
            rightTopTable_->setItem(i, 5, bodiesItem);
        }
        const int maxIndex = std::max(0, static_cast<int>(solarPlacementFinderResults_.size()) - 1);
        solarPlacementFinderSelectedIndex_ = std::clamp(solarPlacementFinderSelectedIndex_, 0, maxIndex);
        rightTopTable_->selectRow(solarPlacementFinderSelectedIndex_);
        showSolarPlacementFinderDetails(solarPlacementFinderSelectedIndex_);
        return;
    }

    if (solarPlacementFinderLastRulerMode_ || solarPlacementFinderLastProfectionMode_) {
        auto houseLabel = [](int house) {
            return (house >= 1 && house <= 12) ? QString("H%1").arg(house) : QString("-");
        };
        const QString rulerHeader = solarPlacementFinderLastProfectionMode_ ? QString("Lord") : QString("Ruler");
        setupTable(rightTopTable_, {"Year", "SR Local Date/Time", rulerHeader, "In House (Whole)",
                                    "In House (Placidus)", "Match"},
                   solarPlacementFinderResults_.size());
        if (auto* header = rightTopTable_->horizontalHeader()) {
            header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
            header->setSectionResizeMode(5, QHeaderView::Stretch);
        }
        for (int i = 0; i < solarPlacementFinderResults_.size(); ++i) {
            const auto& result = solarPlacementFinderResults_[i];
            auto* yearItem = makeCell(QString::number(result.year), Qt::AlignCenter);
            yearItem->setData(Qt::UserRole, result.year);
            rightTopTable_->setItem(i, 0, yearItem);
            rightTopTable_->setItem(i, 1, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm:ss")));
            QString rulerCell = result.rulerNameWhole;
            if (!result.rulerNamePlacidus.isEmpty() && result.rulerNamePlacidus != result.rulerNameWhole) {
                rulerCell = QString("%1 / %2").arg(result.rulerNameWhole.isEmpty() ? "-" : result.rulerNameWhole,
                                                   result.rulerNamePlacidus);
            }
            rightTopTable_->setItem(i, 2, makeCell(rulerCell.isEmpty() ? result.bodyName : rulerCell));
            rightTopTable_->setItem(i, 3, makeCell(houseLabel(result.houseWhole), Qt::AlignCenter));
            rightTopTable_->setItem(i, 4, makeCell(houseLabel(result.housePlacidus), Qt::AlignCenter));
            auto* matchItem = makeCell(matchedByLabel(result));
            if (!result.warning.isEmpty()) {
                matchItem->setToolTip(result.warning);
            }
            rightTopTable_->setItem(i, 5, matchItem);
        }
        const int maxIndex = std::max(0, static_cast<int>(solarPlacementFinderResults_.size()) - 1);
        solarPlacementFinderSelectedIndex_ = std::clamp(solarPlacementFinderSelectedIndex_, 0, maxIndex);
        rightTopTable_->selectRow(solarPlacementFinderSelectedIndex_);
        showSolarPlacementFinderDetails(solarPlacementFinderSelectedIndex_);
        return;
    }

    setupTable(rightTopTable_, {"Year", "SR Local Date/Time", "Planet", "House (Whole)", "House (Placidus)",
                                "House Match", "Conjunction"},
               solarPlacementFinderResults_.size());
    if (auto* header = rightTopTable_->horizontalHeader()) {
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(6, QHeaderView::Stretch);
    }
    for (int i = 0; i < solarPlacementFinderResults_.size(); ++i) {
        const auto& result = solarPlacementFinderResults_[i];
        auto* yearItem = makeCell(QString::number(result.year), Qt::AlignCenter);
        yearItem->setData(Qt::UserRole, result.year);
        rightTopTable_->setItem(i, 0, yearItem);
        rightTopTable_->setItem(i, 1, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm:ss")));
        rightTopTable_->setItem(i, 2, makeCell(result.bodyName));
        rightTopTable_->setItem(i, 3, makeCell(result.houseWhole > 0 ? QString::number(result.houseWhole) : "-", Qt::AlignCenter));
        rightTopTable_->setItem(i, 4, makeCell(result.housePlacidus > 0 ? QString::number(result.housePlacidus) : "-", Qt::AlignCenter));
        auto* matchedByItem = makeCell(matchedByLabel(result));
        if (!result.warning.isEmpty()) {
            matchedByItem->setToolTip(result.warning);
        }
        rightTopTable_->setItem(i, 5, matchedByItem);
        auto* conjunctionItem = makeCell(conjunctionLabel(result));
        if (!result.warning.isEmpty()) {
            conjunctionItem->setToolTip(result.warning);
        }
        rightTopTable_->setItem(i, 6, conjunctionItem);
    }

    const int maxIndex = std::max(0, static_cast<int>(solarPlacementFinderResults_.size()) - 1);
    solarPlacementFinderSelectedIndex_ = std::clamp(solarPlacementFinderSelectedIndex_, 0, maxIndex);
    rightTopTable_->selectRow(solarPlacementFinderSelectedIndex_);
    showSolarPlacementFinderDetails(solarPlacementFinderSelectedIndex_);
}

void MainWindow::showSolarPlacementFinderDetails(int index) {
    if (!rightBottomTable_) {
        return;
    }
    if (!solarPlacementFinderRan_) {
        setupTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, makeCell("Run a finder search to view details."));
        return;
    }

    auto houseModeLabel = [](SolarPlacementFinderHouseMode mode) {
        switch (mode) {
            case SolarPlacementFinderHouseMode::Placidus:
                return QString("Placidus");
            case SolarPlacementFinderHouseMode::Both:
                return QString("Both (OR)");
            case SolarPlacementFinderHouseMode::BothAnd:
                return QString("Both (AND)");
            case SolarPlacementFinderHouseMode::WholeSign:
            default:
                return QString("Whole Sign");
        }
    };
    auto conjunctionFilterLabel = [this]() {
        const QString target = solarPlacementFinderLastConjunctionTarget_.trimmed();
        if (target.isEmpty() || target.compare("None", Qt::CaseInsensitive) == 0) {
            return QString("None");
        }
        return QString("%1 (<= %2 deg)")
            .arg(target, QString::number(solarPlacementFinderLastConjunctionOrb_, 'f', 2));
    };
    const bool conjunctionFiltered =
        (solarPlacementFinderLastConjunctionTarget_.trimmed().compare("None", Qt::CaseInsensitive) != 0);

    QString warningsSummary = "-";
    if (!solarPlacementFinderWarnings_.isEmpty()) {
        const int maxItems = 3;
        if (solarPlacementFinderWarnings_.size() <= maxItems) {
            warningsSummary = solarPlacementFinderWarnings_.join(" | ");
        } else {
            warningsSummary = QString("%1 (+%2 more)")
                .arg(solarPlacementFinderWarnings_.mid(0, maxItems).join(" | "))
                .arg(solarPlacementFinderWarnings_.size() - maxItems);
        }
    }

    const QString rangeLabel = QString("%1 -> %2")
        .arg(solarPlacementFinderLastStartYear_)
        .arg(solarPlacementFinderLastEndYear_);
    const QString stateLabel = solarPlacementFinderStale_
        ? "Stale (filters/inputs changed)"
        : "Current";

    const QString houseTargetLabel = solarPlacementFinderLastAnyHouse_
        ? QString("Any house")
        : QString("House %1").arg(solarPlacementFinderLastHouse_);
    QString criteriaLabel;
    if (solarPlacementFinderLastProfectionMode_) {
        criteriaLabel = QString("Lord of the Year (%1) in House %2")
                            .arg(solarPlacementFinderLastRulerModern_ ? "Modern" : "Traditional")
                            .arg(solarPlacementFinderLastHouse_);
    } else if (solarPlacementFinderLastRulerMode_) {
        criteriaLabel = QString("Ruler of House %1 (%2) in House %3")
                            .arg(solarPlacementFinderLastRulerOfHouse_)
                            .arg(solarPlacementFinderLastRulerModern_ ? "Modern" : "Traditional")
                            .arg(solarPlacementFinderLastHouse_);
    } else if (solarPlacementFinderLastStelliumMode_) {
        criteriaLabel = QString("Stellium: >= %1 planets in %2")
                            .arg(solarPlacementFinderLastStelliumMin_)
                            .arg(solarPlacementFinderLastAnyHouse_ ? QString("a single house")
                                                                   : QString("House %1").arg(solarPlacementFinderLastHouse_));
    } else {
        criteriaLabel = QString("%1 in %2").arg(solarPlacementFinderLastPlanet_, houseTargetLabel);
    }
    const QString criteriaKeyLabel = solarPlacementFinderLastProfectionMode_
        ? QString("Profection Criteria")
        : (solarPlacementFinderLastRulerMode_
               ? QString("House-Ruler Criteria")
               : (solarPlacementFinderLastStelliumMode_ ? QString("Stellium Criteria") : QString("Planet / House")));

    if (index < 0 || index >= solarPlacementFinderResults_.size()) {
        setupTable(rightBottomTable_, {"Item", "Value"}, 9);
        int row = 0;
        rightBottomTable_->setItem(row, 0, makeCell("Run Range"));
        rightBottomTable_->setItem(row++, 1, makeCell(rangeLabel));
        rightBottomTable_->setItem(row, 0, makeCell(criteriaKeyLabel));
        rightBottomTable_->setItem(row++, 1, makeCell(criteriaLabel));
        rightBottomTable_->setItem(row, 0, makeCell("House Mode"));
        rightBottomTable_->setItem(row++, 1, makeCell(houseModeLabel(solarPlacementFinderLastHouseMode_)));
        rightBottomTable_->setItem(row, 0, makeCell("Conjunction Filter"));
        rightBottomTable_->setItem(row++, 1, makeCell(conjunctionFilterLabel()));
        rightBottomTable_->setItem(row, 0, makeCell("Searched Years"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(solarPlacementFinderLastSearchedCount_)));
        rightBottomTable_->setItem(row, 0, makeCell("Matched Years"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(solarPlacementFinderResults_.size())));
        rightBottomTable_->setItem(row, 0, makeCell("Failed Years"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(solarPlacementFinderLastFailedCount_)));
        rightBottomTable_->setItem(row, 0, makeCell("Warnings"));
        auto* warningItem = makeCell(warningsSummary);
        warningItem->setToolTip(solarPlacementFinderWarnings_.join("\n"));
        rightBottomTable_->setItem(row++, 1, warningItem);
        rightBottomTable_->setItem(row, 0, makeCell("Result State"));
        rightBottomTable_->setItem(row++, 1, makeCell(stateLabel));
        rightBottomTable_->setWordWrap(true);
        rightBottomTable_->resizeRowsToContents();
        return;
    }

    const auto& result = solarPlacementFinderResults_[index];

    if (result.isStellium) {
        auto stelliumHouseLabel = [](int house, int count) {
            if (house < 1 || house > 12 || count <= 0) {
                return QString("-");
            }
            return QString("House %1 (%2 planets)").arg(house).arg(count);
        };
        QString matchedBy = "-";
        if (result.matchedWhole && result.matchedPlacidus) {
            matchedBy = "Whole + Placidus";
        } else if (result.matchedWhole) {
            matchedBy = "Whole";
        } else if (result.matchedPlacidus) {
            matchedBy = "Placidus";
        }
        setupTable(rightBottomTable_, {"Item", "Value"}, 13);
        int row = 0;
        rightBottomTable_->setItem(row, 0, makeCell("Year"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(result.year)));
        rightBottomTable_->setItem(row, 0, makeCell("SR Local Date/Time"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm:ss")));
        rightBottomTable_->setItem(row, 0, makeCell("Criteria"));
        rightBottomTable_->setItem(row++, 1, makeCell(criteriaLabel));
        rightBottomTable_->setItem(row, 0, makeCell("House Mode"));
        rightBottomTable_->setItem(row++, 1, makeCell(houseModeLabel(solarPlacementFinderLastHouseMode_)));
        rightBottomTable_->setItem(row, 0, makeCell("Stellium (Whole)"));
        rightBottomTable_->setItem(row++, 1, makeCell(stelliumHouseLabel(result.stelliumHouseWhole, result.stelliumCountWhole)));
        rightBottomTable_->setItem(row, 0, makeCell("Stellium (Placidus)"));
        rightBottomTable_->setItem(row++, 1, makeCell(stelliumHouseLabel(result.stelliumHousePlacidus, result.stelliumCountPlacidus)));
        rightBottomTable_->setItem(row, 0, makeCell("Matched By"));
        rightBottomTable_->setItem(row++, 1, makeCell(matchedBy));
        rightBottomTable_->setItem(row, 0, makeCell("Bodies"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.stelliumBodies.isEmpty() ? "-" : result.stelliumBodies));
        rightBottomTable_->setItem(row, 0, makeCell("Run Range"));
        rightBottomTable_->setItem(row++, 1, makeCell(rangeLabel));
        rightBottomTable_->setItem(row, 0, makeCell("Matched Years"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(solarPlacementFinderResults_.size())));
        rightBottomTable_->setItem(row, 0, makeCell("Result State"));
        rightBottomTable_->setItem(row++, 1, makeCell(stateLabel));
        rightBottomTable_->setItem(row, 0, makeCell("Row Notes"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.warning.isEmpty() ? "-" : result.warning));
        rightBottomTable_->setWordWrap(true);
        rightBottomTable_->resizeRowsToContents();
        return;
    }

    if (result.isHouseRuler) {
        auto houseLabel = [](int house) {
            return (house >= 1 && house <= 12) ? QString("House %1").arg(house) : QString("-");
        };
        QString matchedBy = "-";
        if (result.matchedWhole && result.matchedPlacidus) {
            matchedBy = "Whole + Placidus";
        } else if (result.matchedWhole) {
            matchedBy = "Whole";
        } else if (result.matchedPlacidus) {
            matchedBy = "Placidus";
        }
        setupTable(rightBottomTable_, {"Item", "Value"}, 13);
        int row = 0;
        rightBottomTable_->setItem(row, 0, makeCell("Year"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(result.year)));
        rightBottomTable_->setItem(row, 0, makeCell("SR Local Date/Time"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm:ss")));
        rightBottomTable_->setItem(row, 0, makeCell("Criteria"));
        rightBottomTable_->setItem(row++, 1, makeCell(criteriaLabel));
        rightBottomTable_->setItem(row, 0, makeCell("House Mode"));
        rightBottomTable_->setItem(row++, 1, makeCell(houseModeLabel(solarPlacementFinderLastHouseMode_)));
        rightBottomTable_->setItem(row, 0, makeCell("Ruler (Whole)"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.rulerNameWhole.isEmpty() ? "-" : result.rulerNameWhole));
        rightBottomTable_->setItem(row, 0, makeCell("Ruler (Placidus)"));
        rightBottomTable_->setItem(row++, 1, makeCell(result.rulerNamePlacidus.isEmpty() ? "-" : result.rulerNamePlacidus));
        rightBottomTable_->setItem(row, 0, makeCell("In House (Whole)"));
        rightBottomTable_->setItem(row++, 1, makeCell(houseLabel(result.houseWhole)));
        rightBottomTable_->setItem(row, 0, makeCell("In House (Placidus)"));
        rightBottomTable_->setItem(row++, 1, makeCell(houseLabel(result.housePlacidus)));
        rightBottomTable_->setItem(row, 0, makeCell("Matched By"));
        rightBottomTable_->setItem(row++, 1, makeCell(matchedBy));
        rightBottomTable_->setItem(row, 0, makeCell("Run Range"));
        rightBottomTable_->setItem(row++, 1, makeCell(rangeLabel));
        rightBottomTable_->setItem(row, 0, makeCell("Matched Years"));
        rightBottomTable_->setItem(row++, 1, makeCell(QString::number(solarPlacementFinderResults_.size())));
        rightBottomTable_->setItem(row, 0, makeCell("Result State"));
        rightBottomTable_->setItem(row++, 1, makeCell(stateLabel));
        rightBottomTable_->setWordWrap(true);
        rightBottomTable_->resizeRowsToContents();
        return;
    }

    QString matchedBy = "-";
    if (result.matchedWhole && result.matchedPlacidus) {
        matchedBy = "Whole + Placidus";
    } else if (result.matchedWhole) {
        matchedBy = "Whole";
    } else if (result.matchedPlacidus) {
        matchedBy = "Placidus";
    }
    QString conjunctionMatch = "Not filtered";
    if (conjunctionFiltered) {
        const QString angleName = result.matchedAngleName.isEmpty()
            ? solarPlacementFinderLastConjunctionTarget_
            : result.matchedAngleName;
        conjunctionMatch = QString("%1 (%2 deg)")
            .arg(angleName, QString::number(result.conjunctionOrb, 'f', 2));
    }

    setupTable(rightBottomTable_, {"Item", "Value"}, 16);
    int row = 0;
    rightBottomTable_->setItem(row, 0, makeCell("Year"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString::number(result.year)));
    rightBottomTable_->setItem(row, 0, makeCell("SR Local Date/Time"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.localDateTime.toString("yyyy-MM-dd HH:mm:ss")));
    rightBottomTable_->setItem(row, 0, makeCell("Planet / Target House"));
    rightBottomTable_->setItem(row++, 1, makeCell(criteriaLabel));
    rightBottomTable_->setItem(row, 0, makeCell("House Mode"));
    rightBottomTable_->setItem(row++, 1, makeCell(houseModeLabel(solarPlacementFinderLastHouseMode_)));
    rightBottomTable_->setItem(row, 0, makeCell("House (Whole)"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.houseWhole > 0 ? QString::number(result.houseWhole) : "-"));
    rightBottomTable_->setItem(row, 0, makeCell("House (Placidus)"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.housePlacidus > 0 ? QString::number(result.housePlacidus) : "-"));
    rightBottomTable_->setItem(row, 0, makeCell("Matched By"));
    rightBottomTable_->setItem(row++, 1, makeCell(matchedBy));
    rightBottomTable_->setItem(row, 0, makeCell("Conjunction Filter"));
    rightBottomTable_->setItem(row++, 1, makeCell(conjunctionFilterLabel()));
    rightBottomTable_->setItem(row, 0, makeCell("Conjunction Match"));
    rightBottomTable_->setItem(row++, 1, makeCell(conjunctionMatch));
    rightBottomTable_->setItem(row, 0, makeCell("Run Range"));
    rightBottomTable_->setItem(row++, 1, makeCell(rangeLabel));
    rightBottomTable_->setItem(row, 0, makeCell("Searched Years"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString::number(solarPlacementFinderLastSearchedCount_)));
    rightBottomTable_->setItem(row, 0, makeCell("Matched Years"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString::number(solarPlacementFinderResults_.size())));
    rightBottomTable_->setItem(row, 0, makeCell("Failed Years"));
    rightBottomTable_->setItem(row++, 1, makeCell(QString::number(solarPlacementFinderLastFailedCount_)));
    rightBottomTable_->setItem(row, 0, makeCell("Warnings"));
    auto* warningItem = makeCell(warningsSummary);
    warningItem->setToolTip(solarPlacementFinderWarnings_.join("\n"));
    rightBottomTable_->setItem(row++, 1, warningItem);
    rightBottomTable_->setItem(row, 0, makeCell("Result State"));
    rightBottomTable_->setItem(row++, 1, makeCell(stateLabel));
    rightBottomTable_->setItem(row, 0, makeCell("Row Notes"));
    rightBottomTable_->setItem(row++, 1, makeCell(result.warning.isEmpty() ? "-" : result.warning));
    rightBottomTable_->setWordWrap(true);
    rightBottomTable_->resizeRowsToContents();
}

void MainWindow::handleSolarPlacementFinderResultActivated(int row, int column) {
    Q_UNUSED(column);
    if (row < 0 || row >= solarPlacementFinderResults_.size()) {
        return;
    }
    solarPlacementFinderSelectedIndex_ = row;
    const int year = solarPlacementFinderResults_[row].year;
    if (solarYearSpin_) {
        const QSignalBlocker blocker(solarYearSpin_);
        solarYearSpin_->setValue(year);
    }
    QString err;
    if (!applySolarReturnYear(year, &err)) {
        setStatusMessage(err);
        showSolarPlacementFinderDetails(row);
        return;
    }
    if (isSolarPlacementFinderTabActive()) {
        showSolarPlacementFinderResults();
        if (rightTopTable_ && row >= 0 && row < rightTopTable_->rowCount()) {
            rightTopTable_->selectRow(row);
        }
    }
}

void MainWindow::refreshTransitsTab() {
    if (activeTab_ != AppTab::Transits) {
        return;
    }
    const bool inOverview = (transitSubTab_ == TransitSubTab::Overview);
    auto setOverviewAspectInfo = [this, inOverview](const QString& message) {
        if (!inOverview || !transitAspectsTable_) {
            return;
        }
        setupTable(transitAspectsTable_, {"Info"}, 1);
        transitAspectsTable_->setItem(0, 0, makeCell(message));
        if (transitAspectsCountLabel_) {
            transitAspectsCountLabel_->setText("No current calculation");
        }
        if (transitAspectsCopyButton_) {
            transitAspectsCopyButton_->setEnabled(false);
        }
    };
    if (!hasCurrentChart_ && transitMode_ == TransitMode::NatalOverlay) {
        if (inOverview && rightTopTable_) {
            setupTable(rightTopTable_, {"Info"}, 1);
            rightTopTable_->setItem(0, 0, makeCell("Load a natal chart to use transits."));
        }
        if (inOverview && rightBottomTable_) {
            setupTable(rightBottomTable_, {"Info"}, 1);
            rightBottomTable_->setItem(0, 0, makeCell("Load a natal chart to use transits."));
        }
        setOverviewAspectInfo("Load a natal chart to calculate transit-to-natal aspects.");
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
        setOverviewAspectInfo("Calculate transits to list aspects in effect.");
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
                chartWheel_->setOverlayCharts(natalChartForTransitDisplay(), transitChart, transitHouseSystem_, aspectOrbs_);
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
            populateTransitAspectsInEffect(
                transitChart, transitMode_ == TransitMode::NatalOverlay);
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
            setOverviewAspectInfo("Pending changes. Click Calculate Transits.");
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
    saveCurrentChart();
}

void MainWindow::handleLoadProfile() {
    const QStringList profiles = listProfiles();
    if (profiles.isEmpty()) {
        refreshProfileToolbar();
        setStatusMessage("No saved charts found.");
        return;
    }

    bool ok = false;
    const QString chartName = QInputDialog::getItem(
        this,
        "Load Chart",
        "Saved chart:",
        profiles,
        0,
        false,
        &ok);
    if (!ok) {
        return;
    }
    loadProfileByName(chartName);
}

void MainWindow::handleDeleteProfile() {
    const QStringList profiles = listProfiles();
    if (profiles.isEmpty()) {
        refreshProfileToolbar();
        setStatusMessage("No saved charts found.");
        return;
    }

    bool ok = false;
    const QString chartName = QInputDialog::getItem(
        this,
        "Delete Chart",
        "Saved chart:",
        profiles,
        0,
        false,
        &ok);
    if (!ok) {
        return;
    }
    deleteProfileByName(chartName);
}

static void setupTable(QTableWidget* table, const QStringList& headers, int rows) {
    table->clear();
    table->setColumnCount(headers.size());
    table->setRowCount(rows);
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setShowGrid(false);
    table->setAlternatingRowColors(false);
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

static double aspectExactAngleFor(const QString& label) {
    if (label == "Conjunction") return 0.0;
    if (label == "Sextile") return 60.0;
    if (label == "Square") return 90.0;
    if (label == "Trine") return 120.0;
    if (label == "Opposition") return 180.0;
    return 0.0;
}

// Applying if the orb shrinks when both bodies are projected a small step ahead.
static bool aspectApplyingFor(double lonA, double speedA, double lonB, double speedB, double exact) {
    const double dt = 0.05;
    const double cur = std::fabs(angularDiff(lonA, lonB) - exact);
    const double fut = std::fabs(angularDiff(lonA + speedA * dt, lonB + speedB * dt) - exact);
    return fut < cur;
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
    setupTable(summaryTable_, headers, 12);
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
    summaryTable_->setItem(r++, 1, makeCell(zodiacModeSummary(input)));
    summaryTable_->setItem(r, 0, makeCell("Lunar nodes"));
    summaryTable_->setItem(r++, 1, makeCell(lunarNodePolicySummary(chart.lunarNodePolicy)));
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

    QStringList order = bodyOrderForLunarNodePolicy(chart.lunarNodePolicy);
    int row = 0;
    for (const auto& name : order) {
        if (!map.contains(name)) {
            continue;
        }
        const auto& body = map[name];
        planetsTable_->setItem(row, 0, makeCell(lunarNodeDisplayName(body.name, chart.lunarNodePolicy)));
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

dracoved::AspectMatrixPalette MainWindow::buildAspectMatrixPalette(ThemeMode mode) const {
    AspectMatrixPalette pal;
    if (mode == ThemeMode::Dark) {
        pal.gridBackground = QColor("#0f1112");
        pal.cellBg = QColor("#171b1e");
        pal.cellBorder = QColor("#2a3034");
        pal.diagonalBg = QColor("#202730");
        pal.glyphColor = QColor("#dfe4e8");
        pal.textMuted = QColor("#9aa3a8");
        pal.hoverOverlay = QColor(120, 170, 240, 46);
        pal.aspectColors = {
            {"Conjunction", QColor("#F0B84A")},
            {"Sextile",     QColor("#5FB8E8")},
            {"Square",      QColor("#F0705A")},
            {"Trine",       QColor("#5FBF85")},
            {"Opposition",  QColor("#BE7AD6")},
        };
    } else if (mode == ThemeMode::Creme) {
        pal.gridBackground = QColor("#faf4ec");
        pal.cellBg = QColor("#fbf6ee");
        pal.cellBorder = QColor("#ddceb6");
        pal.diagonalBg = QColor("#eaddc6");
        pal.glyphColor = QColor("#3a2e22");
        pal.textMuted = QColor("#8a7a62");
        pal.hoverOverlay = QColor(170, 130, 70, 46);
        pal.aspectColors = {
            {"Conjunction", QColor("#B07A1E")},
            {"Sextile",     QColor("#2F86B0")},
            {"Square",      QColor("#C5482F")},
            {"Trine",       QColor("#2F8A57")},
            {"Opposition",  QColor("#8A4FA0")},
        };
    } else {  // Light
        pal.gridBackground = QColor("#ffffff");
        pal.cellBg = QColor("#fbfaf7");
        pal.cellBorder = QColor("#e3ddd2");
        pal.diagonalBg = QColor("#f0ebe1");
        pal.glyphColor = QColor("#3a2e22");
        pal.textMuted = QColor("#8a8a8a");
        pal.hoverOverlay = QColor(80, 140, 220, 42);
        pal.aspectColors = {
            {"Conjunction", QColor("#C99A2E")},
            {"Sextile",     QColor("#3FA7D6")},
            {"Square",      QColor("#E0533D")},
            {"Trine",       QColor("#3FA66A")},
            {"Opposition",  QColor("#9B59B6")},
        };
    }
    return pal;
}

void MainWindow::populateAspectMatrix(const QStringList& rowNames, const QStringList& colNames, bool symmetric,
                                      const std::function<AspectMatrixCellData(const QString&, const QString&)>& lookup,
                                      const QString& rowPrefix, const QString& colPrefix) {
    if (!aspectsTable_) {
        return;
    }
    const int rows = rowNames.size();
    const int cols = colNames.size();
    if (rows <= 0 || cols <= 0) {
        setupTable(aspectsTable_, {}, 0);
        aspectTriangleEnabled_ = false;
        return;
    }

    // Transits shows the grid in a narrow column beside the wheel, so it uses a
    // tighter square cell; the other tabs keep the roomier left-dock sizing.
    const bool compactGrid = (activeTab_ == AppTab::Transits);
    const bool largeMatrix = (rows * cols) > 900;
    // ~17 bodies (planets + nodes + Vertex + angles) at 26px fit the Transits
    // column without a horizontal scrollbar.
    const int cellW = compactGrid ? (largeMatrix ? 22 : 26)
                                  : (largeMatrix ? 34 : 42);
    const int cellH = compactGrid ? cellW : cellW - 6;

    if (aspectDelegate_) {
        AspectMatrixPalette palette = buildAspectMatrixPalette(theme_);
        if (compactGrid) {
            // The minimal style drops the per-cell border, and the default fill
            // sits too close to the panel background to read on its own. Give
            // the empty and diagonal tiles a little more separation instead.
            if (theme_ == ThemeMode::Dark) {
                palette.cellBg = QColor("#1b2024");
                palette.diagonalBg = QColor("#252c33");
            } else if (theme_ == ThemeMode::Creme) {
                palette.cellBg = QColor("#f3ebdd");
                palette.diagonalBg = QColor("#e7dcc7");
            } else {
                palette.cellBg = QColor("#f2f1f6");
                palette.diagonalBg = QColor("#e7e5ef");
            }
        }
        aspectDelegate_->setMatrixPalette(palette);
        aspectDelegate_->setCellSize(QSize(cellW, cellH));
        aspectDelegate_->setCompact(compactGrid);
        aspectDelegate_->clearHover();
    }

    aspectsTable_->setUpdatesEnabled(false);
    aspectsTable_->clear();
    aspectsTable_->clearSpans();
    aspectsTable_->setRowCount(rows);
    aspectsTable_->setColumnCount(cols);
    aspectsTable_->setShowGrid(false);
    aspectsTable_->setAlternatingRowColors(false);
    aspectsTable_->setSelectionMode(QAbstractItemView::NoSelection);
    aspectsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    aspectsTable_->horizontalHeader()->setSectionsClickable(false);
    aspectsTable_->horizontalHeader()->setStretchLastSection(false);

    aspectsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    aspectsTable_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    aspectsTable_->horizontalHeader()->setMinimumSectionSize(12);
    aspectsTable_->verticalHeader()->setMinimumSectionSize(12);
    aspectsTable_->horizontalHeader()->setDefaultSectionSize(cellW);
    aspectsTable_->verticalHeader()->setDefaultSectionSize(cellH);

    // Symmetric grids (Transit-Transit, Natal-Natal) are drawn as the classic
    // pyramid: no headers on either axis, with every row ending at a diagonal
    // tile that carries that body's symbol. Cross grids (Transit-Natal) are a
    // full rectangle and label both axes, because the two axes hold different
    // charts and cannot be read off a shared diagonal.
    const bool showHeaders = !symmetric;
    aspectsTable_->horizontalHeader()->setVisible(showHeaders);
    aspectsTable_->verticalHeader()->setVisible(showHeaders);
    if (showHeaders) {
        QStringList rowHeaders;
        QStringList colHeaders;
        rowHeaders.reserve(rows);
        colHeaders.reserve(cols);
        for (const auto& name : rowNames) {
            rowHeaders.push_back(compactGrid ? abbrevForName(name) : aspectHeaderLabel(name));
        }
        for (const auto& name : colNames) {
            colHeaders.push_back(compactGrid ? abbrevForName(name) : aspectHeaderLabel(name));
        }
        aspectsTable_->setHorizontalHeaderLabels(colHeaders);
        aspectsTable_->setVerticalHeaderLabels(rowHeaders);
        applyAspectTableFont();

        // A compact header has no room for words, so the symbol is preferred
        // wherever an SVG exists and the abbreviation is the fallback (angles,
        // Vertex and asteroids have no symbol artwork).
        const bool preferIcons = compactGrid || aspectHeaderMode_ == AspectHeaderMode::Glyphs;
        const QColor headerColor = aspectsTable_->palette().text().color();
        auto applyHeaderIcon = [&](QTableWidgetItem* item, const QString& name) {
            if (!item || !preferIcons) {
                return;
            }
            const QIcon icon = tintedSvgIcon(bodySvgResourcePath(name), headerColor,
                                             compactGrid ? 14 : 17);
            if (!icon.isNull()) {
                item->setText(QString());
                item->setIcon(icon);
            }
        };
        for (int r = 0; r < rows; ++r) {
            if (auto* item = aspectsTable_->verticalHeaderItem(r)) {
                item->setToolTip(rowPrefix.isEmpty() ? rowNames[r] : rowPrefix + ": " + rowNames[r]);
                applyHeaderIcon(item, rowNames[r]);
            }
        }
        for (int c = 0; c < cols; ++c) {
            if (auto* item = aspectsTable_->horizontalHeaderItem(c)) {
                item->setToolTip(colPrefix.isEmpty() ? colNames[c] : colPrefix + ": " + colNames[c]);
                applyHeaderIcon(item, colNames[c]);
            }
        }

        if (compactGrid) {
            aspectsTable_->verticalHeader()->setFixedWidth(cellW);
            aspectsTable_->horizontalHeader()->setFixedHeight(cellH);
        } else {
            aspectsTable_->verticalHeader()->setMinimumWidth(0);
            aspectsTable_->verticalHeader()->setMaximumWidth(QWIDGETSIZE_MAX);
            aspectsTable_->horizontalHeader()->setMinimumHeight(0);
            aspectsTable_->horizontalHeader()->setMaximumHeight(QWIDGETSIZE_MAX);
        }
    }

    auto makeAspectItem = [&](const QString& aName, const QString& bName) -> QTableWidgetItem* {
        auto* item = new QTableWidgetItem();
        item->setFlags(Qt::ItemIsEnabled);
        const AspectMatrixCellData c = lookup(aName, bName);
        if (c.hasAspect && (aspectDisplayMaxOrb_ <= 0.0 || c.orb <= aspectDisplayMaxOrb_)) {
            item->setData(AspectRoles::Kind, AspectFilled);
            item->setData(AspectRoles::Label, c.label);
            item->setData(AspectRoles::Glyph, aspectSymbolForLabel(c.label));
            item->setData(AspectRoles::Orb, c.orb);
            item->setData(AspectRoles::Applying, c.applying);
            const QString motion = (c.applying < 0)
                ? QString()
                : (c.applying == 1 ? QString(" (applying)") : QString(" (separating)"));
            const QString aLabel = rowPrefix.isEmpty() ? aName : rowPrefix + " " + aName;
            const QString bLabel = colPrefix.isEmpty() ? bName : colPrefix + " " + bName;
            item->setToolTip(QString("%1 %2 %3 — orb %4°%5")
                                 .arg(aLabel, c.label, bLabel,
                                      QString::number(c.orb, 'f', 2), motion));
        } else {
            item->setData(AspectRoles::Kind, AspectEmptyBox);
        }
        return item;
    };

    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            QTableWidgetItem* item = nullptr;
            if (symmetric) {
                if (j > i) {
                    item = new QTableWidgetItem();
                    item->setFlags(Qt::ItemIsEnabled);
                    item->setData(AspectRoles::Kind, AspectOutside);
                } else if (i == j) {
                    item = new QTableWidgetItem();
                    item->setFlags(Qt::ItemIsEnabled);
                    item->setData(AspectRoles::Kind, AspectDiagonal);
                    item->setData(AspectRoles::Glyph, bodyGlyph(rowNames[i]));
                    item->setData(AspectRoles::IconPath, bodySvgResourcePath(rowNames[i]));
                    item->setToolTip(rowNames[i]);
                } else {
                    item = makeAspectItem(rowNames[i], colNames[j]);
                }
            } else {
                item = makeAspectItem(rowNames[i], colNames[j]);
            }
            aspectsTable_->setItem(i, j, item);
        }
    }

    aspectTriangleEnabled_ = symmetric;
    clearAspectHover();
    aspectsTable_->setUpdatesEnabled(true);
}

void MainWindow::populateAspects(const NatalChart& chart) {
    const auto& grid = chart.aspects;
    QStringList names;
    QHash<QString, int> indexOf;
    names.reserve(grid.bodyOrder.size());
    for (int i = 0; i < grid.bodyOrder.size(); ++i) {
        const QString& nm = grid.bodyOrder[i];
        if (!isBodyVisibleInAspectGrid(nm)) {
            continue;
        }
        indexOf.insert(nm, i);
        names.push_back(nm);
    }
    if (names.isEmpty()) {
        setupTable(aspectsTable_, {}, 0);
        aspectTriangleEnabled_ = false;
        return;
    }

    auto lookup = [&grid, &indexOf](const QString& a, const QString& b) -> AspectMatrixCellData {
        AspectMatrixCellData out;
        const int ia = indexOf.value(a, -1);
        const int ib = indexOf.value(b, -1);
        if (ia < 0 || ib < 0 || ia >= grid.cells.size() || ib >= grid.cells[ia].size()) {
            return out;
        }
        const auto& c = grid.cells[ia][ib];
        if (!c.hasAspect) {
            return out;
        }
        out.hasAspect = true;
        out.label = c.label;
        out.orb = c.orb;
        out.applying = c.hasMotion ? (c.applying ? 1 : 0) : -1;
        return out;
    };

    populateAspectMatrix(names, names, true, lookup, QString(), QString());
}

void MainWindow::populateCrossAspectsOverlay(const NatalChart& rowChart, const NatalChart& natalChart, const QString& rowPrefix) {
    if (!aspectsTable_) {
        return;
    }
    QMap<QString, double> rowMap;
    QMap<QString, double> rowSpeed;
    for (const auto& body : rowChart.bodies) {
        rowMap.insert(body.name, body.longitude);
        if (body.hasSpeed) {
            rowSpeed.insert(body.name, body.speed);
        }
    }
    if (aspectGridFilter_.showAngles) {
        rowMap.insert("Ascendant", rowChart.angles.asc);
        rowMap.insert("Midheaven", rowChart.angles.mc);
        rowMap.insert("Descendant", rowChart.angles.desc);
        rowMap.insert("IC", rowChart.angles.ic);
    }
    QMap<QString, double> natalMap;
    for (const auto& body : natalChart.bodies) {
        natalMap.insert(body.name, body.longitude);
    }
    if (aspectGridFilter_.showAngles) {
        natalMap.insert("Ascendant", natalChart.angles.asc);
        natalMap.insert("Midheaven", natalChart.angles.mc);
        natalMap.insert("Descendant", natalChart.angles.desc);
        natalMap.insert("IC", natalChart.angles.ic);
    }

    QStringList rowNames;
    QStringList colNames;
    QStringList crossOrder = bodyOrderForLunarNodePolicy(rowChart.lunarNodePolicy);
    for (const auto& name : bodyOrderForLunarNodePolicy(natalChart.lunarNodePolicy)) {
        if (!crossOrder.contains(name)) {
            crossOrder.push_back(name);
        }
    }
    for (const auto& name : crossOrder) {
        if (!isBodyVisibleInAspectGrid(name)) {
            continue;
        }
        if (rowMap.contains(name)) {
            rowNames.push_back(name);
        }
        if (natalMap.contains(name)) {
            colNames.push_back(name);
        }
    }

    auto lookup = [this, &rowMap, &rowSpeed, &natalMap](const QString& rowName, const QString& colName) -> AspectMatrixCellData {
        AspectMatrixCellData out;
        const double rLon = rowMap.value(rowName);
        const double nLon = natalMap.value(colName);
        const double diff = angularDiff(rLon, nLon);
        QString label;
        double orb = 0.0;
        double maxOrb = 0.0;
        if (!aspectForDiff(diff, aspectOrbs_, &label, &orb, &maxOrb)) {
            return out;
        }
        out.hasAspect = true;
        out.label = label;
        out.orb = orb;
        // Natal point is the fixed reference; applying determined by the row
        // chart body's motion only (angles/lots have no speed → unknown).
        if (rowSpeed.contains(rowName)) {
            const bool app = aspectApplyingFor(rLon, rowSpeed.value(rowName), nLon, 0.0,
                                               aspectExactAngleFor(label));
            out.applying = app ? 1 : 0;
        }
        return out;
    };

    populateAspectMatrix(rowNames, colNames, false, lookup, rowPrefix, "Natal");
}

void MainWindow::populateTransitAspectsOverlay(const NatalChart& transitChart, const NatalChart& natalChart) {
    populateCrossAspectsOverlay(transitChart, natalChart, "Transit");
}

void MainWindow::populateProgressedAspectsOverlay(const NatalChart& progressedChart, const NatalChart& natalChart) {
    populateCrossAspectsOverlay(progressedChart, natalChart, "Progressed");
}

void MainWindow::populateSolarNatalAspectsOverlay(const NatalChart& solarChart, const NatalChart& natalChart) {
    populateCrossAspectsOverlay(solarChart, natalChart, "Solar");
}

void MainWindow::populateRelocationNatalAspectsOverlay(const NatalChart& relocationChart, const NatalChart& natalChart) {
    populateCrossAspectsOverlay(relocationChart, natalChart, "Relocation");
}

// The new_svg_icons_by_opus_5 zodiac set, reached through resources.qrc. Windows
// routes the U+2648..U+2653 sign characters through the colour emoji font, which
// renders them as coloured emoji and looks wrong beside the monochrome planet
// symbols, so the side panels draw these SVGs instead.
static QString zodiacSignSvgResourcePath(int signIdx) {
    static const QStringList files = {
        "aries", "taurus", "gemini", "cancer", "leo", "virgo",
        "libra", "scorpio", "sagittarius", "capricorn", "aquarius", "pisces",
    };
    if (signIdx < 0 || signIdx >= files.size()) {
        return QString();
    }
    return QString(":/resources/icons/zodiac_releasing/%1.svg").arg(files[signIdx]);
}

// Compact zodiacal position for the Transits tab, e.g. "03°53' Leo".
// formatDegInSign's "03 Leo 40' 51.98"" form is too wide for the side panel and
// truncated on the longer sign names (Capricorn, Aquarius, Sagittarius). The
// sign symbol is supplied separately as a cell icon.
static QString compactSignPosition(double longitude) {
    const int sign = signIndex(longitude);
    const double deg = degInSign(longitude);
    int whole = static_cast<int>(deg);
    int minutes = static_cast<int>(std::llround((deg - whole) * 60.0));
    if (minutes >= 60) {
        minutes -= 60;
        whole += 1;
    }
    if (whole >= 30) {
        whole -= 30;
    }
    return QString("%1%2%3' %4")
        .arg(QString::number(whole).rightJustified(2, '0'))
        .arg(QChar(0x00B0))
        .arg(QString::number(minutes).rightJustified(2, '0'))
        .arg(signName(sign));
}

// "<sun> Sun" - prefix a display name with its symbol so the side panels stay
// narrow. Angles resolve to text abbreviations ("AC", "MC") rather than real
// symbols, so those are left alone instead of reading "AC Ascendant".
static QString glyphPrefixedName(const QString& internalName, const QString& displayName) {
    const QString glyph = bodyGlyph(internalName);
    const bool isSymbol = !glyph.isEmpty() && glyph.at(0).unicode() > 0x2000;
    return isSymbol ? QString("%1 %2").arg(glyph, displayName) : displayName;
}

void MainWindow::populateTransitList(const NatalChart& transitChart, bool overlayMode) {
    if (!rightTopTable_) {
        return;
    }

    const int filter = transitListFilterCombo_
        ? transitListFilterCombo_->currentData().toInt() : 0;
    setupTable(rightTopTable_, {"Body", "Position", "Hse", "Speed"}, transitChart.bodies.size());

    QMap<QString, BodyPosition> map;
    for (const auto& body : transitChart.bodies) {
        map.insert(body.name, body);
    }

    static const QSet<QString> mainPlanets = {
        "Sun", "Moon", "Mercury", "Venus", "Mars",
        "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto",
    };
    const auto shouldShow = [filter](const QString& name) {
        const bool isMainPlanet = mainPlanets.contains(name);
        const bool isArabicLot = name == "Part of Fortune" || name.startsWith("Lot of ");
        switch (filter) {
            case 1: return !isArabicLot;
            case 2: return isArabicLot;
            case 3: return true;
            case 4: return isLunarNodeName(name);
            case 0:
            default: return isMainPlanet;
        }
    };

    int row = 0;
    for (const auto& name : bodyOrderForLunarNodePolicy(transitChart.lunarNodePolicy)) {
        if (!map.contains(name) || !shouldShow(name)) {
            continue;
        }
        const auto& body = map[name];
        const int house = overlayMode
            ? calcHouseForLongitude(body.longitude, natalPlacidusCusps_,
                                    currentChart_.angles.asc, transitHouseSystem_)
            : body.house;
        const QString displayName = lunarNodeDisplayName(body.name, transitChart.lunarNodePolicy);
        const QString position = QString("%1%2")
            .arg(compactSignPosition(body.longitude), body.retrograde ? QString("  R") : QString());
        QString speed = "-";
        if (body.hasSpeed && std::isfinite(body.speed)) {
            speed = QString("%1%2%3")
                .arg(body.speed >= 0.0 ? "+" : "")
                .arg(QString::number(body.speed, 'f', 3))
                .arg(QChar(0x00B0));
        }

        // Prefer the bundled SVG symbols over the Unicode astrological
        // characters; Windows substitutes a colour emoji font for several of
        // them. Bodies with no SVG (asteroids, Lots) keep the text glyph.
        const QColor iconColor = rightTopTable_->palette().text().color();
        const QIcon bodyIcon = tintedSvgIcon(bodySvgResourcePath(body.name), iconColor, 15);
        auto* bodyItem = bodyIcon.isNull()
            ? makeCell(glyphPrefixedName(body.name, displayName))
            : makeCell(displayName);
        if (!bodyIcon.isNull()) {
            bodyItem->setIcon(bodyIcon);
        }

        auto* positionItem = makeCell(position);
        const QIcon signIcon = tintedSvgIcon(
            zodiacSignSvgResourcePath(signIndex(body.longitude)), iconColor, 15);
        if (!signIcon.isNull()) {
            positionItem->setIcon(signIcon);
        }
        positionItem->setToolTip(QString("%1\n%2")
            .arg(formatDegInSign(body.longitude), formatDailyMotion(body.speed, body.hasSpeed)));
        rightTopTable_->setItem(row, 0, bodyItem);
        rightTopTable_->setItem(row, 1, positionItem);
        rightTopTable_->setItem(row, 2, makeCell(house > 0 ? QString::number(house) : "-", Qt::AlignCenter));
        auto* speedItem = makeCell(speed, Qt::AlignRight | Qt::AlignVCenter);
        // The "/d" suffix was dropped to keep the column narrow; keep the unit
        // discoverable on hover.
        speedItem->setToolTip(formatDailyMotion(body.speed, body.hasSpeed));
        rightTopTable_->setItem(row, 3, speedItem);
        ++row;
    }
    rightTopTable_->setRowCount(row);
    if (auto* header = rightTopTable_->horizontalHeader()) {
        header->setSectionResizeMode(QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::Stretch);
        // setupTable turns this on; leaving it on lets Speed absorb the slack
        // and squeezes Position back into truncation.
        header->setStretchLastSection(false);
    }
    rightTopTable_->scrollToTop();
}
void MainWindow::populateTransitAspectsInEffect(const NatalChart& transitChart, bool overlayMode) {
    if (!transitAspectsTable_) {
        return;
    }

    struct AspectHit {
        QString subject;
        QString target;
        QString label;
        double orb = 0.0;
        int applying = -1;
    };
    QVector<AspectHit> hits;

    static const QSet<QString> mainPlanets = {
        "Sun", "Moon", "Mercury", "Venus", "Mars",
        "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto",
    };
    QVector<BodyPosition> transitBodies;
    for (const auto& body : transitChart.bodies) {
        if (mainPlanets.contains(body.name)) {
            transitBodies.push_back(body);
        }
    }

    auto addAspect = [&](const QString& subject, double subjectLongitude,
                         double subjectSpeed, bool subjectHasSpeed,
                         const QString& target, double targetLongitude,
                         double targetSpeed, bool targetHasSpeed) {
        QString label;
        double orb = 0.0;
        double configuredOrb = 0.0;
        if (!aspectForDiff(angularDiff(subjectLongitude, targetLongitude), aspectOrbs_,
                           &label, &orb, &configuredOrb)) {
            return;
        }
        if (aspectDisplayMaxOrb_ > 0.0 && orb > aspectDisplayMaxOrb_) {
            return;
        }
        int applying = -1;
        if (subjectHasSpeed && targetHasSpeed) {
            applying = aspectApplyingFor(subjectLongitude, subjectSpeed,
                                         targetLongitude, targetSpeed,
                                         aspectExactAngleFor(label)) ? 1 : 0;
        }
        hits.push_back({subject, target, label, orb, applying});
    };

    if (overlayMode && hasCurrentChart_) {
        QVector<BodyPosition> natalBodies;
        for (const auto& body : currentChart_.bodies) {
            if (mainPlanets.contains(body.name)) {
                natalBodies.push_back(body);
            }
        }
        for (const auto& transitBody : transitBodies) {
            for (const auto& natalBody : natalBodies) {
                addAspect(QString("Transit %1").arg(transitBody.name),
                          transitBody.longitude, transitBody.speed, transitBody.hasSpeed,
                          QString("Natal %1").arg(natalBody.name),
                          natalBody.longitude, 0.0, true);
            }
            const QList<QPair<QString, double>> angles = {
                {"Natal Ascendant", currentChart_.angles.asc},
                {"Natal Midheaven", currentChart_.angles.mc},
                {"Natal Descendant", currentChart_.angles.desc},
                {"Natal IC", currentChart_.angles.ic},
            };
            for (const auto& angle : angles) {
                addAspect(QString("Transit %1").arg(transitBody.name),
                          transitBody.longitude, transitBody.speed, transitBody.hasSpeed,
                          angle.first, angle.second, 0.0, true);
            }
        }
    } else {
        for (int i = 0; i < transitBodies.size(); ++i) {
            for (int j = i + 1; j < transitBodies.size(); ++j) {
                const auto& a = transitBodies[i];
                const auto& b = transitBodies[j];
                addAspect(a.name, a.longitude, a.speed, a.hasSpeed,
                          b.name, b.longitude, b.speed, b.hasSpeed);
            }
        }
    }

    std::sort(hits.begin(), hits.end(), [](const AspectHit& a, const AspectHit& b) {
        if (std::fabs(a.orb - b.orb) > 1e-9) {
            return a.orb < b.orb;
        }
        if (a.subject != b.subject) {
            return a.subject < b.subject;
        }
        return a.target < b.target;
    });

    if (transitAspectsCountLabel_) {
        const QString orbScope = aspectDisplayMaxOrb_ > 0.0
            ? QString("within %1%2").arg(QString::number(aspectDisplayMaxOrb_, 'f', 1),
                                          QString(QChar(0x00B0)))
            : QString("within configured orbs");
        transitAspectsCountLabel_->setText(QString("%1 %2").arg(hits.size()).arg(orbScope));
    }
    if (transitAspectsCopyButton_) {
        transitAspectsCopyButton_->setEnabled(!hits.isEmpty());
    }

    // Split the old single "Aspect" column into subject / symbol / target. The
    // combined "Transit Saturn <trine> Natal Midheaven" string outgrew the
    // column and truncated; the words "Transit" and "Natal" are now carried by
    // the headers instead of repeating on every row.
    setupTable(transitAspectsTable_,
               overlayMode ? QStringList{"Transit", "", "Natal", "Orb", "Motion"}
                           : QStringList{"From", "", "To", "Orb", "Motion"},
               hits.size());
    auto formatOrb = [](double orb) {
        const int totalMinutes = qMax(0, qRound(orb * 60.0));
        return QString("%1%2 %3%4")
            .arg(totalMinutes / 60)
            .arg(QChar(0x00B0))
            .arg(totalMinutes % 60, 2, 10, QChar('0'))
            .arg(QChar(0x2032));
    };
    auto shortLabel = [](const QString& fullLabel) {
        QString name = fullLabel;
        if (name.startsWith("Transit ")) {
            name = name.mid(8);
        } else if (name.startsWith("Natal ")) {
            name = name.mid(6);
        }
        return glyphPrefixedName(name, name);
    };
    for (int row = 0; row < hits.size(); ++row) {
        const auto& hit = hits[row];
        const QString tooltip = QString("%1 %2 %3").arg(hit.subject, hit.label, hit.target);
        QColor aspectColor;
        if (hit.label == "Square" || hit.label == "Opposition") {
            aspectColor = QColor("#d9534f");
        } else if (hit.label == "Trine" || hit.label == "Sextile") {
            aspectColor = QColor("#3f8f68");
        }

        auto* subjectItem = makeCell(shortLabel(hit.subject));
        subjectItem->setToolTip(tooltip);
        auto* symbolItem = makeCell(aspectSymbolForLabel(hit.label), Qt::AlignCenter);
        symbolItem->setToolTip(tooltip);
        auto* targetItem = makeCell(shortLabel(hit.target));
        targetItem->setToolTip(tooltip);
        if (aspectColor.isValid()) {
            symbolItem->setForeground(aspectColor);
        }

        const QString motion = hit.orb <= (1.0 / 60.0)
            ? QString("Exact")
            : (hit.applying < 0 ? QString("-")
                : (hit.applying == 1 ? QString("Applying") : QString("Separating")));
        transitAspectsTable_->setItem(row, 0, subjectItem);
        transitAspectsTable_->setItem(row, 1, symbolItem);
        transitAspectsTable_->setItem(row, 2, targetItem);
        transitAspectsTable_->setItem(row, 3,
            makeCell(formatOrb(hit.orb), Qt::AlignRight | Qt::AlignVCenter));
        transitAspectsTable_->setItem(row, 4, makeCell(motion, Qt::AlignCenter));
    }
    if (auto* header = transitAspectsTable_->horizontalHeader()) {
        header->setSectionResizeMode(QHeaderView::ResizeToContents);
        header->setSectionResizeMode(0, QHeaderView::Stretch);
        header->setSectionResizeMode(2, QHeaderView::Stretch);
        // Let the two body columns share the slack instead of Motion taking it.
        header->setStretchLastSection(false);
    }
    transitAspectsTable_->scrollToTop();
}
void MainWindow::updateTransitListFilterVisibility() {
    if (!transitListFilterPanel_) {
        return;
    }
    transitListFilterPanel_->setVisible(
        activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::Overview);
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
        rightTopTable_->setItem(i, 0, makeCell(lunarNodeDisplayName(hit.tName, transitChart.lunarNodePolicy)));
        auto* aspectCell = makeCell(hit.label, Qt::AlignCenter);
        if (hit.label == "Square" || hit.label == "Opposition") {
            aspectCell->setForeground(QColor("#e05555"));
        } else if (hit.label == "Trine" || hit.label == "Sextile") {
            aspectCell->setForeground(QColor("#4aa3ff"));
        }
        rightTopTable_->setItem(i, 1, aspectCell);
        rightTopTable_->setItem(i, 2, makeCell(lunarNodeDisplayName(hit.nName, natalChart.lunarNodePolicy)));
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
        applyZodiacModeToSwe(&swe_, transitInput);
        const int calcFlags = calcFlagsForInput(transitInput);
        auto bodyLongitudeAtUtc = [this, calcFlags](const QString& bodyName, const QDateTime& utc, double* outLon) -> bool {
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
            if (!swe_.calcUt(jd, bodyId, calcFlags, &lon, &calcErr)) {
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
            const QString timeLabel = hiUtc.toTimeZone(tz).toString("d MMM yyyy, HH:mm");
            rows.push_back({bodyName, nextSignName, countdown, timeLabel});
        }

        s_lastCacheKey = cacheKey;
        s_cachedRows = rows;
    }

    setupTable(rightBottomTable_, {"Body", "Next Sign", "In", "Exact"}, rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        rightBottomTable_->setItem(i, 0, makeCell(row.body));
        rightBottomTable_->setItem(i, 1, makeCell(row.nextSign));
        rightBottomTable_->setItem(i, 2, makeCell(row.countdown, Qt::AlignRight | Qt::AlignVCenter));
        rightBottomTable_->setItem(i, 3, makeCell(row.timeLabel));
    }
    if (auto* header = rightBottomTable_->horizontalHeader()) {
        header->setSectionResizeMode(QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::Stretch);
    }
}

}  // namespace dracoved
