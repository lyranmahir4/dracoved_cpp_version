#include "aspect_matrix_delegate.h"

#include <QPainter>
#include <QStyleOptionViewItem>

#include <algorithm>
#include <cmath>

namespace dracoved {

namespace {

QColor blendOver(const QColor& base, const QColor& over) {
    const double a = over.alphaF();
    return QColor(
        static_cast<int>(std::lround(base.red() * (1.0 - a) + over.red() * a)),
        static_cast<int>(std::lround(base.green() * (1.0 - a) + over.green() * a)),
        static_cast<int>(std::lround(base.blue() * (1.0 - a) + over.blue() * a)));
}

}  // namespace

AspectMatrixDelegate::AspectMatrixDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void AspectMatrixDelegate::setMatrixPalette(const AspectMatrixPalette& palette) {
    palette_ = palette;
}

void AspectMatrixDelegate::setCellSize(const QSize& size) {
    cellSize_ = size;
}

void AspectMatrixDelegate::setHoveredCell(int row, int column) {
    hoveredRow_ = row;
    hoveredCol_ = column;
}

void AspectMatrixDelegate::clearHover() {
    hoveredRow_ = -1;
    hoveredCol_ = -1;
}

void AspectMatrixDelegate::setFontScale(double scale) {
    fontScale_ = std::clamp(scale, 0.7, 1.8);
}

QSize AspectMatrixDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(option);
    Q_UNUSED(index);
    return cellSize_;
}

void AspectMatrixDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect r = option.rect;
    const int kind = index.data(AspectRoles::Kind).toInt();

    // Outside the triangle: paint background only, no box.
    if (kind == AspectOutside) {
        painter->fillRect(r, palette_.gridBackground);
        painter->restore();
        return;
    }

    const bool inHoverCross = (hoveredRow_ >= 0)
        && (index.row() == hoveredRow_ || index.column() == hoveredCol_);

    const QRectF cellRect = QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5);
    const double radius = 5.0;

    QColor fill = palette_.cellBg;
    QColor border = palette_.cellBorder;
    QColor contentColor = palette_.textMuted;
    bool exact = false;

    if (kind == AspectDiagonal) {
        fill = palette_.diagonalBg;
    } else if (kind == AspectFilled) {
        const QString label = index.data(AspectRoles::Label).toString();
        const QColor ac = palette_.aspectColors.value(label, palette_.textMuted);
        QColor tint = ac;
        tint.setAlpha(34);
        fill = blendOver(palette_.cellBg, tint);
        contentColor = ac;
        exact = index.data(AspectRoles::Orb).toDouble() < 1.0;
    }

    // Cell background.
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(cellRect, radius, radius);

    // Border (aspect color + thicker for exact aspects).
    QPen borderPen(border, 1.0);
    if (kind == AspectFilled && exact) {
        borderPen = QPen(contentColor, 1.5);
    }
    painter->setPen(borderPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(cellRect, radius, radius);

    QFont glyphFont("Segoe UI Symbol");
    glyphFont.setPixelSize(static_cast<int>(std::lround(14.0 * fontScale_)));
    QFont numberFont("Segoe UI");
    numberFont.setPixelSize(static_cast<int>(std::lround(9.0 * fontScale_)));

    if (kind == AspectDiagonal) {
        const QString g = index.data(AspectRoles::Glyph).toString();
        QFont f = glyphFont;
        f.setBold(true);
        painter->setFont(f);
        painter->setPen(palette_.glyphColor);
        painter->drawText(cellRect, Qt::AlignCenter, g);
    } else if (kind == AspectFilled) {
        const QString g = index.data(AspectRoles::Glyph).toString();
        const double orb = index.data(AspectRoles::Orb).toDouble();
        const int applying = index.data(AspectRoles::Applying).toInt();
        const int orbDeg = static_cast<int>(std::lround(orb));
        const QString orbText = QString::number(orbDeg) + QString(QChar(0x00B0));

        const QRectF glyphArea(cellRect.left(), cellRect.top(),
                               cellRect.width() * 0.52, cellRect.height());
        const QRectF textArea(cellRect.left() + cellRect.width() * 0.50, cellRect.top(),
                              cellRect.width() * 0.50, cellRect.height());

        QFont gf = glyphFont;
        gf.setBold(true);
        painter->setFont(gf);
        painter->setPen(contentColor);
        painter->drawText(glyphArea, Qt::AlignCenter, g);

        painter->setPen(contentColor);
        if (applying < 0) {
            // No motion info (angles/lots): orb only, vertically centered.
            painter->setFont(numberFont);
            painter->drawText(textArea, Qt::AlignCenter, orbText);
        } else {
            painter->setFont(numberFont);
            const QRectF topRect(textArea.left(), textArea.top() + textArea.height() * 0.06,
                                 textArea.width(), textArea.height() * 0.56);
            painter->drawText(topRect, Qt::AlignHCenter | Qt::AlignBottom, orbText);
            QFont sf = numberFont;
            sf.setPixelSize(static_cast<int>(std::lround(8.0 * fontScale_)));
            painter->setFont(sf);
            const QRectF botRect(textArea.left(), textArea.top() + textArea.height() * 0.50,
                                 textArea.width(), textArea.height() * 0.46);
            painter->drawText(botRect, Qt::AlignHCenter | Qt::AlignTop,
                              applying == 1 ? QStringLiteral("a") : QStringLiteral("s"));
        }
    }

    // Hover cross overlay (row + column + the two diagonal cells of the pair,
    // which fall on the same row/column so they highlight automatically).
    if (inHoverCross) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette_.hoverOverlay);
        painter->drawRoundedRect(cellRect, radius, radius);
    }

    painter->restore();
}

}  // namespace dracoved
