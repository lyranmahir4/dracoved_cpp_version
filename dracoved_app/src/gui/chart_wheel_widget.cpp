#include "chart_wheel_widget.h"

#include "../core/fixed_stars.h"
#include "../core/formatting.h"
#include "transit_calc_service.h"

#include <QContextMenuEvent>
#include <QMap>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPaintEvent>
#include <QSvgRenderer>
#include <QBrush>
#include <QLinearGradient>
#include <QLineF>
#include <QEvent>
#include <QFontMetricsF>
#include <QRadialGradient>
#include <QStringList>
#include <QToolTip>
#include <QWheelEvent>
#include <QtGlobal>
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

// ---------------------------------------------------------------------------
// Surface helpers (presentation only).
// ---------------------------------------------------------------------------

// Linear RGBA mix; t = 0 returns a, t = 1 returns b.
QColor blendColor(const QColor& a, const QColor& b, double t) {
    const double k = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(
        a.redF()   + (b.redF()   - a.redF())   * k,
        a.greenF() + (b.greenF() - a.greenF()) * k,
        a.blueF()  + (b.blueF()  - a.blueF())  * k,
        a.alphaF() + (b.alphaF() - a.alphaF()) * k);
}

QColor withAlpha(const QColor& c, int alpha) {
    QColor out = c;
    out.setAlpha(std::clamp(alpha, 0, 255));
    return out;
}

// Any appended surface token that a theme site left default-constructed is
// derived from the long-standing tokens, so no theme can produce a broken look.
ChartWheelTheme resolveSurfaceTokens(const ChartWheelTheme& in) {
    ChartWheelTheme t = in;
    if (!t.zodiacBandInner.isValid()) {
        t.zodiacBandInner = t.ringZodiac;
    }
    if (!t.zodiacBandOuter.isValid()) {
        t.zodiacBandOuter = t.ringZodiac.darker(106);
    }
    if (!t.zodiacEdgeHighlight.isValid()) {
        t.zodiacEdgeHighlight = withAlpha(t.background.lighter(112), 140);
    }
    if (!t.zodiacEdgeShadow.isValid()) {
        t.zodiacEdgeShadow = t.ringOuter.darker(110);
    }
    if (!t.aspectDiscFill.isValid()) {
        t.aspectDiscFill = blendColor(t.background, t.ringHouseInner, 0.06);
    }
    if (!t.wheelHalo.isValid()) {
        t.wheelHalo = withAlpha(t.ringOuter, 40);
    }
    if (!t.cardinalBoundary.isValid()) {
        t.cardinalBoundary = t.signBoundary.darker(125);
    }
    if (!t.labelChipBg.isValid()) {
        t.labelChipBg = withAlpha(t.background, 215);
    }
    if (!t.labelChipBorder.isValid()) {
        t.labelChipBorder = QColor(Qt::transparent);
    }
    if (!t.infoRule.isValid()) {
        t.infoRule = withAlpha(t.ringOuter, 110);
    }
    if (!t.dignityStrong.isValid()) {
        t.dignityStrong = t.angularHouseLabel;
    }
    if (!t.dignityWeak.isValid()) {
        t.dignityWeak = withAlpha(t.houseLabel, 170);
    }
    return t;
}

// ---------------------------------------------------------------------------
// Per-body identity accents (presentation only).
// ---------------------------------------------------------------------------

double relativeLuminance(const QColor& c) {
    return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}

// How far a body's identity hue is pulled back toward the lane colour. Light
// backgrounds need harder muting (saturated hues shout on white); dark
// backgrounds can keep more of the accent. The natal lane is muted more than
// the transit lane so the two overlay rings still separate.
double accentMixFor(const QColor& background, bool isTransit) {
    const bool darkBackground = relativeLuminance(background) < 0.45;
    if (darkBackground) {
        return isTransit ? 0.24 : 0.42;
    }
    return isTransit ? 0.34 : 0.55;
}

// Pluto's near-black identity colour disappears on the Dark theme (and the
// Sun's yellow washes out on white), so any accent that sits too close to the
// background in luminance is stepped away from it until it reads.
QColor readableAccent(const QColor& accent, const QColor& background) {
    const double backgroundLuminance = relativeLuminance(background);
    const QColor target = (backgroundLuminance < 0.5) ? QColor(Qt::white) : QColor(Qt::black);
    QColor out = accent;
    for (int guard = 0; guard < 12; ++guard) {
        if (std::fabs(relativeLuminance(out) - backgroundLuminance) >= 0.28) {
            break;
        }
        out = blendColor(out, target, 0.18);
    }
    return out;
}

// ---------------------------------------------------------------------------
// One typography scale for the whole wheel.
// ---------------------------------------------------------------------------
enum class WheelText { Display, Label, Caption, Micro, BodyGlyph, AspectSymbol };

QFont wheelFont(WheelText role, double fontScale) {
    const double fs = fontScale > 0.0 ? fontScale : 1.0;
    QFont f;
    switch (role) {
    case WheelText::Display:
        f.setFamily(QStringLiteral("Segoe UI"));
        f.setPointSizeF(10.5 * fs);
        f.setWeight(QFont::DemiBold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.3);
        break;
    case WheelText::Label:
        f.setFamily(QStringLiteral("Segoe UI"));
        f.setPointSizeF(9.0 * fs);
        f.setWeight(QFont::Normal);
        break;
    case WheelText::Caption:
        f.setFamily(QStringLiteral("Segoe UI"));
        f.setPointSizeF(8.0 * fs);
        f.setWeight(QFont::Normal);
        break;
    case WheelText::Micro:
        f.setFamily(QStringLiteral("Segoe UI"));
        f.setPointSizeF(7.0 * fs);
        f.setWeight(QFont::Medium);
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        break;
    case WheelText::BodyGlyph:
        f.setFamily(QStringLiteral("Segoe UI Symbol"));
        f.setPointSizeF(12.0 * fs);
        break;
    case WheelText::AspectSymbol:
        f.setFamily(QStringLiteral("Segoe UI Symbol"));
        f.setPointSizeF(8.0 * fs);
        f.setBold(true);
        break;
    }
    return f;
}

// Degree readouts must not jitter when the user steps days, so ask for tabular
// figures and fall back through fixed-pitch families instead of requiring a
// specific font to be installed.
QFont tabularFigures(QFont f) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    f.setFeature(QFont::Tag("tnum"), 1);
    f.setFeature(QFont::Tag("lnum"), 1);
#endif
    f.setStyleHint(QFont::SansSerif, QFont::PreferMatch);
    QStringList families = f.families();
    if (families.isEmpty() && !f.family().isEmpty()) {
        families << f.family();
    }
    for (const QString& fallback : {QStringLiteral("Segoe UI"),
                                    QStringLiteral("Consolas"),
                                    QStringLiteral("DejaVu Sans Mono"),
                                    QStringLiteral("Courier New")}) {
        if (!families.contains(fallback)) {
            families << fallback;
        }
    }
    f.setFamilies(families);
    return f;
}

// Single crisp label treatment: a rounded chip instead of the old multi-pass
// text halo. The chip grows out of the supplied rect, so the collision rect the
// caller reserves stays exactly the same size.
void drawChipText(QPainter& p, const QRectF& rect, const QString& text,
                  const QColor& fg, const ChartWheelTheme& theme) {
    if (rect.isEmpty() || text.isEmpty()) {
        return;
    }
    p.save();
    const QRectF chip = rect.adjusted(-1.5, -0.5, 1.5, 0.5);
    if (theme.labelChipBg.isValid() && theme.labelChipBg.alpha() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme.labelChipBg);
        p.drawRoundedRect(chip, 3.0, 3.0);
    }
    if (theme.labelChipBorder.isValid() && theme.labelChipBorder.alpha() > 0) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(theme.labelChipBorder, 1.0));
        p.drawRoundedRect(chip, 3.0, 3.0);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(fg);
    p.drawText(rect, Qt::AlignCenter, text);
    p.restore();
}

