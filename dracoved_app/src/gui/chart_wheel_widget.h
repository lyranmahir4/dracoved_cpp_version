#pragma once

#include <QWidget>
#include <QPen>
#include <QColor>
#include <QSet>
#include <QHash>
#include <QPixmap>
#include <QPainterPath>
#include <QString>

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
    QColor transitLaneBand;  // subtle background for the outer (transit) lane in overlay
    // --- Surface tokens (appended; every one has a paint-time fallback derived
    // from the tokens above, so older brace-initialised theme literals that stop
    // at transitLaneBand still render correctly). ---
    QColor zodiacBandInner = QColor();      // radial-gradient stop nearer the centre
    QColor zodiacBandOuter = QColor();      // radial-gradient stop nearer the rim
    QColor zodiacEdgeHighlight = QColor();  // 1px outer bevel on the zodiac ring
    QColor zodiacEdgeShadow = QColor();     // 1px inner bevel on the zodiac ring
    QColor aspectDiscFill = QColor();       // faint plate behind the aspect web
    QColor wheelHalo = QColor();            // soft lift just outside zodiacOuter
    QColor cardinalBoundary = QColor();     // emphasised 0 Aries/Cancer/Libra/Capricorn strokes
    QColor labelChipBg = QColor();          // rounded chip behind degree/angle text
    QColor labelChipBorder = QColor();
    QColor infoRule = QColor();             // hairline under the info/title block
    QColor dignityStrong = QColor();        // Ruler / Exalt dot (warm)
    QColor dignityWeak = QColor();          // Detriment / Fall dot (cool grey)
};

class ChartWheelWidget : public QWidget {
    Q_OBJECT
    friend struct LunarReturnViewChecks;

public:
    enum class Mode {
        NatalOnly,
        TransitOnly,
        Overlay,
    };
    enum class TickDensity {
        Full,
        Medium,
        Minimal,
    };

    explicit ChartWheelWidget(QWidget* parent = nullptr);

