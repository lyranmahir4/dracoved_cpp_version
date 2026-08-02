#include "astro_map_widget.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QStringList>
#include <QSvgRenderer>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

constexpr int kTileSize = 256;
constexpr double kMaxMercatorLat = 85.05112878;
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr int kMinZoom = 1;
constexpr int kMaxZoom = 10;
constexpr int kEdgeLabelTop = 52;
constexpr int kEdgeLabelBottom = 42;
constexpr int kEdgeLabelLeft = 48;
constexpr int kEdgeLabelRight = 44;

struct EdgeLabelHit {
    int side = 0;
    double order = 0.0;
    QPointF point;
    QString label;
    QColor color;
    QString symbolResourcePath;
};

QPixmap tintedSvgPixmap(const QString& resourcePath, const QColor& color, int logicalSize) {
    if (resourcePath.isEmpty() || logicalSize <= 0) {
        return {};
    }
    const QString cacheKey = QString("dracoved-map-symbol|%1|%2|%3")
        .arg(resourcePath, color.name(QColor::HexArgb))
        .arg(logicalSize);
    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached)) {
        return cached;
    }

    constexpr int renderScale = 2;
    QPixmap pixmap(logicalSize * renderScale, logicalSize * renderScale);
    pixmap.setDevicePixelRatio(renderScale);
    pixmap.fill(Qt::transparent);
    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid()) {
        return {};
    }
    QPainter iconPainter(&pixmap);
    iconPainter.setRenderHint(QPainter::Antialiasing, true);
    iconPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&iconPainter);
    iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    iconPainter.fillRect(pixmap.rect(), color);
    iconPainter.end();
    QPixmapCache::insert(cacheKey, pixmap);
    return pixmap;
}

int positiveMod(int value, int mod) {
    const int r = value % mod;
    return r < 0 ? r + mod : r;
}

double clampLatitude(double lat) {
    return std::clamp(lat, -kMaxMercatorLat, kMaxMercatorLat);
}

double normalizeLongitude(double lon) {
    double v = std::fmod(lon + 180.0, 360.0);
    if (v < 0.0) {
        v += 360.0;
    }
    return v - 180.0;
}

}  // namespace

AstroMapWidget::AstroMapWidget(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(620, 460);
    updateCursor();
    connect(&net_, &QNetworkAccessManager::finished, this, &AstroMapWidget::handleTileFinished);
}

void AstroMapWidget::setLines(const QVector<AstroMapLine>& lines) {
    lines_ = lines;
    hoveredLine_ = -1;
    update();
}

void AstroMapWidget::clearLines() {
    lines_.clear();
    hoveredLine_ = -1;
    update();
}

void AstroMapWidget::centerOn(double latitude, double longitude, int zoom) {
    centerLat_ = clampLatitude(latitude);
    centerLon_ = normalizeLongitude(longitude);
    if (zoom >= 0) {
        zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
    }
    updateCursor();
    update();
}

void AstroMapWidget::zoomToWorld() {
    centerLat_ = 20.0;
    centerLon_ = 0.0;
    zoom_ = 2;
    updateCursor();
    update();
}

void AstroMapWidget::setHoverInfo(const QString& info) {
    if (hoverInfo_ == info) {
        return;
    }
    hoverInfo_ = info;
    update();
}

void AstroMapWidget::setSelectedLocation(double latitude, double longitude, const QString& label) {
    selectedLonLat_ = {normalizeLongitude(longitude), clampLatitude(latitude)};
    selectedLocationLabel_ = label;
    hasSelectedLocation_ = true;
    update();
}

void AstroMapWidget::clearSelectedLocation() {
    hasSelectedLocation_ = false;
    selectedLocationLabel_.clear();
    update();
}

