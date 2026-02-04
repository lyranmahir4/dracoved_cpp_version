#include "chart_wheel_widget.h"

#include "../core/formatting.h"

#include <QContextMenuEvent>
#include <QMap>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
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
    update();
}

void ChartWheelWidget::setOverlayLabel(const QString& label) {
    const QString trimmed = label.trimmed();
    overlayLabel_ = trimmed.isEmpty() ? "Transit" : trimmed;
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

void ChartWheelWidget::setFontScale(double scale) {
    fontScale_ = std::clamp(scale, 0.8, 1.4);
    update();
}

void ChartWheelWidget::setTheme(const ChartWheelTheme& theme) {
    theme_ = theme;
    update();
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
    hasHighlight_ = false;
    highlightBody_.clear();
    highlightLabel_.clear();
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

double ChartWheelWidget::distanceToSegment(const QPointF& point, const QLineF& line) const {
    const QPointF a = line.p1();
    const QPointF b = line.p2();
    const QPointF ab = b - a;
    const QPointF ap = point - a;
    const double abLen2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (abLen2 <= 0.0001) {
        return std::hypot(ap.x(), ap.y());
    }
    double t = (ap.x() * ab.x() + ap.y() * ab.y()) / abLen2;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF proj = a + ab * t;
    const QPointF diff = point - proj;
    return std::hypot(diff.x(), diff.y());
}

int ChartWheelWidget::hitTestAspect(const QPointF& point) const {
    if (aspectLines_.isEmpty()) {
        return -1;
    }
    const double threshold = 5.0 * fontScale_;
    double best = threshold;
    int bestIndex = -1;
    for (int i = 0; i < aspectLines_.size(); ++i) {
        const auto& info = aspectLines_[i];
        if (info.symbolRect.contains(point)) {
            return i;
        }
        const double dist = distanceToSegment(point, info.line);
        if (dist < best) {
            best = dist;
            bestIndex = i;
        }
    }
    return bestIndex;
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
    if ((event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton) && panning_) {
        panning_ = false;
        updateCursor();
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

void ChartWheelWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), theme_.background);

    const QRectF bounds = rect().adjusted(12, 12, -12, -12);
    const double size = std::min(bounds.width(), bounds.height());
    const QPointF center(bounds.center().x() + panOffset_.x(), bounds.center().y() + panOffset_.y());

    const double outer = size * 0.46 * zoom_;
    const double tickRing = outer + 16.0;
    const double transitRing = outer + 10.0;
    const double zodiacInner = outer * 0.88;
    const double houseOuter = zodiacInner - 6.0;
    // Widen the planet ring significantly to allow for stacking
    const double houseInner = outer * 0.55; 
    const double aspectRadius = houseInner - 15.0; // Push aspect grid further in
    const double planetRadius = (houseOuter + houseInner) * 0.5;
    const double houseLabelRadius = houseInner - 14.0;

    painter.save();
    // Ring stroke hierarchy - outer thicker for visual prominence
    painter.setPen(QPen(theme_.ringOuter, 2.5));
    painter.drawEllipse(center, outer, outer);
    painter.setPen(QPen(theme_.ringZodiac, 1.5));
    painter.drawEllipse(center, zodiacInner, zodiacInner);
    painter.setPen(QPen(theme_.ringHouseOuter, 1.2));
    painter.drawEllipse(center, houseOuter, houseOuter);
    painter.setPen(QPen(theme_.ringHouseInner, 1.0));
    painter.drawEllipse(center, houseInner, houseInner);
    // Aspect inner boundary - subtle dashed circle to contain aspect lines
    QPen aspectBoundaryPen(theme_.aspectInnerCircle, 1.0, Qt::DotLine);
    painter.setPen(aspectBoundaryPen);
    painter.drawEllipse(center, aspectRadius, aspectRadius);
    painter.restore();

    if (!hasChart_) {
        painter.setPen(theme_.placeholderText);
        painter.drawText(rect(), Qt::AlignCenter, "Chart wheel placeholder");
        return;
    }

    QFont glyphFont("Segoe UI Symbol", 14 * fontScale_);
    QFont smallFont("Segoe UI", 9 * fontScale_);
    QFont angleFont = smallFont;
    angleFont.setPointSizeF(smallFont.pointSizeF() + 2.0);
    angleFont.setBold(true);
    painter.setFont(glyphFont);
    const double glyphSize = 22.0 * fontScale_;
    const double glyphHalf = glyphSize * 0.5;
    const double degWidth = 42.0 * fontScale_;
    const double degHeight = 16.0 * fontScale_;
    const double houseLabelSize = 18.0 * fontScale_;
    const double angleLabelWidth = 36.0 * fontScale_;
    const double angleLabelHeight = 20.0 * fontScale_;
    const double angleDegWidth = 46.0 * fontScale_;
    const double degreeOffset = glyphHalf + 10.0;
    const double angleLabelRadius = tickRing + 16.0;
    const double angleDegRadius = tickRing + 40.0;

    // Degree ticks.
    if (showTicks_) {
        painter.setPen(QPen(theme_.tick, 1.0));
        for (int deg = 0; deg < 360; ++deg) {
            const double angle = angleForLongitude(deg);
            const bool major = (deg % 10 == 0);
            const double inner = major ? (tickRing - 10.0) : (tickRing - 6.0);
            const QPointF p1 = pointOnCircle(center, tickRing, angle);
            const QPointF p2 = pointOnCircle(center, inner, angle);
            painter.drawLine(p1, p2);
        }
    }

    // Sign boundaries + glyphs.
    const auto signs = zodiacSigns();
    const auto signGlyphs = zodiacSignGlyphs();
    
    // Draw element-colored zodiac pie segments
    const QVector<QColor> elementColors = {
        theme_.elementFire,   // 0 = Fire
        theme_.elementEarth,  // 1 = Earth
        theme_.elementAir,    // 2 = Air
        theme_.elementWater,  // 3 = Water
    };
    painter.save();
    painter.setPen(Qt::NoPen);
    for (int s = 0; s < 12; ++s) {
        const int elemIdx = elementIndexForSign(s);
        painter.setBrush(elementColors[elemIdx]);
        // Draw pie segment from zodiacInner to outer
        const double startAngle = angleForLongitude(s * 30.0);
        const double spanAngle = 30.0;
        // Qt uses 1/16th of a degree for arc angles
        const int startAngle16 = static_cast<int>(startAngle * 16.0);
        const int spanAngle16 = static_cast<int>(spanAngle * 16.0);
        // Draw outer arc segment
        QPainterPath path;
        QRectF outerRect(center.x() - outer, center.y() - outer, outer * 2, outer * 2);
        QRectF innerRect(center.x() - zodiacInner, center.y() - zodiacInner, zodiacInner * 2, zodiacInner * 2);
        path.arcMoveTo(outerRect, startAngle);
        path.arcTo(outerRect, startAngle, spanAngle);
        path.arcTo(innerRect, startAngle + spanAngle, -spanAngle);
        path.closeSubpath();
        painter.drawPath(path);
    }
    painter.restore();
    
    // Draw sign boundary lines
    painter.setPen(QPen(theme_.signBoundary, 1.5));
    // Resource paths for zodiac icons
    const QStringList svgPaths = {
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
        const QPointF p1 = pointOnCircle(center, outer, angle);
        const QPointF p2 = pointOnCircle(center, zodiacInner, angle);
        painter.drawLine(p1, p2);

        // Draw sign glyph centered in segment using SVG
        const double midLon = lon + 15.0;
        const QPointF pos = pointOnCircle(center, (outer + zodiacInner) * 0.5, angleForLongitude(midLon));
        
        // Rect for the icon
        QRectF iconRect(pos.x() - 14, pos.y() - 14, 28, 28);
        
        if (s < svgPaths.size()) {
            QSvgRenderer renderer(svgPaths[s]);
            if (renderer.isValid()) {
                // To colorize the SVG (which uses currentColor), we can use a temporary pixmap+mask
                // OR simpler: render to a transparent layer, then use composition mode to fill color
                // But QPainter on widget supports direct composition too?
                // Let's rely on the SVG being simple lines.
                // Creating a pixmap cache is better for performance but direct render is fine for now.
                
                // Colorizing method: 
                // 1. Draw SVG into a pixmap
                // 2. Use CompositionMode_SourceIn to fill with theme color
                // 3. Draw pixmap
                
                // We use a small cache key based on color, size, and index? 
                // For simplicity, let's just create the pixmap on fly. Optimization can come later if needed.
                
                // Better approach with QPainter:
                
                painter.save();
                // Translate so we can draw at 0,0
                painter.translate(iconRect.topLeft());
                
                // We want to color it with theme_.signGlyph
                // Strategy: Render SVG to a QImage/QPixmap that is transparent
                // Then use Painter CompositionMode to tint it.
                
                // Create a pixmap of the size
                QPixmap px(iconRect.size().toSize() * painter.device()->devicePixelRatio());
                px.fill(Qt::transparent);
                px.setDevicePixelRatio(painter.device()->devicePixelRatio());
                
                QPainter p(&px);
                renderer.render(&p);
                p.setCompositionMode(QPainter::CompositionMode_SourceIn);
                p.fillRect(px.rect(), theme_.signGlyph);
                p.end();
                
                painter.drawPixmap(0, 0, px);
                painter.restore();
            }
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
    // Draw angle axes with thicker lines for prominence
    for (int i = 0; i < angleLons.size(); ++i) {
        painter.setPen(QPen(angleColors[i], 2.2));
        const QPointF outerPt = pointOnCircle(center, houseOuter, angleForLongitude(angleLons[i]));
        const QPointF innerPt = pointOnCircle(center, houseInner, angleForLongitude(angleLons[i]));
        painter.drawLine(outerPt, innerPt);
    }

    // Draw house numbers with angular house emphasis
    painter.setFont(smallFont);
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
        painter.drawText(QRectF(pos.x() - houseLabelSize * 0.5, pos.y() - houseLabelSize * 0.5,
                                houseLabelSize, houseLabelSize),
                         Qt::AlignCenter, QString::number(houseNum));
    }

    const bool overlay = (mode_ == Mode::Overlay && hasOverlay_);
    const QString overlayPrefix = overlayLabel_.isEmpty() ? QString("Transit") : overlayLabel_;
    if (overlay) {
        painter.save();
        painter.setPen(QPen(theme_.transitRing, 1.0));
        painter.drawEllipse(center, transitRing, transitRing);
        painter.restore();
    }

    // Aspect lines.
    aspectLines_.clear();
    if (showAspects_) {
        auto shouldIncludeOrb = [this](double orb) {
            return aspectDisplayMaxOrb_ <= 0.0 || orb <= aspectDisplayMaxOrb_;
        };
        auto appendAspectLine = [&](double lonA, double lonB,
                                    const QString& nameA, const QString& nameB,
                                    const QString& label, double orb, double maxOrb,
                                    const QColor& color, Qt::PenStyle style,
                                    double opacityScale, const QString& tooltipPrefix) {
            if (!shouldIncludeOrb(orb)) {
                return;
            }
            const QPointF p1 = pointOnCircle(center, aspectRadius, angleForLongitude(lonA));
            const QPointF p2 = pointOnCircle(center, aspectRadius, angleForLongitude(lonB));
            const QLineF line(p1, p2);
            double strength = 0.5;
            if (maxOrb > 0.0) {
                strength = 1.0 - (orb / maxOrb);
            }
            strength = std::clamp(strength, 0.0, 1.0);
            // Reduced opacity for cleaner look reducing visual noise
            double baseOpacity = 0.15 + (0.55 * strength);
            baseOpacity *= opacityScale;
            const double baseWidth = 0.6 + (1.2 * strength);
            QString tooltip = QString("%1%2 - %3 - %4 (orb %5 deg)")
                .arg(tooltipPrefix)
                .arg(nameA)
                .arg(label)
                .arg(nameB)
                .arg(QString::number(orb, 'f', 2));
            if (maxOrb > 0.0) {
                tooltip += QString(", max %1 deg").arg(QString::number(maxOrb, 'f', 1));
            }
            QPointF labelPos = (p1 + p2) * 0.5;
            const QPointF vec = p2 - p1;
            const double len = std::hypot(vec.x(), vec.y());
            if (len > 12.0) {
                const QPointF perp(-vec.y() / len, vec.x() / len);
                labelPos += perp * (6.0 * fontScale_);
            }
            QRectF symbolRect(labelPos.x() - 8, labelPos.y() - 8, 16, 16);
            aspectLines_.push_back(AspectLineInfo{
                line,
                symbolRect,
                tooltip,
                color,
                baseOpacity,
                baseWidth,
                aspectSymbolForLabel(label),
                style,
            });
        };

        if (overlay) {
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
                        const double diff = angularDiff(t.lon, n.lon);
                        QString label;
                        double orb = 0.0;
                        double maxOrb = 0.0;
                        if (!aspectFor(diff, aspectOrbs_, &label, &orb, &maxOrb)) {
                            continue;
                        }
                        QColor color(theme_.aspectLineNeutral);
                        if (label == "Square" || label == "Opposition") {
                            color = QColor("#e05555");
                        } else if (label == "Trine" || label == "Sextile") {
                            color = QColor("#4aa3ff");
                        }
                        appendAspectLine(t.lon, n.lon, overlayPrefix + " " + t.name, "Natal " + n.name,
                                         label, orb, maxOrb, color, Qt::SolidLine, 1.0, "");
                    }
                }
            }
            if (overlayTransitTransitAspects_) {
                for (int i = 0; i < transitPoints.size(); ++i) {
                    for (int j = i + 1; j < transitPoints.size(); ++j) {
                        const auto& a = transitPoints[i];
                        const auto& b = transitPoints[j];
                        const double diff = angularDiff(a.lon, b.lon);
                        QString label;
                        double orb = 0.0;
                        double maxOrb = 0.0;
                        if (!aspectFor(diff, aspectOrbs_, &label, &orb, &maxOrb)) {
                            continue;
                        }
                        QColor color(theme_.aspectLineTransitTransit);
                        if (label == "Square" || label == "Opposition") {
                            color = QColor("#c76b6b");
                        } else if (label == "Trine" || label == "Sextile") {
                            color = QColor("#6aa0d8");
                        }
                        appendAspectLine(a.lon, b.lon, overlayPrefix + " " + a.name, overlayPrefix + " " + b.name,
                                         label, orb, maxOrb, color, Qt::DashLine, 0.65, "");
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
                        const auto& cell = chart_.aspects.cells[i][j];
                        if (!cell.hasAspect) {
                            continue;
                        }
                        const QString& aName = chart_.aspects.bodyOrder[i];
                        const QString& bName = chart_.aspects.bodyOrder[j];
                        if (!bodyMap.contains(aName) || !bodyMap.contains(bName)) {
                            continue;
                        }
                        QColor color(theme_.aspectLineNatalNatal);
                        if (cell.label == "Square" || cell.label == "Opposition") {
                            color = QColor("#a65b5b");
                        } else if (cell.label == "Trine" || cell.label == "Sextile") {
                            color = QColor("#5f86b3");
                        }
                        appendAspectLine(bodyMap.value(aName), bodyMap.value(bName),
                                         "Natal " + aName, "Natal " + bName,
                                         cell.label, cell.orb, cell.maxOrb, color, Qt::DotLine, 0.45, "");
                    }
                }
            }
        } else {
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
                    const auto& cell = chart_.aspects.cells[i][j];
                    if (!cell.hasAspect) {
                        continue;
                    }
                    const QString& aName = chart_.aspects.bodyOrder[i];
                    const QString& bName = chart_.aspects.bodyOrder[j];
                    if (!bodyMap.contains(aName) || !bodyMap.contains(bName)) {
                        continue;
                    }
                    QColor color(theme_.aspectLineNeutral);
                    if (cell.label == "Square" || cell.label == "Opposition") {
                        color = QColor("#e05555");
                    } else if (cell.label == "Trine" || cell.label == "Sextile") {
                        color = QColor("#4aa3ff");
                    }
                    appendAspectLine(bodyMap.value(aName), bodyMap.value(bName), aName, bName,
                                     cell.label, cell.orb, cell.maxOrb, color, Qt::SolidLine, 1.0, "");
                }
            }
        }

        if (hoveredAspectIndex_ >= aspectLines_.size()) {
            hoveredAspectIndex_ = -1;
        }
        const bool hasHover = hoveredAspectIndex_ >= 0 && hoveredAspectIndex_ < aspectLines_.size();
        QFont aspectFont("Segoe UI Symbol", 8 * fontScale_);
        aspectFont.setBold(true);
        for (int i = 0; i < aspectLines_.size(); ++i) {
            const auto& info = aspectLines_[i];
            const bool isHover = (i == hoveredAspectIndex_);
            double opacity = info.baseOpacity;
            double width = info.baseWidth;
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
            painter.setPen(pen);
            painter.drawLine(info.line);
            painter.restore();

            if (!info.symbol.isEmpty()) {
                painter.save();
                const double symbolOpacity = hasHover && !isHover ? 0.35 : 0.85;
                painter.setOpacity(symbolOpacity);
                painter.setBrush(theme_.aspectSymbolBg);
                painter.setPen(Qt::NoPen);
                painter.drawRoundedRect(info.symbolRect.adjusted(-2, -2, 2, 2), 4, 4);
                painter.setFont(aspectFont);
                painter.setPen(info.color);
                painter.drawText(info.symbolRect, Qt::AlignCenter, info.symbol);
                painter.restore();
            }
        }
    }

    // Planet glyphs + degrees.
    struct BodyDraw {
        QString name;
        double lon;
        bool retrograde;
    };

    planetHitAreas_.clear();
    planetTooltips_.clear();
    QVector<QRectF> occupiedRects;
    occupiedRects.reserve(128);

    QFont degreeFont = smallFont;
    degreeFont.setBold(true);
    degreeFont.setPointSizeF(smallFont.pointSizeF() + 0.5);
    QColor degreeBg = theme_.background;
    degreeBg.setAlpha(220);

    auto placeRadialRect = [&](double angleDeg, double baseRadius, double width, double height) {
        const double step = 6.0 * fontScale_;
        QRectF rect;
        double r = baseRadius;
        for (int attempt = 0; attempt < 6; ++attempt) {
            const QPointF p = pointOnCircle(center, r, angleDeg);
            rect = QRectF(p.x() - width * 0.5, p.y() - height * 0.5, width, height);
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
            r += step;
        }
        return rect;
    };

    // Reserve angle label/degree areas so planet degree labels avoid them.
    for (int i = 0; i < angleLons.size(); ++i) {
        const double angleLon = angleLons[i];
        const QPointF labelPos = pointOnCircle(center, angleLabelRadius, angleForLongitude(angleLon));
        const QRectF labelRect(labelPos.x() - angleLabelWidth * 0.5, labelPos.y() - angleLabelHeight * 0.5,
                               angleLabelWidth, angleLabelHeight);
        occupiedRects.push_back(labelRect.adjusted(-6, -4, 6, 4));
        const QPointF degPos = pointOnCircle(center, angleDegRadius, angleForLongitude(angleLon));
        const QRectF degRect(degPos.x() - angleDegWidth * 0.5, degPos.y() - degHeight * 0.5,
                             angleDegWidth, degHeight);
        occupiedRects.push_back(degRect.adjusted(-6, -4, 6, 4));
    }

    auto drawBodies = [&](const QVector<BodyPosition>& bodies, double baseRadius, const QColor& color, const QString& prefix, bool isTransit) {
        QVector<BodyDraw> drawList;
        drawList.reserve(bodies.size());
        for (const auto& pos : bodies) {
            drawList.push_back({pos.name, pos.longitude, pos.retrograde});
        }
        std::sort(drawList.begin(), drawList.end(), [](const BodyDraw& a, const BodyDraw& b) {
            return a.lon < b.lon;
        });

        QVector<double> labelLons(drawList.size(), 0.0);
        if (!drawList.isEmpty()) {
            for (int i = 0; i < drawList.size(); ++i) {
                labelLons[i] = normalizeDegrees(drawList[i].lon);
            }
        }
        if (drawList.size() > 1) {
            const double glyphSizeLocal = 22.0 * fontScale_;
            const double minSpacingDeg = std::max(2.0, (glyphSizeLocal * 1.15 / baseRadius) * qRadiansToDegrees(1.0));
            const double clusterThreshold = minSpacingDeg * 1.1;

            QVector<double> lons(drawList.size());
            for (int i = 0; i < drawList.size(); ++i) {
                lons[i] = normalizeDegrees(drawList[i].lon);
            }

            QVector<bool> breakAfter(drawList.size(), false);
            for (int i = 0; i < drawList.size() - 1; ++i) {
                if ((lons[i + 1] - lons[i]) > clusterThreshold) {
                    breakAfter[i] = true;
                }
            }
            const double wrapGap = (lons.front() + 360.0) - lons.back();
            if (wrapGap > clusterThreshold) {
                breakAfter[drawList.size() - 1] = true;
            }

            int start = 0;
            for (int i = 0; i < breakAfter.size(); ++i) {
                if (breakAfter[i]) {
                    start = (i + 1) % breakAfter.size();
                    break;
                }
            }

            QVector<QVector<int>> clusters;
            QVector<int> current;
            current.reserve(drawList.size());
            for (int step = 0; step < drawList.size(); ++step) {
                const int idx = (start + step) % drawList.size();
                current.append(idx);
                if (breakAfter[idx] && step < drawList.size() - 1) {
                    clusters.append(current);
                    current.clear();
                }
            }
            if (!current.isEmpty()) {
                clusters.append(current);
            }

            for (const auto& cluster : clusters) {
                const int count = cluster.size();
                if (count <= 1) {
                    continue;
                }

                QVector<double> unwrapped(count);
                double prev = lons[cluster[0]];
                unwrapped[0] = prev;
                for (int k = 1; k < count; ++k) {
                    double lon = lons[cluster[k]];
                    if (lon < prev) {
                        lon += 360.0;
                    }
                    unwrapped[k] = lon;
                    prev = lon;
                }

                QVector<double> placed = unwrapped;
                for (int k = 1; k < count; ++k) {
                    if (placed[k] < placed[k - 1] + minSpacingDeg) {
                        placed[k] = placed[k - 1] + minSpacingDeg;
                    }
                }
                for (int k = count - 2; k >= 0; --k) {
                    if (placed[k] > placed[k + 1] - minSpacingDeg) {
                        placed[k] = placed[k + 1] - minSpacingDeg;
                    }
                }
                double deltaSum = 0.0;
                for (int k = 0; k < count; ++k) {
                    deltaSum += (unwrapped[k] - placed[k]);
                }
                const double delta = deltaSum / static_cast<double>(count);
                for (int k = 0; k < count; ++k) {
                    placed[k] += delta;
                }

                for (int k = 0; k < count; ++k) {
                    labelLons[cluster[k]] = placed[k];
                }
            }
        }

        // Use Segoe UI Symbol for robust astrological glyph support
        QFont planetFont("Segoe UI Symbol");
        planetFont.setPixelSize(static_cast<int>(18 * fontScale_));
        painter.setFont(planetFont);

        auto getPlanetChar = [](const QString& name) -> QChar {
            if (name == "Sun") return QChar(0x2609);
            if (name == "Moon") return QChar(0x263E); // Last Quarter Moon (as per user image)
            if (name == "Mercury") return QChar(0x263F);
            if (name == "Venus") return QChar(0x2640);
            if (name == "Mars") return QChar(0x2642);
            if (name == "Jupiter") return QChar(0x2643);
            if (name == "Saturn") return QChar(0x2644);
            if (name == "Uranus") return QChar(0x26E2); // Modern Uranus (Globe-Cross)
            if (name == "Neptune") return QChar(0x2646);
            if (name == "Pluto") return QChar(0x2647);
            if (name == "Chiron") return QChar(0x26B7);
            if (name == "North Node") return QChar(0x260A); // Ascending Node
            if (name == "South Node") return QChar(0x260B); // Descending Node
            if (name == "Lilith") return QChar(0x26B8);     // Black Moon Lilith
            if (name == "Part of Fortune") return QChar(0x2297); // Circled Times
            return QChar();
        };

        auto planetSvgPath = [](const QString& name) -> QString {
            if (name == "Sun") return ":/resources/icons/planets/sun.svg";
            if (name == "Moon") return ":/resources/icons/planets/moon.svg";
            if (name == "Mercury") return ":/resources/icons/planets/mercury.svg";
            if (name == "Venus") return ":/resources/icons/planets/venus.svg";
            if (name == "Mars") return ":/resources/icons/planets/mars.svg";
            if (name == "Jupiter") return ":/resources/icons/planets/jupiter.svg";
            if (name == "Saturn") return ":/resources/icons/planets/saturn.svg";
            if (name == "Uranus") return ":/resources/icons/planets/uranus.svg";
            if (name == "Neptune") return ":/resources/icons/planets/neptune.svg";
            if (name == "Pluto") return ":/resources/icons/planets/pluto.svg";
            if (name == "Chiron") return ":/resources/icons/planets/chiron.svg";
            if (name == "North Node") return ":/resources/icons/planets/north_node.svg";
            if (name == "South Node") return ":/resources/icons/planets/south_node.svg";
            return "";
        };

        for (int i = 0; i < drawList.size(); ++i) {
            const auto& item = drawList[i];
            const double labelLon = labelLons.value(i, item.lon);
            const double angle = angleForLongitude(normalizeDegrees(labelLon));
            const double r = baseRadius;
            const QPointF pos = pointOnCircle(center, r, angle);
            const QString degLabel = formatDegShort(item.lon);
            
            QString glyph;
            if (item.name == "Vertex") {
                glyph = "Vx";
            } else {
                QChar pChar = getPlanetChar(item.name);
                glyph = pChar.isNull() ? item.name.left(2) : QString(pChar);
            }

            // Define glyphRect for hit areas and potential highlight ring
            const double glyphSize = 22.0 * fontScale_;
            QRectF glyphRect(pos.x() - glyphSize/2, pos.y() - glyphSize/2, glyphSize, glyphSize);
            
            QColor bodyColor = color; // Use the passed color for the body
            if (hasHighlight_ && highlightTransit_ == isTransit && highlightBody_ == item.name) {
                painter.save();
                QPen ringPen(highlightColor_, 2.0);
                painter.setPen(ringPen);
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(glyphRect.adjusted(-4, -4, 4, 4));
                painter.restore();
            }

            // Draw leader tick if label is shifted from the true longitude.
            const double labelDelta = angularDiff(normalizeDegrees(labelLon), normalizeDegrees(item.lon));
            if (labelDelta > 0.25) {
                painter.save();
                QPen tickPen(bodyColor, 1.0);
                tickPen.setCapStyle(Qt::RoundCap);
                painter.setPen(tickPen);
                const QPointF tickInner = pointOnCircle(center, r - 5.0, angleForLongitude(item.lon));
                const QPointF tickOuter = pointOnCircle(center, r + 5.0, angleForLongitude(item.lon));
                painter.drawLine(tickInner, tickOuter);
                painter.restore();
            }

            // Draw SVG icon if available, otherwise fallback to Unicode glyph/text.
            bool drewSvg = false;
            const QString svgPath = planetSvgPath(item.name);
            if (!svgPath.isEmpty()) {
                QSvgRenderer renderer(svgPath);
                if (renderer.isValid()) {
                    painter.save();
                    painter.translate(glyphRect.topLeft());
                    QPixmap px(glyphRect.size().toSize() * painter.device()->devicePixelRatio());
                    px.fill(Qt::transparent);
                    px.setDevicePixelRatio(painter.device()->devicePixelRatio());
                    QPainter p(&px);
                    renderer.render(&p);
                    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    p.fillRect(px.rect(), bodyColor);
                    p.end();
                    painter.drawPixmap(0, 0, px);
                    painter.restore();
                    drewSvg = true;
                }
            }
            if (!drewSvg) {
                painter.setPen(bodyColor);
                painter.drawText(glyphRect, Qt::AlignCenter, glyph);
            }
            occupiedRects.push_back(glyphRect.adjusted(-3, -3, 3, 3));

            // Draw Retrograde indicator if needed
            if (item.retrograde) {
                painter.save();
                QFont retroFont = smallFont;
                retroFont.setPointSizeF(smallFont.pointSizeF() * 0.8); // Smaller font
                retroFont.setBold(true);
                painter.setFont(retroFont);
                painter.setPen(theme_.retrogradeIndicator);
                // Position ℞ to the bottom-right of glyphRect
                QRectF retroRect(glyphRect.right() - 4, glyphRect.bottom() - 6, 10, 10);
                painter.drawText(retroRect, Qt::AlignCenter, QString::fromUtf8(u8"℞"));
                painter.restore();
            }

            if (showDegrees_) {
                painter.save();
                painter.setFont(degreeFont);
                QRectF textRect = placeRadialRect(angle, baseRadius + degreeOffset, degWidth, degHeight);
                const QRectF bgRect = textRect.adjusted(-4, -2, 4, 2);
                painter.setPen(Qt::NoPen);
                painter.setBrush(degreeBg);
                painter.drawRoundedRect(bgRect, 4, 4);
                painter.setPen(bodyColor);
                painter.drawText(textRect, Qt::AlignCenter, degLabel);
                painter.restore();
                occupiedRects.push_back(textRect.adjusted(-2, -2, 2, 2));
                // Restore planet font
                painter.setFont(planetFont);
            }
            const int house = houseForLongitude(item.lon, cusps);
            // Include retrograde status in tooltip
            const QString retroLabel = item.retrograde ? " (R)" : "";
            const QString tooltip = QString("%1%2%3 in %4 %5 (House %6)")
                .arg(prefix)
                .arg(item.name)
                .arg(retroLabel)
                .arg(signName(signIndex(item.lon)))
                .arg(degLabel)
                .arg(house);
            planetHitAreas_.push_back(glyphRect.adjusted(-2, -2, 2, 2));
            planetTooltips_.push_back(tooltip);
        }
    };

    if (overlay) {
        const double natalRadius = planetRadius - 8.0;
        const double transitRadius = transitRing - 2.0;
        drawBodies(chart_.bodies, natalRadius, theme_.natalBody, "Natal ", false);
        drawBodies(overlayChart_.bodies, transitRadius, theme_.transitBody, overlayPrefix + " ", true);
    } else {
        drawBodies(chart_.bodies, planetRadius, theme_.body, "", false);
    }

    // Angle labels + degrees.
    QFont angleLabelFont = angleFont;
    angleLabelFont.setPointSizeF(angleFont.pointSizeF() + 1.0);
    painter.setFont(angleLabelFont);
    const QStringList angleNames = {"AC", "MC", "DC", "IC"};
    for (int i = 0; i < angleNames.size(); ++i) {
        const double angleLon = angleLons[i];
        const QPointF labelPos = pointOnCircle(center, angleLabelRadius, angleForLongitude(angleLon));
        const QRectF labelRect(labelPos.x() - angleLabelWidth * 0.5, labelPos.y() - angleLabelHeight * 0.5,
                                angleLabelWidth, angleLabelHeight);
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(degreeBg);
        painter.drawRoundedRect(labelRect.adjusted(-5, -3, 5, 3), 4, 4);
        painter.setPen(angleColors[i]);
        painter.drawText(labelRect, Qt::AlignCenter, angleNames[i]);
        painter.restore();
        planetHitAreas_.push_back(labelRect.adjusted(-2, -2, 2, 2));
        if (showDegrees_) {
            painter.setFont(degreeFont);
            const QPointF degPos = pointOnCircle(center, angleDegRadius, angleForLongitude(angleLon));
            const QRectF degRect(degPos.x() - angleDegWidth * 0.5, degPos.y() - degHeight * 0.5,
                                  angleDegWidth, degHeight);
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(degreeBg);
            painter.drawRoundedRect(degRect.adjusted(-4, -2, 4, 2), 4, 4);
            painter.setPen(angleColors[i]);
            painter.drawText(degRect, Qt::AlignCenter, formatDegShort(angleLon));
            painter.restore();
            painter.setFont(angleLabelFont);
        }
        const QString tooltip = QString("%1 in %2 %3")
            .arg(angleNames[i])
            .arg(signName(signIndex(angleLon)))
            .arg(formatDegShort(angleLon));
        planetTooltips_.push_back(tooltip);
    }
}

}  // namespace dracoved