    void setChart(const NatalChart& chart, HouseSystem system);
    void setTransitChart(const NatalChart& chart, HouseSystem system);
    void setOverlayCharts(const NatalChart& natal, const NatalChart& transit, HouseSystem system, const AspectOrbs& orbs);
    void setOverlayLabel(const QString& label);
    // Label for the INNER chart. Defaults to "Natal" and is reset to that by
    // setOverlayCharts, so any caller wanting something else (Synastry uses a
    // person's name) must call this AFTER setOverlayCharts.
    void setBaseLabel(const QString& label);
    // Draw the OUTER chart's angles. Off by default and reset to off by
    // setOverlayCharts. Transits deliberately omit them because a transiting
    // Ascendant moves about a degree every four minutes; synastry needs them
    // because a planet on the partner's Descendant is a primary contact.
    void setOverlayAnglesVisible(bool visible);
    void setChartNote(const QString& note);
    void setOverlayAspectScopes(bool transitNatal, bool transitTransit, bool natalNatal);
    void setAspectDisplayMaxOrb(double maxOrb);
    void setZoom(double zoom);
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void setShowAspects(bool value);
    void setShowTicks(bool value);
    void setShowDegrees(bool value);
    void setShowAspectSymbols(bool value);
    void setShowAsteroids(bool value);
    void setIncludeAsteroidAspects(bool value);
    void setVisibleAsteroids(const QStringList& names);
    void setShowLots(bool value);
    // Part of Fortune is an Arabic Lot by classification, but it is a first-class
    // traditional point in practice, so it gets its own switch rather than being
    // hidden behind the all-or-nothing Lots toggle.
    void setShowPartOfFortune(bool value);
    void setShowDerivedPoints(bool value);
    void setShowFixedStars(bool value);
    void setVisibleFixedStars(const QStringList& names);
    void setHiddenBodies(const QStringList& names);
    void setTickDensity(TickDensity density);
    void setFontScale(double scale);
    void setTheme(const ChartWheelTheme& theme);
    bool showAspects() const;
    bool showTicks() const;
    bool showDegrees() const;
    bool showAspectSymbols() const;
    bool showAsteroids() const;
    bool includeAsteroidAspects() const;
    QStringList visibleAsteroids() const;
    bool showLots() const;
    bool showPartOfFortune() const;
    bool showDerivedPoints() const;
    bool showFixedStars() const;
    QStringList visibleFixedStars() const;
    TickDensity tickDensity() const;
    double zoom() const;
    double fontScale() const;
    Mode mode() const;
    double aspectDisplayMaxOrb() const;
    const ChartWheelTheme& theme() const;
    void clearChart();
    void setHighlight(const QString& bodyName, bool transit, const QString& label, const QColor& color);
    void clearHighlight();
    // Click-to-focus state (the "Focus: X" pin). Cleared via Esc from the main
    // window's application-wide event filter, so the widget exposes it here.
    bool hasFocusBody() const;
    void clearFocusBody();
    // Opt-in aspect selection for chart comparisons; chart changes reset it.
    void setAspectSelectionEnabled(bool enabled);
    void setAspectHighlight(const QString& scopedBodyA, const QString& scopedBodyB);
    bool hasAspectHighlight() const;
    void clearAspectHighlight();

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
        QPainterPath path;
        QRectF symbolRect;
        QString tooltip;
        QColor color;
        double baseOpacity = 1.0;
        double baseWidth = 1.0;
        QString symbol;
        Qt::PenStyle style = Qt::SolidLine;
        QString rawNameA;   // unprefixed body name (kept for diagnostics)
        QString rawNameB;
        // Scope-qualified endpoint names ("Transit Venus" / "Natal Mercury").
        // A raw name matches a body in BOTH overlay lanes, so raw-name matching
        // ringed four glyphs for a two-body aspect.
        QString scopedNameA;
        QString scopedNameB;
        QString aspectName;
    };

    struct PlacedBody {
        QString name;
        double trueLon;
        double displayLon;
        double displayRadius;
        int    radialLayer;      // 0=base, +1=outward, -1=inward
        bool   retrograde;
        bool   lockLongitude = false;
        LunarNodeType lunarNodeType = LunarNodeType::Mean;
        // Pure passthrough for rendering only: no placement arithmetic reads it.
        QString dignity;
    };

    double angleForLongitude(double lon) const;
    QPointF pointOnCircle(const QPointF& center, double radius, double angleDeg) const;
    QVector<double> buildHouseCusps() const;
    QString formatDegShort(double longitude) const;
    int houseForLongitude(double lon, const QVector<double>& cusps) const;
    int hitTestAspect(const QPointF& point) const;
    bool isAspectHighlighted(const AspectLineInfo& aspect) const;
    bool focusActiveNow() const;
    QPixmap coloredSvgPixmap(const QString& path, const QColor& color, const QSize& sizePx, qreal dpr);
    void updateCursor();
    bool isAsteroidVisible(const QString& name) const;
    bool isFixedStarVisible(const QString& name) const;
    bool isBodyHidden(const QString& name) const;
    QVector<PlacedBody> computePlanetPlacements(
        const QVector<BodyPosition>& bodies,
        double baseRadius,
        double minRadius,
        double maxRadius,
        double glyphSize,
        const QPointF& center) const;

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
    bool showAspectSymbols_ = true;
    bool showAsteroids_ = false;
    bool includeAsteroidAspects_ = false;
    QStringList visibleAsteroids_;
    QSet<QString> visibleAsteroidSet_;
    bool showLots_ = true;
    bool showPartOfFortune_ = true;
    bool showDerivedPoints_ = true;
    bool showFixedStars_ = false;
    QStringList visibleFixedStars_;
    QSet<QString> visibleFixedStarSet_;
    // Per-body hide list. Empty means nothing is hidden, so the default
    // rendering is unchanged.
    QSet<QString> hiddenBodySet_;
    TickDensity tickDensity_ = TickDensity::Full;
    double zoom_ = 1.0;
    double fontScale_ = 1.0;
    double aspectDisplayMaxOrb_ = 0.0;
    bool overlayTransitNatalAspects_ = true;
    bool overlayTransitTransitAspects_ = false;
    bool overlayNatalNatalAspects_ = false;
    QString overlayLabel_ = "Transit";
    QString baseLabel_ = "Natal";
    bool overlayAnglesVisible_ = false;
    QString chartNote_;
    QHash<QString, QPixmap> glyphPixmapCache_;
    ChartWheelTheme theme_;
    QVector<QRectF> planetHitAreas_;
    QVector<QString> planetTooltips_;
    QVector<QString> planetHitNames_;   // parallel to planetHitAreas_; empty for non-focusable hits
    // Parallel to planetHitAreas_, but scope-qualified so overlay lanes are
    // distinguishable. Empty wherever planetHitNames_ is empty.
    QVector<QString> planetHitScopedNames_;
    QVector<AspectLineInfo> aspectLines_;
    QRectF aspectSummaryRect_;
    QString aspectSummaryTooltip_;
    int hoveredAspectIndex_ = -1;
    QString hoveredPlanetScope_;        // scoped name of hovered planet ("" = none); drives hover ring + cursor
    QPointF panOffset_ = {0.0, 0.0};
    QPointF lastPanPos_;
    bool panning_ = false;
    QPointF pressPos_;                  // for click-vs-drag detection
    bool leftPressActive_ = false;
    bool hasFocus_ = false;             // click-to-focus: only show aspects touching focusBody_
    QString focusBody_;
    bool aspectSelectionEnabled_ = false;
    QString selectedAspectBodyA_;
    QString selectedAspectBodyB_;
    bool showAspectLegend_ = true;
    bool hasHighlight_ = false;
    bool highlightTransit_ = false;
    QString highlightBody_;
    QString highlightLabel_;
    QColor highlightColor_ = QColor("#f0c24b");
};

}  // namespace dracoved