bool rectTouchesCircle(const QRectF& rect, const QPointF& center, double radius) {
    const double nx = std::clamp(center.x(), rect.left(), rect.right());
    const double ny = std::clamp(center.y(), rect.top(), rect.bottom());
    const double dx = center.x() - nx;
    const double dy = center.y() - ny;
    return (dx * dx + dy * dy) <= radius * radius;
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
        // Surface tokens - cool neutral greys, near-white chips, subtle bevel
        QColor("#FAFBFC"),          // zodiacBandInner
        QColor("#EDF0F3"),          // zodiacBandOuter
        QColor(255, 255, 255, 170), // zodiacEdgeHighlight
        QColor("#D3D8DE"),          // zodiacEdgeShadow
        QColor("#FBFCFD"),          // aspectDiscFill
        QColor(96, 110, 125, 26),   // wheelHalo
        QColor("#AEB6BF"),          // cardinalBoundary
        QColor(255, 255, 255, 224), // labelChipBg
        QColor(214, 220, 227, 120), // labelChipBorder
        QColor(190, 198, 206, 120), // infoRule
        QColor("#c98a2c"),          // dignityStrong - warm amber
        QColor(124, 134, 145, 190), // dignityWeak - cool grey
    };
}

void ChartWheelWidget::setChart(const NatalChart& chart, HouseSystem system) {
    setAspectSelectionEnabled(false);
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
    setAspectSelectionEnabled(false);
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
    setAspectSelectionEnabled(false);
    chart_ = natal;
    overlayChart_ = transit;
    houseSystem_ = system;
    hasChart_ = true;
    hasOverlay_ = true;
    baseLabel_ = QStringLiteral("Natal");
    overlayAnglesVisible_ = false;
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

void ChartWheelWidget::setBaseLabel(const QString& label) {
    const QString trimmed = label.trimmed();
    baseLabel_ = trimmed.isEmpty() ? "Natal" : trimmed;
    update();
}

void ChartWheelWidget::setOverlayAnglesVisible(bool visible) {
    overlayAnglesVisible_ = visible;
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

void ChartWheelWidget::setShowPartOfFortune(bool value) {
    showPartOfFortune_ = value;
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

void ChartWheelWidget::setHiddenBodies(const QStringList& names) {
    QSet<QString> nextSet;
    for (const auto& name : names) {
        const QString normalized = chartBodyVisibilityKey(name);
        if (!normalized.isEmpty()) {
            nextSet.insert(normalized);
        }
    }
    hiddenBodySet_ = nextSet;
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

bool ChartWheelWidget::showPartOfFortune() const {
    return showPartOfFortune_;
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
    setAspectSelectionEnabled(false);
    hasChart_ = false;
    hasOverlay_ = false;
    mode_ = Mode::NatalOnly;
    hoveredAspectIndex_ = -1;
    aspectLines_.clear();
    planetHitAreas_.clear();
    planetTooltips_.clear();
    planetHitNames_.clear();
    planetHitScopedNames_.clear();
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

bool ChartWheelWidget::hasFocusBody() const {
    return hasFocus_ && !focusBody_.isEmpty();
}

void ChartWheelWidget::setAspectSelectionEnabled(bool enabled) {
    aspectSelectionEnabled_ = enabled;
    if (!enabled) clearAspectHighlight();
}

void ChartWheelWidget::setAspectHighlight(const QString& scopedBodyA, const QString& scopedBodyB) {
    if (!aspectSelectionEnabled_ || scopedBodyA.isEmpty() || scopedBodyB.isEmpty()) return;
    clearFocusBody();
    selectedAspectBodyA_ = scopedBodyA;
    selectedAspectBodyB_ = scopedBodyB;
    update();
}

bool ChartWheelWidget::hasAspectHighlight() const {
    return !selectedAspectBodyA_.isEmpty() && !selectedAspectBodyB_.isEmpty();
}

void ChartWheelWidget::clearAspectHighlight() {
    selectedAspectBodyA_.clear();
    selectedAspectBodyB_.clear();
    update();
}

bool ChartWheelWidget::isAspectHighlighted(const AspectLineInfo& aspect) const {
    return hasAspectHighlight()
        && ((aspect.scopedNameA == selectedAspectBodyA_ && aspect.scopedNameB == selectedAspectBodyB_)
            || (aspect.scopedNameA == selectedAspectBodyB_ && aspect.scopedNameB == selectedAspectBodyA_));
}

void ChartWheelWidget::clearFocusBody() {
    if (!hasFocus_ && focusBody_.isEmpty()) {
        return;
    }
    hasFocus_ = false;
    focusBody_.clear();
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

bool ChartWheelWidget::isBodyHidden(const QString& name) const {
    if (hiddenBodySet_.isEmpty()) {
        return false;
    }
    return hiddenBodySet_.contains(chartBodyVisibilityKey(name));
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
        if (focusActive && info.scopedNameA != focusBody_ && info.scopedNameB != focusBody_) {
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
        if (info.scopedNameA == focusBody_ || info.scopedNameB == focusBody_) {
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
        // Mirror of the paint-time budget clamp so pan limits match what is drawn.
        const double wantBudget = showDegrees_
            ? 112.0 * fontScale_ + 12.0
            : 35.0 * fontScale_ + 34.0;
        const double decorationBudget = std::min(wantBudget, size * 0.30);
        const double fittedOuter = std::max(size * 0.20, size * 0.5 - decorationBudget);
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
    if (aspectSummaryRect_.contains(event->position())) {
        if (hoveredAspectIndex_ != -1 || !hoveredPlanetScope_.isEmpty()) {
            hoveredAspectIndex_ = -1;
            hoveredPlanetScope_.clear();
            update();
        }
        setCursor(Qt::ArrowCursor);
        QToolTip::showText(event->globalPosition().toPoint(), aspectSummaryTooltip_, this);
        return;
    }
    for (int i = 0; i < planetHitAreas_.size(); ++i) {
        if (planetHitAreas_[i].contains(event->position())) {
            if (hoveredAspectIndex_ != -1) {
                hoveredAspectIndex_ = -1;
                update();
            }
            const QString scoped = (i < planetHitScopedNames_.size()) ? planetHitScopedNames_[i] : QString();
            if (hoveredPlanetScope_ != scoped) {
                hoveredPlanetScope_ = scoped;
                update();
            }
            if (!scoped.isEmpty() && !panning_) {
                setCursor(Qt::PointingHandCursor);
            } else if (!panning_) {
                updateCursor();
            }
            QToolTip::showText(event->globalPosition().toPoint(), planetTooltips_[i], this);
            return;
        }
    }
    if (!hoveredPlanetScope_.isEmpty()) {
        hoveredPlanetScope_.clear();
        updateCursor();
        update();
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
    if (aspectSummaryRect_.contains(event->position())
        && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        event->accept();
        return;
    }
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
            // Store the scope-qualified name so "Transit Sun" and "Natal Sun"
            // focus independently instead of lighting up both lanes at once.
            QString hitName;
            const int n = std::min(std::min(planetHitAreas_.size(), planetHitNames_.size()), planetHitScopedNames_.size());
            for (int i = 0; i < n; ++i) {
                if (!planetHitNames_[i].isEmpty() && planetHitAreas_[i].contains(event->position())) {
                    hitName = planetHitScopedNames_[i].isEmpty() ? planetHitNames_[i] : planetHitScopedNames_[i];
                    break;
                }
            }
            if (!hitName.isEmpty()) {
                clearAspectHighlight();
                if (hasFocus_ && focusBody_ == hitName) {
                    hasFocus_ = false;
                    focusBody_.clear();
                } else {
                    hasFocus_ = true;
                    focusBody_ = hitName;
                }
            } else {
                const int aspect = aspectSelectionEnabled_ && showAspects_ ? hitTestAspect(event->position()) : -1;
                if (aspect >= 0 && !isAspectHighlighted(aspectLines_[aspect])) {
                    setAspectHighlight(aspectLines_[aspect].scopedNameA, aspectLines_[aspect].scopedNameB);
                } else {
                    clearAspectHighlight();
                }
                hasFocus_ = false; focusBody_.clear();
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
    if (!hoveredPlanetScope_.isEmpty()) {
        hoveredPlanetScope_.clear();
        update();
    }
    updateCursor();
    QToolTip::hideText();
}

void ChartWheelWidget::updateCursor() {
    // Never switch cursors on zoom alone: the hand reads as "grab now" and was
    // popping up on every scroll-wheel zoom. Hand only while actually panning.
    if (panning_) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    setCursor(Qt::ArrowCursor);
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
        // Rendering passthrough only; no placement arithmetic reads it.
        QString dignity;
    };
    QVector<BodyDraw> drawList;
    drawList.reserve(bodies.size());
    bool hasMeanLunarNodes = false;
    bool hasTrueLunarNodes = false;
    for (const auto& pos : bodies) {
        if (isAsteroidBody(pos.name) && (!showAsteroids_ || !isAsteroidVisible(pos.name)))
            continue;
        // Checked before the general lot gate so Part of Fortune can be shown
        // without enabling the other ~95 lots.
        if (pos.name == "Part of Fortune") {
            if (!showPartOfFortune_)
                continue;
        } else if (isArabicLotName(pos.name) && !showLots_) {
            continue;
        }
        if (pos.name == "Vertex" && !showDerivedPoints_)
            continue;
        if (isBodyHidden(pos.name))
            continue;
        drawList.push_back({pos.name, pos.longitude, pos.retrograde,
                            pos.isLunarNode, pos.lunarNodeType, pos.dignity});
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
        result[i].dignity = drawList[i].dignity;
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
    // Locked bodies (lunar nodes) were re-anchored to their exact degree AFTER
    // all collision passes, so a free body can end up parked underneath a node
    // glyph (and nodes draw last, on top). Move only the free body: first try
    // one radial step out/in, then two; if every lane still hits the node,
    // fall back to a small angular push along the short arc away from it.
    {
        auto glyphRectAt = [&](double lon, double radius) {
            const double angle = angleForLongitude(normalizeDegrees(lon));
            const QPointF p = pointOnCircle(center, radius, angle);
            const double half = glyphSize * 0.5 + 2.0;
            return QRectF(p.x() - half, p.y() - half, half * 2.0, half * 2.0);
        };
        for (const auto& node : result) {
            if (!node.lockLongitude) {
                continue;
            }
            const QRectF nodeRect = glyphRectAt(node.trueLon, node.displayRadius);
            for (auto& freeBody : result) {
                if (freeBody.lockLongitude) {
                    continue;
                }
                if (!glyphRectAt(freeBody.displayLon, freeBody.displayRadius)
                         .intersects(nodeRect)) {
                    continue;
                }
                const double radiusTries[4] = {
                    freeBody.displayRadius + radialStep,
                    freeBody.displayRadius - radialStep,
                    freeBody.displayRadius + 2.0 * radialStep,
                    freeBody.displayRadius - 2.0 * radialStep,
                };
                bool resolved = false;
                for (double candidate : radiusTries) {
                    if (candidate < minRadius - 0.5 || candidate > maxRadius + 0.5) {
                        continue;
                    }
                    if (!glyphRectAt(freeBody.displayLon, candidate).intersects(nodeRect)) {
                        freeBody.displayRadius = candidate;
                        freeBody.radialLayer = qRound((candidate - baseRadius)
                                                      / std::max(radialStep, 1.0));
                        resolved = true;
                        break;
                    }
                }
                if (!resolved) {
                    const double away = std::fmod(
                        freeBody.displayLon - node.trueLon + 540.0, 360.0) - 180.0;
                    freeBody.displayLon += (away >= 0.0 ? 1.0 : -1.0) * minSpacingDeg * 0.9;
                }
            }
        }
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
    aspectSummaryRect_ = {};
    aspectSummaryTooltip_.clear();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), theme_.background);

    const QRectF bounds = rect().adjusted(12, 12, -12, -12);
    const double size = std::min(bounds.width(), bounds.height());
    const QPointF center(bounds.center().x() + panOffset_.x(), bounds.center().y() + panOffset_.y());
    // Surface tokens with paint-time fallbacks, so any theme literal renders.
    const ChartWheelTheme surface = resolveSurfaceTokens(theme_);

    // --- Astro.com-style geometry: planets OUTSIDE the zodiac ring ---
    // At the default zoom, reserve enough room for the planet lane and angle
    // degrees. User zoom is then applied on top, preserving intentional overflow
    // and panning instead of silently disabling magnification.
    // Degree labels need a real lane of their own beyond the body glyphs. The
    // previous fixed budget left that lane inside the glyph rectangles, so the
    // collision checker correctly rejected almost every body-degree label.
    // The decoration budget is capped as a fraction of the widget so the drawn
    // content radius can never exceed the half-size (which used to clip small
    // instances such as the relocation preview wheel). At normal panel sizes the
    // clamps are inactive and the arithmetic is identical to before.
    const double wantBudget = showDegrees_
        ? 112.0 * fontScale_ + 12.0
        : 35.0 * fontScale_ + 34.0;
    const double outerDecorationBudget = std::min(wantBudget, size * 0.30);
    const double fittedOuter = std::max(size * 0.20, size * 0.5 - outerDecorationBudget);
    const double baseZodiacOuter = std::min(size * 0.42, fittedOuter);
    const double zodiacOuter = baseZodiacOuter * zoom_;    // outer edge of zodiac sign ring
    const double zodiacInner = zodiacOuter * 0.86;         // inner edge of zodiac sign ring
    const double houseOuter  = zodiacInner - 4.0;          // outer edge of house area
    const double houseInner  = zodiacOuter * 0.52;         // inner edge of house area
    const double aspectRadius = houseInner - 8.0;          // aspect line endpoints
    const double houseLabelRadius = (houseOuter + houseInner) * 0.5; // center of house area
    // Planet lane sits outside the zodiac ring
    const double planetLaneRadius = zodiacOuter + 18.0 * fontScale_; // center of planet glyph lane

    // Soft halo just outside the zodiac rim. Purely additive: no geometry moves.
    {
        const double haloOuter = zodiacOuter + 10.0 * fontScale_;
        if (haloOuter > zodiacOuter + 0.5) {
            QRadialGradient halo(center, haloOuter);
            halo.setColorAt(0.0, surface.wheelHalo);
            halo.setColorAt(std::clamp(zodiacOuter / haloOuter, 0.0, 0.999),
                            surface.wheelHalo);
            halo.setColorAt(1.0, withAlpha(surface.wheelHalo, 0));
            QPainterPath ring;
            ring.addEllipse(center, haloOuter, haloOuter);
            ring.addEllipse(center, zodiacOuter, zodiacOuter);
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(halo);
            painter.drawPath(ring);
            painter.restore();
        }
    }

    // Zodiac band material. Drawn for every state so the empty wheel still has a
    // ring with substance; the element tints below sit on top of it.
    {
        QRadialGradient band(center, zodiacOuter);
        band.setColorAt(0.0, surface.zodiacBandInner);
        band.setColorAt(std::clamp(zodiacInner / zodiacOuter, 0.0, 0.999),
                        surface.zodiacBandInner);
        band.setColorAt(1.0, surface.zodiacBandOuter);
        QPainterPath ring;
        ring.addEllipse(center, zodiacOuter, zodiacOuter);
        ring.addEllipse(center, zodiacInner, zodiacInner);
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(band);
        painter.drawPath(ring);
        painter.restore();
    }

    // Paint opaque zodiac fills before structural strokes and ticks so those
    // details remain visible rather than being covered later in the frame.
    if (hasChart_) {
        const QVector<QColor> elementColors = {
            theme_.elementFire,
            theme_.elementEarth,
            theme_.elementAir,
            theme_.elementWater,
        };
        const double bandStop = std::clamp(zodiacInner / zodiacOuter, 0.0, 0.999);
        painter.save();
        painter.setPen(Qt::NoPen);
        for (int s = 0; s < 12; ++s) {
            // Element tint stays dominant; only the outermost sliver of the band
            // is shaded so the ring reads as a material instead of flat paint.
            const QColor tint = elementColors[elementIndexForSign(s)];
            QRadialGradient segment(center, zodiacOuter);
            segment.setColorAt(0.0, tint);
            segment.setColorAt(bandStop, tint);
            segment.setColorAt(1.0, blendColor(tint, surface.zodiacBandOuter, 0.12));
            painter.setBrush(segment);
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
    // Aspect plate: give the aspect web its own faint surface.
    painter.setPen(Qt::NoPen);
    painter.setBrush(surface.aspectDiscFill);
    painter.drawEllipse(center, houseInner, houseInner);
    painter.setBrush(Qt::NoBrush);
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
    // Bevel: a hair of light on the rim, a hair of shade on the inner edge, so
    // the zodiac band reads as slightly engraved rather than printed flat.
    painter.setPen(QPen(surface.zodiacEdgeHighlight, 1.0));
    painter.drawEllipse(center, zodiacOuter, zodiacOuter);
    painter.setPen(QPen(surface.zodiacEdgeShadow, 1.0));
    painter.drawEllipse(center, zodiacInner, zodiacInner);
    painter.restore();

    if (!hasChart_) {
        painter.save();
        const double emptyRadius = zodiacOuter * 0.55;
        QPen dashPen(withAlpha(theme_.placeholderText, 90), 1.2, Qt::DashLine);
        dashPen.setDashPattern({4.0, 4.0});
        painter.setPen(dashPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, emptyRadius, emptyRadius);
        painter.setFont(wheelFont(WheelText::Caption, fontScale_));
        painter.setPen(withAlpha(theme_.placeholderText, 180));
        const QRectF placeholderRect(center.x() - 120.0,
                                     center.y() + emptyRadius + 8.0 * fontScale_,
                                     240.0, 40.0);
        painter.drawText(placeholderRect, Qt::AlignHCenter | Qt::AlignTop, "No chart loaded");
        painter.restore();
        return;
    }

    // --- Chart info overlay (top-left corner) ---
    {
        painter.save();
        const int infoX = 10;
        int infoY = 8;
        double infoWidest = 0.0;
        int infoBottom = infoY;
        // Track the widest line so the closing hairline matches the block width.
        auto drawInfoLine = [&](const QString& text) {
            const QFontMetrics fm = painter.fontMetrics();
            painter.drawText(infoX, infoY + fm.ascent(), text);
            infoWidest = std::max(infoWidest,
                                  static_cast<double>(fm.horizontalAdvance(text)));
            infoBottom = infoY + fm.height();
            infoY += fm.height() + 1;
        };

        // Date line - prominent
        painter.setFont(wheelFont(WheelText::Display, fontScale_));
        QColor textColor = theme_.body;
        textColor.setAlpha(200);
        painter.setPen(textColor);

        const bool overlay = (mode_ == Mode::Overlay && hasOverlay_);
        const QDateTime& dt = chart_.localDateTime;
        if (dt.isValid()) {
            // Format: "4 Jan 1999, 4:12 PM" style
            drawInfoLine(dt.date().toString("d MMM yyyy"));

            // Time + timezone - secondary, lighter
            painter.setFont(wheelFont(WheelText::Caption, fontScale_));
            QColor timeColor = theme_.body;
            timeColor.setAlpha(150);
            painter.setPen(timeColor);
            QString timeLine = dt.time().toString("h:mm AP");
            if (!chart_.timezoneLabel.isEmpty()) {
                timeLine += "  " + chart_.timezoneLabel;
            }
            drawInfoLine(timeLine);
        }

        // Optional context note (e.g., Solar Return profected house).
        if (!chartNote_.isEmpty()) {
            infoY += 2;
            QFont noteFont = wheelFont(WheelText::Caption, fontScale_);
            noteFont.setWeight(QFont::DemiBold);
            noteFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.2);
            painter.setFont(noteFont);
            painter.setPen(theme_.angularHouseLabel);
            drawInfoLine(chartNote_);
        }

        // If overlay, show transit date on a second line
        if (overlay && overlayChart_.localDateTime.isValid()) {
            infoY += 3;
            painter.setFont(wheelFont(WheelText::Micro, fontScale_));
            QColor labelColor = theme_.transitBody;
            labelColor.setAlpha(140);
            painter.setPen(labelColor);
            const QString label = overlayLabel_.isEmpty() ? "TRANSIT" : overlayLabel_.toUpper();
            drawInfoLine(label);

            QFont transitDateFont = wheelFont(WheelText::Label, fontScale_);
            transitDateFont.setWeight(QFont::DemiBold);
            painter.setFont(transitDateFont);
            QColor transitColor = theme_.transitBody;
            transitColor.setAlpha(190);
            painter.setPen(transitColor);
            const QDateTime& tdt = overlayChart_.localDateTime;
            drawInfoLine(tdt.date().toString("d MMM yyyy"));

            painter.setFont(wheelFont(WheelText::Caption, fontScale_));
            transitColor.setAlpha(140);
            painter.setPen(transitColor);
            QString transitTime = tdt.time().toString("h:mm AP");
            if (!overlayChart_.timezoneLabel.isEmpty()) {
                transitTime += "  " + overlayChart_.timezoneLabel;
            }
            drawInfoLine(transitTime);
        }

        if (infoWidest > 0.0) {
            painter.setPen(QPen(surface.infoRule, 1.0));
            const double ruleY = infoBottom + 3.5;
            painter.drawLine(QPointF(infoX, ruleY),
                             QPointF(infoX + infoWidest, ruleY));
        }

        painter.restore();
    }

    const QFont smallFont = wheelFont(WheelText::Label, fontScale_);
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
    // Resource paths for zodiac icons. The opus-5 set is what the side panels
    // and the aspect-peak tables already draw, so the same twelve symbols now
    // have one look everywhere in the app.
    // To revert to the previous set, swap "zodiac_releasing" back to "zodiac":
    //   ":/resources/icons/zodiac/aries.svg" ... ":/resources/icons/zodiac/pisces.svg"
    static const QStringList svgPaths = {
        ":/resources/icons/zodiac_releasing/aries.svg",
        ":/resources/icons/zodiac_releasing/taurus.svg",
        ":/resources/icons/zodiac_releasing/gemini.svg",
        ":/resources/icons/zodiac_releasing/cancer.svg",
        ":/resources/icons/zodiac_releasing/leo.svg",
        ":/resources/icons/zodiac_releasing/virgo.svg",
        ":/resources/icons/zodiac_releasing/libra.svg",
        ":/resources/icons/zodiac_releasing/scorpio.svg",
        ":/resources/icons/zodiac_releasing/sagittarius.svg",
        ":/resources/icons/zodiac_releasing/capricorn.svg",
        ":/resources/icons/zodiac_releasing/aquarius.svg",
        ":/resources/icons/zodiac_releasing/pisces.svg"
    };

    for (int s = 0; s < 12; ++s) {
        const double lon = s * 30.0;
        const double angle = angleForLongitude(lon);
        const QPointF p1 = pointOnCircle(center, zodiacOuter, angle);
        const QPointF p2 = pointOnCircle(center, zodiacInner, angle);
        // Cardinal ingresses (0 Aries / Cancer / Libra / Capricorn) carry the
        // structure of the year, so they get a heavier stroke.
        const bool cardinal = (s % 3 == 0);
        painter.setPen(cardinal
                           ? QPen(surface.cardinalBoundary, 2.0 * fontScale_)
                           : QPen(theme_.signBoundary, 1.5));
        painter.drawLine(p1, p2);

        // Draw sign glyph centered in segment using SVG
        const double midLon = lon + 15.0;
        const QPointF pos = pointOnCircle(center, (zodiacOuter + zodiacInner) * 0.5, angleForLongitude(midLon));
        
        // The opus-5 glyphs carry more internal padding than the old set (drawn
        // ink fills roughly 63% of their box against ~85% before), so the icon
        // box grows to keep the symbol the same visual size inside the band.
        // Previous clamp: ((zodiacOuter - zodiacInner) * 0.70, 18.0, 32.0) * fontScale_.
        // Scale with the band and cap only the upper size. A hard minimum would
        // overflow the ring on tiny embedded wheels (the relocation preview),
        // where the band is only a few px thick.
        const double zodiacBand = zodiacOuter - zodiacInner;
        const double iconSize = std::min(zodiacBand * 0.92, 40.0 * fontScale_);
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

    // Cusp hairlines. Cusps sitting on an angle axis are skipped here; the
    // axis pass below gives those positions their own stronger treatment, and
    // double-drawing muddied the joint.
    const double houseLineWidth = 1.6 * fontScale_;
    painter.setPen(QPen(theme_.houseLine, houseLineWidth, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < cusps.size(); ++i) {
        bool onAngleAxis = false;
        for (const double axisLon : angleLons) {
            if (angularDiff(cusps[i], axisLon) < 0.35) {
                onAngleAxis = true;
                break;
            }
        }
        if (onAngleAxis) {
            continue;
        }
        const double angle = angleForLongitude(cusps[i]);
        const QPointF p1 = pointOnCircle(center, houseOuter, angle);
        const QPointF p2 = pointOnCircle(center, houseInner, angle);
        painter.drawLine(p1, p2);
    }

    // Angle axes (Astro.com style): a soft colored glow under a crisp core,
    // with a short stub piercing past the zodiac rim so AC/MC/DC/IC anchor the
    // eye. Primary angles (AC/MC) get a touch more weight than DC/IC.
    // In overlay mode the transit lane band paints over anything past the rim,
    // so the stub only extends where it stays visible.
    const bool hasTransitLane = (mode_ == Mode::Overlay && hasOverlay_);
    const double axisOuterRadius = zodiacOuter + (hasTransitLane ? 1.0 : 4.0) * fontScale_;
    for (int i = 0; i < angleLons.size(); ++i) {
        const double angleVal = angleForLongitude(angleLons[i]);
        const bool primary = (i == 0 || i == 1);
        const QPointF outerPt = pointOnCircle(center, axisOuterRadius, angleVal);
        const QPointF innerPt = pointOnCircle(center, houseInner, angleVal);
        QColor glow = angleColors[i];
        glow.setAlpha(60);
        painter.setPen(QPen(glow, 5.5 * fontScale_, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(outerPt, innerPt);
        painter.setPen(QPen(angleColors[i], (primary ? 2.4 : 2.0) * fontScale_,
                            Qt::SolidLine, Qt::RoundCap));
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
        // The lane stacks three radial layers (base ± one step) for cluster
        // relief, so the band has to span the whole glyph envelope. Computing
        // the outer edge from the base layer alone (previously
        // planetLaneRadius + glyphHalf + 8.0 * fontScale_) left outward-shifted
        // glyphs hanging past the edge, which read as a half-drawn lane.
        // laneStep mirrors radialStep in computePlanetPlacements.
        const double laneStep = glyphSize * 0.7;
        const double bandInner = zodiacOuter + 1.0;
        const double bandOuter = planetLaneRadius + laneStep + glyphHalf + 2.0 * fontScale_;
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
    QMap<QString, int> displayedAspectCounts;
    if (showAspects_) {
        QVector<QRectF> aspectSymbolRects;
        aspectSymbolRects.reserve(64);
        const QString natalScope = baseLabel_ + QStringLiteral(" ");
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
            // Wider weight range so orb tightness actually reads on the ring.
            const double baseWidth = 0.9 + (1.7 * strength);
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
                nameA,
                nameB,
                label,
            });
        };

        if (overlay) {
            auto isHiddenAspectBody = [&](const QString& name) {
                if (isAsteroidBody(name) && (!includeAsteroidAspects_ || !isAsteroidVisible(name))) {
                    return true;
                }
                if (name == "Part of Fortune") {
                    if (!showPartOfFortune_) {
                        return true;
                    }
                } else if (isArabicLotName(name) && !showLots_) {
                    return true;
                }
                if (name == "Vertex" && !showDerivedPoints_) {
                    return true;
                }
                if (isBodyHidden(name)) {
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
            transitPoints.reserve(overlayChart_.bodies.size() + 4);
            for (const auto& pos : overlayChart_.bodies) {
                transitPoints.push_back({pos.name, pos.longitude});
            }
            if (overlayAnglesVisible_) {
                // Only when the outer angles are actually shown, so the lines
                // never reference something the user cannot see.
                transitPoints.push_back({"Ascendant", overlayChart_.angles.asc});
                transitPoints.push_back({"Midheaven", overlayChart_.angles.mc});
                transitPoints.push_back({"Descendant", overlayChart_.angles.desc});
                transitPoints.push_back({"IC", overlayChart_.angles.ic});
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
                        appendAspectLine(t.lon, n.lon, overlayPrefix + " " + t.name, natalScope + n.name,
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
                                         natalScope + aName, natalScope + bName,
                                         cell.label, cell.orb, cell.maxOrb, Qt::DotLine, 0.45, "");
                    }
                }
            }
        } else {
            auto isHiddenAspectBody = [&](const QString& name) {
                if (isAsteroidBody(name) && (!includeAsteroidAspects_ || !isAsteroidVisible(name))) {
                    return true;
                }
                if (name == "Part of Fortune") {
                    if (!showPartOfFortune_) {
                        return true;
                    }
                } else if (isArabicLotName(name) && !showLots_) {
                    return true;
                }
                if (name == "Vertex" && !showDerivedPoints_) {
                    return true;
                }
                if (isBodyHidden(name)) {
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
        const bool hasSelectedAspect = std::any_of(aspectLines_.cbegin(), aspectLines_.cend(),
            [this](const AspectLineInfo& aspect) { return isAspectHighlighted(aspect); });
        // Focus is only "active" this frame if at least one drawn aspect touches
        // the focused body; this gracefully ignores a stale focus after the chart
        // changes (otherwise every line would dim).
        bool focusActive = false;
        if (hasFocus_ && !focusBody_.isEmpty()) {
            for (const auto& info : aspectLines_) {
                if (info.scopedNameA == focusBody_ || info.scopedNameB == focusBody_) {
                    focusActive = true;
                    break;
                }
            }
        }
        const QFont aspectFont = wheelFont(WheelText::AspectSymbol, fontScale_);
        const bool denseAspectField = aspectLines_.size() > 60;
        for (int i = 0; i < aspectLines_.size(); ++i) {
            const auto& info = aspectLines_[i];
            const bool isHover = (i == hoveredAspectIndex_);
            const bool isSelected = isAspectHighlighted(info);
            const bool involvesFocus = focusActive
                && (info.scopedNameA == focusBody_ || info.scopedNameB == focusBody_);
            double opacity = info.baseOpacity;
            double width = info.baseWidth;
            // Click-to-focus: in focus mode show ONLY the lines touching the
            // focused body (others are hidden, not just dimmed, so hover/hit-test
            // never picks up unrelated aspects).
            if (focusActive && !involvesFocus) {
                continue;
            }
            // Count the same filtered pairs we draw, including conjunctions
            // whose endpoints overlap. Hover emphasis does not change totals.
            ++displayedAspectCounts[info.aspectName];
            if (focusActive && involvesFocus) {
                opacity = std::min(1.0, opacity + 0.30);
                width += 0.6;
            }
            // Hover still emphasises a single line on top of any focus state.
            if ((hasHover || hasSelectedAspect) && !isHover && !isSelected) {
                opacity *= 0.25;
            } else if (isHover || isSelected) {
                opacity = std::min(1.0, opacity + 0.35);
                width += 0.8;
            }
            painter.save();
            painter.setOpacity(opacity);
            // Fade the last sliver at each end so a chord meets the glyph ring
            // softly instead of crashing into it. Built per draw from the chord
            // endpoints; style still carries the scope encoding, and the
            // painter opacity above still scales the whole line.
            QLinearGradient chordGradient(info.line.p1(), info.line.p2());
            const QColor chordEnd = withAlpha(info.color, 115);
            chordGradient.setColorAt(0.0, chordEnd);
            chordGradient.setColorAt(0.12, info.color);
            chordGradient.setColorAt(0.88, info.color);
            chordGradient.setColorAt(1.0, chordEnd);
            // Named brush: keeps this an unambiguous variable definition rather
            // than something a compiler could read as a function declaration.
            const QBrush chordBrush(chordGradient);
            QPen pen(chordBrush, width);
            pen.setStyle(info.style);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(info.path);
            // Endpoint dots anchor the chord to the two degrees it joins.
            const double endpointRadius = 1.6 * fontScale_;
            painter.setPen(Qt::NoPen);
            painter.setBrush(info.color);
            painter.drawEllipse(info.line.p1(), endpointRadius, endpointRadius);
            painter.drawEllipse(info.line.p2(), endpointRadius, endpointRadius);
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
    planetHitScopedNames_.clear();
    QVector<QRectF> occupiedRects = houseLabelRects;
    occupiedRects.reserve(128);

    // Label, bold, a half point up - and tabular figures so 08 and 11 occupy the
    // same width and degrees stop jittering while stepping dates.
    QFont degreeFont = smallFont;
    degreeFont.setBold(true);
    degreeFont.setPointSizeF(smallFont.pointSizeF() + 0.5);
    degreeFont = tabularFigures(degreeFont);

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
        const QFont planetFont = wheelFont(WheelText::BodyGlyph, fontScale_);
        painter.setFont(planetFont);
        // How far a body's identity hue is allowed to pull the lane colour.
        const double accentMix = accentMixFor(theme_.background, isTransit);

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

            // Identity accent, shared with the astrocartography map, muted back
            // toward the lane colour so the ring still reads as one family.
            // Bodies with no identity hue (Arabic Lots, asteroids, Vertex,
            // angles) keep the lane colour exactly as before.
            QColor bodyColor = color;
            {
                const QColor accent = transitcalc::bodyAccentColor(item.name);
                if (accent.isValid()) {
                    bodyColor = blendColor(readableAccent(accent, theme_.background),
                                           color, accentMix);
                }
            }

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
                // Retrogrades get a heavier, full-alpha tick so they are
                // identifiable straight off the ring. Geometry is untouched.
                QColor markColor = bodyColor;
                markColor.setAlpha(item.retrograde ? 255 : 230);
                painter.setPen(QPen(markColor, item.retrograde ? 1.9 : 1.3,
                                    Qt::SolidLine, Qt::RoundCap));
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

            // Hover ring: soft echo of the body color, scoped so natal and
            // transit copies of the same body do not both light up.
            if (!hoveredPlanetScope_.isEmpty() && hoveredPlanetScope_ == prefix + item.name) {
                painter.save();
                QColor hoverColor = bodyColor;
                hoverColor.setAlpha(170);
                painter.setPen(QPen(hoverColor, 1.4));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(glyphR.adjusted(-3, -3, 3, 3));
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
            // Retrograde indicator (℞): sits just clear of the glyph along the
            // radial direction (outward for lanes outside the zodiac, inward for
            // the overlay natal lane) instead of overlapping the glyph corner,
            // and gets the same crisp chip treatment as the degree labels.
            if (item.retrograde) {
                painter.save();
                QFont retroFont = smallFont;
                retroFont.setPointSizeF(smallFont.pointSizeF() * 0.75);
                retroFont.setBold(true);
                painter.setFont(retroFont);
                const double retroSize = 10.0 * fontScale_;
                const bool retroOutside = item.displayRadius >= zodiacOuter;
                const int retroDirection = retroOutside ? 1 : -1;
                const double retroRadius = item.displayRadius
                    + retroDirection * (gs * 0.5 + retroSize * 0.5 + 1.0 * fontScale_);
                const QPointF retroPos = pointOnCircle(center, retroRadius, displayAngle);
                const QRectF retroRect(retroPos.x() - retroSize * 0.5,
                                       retroPos.y() - retroSize * 0.5,
                                       retroSize, retroSize);
                drawChipText(painter, retroRect, QString::fromUtf8(u8"℞"),
                             theme_.retrogradeIndicator, surface);
                painter.restore();
                occupiedRects.push_back(retroRect.adjusted(-1, -1, 1, 1));
            }

            // Dignity accent: one small dot at the glyph's BOTTOM-LEFT. Top-right
            // belongs to the retrograde mark and bottom-right to the lunar-node
            // M/T badge, so the left corner is the only clear one. Suppressed on
            // small glyphs (the 220px preview) where a 2px dot is just noise.
            if (glyphSize >= 16.5 && !isArabicLotName(item.name) && item.name != "Vertex"
                && !item.dignity.isEmpty() && item.dignity != "-") {
                QColor dignityColor;
                if (item.dignity == "Ruler" || item.dignity == "Exalt") {
                    dignityColor = surface.dignityStrong;
                } else if (item.dignity == "Detriment" || item.dignity == "Fall") {
                    dignityColor = surface.dignityWeak;
                }
                if (dignityColor.isValid()) {
                    const double dotRadius = 2.0 * fontScale_;
                    const QRectF dotRect(glyphR.left() - dotRadius * 0.5,
                                         glyphR.bottom() - dotRadius * 1.5,
                                         dotRadius * 2.0, dotRadius * 2.0);
                    painter.save();
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(dignityColor);
                    painter.drawEllipse(dotRect);
                    painter.restore();
                    occupiedRects.push_back(dotRect.adjusted(-1, -1, 1, 1));
                }
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
                    drawChipText(painter, textRect, degLabel, bodyColor, surface);
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
            // Dignity is appended only when core actually assigned one, so the
            // existing tooltip text is byte-identical for everything else.
            QString dignitySuffix;
            if (!item.dignity.isEmpty() && item.dignity != "-") {
                dignitySuffix = QString(" %1 %2").arg(QChar(0x00B7)).arg(item.dignity);
            }
            const QString tooltip = QString("%1%2%3 in %4 %5 (House %6)%7")
                .arg(prefix)
                .arg(displayName)
                .arg(retroLabel)
                .arg(signName(signIndex(roundedLon)))
                .arg(degLabel)
                .arg(house)
                .arg(dignitySuffix);
            planetHitAreas_.push_back(glyphR.adjusted(-2, -2, 2, 2));
            planetTooltips_.push_back(tooltip);
            planetHitNames_.push_back(item.name);
            planetHitScopedNames_.push_back(prefix + item.name);
        }
    };

    auto drawFixedStars = [&](const QVector<FixedStarPosition>& stars, double markerRadius,
                              const QColor& color, const QString& prefix) {
        if (!showFixedStars_ || stars.isEmpty()) {
            return;
        }
        QFont starFont = wheelFont(WheelText::AspectSymbol, fontScale_);
        starFont.setBold(false);
        starFont.setPointSizeF(starFont.pointSizeF() - 0.5);
        QFont starDegFont = wheelFont(WheelText::Caption, fontScale_);
        starDegFont.setBold(true);
        starDegFont = tabularFigures(starDegFont);

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
                    drawChipText(painter, textRect, formatDegShort(lon), color, surface);
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
            planetHitScopedNames_.push_back(QString());
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
        drawPlacedBodies(natalPlacements, zodiacInner, theme_.natalBody,
                         baseLabel_ + QStringLiteral(" "), false,
                         chart_.lunarNodePolicy);

        // Transit planets OUTSIDE the zodiac ring (planet lane)
        // Tick lines point inward to zodiacOuter
        const double transitMin = planetLaneRadius - glyphHalf - 4.0;
        const double transitMax = planetLaneRadius + glyphHalf + 4.0;
        auto transitPlacements = computePlanetPlacements(overlayChart_.bodies, planetLaneRadius, transitMin, transitMax, glyphSize, center);
        drawPlacedBodies(transitPlacements, zodiacOuter, theme_.transitBody, overlayPrefix + " ", true,
                         overlayChart_.lunarNodePolicy);
        drawFixedStars(chart_.fixedStars, zodiacOuter - 6.0 * fontScale_, theme_.natalBody,
                       baseLabel_ + QStringLiteral(" "));
        drawFixedStars(overlayChart_.fixedStars, zodiacOuter + 6.0 * fontScale_, theme_.transitBody, overlayPrefix + " ");

        if (overlayAnglesVisible_) {
            const QVector<double> overlayAngleLons = {
                overlayChart_.angles.asc,
                overlayChart_.angles.mc,
                overlayChart_.angles.desc,
                overlayChart_.angles.ic,
            };
            const QStringList overlayAngleNames = {"AC", "MC", "DC", "IC"};
            const QStringList overlayAngleFocus = {
                "Ascendant", "Midheaven", "Descendant", "IC",
            };
            painter.save();
            for (int i = 0; i < overlayAngleLons.size(); ++i) {
                const double angleVal = angleForLongitude(overlayAngleLons[i]);
                // A short tick in the outer lane rather than a full axis: the
                // inner chart's axes carry the house frame and stay dominant.
                painter.setPen(QPen(angleColors[i], 2.0 * fontScale_));
                painter.drawLine(
                    pointOnCircle(center, zodiacOuter + 2.0 * fontScale_, angleVal),
                    pointOnCircle(center, zodiacOuter + 14.0 * fontScale_, angleVal));
                const QPointF labelPos = pointOnCircle(center, angleLabelRadius, angleVal);
                const QRectF labelRect(labelPos.x() - angleLabelWidth * 0.5,
                                       labelPos.y() - angleLabelHeight * 0.5,
                                       angleLabelWidth, angleLabelHeight);
                painter.setFont(angleFont);
                drawChipText(painter, labelRect, overlayAngleNames[i],
                             angleColors[i], surface);
                const double roundedOverlayLon = normalizeDegrees(
                    std::round(normalizeDegrees(overlayAngleLons[i]) * 120.0) / 120.0);
                planetHitAreas_.push_back(labelRect.adjusted(-2, -2, 2, 2));
                planetTooltips_.push_back(QString("%1 %2 in %3 %4")
                    .arg(overlayPrefix,
                         overlayAngleNames[i],
                         signName(signIndex(roundedOverlayLon)),
                         formatDegShort(overlayAngleLons[i])));
                planetHitNames_.push_back(overlayAngleFocus[i]);
                planetHitScopedNames_.push_back(
                    overlayPrefix + QStringLiteral(" ") + overlayAngleFocus[i]);
            }
            painter.restore();
        }
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
        drawChipText(painter, labelRect, angleNames[i], angleColors[i], surface);
        QRectF angleHitRect = labelRect.adjusted(-2, -2, 2, 2);
        if (showDegrees_) {
            painter.setFont(degreeFont);
            const double angle = angleForLongitude(angleLon);
            const double angleDegRadius = angleDegreeRadiusFor(angle);
            const QPointF degPos = pointOnCircle(center, angleDegRadius, angle);
            const QRectF degRect(degPos.x() - angleDegWidth * 0.5, degPos.y() - degHeight * 0.5,
                                  angleDegWidth, degHeight);
            drawChipText(painter, degRect, formatDegShort(angleLon), angleColors[i], surface);
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
        // Angles are drawn from the natal chart only, so they carry the natal
        // scope in overlay mode and no prefix otherwise.
        planetHitScopedNames_.push_back(
            (overlay ? baseLabel_ + QStringLiteral(" ") : QString()) + angleFocusNames[i]);
    }

    // --- Hovered/selected aspects: ring the two endpoints they join ---
    // Deliberately last and purely additive: the hit vectors are only complete
    // once bodies and angles have been drawn. Matching is scope-qualified: a raw
    // name such as "Venus" exists in BOTH overlay lanes, so comparing raw names
    // ringed four glyphs for a two-body aspect.
    for (int aspectIndex = 0; showAspects_ && aspectIndex < aspectLines_.size(); ++aspectIndex) {
        const auto& hovered = aspectLines_[aspectIndex];
        const bool selected = isAspectHighlighted(hovered);
        if (aspectIndex != hoveredAspectIndex_ && !selected) continue;
        const int hitCount = static_cast<int>(
            std::min(planetHitAreas_.size(), planetHitScopedNames_.size()));
        painter.save();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(hovered.color, selected ? 2.2 : 1.4));
        for (int i = 0; i < hitCount; ++i) {
            const QString& hitName = planetHitScopedNames_[i];
            if (hitName.isEmpty()
                || (hitName != hovered.scopedNameA && hitName != hovered.scopedNameB)) {
                continue;
            }
            painter.drawEllipse(planetHitAreas_[i].adjusted(-2.5, -2.5, 2.5, 2.5));
        }
        painter.restore();
    }

    // --- Aspect colour legend (bottom-left) ---
    if (hasChart_ && showAspects_ && showAspectLegend_ && !aspectLines_.isEmpty()) {
        const QStringList aspNames = {"Conjunction", "Sextile", "Square", "Trine", "Opposition"};
        // Only advertise scopes that are actually drawn, and name the inner chart
        // properly: "Natal" is wrong when two different people are compared.
        QStringList scopeNames;
        QVector<Qt::PenStyle> scopeStyles;
        if (overlay) {
            if (overlayTransitNatalAspects_) {
                scopeNames << (overlayPrefix + " - " + baseLabel_);
                scopeStyles << Qt::SolidLine;
            }
            if (overlayTransitTransitAspects_) {
                scopeNames << (overlayPrefix + " - " + overlayPrefix);
                scopeStyles << Qt::DashLine;
            }
            if (overlayNatalNatalAspects_) {
                scopeNames << (baseLabel_ + " - " + baseLabel_);
                scopeStyles << Qt::DotLine;
            }
        }
        const QFont legendFont = wheelFont(WheelText::Caption, fontScale_);
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
        // Prefer bottom-left, but never let the panel sit on top of the wheel.
        const double margin = 10.0;
        const QVector<QPointF> legendSpots = {
            QPointF(margin, height() - panelH - margin),                  // bottom-left
            QPointF(width() - panelW - margin, height() - panelH - margin),// bottom-right
            QPointF(margin, margin),                                      // top-left
        };
        const double legendClearance = zodiacOuter + (overlay ? 34.0 : 12.0) * fontScale_;
        QPointF legendPos = legendSpots.front();
        for (const QPointF& spot : legendSpots) {
            if (!rectTouchesCircle(QRectF(spot.x(), spot.y(), panelW, panelH),
                                   center, legendClearance)) {
                legendPos = spot;
                break;
            }
        }
        const double px = legendPos.x();
        const double py = legendPos.y();

        painter.save();
        painter.setPen(QPen(theme_.ringOuter, 1.0));
        painter.setBrush(surface.labelChipBg);
        painter.drawRoundedRect(QRectF(px, py, panelW, panelH), 6, 6);

        const QFont glyphLegendFont = wheelFont(WheelText::AspectSymbol, fontScale_);
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

    // Aspect counts stay anchored inside every wheel, independent of zoom/pan.
    {
        int total = 0;
        for (int count : displayedAspectCounts) total += count;
        const int positive = displayedAspectCounts.value("Sextile") + displayedAspectCounts.value("Trine");
        const int negative = displayedAspectCounts.value("Square") + displayedAspectCounts.value("Opposition");
        const int neutral = total - positive - negative;
        const int net = positive - negative;
        const QString title = !showAspects_ ? "Aspects hidden"
            : focusActiveNow() ? "Focused aspects" : "Aspect counts";
        const QStringList labels = {"Total", "Positive", "Negative", "Neutral", "Net"};
        const QStringList values = {QString::number(total), QString::number(positive),
            QString::number(negative), QString::number(neutral),
            (net > 0 ? QString("+") : QString()) + QString::number(net)};
        const QColor green("#2E8B57"), red("#C4473A");
        const QVector<QColor> colors = {theme_.body, green, red, aspectTypeColor("Conjunction"),
            net > 0 ? green : net < 0 ? red : theme_.body};
        const QFont summaryFont = wheelFont(WheelText::Caption, fontScale_);
        QFont headingFont = summaryFont;
        headingFont.setBold(true);
        const QFontMetrics fm(summaryFont), headingMetrics(headingFont);
        const double rowH = fm.height() + 3.0;
        const double panelW = std::max(headingMetrics.horizontalAdvance(title) + 18.0,
            fm.horizontalAdvance("Negative") + fm.horizontalAdvance(QString::number(total)) + 38.0);
        const double panelH = rowH * 6 + 14.0;
        aspectSummaryRect_ = QRectF(width() - panelW - 10.0, 10.0, panelW, panelH);
        painter.save();
        painter.setPen(QPen(theme_.ringOuter, 1.0));
        painter.setBrush(theme_.background);
        painter.drawRoundedRect(aspectSummaryRect_, 5, 5);
        const double x = aspectSummaryRect_.left() + 8.0;
        double y = aspectSummaryRect_.top() + 5.0;
        painter.setFont(headingFont);
        painter.setPen(theme_.body);
        painter.drawText(QRectF(x, y, panelW - 16.0, rowH), Qt::AlignVCenter | Qt::AlignLeft, title);
        y += rowH + 4.0;
        for (int i = 0; i < labels.size(); ++i) {
            if (i == 4) {
                painter.setPen(QPen(theme_.ringOuter, 1.0));
                painter.drawLine(QPointF(x, y), QPointF(x + panelW - 16.0, y));
            }
            painter.setFont(summaryFont);
            painter.setPen(theme_.body);
            painter.drawText(QRectF(x, y, panelW - 16.0, rowH), Qt::AlignVCenter | Qt::AlignLeft, labels[i]);
            painter.setFont(headingFont);
            painter.setPen(showAspects_ ? colors[i] : theme_.body);
            painter.drawText(QRectF(x, y, panelW - 16.0, rowH), Qt::AlignVCenter | Qt::AlignRight,
                showAspects_ ? values[i] : QString::fromUtf8("—"));
            y += rowH;
        }
        painter.restore();
        aspectSummaryTooltip_ = showAspects_
            ? QString("Counts of displayed aspect pairs (not weighted scores).\n"
                      "Positive: sextiles (%1) + trines (%2).\n"
                      "Negative: squares (%3) + oppositions (%4).\n"
                      "Neutral: conjunctions and any unclassified aspects (%5).\n"
                      "Net = positive count minus negative count.\n"
                      "Follows current orb, visible bodies, enabled scopes and body focus.\n"
                      "Zoom, pan and hover do not change counts.")
                  .arg(displayedAspectCounts.value("Sextile")).arg(displayedAspectCounts.value("Trine"))
                  .arg(displayedAspectCounts.value("Square")).arg(displayedAspectCounts.value("Opposition")).arg(neutral)
            : QString("Aspect lines are hidden. Enable aspects to see their counts.");
    }

    // --- Focus indicator below the aspect counts when a body is pinned ---
    if (hasChart_ && hasFocus_ && !focusBody_.isEmpty()) {
        bool focusActive = false;
        for (const auto& info : aspectLines_) {
            if (info.scopedNameA == focusBody_ || info.scopedNameB == focusBody_) {
                focusActive = true;
                break;
            }
        }
        if (focusActive) {
            QFont focusFont = wheelFont(WheelText::Caption, fontScale_);
            focusFont.setPointSizeF(focusFont.pointSizeF() + 0.5);
            focusFont.setBold(true);
            painter.setFont(focusFont);
            const QString text = QString("Focus: %1  (click empty or press Esc to clear)").arg(focusBody_);
            const QFontMetrics fm(focusFont);
            const double w = fm.horizontalAdvance(text) + 18.0;
            const double h = fm.height() + 8.0;
            const double x = width() - w - 12.0;
            const double y = aspectSummaryRect_.bottom() + 6.0;
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
