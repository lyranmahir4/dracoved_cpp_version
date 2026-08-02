#pragma once

#include <QColor>
#include <QHash>
#include <QLineF>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

class QKeyEvent;
class QNetworkReply;
class QPainter;

namespace dracoved {

struct AstroMapLine {
    QString label;
    QString edgeLabel;
    QString symbolResourcePath;
    QColor color;
    QVector<QPointF> lonLatPoints;  // x = longitude, y = latitude
    double width = 1.6;
};

class AstroMapWidget : public QWidget {
    Q_OBJECT

public:
    enum class InteractionMode {
        Select,
        Pan,
    };

    explicit AstroMapWidget(QWidget* parent = nullptr);

    void setLines(const QVector<AstroMapLine>& lines);
    void clearLines();
    void centerOn(double latitude, double longitude, int zoom = -1);
    void zoomToWorld();
    void setHoverInfo(const QString& info);
    void setSelectedLocation(double latitude, double longitude, const QString& label);
    void clearSelectedLocation();

signals:
    void mapClicked(double latitude, double longitude);
    void mapHovered(double latitude, double longitude);
    void mapHoverCleared();

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    QPointF lonLatToWorld(double longitude, double latitude) const;
    QPointF worldToLonLat(const QPointF& world) const;
    QRectF mapRect() const;
    QPointF lonLatToScreen(double longitude, double latitude) const;
    QPointF screenToLonLat(const QPointF& screen) const;
    QString tileKey(int zoom, int x, int y) const;
    QRect selectToolRect() const;
    QRect panToolRect() const;
    int toolbarHit(const QPointF& point) const;
    void requestTile(int zoom, int x, int y);
    void handleTileFinished(QNetworkReply* reply);
    void drawModeToolbar(QPainter& painter) const;
    void drawSelectedLocation(QPainter& painter) const;
    void drawLine(QPainter& painter, int lineIndex, bool highlighted) const;
    void drawEdgeLabels(QPainter& painter) const;
    void drawHoverLabel(QPainter& painter) const;
    double distanceToSegment(const QPointF& point, const QLineF& line) const;
    int hitTestLine(const QPointF& point) const;
    void updateCursor();

    QNetworkAccessManager net_;
    QHash<QString, QPixmap> tileCache_;
    QSet<QString> pendingTiles_;
    QVector<AstroMapLine> lines_;
    double centerLat_ = 20.0;
    double centerLon_ = 0.0;
    int zoom_ = 2;
    QPointF lastPanPos_;
    QPointF hoverPos_;
    QPointF selectedLonLat_;
    QString hoverInfo_;
    QString selectedLocationLabel_;
    Qt::MouseButton panButton_ = Qt::NoButton;
    InteractionMode mode_ = InteractionMode::Select;
    bool panning_ = false;
    bool spacePanActive_ = false;
    bool hasSelectedLocation_ = false;
    int hoveredLine_ = -1;
};

}  // namespace dracoved