QPointF AstroMapWidget::lonLatToWorld(double longitude, double latitude) const {
    const double lat = clampLatitude(latitude);
    const double lon = normalizeLongitude(longitude);
    const double scale = static_cast<double>(kTileSize * (1 << zoom_));
    const double x = (lon + 180.0) / 360.0 * scale;
    const double sinLat = std::sin(lat * kPi / 180.0);
    const double y = (0.5 - std::log((1.0 + sinLat) / (1.0 - sinLat)) / (4.0 * kPi)) * scale;
    return {x, y};
}

QPointF AstroMapWidget::worldToLonLat(const QPointF& world) const {
    const double scale = static_cast<double>(kTileSize * (1 << zoom_));
    const double lon = world.x() / scale * 360.0 - 180.0;
    const double n = kPi - 2.0 * kPi * world.y() / scale;
    const double lat = 180.0 / kPi * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
    return {normalizeLongitude(lon), clampLatitude(lat)};
}

QRectF AstroMapWidget::mapRect() const {
    QRectF area = rect().adjusted(kEdgeLabelLeft, kEdgeLabelTop, -kEdgeLabelRight, -kEdgeLabelBottom);
    if (area.width() < 160.0 || area.height() < 140.0) {
        area = rect().adjusted(8, 8, -8, -8);
    }
    return area;
}

QPointF AstroMapWidget::lonLatToScreen(double longitude, double latitude) const {
    const QRectF area = mapRect();
    const QPointF centerWorld = lonLatToWorld(centerLon_, centerLat_);
    const QPointF world = lonLatToWorld(longitude, latitude);
    const double scale = static_cast<double>(kTileSize * (1 << zoom_));
    double dx = world.x() - centerWorld.x();
    if (dx > scale / 2.0) {
        dx -= scale;
    } else if (dx < -scale / 2.0) {
        dx += scale;
    }
    return {area.center().x() + dx, area.center().y() + world.y() - centerWorld.y()};
}

QPointF AstroMapWidget::screenToLonLat(const QPointF& screen) const {
    const QRectF area = mapRect();
    const QPointF centerWorld = lonLatToWorld(centerLon_, centerLat_);
    QPointF world(centerWorld.x() + screen.x() - area.center().x(),
                  centerWorld.y() + screen.y() - area.center().y());
    const double scale = static_cast<double>(kTileSize * (1 << zoom_));
    while (world.x() < 0.0) world.rx() += scale;
    while (world.x() >= scale) world.rx() -= scale;
    world.ry() = std::clamp(world.y(), 0.0, scale);
    return worldToLonLat(world);
}

QString AstroMapWidget::tileKey(int zoom, int x, int y) const {
    return QString("%1/%2/%3").arg(zoom).arg(x).arg(y);
}

QRect AstroMapWidget::selectToolRect() const {
    const QRectF area = mapRect();
    return QRect(static_cast<int>(std::lround(area.left())) + 12,
                 static_cast<int>(std::lround(area.top())) + 12,
                 80, 30);
}

QRect AstroMapWidget::panToolRect() const {
    const QRectF area = mapRect();
    return QRect(static_cast<int>(std::lround(area.left())) + 94,
                 static_cast<int>(std::lround(area.top())) + 12,
                 66, 30);
}

int AstroMapWidget::toolbarHit(const QPointF& point) const {
    const QPoint p = point.toPoint();
    if (selectToolRect().contains(p)) {
        return 0;
    }
    if (panToolRect().contains(p)) {
        return 1;
    }
    return -1;
}

void AstroMapWidget::requestTile(int zoom, int x, int y) {
    const int tileCount = 1 << zoom;
    if (y < 0 || y >= tileCount) {
        return;
    }
    const int wrappedX = positiveMod(x, tileCount);
    const QString key = tileKey(zoom, wrappedX, y);
    if (tileCache_.contains(key) || pendingTiles_.contains(key)) {
        return;
    }
    pendingTiles_.insert(key);
    const QUrl url(QString("https://tile.openstreetmap.org/%1/%2/%3.png").arg(zoom).arg(wrappedX).arg(y));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "DracoVedCpp/0.1 astrocartography map");
    auto* reply = net_.get(request);
    reply->setProperty("tileKey", key);
}

