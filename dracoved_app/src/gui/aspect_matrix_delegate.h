#pragma once

#include <QStyledItemDelegate>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QSize>
#include <QString>

namespace dracoved {

// Cell classification stored in AspectRoles::Kind.
enum AspectCellKind {
    AspectOutside = 0,   // outside the triangle: no box drawn
    AspectEmptyBox = 1,  // valid cell, no aspect
    AspectFilled = 2,    // valid cell with an aspect
    AspectDiagonal = 3,  // diagonal identity cell (body glyph)
};

namespace AspectRoles {
constexpr int Kind = Qt::UserRole + 1;
constexpr int Label = Qt::UserRole + 2;     // aspect label (e.g., "Square")
constexpr int Glyph = Qt::UserRole + 3;     // aspect glyph OR body glyph (diagonal)
constexpr int Orb = Qt::UserRole + 4;       // double, degrees
constexpr int Applying = Qt::UserRole + 5;  // -1 unknown, 0 separating, 1 applying
constexpr int IconPath = Qt::UserRole + 6;  // planetary SVG resource path for diagonal cells
}  // namespace AspectRoles

struct AspectMatrixPalette {
    QColor gridBackground = QColor("#ffffff");  // outside-triangle / behind table
    QColor cellBg = QColor("#faf6ee");          // empty/aspect cell fill
    QColor cellBorder = QColor("#e0d9cc");
    QColor diagonalBg = QColor("#efe7d8");      // diagonal identity cells
    QColor glyphColor = QColor("#3a2e22");      // diagonal body glyph
    QColor textMuted = QColor("#8a8a8a");
    QColor hoverOverlay = QColor(80, 140, 220, 40);
    QHash<QString, QColor> aspectColors;        // label -> color
};

class AspectMatrixDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit AspectMatrixDelegate(QObject* parent = nullptr);

    void setMatrixPalette(const AspectMatrixPalette& palette);
    void setCellSize(const QSize& size);
    void setHoveredCell(int row, int column);
    void clearHover();
    void setFontScale(double scale);
    // Compact cells have no room for the orb text beside the glyph, so
    // tightness is carried by the glyph weight instead. See paint().
    void setCompact(bool compact);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    AspectMatrixPalette palette_;
    QSize cellSize_{42, 36};
    int hoveredRow_ = -1;
    int hoveredCol_ = -1;
    double fontScale_ = 1.0;
    bool compact_ = false;
};

}  // namespace dracoved
