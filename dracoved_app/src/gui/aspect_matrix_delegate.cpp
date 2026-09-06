#include "aspect_matrix_delegate.h"

#include <QImage>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QSvgRenderer>

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

bool drawTintedSvg(QPainter* painter, const QRectF& target, const QString& resourcePath, const QColor& color) {
    if (!painter || resourcePath.isEmpty() || target.isEmpty()) {
        return false;
    }
    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid()) {
        return false;
    }

    constexpr qreal renderScale = 2.0;
    const QSize pixelSize(
        std::max(1, static_cast<int>(std::ceil(target.width() * renderScale))),
        std::max(1, static_cast<int>(std::ceil(target.height() * renderScale))));
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter iconPainter(&image);
    iconPainter.setRenderHint(QPainter::Antialiasing, true);
    iconPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&iconPainter, QRectF(QPointF(0.0, 0.0), QSizeF(pixelSize)));
    iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    iconPainter.fillRect(image.rect(), color);
    iconPainter.end();
    painter->drawImage(target, image);
    return true;
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

void AspectMatrixDelegate::setCompact(bool compact) {
    compact_ = compact;
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

    // Dense style: square corners everywhere and a single-pixel gutter, so the
    // grid reads as a packed field of tiles with no rounded edges and no spare
    // padding. Text layout is driven by the measured tile, not by a mode flag,
    // so shrinking the cells can never clip the orb text.
    const QRectF cellRect = QRectF(r).adjusted(1.0, 1.0, -1.0, -1.0);
    auto fillCell = [&](const QColor& color) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawRect(cellRect);
    };
    // Transits deliberately stays glyph-only in its narrow column; elsewhere the
    // orb appears only when the tile can actually fit it.
    const bool showOrbText = !compact_ && cellRect.width() >= 26.0;
    const bool showMotion = showOrbText && cellRect.height() >= 22.0;

    QColor fill = palette_.cellBg;
    QColor contentColor = palette_.textMuted;

    if (kind == AspectDiagonal) {
        fill = palette_.diagonalBg;
    } else if (kind == AspectFilled) {
        const QString label = index.data(AspectRoles::Label).toString();
        const QColor ac = palette_.aspectColors.value(label, palette_.textMuted);
        QColor tint = ac;
        // Every cell is borderless now, so the wash alone has to carry it.
        tint.setAlpha(48);
        fill = blendOver(palette_.cellBg, tint);
        contentColor = ac;
    }

    // Cell background.
    fillCell(fill);

    // No per-cell outline at all. Outlining every cell is what made the grid
    // read as a spreadsheet; tight orbs are emphasised with a bold glyph below.

    QFont glyphFont("Segoe UI Symbol");
    glyphFont.setPixelSize(static_cast<int>(std::lround(14.0 * fontScale_)));
    QFont numberFont("Segoe UI");
    numberFont.setPixelSize(static_cast<int>(std::lround(9.0 * fontScale_)));

    // The diagonal carries the body symbol; it is what turns the triangle into
    // a readable pyramid when the grid has no headers.
    if (kind == AspectDiagonal) {
        const QString iconPath = index.data(AspectRoles::IconPath).toString();
        const double iconSize = std::max(1.0, std::min({
            cellRect.width() - 3.0,
            cellRect.height() - 3.0,
            20.0 * fontScale_,
        }));
        const QRectF iconRect(
            cellRect.center().x() - iconSize * 0.5,
            cellRect.center().y() - iconSize * 0.5,
            iconSize,
            iconSize);
        if (!drawTintedSvg(painter, iconRect, iconPath, palette_.glyphColor)) {
            const QString g = index.data(AspectRoles::Glyph).toString();
            QFont f = glyphFont;
            f.setBold(true);
            painter->setFont(f);
            painter->setPen(palette_.glyphColor);
            painter->drawText(cellRect, Qt::AlignCenter, g);
        }
    } else if (kind == AspectFilled) {
        const QString g = index.data(AspectRoles::Glyph).toString();
        const double orb = index.data(AspectRoles::Orb).toDouble();
        const int applying = index.data(AspectRoles::Applying).toInt();

        if (!showOrbText) {
            // Narrow tiles have no room for the orb beside the glyph, so
            // tightness is carried by weight instead: bold means inside 2
            // degrees. The exact orb and the applying or separating state stay
            // on the cell tooltip.
            QFont cf = glyphFont;
            cf.setBold(orb < 2.0);
            painter->setFont(cf);
            painter->setPen(contentColor);
            painter->drawText(cellRect, Qt::AlignCenter, g);
        } else {
            // Bare integer degrees: the degree sign costs horizontal room the
            // tile no longer has, and the unit is unambiguous inside a grid.
            const QString orbText = QString::number(
                static_cast<int>(std::lround(orb)));
            const QRectF glyphArea(cellRect.left(), cellRect.top(),
                                   cellRect.width() * 0.56, cellRect.height());
            const QRectF textArea(cellRect.left() + cellRect.width() * 0.54, cellRect.top(),
                                  cellRect.width() * 0.46, cellRect.height());

            QFont gf = glyphFont;
            gf.setBold(orb < 2.0);
            painter->setFont(gf);
            painter->setPen(contentColor);
            painter->drawText(glyphArea, Qt::AlignCenter, g);

            painter->setFont(numberFont);
            painter->setPen(contentColor);
            if (applying < 0 || !showMotion) {
                // No motion info (angles/lots), or no vertical room for it.
                painter->drawText(textArea, Qt::AlignCenter, orbText);
            } else {
                const QRectF topRect(textArea.left(), textArea.top(),
                                     textArea.width(), textArea.height() * 0.58);
                painter->drawText(topRect, Qt::AlignHCenter | Qt::AlignBottom, orbText);
                QFont sf = numberFont;
                sf.setPixelSize(static_cast<int>(std::lround(8.0 * fontScale_)));
                painter->setFont(sf);
                const QRectF botRect(textArea.left(), textArea.top() + textArea.height() * 0.52,
                                     textArea.width(), textArea.height() * 0.48);
                painter->drawText(botRect, Qt::AlignHCenter | Qt::AlignTop,
                                  applying == 1 ? QStringLiteral("a") : QStringLiteral("s"));
            }
        }
    }

    // Hover cross overlay (row + column + the two diagonal cells of the pair,
    // which fall on the same row/column so they highlight automatically).
    if (inHoverCross) {
        fillCell(palette_.hoverOverlay);
    }

    painter->restore();
}

}  // namespace dracoved
