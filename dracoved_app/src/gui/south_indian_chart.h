#pragma once

#include <QColor>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>

namespace dracoved {

// One placement in a South Indian chart box. The caller decides which sign it
// falls in and which degree to print, so the same widget draws D1 and any varga.
struct SouthIndianEntry {
    QString key;          // stable identity, e.g. "Sun", "Rahu (Mean)", "Ascendant"
    QString label;        // short label drawn in the box: "Su", "As", "Ra(M)"
    int sign = -1;        // 0 = Aries .. 11 = Pisces
    double degree = 0.0;  // degree within the sign, as shown for this chart
    bool retrograde = false;
    bool vargottama = false;
    bool lagna = false;
    QString tooltip;
};

// Fixed-sign South Indian chart: Pisces top-left, signs running clockwise
// around the twelve outer boxes, chart details in the 2x2 centre. The Lagna box
// carries the traditional diagonal stroke. Paints with the widget palette, so
// it follows the application theme like the other Vedic panels.
class SouthIndianChart final : public QWidget {
public:
    explicit SouthIndianChart(QWidget* parent = nullptr);

    void setChart(const QString& title, const QStringList& details,
                  const QVector<SouthIndianEntry>& entries);
    void clear(const QString& message);
    void setHighlightedKey(const QString& key);
    QString highlightedKey() const { return highlightedKey_; }
    const QVector<SouthIndianEntry>& entries() const { return entries_; }
    // Box rectangle of a sign in widget coordinates (for tests and hit-testing).
    QRectF signBox(int sign) const;
    // Where an entry was last drawn; empty when not drawn.
    QRectF entryRect(const QString& key) const;

    static QPoint gridCell(int sign);  // (column, row) of a sign in the 4x4 grid

    std::function<void(const QString& key)> onEntryClicked;

    QSize sizeHint() const override { return {320, 320}; }
    QSize minimumSizeHint() const override { return {180, 180}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return width; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF chartSquare() const;
    int entryAt(const QPointF& point) const;

    QString title_;
    QStringList details_;
    QString message_ = "Load a birth chart.";
    QVector<SouthIndianEntry> entries_;
    QVector<QRectF> entryRects_;  // parallel to entries_, filled while painting
    QString highlightedKey_;
    int hovered_ = -1;
};

}  // namespace dracoved
