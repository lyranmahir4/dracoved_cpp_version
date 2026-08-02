#include "chart_wheel_widget.h"

#include "../core/fixed_stars.h"
#include "../core/formatting.h"

#include <QContextMenuEvent>
#include <QMap>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPaintEvent>
#include <QSvgRenderer>
#include <QLineF>
#include <QEvent>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <QtMath>

namespace dracoved {

namespace {

double angularDiff(double a, double b) {
    double d = std::fmod((a - b + 540.0), 360.0) - 180.0;
    return std::fabs(d);
}

QString aspectSymbolForLabel(const QString& label) {
    if (label == "Conjunction") return QString(QChar(0x260C));
    if (label == "Sextile") return QString(QChar(0x2736));
    if (label == "Square") return QString(QChar(0x25A1));
    if (label == "Trine") return QString(QChar(0x25B3));
    if (label == "Opposition") return QString(QChar(0x260D));
    return "";
}

// Distinct color per aspect type. Scope (transit-natal / transit-transit /
// natal-natal) is encoded separately via line style, so colour can be used
// purely to differentiate the five Ptolemaic aspects for easier reading.
QColor aspectTypeColor(const QString& label) {
    if (label == "Conjunction") return QColor("#C99A2E");  // amber/gold
    if (label == "Sextile")     return QColor("#3FA7D6");  // cyan-blue
    if (label == "Square")      return QColor("#E0533D");  // red
    if (label == "Trine")       return QColor("#3FA66A");  // green
    if (label == "Opposition")  return QColor("#9B59B6");  // violet
    return QColor("#90A4AE");
}

// The two lunar nodes are always exactly 180 deg apart, so an aspect line
// between them carries no information and only crowds the wheel.
bool isLunarNodePair(const QString& a, const QString& b) {
    if (!isLunarNodeName(a) || !isLunarNodeName(b)
        || isNorthLunarNodeName(a) == isNorthLunarNodeName(b)) {
        return false;
    }
    const bool aGeneric = a == "North Node" || a == "South Node";
    const bool bGeneric = b == "North Node" || b == "South Node";
    if (aGeneric || bGeneric) {
        return aGeneric && bGeneric;
    }
    return lunarNodeTypeForName(a, LunarNodeType::Mean)
        == lunarNodeTypeForName(b, LunarNodeType::Mean);
}

// Chart angles are fixed relative to one another (AC/DC and MC/IC are exact
// oppositions; their square relationships are structural), so aspect lines
// between angles are redundant too.
bool isChartAngleName(const QString& n) {
    return n == "Ascendant" || n == "Midheaven" || n == "Descendant" || n == "IC";
}

bool aspectFor(double diff, const AspectOrbs& orbs, QString* outLabel, double* outOrb, double* outMaxOrb) {
    struct AspectDef {
        const char* name;
        double exact;
        double orb;
    };
    const AspectDef aspects[] = {
        {"Conjunction", 0.0, orbs.conjunction},
        {"Sextile", 60.0, orbs.sextile},
        {"Square", 90.0, orbs.square},
        {"Trine", 120.0, orbs.trine},
        {"Opposition", 180.0, orbs.opposition},
    };
    for (const auto& asp : aspects) {
        double delta = std::fabs(diff - asp.exact);
        if (delta <= asp.orb) {
            if (outLabel) {
                *outLabel = asp.name;
            }
            if (outOrb) {
                *outOrb = delta;
            }
            if (outMaxOrb) {
                *outMaxOrb = asp.orb;
            }
            return true;
        }
    }
    return false;
}

}  // namespace

ChartWheelWidget::ChartWheelWidget(QWidget* parent)
    : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setVisibleAsteroids(asteroidBodyOrder());
    setVisibleFixedStars(fixedStarCatalog());
    theme_ = ChartWheelTheme{
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
        QColor("#EEF2F7"),          // transitLaneBand - faint cool tint
    };
}

void ChartWheelWidget::setChart(const NatalChart& chart, HouseSystem system) {
    chart_ = chart;
    houseSystem_ = system;
    hasChart_ = true;
    hasOverlay_ = false;
    mode_ = Mode::NatalOnly;
    hoveredAspectIndex_ = -1;
    aspectLines_.clear();
    chartNote_.clear();
    update();
}

void ChartWheelWidget::setTransitChart(const NatalChart& chart, HouseSystem system) {
    chart_ = chart;
    houseSystem_ = system;
    hasChart_ = true;
    hasOverlay_ = false;
    mode_ = Mode::TransitOnly;
    hoveredAspectIndex_ = -1;
    aspectLines_.clear();
    chartNote_.clear();
    update();
}

void ChartWheelWidget::setOverlayCharts(const NatalChart& natal, const NatalChart& transit, HouseSystem system, const AspectOrbs& orbs) {
    chart_ = natal;
    overlayChart_ = transit;
    houseSystem_ = system;
    hasChart_ = true;
    hasOverlay_ = true;
    mode_ = Mode::Overlay;
    aspectOrbs_ = orbs;
    hoveredAspectIndex_ = -1;
    aspectLines_.clear();
    chartNote_.clear();
    update();
}

void ChartWheelWidget::setOverlayLabel(const QString& label) {
    const QString trimmed = label.trimmed();
    overlayLabel_ = trimmed.isEmpty() ? "Transit" : trimmed;
    update();
}

void ChartWheelWidget::setChartNote(const QString& note) {
    chartNote_ = note;
    update();
}

void ChartWheelWidget::setOverlayAspectScopes(bool transitNatal, bool transitTransit, bool natalNatal) {
    overlayTransitNatalAspects_ = transitNatal;
    overlayTransitTransitAspects_ = transitTransit;
    overlayNatalNatalAspects_ = natalNatal;
    update();
}

void ChartWheelWidget::setAspectDisplayMaxOrb(double maxOrb) {
    aspectDisplayMaxOrb_ = std::max(0.0, maxOrb);
    update();
}

void ChartWheelWidget::setZoom(double zoom) {
    zoom_ = std::clamp(zoom, 0.6, 1.8);
    if (zoom_ <= 1.0) {
        panOffset_ = {0.0, 0.0};
    }
    updateCursor();
    update();
}

void ChartWheelWidget::zoomIn() {
    setZoom(zoom_ + 0.1);
}

void ChartWheelWidget::zoomOut() {
    setZoom(zoom_ - 0.1);
}

void ChartWheelWidget::resetZoom() {
    zoom_ = 1.0;
    panOffset_ = {0.0, 0.0};
    updateCursor();
    update();
}

void ChartWheelWidget::setShowAspects(bool value) {
    showAspects_ = value;
    if (!showAspects_) {
        hoveredAspectIndex_ = -1;
    }
    update();
}

void ChartWheelWidget::setShowTicks(bool value) {
    showTicks_ = value;
    update();
}

void ChartWheelWidget::setShowDegrees(bool value) {
    showDegrees_ = value;
    update();
}

void ChartWheelWidget::setShowAspectSymbols(bool value) {
    showAspectSymbols_ = value;
    update();
}

void ChartWheelWidget::setShowAsteroids(bool value) {
    showAsteroids_ = value;
    update();
}

void ChartWheelWidget::setIncludeAsteroidAspects(bool value) {
    includeAsteroidAspects_ = value;
    update();
}

void ChartWheelWidget::setVisibleAsteroids(const QStringList& names) {
    QSet<QString> nextSet;
    for (const auto& name : names) {
        if (isAsteroidBody(name)) {
            nextSet.insert(name);
        }
    }
    visibleAsteroidSet_ = nextSet;
    visibleAsteroids_.clear();
    for (const auto& name : asteroidBodyOrder()) {
        if (visibleAsteroidSet_.contains(name)) {
            visibleAsteroids_.push_back(name);
        }
    }
    update();
}

void ChartWheelWidget::setShowLots(bool value) {
    showLots_ = value;
    update();
}

void ChartWheelWidget::setShowDerivedPoints(bool value) {
    showDerivedPoints_ = value;
    update();
}

void ChartWheelWidget::setShowFixedStars(bool value) {
    showFixedStars_ = value;
    update();
}

void ChartWheelWidget::setVisibleFixedStars(const QStringList& names) {
    QSet<QString> nextSet;
    for (const auto& name : names) {
        const QString normalized = name.trimmed().toCaseFolded();
        if (!normalized.isEmpty()) {
            nextSet.insert(normalized);
        }
    }
    visibleFixedStarSet_ = nextSet;
    visibleFixedStars_.clear();
    for (const auto& name : fixedStarCatalog()) {
        const QString normalized = name.trimmed().toCaseFolded();
        if (!normalized.isEmpty() && visibleFixedStarSet_.contains(normalized)) {
            visibleFixedStars_.push_back(name);
        }
    }
    update();
}

void ChartWheelWidget::setTickDensity(TickDensity density) {
    tickDensity_ = density;
    update();
}

void ChartWheelWidget::setFontScale(double scale) {
    fontScale_ = std::clamp(scale, 0.8, 1.4);
    update();
}

void ChartWheelWidget::setTheme(const ChartWheelTheme& theme) {
    theme_ = theme;
    glyphPixmapCache_.clear();
    update();
}

QPixmap ChartWheelWidget::coloredSvgPixmap(const QString& path, const QColor& color, const QSize& sizePx, qreal dpr) {
    const QString key = QStringLiteral("%1|%2|%3x%4|%5")
        .arg(path, color.name(QColor::HexArgb))
        .arg(sizePx.width())
        .arg(sizePx.height())
        .arg(QString::number(dpr, 'f', 2));
    const auto it = glyphPixmapCache_.constFind(key);
    if (it != glyphPixmapCache_.constEnd()) {
        return it.value();
    }
    QPixmap px(sizePx * dpr);
    px.fill(Qt::transparent);
    px.setDevicePixelRatio(dpr);
    QSvgRenderer renderer(path);
    if (renderer.isValid()) {
        QPainter p(&px);
        renderer.render(&p);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(px.rect(), color);
    }
    glyphPixmapCache_.insert(key, px);
    return px;
}

bool ChartWheelWidget::showAspects() const {
    return showAspects_;
}

bool ChartWheelWidget::showTicks() const {
    return showTicks_;
}

bool ChartWheelWidget::showDegrees() const {
    return showDegrees_;
}

bool ChartWheelWidget::showAspectSymbols() const {
    return showAspectSymbols_;
}

bool ChartWheelWidget::showAsteroids() const {
    return showAsteroids_;
}

bool ChartWheelWidget::includeAsteroidAspects() const {
    return includeAsteroidAspects_;
}

QStringList ChartWheelWidget::visibleAsteroids() const {
    return visibleAsteroids_;
}

bool ChartWheelWidget::showLots() const {
    return showLots_;
}

bool ChartWheelWidget::showDerivedPoints() const {
    return showDerivedPoints_;
}

bool ChartWheelWidget::showFixedStars() const {
    return showFixedStars_;
}

QStringList ChartWheelWidget::visibleFixedStars() const {
    return visibleFixedStars_;
}

ChartWheelWidget::TickDensity ChartWheelWidget::tickDensity() const {
    return tickDensity_;
}

double ChartWheelWidget::zoom() const {
    return zoom_;
}

double ChartWheelWidget::fontScale() const {
    return fontScale_;
}

ChartWheelWidget::Mode ChartWheelWidget::mode() const {
    return mode_;
}

double ChartWheelWidget::aspectDisplayMaxOrb() const {
    return aspectDisplayMaxOrb_;
}

const ChartWheelTheme& ChartWheelWidget::theme() const {
    return theme_;
}

void ChartWheelWidget::clearChart() {
    hasChart_ = false;
    hasOverlay_ = false;
    mode_ = Mode::NatalOnly;
    hoveredAspectIndex_ = -1;
    aspectLines_.clear();
    planetHitAreas_.clear();
    planetTooltips_.clear();
    planetHitNames_.clear();
    hasHighlight_ = false;
    highlightBody_.clear();
    highlightLabel_.clear();
    hasFocus_ = false;
    focusBody_.clear();
    chartNote_.clear();
    update();
}