void AstroMapWidget::handleTileFinished(QNetworkReply* reply) {
    if (!reply) {
        return;
    }
    const QString key = reply->property("tileKey").toString();
    pendingTiles_.remove(key);
    if (reply->error() == QNetworkReply::NoError) {
        QPixmap pixmap;
        if (pixmap.loadFromData(reply->readAll())) {
            tileCache_.insert(key, pixmap);
        }
    }
    reply->deleteLater();
    update();
}

void AstroMapWidget::drawModeToolbar(QPainter& painter) const {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect selectRect = selectToolRect();
    const QRect panRect = panToolRect();
    auto drawButton = [&](const QRect& r, bool active, const QString& text) {
        painter.setPen(active ? QColor(38, 94, 176) : QColor(86, 92, 99, 210));
        painter.setBrush(active ? QColor(229, 241, 255, 238) : QColor(255, 255, 255, 228));
        painter.drawRoundedRect(r, 4, 4);
        painter.setPen(active ? QColor(22, 72, 146) : QColor(32, 36, 40));
        painter.drawText(r.adjusted(28, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    };

    drawButton(selectRect, mode_ == InteractionMode::Select, "Select");
    drawButton(panRect, mode_ == InteractionMode::Pan, "Pan");

    QPainterPath cursorIcon;
    cursorIcon.moveTo(selectRect.left() + 10, selectRect.top() + 7);
    cursorIcon.lineTo(selectRect.left() + 10, selectRect.bottom() - 7);
    cursorIcon.lineTo(selectRect.left() + 20, selectRect.bottom() - 13);
    cursorIcon.lineTo(selectRect.left() + 15, selectRect.bottom() - 14);
    cursorIcon.lineTo(selectRect.left() + 20, selectRect.bottom() - 5);
    painter.setPen(QPen(QColor(28, 32, 36), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(28, 32, 36));
    painter.drawPath(cursorIcon);

    const int x = panRect.left() + 12;
    const int y = panRect.top() + 8;
    painter.setPen(QPen(QColor(28, 32, 36), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(x + 2, y + 6, x + 2, y + 16);
    painter.drawLine(x + 6, y + 3, x + 6, y + 16);
    painter.drawLine(x + 10, y + 5, x + 10, y + 16);
    painter.drawLine(x + 14, y + 8, x + 14, y + 16);
    painter.drawLine(x + 2, y + 16, x + 14, y + 16);
    painter.restore();
}

void AstroMapWidget::drawSelectedLocation(QPainter& painter) const {
    if (!hasSelectedLocation_) {
        return;
    }
    const QRectF area = mapRect();
    const QPointF screen = lonLatToScreen(selectedLonLat_.x(), selectedLonLat_.y());
    if (!area.adjusted(-40, -40, 40, 40).contains(screen)) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(255, 255, 255), 4.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(screen + QPointF(-8, 0), screen + QPointF(8, 0));
    painter.drawLine(screen + QPointF(0, -8), screen + QPointF(0, 8));
    painter.setPen(QPen(QColor(224, 68, 45), 2.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(screen + QPointF(-8, 0), screen + QPointF(8, 0));
    painter.drawLine(screen + QPointF(0, -8), screen + QPointF(0, 8));
    painter.setBrush(QColor(224, 68, 45));
    painter.drawEllipse(screen, 3.5, 3.5);

    if (!selectedLocationLabel_.isEmpty()) {
        const QFontMetrics fm(painter.font());
        const QRect labelBounds = fm.boundingRect(selectedLocationLabel_).adjusted(-7, -4, 7, 4);
        QRect bubble(screen.toPoint() + QPoint(12, 10), labelBounds.size());
        if (bubble.right() > area.right() - 8) {
            bubble.moveLeft(static_cast<int>(screen.x()) - bubble.width() - 12);
        }
        if (bubble.bottom() > area.bottom() - 8) {
            bubble.moveTop(static_cast<int>(screen.y()) - bubble.height() - 12);
        }
        painter.setPen(QColor(92, 92, 92, 210));
        painter.setBrush(QColor(255, 255, 255, 235));
        painter.drawRoundedRect(bubble, 4, 4);
        painter.setPen(QColor(28, 32, 36));
        painter.drawText(bubble, Qt::AlignCenter, selectedLocationLabel_);
    }
    painter.restore();
}

void AstroMapWidget::drawLine(QPainter& painter, int lineIndex, bool highlighted) const {
    if (lineIndex < 0 || lineIndex >= lines_.size()) {
        return;
    }
    const auto& line = lines_[lineIndex];
    if (line.lonLatPoints.size() < 2) {
        return;
    }

    auto stroke = [&](const QPen& pen) {
        painter.setPen(pen);
        QPainterPath path;
        bool activePath = false;
        QPointF previousScreen;
        QPointF previousLonLat;
        for (const auto& lonLat : line.lonLatPoints) {
            const QPointF screen = lonLatToScreen(lonLat.x(), lonLat.y());
            if (!activePath) {
                path.moveTo(screen);
                activePath = true;
            } else {
                const bool wrapsDateline = std::fabs(lonLat.x() - previousLonLat.x()) > 180.0;
                const bool jumpsAcrossViewport = std::fabs(screen.x() - previousScreen.x()) > mapRect().width() * 0.55;
                if (wrapsDateline || jumpsAcrossViewport) {
                    painter.drawPath(path);
                    path = QPainterPath();
                    path.moveTo(screen);
                } else {
                    path.lineTo(screen);
                }
            }
            previousScreen = screen;
            previousLonLat = lonLat;
        }
        if (activePath) {
            painter.drawPath(path);
        }
    };

    const double baseWidth = std::max(1.15, line.width);
    if (highlighted) {
        stroke(QPen(QColor(255, 255, 255, 210), baseWidth + 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    QColor color = line.color;
    color.setAlpha(highlighted ? 245 : (line.width < 1.3 ? 132 : 178));
    const double penWidth = highlighted ? baseWidth + 1.35 : baseWidth;
    stroke(QPen(color, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
}

void AstroMapWidget::drawEdgeLabels(QPainter& painter) const {
    const QRectF area = mapRect();
    if (lines_.isEmpty()) {
        return;
    }

    const QLineF boundaries[4] = {
        QLineF(area.topLeft(), area.topRight()),
        QLineF(area.bottomLeft(), area.bottomRight()),
        QLineF(area.topLeft(), area.bottomLeft()),
        QLineF(area.topRight(), area.bottomRight()),
    };

    QVector<EdgeLabelHit> hits;
    hits.reserve(lines_.size() * 2);
    for (int i = 0; i < lines_.size(); ++i) {
        const auto& line = lines_[i];
        if (line.edgeLabel.trimmed().isEmpty() || line.lonLatPoints.size() < 2) {
            continue;
        }
        bool usedSide[4] = {false, false, false, false};
        for (int j = 1; j < line.lonLatPoints.size(); ++j) {
            const QPointF prevLonLat = line.lonLatPoints[j - 1];
            const QPointF lonLat = line.lonLatPoints[j];
            if (std::fabs(lonLat.x() - prevLonLat.x()) > 180.0) {
                continue;
            }
            const QPointF a = lonLatToScreen(prevLonLat.x(), prevLonLat.y());
            const QPointF b = lonLatToScreen(lonLat.x(), lonLat.y());
            if (std::fabs(a.x() - b.x()) > area.width() * 0.55) {
                continue;
            }
            const QLineF segment(a, b);
            for (int side = 0; side < 4; ++side) {
                if (usedSide[side]) {
                    continue;
                }
                QPointF hit;
                if (segment.intersects(boundaries[side], &hit) == QLineF::BoundedIntersection) {
                    EdgeLabelHit entry;
                    entry.side = side;
                    entry.point = hit;
                    entry.order = (side < 2) ? hit.x() : hit.y();
                    entry.label = line.edgeLabel;
                    entry.color = line.color;
                    entry.symbolResourcePath = line.symbolResourcePath;
                    hits.push_back(entry);
                    usedSide[side] = true;
                }
            }
        }
    }
    if (hits.isEmpty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setPointSizeF(std::max(8.0, font.pointSizeF() + 1.0));
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics fm(font);
    const int lineHeight = fm.height();

    constexpr int symbolSize = 17;
    auto labelSize = [&](const EdgeLabelHit& hit) {
        const QStringList rows = hit.label.split('\n');
        int width = 0;
        for (const auto& row : rows) {
            width = std::max(width, fm.horizontalAdvance(row));
        }
        const int symbolSpace = hit.symbolResourcePath.isEmpty() ? 0 : symbolSize + 4;
        return QSize(width + symbolSpace + 8,
                     std::max(static_cast<int>(rows.size()) * lineHeight + 4, symbolSize + 4));
    };

    auto drawOne = [&](const EdgeLabelHit& hit, const QRect& box) {
        QColor textColor = hit.color;
        textColor.setAlpha(255);
        QColor lineColor = hit.color;
        lineColor.setAlpha(185);
        QPointF anchor;
        if (hit.side == 0) {
            anchor = QPointF(box.center().x(), box.bottom());
        } else if (hit.side == 1) {
            anchor = QPointF(box.center().x(), box.top());
        } else if (hit.side == 2) {
            anchor = QPointF(box.right(), box.center().y());
        } else {
            anchor = QPointF(box.left(), box.center().y());
        }
        painter.setPen(QPen(lineColor, 1.0));
        painter.drawLine(hit.point, anchor);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 190));
        painter.drawRoundedRect(box.adjusted(-2, -1, 2, 1), 3, 3);
        QRect textBox = box;
        if (!hit.symbolResourcePath.isEmpty()) {
            const QRect symbolRect(
                box.left() + 3,
                box.center().y() - symbolSize / 2,
                symbolSize,
                symbolSize);
            const QPixmap symbol = tintedSvgPixmap(hit.symbolResourcePath, textColor, symbolSize);
            if (!symbol.isNull()) {
                painter.drawPixmap(symbolRect, symbol);
                textBox.setLeft(symbolRect.right() + 3);
            }
        }
        painter.setPen(textColor);
        painter.drawText(textBox, Qt::AlignCenter, hit.label);
    };

    for (int side = 0; side < 4; ++side) {
        QVector<EdgeLabelHit> sideHits;
        for (const auto& hit : hits) {
            if (hit.side == side) {
                sideHits.push_back(hit);
            }
        }
        std::sort(sideHits.begin(), sideHits.end(), [](const EdgeLabelHit& a, const EdgeLabelHit& b) {
            return a.order < b.order;
        });

        int cursor = 2;
        const int maxHorizontal = width() - 2;
        const int maxVertical = height() - 2;
        for (const auto& hit : sideHits) {
            const QSize size = labelSize(hit);
            QRect box;
            if (side == 0 || side == 1) {
                int left = static_cast<int>(std::lround(hit.point.x() - size.width() * 0.5));
                left = std::clamp(left, 2, std::max(2, width() - size.width() - 2));
                if (left < cursor) {
                    left = cursor;
                }
                if (left + size.width() > maxHorizontal) {
                    continue;
                }
                const int top = (side == 0)
                    ? static_cast<int>(std::lround(area.top())) - size.height() - 5
                    : static_cast<int>(std::lround(area.bottom())) + 5;
                box = QRect(left, top, size.width(), size.height());
                cursor = left + size.width() + 2;
            } else {
                int top = static_cast<int>(std::lround(hit.point.y() - size.height() * 0.5));
                top = std::clamp(top, 2, std::max(2, height() - size.height() - 2));
                if (top < cursor) {
                    top = cursor;
                }
                if (top + size.height() > maxVertical) {
                    continue;
                }
                const int left = (side == 2)
                    ? static_cast<int>(std::lround(area.left())) - size.width() - 5
                    : static_cast<int>(std::lround(area.right())) + 5;
                box = QRect(left, top, size.width(), size.height());
                cursor = top + size.height() + 2;
            }
            drawOne(hit, box);
        }
    }
    painter.restore();
}

void AstroMapWidget::drawHoverLabel(QPainter& painter) const {
    const QRectF area = mapRect();
    QStringList rows;
    const bool hasLine = hoveredLine_ >= 0 && hoveredLine_ < lines_.size() && !lines_[hoveredLine_].label.isEmpty();
    if (hasLine) {
        rows << lines_[hoveredLine_].label;
    }
    if (!hoverInfo_.isEmpty() && mode_ == InteractionMode::Select) {
        rows << hoverInfo_;
    }
    if (rows.isEmpty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setBold(hasLine);
    painter.setFont(font);
    const QFontMetrics fm(font);
    int textWidth = 0;
    for (const auto& row : rows) {
        textWidth = std::max(textWidth, fm.horizontalAdvance(row));
    }
    const int leftPad = hasLine ? 22 : 8;
    const int lineHeight = fm.height();
    const QSize bubbleSize(textWidth + leftPad + 8, rows.size() * lineHeight + 10);
    QPoint topLeft = hoverPos_.toPoint() + QPoint(14, -bubbleSize.height() - 10);
    if (topLeft.x() + bubbleSize.width() > area.right() - 8) {
        topLeft.setX(hoverPos_.toPoint().x() - bubbleSize.width() - 14);
    }
    if (topLeft.y() < area.top() + 8) {
        topLeft.setY(hoverPos_.toPoint().y() + 18);
    }
    const int minX = static_cast<int>(std::lround(area.left())) + 8;
    const int maxX = static_cast<int>(std::lround(area.right())) - bubbleSize.width() - 8;
    const int minY = static_cast<int>(std::lround(area.top())) + 8;
    const int maxY = static_cast<int>(std::lround(area.bottom())) - bubbleSize.height() - 8;
    topLeft.setX(std::clamp(topLeft.x(), minX, std::max(minX, maxX)));
    topLeft.setY(std::clamp(topLeft.y(), minY, std::max(minY, maxY)));

    const QRect bubble(topLeft, bubbleSize);
    painter.setPen(QColor(80, 86, 92, 210));
    painter.setBrush(QColor(255, 255, 255, 238));
    painter.drawRoundedRect(bubble, 4, 4);

    if (hasLine) {
        QColor swatch = lines_[hoveredLine_].color;
        swatch.setAlpha(255);
        const QRect swatchRect(bubble.left() + 7, bubble.top() + 9, 8, 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(swatch);
        painter.drawEllipse(swatchRect);
    }

    painter.setPen(QColor(25, 28, 32));
    int y = bubble.top() + 5 + fm.ascent();
    for (const auto& row : rows) {
        painter.drawText(bubble.left() + leftPad, y, row);
        y += lineHeight;
    }
    painter.restore();
}

void AstroMapWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#f7f7f4"));

    const QRectF area = mapRect();
    const QRect mapArea = area.toAlignedRect();
    painter.fillRect(mapArea, QColor("#dbe7ef"));

    const int tileCount = 1 << zoom_;
    const QPointF centerWorld = lonLatToWorld(centerLon_, centerLat_);
    const QPointF topLeftWorld(centerWorld.x() - area.width() * 0.5,
                               centerWorld.y() - area.height() * 0.5);
    const int minX = static_cast<int>(std::floor(topLeftWorld.x() / kTileSize)) - 1;
    const int maxX = static_cast<int>(std::floor((topLeftWorld.x() + area.width()) / kTileSize)) + 1;
    const int minY = std::max(0, static_cast<int>(std::floor(topLeftWorld.y() / kTileSize)) - 1);
    const int maxY = std::min(tileCount - 1, static_cast<int>(std::floor((topLeftWorld.y() + area.height()) / kTileSize)) + 1);

    painter.save();
    painter.setClipRect(mapArea);
    for (int ty = minY; ty <= maxY; ++ty) {
        for (int tx = minX; tx <= maxX; ++tx) {
            const int wrappedX = positiveMod(tx, tileCount);
            const QString key = tileKey(zoom_, wrappedX, ty);
            const int sx = static_cast<int>(std::lround(area.left() + tx * kTileSize - topLeftWorld.x()));
            const int sy = static_cast<int>(std::lround(area.top() + ty * kTileSize - topLeftWorld.y()));
            const QRect target(sx, sy, kTileSize, kTileSize);
            if (tileCache_.contains(key)) {
                painter.drawPixmap(target, tileCache_.value(key));
            } else {
                requestTile(zoom_, tx, ty);
                painter.fillRect(target, QColor(236, 241, 244));
                painter.setPen(QColor(195, 205, 212));
                painter.drawRect(target.adjusted(0, 0, -1, -1));
            }
        }
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < lines_.size(); ++i) {
        if (i != hoveredLine_) {
            drawLine(painter, i, false);
        }
    }
    if (hoveredLine_ >= 0) {
        drawLine(painter, hoveredLine_, true);
    }
    painter.restore();

    painter.save();
    painter.setPen(QColor(160, 166, 172, 220));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(mapArea.adjusted(0, 0, -1, -1));
    painter.restore();

    drawEdgeLabels(painter);
    drawSelectedLocation(painter);
    drawHoverLabel(painter);
    drawModeToolbar(painter);

    painter.save();
    painter.setPen(QColor(50, 55, 60, 210));
    painter.setBrush(QColor(255, 255, 255, 220));
    const QString status = QString("Zoom %1  |  Map data OpenStreetMap contributors").arg(zoom_);
    const QRect textRect = painter.fontMetrics().boundingRect(status).adjusted(-7, -4, 7, 4);
    const QRect badge(static_cast<int>(std::lround(area.left())) + 10,
                      static_cast<int>(std::lround(area.bottom())) - textRect.height() - 10,
                      textRect.width(), textRect.height());
    painter.drawRoundedRect(badge, 4, 4);
    painter.drawText(badge, Qt::AlignCenter, status);
    painter.restore();
}

void AstroMapWidget::wheelEvent(QWheelEvent* event) {
    const int oldZoom = zoom_;
    zoom_ += event->angleDelta().y() > 0 ? 1 : -1;
    zoom_ = std::clamp(zoom_, kMinZoom, kMaxZoom);
    if (zoom_ != oldZoom) {
        update();
    }
    event->accept();
}

void AstroMapWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton) {
        const int tool = toolbarHit(event->position());
        if (tool == 0 || tool == 1) {
            mode_ = (tool == 0) ? InteractionMode::Select : InteractionMode::Pan;
            hoveredLine_ = -1;
            hoverInfo_.clear();
            emit mapHoverCleared();
            updateCursor();
            update();
            event->accept();
            return;
        }
    }

    const bool startsPan = event->button() == Qt::MiddleButton
        || event->button() == Qt::RightButton
        || (event->button() == Qt::LeftButton && (spacePanActive_ || mode_ == InteractionMode::Pan));
    if (startsPan) {
        panning_ = true;
        panButton_ = event->button();
        lastPanPos_ = event->position();
        hoveredLine_ = -1;
        hoverInfo_.clear();
        emit mapHoverCleared();
        updateCursor();
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && mode_ == InteractionMode::Select) {
        if (!mapRect().contains(event->position())) {
            QWidget::mousePressEvent(event);
            return;
        }
        const QPointF lonLat = screenToLonLat(event->position());
        emit mapClicked(lonLat.y(), lonLat.x());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void AstroMapWidget::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const QPointF delta = event->position() - lastPanPos_;
        lastPanPos_ = event->position();
        QPointF centerWorld = lonLatToWorld(centerLon_, centerLat_);
        centerWorld -= delta;
        const double scale = static_cast<double>(kTileSize * (1 << zoom_));
        while (centerWorld.x() < 0.0) centerWorld.rx() += scale;
        while (centerWorld.x() >= scale) centerWorld.rx() -= scale;
        centerWorld.ry() = std::clamp(centerWorld.y(), 0.0, scale);
        const QPointF lonLat = worldToLonLat(centerWorld);
        centerLon_ = lonLat.x();
        centerLat_ = lonLat.y();
        update();
        event->accept();
        return;
    }

    hoverPos_ = event->position();
    if (toolbarHit(hoverPos_) >= 0 || mode_ == InteractionMode::Pan || !mapRect().contains(hoverPos_)) {
        if (hoveredLine_ != -1 || !hoverInfo_.isEmpty()) {
            hoveredLine_ = -1;
            hoverInfo_.clear();
            emit mapHoverCleared();
            update();
        } else {
            emit mapHoverCleared();
        }
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPointF lonLat = screenToLonLat(hoverPos_);
    emit mapHovered(lonLat.y(), lonLat.x());
    const int idx = hitTestLine(hoverPos_);
    if (idx != hoveredLine_) {
        hoveredLine_ = idx;
    }
    update();
    QWidget::mouseMoveEvent(event);
}

void AstroMapWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_ && event->button() == panButton_) {
        panning_ = false;
        panButton_ = Qt::NoButton;
        updateCursor();
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void AstroMapWidget::leaveEvent(QEvent* event) {
    Q_UNUSED(event);
    if (hoveredLine_ != -1 || !hoverInfo_.isEmpty()) {
        hoveredLine_ = -1;
        hoverInfo_.clear();
        emit mapHoverCleared();
        update();
    } else {
        emit mapHoverCleared();
    }
}

void AstroMapWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spacePanActive_ = true;
        updateCursor();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AstroMapWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spacePanActive_ = false;
        updateCursor();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

double AstroMapWidget::distanceToSegment(const QPointF& point, const QLineF& line) const {
    const QPointF a = line.p1();
    const QPointF b = line.p2();
    const QPointF ab = b - a;
    const QPointF ap = point - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= 0.0001) {
        return std::hypot(ap.x(), ap.y());
    }
    const double t = std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / len2, 0.0, 1.0);
    const QPointF closest = a + ab * t;
    return std::hypot(point.x() - closest.x(), point.y() - closest.y());
}

int AstroMapWidget::hitTestLine(const QPointF& point) const {
    if (!mapRect().adjusted(-6, -6, 6, 6).contains(point)) {
        return -1;
    }
    int bestIndex = -1;
    double bestDistance = 7.0;
    for (int i = 0; i < lines_.size(); ++i) {
        const auto& line = lines_[i];
        for (int j = 1; j < line.lonLatPoints.size(); ++j) {
            const QPointF prevLonLat = line.lonLatPoints[j - 1];
            const QPointF lonLat = line.lonLatPoints[j];
            if (std::fabs(lonLat.x() - prevLonLat.x()) > 180.0) {
                continue;
            }
            const QPointF a = lonLatToScreen(prevLonLat.x(), prevLonLat.y());
            const QPointF b = lonLatToScreen(lonLat.x(), lonLat.y());
            if (std::fabs(a.x() - b.x()) > mapRect().width() * 0.55) {
                continue;
            }
            const double dist = distanceToSegment(point, QLineF(a, b));
            if (dist < bestDistance) {
                bestDistance = dist;
                bestIndex = i;
            }
        }
    }
    return bestIndex;
}

void AstroMapWidget::updateCursor() {
    if (panning_) {
        setCursor(Qt::ClosedHandCursor);
    } else if (mode_ == InteractionMode::Pan || spacePanActive_) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

}  // namespace dracoved
