#pragma once

#include <QWidget>
#include <QPen>
#include <QColor>

#include "../core/chart_types.h"

class QContextMenuEvent;

namespace dracoved {

struct ChartWheelTheme {
    QColor background;
    QColor ringOuter;
    QColor ringZodiac;
    QColor ringHouseOuter;
    QColor ringHouseInner;
    QColor placeholderText;
    QColor tick;
    QColor signBoundary;
    QColor signGlyph;
    QColor houseLine;
    QColor houseLabel;
    QColor transitRing;
    QColor aspectSymbolBg;
    QColor aspectLineNeutral;
    QColor aspectLineTransitTransit;
    QColor aspectLineNatalNatal;
    QColor body;
    QColor natalBody;
    QColor transitBody;
    // Element colors for zodiac segments
    QColor elementFire;
    QColor elementEarth;
    QColor elementAir;
    QColor elementWater;
    // Additional visual enhancements
    QColor aspectInnerCircle;
    QColor retrogradeIndicator;
    QColor angularHouseLabel;
};

class ChartWheelWidget : public QWidget {
    Q_OBJECT

public:
    enum class Mode {
        NatalOnly,
        TransitOnly,
        Overlay,
    };

    explicit ChartWheelWidget(QWidget* parent = nullptr);

    void setChart(const NatalChart& chart, HouseSystem system);
    void setTransitChart(const NatalChart& chart, HouseSystem system);
    void setOverlayCharts(const NatalChart& natal, const NatalChart& transit, HouseSystem system, const AspectOrbs& orbs);
    void setOverlayLabel(const QString& label);
    void setOverlayAspectScopes(bool transitNatal, bool transitTransit, bool natalNatal);
    void setAspectDisplayMaxOrb(double maxOrb);
    void setZoom(double zoom);
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void setShowAspects(bool value);
    void setShowTicks(bool value);
    void setShowDegrees(bool value);
    void setFontScale(double scale);
    void setTheme(const ChartWheelTheme& theme);
    bool showAspects() const;
    bool showTicks() const;
    bool showDegrees() const;
    double zoom() const;
    double fontScale() const;
    Mode mode() const;
    double aspectDisplayMaxOrb() const;
    const ChartWheelTheme& theme() const;
    void clearChart();
    void setHighlight(const QString& bodyName, bool transit, const QString& label, const QColor& color);
    void clearHighlight();

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize minimumSizeHint() const override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct AspectLineInfo {
        QLineF line;
        QRectF symbolRect;
        QString tooltip;
        QColor color;
        double baseOpacity = 1.0;
        double baseWidth = 1.0;
        QString symbol;
        Qt::PenStyle style = Qt::SolidLine;
    };

    double angleForLongitude(double lon) const;
    QPointF pointOnCircle(const QPointF& center, double radius, double angleDeg) const;
    QVector<double> buildHouseCusps() const;
    QString formatDegShort(double longitude) const;
    int houseForLongitude(double lon, const QVector<double>& cusps) const;
    double distanceToSegment(const QPointF& point, const QLineF& line) const;
    int hitTestAspect(const QPointF& point) const;
    void updateCursor();

    NatalChart chart_;
    NatalChart overlayChart_;
    HouseSystem houseSystem_ = HouseSystem::WholeSign;
    bool hasChart_ = false;
    bool hasOverlay_ = false;
    Mode mode_ = Mode::NatalOnly;
    AspectOrbs aspectOrbs_ = defaultAspectOrbs();
    bool showAspects_ = true;
    bool showTicks_ = true;
    bool showDegrees_ = true;
    double zoom_ = 1.0;
    double fontScale_ = 1.0;
    double aspectDisplayMaxOrb_ = 0.0;
    bool overlayTransitNatalAspects_ = true;
    bool overlayTransitTransitAspects_ = false;
    bool overlayNatalNatalAspects_ = false;
    QString overlayLabel_ = "Transit";
    ChartWheelTheme theme_;
    QVector<QRectF> planetHitAreas_;
    QVector<QString> planetTooltips_;
    QVector<AspectLineInfo> aspectLines_;
    int hoveredAspectIndex_ = -1;
    QPointF panOffset_ = {0.0, 0.0};
    QPointF lastPanPos_;
    bool panning_ = false;
    bool hasHighlight_ = false;
    bool highlightTransit_ = false;
    QString highlightBody_;
    QString highlightLabel_;
    QColor highlightColor_ = QColor("#f0c24b");
};

}  // namespace dracoved