void ChartWheelWidget::setHighlight(const QString& bodyName, bool transit, const QString& label, const QColor& color) {
    highlightBody_ = bodyName;
    highlightTransit_ = transit;
    highlightLabel_ = label;
    highlightColor_ = color;
    hasHighlight_ = !highlightBody_.isEmpty();
    update();
}

void ChartWheelWidget::clearHighlight() {
    hasHighlight_ = false;
    highlightBody_.clear();
    highlightLabel_.clear();
    update();
}

QSize ChartWheelWidget::minimumSizeHint() const {
    return {520, 520};
}

double ChartWheelWidget::angleForLongitude(double lon) const {
    const double asc = chart_.angles.asc;
    double angle = 180.0 + (lon - asc);
    angle = normalizeDegrees(angle);
    return angle;
}

QPointF ChartWheelWidget::pointOnCircle(const QPointF& center, double radius, double angleDeg) const {
    const double rad = qDegreesToRadians(angleDeg);
    return {
        center.x() + std::cos(rad) * radius,
        center.y() - std::sin(rad) * radius,
    };
}

QVector<double> ChartWheelWidget::buildHouseCusps() const {
    QVector<double> cusps;
    if (houseSystem_ == HouseSystem::Placidus && chart_.cusps.size() == 12) {
        cusps.reserve(12);
        for (const auto& c : chart_.cusps) {
            cusps.push_back(normalizeDegrees(c.longitude));
        }
        return cusps;
    }
    const double asc = chart_.angles.asc;
    double base = std::floor(normalizeDegrees(asc) / 30.0) * 30.0;
    cusps.reserve(12);
    for (int i = 0; i < 12; ++i) {
        cusps.push_back(normalizeDegrees(base + i * 30.0));
    }
    return cusps;
}

QString ChartWheelWidget::formatDegShort(double longitude) const {
    const double degVal = degInSign(longitude);
    int wholeDeg = static_cast<int>(degVal);
    int minutes = static_cast<int>((degVal - wholeDeg) * 60.0 + 0.5);
    if (minutes >= 60) {
        minutes -= 60;
        wholeDeg += 1;
    }
    if (wholeDeg >= 30) {
        wholeDeg -= 30;
    }
    return QString("%1%2%3'")
        .arg(QString::number(wholeDeg).rightJustified(2, '0'))
        .arg(QChar(0x00B0))
        .arg(QString::number(minutes).rightJustified(2, '0'));
}

int ChartWheelWidget::houseForLongitude(double lon, const QVector<double>& cusps) const {
    if (cusps.size() < 12) {
        return 0;
    }
    const double c1 = cusps[0];
    double target = normalizeDegrees(lon - c1);
    int house = 1;
    double last = 0.0;
    for (int i = 0; i < cusps.size(); ++i) {
        double v = normalizeDegrees(cusps[i] - c1);
        if (v < last) {
            continue;
        }
        if (target >= v) {
            house = i + 1;
            last = v;
        }
    }
    return house;
}

bool ChartWheelWidget::isAsteroidVisible(const QString& name) const {
    if (!isAsteroidBody(name)) {
        return true;
    }
    return visibleAsteroidSet_.contains(name);
}

bool ChartWheelWidget::isFixedStarVisible(const QString& name) const {
    const QString normalized = name.trimmed().toCaseFolded();
    if (normalized.isEmpty()) {
        return false;
    }
    return visibleFixedStarSet_.contains(normalized);
}

int ChartWheelWidget::hitTestAspect(const QPointF& point) const {
    if (aspectLines_.isEmpty()) {
        return -1;
    }
    const bool focusActive = focusActiveNow();
    const double threshold = 5.0 * fontScale_;
    for (int i = 0; i < aspectLines_.size(); ++i) {
        const auto& info = aspectLines_[i];
        // In focus mode only the focused body's lines are visible/interactive.
        if (focusActive && info.rawNameA != focusBody_ && info.rawNameB != focusBody_) {
            continue;
        }
        if (info.symbolRect.contains(point)) {
            return i;
        }
        QPainterPathStroker stroker;
        stroker.setWidth(threshold * 2.0);
        stroker.setCapStyle(Qt::RoundCap);
        if (stroker.createStroke(info.path).contains(point)) {
            return i;
        }
    }
    return -1;
}

bool ChartWheelWidget::focusActiveNow() const {
    if (!hasFocus_ || focusBody_.isEmpty()) {
        return false;
    }
    for (const auto& info : aspectLines_) {
        if (info.rawNameA == focusBody_ || info.rawNameB == focusBody_) {
            return true;
        }
    }
    return false;
}

void ChartWheelWidget::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() > 0) {
        zoomIn();
    } else {
        zoomOut();
    }
    event->accept();
}

void ChartWheelWidget::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const QPointF delta = event->position() - lastPanPos_;
        panOffset_ += delta;
        const double availableWidth = std::max(0.0, width() - 24.0);
        const double availableHeight = std::max(0.0, height() - 24.0);
        const double size = std::min(availableWidth, availableHeight);
        const double decorationBudget = 35.0 * fontScale_ + 34.0;
        const double fittedOuter = std::max(72.0, size * 0.5 - decorationBudget);
        const double zodiacRadius = std::min(size * 0.42, fittedOuter) * zoom_;
        const double contentRadius = zodiacRadius + decorationBudget;
        const double maxX = std::max(0.0, width() * 0.5 + contentRadius - 24.0);
        const double maxY = std::max(0.0, height() * 0.5 + contentRadius - 24.0);
        panOffset_.setX(std::clamp(panOffset_.x(), -maxX, maxX));
        panOffset_.setY(std::clamp(panOffset_.y(), -maxY, maxY));
        lastPanPos_ = event->position();
        update();
        return;
    }
    for (int i = 0; i < planetHitAreas_.size(); ++i) {
        if (planetHitAreas_[i].contains(event->position())) {
            if (hoveredAspectIndex_ != -1) {
                hoveredAspectIndex_ = -1;
                update();
            }
            QToolTip::showText(event->globalPosition().toPoint(), planetTooltips_[i], this);
            return;
        }
    }
    const int aspectIdx = showAspects_ ? hitTestAspect(event->position()) : -1;
    if (aspectIdx != hoveredAspectIndex_) {
        hoveredAspectIndex_ = aspectIdx;
        update();
    }
    if (aspectIdx >= 0 && aspectIdx < aspectLines_.size()) {
        QToolTip::showText(event->globalPosition().toPoint(), aspectLines_[aspectIdx].tooltip, this);
        return;
    }
    QToolTip::hideText();
}

void ChartWheelWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        leftPressActive_ = true;
        pressPos_ = event->position();
    }
    const bool leftPan = (event->button() == Qt::LeftButton && zoom_ > 1.0);
    if (event->button() == Qt::MiddleButton || leftPan) {
        panning_ = true;
        lastPanPos_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ChartWheelWidget::mouseReleaseEvent(QMouseEvent* event) {
    const bool wasPanning = panning_;
    if ((event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton) && panning_) {
        panning_ = false;
        updateCursor();
    }
    if (event->button() == Qt::LeftButton && leftPressActive_) {
        leftPressActive_ = false;
        const QPointF delta = event->position() - pressPos_;
        const double moved = std::hypot(delta.x(), delta.y());
        if (moved <= 4.0) {
            // Treat as a click (not a pan drag): toggle click-to-focus.
            QString hitName;
            const int n = std::min(planetHitAreas_.size(), planetHitNames_.size());
            for (int i = 0; i < n; ++i) {
                if (!planetHitNames_[i].isEmpty() && planetHitAreas_[i].contains(event->position())) {
                    hitName = planetHitNames_[i];
                    break;
                }
            }
            if (!hitName.isEmpty()) {
                if (hasFocus_ && focusBody_ == hitName) {
                    hasFocus_ = false;
                    focusBody_.clear();
                } else {
                    hasFocus_ = true;
                    focusBody_ = hitName;
                }
            } else if (hasFocus_) {
                hasFocus_ = false;
                focusBody_.clear();
            }
            update();
            event->accept();
            return;
        }
    }
    if (wasPanning) {
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ChartWheelWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QAction* fitAction = menu.addAction("Fit to Screen");
    QAction* zoomInAction = menu.addAction("Zoom In");
    QAction* zoomOutAction = menu.addAction("Zoom Out");
    QAction* centerAction = menu.addAction("Center Chart");

    QAction* chosen = menu.exec(event->globalPos());
    if (!chosen) {
        return;
    }
    if (chosen == fitAction) {
        resetZoom();
    } else if (chosen == zoomInAction) {
        zoomIn();
    } else if (chosen == zoomOutAction) {
        zoomOut();
    } else if (chosen == centerAction) {
        panOffset_ = {0.0, 0.0};
        updateCursor();
        update();
    }
}

void ChartWheelWidget::leaveEvent(QEvent* event) {
    Q_UNUSED(event);
    if (hoveredAspectIndex_ != -1) {
        hoveredAspectIndex_ = -1;
        update();
    }
    QToolTip::hideText();
}

void ChartWheelWidget::updateCursor() {
    if (panning_) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (zoom_ > 1.0) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

QVector<ChartWheelWidget::PlacedBody> ChartWheelWidget::computePlanetPlacements(
    const QVector<BodyPosition>& bodies,
    double baseRadius,
    double minRadius,
    double maxRadius,
    double glyphSize,
    const QPointF& center) const
{
    // Filter and build initial list
    struct BodyDraw {
        QString name;
        double lon;
        bool retrograde;
        bool lockLongitude;
        LunarNodeType lunarNodeType;
    };
    QVector<BodyDraw> drawList;
    drawList.reserve(bodies.size());
    bool hasMeanLunarNodes = false;
    bool hasTrueLunarNodes = false;
    for (const auto& pos : bodies) {
        if (isAsteroidBody(pos.name) && (!showAsteroids_ || !isAsteroidVisible(pos.name)))
            continue;
        if (isArabicLotName(pos.name) && !showLots_)
            continue;
        if (pos.name == "Vertex" && !showDerivedPoints_)
            continue;
        drawList.push_back({pos.name, pos.longitude, pos.retrograde,
                            pos.isLunarNode, pos.lunarNodeType});
        if (pos.isLunarNode) {
            hasMeanLunarNodes = hasMeanLunarNodes || pos.lunarNodeType == LunarNodeType::Mean;
            hasTrueLunarNodes = hasTrueLunarNodes || pos.lunarNodeType == LunarNodeType::True;
        }
    }
    std::sort(drawList.begin(), drawList.end(), [](const BodyDraw& a, const BodyDraw& b) {
        return a.lon < b.lon;
    });

    // Initialize placements
    QVector<PlacedBody> result(drawList.size());
    for (int i = 0; i < drawList.size(); ++i) {
        result[i].name = drawList[i].name;
        result[i].trueLon = drawList[i].lon;
        result[i].displayLon = normalizeDegrees(drawList[i].lon);
        result[i].displayRadius = baseRadius;
        result[i].radialLayer = 0;
        result[i].retrograde = drawList[i].retrograde;
        result[i].lockLongitude = drawList[i].lockLongitude;
        result[i].lunarNodeType = drawList[i].lunarNodeType;
    }

    if (result.size() <= 1)
        return result;

    // --- Phase 1: Angular spreading (existing algorithm) ---
    const double minSpacingDeg = std::max(2.0, (glyphSize * 1.15 / baseRadius) * qRadiansToDegrees(1.0));
    const double clusterThreshold = minSpacingDeg * 1.1;

    QVector<double> lons(result.size());
    for (int i = 0; i < result.size(); ++i)
        lons[i] = result[i].displayLon;

    // Identify cluster breaks
    QVector<bool> breakAfter(result.size(), false);
    for (int i = 0; i < result.size() - 1; ++i) {
        if ((lons[i + 1] - lons[i]) > clusterThreshold)
            breakAfter[i] = true;
    }
    const double wrapGap = (lons.front() + 360.0) - lons.back();
    if (wrapGap > clusterThreshold)
        breakAfter[result.size() - 1] = true;

    int start = 0;
    for (int i = 0; i < breakAfter.size(); ++i) {
        if (breakAfter[i]) {
            start = (i + 1) % breakAfter.size();
            break;
        }
    }

    QVector<QVector<int>> clusters;
    QVector<int> current;
    current.reserve(result.size());
    for (int step = 0; step < result.size(); ++step) {
        const int idx = (start + step) % result.size();
        current.append(idx);
        if (breakAfter[idx] && step < result.size() - 1) {
            clusters.append(current);
            current.clear();
        }
    }
    if (!current.isEmpty())
        clusters.append(current);

    // Spread each cluster angularly
    for (const auto& cluster : clusters) {
        const int count = cluster.size();
        if (count <= 1)
            continue;

        QVector<double> unwrapped(count);
        double prev = lons[cluster[0]];
        unwrapped[0] = prev;
        for (int k = 1; k < count; ++k) {
            double lon = lons[cluster[k]];
            if (lon < prev) lon += 360.0;
            unwrapped[k] = lon;
            prev = lon;
        }

        QVector<double> placed = unwrapped;
        // Forward pass
        for (int k = 1; k < count; ++k) {
            if (placed[k] < placed[k - 1] + minSpacingDeg)
                placed[k] = placed[k - 1] + minSpacingDeg;
        }
        // Backward pass
        for (int k = count - 2; k >= 0; --k) {
            if (placed[k] > placed[k + 1] - minSpacingDeg)
                placed[k] = placed[k + 1] - minSpacingDeg;
        }
        // Center the group
        double deltaSum = 0.0;
        for (int k = 0; k < count; ++k)
            deltaSum += (unwrapped[k] - placed[k]);
        const double delta = deltaSum / static_cast<double>(count);
        for (int k = 0; k < count; ++k)
            placed[k] += delta;

        for (int k = 0; k < count; ++k)
            result[cluster[k]].displayLon = placed[k];
    }

    // Lunar nodes are calculated axes, not ordinary labels. Their glyphs must
    // remain tied to the exact ephemeris longitude while dates are stepped.
    // Let radial layering handle nearby bodies instead of allowing a node to
    // jump several degrees when a collision cluster changes membership.
    for (auto& item : result) {
        if (item.lockLongitude) {
            item.displayLon = normalizeDegrees(item.trueLon);
        }
    }

    // --- Phase 2: Pixel-space overlap detection ---
    const double pad = 3.0;
    const double halfG = glyphSize * 0.5;

    auto glyphRect = [&](int i) -> QRectF {
        const double angle = angleForLongitude(normalizeDegrees(result[i].displayLon));
        const QPointF p = pointOnCircle(center, result[i].displayRadius, angle);
        return QRectF(p.x() - halfG - pad, p.y() - halfG - pad,
                      glyphSize + 2.0 * pad, glyphSize + 2.0 * pad);
    };

    // Build collision groups using union-find
    QVector<int> parent(result.size());
    for (int i = 0; i < result.size(); ++i) parent[i] = i;

    auto findRoot = [&](int x) -> int {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    auto unite = [&](int a, int b) {
        a = findRoot(a); b = findRoot(b);
        if (a != b) parent[a] = b;
    };

    for (int i = 0; i < result.size(); ++i) {
        QRectF ri = glyphRect(i);
        for (int j = i + 1; j < result.size(); ++j) {
            QRectF rj = glyphRect(j);
            if (ri.intersects(rj))
                unite(i, j);
        }
    }

    // Collect collision groups with 2+ members
    QMap<int, QVector<int>> groupMap;
    for (int i = 0; i < result.size(); ++i) {
        int root = findRoot(i);
        groupMap[root].append(i);
    }

    // --- Phase 3: Radial layer assignment ---
    // Keep radial lanes far enough apart to help dense clusters, while still
    // fitting inside the deliberately narrow natal/transit bands.
    const double radialStep = glyphSize * 0.7;
    static constexpr int kMaxLayers = 2;

    for (auto it = groupMap.begin(); it != groupMap.end(); ++it) {
        QVector<int>& group = it.value();
        if (group.size() <= 1)
            continue;

        // Sort group by displayLon for consistent assignment
        std::sort(group.begin(), group.end(), [&](int a, int b) {
            return result[a].displayLon < result[b].displayLon;
        });

        const int n = group.size();
        // Assign layers: cycle through -1, 0, +1 centered approach
        // For N=2: [0, +1], N=3: [-1, 0, +1], N>=4: round-robin
        QVector<int> layerPattern;
        if (n == 2) {
            layerPattern = {0, 1};
        } else if (n == 3) {
            layerPattern = {-1, 0, 1};
        } else {
            // Round-robin: -1, 0, +1, -1, 0, +1, ...
            for (int k = 0; k < n; ++k) {
                int layer = (k % 3) - 1; // -1, 0, 1, -1, 0, 1, ...
                layerPattern.append(layer);
            }
        }

        for (int k = 0; k < n; ++k) {
            int layer = layerPattern[k];
            // Clamp to max layers
            layer = std::clamp(layer, -kMaxLayers, kMaxLayers);
            double newRadius = baseRadius + layer * radialStep;
            // Clamp to allowed range
            newRadius = std::clamp(newRadius, minRadius, maxRadius);
            result[group[k]].displayRadius = newRadius;
            result[group[k]].radialLayer = qRound((newRadius - baseRadius) / radialStep);
        }
    }

    // --- Phase 4: Per-radius angular re-check ---
    // Group by the actual post-clamp radius. This prevents nominal layers that
    // collapsed onto one radius from being treated as separate, non-colliding lanes.
    QMap<int, QVector<int>> radiusGroups;
    for (int i = 0; i < result.size(); ++i) {
        radiusGroups[qRound(result[i].displayRadius * 10.0)].append(i);
    }
    for (auto it = radiusGroups.begin(); it != radiusGroups.end(); ++it) {
        QVector<int> indices = it.value();
        if (indices.size() <= 1) {
            continue;
        }
        std::sort(indices.begin(), indices.end(), [&](int a, int b) {
            return normalizeDegrees(result[a].displayLon) < normalizeDegrees(result[b].displayLon);
        });

        // Start immediately after the largest circular gap so the unwrap seam
        // cannot split a cluster at 0/360 degrees.
        int startIndex = 0;
        double largestGap = -1.0;
        for (int k = 0; k < indices.size(); ++k) {
            const double here = normalizeDegrees(result[indices[k]].displayLon);
            const double next = normalizeDegrees(result[indices[(k + 1) % indices.size()]].displayLon);
            const double gap = (k + 1 < indices.size()) ? (next - here) : (next + 360.0 - here);
            if (gap > largestGap) {
                largestGap = gap;
                startIndex = (k + 1) % indices.size();
            }
        }

        QVector<int> ordered;
        QVector<double> unwrapped;
        ordered.reserve(indices.size());
        unwrapped.reserve(indices.size());
        for (int step = 0; step < indices.size(); ++step) {
            const int idx = indices[(startIndex + step) % indices.size()];
            double lon = normalizeDegrees(result[idx].displayLon);
            if (!unwrapped.isEmpty() && lon < unwrapped.back()) {
                lon += 360.0;
            }
            ordered.append(idx);
            unwrapped.append(lon);
        }

        double radiusSum = 0.0;
        for (const int idx : ordered) {
            radiusSum += result[idx].displayRadius;
        }
        const double actualRadius = radiusSum / static_cast<double>(ordered.size());
        const double minSpacing = std::max(
            2.0, (glyphSize * 1.15 / std::max(actualRadius, 1.0)) * qRadiansToDegrees(1.0));
        QVector<double> placed = unwrapped;
        for (int k = 1; k < placed.size(); ++k) {
            placed[k] = std::max(placed[k], placed[k - 1] + minSpacing);
        }
        double recenter = 0.0;
        for (int k = 0; k < placed.size(); ++k) {
            recenter += unwrapped[k] - placed[k];
        }
        recenter /= static_cast<double>(placed.size());
        for (int k = 0; k < placed.size(); ++k) {
            result[ordered[k]].displayLon = placed[k] + recenter;
        }
    }

    const bool showBothNodeModels = hasMeanLunarNodes && hasTrueLunarNodes;
    for (auto& item : result) {
        if (!item.lockLongitude) {
            continue;
        }
        item.displayLon = normalizeDegrees(item.trueLon);
        item.displayRadius = showBothNodeModels
            ? (item.lunarNodeType == LunarNodeType::True ? maxRadius : minRadius)
            : baseRadius;
        item.radialLayer = qRound((item.displayRadius - baseRadius)
                                  / std::max(glyphSize * 0.7, 1.0));
    }
    // Draw anchored node glyphs after ordinary bodies so a nearby low-priority
    // point cannot cover the node at its exact degree.
    std::stable_sort(result.begin(), result.end(), [](const PlacedBody& a, const PlacedBody& b) {
        return !a.lockLongitude && b.lockLongitude;
    });

    return result;
}

void ChartWheelWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), theme_.background);

    const QRectF bounds = rect().adjusted(12, 12, -12, -12);
    const double size = std::min(bounds.width(), bounds.height());
    const QPointF center(bounds.center().x() + panOffset_.x(), bounds.center().y() + panOffset_.y());

    // --- Astro.com-style geometry: planets OUTSIDE the zodiac ring ---
    // At the default zoom, reserve enough room for the planet lane and angle
    // degrees. User zoom is then applied on top, preserving intentional overflow
    // and panning instead of silently disabling magnification.
    // Degree labels need a real lane of their own beyond the body glyphs. The
    // previous fixed budget left that lane inside the glyph rectangles, so the
    // collision checker correctly rejected almost every body-degree label.
    const double outerDecorationBudget = showDegrees_
        ? 112.0 * fontScale_ + 12.0
        : 35.0 * fontScale_ + 34.0;
    const double fittedOuter = std::max(72.0, size * 0.5 - outerDecorationBudget);
    const double baseZodiacOuter = std::min(size * 0.42, fittedOuter);
    const double zodiacOuter = baseZodiacOuter * zoom_;    // outer edge of zodiac sign ring
    const double zodiacInner = zodiacOuter * 0.86;         // inner edge of zodiac sign ring
    const double houseOuter  = zodiacInner - 4.0;          // outer edge of house area
    const double houseInner  = zodiacOuter * 0.52;         // inner edge of house area
    const double aspectRadius = houseInner - 8.0;          // aspect line endpoints
    const double houseLabelRadius = (houseOuter + houseInner) * 0.5; // center of house area
    // Planet lane sits outside the zodiac ring
    const double planetLaneRadius = zodiacOuter + 18.0 * fontScale_; // center of planet glyph lane

    // Paint opaque zodiac fills before structural strokes and ticks so those
    // details remain visible rather than being covered later in the frame.
    if (hasChart_) {
        const QVector<QColor> elementColors = {
            theme_.elementFire,
            theme_.elementEarth,
            theme_.elementAir,
            theme_.elementWater,
        };
        painter.save();
        painter.setPen(Qt::NoPen);
        for (int s = 0; s < 12; ++s) {
            painter.setBrush(elementColors[elementIndexForSign(s)]);
            const double startAngle = angleForLongitude(s * 30.0);
            QPainterPath path;
            const QRectF outerRect(center.x() - zodiacOuter, center.y() - zodiacOuter,
                                   zodiacOuter * 2.0, zodiacOuter * 2.0);
            const QRectF innerRect(center.x() - zodiacInner, center.y() - zodiacInner,
                                   zodiacInner * 2.0, zodiacInner * 2.0);
            path.arcMoveTo(outerRect, startAngle);
            path.arcTo(outerRect, startAngle, 30.0);
            path.arcTo(innerRect, startAngle + 30.0, -30.0);
            path.closeSubpath();
            painter.drawPath(path);
        }
        painter.restore();
    }

    painter.save();
    // Zodiac outer ring - the main circle
    painter.setPen(QPen(theme_.ringOuter, 2.0));
    painter.drawEllipse(center, zodiacOuter, zodiacOuter);
    // Zodiac inner ring
    painter.setPen(QPen(theme_.ringZodiac, 1.5));
    painter.drawEllipse(center, zodiacInner, zodiacInner);
    // House outer boundary
    painter.setPen(QPen(theme_.ringHouseOuter, 1.0));
    painter.drawEllipse(center, houseOuter, houseOuter);
    // House inner boundary
    painter.setPen(QPen(theme_.ringHouseInner, 1.0));
    painter.drawEllipse(center, houseInner, houseInner);
    painter.restore();

    if (!hasChart_) {
        painter.setPen(theme_.placeholderText);
        const QRectF placeholderRect(center.x() - 120.0, center.y() - 20.0, 240.0, 40.0);
        painter.drawText(placeholderRect, Qt::AlignCenter, "Chart wheel placeholder");
        return;
    }

    // --- Chart info overlay (top-left corner) ---
    {
        painter.save();
        const int infoX = 10;
        int infoY = 8;

        // Date line - prominent
        QFont dateFont("Segoe UI");
        dateFont.setPointSizeF(10.0 * fontScale_);
        dateFont.setWeight(QFont::DemiBold);
        dateFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.3);
        painter.setFont(dateFont);
        QColor textColor = theme_.body;
        textColor.setAlpha(200);
        painter.setPen(textColor);

        const bool overlay = (mode_ == Mode::Overlay && hasOverlay_);
        const QDateTime& dt = chart_.localDateTime;
        if (dt.isValid()) {
            // Format: "4 Jan 1999, 4:12 PM" style
            const QString dateLine = dt.date().toString("d MMM yyyy");
            painter.drawText(infoX, infoY + painter.fontMetrics().ascent(), dateLine);
            infoY += painter.fontMetrics().height() + 1;

            // Time + timezone - secondary, lighter
            QFont timeFont("Segoe UI");
            timeFont.setPointSizeF(8.0 * fontScale_);
            timeFont.setWeight(QFont::Normal);
            painter.setFont(timeFont);
            QColor timeColor = theme_.body;
            timeColor.setAlpha(150);
            painter.setPen(timeColor);
            QString timeLine = dt.time().toString("h:mm AP");
            if (!chart_.timezoneLabel.isEmpty()) {
                timeLine += "  " + chart_.timezoneLabel;
            }
            painter.drawText(infoX, infoY + painter.fontMetrics().ascent(), timeLine);
            infoY += painter.fontMetrics().height() + 1;
        }

        // Optional context note (e.g., Solar Return profected house).
        if (!chartNote_.isEmpty()) {
            infoY += 2;
            QFont noteFont("Segoe UI");
            noteFont.setPointSizeF(8.0 * fontScale_);
            noteFont.setWeight(QFont::DemiBold);
            noteFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.2);
            painter.setFont(noteFont);
            QColor noteColor = theme_.angularHouseLabel;
            painter.setPen(noteColor);
            painter.drawText(infoX, infoY + painter.fontMetrics().ascent(), chartNote_);
            infoY += painter.fontMetrics().height() + 1;
        }

        // If overlay, show transit date on a second line
        if (overlay && overlayChart_.localDateTime.isValid()) {
            infoY += 3;
            QFont transitLabelFont("Segoe UI");
            transitLabelFont.setPointSizeF(7.0 * fontScale_);
            transitLabelFont.setWeight(QFont::Medium);
            transitLabelFont.setCapitalization(QFont::AllUppercase);
            transitLabelFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
            painter.setFont(transitLabelFont);
            QColor labelColor = theme_.transitBody;
            labelColor.setAlpha(140);
            painter.setPen(labelColor);
            const QString label = overlayLabel_.isEmpty() ? "TRANSIT" : overlayLabel_.toUpper();
            painter.drawText(infoX, infoY + painter.fontMetrics().ascent(), label);
            infoY += painter.fontMetrics().height() + 1;

            QFont transitDateFont("Segoe UI");
            transitDateFont.setPointSizeF(9.0 * fontScale_);
            transitDateFont.setWeight(QFont::DemiBold);
            painter.setFont(transitDateFont);
            QColor transitColor = theme_.transitBody;
            transitColor.setAlpha(190);
            painter.setPen(transitColor);
            const QDateTime& tdt = overlayChart_.localDateTime;
            const QString transitDate = tdt.date().toString("d MMM yyyy");
            painter.drawText(infoX, infoY + painter.fontMetrics().ascent(), transitDate);
            infoY += painter.fontMetrics().height() + 1;

            QFont transitTimeFont("Segoe UI");
            transitTimeFont.setPointSizeF(8.0 * fontScale_);
            painter.setFont(transitTimeFont);
            transitColor.setAlpha(140);
            painter.setPen(transitColor);
            QString transitTime = tdt.time().toString("h:mm AP");
            if (!overlayChart_.timezoneLabel.isEmpty()) {
                transitTime += "  " + overlayChart_.timezoneLabel;
            }
            painter.drawText(infoX, infoY + painter.fontMetrics().ascent(), transitTime);
        }

        painter.restore();
    }

    QFont smallFont("Segoe UI");
    smallFont.setPointSizeF(9.0 * fontScale_);
    QFont angleFont = smallFont;
    angleFont.setPointSizeF(smallFont.pointSizeF() + 2.0);
    angleFont.setBold(true);
    const double glyphSize = 20.0 * fontScale_;
    const double glyphHalf = glyphSize * 0.5;
    const double degWidth = 42.0 * fontScale_;
    const double degHeight = 14.0 * fontScale_;
    const double houseLabelSize = 18.0 * fontScale_;
    const double angleLabelWidth = 36.0 * fontScale_;
    const double angleLabelHeight = 20.0 * fontScale_;
    const double angleDegWidth = 46.0 * fontScale_;
    // Angle labels sit just beyond the planet lane.
    const double angleLabelRadius = planetLaneRadius + glyphHalf + 14.0;

    // Axis-aligned text rectangles need different radial clearances at
    // different points around the wheel: their width is radial at AC/DC while
    // their height is radial at MC/IC.
    auto radialHalfExtent = [](double width, double height, double angleDeg) {
        const double radians = qDegreesToRadians(angleDeg);
        return 0.5 * (std::fabs(std::cos(radians)) * width
                      + std::fabs(std::sin(radians)) * height);
    };
    auto angleDegreeRadiusFor = [&](double angleDeg) {
        const double nameExtent = radialHalfExtent(
            angleLabelWidth, angleLabelHeight, angleDeg);
        const double degreeExtent = radialHalfExtent(
            angleDegWidth, degHeight, angleDeg);
        return angleLabelRadius + nameExtent + degreeExtent
            + 5.0 * fontScale_;
    };

    // Degree ticks on the inner edge of the zodiac ring (Astro.com style).
    if (showTicks_) {
        painter.setPen(QPen(theme_.tick, 0.8));
        int stepDeg = 1;
        if (tickDensity_ == TickDensity::Medium) {
            stepDeg = 2;
        } else if (tickDensity_ == TickDensity::Minimal) {
            stepDeg = 5;
        }
        for (int deg = 0; deg < 360; deg += stepDeg) {
            const double angle = angleForLongitude(deg);
            const bool major = (deg % 10 == 0);
            const bool medium = (deg % 5 == 0);
            double tickOuter = zodiacInner + 5.0;
            if (major) {
                tickOuter = zodiacInner + 9.0;
            } else if (medium) {
                tickOuter = zodiacInner + 7.0;
            }
            const QPointF p1 = pointOnCircle(center, zodiacInner, angle);
            const QPointF p2 = pointOnCircle(center, tickOuter, angle);
            painter.drawLine(p1, p2);
        }
    }

    // Sign boundaries + glyphs.
    painter.setPen(QPen(theme_.signBoundary, 1.5));
    // Resource paths for zodiac icons
    static const QStringList svgPaths = {
        ":/resources/icons/zodiac/aries.svg",
        ":/resources/icons/zodiac/taurus.svg",
        ":/resources/icons/zodiac/gemini.svg",
        ":/resources/icons/zodiac/cancer.svg",
        ":/resources/icons/zodiac/leo.svg",
        ":/resources/icons/zodiac/virgo.svg",
        ":/resources/icons/zodiac/libra.svg",
        ":/resources/icons/zodiac/scorpio.svg",
        ":/resources/icons/zodiac/sagittarius.svg",
        ":/resources/icons/zodiac/capricorn.svg",
        ":/resources/icons/zodiac/aquarius.svg",
        ":/resources/icons/zodiac/pisces.svg"
    };

    for (int s = 0; s < 12; ++s) {
        const double lon = s * 30.0;
        const double angle = angleForLongitude(lon);
        const QPointF p1 = pointOnCircle(center, zodiacOuter, angle);
        const QPointF p2 = pointOnCircle(center, zodiacInner, angle);
        painter.drawLine(p1, p2);

        // Draw sign glyph centered in segment using SVG
        const double midLon = lon + 15.0;
        const QPointF pos = pointOnCircle(center, (zodiacOuter + zodiacInner) * 0.5, angleForLongitude(midLon));
        
        const double iconSize = std::clamp((zodiacOuter - zodiacInner) * 0.70,
                                           18.0 * fontScale_, 32.0 * fontScale_);
        QRectF iconRect(pos.x() - iconSize * 0.5, pos.y() - iconSize * 0.5,
                        iconSize, iconSize);
        
        if (s < svgPaths.size()) {
            const QPixmap px = coloredSvgPixmap(svgPaths[s], theme_.signGlyph,
                                                iconRect.size().toSize(),
                                                painter.device()->devicePixelRatio());
            painter.drawPixmap(iconRect.topLeft(), px);
        }
    }

    // House lines + numbers.
    const QVector<double> cusps = buildHouseCusps();
    const double houseLineWidth = 1.6 * fontScale_;
    painter.setPen(QPen(theme_.houseLine, houseLineWidth, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < cusps.size(); ++i) {
        const double angle = angleForLongitude(cusps[i]);
        const QPointF p1 = pointOnCircle(center, houseOuter, angle);
        const QPointF p2 = pointOnCircle(center, houseInner, angle);
        painter.drawLine(p1, p2);
    }

    const QVector<QColor> angleColors = {
        QColor("#e05555"),  // AC
        QColor("#e0b84a"),  // MC
        QColor("#4aa3ff"),  // DC
        QColor("#53b987"),  // IC
    };
    const QVector<double> angleLons = {
        chart_.angles.asc,
        chart_.angles.mc,
        chart_.angles.desc,
        chart_.angles.ic,
    };
    // Draw angle axes with thicker lines extending through zodiac ring (Astro.com style)
    for (int i = 0; i < angleLons.size(); ++i) {
        const double angleVal = angleForLongitude(angleLons[i]);
        // Inner portion through house area
        painter.setPen(QPen(angleColors[i], 2.2 * fontScale_));
        const QPointF outerPt = pointOnCircle(center, zodiacOuter, angleVal);
        const QPointF innerPt = pointOnCircle(center, houseInner, angleVal);
        painter.drawLine(outerPt, innerPt);
    }

    // Draw house numbers with angular house emphasis
    painter.setFont(smallFont);
    QVector<QRectF> houseLabelRects;
    houseLabelRects.reserve(12);
    for (int i = 0; i < cusps.size(); ++i) {
        const double a1 = angleForLongitude(cusps[i]);
        const double a2 = angleForLongitude(cusps[(i + 1) % cusps.size()]);
        double delta = a2 - a1;
        if (delta < 0) {
            delta += 360.0;
        }
        const double mid = a1 + delta * 0.5;
        const QPointF pos = pointOnCircle(center, houseLabelRadius, mid);
        
        // Angular houses (1, 4, 7, 10) get special emphasis
        const int houseNum = i + 1;
        const bool isAngular = (houseNum == 1 || houseNum == 4 || houseNum == 7 || houseNum == 10);
        if (isAngular) {
            painter.setPen(theme_.angularHouseLabel);
            QFont angularFont = smallFont;
            angularFont.setBold(true);
            painter.setFont(angularFont);
        } else {
            painter.setPen(theme_.houseLabel);
            painter.setFont(smallFont);
        }
        const QRectF houseRect(pos.x() - houseLabelSize * 0.5, pos.y() - houseLabelSize * 0.5,
                               houseLabelSize, houseLabelSize);
        painter.drawText(houseRect, Qt::AlignCenter, QString::number(houseNum));
        houseLabelRects.append(houseRect.adjusted(-3, -3, 3, 3));
    }

    const bool overlay = (mode_ == Mode::Overlay && hasOverlay_);
    const QString overlayPrefix = overlayLabel_.isEmpty() ? QString("Transit") : overlayLabel_;
    if (overlay) {
        // Subtle, intentional background lane for the outer (transit) ring, with
        // thin crisp edges so the two chart layers read as distinct rings.
        const double bandInner = zodiacOuter + 1.0;
        const double bandOuter = planetLaneRadius + glyphHalf + 8.0 * fontScale_;
        QPainterPath band;
        band.addEllipse(center, bandOuter, bandOuter);
        band.addEllipse(center, bandInner, bandInner);
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme_.transitLaneBand);
        painter.drawPath(band);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(theme_.ringOuter, 1.0));
        painter.drawEllipse(center, bandOuter, bandOuter);
        painter.drawEllipse(center, bandInner, bandInner);
        painter.restore();
    }

    // Aspect lines.
    aspectLines_.clear();
    if (showAspects_) {
        QVector<QRectF> aspectSymbolRects;
        aspectSymbolRects.reserve(64);
        const QString natalScope = QStringLiteral("Natal ");
        const QString transitScope = QStringLiteral("Transit ");
        const QString overlayScope = overlayPrefix + " ";
        auto shouldIncludeOrb = [this](double orb) {
            return aspectDisplayMaxOrb_ <= 0.0 || orb <= aspectDisplayMaxOrb_;
        };
        auto appendAspectLine = [&](double lonA, double lonB,
                                    const QString& nameA, const QString& nameB,
                                    const QString& label, double orb, double maxOrb,
                                    Qt::PenStyle style, double opacityScale,
                                    const QString& tooltipPrefix) {
            if (!shouldIncludeOrb(orb)) {
                return;
            }
            // Colour encodes aspect TYPE; line style (passed in) encodes SCOPE.
            const QColor lineColor = aspectTypeColor(label);
            // Strip scope prefixes to recover the raw body name for click-to-focus.
            auto stripPrefix = [&](const QString& n) -> QString {
                if (n.startsWith(natalScope)) return n.mid(natalScope.size());
                if (n.startsWith(transitScope)) return n.mid(transitScope.size());
                if (!overlayScope.trimmed().isEmpty() && n.startsWith(overlayScope)) {
                    return n.mid(overlayScope.size());
                }
                return n;
            };
            const QString rawA = stripPrefix(nameA);
            const QString rawB = stripPrefix(nameB);
            auto displayScopedName = [&](const QString& scopedName) -> QString {
                QString prefix;
                const LunarNodePolicy* policy = &chart_.lunarNodePolicy;
                QString rawName = scopedName;
                if (scopedName.startsWith(natalScope)) {
                    prefix = natalScope;
                    rawName = scopedName.mid(natalScope.size());
                    policy = &chart_.lunarNodePolicy;
                } else if (!overlayScope.trimmed().isEmpty() && scopedName.startsWith(overlayScope)) {
                    prefix = overlayScope;
                    rawName = scopedName.mid(overlayScope.size());
                    policy = hasOverlay_ ? &overlayChart_.lunarNodePolicy : &chart_.lunarNodePolicy;
                } else if (scopedName.startsWith(transitScope)) {
                    prefix = transitScope;
                    rawName = scopedName.mid(transitScope.size());
                    policy = hasOverlay_ ? &overlayChart_.lunarNodePolicy : &chart_.lunarNodePolicy;
                }
                return prefix + lunarNodeDisplayName(rawName, *policy);
            };
            // Suppress structurally redundant aspect lines within a single chart:
            //   - North Node / South Node are always exactly opposite.
            //   - Chart angles (AC/MC/DC/IC) are fixed relative to each other.
            // Detect "same chart" by comparing scope prefixes so a genuine
            // transit-node-to-natal-node aspect is still drawn in overlays.
            auto scopeOf = [&](const QString& n) -> QString {
                if (n.startsWith(natalScope)) return natalScope;
                if (n.startsWith(transitScope)) return transitScope;
                if (!overlayScope.trimmed().isEmpty() && n.startsWith(overlayScope)) return overlayScope;
                return QString();
            };
            const bool sameChart = (scopeOf(nameA) == scopeOf(nameB));
            if (sameChart
                && (isLunarNodePair(rawA, rawB)
                    || (isChartAngleName(rawA) && isChartAngleName(rawB)))) {
                return;
            }
            const QPointF p1 = pointOnCircle(center, aspectRadius, angleForLongitude(lonA));
            const QPointF p2 = pointOnCircle(center, aspectRadius, angleForLongitude(lonB));
            const QLineF line(p1, p2);
            const QPointF chord = p2 - p1;
            const double chordLength = std::hypot(chord.x(), chord.y());
            const QPointF midpoint = (p1 + p2) * 0.5;
            const double relativeLength = std::clamp(
                chordLength / std::max(2.0 * aspectRadius, 1.0), 0.0, 1.0);
            const double bendFactor = 0.08 + 0.22 * relativeLength;
            const QPointF control = midpoint + (center - midpoint) * bendFactor;
            QPainterPath path(p1);
            path.quadTo(control, p2);
            double strength = 0.5;
            if (maxOrb > 0.0) {
                strength = 1.0 - (orb / maxOrb);
            }
            strength = std::clamp(strength, 0.0, 1.0);
            double baseOpacity = std::max(0.12, (0.24 + 0.56 * strength) * opacityScale);
            const double baseWidth = 0.6 + (1.2 * strength);
            QString tooltip = QString("%1%2 - %3 - %4 (orb %5 deg)")
                .arg(tooltipPrefix)
                .arg(displayScopedName(nameA))
                .arg(label)
                .arg(displayScopedName(nameB))
                .arg(QString::number(orb, 'f', 2));
            if (maxOrb > 0.0) {
                tooltip += QString(", max %1 deg").arg(QString::number(maxOrb, 'f', 1));
            }
            QPointF labelPos = (p1 + control * 2.0 + p2) * 0.25;
            QPointF tangent = p2 - p1;
            const double tangentLength = std::hypot(tangent.x(), tangent.y());
            QPointF perpendicular;
            if (tangentLength > 1.0) {
                tangent /= tangentLength;
                perpendicular = QPointF(-tangent.y(), tangent.x());
                labelPos += perpendicular * (4.0 * fontScale_);
            }
            const double symbolSize = std::clamp(16.0 * fontScale_, 14.0, 24.0);
            QRectF symbolRect;
            const QVector<QPointF> symbolOffsets = {
                QPointF(), perpendicular * symbolSize, perpendicular * -symbolSize,
                tangent * symbolSize, tangent * -symbolSize,
            };
            for (const QPointF& offset : symbolOffsets) {
                const QPointF candidate = labelPos + offset;
                const QRectF candidateRect(candidate.x() - symbolSize * 0.5,
                                           candidate.y() - symbolSize * 0.5,
                                           symbolSize, symbolSize);
                bool overlaps = false;
                for (const QRectF& used : aspectSymbolRects) {
                    if (candidateRect.adjusted(-2, -2, 2, 2).intersects(used)) {
                        overlaps = true;
                        break;
                    }
                }
                if (!overlaps) {
                    symbolRect = candidateRect;
                    aspectSymbolRects.append(candidateRect.adjusted(-2, -2, 2, 2));
                    break;
                }
            }
            aspectLines_.push_back(AspectLineInfo{
                line,
                path,
                symbolRect,
                tooltip,
                lineColor,
                baseOpacity,
                baseWidth,
                aspectSymbolForLabel(label),
                style,
                rawA,
                rawB,
            });
        };

        if (overlay) {
            auto isHiddenAspectBody = [&](const QString& name) {
                if (isAsteroidBody(name) && (!includeAsteroidAspects_ || !isAsteroidVisible(name))) {
                    return true;
                }
                if (isArabicLotName(name) && !showLots_) {
                    return true;
                }
                if (name == "Vertex" && !showDerivedPoints_) {
                    return true;
                }
                return false;
            };
            auto skipHiddenAspect = [&](const QString& aName, const QString& bName) {
                return isHiddenAspectBody(aName) || isHiddenAspectBody(bName);
            };
            struct NamedPoint {
                QString name;
                double lon;
            };
            QVector<NamedPoint> natalPoints;
            natalPoints.reserve(chart_.bodies.size() + 4);
            for (const auto& pos : chart_.bodies) {
                natalPoints.push_back({pos.name, pos.longitude});
            }
            natalPoints.push_back({"Ascendant", chart_.angles.asc});
            natalPoints.push_back({"Midheaven", chart_.angles.mc});
            natalPoints.push_back({"Descendant", chart_.angles.desc});
            natalPoints.push_back({"IC", chart_.angles.ic});

            QVector<NamedPoint> transitPoints;
            transitPoints.reserve(overlayChart_.bodies.size());
            for (const auto& pos : overlayChart_.bodies) {
                transitPoints.push_back({pos.name, pos.longitude});
            }

            if (overlayTransitNatalAspects_) {
                for (const auto& t : transitPoints) {
                    for (const auto& n : natalPoints) {
                        if (skipHiddenAspect(t.name, n.name)) {
                            continue;
                        }
                        const double diff = angularDiff(t.lon, n.lon);
                        QString label;
                        double orb = 0.0;
                        double maxOrb = 0.0;
                        if (!aspectFor(diff, aspectOrbs_, &label, &orb, &maxOrb)) {
                            continue;
                        }
                        appendAspectLine(t.lon, n.lon, overlayPrefix + " " + t.name, "Natal " + n.name,
                                         label, orb, maxOrb, Qt::SolidLine, 1.0, "");
                    }
                }
            }
            if (overlayTransitTransitAspects_) {
                for (int i = 0; i < transitPoints.size(); ++i) {
                    for (int j = i + 1; j < transitPoints.size(); ++j) {
                        const auto& a = transitPoints[i];
                        const auto& b = transitPoints[j];
                        if (skipHiddenAspect(a.name, b.name)) {
                            continue;
                        }
                        const double diff = angularDiff(a.lon, b.lon);
                        QString label;
                        double orb = 0.0;
                        double maxOrb = 0.0;
                        if (!aspectFor(diff, aspectOrbs_, &label, &orb, &maxOrb)) {
                            continue;
                        }
                        appendAspectLine(a.lon, b.lon, overlayPrefix + " " + a.name, overlayPrefix + " " + b.name,
                                         label, orb, maxOrb, Qt::DashLine, 0.65, "");
                    }
                }
            }
            if (overlayNatalNatalAspects_) {
                QMap<QString, double> bodyMap;
                for (const auto& pos : chart_.bodies) {
                    bodyMap.insert(pos.name, pos.longitude);
                }
                bodyMap.insert("Ascendant", chart_.angles.asc);
                bodyMap.insert("Midheaven", chart_.angles.mc);
                bodyMap.insert("Descendant", chart_.angles.desc);
                bodyMap.insert("IC", chart_.angles.ic);
                for (int i = 0; i < chart_.aspects.bodyOrder.size(); ++i) {
                    for (int j = i + 1; j < chart_.aspects.bodyOrder.size(); ++j) {
                        if (i >= chart_.aspects.cells.size()
                            || j >= chart_.aspects.cells[i].size()) {
                            continue;
                        }
                        const auto& cell = chart_.aspects.cells[i][j];
                        if (!cell.hasAspect) {
                            continue;
                        }
                        const QString& aName = chart_.aspects.bodyOrder[i];
                        const QString& bName = chart_.aspects.bodyOrder[j];
                        if (!bodyMap.contains(aName) || !bodyMap.contains(bName)) {
                            continue;
                        }
                        if (skipHiddenAspect(aName, bName)) {
                            continue;
                        }
                        appendAspectLine(bodyMap.value(aName), bodyMap.value(bName),
                                         "Natal " + aName, "Natal " + bName,
                                         cell.label, cell.orb, cell.maxOrb, Qt::DotLine, 0.45, "");
                    }
                }
            }
        } else {
            auto isHiddenAspectBody = [&](const QString& name) {
                if (isAsteroidBody(name) && (!includeAsteroidAspects_ || !isAsteroidVisible(name))) {
                    return true;
                }
                if (isArabicLotName(name) && !showLots_) {
                    return true;
                }
                if (name == "Vertex" && !showDerivedPoints_) {
                    return true;
                }
                return false;
            };
            auto skipHiddenAspect = [&](const QString& aName, const QString& bName) {
                return isHiddenAspectBody(aName) || isHiddenAspectBody(bName);
            };
            QMap<QString, double> bodyMap;
            for (const auto& pos : chart_.bodies) {
                bodyMap.insert(pos.name, pos.longitude);
            }
            bodyMap.insert("Ascendant", chart_.angles.asc);
            bodyMap.insert("Midheaven", chart_.angles.mc);
            bodyMap.insert("Descendant", chart_.angles.desc);
            bodyMap.insert("IC", chart_.angles.ic);

            for (int i = 0; i < chart_.aspects.bodyOrder.size(); ++i) {
                for (int j = i + 1; j < chart_.aspects.bodyOrder.size(); ++j) {
                    if (i >= chart_.aspects.cells.size()
                        || j >= chart_.aspects.cells[i].size()) {
                        continue;
                    }
                    const auto& cell = chart_.aspects.cells[i][j];
                    if (!cell.hasAspect) {
                        continue;
                    }
                    const QString& aName = chart_.aspects.bodyOrder[i];
                    const QString& bName = chart_.aspects.bodyOrder[j];
                    if (!bodyMap.contains(aName) || !bodyMap.contains(bName)) {
                        continue;
                    }
                    if (skipHiddenAspect(aName, bName)) {
                        continue;
                    }
                    appendAspectLine(bodyMap.value(aName), bodyMap.value(bName), aName, bName,
                                     cell.label, cell.orb, cell.maxOrb, Qt::SolidLine, 1.0, "");
                }
            }
        }

        if (hoveredAspectIndex_ >= aspectLines_.size()) {
            hoveredAspectIndex_ = -1;
        }
        const bool hasHover = hoveredAspectIndex_ >= 0 && hoveredAspectIndex_ < aspectLines_.size();
        // Focus is only "active" this frame if at least one drawn aspect touches
        // the focused body; this gracefully ignores a stale focus after the chart
        // changes (otherwise every line would dim).
        bool focusActive = false;
        if (hasFocus_ && !focusBody_.isEmpty()) {
            for (const auto& info : aspectLines_) {
                if (info.rawNameA == focusBody_ || info.rawNameB == focusBody_) {
                    focusActive = true;
                    break;
                }
            }
        }
        QFont aspectFont("Segoe UI Symbol");
        aspectFont.setPointSizeF(8.0 * fontScale_);
        aspectFont.setBold(true);
        const bool denseAspectField = aspectLines_.size() > 60;
        for (int i = 0; i < aspectLines_.size(); ++i) {
            const auto& info = aspectLines_[i];
            const bool isHover = (i == hoveredAspectIndex_);
            const bool involvesFocus = focusActive
                && (info.rawNameA == focusBody_ || info.rawNameB == focusBody_);
            double opacity = info.baseOpacity;
            double width = info.baseWidth;
            // Click-to-focus: in focus mode show ONLY the lines touching the
            // focused body (others are hidden, not just dimmed, so hover/hit-test
            // never picks up unrelated aspects).
            if (focusActive && !involvesFocus) {
                continue;
            }
            if (focusActive && involvesFocus) {
                opacity = std::min(1.0, opacity + 0.30);
                width += 0.6;
            }
            // Hover still emphasises a single line on top of any focus state.
            if (hasHover && !isHover) {
                opacity *= 0.25;
            } else if (hasHover && isHover) {
                opacity = std::min(1.0, opacity + 0.35);
                width += 0.8;
            }
            painter.save();
            painter.setOpacity(opacity);
            QPen pen(info.color, width);
            pen.setStyle(info.style);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(info.path);
            painter.restore();

            const bool focusSuppressed = focusActive && !involvesFocus;
            const bool showSymbol = !info.symbol.isEmpty() && !info.symbolRect.isEmpty()
                && !focusSuppressed
                && (isHover || (showAspectSymbols_ && (!denseAspectField || focusActive)));
            if (showSymbol) {
                painter.save();
                const double symbolOpacity = showAspectSymbols_
                    ? (hasHover && !isHover ? 0.35 : 0.85)
                    : (isHover ? 0.95 : 0.0);
                painter.setOpacity(symbolOpacity);
                painter.setBrush(theme_.aspectSymbolBg);
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(info.symbolRect.adjusted(-1, -1, 1, 1));
                painter.setFont(aspectFont);
                painter.setPen(info.color);
                painter.drawText(info.symbolRect, Qt::AlignCenter, info.symbol);
                painter.restore();
            }
        }
    }

    // Planet glyphs + degrees.
    planetHitAreas_.clear();
    planetTooltips_.clear();
    planetHitNames_.clear();
    QVector<QRectF> occupiedRects = houseLabelRects;
    occupiedRects.reserve(128);

    QFont degreeFont = smallFont;
    degreeFont.setBold(true);
    degreeFont.setPointSizeF(smallFont.pointSizeF() + 0.5);

    auto placeRadialRect = [&](double angleDeg, double baseRad, double width, double height,
                               double minRad, double maxRad, int preferredDirection) {
        const double step = 6.0 * fontScale_;
        const int direction = preferredDirection >= 0 ? 1 : -1;
        const QVector<double> angleOffsets = {
            0.0, 2.5, -2.5, 5.0, -5.0, 7.5, -7.5,
        };
        for (int attempt = 0; attempt < 15; ++attempt) {
            const int distanceStep = (attempt + 1) / 2;
            const int attemptDirection = (attempt == 0)
                ? 0
                : ((attempt % 2 == 1) ? direction : -direction);
            const double r = std::clamp(baseRad + attemptDirection * distanceStep * step,
                                        minRad, maxRad);
            for (double angleOffset : angleOffsets) {
                const QPointF p = pointOnCircle(center, r, angleDeg + angleOffset);
                const QRectF rect(p.x() - width * 0.5, p.y() - height * 0.5,
                                  width, height);
                bool hit = false;
                for (const auto& occ : occupiedRects) {
                    if (rect.intersects(occ)) {
                        hit = true;
                        break;
                    }
                }
                if (!hit) {
                    return rect;
                }
            }
        }
        return QRectF();
    };

    // Reserve angle label/degree areas so planet degree labels avoid them.
    for (int i = 0; i < angleLons.size(); ++i) {
        const double angleLon = angleLons[i];
        const QPointF labelPos = pointOnCircle(center, angleLabelRadius, angleForLongitude(angleLon));
        const QRectF labelRect(labelPos.x() - angleLabelWidth * 0.5, labelPos.y() - angleLabelHeight * 0.5,
                               angleLabelWidth, angleLabelHeight);
        occupiedRects.push_back(labelRect.adjusted(-6, -4, 6, 4));
        if (showDegrees_) {
            const double angle = angleForLongitude(angleLon);
            const double angleDegRadius = angleDegreeRadiusFor(angle);
            const QPointF degPos = pointOnCircle(center, angleDegRadius, angle);
            const QRectF degRect(degPos.x() - angleDegWidth * 0.5,
                                 degPos.y() - degHeight * 0.5,
                                 angleDegWidth, degHeight);
            occupiedRects.push_back(degRect.adjusted(-6, -4, 6, 4));
        }
    }

    // Helper lambdas for planet rendering
    auto getPlanetChar = [](const QString& name) -> QChar {
        static const QHash<QString, QChar> characters = {
            {"Sun", QChar(0x2609)}, {"Moon", QChar(0x263E)},
            {"Mercury", QChar(0x263F)}, {"Venus", QChar(0x2640)},
            {"Mars", QChar(0x2642)}, {"Jupiter", QChar(0x2643)},
            {"Saturn", QChar(0x2644)}, {"Uranus", QChar(0x26E2)},
            {"Neptune", QChar(0x2646)}, {"Pluto", QChar(0x2647)},
            {"Chiron", QChar(0x26B7)}, {"Ceres", QChar(0x26B3)},
            {"Pallas", QChar(0x26B4)}, {"Juno", QChar(0x26B5)},
            {"Vesta", QChar(0x26B6)}, {"North Node", QChar(0x260A)},
            {"South Node", QChar(0x260B)}, {"Lilith", QChar(0x26B8)},
            {"Part of Fortune", QChar(0x2297)},
        };
        const QString lookupName = isLunarNodeName(name)
            ? (isNorthLunarNodeName(name) ? QString("North Node") : QString("South Node"))
            : name;
        return characters.value(lookupName);
    };

    auto planetSvgPath = [](const QString& name) -> QString {
        return bodySvgResourcePath(name);
    };

    auto drawPlacedBodies = [&](const QVector<PlacedBody>& placements, double tickTargetRadius,
                                const QColor& color, const QString& prefix, bool isTransit,
                                const LunarNodePolicy& nodePolicy) {
        QFont planetFont("Segoe UI Symbol");
        planetFont.setPointSizeF(12.0 * fontScale_);
        painter.setFont(planetFont);

        // Reserve every glyph before placing any degree label. Otherwise an
        // early label can occupy the space needed by a later planet glyph.
        for (const auto& item : placements) {
            const double angle = angleForLongitude(normalizeDegrees(item.displayLon));
            const QPointF glyphPos = pointOnCircle(center, item.displayRadius, angle);
            const QRectF reservedGlyph(glyphPos.x() - glyphSize * 0.5,
                                       glyphPos.y() - glyphSize * 0.5,
                                       glyphSize, glyphSize);
            occupiedRects.push_back(reservedGlyph.adjusted(-2, -2, 2, 2));
        }

        for (int i = 0; i < placements.size(); ++i) {
            const auto& item = placements[i];
            const double displayAngle = angleForLongitude(normalizeDegrees(item.displayLon));
            const double trueAngle = angleForLongitude(normalizeDegrees(item.trueLon));
            const QPointF pos = pointOnCircle(center, item.displayRadius, displayAngle);
            const QString degLabel = formatDegShort(item.trueLon);

            QString glyph = bodyGlyph(item.name);
            if (glyph.trimmed().isEmpty() || glyph == "?") {
                QChar pChar = getPlanetChar(item.name);
                glyph = pChar.isNull() ? item.name.left(2) : QString(pChar);
            }

            const double gs = glyphSize;
            QRectF glyphR(pos.x() - gs / 2, pos.y() - gs / 2, gs, gs);

            QColor bodyColor = color;

            // --- Exact-degree marker on the ring + leader to a displaced glyph ---
            {
                const double dir = (item.displayRadius >= tickTargetRadius) ? 1.0 : -1.0;
                const QPointF ringPt = pointOnCircle(center, tickTargetRadius, trueAngle);
                const QPointF stubPt = pointOnCircle(center, tickTargetRadius + dir * 5.0 * fontScale_, trueAngle);
                double glyphEdgeR = item.displayRadius - dir * (gs * 0.5 + 1.0);
                if (dir > 0.0) {
                    glyphEdgeR = std::max(glyphEdgeR, tickTargetRadius + 5.0 * fontScale_);
                } else {
                    glyphEdgeR = std::min(glyphEdgeR, tickTargetRadius - 5.0 * fontScale_);
                }
                const QPointF glyphPt = pointOnCircle(center, glyphEdgeR, displayAngle);
                const bool displaced = (angularDiff(item.displayLon, item.trueLon) > 0.4)
                    || (item.radialLayer != 0);

                painter.save();
                // Always draw a crisp short tick at the body's exact degree.
                QColor markColor = bodyColor;
                markColor.setAlpha(230);
                painter.setPen(QPen(markColor, 1.3, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(ringPt, stubPt);
                // Only when the glyph has been moved to avoid overlap: a thin, faint
                // leader from the exact-degree tick to the glyph. Keeping it thin and
                // only-when-needed keeps dense clusters legible.
                if (displaced && QLineF(stubPt, glyphPt).length() >= 4.0 * fontScale_) {
                    QColor leadColor = bodyColor;
                    leadColor.setAlpha(115);
                    painter.setPen(QPen(leadColor, 0.8, Qt::SolidLine, Qt::RoundCap));
                    painter.drawLine(stubPt, glyphPt);
                }
                painter.restore();
            }

            // Highlight ring
            if (hasHighlight_ && highlightTransit_ == isTransit && highlightBody_ == item.name) {
                painter.save();
                QPen ringPen(highlightColor_, 2.0);
                painter.setPen(ringPen);
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(glyphR.adjusted(-4, -4, 4, 4));
                painter.restore();
            }

            // Draw SVG icon if available, otherwise fallback to Unicode glyph/text.
            bool drewSvg = false;
            const QString svgPath = planetSvgPath(item.name);
            if (!svgPath.isEmpty()) {
                const QPixmap px = coloredSvgPixmap(svgPath, bodyColor, glyphR.size().toSize(),
                                                    painter.device()->devicePixelRatio());
                painter.drawPixmap(glyphR.topLeft(), px);
                drewSvg = true;
            }
            if (!drewSvg) {
                painter.setPen(bodyColor);
                painter.drawText(glyphR, Qt::AlignCenter, glyph);
            }
            if (isLunarNodeName(item.name)) {
                painter.save();
                QFont nodeModelFont = smallFont;
                nodeModelFont.setPointSizeF(std::max(6.0, smallFont.pointSizeF() * 0.68));
                nodeModelFont.setBold(true);
                painter.setFont(nodeModelFont);
                painter.setPen(bodyColor);
                const LunarNodeType nodeType = lunarNodeTypeForName(
                    item.name, effectivePrimaryNodeType(nodePolicy));
                const QRectF modelRect(glyphR.right() - 1.0 * fontScale_,
                                       glyphR.bottom() - 7.0 * fontScale_,
                                       9.0 * fontScale_, 9.0 * fontScale_);
                painter.drawText(modelRect, Qt::AlignCenter,
                                 nodeType == LunarNodeType::True ? "T" : "M");
                painter.restore();
                occupiedRects.push_back(modelRect.adjusted(-1, -1, 1, 1));
            }
            // Draw Retrograde indicator (℞) as superscript next to glyph
            if (item.retrograde) {
                painter.save();
                QFont retroFont = smallFont;
                retroFont.setPointSizeF(smallFont.pointSizeF() * 0.75);
                retroFont.setBold(true);
                painter.setFont(retroFont);
                painter.setPen(theme_.retrogradeIndicator);
                const double retroSize = 10.0 * fontScale_;
                QRectF retroRect(glyphR.right() - 2.0 * fontScale_,
                                 glyphR.top() - 2.0 * fontScale_,
                                 retroSize, retroSize);
                painter.drawText(retroRect, Qt::AlignCenter, QString::fromUtf8(u8"℞"));
                painter.restore();
                occupiedRects.push_back(retroRect.adjusted(-1, -1, 1, 1));
            }

            // Degree label placed just outside the glyph along the radial direction
            if (showDegrees_) {
                painter.save();
                painter.setFont(degreeFont);
                const bool outsideWheel = item.displayRadius >= zodiacOuter;
                const int labelDirection = outsideWheel ? 1 : -1;
                const double labelExtent = radialHalfExtent(
                    degWidth, degHeight, displayAngle);
                const double degRad = item.displayRadius
                    + labelDirection * (glyphHalf + labelExtent
                                        + 3.0 * fontScale_);
                const double minLabelRadius = outsideWheel
                    ? zodiacOuter + labelExtent + 2.0 * fontScale_
                    : houseInner + labelExtent + 2.0 * fontScale_;
                const double rawMaxLabelRadius = outsideWheel
                    ? zodiacOuter + outerDecorationBudget - labelExtent
                        - 8.0 * fontScale_
                    : houseOuter - labelExtent - 2.0 * fontScale_;
                const double maxLabelRadius = std::max(minLabelRadius, rawMaxLabelRadius);
                const QRectF textRect = placeRadialRect(displayAngle, degRad, degWidth, degHeight,
                                                        minLabelRadius, maxLabelRadius, labelDirection);
                if (!textRect.isEmpty()) {
                    QColor haloColor = theme_.background;
                    haloColor.setAlpha(230);
                    painter.setPen(haloColor);
                    painter.drawText(textRect.translated(-1, 0), Qt::AlignCenter, degLabel);
                    painter.drawText(textRect.translated(1, 0), Qt::AlignCenter, degLabel);
                    painter.drawText(textRect.translated(0, -1), Qt::AlignCenter, degLabel);
                    painter.drawText(textRect.translated(0, 1), Qt::AlignCenter, degLabel);
                    painter.setPen(bodyColor);
                    painter.drawText(textRect, Qt::AlignCenter, degLabel);
                    occupiedRects.push_back(textRect.adjusted(-2, -2, 2, 2));
                }
                painter.restore();
                painter.setFont(planetFont);
            }

            const int house = houseForLongitude(item.trueLon, cusps);
            const QString retroLabel = item.retrograde ? " (R)" : "";
            const double roundedLon = normalizeDegrees(
                std::round(normalizeDegrees(item.trueLon) * 120.0) / 120.0);
            const QString displayName = lunarNodeDisplayName(item.name, nodePolicy);
            const QString tooltip = QString("%1%2%3 in %4 %5 (House %6)")
                .arg(prefix)
                .arg(displayName)
                .arg(retroLabel)
                .arg(signName(signIndex(roundedLon)))
                .arg(degLabel)
                .arg(house);
            planetHitAreas_.push_back(glyphR.adjusted(-2, -2, 2, 2));
            planetTooltips_.push_back(tooltip);
            planetHitNames_.push_back(item.name);
        }
    };

    auto drawFixedStars = [&](const QVector<FixedStarPosition>& stars, double markerRadius,
                              const QColor& color, const QString& prefix) {
        if (!showFixedStars_ || stars.isEmpty()) {
            return;
        }
        QFont starFont("Segoe UI Symbol");
        starFont.setPointSizeF(7.5 * fontScale_);
        QFont starDegFont("Segoe UI");
        starDegFont.setPointSizeF(8.0 * fontScale_);
        starDegFont.setBold(true);

        static const QVector<double> angleOffsets = {0.0, 2.5, -2.5};
        const QVector<double> radialOffsets = {
            0.0,
            4.0 * fontScale_,
            -4.0 * fontScale_,
            8.0 * fontScale_,
        };

        for (const auto& star : stars) {
            if (!isFixedStarVisible(star.name)) {
                continue;
            }
            const double lon = normalizeDegrees(star.longitude);
            const double baseAngle = angleForLongitude(lon);
            const QPointF pInner = pointOnCircle(center, markerRadius - 3.5 * fontScale_, baseAngle);
            const QPointF pOuter = pointOnCircle(center, markerRadius + 3.5 * fontScale_, baseAngle);

            painter.save();
            QColor lineColor = color;
            lineColor.setAlpha(180);
            painter.setPen(QPen(lineColor, 1.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(pInner, pOuter);
            painter.restore();

            QRectF symbolRect;
            double placedAngle = baseAngle;
            double placedRadius = markerRadius + 8.5 * fontScale_;
            bool placed = false;
            for (double radialOffset : radialOffsets) {
                for (double angleOffset : angleOffsets) {
                    const double testAngle = baseAngle + angleOffset;
                    const QPointF testPos = pointOnCircle(center, markerRadius + 8.5 * fontScale_ + radialOffset, testAngle);
                    const QRectF testRect(
                        testPos.x() - 6.0 * fontScale_,
                        testPos.y() - 6.0 * fontScale_,
                        12.0 * fontScale_,
                        12.0 * fontScale_);
                    bool hit = false;
                    for (const auto& occ : occupiedRects) {
                        if (testRect.intersects(occ)) {
                            hit = true;
                            break;
                        }
                    }
                    if (!hit) {
                        symbolRect = testRect;
                        placedAngle = testAngle;
                        placedRadius = markerRadius + 8.5 * fontScale_ + radialOffset;
                        placed = true;
                        break;
                    }
                }
                if (placed) {
                    break;
                }
            }
            if (!placed) {
                // Keep the exact-position tick visible even if density leaves no
                // honest place for a readable star symbol.
                continue;
            }

            painter.save();
            if (angularDiff(placedAngle, baseAngle) > 0.1
                || std::fabs(placedRadius - (markerRadius + 8.5 * fontScale_)) > 0.1) {
                QColor leaderColor = color;
                leaderColor.setAlpha(90);
                painter.setPen(QPen(leaderColor, 0.7, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(pOuter, symbolRect.center());
            }
            painter.restore();

            painter.save();
            painter.setFont(starFont);
            painter.setPen(color);
            painter.drawText(symbolRect, Qt::AlignCenter, QString::fromUtf8(u8"✶"));
            painter.restore();

            if (showDegrees_) {
                painter.save();
                painter.setFont(starDegFont);
                const bool outsideWheel = placedRadius >= zodiacOuter;
                const int labelDirection = outsideWheel ? 1 : -1;
                const double labelExtent = radialHalfExtent(
                    degWidth, degHeight, placedAngle);
                const double labelBase = placedRadius
                    + labelDirection * (6.0 * fontScale_ + labelExtent);
                const double minLabelRadius = outsideWheel
                    ? zodiacOuter + labelExtent + 2.0 * fontScale_
                    : houseInner + labelExtent + 2.0 * fontScale_;
                const double rawMaxLabelRadius = outsideWheel
                    ? zodiacOuter + outerDecorationBudget - labelExtent
                        - 8.0 * fontScale_
                    : houseOuter - labelExtent - 2.0 * fontScale_;
                const double maxLabelRadius = std::max(minLabelRadius, rawMaxLabelRadius);
                const QRectF textRect = placeRadialRect(placedAngle, labelBase, degWidth, degHeight,
                                                        minLabelRadius, maxLabelRadius, labelDirection);
                if (!textRect.isEmpty()) {
                    QColor haloColor = theme_.background;
                    haloColor.setAlpha(230);
                    const QString starDegree = formatDegShort(lon);
                    painter.setPen(haloColor);
                    painter.drawText(textRect.translated(-1, 0), Qt::AlignCenter, starDegree);
                    painter.drawText(textRect.translated(1, 0), Qt::AlignCenter, starDegree);
                    painter.drawText(textRect.translated(0, -1), Qt::AlignCenter, starDegree);
                    painter.drawText(textRect.translated(0, 1), Qt::AlignCenter, starDegree);
                    painter.setPen(color);
                    painter.drawText(textRect, Qt::AlignCenter, starDegree);
                    occupiedRects.push_back(textRect.adjusted(-2, -2, 2, 2));
                }
                painter.restore();
            }

            const double roundedLon = normalizeDegrees(
                std::round(normalizeDegrees(lon) * 120.0) / 120.0);
            const QString tooltip = QString("%1%2 in %3 %4 (House %5)")
                .arg(prefix)
                .arg(star.name)
                .arg(signName(signIndex(roundedLon)))
                .arg(formatDegShort(lon))
                .arg(star.house > 0 ? QString::number(star.house) : QString("-"));
            planetHitAreas_.push_back(symbolRect.adjusted(-2, -2, 2, 2));
            planetTooltips_.push_back(tooltip);
            planetHitNames_.push_back(QString());
            occupiedRects.push_back(symbolRect.adjusted(-2, -2, 2, 2));
        }
    };

    if (overlay) {
        // Natal planets INSIDE the wheel (between houseOuter and houseInner)
        // Tick lines point outward to zodiacInner
        // Keep overlay natal bodies away from the house-number lane, which is
        // centered in this band. The chart longitudes remain unchanged.
        const double natalBaseR = houseInner + (houseOuter - houseInner) * 0.30;
        const double natalMin = houseInner + glyphHalf + 4.0;
        const double natalMax = houseOuter - glyphHalf - 2.0;
        auto natalPlacements = computePlanetPlacements(chart_.bodies, natalBaseR, natalMin, natalMax, glyphSize, center);
        drawPlacedBodies(natalPlacements, zodiacInner, theme_.natalBody, "Natal ", false,
                         chart_.lunarNodePolicy);

        // Transit planets OUTSIDE the zodiac ring (planet lane)
        // Tick lines point inward to zodiacOuter
        const double transitMin = planetLaneRadius - glyphHalf - 4.0;
        const double transitMax = planetLaneRadius + glyphHalf + 4.0;
        auto transitPlacements = computePlanetPlacements(overlayChart_.bodies, planetLaneRadius, transitMin, transitMax, glyphSize, center);
        drawPlacedBodies(transitPlacements, zodiacOuter, theme_.transitBody, overlayPrefix + " ", true,
                         overlayChart_.lunarNodePolicy);
        drawFixedStars(chart_.fixedStars, zodiacOuter - 6.0 * fontScale_, theme_.natalBody, "Natal ");
        drawFixedStars(overlayChart_.fixedStars, zodiacOuter + 6.0 * fontScale_, theme_.transitBody, overlayPrefix + " ");
    } else {
        // Natal-only: single planet lane outside zodiac
        const double minR = planetLaneRadius - glyphHalf - 4.0;
        const double maxR = planetLaneRadius + glyphHalf + 4.0;
        auto placements = computePlanetPlacements(chart_.bodies, planetLaneRadius, minR, maxR, glyphSize, center);
        drawPlacedBodies(placements, zodiacOuter, theme_.body, "", false,
                         chart_.lunarNodePolicy);
        const QColor starColor = (mode_ == Mode::TransitOnly) ? theme_.transitBody : theme_.body;
        const QString starPrefix = (mode_ == Mode::TransitOnly) ? QString("Transit ") : QString();
        drawFixedStars(chart_.fixedStars, zodiacOuter + 6.0 * fontScale_, starColor, starPrefix);
    }

    // Angle labels + degrees.
    QFont angleLabelFont = angleFont;
    angleLabelFont.setPointSizeF(angleFont.pointSizeF() + 1.0);
    painter.setFont(angleLabelFont);
    const QStringList angleNames = {"AC", "MC", "DC", "IC"};
    // Names used by the aspect lines, so click-to-focus on an angle matches.
    const QStringList angleFocusNames = {"Ascendant", "Midheaven", "Descendant", "IC"};
    for (int i = 0; i < angleNames.size(); ++i) {
        const double angleLon = angleLons[i];
        const QPointF labelPos = pointOnCircle(center, angleLabelRadius, angleForLongitude(angleLon));
        const QRectF labelRect(labelPos.x() - angleLabelWidth * 0.5, labelPos.y() - angleLabelHeight * 0.5,
                                angleLabelWidth, angleLabelHeight);
        painter.save();
        const QColor angleHalo = theme_.background;
        painter.setPen(angleHalo);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                painter.drawText(labelRect.translated(dx, dy), Qt::AlignCenter, angleNames[i]);
            }
        }
        painter.setPen(angleColors[i]);
        painter.drawText(labelRect, Qt::AlignCenter, angleNames[i]);
        painter.restore();
        QRectF angleHitRect = labelRect.adjusted(-2, -2, 2, 2);
        if (showDegrees_) {
            painter.setFont(degreeFont);
            const double angle = angleForLongitude(angleLon);
            const double angleDegRadius = angleDegreeRadiusFor(angle);
            const QPointF degPos = pointOnCircle(center, angleDegRadius, angle);
            const QRectF degRect(degPos.x() - angleDegWidth * 0.5, degPos.y() - degHeight * 0.5,
                                  angleDegWidth, degHeight);
            painter.save();
            const QColor degHalo = theme_.background;
            painter.setPen(degHalo);
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    painter.drawText(degRect.translated(dx, dy), Qt::AlignCenter, formatDegShort(angleLon));
                }
            }
            painter.setPen(angleColors[i]);
            painter.drawText(degRect, Qt::AlignCenter, formatDegShort(angleLon));
            painter.restore();
            painter.setFont(angleLabelFont);
            angleHitRect = angleHitRect.united(degRect.adjusted(-2, -2, 2, 2));
        }
        const double roundedAngleLon = normalizeDegrees(
            std::round(normalizeDegrees(angleLon) * 120.0) / 120.0);
        const QString tooltip = QString("%1 in %2 %3")
            .arg(angleNames[i])
            .arg(signName(signIndex(roundedAngleLon)))
            .arg(formatDegShort(angleLon));
        planetHitAreas_.push_back(angleHitRect);
        planetTooltips_.push_back(tooltip);
        planetHitNames_.push_back(angleFocusNames[i]);
    }

    // --- Aspect colour legend (bottom-left) ---
    if (hasChart_ && showAspects_ && showAspectLegend_ && !aspectLines_.isEmpty()) {
        const QStringList aspNames = {"Conjunction", "Sextile", "Square", "Trine", "Opposition"};
        const QStringList scopeNames = overlay
            ? QStringList{overlayPrefix + " - Natal", overlayPrefix + " - " + overlayPrefix,
                          "Natal - Natal"}
            : QStringList{};
        const QVector<Qt::PenStyle> scopeStyles = {
            Qt::SolidLine, Qt::DashLine, Qt::DotLine,
        };
        QFont legendFont("Segoe UI");
        legendFont.setPointSizeF(8.0 * fontScale_);
        painter.setFont(legendFont);
        const QFontMetrics fm(legendFont);
        const double rowH = fm.height() + 3.0;
        const double sampleW = 20.0;
        double maxTextW = 0.0;
        for (const auto& n : aspNames) {
            maxTextW = std::max(maxTextW, static_cast<double>(fm.horizontalAdvance(n)));
        }
        for (const auto& n : scopeNames) {
            maxTextW = std::max(maxTextW, static_cast<double>(fm.horizontalAdvance(n)));
        }
        const double padX = 8.0;
        const double padY = 6.0;
        const double panelW = padX * 2 + sampleW + 8.0 + maxTextW + 14.0;
        const double scopeGap = scopeNames.isEmpty() ? 0.0 : 5.0;
        const double panelH = padY * 2 + rowH * (aspNames.size() + scopeNames.size()) + scopeGap;
        const double px = 10.0;
        const double py = height() - panelH - 10.0;

        painter.save();
        QColor bg = theme_.background;
        bg.setAlpha(215);
        painter.setPen(QPen(theme_.ringOuter, 1.0));
        painter.setBrush(bg);
        painter.drawRoundedRect(QRectF(px, py, panelW, panelH), 6, 6);

        QFont glyphLegendFont("Segoe UI Symbol");
        glyphLegendFont.setPointSizeF(8.0 * fontScale_);
        glyphLegendFont.setBold(true);
        for (int r = 0; r < aspNames.size(); ++r) {
            const QString& name = aspNames[r];
            const QColor color = aspectTypeColor(name);
            const double rowY = py + padY + rowH * r + rowH * 0.5;
            const double lx = px + padX;
            painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(lx, rowY), QPointF(lx + sampleW, rowY));
            painter.setFont(glyphLegendFont);
            painter.setPen(color);
            painter.drawText(QRectF(lx + sampleW + 2.0, rowY - rowH * 0.5, 12.0, rowH),
                             Qt::AlignCenter, aspectSymbolForLabel(name));
            painter.setFont(legendFont);
            QColor textColor = theme_.body;
            textColor.setAlpha(220);
            painter.setPen(textColor);
            painter.drawText(QRectF(lx + sampleW + 14.0, rowY - rowH * 0.5, maxTextW + 4.0, rowH),
                             Qt::AlignVCenter | Qt::AlignLeft, name);
        }
        for (int r = 0; r < scopeNames.size(); ++r) {
            const double rowY = py + padY + rowH * (aspNames.size() + r) + scopeGap + rowH * 0.5;
            const double lx = px + padX;
            QColor sampleColor = theme_.body;
            sampleColor.setAlpha(190);
            painter.setPen(QPen(sampleColor, 1.5, scopeStyles.value(r, Qt::SolidLine), Qt::RoundCap));
            painter.drawLine(QPointF(lx, rowY), QPointF(lx + sampleW, rowY));
            painter.setFont(legendFont);
            painter.setPen(sampleColor);
            painter.drawText(QRectF(lx + sampleW + 14.0, rowY - rowH * 0.5,
                                    maxTextW + 4.0, rowH),
                             Qt::AlignVCenter | Qt::AlignLeft, scopeNames[r]);
        }
        painter.restore();
    }

    // --- Focus indicator (top-right) when a body is pinned via click ---
    if (hasChart_ && hasFocus_ && !focusBody_.isEmpty()) {
        bool focusActive = false;
        for (const auto& info : aspectLines_) {
            if (info.rawNameA == focusBody_ || info.rawNameB == focusBody_) {
                focusActive = true;
                break;
            }
        }
        if (focusActive) {
            QFont focusFont("Segoe UI");
            focusFont.setPointSizeF(8.5 * fontScale_);
            focusFont.setBold(true);
            painter.setFont(focusFont);
            const QString text = QString("Focus: %1  (click empty to clear)").arg(focusBody_);
            const QFontMetrics fm(focusFont);
            const double w = fm.horizontalAdvance(text) + 18.0;
            const double h = fm.height() + 8.0;
            const double x = width() - w - 12.0;
            const double y = 10.0;
            painter.save();
            QColor bg = highlightColor_;
            bg.setAlpha(40);
            painter.setPen(QPen(highlightColor_, 1.0));
            painter.setBrush(bg);
            painter.drawRoundedRect(QRectF(x, y, w, h), 6, 6);
            QColor textColor = theme_.body;
            textColor.setAlpha(230);
            painter.setPen(textColor);
            painter.drawText(QRectF(x, y, w, h), Qt::AlignCenter, text);
            painter.restore();
        }
    }
}

}  // namespace dracoved
